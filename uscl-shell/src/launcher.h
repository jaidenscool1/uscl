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

public:
	explicit Launcher(QObject* parent = nullptr);

	bool running() const { return m_process.state() != QProcess::NotRunning; }
	QString title() const { return m_title; }

	Q_INVOKABLE bool startGame(const QString& path, const QString& title);
	Q_INVOKABLE bool openClassic();
	Q_INVOKABLE void stop();
	Q_INVOKABLE static bool coreFound();
	static QString corePath();

signals:
	void runningChanged();
	// crashed: the core didn't exit cleanly; details: the end of its output, for the error screen
	void finished(bool crashed, const QString& details);

private:
	bool start(const QStringList& args, const QString& title);

	QProcess m_process;
	QString m_title;
	QStringList m_tail;
	bool m_stopping = false;
};
