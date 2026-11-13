#include "gameupdates.h"

#include "games.h"
#include "launcher.h"

#include <QCryptographicHash>
#include <QDir>
#include <QNetworkReply>
#include <QSslError>
#include <QStandardPaths>
#include <QXmlStreamReader>

namespace
{
	// Sony's update lists. Its certificate is from Sony's own authority, which PCs don't trust; only this host's
	// errors are ignored. shortcut: someone who can tamper with the connection could offer another package (each is
	// checked against the list's SHA-1, which comes over the same connection); pin Sony's certificate if that matters
	const QString list_host = "a0.ww.np.dl.playstation.net";
}

GameUpdates::GameUpdates(QObject* parent)
	: QObject(parent)
{
}

QVariantMap GameUpdates::available() const
{
	QVariantMap out;
	for (auto it = m_available.cbegin(); it != m_available.cend(); ++it)
	{
		QVariantList list;
		for (const Package& p : it.value())
			list << QVariantMap{{"version", p.version}, {"size", p.size}};
		out[it.key()] = list;
	}
	return out;
}

QList<GameUpdates::Package> GameUpdates::parseList(const QByteArray& xml)
{
	QList<Package> out;
	QXmlStreamReader r(xml);
	while (!r.atEnd() && !r.hasError())
	{
		if (r.readNext() != QXmlStreamReader::StartElement || r.name() != QLatin1String("package"))
			continue;
		const auto a = r.attributes();
		Package p{a.value("version").toString(), a.value("url").toString(), a.value("sha1sum").toString().toLower(),
		          a.value("size").toLongLong()};
		if (!p.version.isEmpty() && p.url.startsWith("http") && p.sha1.size() == 40)
			out << p;
	}
	std::sort(out.begin(), out.end(), [](const Package& a, const Package& b) { return newer(b.version, a.version); });
	return out;
}

bool GameUpdates::newer(const QString& a, const QString& b)
{
	return a.toDouble() > b.toDouble();
}

QByteArray GameUpdates::packageSha1(QIODevice& pkg, qint64 size)
{
	QCryptographicHash h(QCryptographicHash::Sha1);
	for (qint64 left = size - 32; left > 0;)
	{
		const QByteArray chunk = pkg.read(qMin<qint64>(left, 1 << 20));
		if (chunk.isEmpty())
			return {};
		h.addData(chunk);
		left -= chunk.size();
	}
	return h.result().toHex();
}

QString GameUpdates::installedVersion(const QString& serial)
{
	QFile f(GameModel::rpcs3ConfigDir() + "dev_hdd0/game/" + serial + "/PARAM.SFO");
	return f.open(QIODevice::ReadOnly) ? GameModel::readSfo(f.readAll()).value("APP_VER") : QString();
}

void GameUpdates::check(const QVariantList& games)
{
	const bool idle = m_to_check.isEmpty();
	for (const QVariant& g : games)
	{
		const QVariantMap m = g.toMap();
		const QString serial = m.value("serial").toString();
		// serials are 4 letters + 5 digits; anything else isn't a PS3 title Sony lists
		if (serial.size() == 9 && serial.left(4).toUpper() == serial.left(4))
			m_to_check << qMakePair(serial, m.value("version").toString());
	}
	emit changed();
	if (idle)
		checkNext();
}

void GameUpdates::checkNext()
{
	if (m_to_check.isEmpty())
	{
		emit changed();
		return;
	}
	const auto [serial, version] = m_to_check.first();
	QNetworkRequest req(QUrl(QString("https://%1/tpl/np/%2/%2-ver.xml").arg(list_host, serial)));
	req.setTransferTimeout(15000);
	QNetworkReply* reply = m_net.get(req);
	connect(reply, &QNetworkReply::sslErrors, reply, [reply](const QList<QSslError>&) { reply->ignoreSslErrors(); });
	connect(reply, &QNetworkReply::finished, this, [this, reply, serial, version] {
		reply->deleteLater();
		QList<Package> pending;
		// no list (no updates, or unreachable) leaves the game as it is
		if (reply->error() == QNetworkReply::NoError)
		{
			for (const Package& p : parseList(reply->readAll()))
			{
				if (newer(p.version, version))
					pending << p;
			}
		}
		if (pending.isEmpty())
			m_available.remove(serial);
		else
			m_available[serial] = pending;
		m_to_check.removeFirst();
		checkNext();
	});
}

void GameUpdates::setProgress(double progress, const QString& status)
{
	m_progress = progress;
	m_status = status;
	emit progressChanged();
}

void GameUpdates::install(const QStringList& serials)
{
	if (m_busy)
		return;
	m_queue.clear();
	for (const QString& serial : serials)
	{
		for (const Package& p : m_available.value(serial))
			m_queue << qMakePair(serial, p);
	}
	if (m_queue.isEmpty())
		return;
	m_busy = true;
	m_total = int(m_queue.size());
	m_done = 0;
	m_problem.clear();
	installNext();
}

void GameUpdates::fail(const QString& problem)
{
	m_problem = problem;
	// the rest of that game's updates build on this one: skip them, carry on with other games
	const QString serial = m_queue.first().first;
	m_queue.removeIf([&](const auto& q) { return q.first == serial; });
	installNext();
}

void GameUpdates::installNext()
{
	if (m_queue.isEmpty())
	{
		m_busy = false;
		setProgress(1, {});
		emit changed();
		emit finished(m_done, m_problem);
		return;
	}
	const auto [serial, p] = m_queue.first();
	const QString dir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + "/updates";
	QDir().mkpath(dir);
	m_file.setFileName(dir + "/" + serial + "-" + p.version + ".pkg");
	if (!m_file.open(QIODevice::WriteOnly | QIODevice::Truncate))
	{
		fail(tr("Couldn't save the download."));
		return;
	}
	const QString step = tr("Update %1 of %2: %3 %4").arg(m_done + 1).arg(m_total).arg(serial, p.version);
	setProgress(double(m_done) / m_total, step);
	QNetworkReply* reply = m_net.get(QNetworkRequest(QUrl(p.url)));
	connect(reply, &QNetworkReply::readyRead, this, [this, reply] { m_file.write(reply->readAll()); });
	connect(reply, &QNetworkReply::downloadProgress, this, [this, step](qint64 got, qint64 total) {
		if (total > 0)
			setProgress((m_done + 0.8 * got / total) / m_total, step);
	});
	connect(reply, &QNetworkReply::finished, this, [this, reply, serial, p, step] {
		reply->deleteLater();
		m_file.write(reply->readAll());
		m_file.close();
		if (reply->error() != QNetworkReply::NoError)
		{
			m_file.remove();
			fail(tr("Couldn't download %1 %2 from Sony.").arg(serial, p.version));
			return;
		}
		if (!m_file.open(QIODevice::ReadOnly) || packageSha1(m_file, m_file.size()) != p.sha1.toLatin1())
		{
			m_file.close();
			m_file.remove();
			fail(tr("The download of %1 %2 was damaged.").arg(serial, p.version));
			return;
		}
		m_file.close();
		setProgress((m_done + 0.85) / m_total, tr("Installing %1 %2...").arg(serial, p.version));
		m_install.disconnect(this);
		connect(&m_install, &QProcess::finished, this, [this, serial, p] {
			m_file.remove();
			// like the system software: trust what got installed, not RPCS3's exit code
			if (newer(p.version, installedVersion(serial)))
			{
				fail(tr("%1 %2 didn't install.").arg(serial, p.version));
				return;
			}
			m_done++;
			QList<Package>& left = m_available[serial];
			left.removeIf([&](const Package& q) { return !newer(q.version, p.version); });
			if (left.isEmpty())
				m_available.remove(serial);
			emit changed();
			m_queue.removeFirst();
			installNext();
		});
		m_install.start(Launcher::corePath(), {"--headless", "--installpkg", m_file.fileName()});
		if (!m_install.waitForStarted(5000))
			fail(tr("Couldn't start the PS3 emulator to install it."));
	});
}
