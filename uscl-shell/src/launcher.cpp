#include "launcher.h"
#include "padconfig.h"
#include "games.h"
#include "online.h"
#include "switchcore.h"

#include <QCoreApplication>
#include <QColor>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMap>
#include <QTimer>

#include <utility>
#include <QSettings>
#include <QUrl>

Launcher::Launcher(QObject* parent)
	: QObject(parent)
{
	m_process.setProcessChannelMode(QProcess::MergedChannels);
	connect(&m_process, &QProcess::stateChanged, this, [this] {
		emit runningChanged();
		emit gameShownChanged();
	});
	connect(&m_process, &QProcess::readyRead, this, [this] {
		for (const QByteArray& line : m_process.readAll().split('\n'))
		{
			if (line.trimmed() == "USCL:home")
				emit homeRequested();
			else if (!line.trimmed().isEmpty())
				m_tail << QString::fromUtf8(line.trimmed());
		}
		while (m_tail.size() > 30)
			m_tail.removeFirst();
	});
	connect(&m_process, &QProcess::finished, this, [this](int code, QProcess::ExitStatus status) {
		const bool crashed = !m_stopping && (status == QProcess::CrashExit || code != 0);
		m_stopping = false;
		emit runningChanged();
		emit gameShownChanged();
		emit finished(crashed, m_tail.join('\n'));
	});
	connect(&m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
		if (error == QProcess::FailedToStart)
		{
			emit runningChanged();
			emit finished(true, tr("Could not start %1").arg(m_process.program()));
		}
	});
}

QString Launcher::loadingStyle(const QString& background, const QString& foreground, const QString& accent, const QString& panel)
{
	const QColor bg(background), fg(foreground), focus(accent), surface(panel);
	if (!bg.isValid() || !fg.isValid() || !focus.isValid() || !surface.isValid())
		return {};
	return QStringLiteral(
	    "QWidget { background-color: %1; color: %2; } "
	    "QLabel { background-color: transparent; color: %2; } "
	    "QProgressBar { background-color: %4; color: %2; border: 1px solid %3; border-radius: 4px; padding: 2px; } "
	    "QProgressBar::chunk { background-color: %3; border-radius: 2px; } "
	    "QPushButton { background-color: %4; color: %2; border: 1px solid %3; padding: 6px 18px; } "
	    "QPushButton:focus { border: 2px solid %3; }")
	    .arg(bg.name(), fg.name(), focus.name(), surface.name());
}

void Launcher::setLoadingTheme(const QString& theme, const QString& background, const QString& foreground,
                               const QString& accent, const QString& panel)
{
	static const QStringList allowed = {"ps3", "ps4", "ps5", "x360classic", "x360", "xboxone", "switch", "switch2"};
	m_loading_style = allowed.contains(theme) ? loadingStyle(background, foreground, accent, panel) : QString();
	m_theme = m_loading_style.isEmpty() ? QString() : theme;
	m_loading_colors = m_theme.isEmpty() ? QString() : QColor(background).name().mid(1) + "," +
	    QColor(foreground).name().mid(1) + "," + QColor(accent).name().mid(1);
}

void Launcher::applyLoadingEnvironment(QProcessEnvironment& env, bool game) const
{
	// No inherited styling leaks into classic/settings tools or unconfigured launches.
	env.remove("USCL_THEME");
	env.remove("USCL_LOADING_STYLE");
	env.remove("USCL_LOADING_COLORS");
	if (game && !m_theme.isEmpty())
	{
		env.insert("USCL_THEME", m_theme);
		env.insert("USCL_LOADING_STYLE", m_loading_style);
		env.insert("USCL_LOADING_COLORS", m_loading_colors);
	}
}

QString Launcher::corePath()
{
	if (qEnvironmentVariableIsSet("USCL_RPCS3"))
		return qEnvironmentVariable("USCL_RPCS3");
#ifdef _WIN32
	return QCoreApplication::applicationDirPath() + "/rpcs3.exe";
#else
	return QCoreApplication::applicationDirPath() + "/rpcs3";
#endif
}

QString Launcher::ps4CorePath()
{
	if (qEnvironmentVariableIsSet("USCL_SHADPS4"))
		return qEnvironmentVariable("USCL_SHADPS4");
#ifdef _WIN32
	return QCoreApplication::applicationDirPath() + "/shadps4.exe";
#else
	return QCoreApplication::applicationDirPath() + "/shadps4";
#endif
}

bool Launcher::coreFound()
{
	return QFileInfo(corePath()).isExecutable();
}

bool Launcher::ps4CoreFound()
{
	return QFileInfo(ps4CorePath()).isExecutable();
}

bool Launcher::start(const QStringList& args, const QString& title, bool online, bool game, bool ps4, const QString& core)
{
	if (running())
		return false;
	// in an online session RPCS3 runs the game unmodified: no game patches, debugger or logpoints (uscl-rpcs3 0006)
	QProcessEnvironment env = core.isEmpty() ? QProcessEnvironment::systemEnvironment() : SwitchCore::environment();
	if (online)
		env.insert("USCL_ONLINE", "1");
	else
		env.remove("USCL_ONLINE");
	// a game's PS button comes to us (uscl-rpcs3 0008); the classic window keeps RPCS3's own home menu
	if (std::exchange(m_auto_pads, false) && !core.isEmpty())
		env.insert("USCL_AUTO_PADS", "1");
	if (game)
		env.insert("USCL_SHELL", "1");
	else
		env.remove("USCL_SHELL");
	if (ps4)
	{
		// Demonware names go to our server (or the bridge to the host's); the game binds 127.0.0.2 so the bridge
		// can own 127.0.0.1:3074/3077/3078
		env.insert("USCL_HOSTS", "*.demonware.net=127.0.0.1");
		env.insert("USCL_BIND_ADDR", "127.0.0.2");
		m_process.setWorkingDirectory(Online::preparePs4());
	}
	else
	{
		m_process.setWorkingDirectory({});
	}
	applyLoadingEnvironment(env, game);
	m_process.setProcessEnvironment(env);
	if (!core.isEmpty())
	{
		QDir().mkpath(SwitchCore::profileDir());
		m_process.setWorkingDirectory(SwitchCore::profileDir());
	}
	else if (!ps4)
		m_process.setWorkingDirectory(QCoreApplication::applicationDirPath());
	m_tail.clear();
	m_title = title;
	m_stopping = false;
	m_shown = true;
	m_process.start(core.isEmpty() ? (ps4 ? ps4CorePath() : corePath()) : core, args);
	emit runningChanged();
	emit gameShownChanged();
	return true;
}

// Wii and 3DS games don't run in RPCS3 -- they launch in Dolphin / Azahar (flatpaks). A crash or quit comes back
// here the same way an RPCS3 game does. 3DS: on Wayland our Azahar runs under XWayland (native Wayland renders
// black on NVIDIA) with OpenGL; the Flathub app needs software GL for its frontend. See uscl-3ds/ and uscl-dolphin/.
// Our Dolphin (uscl-dolphin patches: NetPlay host/join without its dialogs), else the Flathub app (offline only)
QString Launcher::ownDolphin()
{
	const QString path = qEnvironmentVariableIsSet("USCL_DOLPHIN") ? qEnvironmentVariable("USCL_DOLPHIN")
#ifdef Q_OS_WIN
	// own folder: Dolphin ships its own Qt DLLs, which would clash with RPCS3's next to uscl.exe
	                                                               : QCoreApplication::applicationDirPath() + "/Dolphin/Dolphin.exe";
#else
	                                                               : QCoreApplication::applicationDirPath() + "/dolphin-emu";
#endif
	return QFileInfo(path).isExecutable() ? path : QString();
}

// Where the Dolphin that runs keeps its settings: our own build gets a USCL profile (-u), the Flathub app its own
QString Launcher::dolphinConfigDir()
{
	return ownDolphin().isEmpty() ? QDir::homePath() + "/.var/app/org.DolphinEmu.dolphin-emu/config/dolphin-emu/"
	                              : GameModel::rpcs3ConfigDir() + "uscl/dolphin/Config/";
}

// Our Azahar (uscl-3ds patches: joins a room by itself) and its room server, else the Flathub app (offline only)
QString Launcher::ownAzahar(const QString& name)
{
	const QString var = name == "azahar-room" ? "USCL_AZAHAR_ROOM" : "USCL_AZAHAR";
#ifdef _WIN32
	const QString file = "Azahar/" + name + ".exe";   // own folder: Azahar's Qt DLLs (see ownDolphin)
#else
	const QString file = name;
#endif
	const QString path = qEnvironmentVariableIsSet(var.toLatin1().constData()) ? qEnvironmentVariable(var.toLatin1().constData())
	                                                                            : QCoreApplication::applicationDirPath() + "/" + file;
	return QFileInfo(path).isExecutable() ? path : QString();
}

// Azahar's settings: our build gets a USCL profile (XDG dirs under uscl/azahar), the Flathub app its own
QString Launcher::azaharConfigFile()
{
	return ownAzahar().isEmpty() ? QDir::homePath() + "/.var/app/org.azahar_emu.Azahar/config/azahar-emu/qt-config.ini"
	                             : GameModel::rpcs3ConfigDir() + "uscl/azahar/config/azahar-emu/qt-config.ini";
}

// Every Dolphin start: fullscreen like the other consoles; pads count even when the home screen kept the focus (Dolphin
// drops all input otherwise); the Wii Remote's speaker plays through the PC (Wii Sports' racket and pins; off by default)
static QStringList dolphinOptions()
{
	return {"-C", "Dolphin.Display.Fullscreen=True", "-C", "Dolphin.Input.BackgroundInput=True",
	        "-C", "Dolphin.Core.WiimoteEnableSpeaker=True"};
}

bool Launcher::startEmu(const QString& path, const QString& title, const QString& console, const QString& netplay,
                        const QString& nickname, int players)
{
	if (running())
		return false;
	QString program = "flatpak";
	QString workdir;
	QStringList args;
	QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
	if ((console == "Wii" || console == "GameCube") && !ownDolphin().isEmpty())
	{
		program = ownDolphin();
		args = {"-u", QDir(dolphinConfigDir()).absoluteFilePath(".."), "-C", "Dolphin.Analytics.PermissionAsked=True"};
		args << dolphinOptions();
		if (netplay.isEmpty())
			args << "-b" << "-e" << path;
		else
		{
			// NetPlay over the Steam tunnel: the host's server on 2626, a member's bridge serves 127.0.0.1:2626
			// (uscl-bridge preset "dolphin"). The game is found through the game list, so its folder goes there.
			for (const QString& c : {"Dolphin.NetPlay.TraversalChoice=direct", "Dolphin.NetPlay.HostPort=2626",
			                         "Dolphin.NetPlay.Address=127.0.0.1", "Dolphin.NetPlay.ConnectPort=2626",
			                         "Dolphin.NetPlay.UseUPNP=False", "Dolphin.General.ISOPaths=1"})
				args << "-C" << c;
			args << "-C" << "Dolphin.NetPlay.Nickname=" + nickname << "-C" << "Dolphin.General.ISOPath0=" + QFileInfo(path).absolutePath();
			env.insert("USCL_NETPLAY", netplay);
			env.insert("USCL_NETPLAY_GAME", path);
			// shortcut: the host starts once this many are in (2 at least); a later joiner waits for the next game
			env.insert("USCL_NETPLAY_PLAYERS", QString::number(std::max(2, players)));
		}
	}
	else if (console == "Wii" || console == "GameCube")
		args = QStringList{"run", "org.DolphinEmu.dolphin-emu"} + dolphinOptions() + QStringList{"-b", "-e", path};
	else if (console == "DS")
	{
		// our melonDS (uscl-melonds) next to uscl, else the Flathub app
#ifdef _WIN32
		const QString melon = QCoreApplication::applicationDirPath() + "/melonDS/melonDS.exe";
#else
		const QString melon = QCoreApplication::applicationDirPath() + "/melonDS";
#endif
		const QString own = qEnvironmentVariable("USCL_MELONDS", melon);
		if (QFileInfo(own).isExecutable())
			program = own, args = {path};
		else
			args = {"run", "net.kuribo64.melonDS", path};
	}
	else if (console == "3DS" && !ownAzahar().isEmpty())
	{
		program = ownAzahar();
		args = {"-f", path};   // fullscreen like the other consoles
		const QString profile = GameModel::rpcs3ConfigDir() + "uscl/azahar/";
		env.insert("XDG_CONFIG_HOME", profile + "config");
		env.insert("XDG_DATA_HOME", profile + "data");
#ifdef __linux__
		const QString platform = env.value("QT_QPA_PLATFORM");
		if (!env.value("DISPLAY").isEmpty() && (platform.isEmpty() || platform.startsWith("wayland")))
			env.insert("QT_QPA_PLATFORM", "xcb");
#endif
		// in a session: the room on the host's PC (UDP 24872), reached through the tunnel (uscl-bridge preset "3ds")
		if (!netplay.isEmpty())
			env.insert("USCL_ROOM", nickname + "@127.0.0.1:24872");
	}
	else if (console == "3DS")
		args = {"run", "--env=LIBGL_ALWAYS_SOFTWARE=1", "org.azahar_emu.Azahar", "-f", path};
	else if (console == "WiiU")
		args = {"run", "info.cemu.Cemu", "-f", "-g", path};   // Cemu: fullscreen, load game
	else if (console == "Xbox360")
	{
		// Xenia is a Windows build (no flatpak); run it under Wine. Experimental on Linux.
		const QString xenia = qEnvironmentVariableIsSet("USCL_XENIA")
			? qEnvironmentVariable("USCL_XENIA")
			: QDir::homePath() + "/.local/share/uscl/xenia/xenia.exe";
		program = "wine";
		args = {xenia, "--fullscreen", path};
	}
	else if (console == "PS5")
	{
		// KytyPS5 (github.com/KytyPS5/KytyPS5, GPL-2.0), its release folder as-is: own folder, it ships its own Qt.
		// It keeps shader caches and temp data in the working directory, so it gets a USCL profile folder.
#ifdef _WIN32
		const QString kyty_file = "/KytyPS5/kyty_emulator.exe";
#else
		const QString kyty_file = "/KytyPS5/kyty_emulator";
#endif
		program = qEnvironmentVariableIsSet("USCL_KYTYPS5") ? qEnvironmentVariable("USCL_KYTYPS5")
		        : QFileInfo(QCoreApplication::applicationDirPath() + kyty_file).isExecutable() ? QCoreApplication::applicationDirPath() + kyty_file
		                                                                                      : QDir::homePath() + "/.local/share/uscl" + kyty_file;
		workdir = GameModel::rpcs3ConfigDir() + "uscl/kytyps5";
		if (!QFileInfo(program).isExecutable() || !QDir().mkpath(workdir))
		{
			emit finished(true, tr("The PS5 emulator (KytyPS5) is missing. Put its release folder next to USCL as KytyPS5, or set USCL_KYTYPS5."));
			return false;
		}
		args = {"--game", path, "--fullscreen", "--shader-cache", "--hide-cursor"};
	}
	else
		return false;
	env.remove("USCL_ONLINE");
	env.insert("USCL_SHELL", "1");
	m_process.setWorkingDirectory(workdir);
	applyLoadingEnvironment(env, true);
	m_process.setProcessEnvironment(env);
	m_tail.clear();
	m_title = title;
	m_stopping = false;
	m_shown = true;
	m_process.start(program, args);
	emit runningChanged();
	emit gameShownChanged();
	return true;
}

bool Launcher::startGame(const QString& path, const QString& title, bool online, const QString& console, const QString& nickname)
{
	if (console == "Switch")
	{
		QString problem = SwitchCore::prerequisiteError(path);
		if (problem.isEmpty() && online && SwitchCore::isSwitch2(path))
			problem = tr("Switch 2 games play offline in USCL for now.");
		if (problem.isEmpty()) problem = SwitchCore::prepareLaunchConfig();
		if (!problem.isEmpty())
		{
			emit finished(true, problem);
			return false;
		}
		return start(SwitchCore::arguments(path, online, nickname), title, false, true, false, SwitchCore::executable(false, path));
	}
	if (QFileInfo::exists(path + "/sce_sys/param.sfo"))
	{
		// Black Ops II's eboot only picks the mode; codmp.elf is multiplayer, the online part
		const QString elf = QFileInfo::exists(path + "/codmp.elf") ? "/codmp.elf" : "/eboot.bin";
		return start({"--game", path + elf, "--override-root", path, "--fullscreen", "true"}, title, online, true, true);
	}
	return start({"--no-gui", "--fullscreen", path}, title, online, true);
}

bool Launcher::openSwitchSettings()
{
	if (!QFileInfo(SwitchCore::executable(true)).isExecutable())
	{
		emit finished(true, tr("The Switch settings application (eden) is missing. Set USCL_SWITCH_GUI to its executable path."));
		return false;
	}
	return start({}, tr("Switch Settings"), false, false, false, SwitchCore::executable(true));
}

QString Launcher::switchStatus(const QString& path) { return SwitchCore::prerequisiteError(path); }
bool Launcher::switchFilesMissing(const QString& path) { return SwitchCore::consoleFilesMissing(path); }
QString Launcher::switchProfile() { return SwitchCore::dataDir(); }
QString Launcher::importSwitchKeys(const QString& path) { return SwitchCore::importKeys(QUrl(path).isLocalFile() ? QUrl(path).toLocalFile() : path); }
QString Launcher::importSwitchFirmware(const QString& path) { return SwitchCore::importFirmware(QUrl(path).isLocalFile() ? QUrl(path).toLocalFile() : path); }
QVariantMap Launcher::switchFiles() { return SwitchCore::installedConsoleFiles(); }

QString Launcher::configureSwitchTool(const QString& kind, const QString& path)
{
	if (kind != "CORE" && kind != "QT" && kind != "GUI" && kind != "ROOM")
		return tr("Unknown Switch component.");
	const QString local = QUrl(path).isLocalFile() ? QUrl(path).toLocalFile() : path;
	if (!QFileInfo(local).isExecutable())
		return tr("Choose an executable file.");
	QSettings settings;
	settings.setValue("switch/USCL_SWITCH_" + (kind == "QT" ? QString("CORE") : kind), QFileInfo(local).absoluteFilePath());
	if (kind == "QT" || kind == "CORE") settings.setValue("switch/frontend", kind == "QT" ? "qt" : "sdl");
	if (kind == "QT") settings.setValue("switch/USCL_SWITCH_GUI", QFileInfo(local).absoluteFilePath());
	settings.sync();
	return settings.status() == QSettings::NoError ? QString() : tr("Couldn't save the emulator location.");
}

bool Launcher::openClassic()
{
	return start({}, tr("All Settings"), false, false);
}

void Launcher::command(const char* line, bool shown)
{
	if (!running() || m_shown == shown)
		return;
	m_process.write(line);
	m_shown = shown;
	emit gameShownChanged();
}

void Launcher::showHome()
{
	command("hide\n", false);
}

void Launcher::resume()
{
	command("show\n", true);
}

void Launcher::stop()
{
	if (!running())
		return;
	m_stopping = true;
	m_process.terminate();
	QTimer::singleShot(5000, this, [this] {
		if (running())
			m_process.kill();
	});
}

namespace
{
	QString resource(const QString& name)
	{
		QFile f(":/qt/qml/Uscl/input/" + name);
		return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString();
	}
}

QString Launcher::inputYml(const QString& current, const QStringList& pads)
{
	// split into "Player N Input:" blocks
	QMap<int, QString> blocks;
	int player = 0;
	for (const QString& line : current.split('\n'))
	{
		if (line.startsWith("Player ") && line.endsWith(" Input:"))
			player = line.section(' ', 1, 1).toInt();
		if (player > 0 && !line.isEmpty())
			blocks[player] += line + '\n';
	}

	const QString sdl = resource("sdl_player.yml"), none = resource("null_player.yml");
	QMap<QString, int> seen;
	for (int i = 1; i <= 7; i++)
	{
		const QString head = QString("Player %1 Input:\n").arg(i);
		if (i <= pads.size())
		{
			// RPCS3's SDL handler numbers controllers that share a name: "Name 1", "Name 2"
			const QString device = pads[i - 1] + " " + QString::number(++seen[pads[i - 1]]);
			const QString dev_line = "  Device: \"" + QString(device).replace("\"", "\\\"") + "\"";
			QStringList lines = blocks.value(i).split('\n');
			if (lines.size() > 2 && lines[1] == "  Handler: SDL" && lines[2].startsWith("  Device:"))
			{
				lines[2] = dev_line;
				blocks[i] = lines.join('\n');
			}
			else
			{
				blocks[i] = head + "  Handler: SDL\n" + dev_line + "\n" + sdl;
			}
		}
		else if (!blocks.contains(i))
		{
			blocks[i] = head + "  Handler: \"Null\"\n  Device: \"Null\"\n" + none;
		}
		if (!blocks[i].endsWith('\n'))
			blocks[i] += '\n';
	}
	QString out;
	for (int i = 1; i <= 7; i++)
		out += blocks[i];
	return out;
}

bool Launcher::prepareInput(const QStringList& pads, const QString& console)
{
	if (console == "Switch")
		return m_auto_pads = true; // Eden maps them itself, with its own per-controller layouts
	if (console == "Wii" || console == "GameCube")
		return PadConfig::writeDolphin(PadConfig::connected(), dolphinConfigDir());
	if (console == "3DS")
		return PadConfig::writeAzahar(PadConfig::connected(), azaharConfigFile());
	if (console == "WiiU")
		return PadConfig::writeCemu(PadConfig::connected());
	if (console != "PS3")
		return true; // PS4: shadPS4 gives players 1-4 the controllers in order; Xbox 360: Wine's XInput does
	if (pads.isEmpty())
		return true; // keyboard players keep whatever is set
#ifdef _WIN32
	const QString dir = GameModel::rpcs3ConfigDir() + "config/input_configs/";
#else
	const QString dir = GameModel::rpcs3ConfigDir() + "input_configs/";
#endif
	QDir().mkpath(dir + "global");
	QFile active(dir + "active_input_configurations.yml");
	if (!active.exists() && active.open(QIODevice::WriteOnly))
		active.write("Active Configurations:\n  global: Default\n");
	QFile f(dir + "global/Default.yml");
	const QString current = f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString();
	f.close();
	const QString next = inputYml(current, pads);
	if (next == current)
		return true;
	return f.open(QIODevice::WriteOnly | QIODevice::Truncate) && f.write(next.toUtf8()) >= 0;
}
