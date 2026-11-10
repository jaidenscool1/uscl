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
#include <QtEndian>

namespace
{
	QString findTool(const QString& variable, const QString& name)
	{
		if (qEnvironmentVariableIsSet(variable.toLatin1().constData()))
			return qEnvironmentVariable(variable.toLatin1().constData());
		const QString configured = QSettings().value("switch/" + variable).toString();
		if (!configured.isEmpty())
			return configured;
#ifdef _WIN32
		const QString file = name + ".exe";
#else
		const QString file = name;
#endif
		const QString bundled = QCoreApplication::applicationDirPath() + '/' + file;
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
#endif
#ifdef _WIN32
	env.insert("APPDATA", profileDir() + "data");
#endif
	return env;
}

QString SwitchCore::executable(bool settings)
{
	return findTool(settings ? "USCL_SWITCH_GUI" : "USCL_SWITCH_CORE", settings ? "eden" : "eden-cli");
}

QString SwitchCore::roomExecutable()
{
	return findTool("USCL_SWITCH_ROOM", "eden-room");
}

bool SwitchCore::isGame(const QString& path)
{
	const QString ext = QFileInfo(path).suffix().toLower();
	if (ext != "nsp" && ext != "xci" && ext != "nro")
		return false;
	QFile f(path);
	if (!f.open(QIODevice::ReadOnly))
		return false;
	if (ext == "nsp")
	{
		const auto header = f.read(16);
		if (header.size() != 16 || !header.startsWith("PFS0"))
			return false;
		const auto count = qFromLittleEndian<quint32>(header.constData() + 4);
		const auto strings = qFromLittleEndian<quint32>(header.constData() + 8);
		return count > 0 && count <= 65536 && strings <= 16 * 1024 * 1024 && 16 + quint64(count) * 24 + strings <= quint64(f.size());
	}
	if (ext == "xci")
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
	QString title = QFileInfo(path).completeBaseName();
	static const QRegularExpression id("\\[([0-9a-fA-F]{16})\\]");
	const auto match = id.match(title);
	QString serial = match.hasMatch() ? match.captured(1).toUpper() : QString();
	// Update and DLC containers are installed in Eden, not launched as games.
	if (!serial.isEmpty() && (serial.toULongLong(nullptr, 16) & 0x1fff) != 0)
		return {};
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
#ifdef _WIN32
	const QString config = dataDir() + "config/qt-config.ini";
#else
	const QString config = profileDir() + "config/eden/qt-config.ini";
#endif
	QStringList args{"--config", config, "--fullscreen"};
	if (executable().endsWith(".AppImage", Qt::CaseInsensitive))
		args.prepend("eden-cli");
	if (online)
	{
		QString name = nickname;
		name.replace(QRegularExpression("[^A-Za-z0-9_. -]"), "_");
		if (name.size() < 4)
			name = "USCL_" + name;
		args << "--multiplayer" << name.left(20) + "@127.0.0.1:24872";
	}
	return args << "--game" << QFileInfo(path).absoluteFilePath();
}

QString SwitchCore::prerequisiteError(const QString& path)
{
	if (!QFileInfo(executable()).isExecutable())
		return QCoreApplication::translate("SwitchCore", "The Switch emulator (eden-cli) is missing. Configure it in Settings → Switch Emulation.");
#ifdef _WIN32
	if (QFileInfo(QFileInfo(executable()).absolutePath() + "/user").isDir())
		return QCoreApplication::translate("SwitchCore", "Use an Eden installation without a portable user folder so USCL can keep a separate Switch profile.");
#endif
	if (QFileInfo(path).suffix().compare("nro", Qt::CaseInsensitive) == 0)
		return {};
	if (!QFileInfo(dataDir() + "keys/prod.keys").isFile())
		return QCoreApplication::translate("SwitchCore", "Import prod.keys dumped from your Switch in Settings → Switch Emulation.");
	return {};
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
	return copyFile(path, dataDir() + "keys/prod.keys") ? QString() : QCoreApplication::translate("SwitchCore", "Couldn't save the key file.");
}

QString SwitchCore::importFirmware(const QString& path)
{
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
