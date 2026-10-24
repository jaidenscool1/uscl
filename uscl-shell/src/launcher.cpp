#include "launcher.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QTimer>

Launcher::Launcher(QObject* parent)
	: QObject(parent)
{
	m_process.setProcessChannelMode(QProcess::MergedChannels);
	connect(&m_process, &QProcess::readyRead, this, [this] {
		for (const QByteArray& line : m_process.readAll().split('\n'))
		{
			if (!line.trimmed().isEmpty())
				m_tail << QString::fromUtf8(line.trimmed());
		}
		while (m_tail.size() > 30)
			m_tail.removeFirst();
	});
	connect(&m_process, &QProcess::finished, this, [this](int code, QProcess::ExitStatus status) {
		const bool crashed = !m_stopping && (status == QProcess::CrashExit || code != 0);
		m_stopping = false;
		emit runningChanged();
		emit finished(crashed, m_tail.join('\n'));
	});
	connect(&m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
		if (error == QProcess::FailedToStart)
		{
			emit runningChanged();
			emit finished(true, tr("Could not start %1").arg(corePath()));
		}
	});
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

bool Launcher::coreFound()
{
	return QFileInfo(corePath()).isExecutable();
}

bool Launcher::start(const QStringList& args, const QString& title)
{
	if (running())
		return false;
	m_tail.clear();
	m_title = title;
	m_stopping = false;
	m_process.start(corePath(), args);
	emit runningChanged();
	return true;
}

bool Launcher::startGame(const QString& path, const QString& title)
{
	return start({"--no-gui", "--fullscreen", path}, title);
}

bool Launcher::openClassic()
{
	return start({}, tr("All Settings"));
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
