#include "launcher.h"
#include "games.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMap>
#include <QTimer>

Launcher::Launcher(QObject* parent)
	: QObject(parent)
{
	m_process.setProcessChannelMode(QProcess::MergedChannels);
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

bool Launcher::start(const QStringList& args, const QString& title, bool online, bool game)
{
	if (running())
		return false;
	// in an online session RPCS3 runs the game unmodified: no game patches, debugger or logpoints (uscl-rpcs3 0006)
	QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
	if (online)
		env.insert("USCL_ONLINE", "1");
	else
		env.remove("USCL_ONLINE");
	// a game's PS button comes to us (uscl-rpcs3 0008); the classic window keeps RPCS3's own home menu
	if (game)
		env.insert("USCL_SHELL", "1");
	else
		env.remove("USCL_SHELL");
	m_process.setProcessEnvironment(env);
	m_tail.clear();
	m_title = title;
	m_stopping = false;
	m_shown = true;
	m_process.start(corePath(), args);
	emit runningChanged();
	emit gameShownChanged();
	return true;
}

bool Launcher::startGame(const QString& path, const QString& title, bool online)
{
	return start({"--no-gui", "--fullscreen", path}, title, online, true);
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

bool Launcher::prepareInput(const QStringList& pads)
{
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
