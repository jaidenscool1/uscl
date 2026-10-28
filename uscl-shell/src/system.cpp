#include "system.h"
#include "games.h"
#include "launcher.h"
#include "online.h"

#include <QDir>
#include <QFileInfo>
#include <QNetworkInformation>
#include <QNetworkReply>
#include <QSysInfo>
#include <QUrl>
#include <QtGui/qtguiglobal.h>
#if QT_CONFIG(vulkan)
#include <QVulkanFunctions>
#include <QVulkanInstance>
#endif
#include <QRegularExpression>
#include <QStandardPaths>

#include <algorithm>

namespace
{
	QString rpcs3Config()
	{
#ifdef _WIN32
		return GameModel::rpcs3ConfigDir() + "config/config.yml";
#else
		return GameModel::rpcs3ConfigDir() + "config.yml";
#endif
	}

	QString readText(const QString& path)
	{
		QFile f(path);
		return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString();
	}

	// Sony's list of the current PS3 system software, US region
	constexpr auto update_list = "http://fus01.ps3.update.playstation.net/update/ps3/list/us/ps3-updatelist.txt";
}

System::System(QObject* parent)
	: QObject(parent)
{
	QNetworkInformation::loadDefaultBackend();
	if (QNetworkInformation* n = QNetworkInformation::instance())
		connect(n, &QNetworkInformation::reachabilityChanged, this, &System::statusChanged);
	connect(&m_poll, &QTimer::timeout, this, &System::pollStatus);
	m_poll.start(5000);
	pollStatus();
	detectGpu();
}

void System::detectGpu()
{
#if QT_CONFIG(vulkan)
	QVulkanInstance inst;
	if (!inst.create())
		return;
	QVulkanFunctions* f = inst.functions();
	uint32_t n = 0;
	f->vkEnumeratePhysicalDevices(inst.vkInstance(), &n, nullptr);
	std::vector<VkPhysicalDevice> devices(n);
	f->vkEnumeratePhysicalDevices(inst.vkInstance(), &n, devices.data());
	for (VkPhysicalDevice d : devices)
	{
		VkPhysicalDeviceProperties p;
		f->vkGetPhysicalDeviceProperties(d, &p);
		VkPhysicalDeviceMemoryProperties m;
		f->vkGetPhysicalDeviceMemoryProperties(d, &m);
		VkDeviceSize vram = 0;
		for (uint32_t i = 0; i < m.memoryHeapCount; i++)
		{
			if (m.memoryHeaps[i].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT)
				vram = std::max(vram, m.memoryHeaps[i].size);
		}
		const bool discrete = p.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU;
		const int mb = int(vram / (1024 * 1024));
		// best card: a discrete one first, then the most video memory (what RPCS3 picks on most PCs)
		if (m_gpu.isEmpty() || (discrete && !m_discrete) || (discrete == m_discrete && mb > m_vram_mb))
		{
			m_gpu = QString::fromUtf8(p.deviceName);
			m_vram_mb = mb;
			m_discrete = discrete;
		}
	}
#endif
}

int System::scaleFor(bool discrete, int vram_mb, int screen_height)
{
	// PS3 games render at 720p: 150% is 1080p, 200% 1440p, 300% 4K
	const int gpu = !discrete || vram_mb < 3500 ? 100 : vram_mb < 6000 ? 150 : vram_mb < 10000 ? 200 : 300;
	const int screen = screen_height <= 800 ? 100 : screen_height <= 1100 ? 150 : screen_height <= 1500 ? 200 : 300;
	return std::min(gpu, screen);
}

QString System::soundUrl(const QString& name) const
{
	const QString own = GameModel::rpcs3ConfigDir() + "sounds/" + name + ".wav";
	return QFileInfo::exists(own) ? QUrl::fromLocalFile(own).toString() : "qrc:/qt/qml/Uscl/sounds/xmb/" + name + ".wav";
}

QString System::version()
{
	return QStringLiteral(USCL_VERSION);
}

int System::internet() const
{
	const QNetworkInformation* n = QNetworkInformation::instance();
	if (!n || n->reachability() == QNetworkInformation::Reachability::Unknown)
		return -1;
	return n->reachability() == QNetworkInformation::Reachability::Online ? 1 : 0;
}

void System::pollStatus()
{
	int level = -1;
	bool charging = false;
#ifdef __linux__
	for (const QString& bat : QDir("/sys/class/power_supply").entryList({"BAT*"}, QDir::Dirs | QDir::System))
	{
		const QString cap = readText("/sys/class/power_supply/" + bat + "/capacity").trimmed();
		if (!cap.isEmpty())
		{
			level = cap.toInt();
			charging = readText("/sys/class/power_supply/" + bat + "/status").trimmed() != "Discharging";
			break;
		}
	}
#endif
	if (level != m_battery || charging != m_charging)
	{
		m_battery = level;
		m_charging = charging;
		emit statusChanged();
	}
}

QString System::firmwareVersion() const
{
	// "release:04.9300:" -> 4.93
	static const QRegularExpression re("release:0*(\\d+)\\.(\\d\\d)");
	const auto m = re.match(readText(GameModel::rpcs3ConfigDir() + "dev_flash/vsh/etc/version.txt"));
	return m.hasMatch() ? m.captured(1) + "." + m.captured(2) : QString();
}

QString System::rpcs3Value(const QString& section, const QString& key) const
{
	return Online::yamlValue(readText(rpcs3Config()), section, key);
}

bool System::setRpcs3Value(const QString& section, const QString& key, const QString& value)
{
	const QString text = Online::setYamlValue(readText(rpcs3Config()), section, key, value);
	QFile f(rpcs3Config());
	return f.open(QIODevice::WriteOnly | QIODevice::Truncate) && f.write(text.toUtf8()) >= 0;
}

QString System::info() const
{
	QString cpu = QSysInfo::currentCpuArchitecture();
#ifdef __linux__
	for (const QString& line : readText("/proc/cpuinfo").split('\n'))
	{
		if (line.startsWith("model name"))
		{
			cpu = line.section(':', 1).trimmed();
			break;
		}
	}
	const QString mem = readText("/proc/meminfo").section('\n', 0, 0).section(':', 1).trimmed().section(' ', 0, 0);
	const QString ram = mem.isEmpty() ? QString() : QString::number(qRound(mem.toDouble() / 1024 / 1024)) + " GB";
#else
	const QString ram;
#endif
	QStringList lines{tr("USCL: %1").arg(version()), tr("PS3 system software: %1").arg(firmwareVersion().isEmpty() ? tr("not installed") : firmwareVersion()),
	                  tr("Operating system: %1").arg(QSysInfo::prettyProductName()), tr("Processor: %1").arg(cpu)};
	if (!ram.isEmpty())
		lines << tr("Memory: %1").arg(ram);
	if (!m_gpu.isEmpty())
		lines << tr("Graphics: %1 (%2 GB)").arg(m_gpu).arg(qRound(m_vram_mb / 1024.0));
	lines << tr("Settings folder: %1").arg(GameModel::rpcs3ConfigDir());
	return lines.join('\n');
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

