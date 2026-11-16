#include "switchcore.h"
#include "games.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <QSettings>
#include <QProcess>
#include <QTemporaryDir>
#include <QVersionNumber>
#include <QtEndian>
#include <SDL3/SDL.h>

namespace
{
	bool qtFrontend()
	{
		return qEnvironmentVariable("USCL_SWITCH_FRONTEND", QSettings().value("switch/frontend", "sdl").toString()) == "qt";
	}

	QString configDir()
	{
#ifdef _WIN32
		return SwitchCore::dataDir() + "config/";
#else
		return SwitchCore::profileDir() + "config/eden/";
#endif
	}

	int keyboardScancode(int key)
	{
		if (key >= Qt::Key_A && key <= Qt::Key_Z)
			return SDL_SCANCODE_A + key - Qt::Key_A;
		if (key >= Qt::Key_F1 && key <= Qt::Key_F24)
			return key <= Qt::Key_F12 ? SDL_SCANCODE_F1 + key - Qt::Key_F1 : SDL_SCANCODE_F13 + key - Qt::Key_F13;
		switch (key)
		{
		case Qt::Key_Left: return SDL_SCANCODE_LEFT;
		case Qt::Key_Right: return SDL_SCANCODE_RIGHT;
		case Qt::Key_Up: return SDL_SCANCODE_UP;
		case Qt::Key_Down: return SDL_SCANCODE_DOWN;
		case Qt::Key_Shift: return SDL_SCANCODE_LSHIFT;
		case Qt::Key_Control: return SDL_SCANCODE_LCTRL;
		case Qt::Key_Alt: return SDL_SCANCODE_LALT;
		case Qt::Key_Meta: return SDL_SCANCODE_LGUI;
		case Qt::Key_Return: return SDL_SCANCODE_RETURN;
		case Qt::Key_Enter: return SDL_SCANCODE_KP_ENTER;
		case Qt::Key_Escape: return SDL_SCANCODE_ESCAPE;
		case Qt::Key_Tab: return SDL_SCANCODE_TAB;
		case Qt::Key_Backspace: return SDL_SCANCODE_BACKSPACE;
		case Qt::Key_Delete: return SDL_SCANCODE_DELETE;
		case Qt::Key_Insert: return SDL_SCANCODE_INSERT;
		case Qt::Key_Home: return SDL_SCANCODE_HOME;
		case Qt::Key_End: return SDL_SCANCODE_END;
		case Qt::Key_PageUp: return SDL_SCANCODE_PAGEUP;
		case Qt::Key_PageDown: return SDL_SCANCODE_PAGEDOWN;
		default: return key >= 32 && key <= 126 ? SDL_GetScancodeFromKey(SDL_Keycode(key), nullptr) : SDL_SCANCODE_UNKNOWN;
		}
	}

	// Eden serializes nested bindings with $0/:, $1/, and $2/$. Only keyboard
	// parameters change; physical controller GUIDs and button numbers stay intact.
	QString sdlBinding(const QString& binding, int depth = 0)
	{
		if (depth > 8 || !binding.contains(':'))
			return binding;
		QMap<QString, QString> params;
		for (const QString& pair : binding.split(','))
		{
			const auto at = pair.indexOf(':');
			if (at < 0) return binding;
			auto decode = [](QString s) { return s.replace("$0", ":").replace("$1", ",").replace("$2", "$"); };
			params.insert(decode(pair.left(at)), decode(pair.mid(at + 1)));
		}
		if (params.value("engine") == "keyboard" && params.contains("code"))
			params["code"] = QString::number(keyboardScancode(params.value("code").toInt()));
		else
			for (auto it = params.begin(); it != params.end(); ++it)
				if (it.value().contains("engine:")) it.value() = sdlBinding(it.value(), depth + 1);
		QStringList pairs;
		auto encode = [](QString s) { return s.replace("$", "$2").replace(",", "$1").replace(":", "$0"); };
		for (auto it = params.cbegin(); it != params.cend(); ++it)
			pairs << encode(it.key()) + ':' + encode(it.value());
		return pairs.join(',');
	}

	// An extracted dump (exefs/main beside main.npdm) has no file name to read an ID from: the npdm's ACI0 holds it.
	QString npdmProgramId(const QString& main)
	{
		QFile f(QFileInfo(main).absolutePath() + "/main.npdm");
		if (!f.open(QIODevice::ReadOnly) || f.size() > 1024 * 1024)
			return {};
		const QByteArray d = f.readAll();
		if (d.size() < 0x80 || !d.startsWith("META"))
			return {};
		const quint64 aci = qFromLittleEndian<quint32>(d.constData() + 0x70);
		if (aci + 0x18 > quint64(d.size()) || d.mid(aci, 4) != "ACI0")
			return {};
		return QString("%1").arg(qFromLittleEndian<quint64>(d.constData() + aci + 0x10), 16, 16, QChar('0')).toUpper();
	}

	// File names inside an NSP/NSZ (PFS0): its tickets are named after the rights ID, title ID + key generation
	QStringList pfs0Names(const QString& path)
	{
		QFile f(path);
		const QByteArray h = f.open(QIODevice::ReadOnly) ? f.read(16) : QByteArray();
		if (h.size() != 16 || !h.startsWith("PFS0"))
			return {};
		const quint32 count = qFromLittleEndian<quint32>(h.constData() + 4), strings = qFromLittleEndian<quint32>(h.constData() + 8);
		if (count == 0 || count > 4096 || strings > 1024 * 1024)
			return {};
		const QByteArray entries = f.read(qint64(count) * 24), table = f.read(strings);
		if (entries.size() != qint64(count) * 24 || table.size() != qint64(strings))
			return {};
		QStringList out;
		for (quint32 i = 0; i < count; i++)
		{
			const quint32 at = qFromLittleEndian<quint32>(entries.constData() + i * 24 + 16);
			if (at < quint32(table.size()))
				out << QString::fromUtf8(table.constData() + at);
		}
		return out;
	}

	// Master key index -> the first Switch system software that has it (the console's key generations).
	// shortcut: the table ends at 22.0.0; a newer major version counts as one more key each, until it's extended
	const char* const firstFirmwareForKey[] = {"1.0.0", "3.0.0", "3.0.1", "4.0.0", "5.0.0", "6.0.0", "6.2.0", "7.0.0", "8.1.0",
	                                          "9.0.0", "9.1.0", "12.1.0", "13.0.0", "14.0.0", "15.0.0", "16.0.0", "17.0.0",
	                                          "18.0.0", "19.0.0", "20.0.0", "21.0.0", "22.0.0"};
	constexpr int knownKeys = int(std::size(firstFirmwareForKey));

	int keyForFirmware(const QVersionNumber& v)
	{
		int key = -1;
		for (int i = 0; i < knownKeys; i++)
			if (QVersionNumber::fromString(firstFirmwareForKey[i]) <= v)
				key = i;
		return key == knownKeys - 1 ? key + std::max(0, v.majorVersion() - 22) : key;
	}

	QString firmwareForKey(int key)
	{
		return key < knownKeys ? QString(firstFirmwareForKey[std::max(key, 0)]) : QString("%1.0.0").arg(22 + key - (knownKeys - 1));
	}

	// Highest master_key_XX with a real value in a prod.keys, -1 if none (a blank template has none)
	int highestMasterKey(const QString& keys)
	{
		QFile f(keys);
		if (!f.open(QIODevice::ReadOnly) || f.size() > 1024 * 1024)
			return -1;
		static const QRegularExpression re("(?m)^\\s*master_key_([0-9a-fA-F]{2})\\s*=\\s*([0-9a-fA-F]{32})\\s*$");
		int best = -1;
		for (auto it = re.globalMatch(QString::fromLatin1(f.readAll())); it.hasNext();)
		{
			const auto m = it.next();
			if (m.captured(2).count('0') != 32)
				best = std::max(best, m.captured(1).toInt(nullptr, 16));
		}
		return best;
	}

	QVersionNumber versionInName(const QString& name)
	{
		static const QRegularExpression re("(\\d{1,2})\\.(\\d{1,2})\\.(\\d{1,2})");
		const auto m = re.match(name);
		return m.hasMatch() ? QVersionNumber(m.captured(1).toInt(), m.captured(2).toInt(), m.captured(3).toInt()) : QVersionNumber();
	}

	// prod.keys / title.keys and system software (a folder of .nca files, or a firmware .zip) under dir
	void collectConsoleFiles(const QString& dir, int depth, QStringList& keys, QStringList& titles, QStringList& firmware, const QString& skip)
	{
		if (QDir(dir).absolutePath().startsWith(QDir(skip).absolutePath()))
			return;
		const QDir d(dir);
		if (QFileInfo(d.filePath("prod.keys")).isFile() && !keys.contains(d.filePath("prod.keys")))
			keys << d.filePath("prod.keys");
		if (QFileInfo(d.filePath("title.keys")).isFile() && !titles.contains(d.filePath("title.keys")))
			titles << d.filePath("title.keys");
		if (d.entryList({"*.nca"}, QDir::Files).size() >= 40 && !firmware.contains(d.absolutePath()))   // a set has hundreds
			firmware << d.absolutePath();
		for (const QFileInfo& zip : d.entryInfoList({"*irmware*.zip"}, QDir::Files))
			if (!firmware.contains(zip.absoluteFilePath()))
				firmware << zip.absoluteFilePath();
		if (depth > 0)
			for (const QFileInfo& sub : d.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::NoSymLinks))
				collectConsoleFiles(sub.absoluteFilePath(), depth - 1, keys, titles, firmware, skip);
	}

	// Unpacks a firmware .zip with the system's own tools (Windows 10+ ships tar, which reads zip)
	bool unzipTo(const QString& zip, const QString& dir)
	{
		for (const QStringList& cmd : {QStringList{"tar", "-xf", zip, "-C", dir}, QStringList{"unzip", "-q", "-o", zip, "-d", dir},
		                               QStringList{"bsdtar", "-xf", zip, "-C", dir}})
		{
			QProcess p;
			p.start(cmd[0], cmd.mid(1));
			if (p.waitForFinished(10 * 60 * 1000) && p.exitStatus() == QProcess::NormalExit && p.exitCode() == 0)
				return true;
		}
		return false;
	}

	// Copies every .nca under dir (a firmware zip may nest them one folder down) into a core's system storage
	bool copyFirmware(const QString& dir, const QString& registered)
	{
		bool any = false;
		QDir src(dir);
		QFileInfoList files = src.entryInfoList({"*.nca"}, QDir::Files);
		for (const QFileInfo& sub : src.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot))
			if (files.isEmpty())
				files = QDir(sub.absoluteFilePath()).entryInfoList({"*.nca"}, QDir::Files);
		if (files.isEmpty() || !QDir().mkpath(registered))
			return false;
		for (const QFileInfo& f : files)
		{
			const QString to = registered + f.fileName();
			QFile::remove(to);
			if (!QFile::copy(f.absoluteFilePath(), to))
				return false;
			any = true;
		}
		return any;
	}

	// one core's keys and system software (Eden: data/eden/, Eglantine: data/eglantine/)
	QVariantMap coreFiles(const QString& core)
	{
		QFile version(core + "firmware-version.txt");
		return {{"master", highestMasterKey(core + "keys/prod.keys")}, {"titleKeys", QFileInfo::exists(core + "keys/title.keys")},
		        {"version", version.open(QIODevice::ReadOnly) ? QString::fromLatin1(version.readAll()).trimmed() : QString()},
		        {"firmware", !QDir(core + "nand/system/Contents/registered").entryList({"*.nca"}, QDir::Files).isEmpty()}};
	}

	// Newest numbered update folder holding a full dump (updates/<version>/exefs/main), or empty.
	QString newestUpdate(const QDir& updates)
	{
		QString name;
		qulonglong best = 0;
		for (const QString& n : updates.entryList(QDir::Dirs | QDir::NoDotAndDotDot))
		{
			bool ok = false;
			const qulonglong v = n.toULongLong(&ok);
			if (ok && (name.isEmpty() || v > best) && QFileInfo::exists(updates.filePath(n + "/exefs/main")))
				best = v, name = n;
		}
		return name;
	}

	QString findTool(const QString& variable, const QString& name)
	{
		if (qEnvironmentVariableIsSet(variable.toLatin1().constData()))
			return qEnvironmentVariable(variable.toLatin1().constData());
		const QString configured = QSettings().value("switch/" + variable).toString();
		if (!configured.isEmpty())
			return configured;
#ifdef _WIN32
		const QString file = name + ".exe";
		// own folder: each core ships its own Qt DLLs, which would clash with RPCS3's next to uscl.exe
		const QString bundled = QCoreApplication::applicationDirPath() + (name == "eglantine" ? "/Eglantine/" : "/Eden/") + file;
#else
		const QString file = name;
		const QString bundled = QCoreApplication::applicationDirPath() + '/' + file;
#endif
		return QFileInfo(bundled).isExecutable() ? bundled : QStandardPaths::findExecutable(file);
	}

	bool copyFile(const QString& from, const QString& to)
	{
		QFile input(from);
		QSaveFile output(to);
		if (!input.open(QIODevice::ReadOnly) || !QDir().mkpath(QFileInfo(to).absolutePath()) || !output.open(QIODevice::WriteOnly))
			return false;
		while (!input.atEnd())
		{
			const QByteArray block = input.read(1 << 20);
			if (block.isEmpty() || output.write(block) != block.size())
				return false;
		}
		output.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
		return output.commit();
	}
}

QString SwitchCore::profileDir()
{
	return QDir(qEnvironmentVariable("USCL_SWITCH_PROFILE", GameModel::rpcs3ConfigDir() + "uscl/switch")).absolutePath() + '/';
}

QString SwitchCore::dataDir()
{
	return profileDir() + "data/eden/";
}

QProcessEnvironment SwitchCore::environment()
{
	auto env = QProcessEnvironment::systemEnvironment();
	env.remove("USCL_ONLINE");
	env.insert("XDG_CONFIG_HOME", profileDir() + "config");
	env.insert("XDG_DATA_HOME", profileDir() + "data");
	env.insert("XDG_CACHE_HOME", profileDir() + "cache");
#ifdef __linux__
	// Eden's bundled SDL2 needs XWayland on desktops that force the Wayland driver globally.
	if (env.value("SDL_VIDEODRIVER") == "wayland" && !env.value("DISPLAY").isEmpty())
		env.insert("SDL_VIDEODRIVER", "x11");
	// The Qt/Vulkan cores (Eden's and Eglantine for Switch 2) need XWayland on this desktop; the SDL core ignores it.
	const QString platform = env.value("QT_QPA_PLATFORM");
	if (platform.startsWith("wayland") && !env.value("DISPLAY").isEmpty())
		env.insert("QT_QPA_PLATFORM", "xcb");
#endif
#ifdef _WIN32
	env.insert("APPDATA", profileDir() + "data");
#endif
	return env;
}

bool SwitchCore::isSwitch2(const QString& path)
{
	// Switch 2 program IDs start with 04, Switch ones with 01.
	return describe(path).value("serial").toString().startsWith("04");
}

bool SwitchCore::needsEglantine(const QString& path)
{
	const QString ext = QFileInfo(path).suffix().toLower();
	return ext == "nsz" || ext == "xcz" || isSwitch2(path);
}

QString SwitchCore::executable(bool settings, const QString& game)
{
	// Eden can't run Switch 2 builds or compressed dumps; Eglantine (an Eden fork with NX2 support) can.
	if (!game.isEmpty() && needsEglantine(game))
		return findTool("USCL_SWITCH2_CORE", "eglantine");
	return findTool(settings ? "USCL_SWITCH_GUI" : "USCL_SWITCH_CORE", settings ? "eden" : "eden-cli");
}

QString SwitchCore::roomExecutable()
{
	return findTool("USCL_SWITCH_ROOM", "eden-room");
}

bool SwitchCore::isGame(const QString& path)
{
	const QFileInfo info(path);
	const QString ext = info.suffix().toLower();
	const bool extracted = info.fileName() == "main" && info.dir().dirName() == "exefs";
	if (ext != "nsp" && ext != "nsz" && ext != "xci" && ext != "xcz" && ext != "nro" && !extracted)
		return false;
	QFile f(path);
	if (!f.open(QIODevice::ReadOnly))
		return false;
	if (extracted)
		return f.read(4) == "NSO0" && !npdmProgramId(path).isEmpty();
	if (ext == "nsp" || ext == "nsz")
	{
		const auto header = f.read(16);
		if (header.size() != 16 || !header.startsWith("PFS0"))
			return false;
		const auto count = qFromLittleEndian<quint32>(header.constData() + 4);
		const auto strings = qFromLittleEndian<quint32>(header.constData() + 8);
		return count > 0 && count <= 65536 && strings <= 16 * 1024 * 1024 && 16 + quint64(count) * 24 + strings <= quint64(f.size());
	}
	if (ext == "xci" || ext == "xcz")
		return f.seek(0x100) && f.read(4) == "HEAD";
	if (ext == "nro")
		return f.seek(0x10) && f.read(4) == "NRO0";
	return false;
}

bool SwitchCore::isSerial(const QString& serial)
{
	static const QRegularExpression id("^(?:[0-9A-Fa-f]{16}|SWITCH-[0-9a-f]{16})$");
	return id.match(serial).hasMatch();
}

QVariantMap SwitchCore::describe(const QString& path)
{
	if (!isGame(path))
		return {};
	if (QFileInfo(path).fileName() == "main")
	{
		// Dumpers write base/ and full updates/<version>/ side by side: list only the newest.
		QDir version = QFileInfo(path).dir(), dump;
		version.cdUp();
		dump = version;
		dump.cdUp();
		QString title = version.dirName();
		if (version.dirName() == "base")
		{
			if (!newestUpdate(QDir(dump.filePath("updates"))).isEmpty())
				return {};
			title = dump.dirName();
		}
		else if (dump.dirName() == "updates")
		{
			if (newestUpdate(dump) != version.dirName())
				return {};
			dump.cdUp();
			title = dump.dirName();
		}
		static const QRegularExpression noise("\\[[^\\]]*\\]|[ _]v\\d+(\\.\\d+)*$");
		static const QRegularExpression camel("([a-z])([A-Z])"); // NintendoSwitchSports -> Nintendo Switch Sports
		title = title.remove(noise).replace('_', ' ').replace(camel, "\\1 \\2").trimmed();
		const QString serial = npdmProgramId(path);
		if ((serial.toULongLong(nullptr, 16) & 0x1fff) != 0)
			return {};
		return {{"title", title}, {"serial", serial}, {"path", QFileInfo(path).absoluteFilePath()}, {"console", "Switch"}, {"version", ""}};
	}
	QString title = QFileInfo(path).completeBaseName();
	static const QRegularExpression id("\\[([0-9a-fA-F]{16})\\]");
	const auto match = id.match(title);
	QString serial = match.hasMatch() ? match.captured(1).toUpper() : QString();
	// Update and DLC containers are installed in Eden, not launched as games.
	if (!serial.isEmpty() && (serial.toULongLong(nullptr, 16) & 0x1fff) != 0)
		return {};
	// no [title id] in the name: an NSP's ticket is named after it (update and DLC tickets too, so they're skipped)
	for (const QString& name : serial.isEmpty() ? pfs0Names(path) : QStringList())
	{
		if (name.endsWith(".tik") && name.size() == 36)
		{
			serial = name.left(16).toUpper();
			if ((serial.toULongLong(nullptr, 16) & 0x1fff) != 0)
				return {};
			break;
		}
	}
	if (serial.isEmpty())
	{
		QFile file(path);
		if (!file.open(QIODevice::ReadOnly))
			return {};
		const QByteArray identity = QByteArray::number(file.size()) + ':' + file.read(65536);
		serial = "SWITCH-" + QString::fromLatin1(QCryptographicHash::hash(identity, QCryptographicHash::Sha256).toHex().left(16));
	}
	if (match.hasMatch())
		title = title.left(match.capturedStart()).trimmed();
	return {{"title", title}, {"serial", serial}, {"path", QFileInfo(path).absoluteFilePath()}, {"console", "Switch"}, {"version", ""}};
}

QStringList SwitchCore::arguments(const QString& path, bool online, const QString& nickname)
{
	// shortcut: Eglantine games run offline (it has no --uscl-room); port uscl-eden 0003 to uscl-eglantine for rooms
	if (needsEglantine(path))
		return {"-f", "-g", QFileInfo(path).absoluteFilePath()};
	const QString config = configDir() + "sdl2-config.ini";
	const bool qt = qtFrontend();
	QStringList args = qt ? QStringList{"-f"} : QStringList{"--config", config, "--fullscreen"};
	if (!qt && executable().endsWith(".AppImage", Qt::CaseInsensitive))
		args.prepend("eden-cli");
	if (online)
	{
		QString name = nickname;
		name.replace(QRegularExpression("[^A-Za-z0-9_. -]"), "_");
		if (name.size() < 4)
			name = "USCL_" + name;
		args << (qt ? "--uscl-room" : "--multiplayer") << name.left(20) + "@127.0.0.1:24872";
	}
	return args << (qt ? "-g" : "--game") << QFileInfo(path).absoluteFilePath();
}

QString SwitchCore::prepareLaunchConfig()
{
	// Eglantine offers to move a legacy eden folder when its own config folder is missing; one choice renames
	// Eden's profile away, so the folder always exists before a launch.
#ifdef _WIN32
	const QString eglantine = profileDir() + "data/eglantine/config";
#else
	const QString eglantine = profileDir() + "config/eglantine";
#endif
	if (!QDir().mkpath(eglantine))
		return QCoreApplication::translate("SwitchCore", "Couldn't prepare the Switch 2 profile.");
	{
		// Eglantine asks "close Eglantine?" on SIGTERM, so stop() would always end in a kill; 2 = Ask_Never lets it save and exit.
		QSettings ui(eglantine + "/qt-config.ini", QSettings::IniFormat);
		ui.setValue("UI/confirmStop/default", false);
		ui.setValue("UI/confirmStop", 2);
		ui.sync();
		if (ui.status() != QSettings::NoError)
			return QCoreApplication::translate("SwitchCore", "Couldn't save Switch 2 launch settings.");
	}
	if (qtFrontend()) return {};
	const QString source = configDir() + "qt-config.ini";
	// With no GUI configuration, let the SDL frontend create its own defaults.
	if (!QFileInfo::exists(source)) return {};
	QSettings gui(source, QSettings::IniFormat);
	// written straight to its place: QSettings saves atomically by itself, and on Windows a QTemporaryFile
	// round trip (QSettings replacing a file the temporary still owns) failed
	const QString target = configDir() + "sdl2-config.ini";
	if (QFile::exists(target) && !QFile::remove(target))
		return QCoreApplication::translate("SwitchCore", "Couldn't prepare Switch controls.");
	QSettings sdl(target, QSettings::IniFormat);
	for (const QString& key : gui.allKeys())
	{
		const QVariant value = gui.value(key);
		sdl.setValue(key, key.startsWith("Controls/") && value.toString().contains("engine:") ? sdlBinding(value.toString()) : value);
	}
	for (const QString& key : gui.allKeys())
		if (key.startsWith("Controls/") && gui.value(key).toString().contains("engine:"))
			sdl.setValue(key + "/default", false);
	sdl.sync();
	if (gui.status() != QSettings::NoError || sdl.status() != QSettings::NoError)
		return QCoreApplication::translate("SwitchCore", "Couldn't save Switch launch settings.");
	return {};
}

QString SwitchCore::prerequisiteError(const QString& path)
{
	const bool switch2 = !path.isEmpty() && needsEglantine(path);   // Switch 2 games and compressed dumps run in Eglantine
	if (switch2 && !QFileInfo(executable(false, path)).isExecutable())
		return QCoreApplication::translate("SwitchCore", "This game needs the Eglantine emulator, which is missing. Reinstall USCL, or set USCL_SWITCH2_CORE.");
	if (!QFileInfo(executable()).isExecutable() && !switch2)
		return QCoreApplication::translate("SwitchCore", "The Switch emulator is missing. Configure it in Settings → Switch Emulation.");
#ifdef _WIN32
	if (QFileInfo(QFileInfo(executable()).absolutePath() + "/user").isDir())
		return QCoreApplication::translate("SwitchCore", "Use an Eden installation without a portable user folder so USCL can keep a separate Switch profile.");
#endif
	if (QFileInfo(path).suffix().compare("nro", Qt::CaseInsensitive) == 0)
		return {};
	const QString keys = (switch2 ? profileDir() + "data/eglantine/" : dataDir()) + "keys/prod.keys";
	if (!QFileInfo(keys).isFile())
		return QCoreApplication::translate("SwitchCore", "No console keys yet. Put prod.keys dumped from your Switch in Downloads (USCL finds it), or import it in Settings → Switch Emulation.");
	// what the game needs: its tickets name the key generation (NSP/NSZ; cartridge dumps don't carry one)
	int need = -1;
	for (const QString& name : pfs0Names(path))
		if (name.endsWith(".tik") && name.size() == 36)
			need = std::max(need, std::max(name.mid(30, 2).toInt(nullptr, 16), 1) - 1);
	if (need < 0)
		return {};
	const int have = highestMasterKey(keys);
	if (need > have)
		return QCoreApplication::translate("SwitchCore", "This game needs newer console keys: master key %1, from Switch system software %2 or newer. "
		                                                 "Yours go up to master key %3 (%4). Dump prod.keys from a Switch on %2 or newer.")
		    .arg(need, 2, 16, QChar('0')).arg(firmwareForKey(need)).arg(have, 2, 16, QChar('0')).arg(firmwareForKey(have));
	const QString installed = coreFiles(switch2 ? profileDir() + "data/eglantine/" : dataDir()).value("version").toString();
	if (!installed.isEmpty() && keyForFirmware(QVersionNumber::fromString(installed)) < need)
		return QCoreApplication::translate("SwitchCore", "This game needs Switch system software %1 or newer; %2 is installed. "
		                                                 "Put a newer firmware folder or .zip in Downloads and run Add Games, or import it in Settings → Switch Emulation.")
		    .arg(firmwareForKey(need), installed);
	return {};
}

QVariantMap SwitchCore::installedConsoleFiles()
{
	QVariantMap eden = coreFiles(dataDir());   // what Switch games use; Eglantine's alongside
	eden["eglantine"] = coreFiles(profileDir() + "data/eglantine/");
	return eden;
}

QVariantMap SwitchCore::findConsoleFiles(const QStringList& folders)
{
	QStringList keys, titles, firmware;
	const QString home = QDir::homePath(), skip = profileDir();
	for (const QString& dir : folders)
		collectConsoleFiles(dir, 8, keys, titles, firmware, skip);
	if (folders.isEmpty())
		for (const auto& [dir, depth] : {std::pair{home + "/Downloads", 6}, {home + "/Desktop", 4}, {home + "/Documents", 4}, {home, 1},
		                                 {home + "/.var/app", 5}})
			collectConsoleFiles(dir, depth, keys, titles, firmware, skip);
	// other Switch emulators keep keys in <data>/<name>/keys and system software in <data>/<name>/nand/system/Contents/registered
	for (const QString& base : {home + "/.local/share", home + "/.config", qEnvironmentVariable("APPDATA"), qEnvironmentVariable("LOCALAPPDATA")})
		for (const char* name : {"eden", "yuzu", "suyu", "sudachi", "citron", "torzu", "Ryujinx", "eglantine"})
			if (!base.isEmpty() && folders.isEmpty())
				collectConsoleFiles(base + '/' + name, 4, keys, titles, firmware, skip);

	QString bestKeys;
	int master = -1;
	for (const QString& k : keys)
		if (const int m = highestMasterKey(k); m > master)
			master = m, bestKeys = k;
	QString titleKeys = titles.contains(QFileInfo(bestKeys).absolutePath() + "/title.keys") ? QFileInfo(bestKeys).absolutePath() + "/title.keys"
	                                                                                        : titles.value(0);
	// the newest system software the keys can open (newer needs a master key they don't have)
	const QVariantMap have = installedConsoleFiles();
	const int usable = std::max({master, have.value("master").toInt(), have.value("eglantine").toMap().value("master").toInt()});
	QString bestFirmware;
	QVersionNumber best;
	QStringList skipped;
	for (const QString& f : firmware)
	{
		const QVersionNumber v = versionInName(QFileInfo(f).fileName());
		if (!v.isNull() && keyForFirmware(v) > usable)
		{
			if (!skipped.contains(v.toString()))
				skipped << v.toString();
			continue;
		}
		const bool better = bestFirmware.isEmpty() || v > best || (v == best && bestFirmware.endsWith(".zip") && !f.endsWith(".zip"));
		if (better)
			bestFirmware = f, best = v;
	}
	return {{"keys", bestKeys}, {"master", master}, {"titleKeys", titleKeys},
	        {"firmware", bestFirmware}, {"version", best.isNull() ? QString() : best.toString()}, {"skipped", skipped}};
}

QString SwitchCore::installConsoleFiles(const QVariantMap& found, QString* error)
{
	bool keys = false, titles = false, system = false;
	auto done = [&] {
		QStringList did;
		if (keys) did << QCoreApplication::translate("SwitchCore", "console keys (up to master key %1)").arg(found.value("master").toInt(), 2, 16, QChar('0'));
		if (titles) did << QCoreApplication::translate("SwitchCore", "title keys");
		if (system) did << (found.value("version").toString().isEmpty() ? QCoreApplication::translate("SwitchCore", "system software")
		                                                               : QCoreApplication::translate("SwitchCore", "system software %1").arg(found.value("version").toString()));
		return did.join(", ");
	};
	auto fail = [&](const QString& message) { if (error) *error = message; return done(); };
	const QString firmware = found.value("firmware").toString();
	const QVersionNumber version = QVersionNumber::fromString(found.value("version").toString());
	QTemporaryDir unpacked;
	QString source;   // the firmware's .nca folder, once needed (a .zip is unpacked once for both cores)
	// each core on its own: one may have keys or system software the other lacks
	for (const QString& core : {dataDir(), profileDir() + "data/eglantine/"})
	{
		const QVariantMap have = coreFiles(core);
		const bool newKeys = found.value("master").toInt() >= 0 && found.value("master").toInt() > have.value("master").toInt();
		if (newKeys)
		{
			if (!copyFile(found.value("keys").toString(), core + "keys/prod.keys"))
				return fail(QCoreApplication::translate("SwitchCore", "Couldn't save the console keys."));
			keys = true;
		}
		if (const QString t = found.value("titleKeys").toString(); !t.isEmpty() && (newKeys || !have.value("titleKeys").toBool()) && copyFile(t, core + "keys/title.keys"))
			titles = true;
		// installed system software of unknown version (put there by hand or by the emulator) is kept: it may be newer
		const QVersionNumber installed = QVersionNumber::fromString(have.value("version").toString());
		if (firmware.isEmpty() || (have.value("firmware").toBool() && (installed.isNull() || version.isNull() || version <= installed)))
			continue;
		if (source.isEmpty())
		{
			source = firmware;
			if (firmware.endsWith(".zip", Qt::CaseInsensitive))
			{
				if (!unpacked.isValid() || !unzipTo(firmware, unpacked.path()))
					return fail(QCoreApplication::translate("SwitchCore", "Couldn't unpack %1.").arg(QFileInfo(firmware).fileName()));
				source = unpacked.path();
			}
		}
		const QString registered = core + "nand/system/Contents/registered/";
		QDir(registered).removeRecursively();   // one version only: leftovers of an older one would mix in
		if (!copyFirmware(source, registered))
			return fail(QCoreApplication::translate("SwitchCore", "Couldn't copy the system software. Check free space."));
		QSaveFile v(core + "firmware-version.txt");
		if (v.open(QIODevice::WriteOnly))
		{
			v.write(version.toString().toLatin1());
			v.commit();
		}
		system = true;
	}
	return done();
}

QString SwitchCore::importKeys(const QString& path)
{
	QFile f(path);
	if (!f.open(QIODevice::ReadOnly) || f.size() > 1024 * 1024)
		return QCoreApplication::translate("SwitchCore", "Couldn't read the key file.");
	const QByteArray text = f.readAll();
	static const QRegularExpression key("(?m)^\\s*(?:master_key_[0-9a-f]{2}|header_key)\\s*=\\s*[0-9a-fA-F]{32,64}\\s*$");
	if (!key.match(QString::fromLatin1(text)).hasMatch())
		return QCoreApplication::translate("SwitchCore", "This doesn't look like a prod.keys dump. A blank template cannot run games.");
	for (const QString& core : {dataDir(), profileDir() + "data/eglantine/"})
	{
		if (!copyFile(path, core + "keys/prod.keys"))
			return QCoreApplication::translate("SwitchCore", "Couldn't save the key file.");
		if (const QString titles = QFileInfo(path).absolutePath() + "/title.keys"; QFileInfo(titles).isFile())
			copyFile(titles, core + "keys/title.keys");
	}
	return {};
}

QString SwitchCore::importFirmware(const QString& path)
{
	if (path.endsWith(".zip", Qt::CaseInsensitive) || QDir(path).entryList({"*.nca"}, QDir::Files).size() >= 40)
	{
		QString error;
		const QString did = installConsoleFiles({{"firmware", path}, {"version", versionInName(QFileInfo(path).fileName()).toString()},
		                                         {"master", -1}}, &error);
		return !error.isEmpty() ? error : did.isEmpty() ? QCoreApplication::translate("SwitchCore", "That system software is already installed.") : QString();
	}
	QDir source(path);
	const auto files = source.entryInfoList({"*.nca"}, QDir::Files | QDir::NoSymLinks);
	if (files.isEmpty())
		return QCoreApplication::translate("SwitchCore", "Choose an extracted firmware folder containing .nca files.");
	const QString target = dataDir() + "nand/system/Contents/registered/";
	for (const auto& file : files)
	{
		if (!copyFile(file.absoluteFilePath(), target + file.fileName()))
			return QCoreApplication::translate("SwitchCore", "Couldn't copy the firmware. Check free space and folder permissions.");
	}
	return {};
}

QMap<QString, QString> SwitchCore::library()
{
	QFile f(profileDir() + "games.json");
	QMap<QString, QString> out;
	if (f.open(QIODevice::ReadOnly))
	{
		const auto object = QJsonDocument::fromJson(f.readAll()).object();
		for (auto it = object.begin(); it != object.end(); ++it)
			if (it.value().isString())
				out[it.key()] = it.value().toString();
	}
	return out;
}

bool SwitchCore::saveLibrary(const QMap<QString, QString>& games)
{
	QJsonObject object;
	for (auto it = games.cbegin(); it != games.cend(); ++it)
		object[it.key()] = it.value();
	QSaveFile f(profileDir() + "games.json");
	const auto bytes = QJsonDocument(object).toJson();
	return QDir().mkpath(profileDir()) && f.open(QIODevice::WriteOnly) && f.write(bytes) == bytes.size() && f.commit();
}
