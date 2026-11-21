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
	static bool needsEglantine(const QString& path);   // Switch 2 games, and .nsz/.xcz, which Eden can't open
	// Console keys and system software: the best ones in these folders, or with none, on this PC (Downloads, Desktop,
	// Documents, other Switch emulators). {keys, master, titleKeys, firmware, version, skipped}
	static QVariantMap findConsoleFiles(const QStringList& folders = {});
	static QVariantMap installedConsoleFiles();
	// Installs what findConsoleFiles found where it beats what's installed, for Eden and Eglantine. Returns what
	// it did ("" when nothing was newer); a failure goes to *error.
	static QString installConsoleFiles(const QVariantMap& found, QString* error = nullptr);
	static QString executable(bool settings = false, const QString& game = {});
	static QString roomExecutable();
	static QProcessEnvironment environment();
	static QStringList arguments(const QString& path, bool online, const QString& nickname);
	static QString prepareLaunchConfig();
	static QString prerequisiteError(const QString& path = {});
	// The game can't start for want of console files a new search might find: no (complete) keys, keys or system
	// software older than the game needs. The home screen searches the PC once more before showing the error.
	static bool consoleFilesMissing(const QString& path);
	// the master key a game's tickets need, -1 if it doesn't say
	static int neededMasterKey(const QString& path);
	static QString importKeys(const QString& path);
	static QString importFirmware(const QString& path);
	static QMap<QString, QString> library();
	static bool saveLibrary(const QMap<QString, QString>& games);
};
