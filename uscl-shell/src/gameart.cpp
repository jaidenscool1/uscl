#include "gameart.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QNetworkReply>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QUrl>
#include <QtEndian>

#include <algorithm>
#include <utility>

namespace
{
	const QString libretro = "https://thumbnails.libretro.com/";

	QString artDir() { return QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + "/art/"; }
	QString artFile(const QString& path, const QString& kind)
	{
		return artDir() + QCryptographicHash::hash(path.toUtf8(), QCryptographicHash::Sha1).toHex().left(16) + "." + kind;
	}

	QString libretroSystem(const QString& console)
	{
		static const QMap<QString, QString> systems = {{"Wii", "Nintendo - Wii"}, {"GameCube", "Nintendo - GameCube"},
			{"3DS", "Nintendo - Nintendo 3DS"}, {"WiiU", "Nintendo - Wii U"}, {"Xbox360", "Microsoft - Xbox 360"}};
		return systems.value(console);
	}

	// GameTDB's cover region from the id's region letter
	QStringList regions(QChar r)
	{
		const QString first = r == 'P' ? "EN" : r == 'J' ? "JA" : r == 'K' ? "KO" : "US";
		QStringList out{first};
		for (const QString& more : {QStringLiteral("US"), QStringLiteral("EN")})
			if (!out.contains(more))
				out << more;
		return out;
	}
}

GameArt::GameArt(QObject* parent)
	: QObject(parent)
{
}

QString GameArt::cached(const QString& path, const QString& kind)
{
	const QString file = artFile(path, kind);
	return QFileInfo::exists(file) ? QUrl::fromLocalFile(file).toString() : QString();
}

QString GameArt::gameId(const QString& path, const QString& console)
{
	QFile f(path);
	if (!f.open(QIODevice::ReadOnly))
		return {};
	if (console == "Wii" || console == "GameCube")
	{
		// .wbfs keeps a copy of the disc header at 0x200; .iso/.gcm start with it
		const QByteArray head = f.read(0x206);
		const QString id = QString::fromLatin1(head.startsWith("WBFS") ? head.mid(0x200, 6) : head.left(6));
		static const QRegularExpression ok("^[A-Z0-9]{6}$");
		return ok.match(id).hasMatch() ? id : QString();
	}
	if (console == "3DS")
	{
		// NCSD (.cci/.3ds): partition 0 is the game's NCCH, whose product code is "CTR-P-AMKE"
		const QByteArray ncsd = f.read(0x200);
		if (ncsd.size() < 0x200 || ncsd.mid(0x100, 4) != "NCSD" || !f.seek(qint64(qFromLittleEndian<quint32>(ncsd.constData() + 0x120)) * 0x200))
			return {};
		const QByteArray ncch = f.read(0x200);
		if (ncch.size() < 0x200 || ncch.mid(0x100, 4) != "NCCH")
			return {};
		const QString code = QString::fromLatin1(ncch.mid(0x150, 16)).remove(QChar(0)).section('-', -1);
		static const QRegularExpression ok("^[A-Z0-9]{4}$");
		return ok.match(code).hasMatch() ? code : QString();
	}
	return {};
}

QString GameArt::normName(QString name)
{
	name = name.normalized(QString::NormalizationForm_KD).toLower();
	static const QRegularExpression tags("\\([^)]*\\)|\\[[^\\]]*\\]"), the("\\bthe\\b"), other("[^a-z0-9]");
	return name.remove(tags).remove(the).remove(other);
}

QMap<QString, QString> GameArt::parseListing(const QString& html)
{
	QMap<QString, QString> out;
	static const QRegularExpression href("href=\"([^\"/]+)\\.png\"");
	for (auto it = href.globalMatch(html); it.hasNext();)
	{
		const QString name = QUrl::fromPercentEncoding(it.next().captured(1).toUtf8()).replace("&amp;", "&");
		const QString key = normName(name);
		// several regions share a name: prefer the USA one
		if (!key.isEmpty() && (!out.contains(key) || (name.contains("(USA") && !out[key].contains("(USA"))))
			out[key] = name;
	}
	return out;
}

void GameArt::fetch(const QList<Want>& games)
{
	for (const Want& g : games)
	{
		// PS3 / PS4 games bring their own icon and PIC1; only the other consoles' games come here
		if (g.console == "PS3" || g.console == "PS4")
			continue;
		for (const QString kind : {QStringLiteral("icon"), QStringLiteral("pic")})
		{
			const QString file = artFile(g.path, kind);
			if (QFileInfo::exists(file) || QFileInfo::exists(file + ".none")
			    || std::any_of(m_jobs.cbegin(), m_jobs.cend(), [&](const Job& j) { return j.game.path == g.path && j.kind == kind; }))
				continue;
			Job job{g, kind, {}};
			static const QRegularExpression title_id("^[0-9A-Fa-f]{16}$");
			if (g.console == "Switch" && title_id.match(g.serial).hasMatch())
				job.urls << "https://api.nlib.cc/nx/" + g.serial.toUpper() + (kind == "icon" ? "/icon/256/256" : "/screen/1");
			else if (kind == "icon" && (g.console == "Wii" || g.console == "GameCube" || g.console == "3DS"))
			{
				const QString id = gameId(g.path, g.console);
				if (!id.isEmpty())
					for (const QString& r : regions(id[3]))
						job.urls << (g.console == "3DS" ? "https://art.gametdb.com/3ds/cover/" + r + "/" + id + ".jpg"
						                                : "https://art.gametdb.com/wii/cover/" + r + "/" + id + ".png");
			}
			m_jobs << job;
		}
	}
	next();
}

void GameArt::next()
{
	if (m_busy)
		return;
	if (m_jobs.isEmpty())
	{
		if (std::exchange(m_got, false))
			emit updated();
		return;
	}
	m_busy = true;
	tryNext(m_jobs.takeFirst());
}

QStringList GameArt::libretroUrls(const Job& job)
{
	const QString system = libretroSystem(job.game.console);
	QStringList out;
	for (const QString dir : job.kind == "icon" ? QStringList{"Named_Boxarts"} : QStringList{"Named_Titles", "Named_Snaps"})
	{
		const auto names = m_listings.value(system + "/" + dir);
		for (const QString& want : {normName(QFileInfo(job.game.path).completeBaseName()), normName(job.game.title)})
			if (const QString name = names.value(want); !want.isEmpty() && !name.isEmpty())
			{
				out << libretro + QUrl::toPercentEncoding(system) + "/" + dir + "/" + QUrl::toPercentEncoding(name) + ".png";
				break;
			}
	}
	return out;
}

void GameArt::tryNext(Job job)
{
	const QString system = libretroSystem(job.game.console);
	if (job.urls.isEmpty() && !job.listed && !system.isEmpty())
	{
		// libretro's folder listings (names to match against), from disk when under a week old
		for (const QString dir : job.kind == "icon" ? QStringList{"Named_Boxarts"} : QStringList{"Named_Titles", "Named_Snaps"})
		{
			const QString key = system + "/" + dir;
			if (m_listings.contains(key))
				continue;
			const QString file = artDir() + "listing-" + QCryptographicHash::hash(key.toUtf8(), QCryptographicHash::Sha1).toHex().left(12) + ".html";
			QFile cache(file);
			if (QFileInfo(file).lastModified().daysTo(QDateTime::currentDateTime()) < 7 && cache.open(QIODevice::ReadOnly))
			{
				m_listings[key] = parseListing(QString::fromUtf8(cache.readAll()));
				continue;
			}
			QNetworkRequest req(QUrl(libretro + QUrl::toPercentEncoding(system) + "/" + dir + "/"));
			req.setHeader(QNetworkRequest::UserAgentHeader, "USCL");
			req.setTransferTimeout(30000);
			QNetworkReply* reply = m_net.get(req);
			connect(reply, &QNetworkReply::finished, this, [this, reply, key, file, job] {
				reply->deleteLater();
				const QByteArray html = reply->readAll();
				m_listings[key] = reply->error() == QNetworkReply::NoError ? parseListing(QString::fromUtf8(html)) : QMap<QString, QString>{};
				if (reply->error() == QNetworkReply::NoError && QDir().mkpath(artDir()))
				{
					QFile out(file);
					if (out.open(QIODevice::WriteOnly))
						out.write(html);
				}
				tryNext(job);
			});
			return;
		}
		job.urls = libretroUrls(job);
		job.listed = true;
	}
	if (job.urls.isEmpty())
	{
		// nothing anywhere: remember, so it isn't asked again on every start
		QDir().mkpath(artDir());
		if (QFile marker(artFile(job.game.path, job.kind) + ".none"); !job.netError && !marker.open(QIODevice::WriteOnly))
			qWarning("game art: can't write %s", qPrintable(marker.fileName()));
		m_busy = false;
		next();
		return;
	}
	QNetworkRequest req(QUrl(job.urls.takeFirst()));
	req.setHeader(QNetworkRequest::UserAgentHeader, "USCL");
	req.setTransferTimeout(20000);
	QNetworkReply* reply = m_net.get(req);
	connect(reply, &QNetworkReply::finished, this, [this, reply, job]() mutable {
		reply->deleteLater();
		const QByteArray data = reply->readAll();
		const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
		if (reply->error() == QNetworkReply::NoError && !QImage::fromData(data).isNull() && QDir().mkpath(artDir()))
		{
			QFile out(artFile(job.game.path, job.kind));
			if (out.open(QIODevice::WriteOnly) && out.write(data) == data.size())
			{
				m_got = true;
				m_busy = false;
				next();
				return;
			}
		}
		if (reply->error() != QNetworkReply::NoError && status != 404)
			job.netError = true;
		tryNext(job);
	});
}
