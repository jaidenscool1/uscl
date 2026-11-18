#include "games.h"

#include "gameart.h"
#include "gameupdates.h"
#include "switchcore.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QStorageInfo>
#include <QSaveFile>
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
	: QAbstractListModel(parent), m_art(new GameArt(this))
{
	connect(m_art, &GameArt::updated, this, &GameModel::reload); // downloaded icons / previews show up
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
	if (const QVariantMap entry = SwitchCore::describe(path); !entry.isEmpty())
	{
		game = {entry["title"].toString(), entry["serial"].toString(), entry["path"].toString(), {}, "Switch", {}};
		return true;
	}
	const QFileInfo info(path);
	QByteArray sfo, icon;
	QString icon_file, pic_file;
	const QString cache = QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + "/icons/";
	const QString cache_name = cache + QCryptographicHash::hash(path.toUtf8(), QCryptographicHash::Sha1).toHex().left(16);

	bool ps4 = false;
	if (isPs4Dir(path))
	{
		QFile f(path + "/sce_sys/param.sfo");
		if (f.open(QIODevice::ReadOnly))
			sfo = f.readAll();
		icon_file = path + "/sce_sys/icon0.png";
		pic_file = QFileInfo::exists(path + "/sce_sys/pic1.png") ? path + "/sce_sys/pic1.png" : path + "/sce_sys/pic0.png";
		ps4 = true;
	}
	else if (info.isFile() && path.endsWith(".iso", Qt::CaseInsensitive))
	{
		sfo = readIsoFile(path, {"PS3_GAME", "PARAM.SFO"});
		icon = readIsoFile(path, {"PS3_GAME", "ICON0.PNG"});
		// the background picture: read out of the ISO once, then from the cache
		pic_file = cache_name + "-pic1.png";
		if (!QFileInfo::exists(pic_file) && QDir().mkpath(cache))
			if (const QByteArray pic = readIsoFile(path, {"PS3_GAME", "PIC1.PNG"}); !pic.isEmpty())
			{
				QFile out(pic_file);
				if (out.open(QIODevice::WriteOnly))
					out.write(pic);
			}
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
					pic_file = dir.filePath(sub + "PIC1.PNG");
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
		QDir().mkpath(cache);
		icon_file = cache_name + ".png";
		QFile out(icon_file);
		if (!out.exists() && out.open(QIODevice::WriteOnly))
			out.write(icon);
	}
	if (!icon_file.isEmpty() && QFileInfo::exists(icon_file))
		game.icon = QUrl::fromLocalFile(icon_file).toString();
	if (!pic_file.isEmpty() && QFileInfo::exists(pic_file))
		game.pic = QUrl::fromLocalFile(pic_file).toString();
	return !game.title.isEmpty();
}

void GameModel::reload()
{
	beginResetModel();
	m_games.clear();
	for (const QString& path : SwitchCore::library())
	{
		if (Game g; describe(path, g))
			m_games.push_back(g);
	}

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

	loadEmuGames();

	// games with no icon or preview of their own: art downloaded earlier, and fetch what's still missing
	QList<GameArt::Want> missing;
	for (Game& g : m_games)
	{
		if (g.icon.isEmpty())
			g.icon = GameArt::cached(g.path, "icon");
		if (g.pic.isEmpty())
			g.pic = GameArt::cached(g.path, "pic");
		if (g.icon.isEmpty() || g.pic.isEmpty())
			missing << GameArt::Want{g.path, g.title, g.console, g.serial};
	}

	std::sort(m_games.begin(), m_games.end(), [](const Game& a, const Game& b) {
		return QString::localeAwareCompare(a.title, b.title) < 0;
	});
	endResetModel();
	emit countChanged();
	m_art->fetch(missing);
}

// Wii/3DS games aren't in RPCS3's games.yml; USCL keeps them in emu_games.json next to games.yml
// (written by the folder scan or by hand): [{title, console:"Wii"|"3DS", path, serial, version?, icon?}].
// Purely additive -- if the file is missing or malformed, nothing changes.
void GameModel::loadEmuGames()
{
	QFile f(rpcs3ConfigDir() + "emu_games.json");
	if (!f.open(QIODevice::ReadOnly))
		return;
	const QJsonArray arr = QJsonDocument::fromJson(f.readAll()).array();
	for (const QJsonValue& v : arr)
	{
		const QJsonObject o = v.toObject();
		Game g;
		g.title = o.value("title").toString();
		g.console = o.value("console").toString();
		g.path = o.value("path").toString();
		g.serial = o.value("serial").toString();
		g.icon = o.value("icon").toString();   // usually empty -> the home screen uses its default game icon
		g.version = o.value("version").toString();
		if (!g.title.isEmpty() && !g.path.isEmpty()
		    && (g.console == "Wii" || g.console == "GameCube" || g.console == "WiiU" || g.console == "3DS" || g.console == "DS"
		        || g.console == "Xbox360")
		    && QFileInfo::exists(g.path))
			m_games.push_back(g);
	}
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
	case PicRole: return g.pic;
	default: return {};
	}
}

QHash<int, QByteArray> GameModel::roleNames() const
{
	return {{TitleRole, "title"}, {SerialRole, "serial"}, {PathRole, "path"}, {IconRole, "icon"}, {ConsoleRole, "console"}, {VersionRole, "version"}, {PicRole, "pic"}};
}

QVariantMap GameModel::get(int row) const
{
	if (row < 0 || row >= m_games.size())
		return {};
	const Game& g = m_games[row];
	return {{"title", g.title}, {"serial", g.serial}, {"path", g.path}, {"icon", g.icon}, {"console", g.console}, {"version", g.version}, {"pic", g.pic}};
}

namespace
{
	void walk(const QString& dir, int depth, QMap<QString, QString>& out, int& encrypted, int& archives)
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
		for (const QFileInfo& file : d.entryInfoList(QDir::Files))
		{
			// games still packed in an archive (each multi-part .rar once; Switch keys / firmware zips are found elsewhere)
			static const QRegularExpression packed("\\.(rar|7z|zip)$", QRegularExpression::CaseInsensitiveOption),
			                                later_part("\\.part0*([2-9]|\\d{2,})\\.rar$|\\.r\\d\\d$", QRegularExpression::CaseInsensitiveOption),
			                                not_game("irmware|keys", QRegularExpression::CaseInsensitiveOption);
			if (packed.match(file.fileName()).hasMatch())
			{
				if (!later_part.match(file.fileName()).hasMatch() && !not_game.match(file.fileName()).hasMatch())
					archives++;
				continue;
			}
			const auto entry = SwitchCore::describe(file.absoluteFilePath());
			if (!entry.isEmpty())
				out[entry["serial"].toString()] = file.absoluteFilePath();
			else if (file.suffix().compare("iso", Qt::CaseInsensitive) != 0)   // ISOs: below, after the PS3 check
				if (const QVariantMap emu = GameModel::emuGame(file.absoluteFilePath()); !emu.isEmpty() && !out.contains(emu["serial"].toString()))
					out[emu["serial"].toString()] = file.absoluteFilePath();
		}
		for (const QFileInfo& f : d.entryInfoList({"*.iso", "*.ISO"}, QDir::Files))
		{
			const QByteArray sfo = GameModel::readIsoFile(f.absoluteFilePath(), {"PS3_GAME", "PARAM.SFO"});
			const QString serial = GameModel::readSfo(sfo).value("TITLE_ID");
			if (!serial.isEmpty() && !out.contains(serial))
				out[serial] = f.absoluteFilePath();
			else if (serial.isEmpty() && !sfo.isEmpty())
				encrypted++; // the file is listed but its contents are scrambled: a disc image that isn't decrypted
			else if (sfo.isEmpty())   // not a PS3 disc: Wii, GameCube or Xbox 360
				if (const QVariantMap emu = GameModel::emuGame(f.absoluteFilePath()); !emu.isEmpty() && !out.contains(emu["serial"].toString()))
					out[emu["serial"].toString()] = f.absoluteFilePath();
		}
		if (depth > 0)
		{
			for (const QFileInfo& sub : d.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::NoSymLinks))
				walk(sub.absoluteFilePath(), depth - 1, out, encrypted, archives);
		}
	}
}

// ---- Wii, GameCube, 3DS, DS, Wii U and Xbox 360 games (they run in Dolphin, Azahar, melonDS, Cemu, Xenia)

namespace
{
	QByteArray readAt(QFile& f, qint64 at, int n)
	{
		return at + n <= f.size() && f.seek(at) ? f.read(n) : QByteArray();
	}

	quint32 be32(const QByteArray& b, int at)
	{
		return b.size() >= at + 4 ? qFromBigEndian<quint32>(b.constData() + at) : 0;
	}

	// a file name without dump tags: "Wii Sports (USA) [!]" -> "Wii Sports"
	QString cleanTitle(QString name)
	{
		static const QRegularExpression tags("\\s*[\\[(][^\\])]*[\\])]"), dash("\\s+-\\s+");
		return name.remove(tags).replace('_', ' ').replace(dash, ": ").simplified();
	}

	QString idPart(QString s)
	{
		static const QRegularExpression other("[^A-Za-z0-9]");
		return s.remove(other).left(16);
	}

	// a Wii / GameCube disc header (any container keeps a copy): console, game id, internal name
	bool discHeader(const QByteArray& h, QString& console, QString& id, QString& title)
	{
		const bool wii = be32(h, 0x18) == 0x5D1C9EA3, gc = be32(h, 0x1C) == 0xC2339F3D;
		if (!wii && !gc)
			return false;
		console = wii ? "Wii" : "GameCube";
		id = QString::fromLatin1(h.left(6));
		if (const QString name = QString::fromLatin1(h.mid(0x20, 0x40).split('\0').first()).simplified(); !name.isEmpty())
			title = name;
		return true;
	}
}

bool GameModel::isEmuSerial(const QString& serial)
{
	for (const char* c : {"Wii-", "GameCube-", "3DS-", "DS-", "WiiU-", "Xbox360-"})
		if (serial.startsWith(c))
			return true;
	return false;
}

QVariantMap GameModel::emuGame(const QString& path)
{
	const QFileInfo info(path);
	const QString ext = info.suffix().toLower();
	static const QStringList known = {"iso", "gcm", "wbfs", "ciso", "rvz", "wia", "gcz", "3ds", "cci", "cxi", "3dsx", "nds",
	                                  "wua", "wud", "wux", "rpx", "xex"};
	if (!known.contains(ext))
		return {};
	QFile f(path);
	if (!f.open(QIODevice::ReadOnly))
		return {};
	QString console, id, title = cleanTitle(info.completeBaseName());
	if (ext == "iso" || ext == "gcm")
		discHeader(readAt(f, 0, 0x60), console, id, title);
	else if (ext == "wbfs" && readAt(f, 0, 4) == "WBFS")
		discHeader(readAt(f, 0x200, 0x60), console, id, title);
	else if (ext == "ciso" && readAt(f, 0, 4) == "CISO")
		discHeader(readAt(f, 0x8000, 0x60), console, id, title);
	else if ((ext == "rvz" || ext == "wia") && (readAt(f, 0, 4) == QByteArray("RVZ\x01", 4) || readAt(f, 0, 4) == QByteArray("WIA\x01", 4)))
		discHeader(readAt(f, 0x58, 0x60), console, id, title);   // the disc header copy in the WIA/RVZ disc struct
	else if (ext == "gcz" && readAt(f, 0, 4) == QByteArray("\x01\xC0\x0B\xB1", 4))
		console = qFromLittleEndian<quint32>(readAt(f, 4, 4).constData()) == 1 ? "Wii" : "GameCube";
	else if ((ext == "3ds" || ext == "cci") && readAt(f, 0x100, 4) == "NCSD")
	{
		console = "3DS";   // product code of the game partition, e.g. CTR-P-AMKE
		const QByteArray at = readAt(f, 0x120, 4);
		if (at.size() == 4)
			id = QString::fromLatin1(readAt(f, qint64(qFromLittleEndian<quint32>(at.constData())) * 0x200 + 0x150, 16).split('\0').first());
	}
	else if (ext == "cxi" && readAt(f, 0x100, 4) == "NCCH")
		console = "3DS", id = QString::fromLatin1(readAt(f, 0x150, 16).split('\0').first());
	else if (ext == "3dsx" && readAt(f, 0, 4) == "3DSX")
		console = "3DS";
	else if (ext == "nds" && readAt(f, 0x15C, 2) == QByteArray("\x56\xCF", 2))   // the header's Nintendo logo checksum
		console = "DS", id = QString::fromLatin1(readAt(f, 0xC, 4));
	else if (ext == "wua" || ext == "wud" || ext == "wux")
		console = "WiiU";
	else if (ext == "rpx")
	{
		// an extracted Wii U game: code/<name>.rpx beside meta/meta.xml (updates and DLC have one too: skipped)
		QFile meta(info.absolutePath() + "/../meta/meta.xml");
		if (!meta.open(QIODevice::ReadOnly) || meta.size() > 1024 * 1024)
			return {};
		const QString xml = QString::fromUtf8(meta.readAll());
		static const QRegularExpression name("<longname_en[^>]*>([^<]+)</longname_en>"), tid("<title_id[^>]*>([0-9A-Fa-f]{16})</title_id>");
		id = tid.match(xml).captured(1).toUpper();
		if (id.startsWith("0005000E") || id.startsWith("0005000C"))
			return {};
		console = "WiiU";
		if (const QString n = name.match(xml).captured(1).simplified(); !n.isEmpty())
			title = n;
		else
			title = cleanTitle(QDir(info.absolutePath() + "/..").dirName());
	}
	else if (ext == "xex" && info.fileName().compare("default.xex", Qt::CaseInsensitive) == 0)
		console = "Xbox360", title = cleanTitle(info.dir().dirName());
	if (console.isEmpty() && ext == "iso")
	{
		// Xbox 360: the XDVDFS signature, at the start of the game partition of each disc layout
		for (const qint64 base : {qint64(0), qint64(0x2080000), qint64(0xFD90000), qint64(0x18300000)})
			if (readAt(f, base + 0x10000, 20) == "MICROSOFT*XBOX*MEDIA")
			{
				console = "Xbox360";
				break;
			}
	}
	if (console.isEmpty())
		return {};
	const QString key = idPart(id.isEmpty() ? info.completeBaseName() : id);
	return {{"title", title.isEmpty() ? info.completeBaseName() : title}, {"console", console},
	        {"serial", console + '-' + (key.isEmpty() ? QString("game") : key)}, {"path", info.absoluteFilePath()}};
}

void GameModel::setupSwitchFiles(const QString& extraFolder)
{
	auto* result = new QVariantMap;
	QThread* t = QThread::create([result, extraFolder] {
		const QVariantMap found = SwitchCore::findConsoleFiles(extraFolder.isEmpty() ? QStringList() : QStringList{extraFolder});
		QString error;
		*result = {{"installed", SwitchCore::installConsoleFiles(found, &error)}, {"tooNew", found.value("skipped")}, {"error", error}};
	});
	connect(t, &QThread::finished, this, [this, t, result] {
		t->deleteLater();
		emit switchFilesDone(result->value("installed").toString(), result->value("tooNew").toStringList(), result->value("error").toString());
		delete result;
	});
	t->start();
}

QMap<QString, QString> GameModel::findGames(const QString& path, int* encrypted, int* archives)
{
	QMap<QString, QString> out;
	int scrambled = 0, packed = 0;
	walk(QDir::cleanPath(path), 8, out, scrambled, packed);
	if (encrypted)
		*encrypted = scrambled;
	if (archives)
		*archives = packed;
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
	auto* archives = new int(0);
	QThread* t = QThread::create([found, encrypted, archives, path] { *found = findGames(path, encrypted, archives); });
	connect(t, &QThread::finished, this, [this, t, found, encrypted, archives, scanned = path] {
		t->deleteLater();
		int added = 0, listed = 0;
		QString problem;
		auto switch_games = SwitchCore::library();
		int switch_added = 0;
		for (auto it = found->cbegin(); it != found->cend(); ++it)
		{
			if (!SwitchCore::isSerial(it.key()))
				continue;
			if (switch_games.contains(it.key()) && QFileInfo::exists(switch_games.value(it.key())))
				listed++;
			else
			{
				switch_games[it.key()] = it.value();
				switch_added++;
			}
		}
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
				if (SwitchCore::isSerial(it.key()) || isEmuSerial(it.key()) || ymlFor(it.key()) != list)
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
				QSaveFile output(list);
				if (!output.open(QIODevice::WriteOnly) || output.write(text) != text.size() || !output.commit())
				{
					new_here = 0;
					problem += tr(" Couldn't save the %1 game library.").arg(list == ps4YmlPath() ? "PS4" : "PS3");
				}
			}
			added += new_here;
		}
		const int scrambled = *encrypted, packed = *archives;
		delete encrypted;
		delete archives;
		// Wii, GameCube, 3DS, DS, Wii U, Xbox 360: emu_games.json (a game already listed by path or serial stays)
		int emu_added = 0;
		{
			QFile f(rpcs3ConfigDir() + "emu_games.json");
			QJsonArray list = f.open(QIODevice::ReadOnly) ? QJsonDocument::fromJson(f.readAll()).array() : QJsonArray();
			f.close();
			QSet<QString> paths, serials;
			for (const QJsonValue& v : list)
				paths << QFileInfo(v["path"].toString()).absoluteFilePath(), serials << v["serial"].toString();
			for (auto it = found->cbegin(); it != found->cend(); ++it)
			{
				if (!isEmuSerial(it.key()))
					continue;
				if (paths.contains(QFileInfo(it.value()).absoluteFilePath()) || serials.contains(it.key()))
				{
					listed++;
					continue;
				}
				const QVariantMap g = emuGame(it.value());
				static const QMap<QString, QString> emu = {{"Wii", "dolphin"}, {"GameCube", "dolphin"}, {"3DS", "azahar"},
				                                           {"DS", "melonds"}, {"WiiU", "cemu"}, {"Xbox360", "xenia"}};
				list.append(QJsonObject{{"title", g["title"].toString()}, {"console", g["console"].toString()}, {"path", g["path"].toString()},
				                        {"serial", g["serial"].toString()}, {"emu", emu.value(g["console"].toString())}});
				emu_added++;
			}
			QSaveFile out(rpcs3ConfigDir() + "emu_games.json");
			const QByteArray json = QJsonDocument(list).toJson();
			if (emu_added && (!QDir().mkpath(rpcs3ConfigDir()) || !out.open(QIODevice::WriteOnly) || out.write(json) != json.size() || !out.commit()))
			{
				emu_added = 0;
				problem += tr(" Couldn't save the game library.");
			}
		}
		delete found;
		if (switch_added && !SwitchCore::saveLibrary(switch_games))
		{
			switch_added = 0;
			problem += tr(" Couldn't save the Switch game library.");
		}
		reload();
		m_scanning = false;
		emit scanningChanged();
		emit scanFinished(added + switch_added + emu_added, listed, scrambled, packed);
		if (!problem.isEmpty())
			emit scanFailed(problem.trimmed());
		setupSwitchFiles(scanned);
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
	if (SwitchCore::isSerial(serial))
	{
		auto entries = SwitchCore::library();
		entries.remove(serial);
		if (!SwitchCore::saveLibrary(entries))
			emit scanFailed(tr("Couldn't save the Switch game library."));
		reload();
		return;
	}
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
