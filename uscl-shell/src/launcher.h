#pragma once

#include <QObject>
#include <QProcess>
#include <QStringList>

// Starts the emulator core as its own process, so a game crash brings the player back home instead of
// taking the home screen down with it. PS3 uses RPCS3; Switch uses an external Eden frontend.
class Launcher : public QObject
{
	Q_OBJECT
	Q_PROPERTY(bool running READ running NOTIFY runningChanged)
	Q_PROPERTY(QString title READ title NOTIFY runningChanged)
	// a running game's window is on screen (false while the home screen is shown over it)
	Q_PROPERTY(bool gameShown READ gameShown NOTIFY gameShownChanged)

public:
	explicit Launcher(QObject* parent = nullptr);

	bool running() const { return m_process.state() != QProcess::NotRunning; }
	QString title() const { return m_title; }
	bool gameShown() const { return running() && m_shown; }

	Q_INVOKABLE bool startGame(const QString& path, const QString& title, bool online, const QString& console = "PS3", const QString& nickname = "USCLPlayer");
	// Wii->Dolphin, 3DS->Azahar, ... netplay "host"/"join" (Wii, our Dolphin): a NetPlay session over the Steam tunnel
	Q_INVOKABLE bool startEmu(const QString& path, const QString& title, const QString& console, const QString& netplay = {},
	                          const QString& nickname = {}, int players = 0);
	Q_INVOKABLE static bool ownDolphinFound() { return !ownDolphin().isEmpty(); }
	static QString ownDolphin();
	static QString dolphinConfigDir();
	Q_INVOKABLE bool openSwitchSettings();
	Q_INVOKABLE static QString switchStatus(const QString& path = {});
	Q_INVOKABLE static QString importSwitchKeys(const QString& path);
	Q_INVOKABLE static QString importSwitchFirmware(const QString& path);
	Q_INVOKABLE static QString switchProfile();
	Q_INVOKABLE static QString configureSwitchTool(const QString& kind, const QString& path);
	Q_INVOKABLE bool openClassic();
	Q_INVOKABLE void stop();
	// PS button in a game (uscl-rpcs3 0008): the game keeps running, its window steps aside for the home screen
	Q_INVOKABLE void showHome();
	Q_INVOKABLE void resume();
	// Gives RPCS3 a player per connected controller (SDL names, same library as ours). A player that already
	// uses a controller only gets its device updated, so custom button mappings stay.
	// Automatic Controller Setup: every connected controller becomes a player of the game about to start
	Q_INVOKABLE bool prepareInput(const QStringList& pads, const QString& console = "PS3");
	static QString inputYml(const QString& current, const QStringList& pads);
	Q_INVOKABLE static bool coreFound();
	Q_INVOKABLE static bool ps4CoreFound();
	static QString corePath();
	static QString ps4CorePath(); // our shadPS4, for PS4 games

signals:
	void runningChanged();
	void gameShownChanged();
	// the player pressed PS in the game
	void homeRequested();
	// crashed: the core didn't exit cleanly; details: the end of its output, for the error screen
	void finished(bool crashed, const QString& details);

private:
	bool start(const QStringList& args, const QString& title, bool online, bool game, bool ps4 = false, const QString& core = {});
	void command(const char* line, bool shown);

	QProcess m_process;
	QString m_title;
	QStringList m_tail;
	bool m_stopping = false;
	bool m_shown = true;
	bool m_auto_pads = false; // the next Switch launch maps the controllers (uscl-eden 0012)
};
