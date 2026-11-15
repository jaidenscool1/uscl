#pragma once

#include <QProcessEnvironment>
#include <QVariantMap>

class SwitchCore
{
public:
	static bool isGame(const QString& path);
	static bool isSerial(const QString& serial);
	static QVariantMap describe(const QString& path);
	static QString profileDir();
	static QString dataDir();
	static bool isSwitch2(const QString& path);
	static QString executable(bool settings = false, const QString& game = {});
	static QString roomExecutable();
	static QProcessEnvironment environment();
	static QStringList arguments(const QString& path, bool online, const QString& nickname);
	static QString prepareLaunchConfig();
	static QString prerequisiteError(const QString& path = {});
	static QString importKeys(const QString& path);
	static QString importFirmware(const QString& path);
	static QMap<QString, QString> library();
	static bool saveLibrary(const QMap<QString, QString>& games);
};
