#pragma once

#include <QObject>
#include <QProcess>
#include <QStringList>

// Starts the emulator core as its own process, so a game crash brings the player back home instead of
// taking the home screen down with it. Today that's our RPCS3 build, next to this executable.
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

	Q_INVOKABLE bool startGame(const QString& path, const QString& title, bool online);
	Q_INVOKABLE bool openClassic();
	Q_INVOKABLE void stop();
	// PS button in a game (uscl-rpcs3 0008): the game keeps running, its window steps aside for the home screen
	Q_INVOKABLE void showHome();
	Q_INVOKABLE void resume();
	// Gives RPCS3 a player per connected controller (SDL names, same library as ours). A player that already
	// uses a controller only gets its device updated, so custom button mappings stay.
	Q_INVOKABLE static bool prepareInput(const QStringList& pads);
	static QString inputYml(const QString& current, const QStringList& pads);
	Q_INVOKABLE static bool coreFound();
	static QString corePath();

signals:
	void runningChanged();
	void gameShownChanged();
	// the player pressed PS in the game
	void homeRequested();
	// crashed: the core didn't exit cleanly; details: the end of its output, for the error screen
	void finished(bool crashed, const QString& details);

private:
	bool start(const QStringList& args, const QString& title, bool online, bool game);
	void command(const char* line, bool shown);

	QProcess m_process;
	QString m_title;
	QStringList m_tail;
	bool m_stopping = false;
	bool m_shown = true;
};
