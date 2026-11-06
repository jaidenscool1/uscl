#include "games.h"

#include "gameupdates.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QStorageInfo>
#include <QThread>
#include <QUrl>
#include <QtEndian>

#include <algorithm>

namespace
{
	constexpr qint64 sector = 2048;

	quint32 le32(const char* p) { return qFromLittleEndian<quint32>(p); }
	quint16 le16(const char* p) { return qFromLittleEndian<quint16>(p); }

	// games.yml lives in config/ on Windows; RPCS3's dev_hdd0 sits next to it on Linux and next to rpcs3.exe on Windows
	QString gamesYmlPath()
	{
#ifdef _WIN32
		return GameModel::rpcs3ConfigDir() + "config/games.yml";
#else
		return GameModel::rpcs3ConfigDir() + "games.yml";
#endif
	}

	// PS4 games (run by our shadPS4) are listed apart, so RPCS3 never sees them in its games.yml
	QString ps4YmlPath() { return GameModel::rpcs3ConfigDir() + "uscl/ps4_games.yml"; }
	QString ymlFor(const QString& serial) { return GameModel::isPs4Serial(serial) ? ps4YmlPath() : gamesYmlPath(); }

	// An installed PS4 game: sce_sys/param.sfo next to eboot.bin. "<serial>-UPDATE" folders hold its patch.
	bool isPs4Dir(const QString& dir)
	{
		return QFileInfo(dir + "/sce_sys/param.sfo").isFile() && QFileInfo(dir + "/eboot.bin").isFile()
			&& !dir.endsWith("-UPDATE", Qt::CaseInsensitive);
	}
}

bool GameModel::isPs4Serial(const QString& serial)
{
	return serial.startsWith("CUSA");
}

GameModel::GameModel(QObject* parent)
	: QAbstractListModel(parent)
{
	reload();
}

QString GameModel::rpcs3ConfigDir()
{
#ifdef _WIN32
	return QCoreApplication::applicationDirPath() + '/';
#else
	const QString base = qEnvironmentVariableIsSet("XDG_CONFIG_HOME") ? qEnvironmentVariable("XDG_CONFIG_HOME")
	                                                                    : QDir::homePath() + "/.config";
	return base + "/uscl/";
#endif
}

QMap<QString, QString> GameModel::readSfo(const QByteArray& sfo)
{
	QMap<QString, QString> out;
	if (sfo.size() < 20 || !sfo.startsWith(QByteArray("\0PSF", 4)))
		return out;

	const char* d = sfo.constData();
	const quint32 keys = le32(d + 8), values = le32(d + 12), count = le32(d + 16);
	for (quint32 i = 0; i < count && 20 + (i + 1) * 16 <= quint32(sfo.size()); i++)
	{
		const char* e = d + 20 + i * 16;
		const quint32 key_at = keys + le16(e), fmt = le16(e + 2), len = le32(e + 4), value_at = values + le32(e + 12);
		if (key_at >= quint32(sfo.size()) || value_at + len > quint32(sfo.size()))
			continue;
		const QString key = QString::fromLatin1(d + key_at);
		if (fmt == 0x0204 || fmt == 0x0004) // UTF-8 string (terminated or not)
			out[key] = QString::fromUtf8(d + value_at, len).section(QChar(0), 0, 0);
		else if (fmt == 0x0404 && len >= 4)
			out[key] = QString::number(le32(d + value_at));
	}
	return out;
}

// Reads one file out of an ISO 9660 image (what RPCS3 accepts as a disc image), by path components
QByteArray GameModel::readIsoFile(const QString& iso, const QStringList& path)
{
	QFile f(iso);
	if (!f.open(QIODevice::ReadOnly) || !f.seek(16 * sector))
		return {};

	const QByteArray pvd = f.read(sector);
	if (pvd.size() < 190 || pvd.mid(1, 5) != "CD001")
		return {};

	quint32 lba = le32(pvd.constData() + 156 + 2), size = le32(pvd.constData() + 156 + 10);
	for (int depth = 0; depth < path.size(); depth++)
	{
		if (size > 16 * 1024 * 1024 || !f.seek(qint64(lba) * sector))
			return {};
		const QByteArray dir = f.read(size);
		bool found = false;
		for (qint64 at = 0; at < dir.size();)
		{
			const quint8 len = quint8(dir[at]);
			if (len == 0) // records don't cross sectors: the rest of this one is padding
			{
				at = (at / sector + 1) * sector;
				continue;
			}
			if (at + len > dir.size() || len < 34)
				break;
			const char* r = dir.constData() + at;
			const QString name = QString::fromLatin1(r + 33, quint8(r[32])).section(';', 0, 0);
			if (name.compare(path[depth], Qt::CaseInsensitive) == 0)
			{
				lba = le32(r + 2);
				size = le32(r + 10);
				found = true;
				break;
			}
			at += len;
		}
		if (!found)
			return {};
	}
	if (size > 64 * 1024 * 1024 || !f.seek(qint64(lba) * sector))
		return {};
	return f.read(size);
}

QMap<QString, QString> GameModel::readGamesYml(const QByteArray& yml)
{
	// RPCS3 writes one "SERIAL: path" per line; YAML quotes the path when it needs to
	QMap<QString, QString> out;
	for (const QByteArray& raw : yml.split('\n'))
	{
		const QString line = QString::fromUtf8(raw).trimmed();
		const qsizetype colon = line.indexOf(':');
		if (colon <= 0 || line.startsWith('#'))
			continue;
		QString value = line.mid(colon + 1).trimmed();
		if (value.size() >= 2 && value.startsWith('\'') && value.endsWith('\''))
			value = value.mid(1, value.size() - 2).replace("''", "'");
		else if (value.size() >= 2 && value.startsWith('"') && value.endsWith('"'))
			value = value.mid(1, value.size() - 2).replace("\\\"", "\"").replace("\\\\", "\\");
		if (!value.isEmpty())
			out[line.left(colon).trimmed()] = value;
	}
	return out;
}

QString GameModel::gameHash(const QString& path, const QString& serial)
{
	// shortcut: only EBOOT.BIN, the one file an ISO and a disc folder of the same game always share; add the
	// game's other executables (USRDIR/*.self) if mismatched copies get through
	QByteArray eboot;
	QFile update(rpcs3ConfigDir() + "dev_hdd0/game/" + serial + "/USRDIR/EBOOT.BIN");
	if (QFile ps4(path + "/eboot.bin"); isPs4Dir(path) && ps4.open(QIODevice::ReadOnly))
	{
		eboot = ps4.readAll();
	}
	else if (!serial.isEmpty() && update.open(QIODevice::ReadOnly))
	{
		eboot = update.readAll();
	}
	else if (path.endsWith(".iso", Qt::CaseInsensitive))
	{
		eboot = readIsoFile(path, {"PS3_GAME", "USRDIR", "EBOOT.BIN"});
	}
	else
	{
		const QFileInfo info(path);
		QDir dir(info.isDir() ? path : info.absolutePath());
		for (int up = 0; up < 3 && eboot.isEmpty(); up++)
		{
			for (const QString& sub : {QStringLiteral("PS3_GAME/USRDIR/"), QStringLiteral("USRDIR/"), QString()})
			{
				QFile f(dir.filePath(sub + "EBOOT.BIN"));
				if (f.open(QIODevice::ReadOnly))
				{
					eboot = f.readAll();
					break;
				}
			}
			if (!dir.cdUp())
				break;
		}
	}
	return eboot.isEmpty() ? QString() : QString::fromLatin1(QCryptographicHash::hash(eboot, QCryptographicHash::Sha256).toHex().left(16));
}

bool GameModel::describe(const QString& path, Game& game) const
{
	const QFileInfo info(path);
	QByteArray sfo, icon;
	QString icon_file;

	bool ps4 = false;
	if (isPs4Dir(path))
	{
		QFile f(path + "/sce_sys/param.sfo");
		if (f.open(QIODevice::ReadOnly))
			sfo = f.readAll();
		icon_file = path + "/sce_sys/icon0.png";
		ps4 = true;
	}
	else if (info.isFile() && path.endsWith(".iso", Qt::CaseInsensitive))
	{
		sfo = readIsoFile(path, {"PS3_GAME", "PARAM.SFO"});
		icon = readIsoFile(path, {"PS3_GAME", "ICON0.PNG"});
	}
	else
	{
		// a disc folder (PS3_GAME inside), an installed game folder, or an executable inside one (USRDIR/EBOOT.BIN)
		QDir dir(info.isDir() ? path : info.absolutePath());
		for (int up = 0; up < 3; up++)
		{
			for (const QString& sub : {QStringLiteral("PS3_GAME/"), QString()})
			{
				QFile f(dir.filePath(sub + "PARAM.SFO"));
				if (f.open(QIODevice::ReadOnly))
				{
					sfo = f.readAll();
					icon_file = dir.filePath(sub + "ICON0.PNG");
					break;
				}
			}
			if (!sfo.isEmpty() || !dir.cdUp())
				break;
		}
	}

	const QMap<QString, QString> params = readSfo(sfo);
	if (params.isEmpty())
		return false;

	game.title = params.value("TITLE").simplified();
	game.serial = params.value("TITLE_ID");
	game.path = path;
	game.console = ps4 ? "PS4" : "PS3";
	game.version = params.value("APP_VER");
	if (ps4)
	{
		// shadPS4 runs the patch from "<game>-UPDATE" next to the game
		QFile f(path + "-UPDATE/sce_sys/param.sfo");
		if (const QString update = f.open(QIODevice::ReadOnly) ? readSfo(f.readAll()).value("APP_VER") : QString();
			GameUpdates::newer(update, game.version))
			game.version = update;
	}
	else if (const QString update = GameUpdates::installedVersion(game.serial); GameUpdates::newer(update, game.version))
	{
		game.version = update;
	}

	if (!icon.isEmpty())
	{
		// ISOs: keep the icon in our cache so QML can load it as a file
		const QString cache = QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + "/icons/";
		QDir().mkpath(cache);
		icon_file = cache + QCryptographicHash::hash(path.toUtf8(), QCryptographicHash::Sha1).toHex().left(16) + ".png";
		QFile out(icon_file);
		if (!out.exists() && out.open(QIODevice::WriteOnly))
			out.write(icon);
	}
	if (!icon_file.isEmpty() && QFileInfo::exists(icon_file))
		game.icon = QUrl::fromLocalFile(icon_file).toString();
	return !game.title.isEmpty();
}

void GameModel::reload()
{
	beginResetModel();
	m_games.clear();

	for (const QString& list : {gamesYmlPath(), ps4YmlPath()})
	{
		QFile yml(list);
		if (!yml.open(QIODevice::ReadOnly))
			continue;
		for (const QString& path : readGamesYml(yml.readAll()))
		{
			if (Game g; describe(path, g))
				m_games.push_back(g);
		}
	}

	// installed games (PSN titles, disc game data) that can start on their own
	// shortcut: assumes the default dev_hdd0 location, read vfs.yml if players move it
	const QString hdd_games = rpcs3ConfigDir() + "dev_hdd0/game/";
	for (const QString& sub : QDir(hdd_games).entryList(QDir::Dirs | QDir::NoDotAndDotDot))
	{
		QFile f(hdd_games + sub + "/PARAM.SFO");
		if (!f.open(QIODevice::ReadOnly) || readSfo(f.readAll()).value("CATEGORY") != "HG")
			continue;
		if (Game g; describe(hdd_games + sub, g))
			m_games.push_back(g);
	}

	std::sort(m_games.begin(), m_games.end(), [](const Game& a, const Game& b) {
		return QString::localeAwareCompare(a.title, b.title) < 0;
	});
	endResetModel();
	emit countChanged();
}

int GameModel::rowCount(const QModelIndex& parent) const
{
	return parent.isValid() ? 0 : int(m_games.size());
}

QVariant GameModel::data(const QModelIndex& index, int role) const
{
	if (!index.isValid() || index.row() >= m_games.size())
		return {};
	const Game& g = m_games[index.row()];
	switch (role)
	{
	case TitleRole: return g.title;
	case SerialRole: return g.serial;
	case PathRole: return g.path;
	case IconRole: return g.icon;
	case ConsoleRole: return g.console;
	case VersionRole: return g.version;
	default: return {};
	}
}

QHash<int, QByteArray> GameModel::roleNames() const
{
	return {{TitleRole, "title"}, {SerialRole, "serial"}, {PathRole, "path"}, {IconRole, "icon"}, {ConsoleRole, "console"}, {VersionRole, "version"}};
}

QVariantMap GameModel::get(int row) const
{
	if (row < 0 || row >= m_games.size())
		return {};
	const Game& g = m_games[row];
	return {{"title", g.title}, {"serial", g.serial}, {"path", g.path}, {"icon", g.icon}, {"console", g.console}, {"version", g.version}};
}

namespace
{
	void walk(const QString& dir, int depth, QMap<QString, QString>& out, int& encrypted)
	{
		const auto serialOf = [](const QString& sfo) {
			QFile f(sfo);
			return f.open(QIODevice::ReadOnly) ? GameModel::readSfo(f.readAll()).value("TITLE_ID") : QString();
		};
		// a disc folder, or an installed game: this folder is the game, don't look inside it
		if (isPs4Dir(dir))
		{
			if (const QString serial = serialOf(dir + "/sce_sys/param.sfo"); !serial.isEmpty() && !out.contains(serial))
				out[serial] = dir;
			return;
		}
		for (const QString& sfo : {dir + "/PS3_GAME/PARAM.SFO", dir + "/PARAM.SFO"})
		{
			if (QFileInfo(sfo).isFile() && (sfo.contains("PS3_GAME") || QFileInfo(dir + "/USRDIR").isDir()))
			{
				if (const QString serial = serialOf(sfo); !serial.isEmpty() && !out.contains(serial))
					out[serial] = dir;
				return;
			}
		}
		const QDir d(dir);
		for (const QFileInfo& f : d.entryInfoList({"*.iso", "*.ISO"}, QDir::Files))
		{
			const QByteArray sfo = GameModel::readIsoFile(f.absoluteFilePath(), {"PS3_GAME", "PARAM.SFO"});
			const QString serial = GameModel::readSfo(sfo).value("TITLE_ID");
			if (!serial.isEmpty() && !out.contains(serial))
				out[serial] = f.absoluteFilePath();
			else if (serial.isEmpty() && !sfo.isEmpty())
				encrypted++; // the file is listed but its contents are scrambled: a disc image that isn't decrypted
		}
		if (depth > 0)
		{
			for (const QFileInfo& sub : d.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::NoSymLinks))
				walk(sub.absoluteFilePath(), depth - 1, out, encrypted);
		}
	}
}

QMap<QString, QString> GameModel::findGames(const QString& path, int* encrypted)
{
	QMap<QString, QString> out;
	int scrambled = 0;
	walk(QDir::cleanPath(path), 6, out, scrambled);
	if (encrypted)
		*encrypted = scrambled;
	return out;
}

QString GameModel::ymlLine(const QString& serial, const QString& path)
{
	QString p = path;
	return serial + ": '" + p.replace("'", "''") + "'";
}

void GameModel::scanFolder(const QString& path)
{
	if (m_scanning)
		return;
	m_scanning = true;
	emit scanningChanged();
	auto* found = new QMap<QString, QString>;
	auto* encrypted = new int(0);
	QThread* t = QThread::create([found, encrypted, path] { *found = findGames(path, encrypted); });
	connect(t, &QThread::finished, this, [this, t, found, encrypted] {
		t->deleteLater();
		int added = 0, listed = 0;
		for (const QString& list : {gamesYmlPath(), ps4YmlPath()})
		{
			QFile yml(list);
			QByteArray text;
			if (yml.open(QIODevice::ReadOnly))
				text = yml.readAll();
			yml.close();
			const QMap<QString, QString> known = readGamesYml(text);
			int new_here = 0;
			for (auto it = found->cbegin(); it != found->cend(); ++it)
			{
				if (ymlFor(it.key()) != list)
					continue;
				if (known.contains(it.key()))
				{
					listed++;
					continue;
				}
				if (!text.isEmpty() && !text.endsWith('\n'))
					text += '\n';
				text += ymlLine(it.key(), it.value()).toUtf8() + '\n';
				new_here++;
			}
			if (new_here)
			{
				QDir().mkpath(QFileInfo(list).absolutePath());
				if (yml.open(QIODevice::WriteOnly | QIODevice::Truncate))
					yml.write(text);
				yml.close(); // flush before reload() reads it back
			}
			added += new_here;
		}
		delete found;
		const int scrambled = *encrypted;
		delete encrypted;
		if (added)
			reload();
		m_scanning = false;
		emit scanningChanged();
		emit scanFinished(added, listed, scrambled);
	});
	t->start();
}

QVariantList GameModel::roots()
{
	QVariantList out{QVariantMap{{"name", tr("Home")}, {"path", QDir::homePath()}}};
	for (const QStorageInfo& v : QStorageInfo::mountedVolumes())
	{
		const QString root = v.rootPath();
#ifndef _WIN32
		// real drives only: removable media and extra disks, not system mounts
		if (!(root.startsWith("/run/media/") || root.startsWith("/media/") || root.startsWith("/mnt/")))
			continue;
#endif
		if (v.isValid() && v.isReady())
			out << QVariantMap{{"name", v.displayName().isEmpty() ? root : v.displayName() + "  (" + root + ")"}, {"path", root}};
	}
	return out;
}

QVariantList GameModel::folders(const QString& path)
{
	QVariantList out;
	const QDir d(path);
	for (const QFileInfo& f : d.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name | QDir::IgnoreCase))
		out << QVariantMap{{"name", f.fileName()}, {"path", f.absoluteFilePath()}};
	return out;
}

void GameModel::remove(const QString& serial)
{
	QFile yml(ymlFor(serial));
	if (!yml.open(QIODevice::ReadOnly))
		return;
	QByteArrayList kept;
	for (const QByteArray& line : yml.readAll().split('\n'))
	{
		if (!line.startsWith(serial.toUtf8() + ":"))
			kept << line;
	}
	yml.close();
	if (yml.open(QIODevice::WriteOnly | QIODevice::Truncate))
		yml.write(kept.join('\n'));
	yml.close();
	reload();
}
