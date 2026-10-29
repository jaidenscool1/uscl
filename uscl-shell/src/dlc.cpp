#include "dlc.h"
#include "games.h"
#include "launcher.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QMap>
#include <QRegularExpression>
#include <QtEndian>

namespace
{
	QString hdd0() { return GameModel::rpcs3ConfigDir() + "dev_hdd0/"; }

	QByteArray head(const QString& path)
	{
		QFile f(path);
		return f.open(QIODevice::ReadOnly) ? f.read(0x80) : QByteArray();
	}

	// RPCS3 reads licenses from the signed-in user's exdata; USCL always uses user 00000001
	QString exdata() { return hdd0() + "home/00000001/exdata/"; }
}

Dlc::Dlc(QObject* parent)
	: QObject(parent)
{
	connect(&m_install, &QProcess::stateChanged, this, &Dlc::busyChanged);
}

QString Dlc::contentId(const QByteArray& h)
{
	// NPD (.edat/.sdat): ID at 0x10; PKG: magic 7F 50 4B 47, ID at 0x30. Both 36 chars like UP0002-BLUS31011_00-NAME.
	int at = -1;
	if (h.startsWith(QByteArray("NPD\0", 4)))
		at = 0x10;
	else if (h.startsWith("\x7FPKG"))
		at = 0x30;
	if (at < 0 || h.size() < at + 36)
		return {};
	const QString id = QString::fromLatin1(h.mid(at, 36));
	static const QRegularExpression re("^[A-Z]{2}\\d{4}-[A-Z]{4}\\d{5}_\\d{2}-[A-Z0-9_]{16}$");
	return re.match(id).hasMatch() ? id : QString();
}

int Dlc::licenseType(const QByteArray& h)
{
	return h.startsWith(QByteArray("NPD\0", 4)) && h.size() >= 12 ? qFromBigEndian<quint32>(h.constData() + 8) : 0;
}

QVariantList Dlc::list(const QString& serial)
{
	struct item
	{
		bool content = false, needs_license = false, licensed = false;
	};
	QMap<QString, item> items;
	// content: the game's folder (disc games' DLC lands there too) and exdata (unlock keys)
	for (const QString& dir : {hdd0() + "game/" + serial, exdata()})
	{
		QDirIterator it(dir, {"*.edat", "*.EDAT", "*.sdat", "*.SDAT"}, QDir::Files, QDirIterator::Subdirectories);
		while (it.hasNext())
		{
			const QByteArray h = head(it.next());
			const QString id = contentId(h);
			if (id.isEmpty() || (!dir.contains("/game/") && !id.contains(serial)))
				continue;
			item& i = items[id];
			i.content = true;
			const int type = licenseType(h);
			i.needs_license = i.needs_license || type == 1 || type == 2;
		}
	}
	for (const QFileInfo& rap : QDir(exdata()).entryInfoList({"*.rap", "*.RAP"}, QDir::Files))
	{
		const QString id = rap.completeBaseName();
		if (id.contains(serial))
			items[id].licensed = true;
	}
	QVariantList out;
	for (auto it = items.cbegin(); it != items.cend(); ++it)
	{
		out << QVariantMap{{"id", it.key()}, {"name", it.key().mid(20)}, {"content", it->content},
			{"licensed", it->licensed || (it->content && !it->needs_license)}};
	}
	return out;
}

QVariantList Dlc::files(const QString& path)
{
	QVariantList out;
	for (const QFileInfo& f : QDir(path).entryInfoList({"*.pkg", "*.PKG", "*.rap", "*.RAP"}, QDir::Files, QDir::Name))
		out << QVariantMap{{"name", f.fileName()}, {"path", f.absoluteFilePath()}};
	return out;
}

void Dlc::install(const QString& path)
{
	if (busy())
		return;
	if (path.endsWith(".rap", Qt::CaseInsensitive))
	{
		// a license: 16 bytes, named after its content ID
		const QFileInfo rap(path);
		static const QRegularExpression id("^[A-Z]{2}\\d{4}-[A-Z]{4}\\d{5}_\\d{2}-[A-Z0-9_]{16}$");
		if (rap.size() != 16 || !id.match(rap.completeBaseName()).hasMatch())
		{
			emit finished(false, tr("%1 isn't a license file (.rap named after its content ID).").arg(rap.fileName()));
			return;
		}
		QDir().mkpath(exdata());
		const QString to = exdata() + rap.completeBaseName() + ".rap";
		QFile::remove(to);
		const bool ok = QFile::copy(path, to);
		emit finished(ok, ok ? tr("License %1 installed.").arg(rap.completeBaseName()) : tr("Couldn't copy %1.").arg(rap.fileName()));
		return;
	}
	const QString id = contentId(head(path));
	if (id.isEmpty())
	{
		emit finished(false, tr("%1 isn't a PS3 package.").arg(QFileInfo(path).fileName()));
		return;
	}
	const QString serial = id.mid(7, 9);
	const auto withContent = [serial] {
		int n = 0;
		for (const QVariant& v : list(serial))
			n += v.toMap().value("content").toBool();
		return n;
	};
	const int before = withContent();
	m_install.disconnect(this);
	connect(&m_install, &QProcess::stateChanged, this, &Dlc::busyChanged);
	connect(&m_install, &QProcess::finished, this, [this, id, before, withContent] {
		// trust what got installed, not RPCS3's exit code: new content shows up in the game's list
		const bool ok = withContent() > before;
		emit finished(ok, ok ? tr("%1 installed.").arg(id) : tr("%1 didn't add any content.").arg(id));
	});
	m_install.start(Launcher::corePath(), {"--headless", "--installpkg", path});
	if (!m_install.waitForStarted(5000))
		emit finished(false, tr("Couldn't start the PS3 emulator to install it."));
}
