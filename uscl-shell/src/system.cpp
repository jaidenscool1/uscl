#include "system.h"
#include "games.h"
#include "launcher.h"

#include <QDir>
#include <QFileInfo>
#include <QNetworkReply>
#include <QRegularExpression>
#include <QStandardPaths>

#include <algorithm>

namespace
{
	// Sony's list of the current PS3 system software, US region
	constexpr auto update_list = "http://fus01.ps3.update.playstation.net/update/ps3/list/us/ps3-updatelist.txt";
}

QString System::onlineName() const
{
	// RPCS3's rpcn.yml: "NPID: name"; RPCS3 makes up "RPCS3_..." until the player picks one
	QFile f(GameModel::rpcs3ConfigDir() +
#ifdef _WIN32
		"config/"
#endif
		"rpcn.yml");
	if (!f.open(QIODevice::ReadOnly))
		return {};
	for (const QByteArray& line : f.readAll().split('\n'))
	{
		if (line.startsWith("NPID:"))
		{
			const QString name = QString::fromUtf8(line.mid(5)).trimmed();
			return name.startsWith("RPCS3_") ? QString() : name;
		}
	}
	return {};
}

bool System::firmwareInstalled() const
{
	// what RPCS3's firmware installer puts in place first
	return QFileInfo::exists(GameModel::rpcs3ConfigDir() + "dev_flash/vsh/module/vsh.self");
}

namespace
{
	void findPups(const QString& dir, int depth, QFileInfoList& found)
	{
		const QFileInfo pup(dir + "/PS3UPDAT.PUP");
		if (pup.isFile() && !found.contains(pup))
			found << pup;
		if (depth > 0)
		{
			for (const QString& sub : QDir(dir).entryList(QDir::Dirs | QDir::NoDotAndDotDot))
				findPups(dir + "/" + sub, depth - 1, found);
		}
	}
}

QStringList System::findFirmware() const
{
	const QString home = QDir::homePath();
	QFileInfoList found;
	findPups(home + "/Downloads", 4, found);
	findPups(home + "/Desktop", 2, found);
	findPups(home, 1, found);
	std::sort(found.begin(), found.end(), [](const QFileInfo& a, const QFileInfo& b) { return a.lastModified() > b.lastModified(); });
	QStringList out;
	for (const QFileInfo& f : found)
		out << f.absoluteFilePath();
	return out;
}

void System::setFirmware(bool busy, double progress, const QString& status)
{
	m_busy = busy;
	m_progress = progress;
	m_status = status;
	emit firmwareChanged();
}

void System::installFirmware(const QString& pup)
{
	if (m_busy && m_install.state() != QProcess::NotRunning)
		return;
	setFirmware(true, 0, tr("Installing the system software..."));
	m_install.disconnect(this);
	connect(&m_install, &QProcess::finished, this, [this] {
		// RPCS3 can crash while shutting down after a headless install, so check the result, not the exit code
		emit changed();
		setFirmware(false, 1, firmwareInstalled() ? tr("The system software is installed.") : tr("The install didn't finish. Try again, or use another PS3UPDAT.PUP."));
	});
	m_install.start(Launcher::corePath(), {"--headless", "--installfw", pup});
	if (!m_install.waitForStarted(5000))
		setFirmware(false, 0, tr("Couldn't start the PS3 emulator to install it."));
}

QString System::cdnUrl(const QByteArray& updatelist)
{
	static const QRegularExpression re("CDN=(https?://[^;]+/PS3UPDAT\\.PUP);");
	return re.match(QString::fromLatin1(updatelist)).captured(1);
}

void System::downloadFirmware()
{
	if (m_busy)
		return;
	setFirmware(true, 0, tr("Finding the latest system software..."));
	QNetworkReply* list = m_net.get(QNetworkRequest(QUrl(update_list)));
	connect(list, &QNetworkReply::finished, this, [this, list] {
		list->deleteLater();
		const QString url = cdnUrl(list->readAll());
		if (list->error() != QNetworkReply::NoError || url.isEmpty())
		{
			setFirmware(false, 0, tr("Couldn't reach Sony's update server. Check the internet connection."));
			return;
		}
		const QString dir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
		QDir().mkpath(dir);
		m_download.setFileName(dir + "/PS3UPDAT.PUP.part");
		if (!m_download.open(QIODevice::WriteOnly | QIODevice::Truncate))
		{
			setFirmware(false, 0, tr("Couldn't save the download."));
			return;
		}
		setFirmware(true, 0, tr("Downloading the system software from Sony..."));
		QNetworkReply* pup = m_net.get(QNetworkRequest(QUrl(url)));
		connect(pup, &QNetworkReply::readyRead, this, [this, pup] { m_download.write(pup->readAll()); });
		connect(pup, &QNetworkReply::downloadProgress, this, [this](qint64 got, qint64 total) {
			if (total > 0)
				setFirmware(true, double(got) / double(total), m_status);
		});
		connect(pup, &QNetworkReply::finished, this, [this, pup] {
			pup->deleteLater();
			m_download.write(pup->readAll());
			m_download.close();
			const QString final_path = m_download.fileName().chopped(5);
			if (pup->error() != QNetworkReply::NoError || m_download.size() < 100 * 1024 * 1024)
			{
				m_download.remove();
				setFirmware(false, 0, tr("The download didn't finish. Try again."));
				return;
			}
			QFile::remove(final_path);
			m_download.rename(final_path);
			installFirmware(final_path);
		});
	});
}
