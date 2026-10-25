#include "online.h"
#include "games.h"

#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QSettings>
#include <QTcpServer>
#include <QUdpSocket>

#include <algorithm>

namespace
{
	// Our Demonware serves auth, lobby and NAT discovery on 127.0.0.1 (uscl-dw/README.md)
	const QString swap_list =
		"ops2-ps3-auth.prod.demonware.net=127.0.0.1&&ops2-ps3-lobby.prod.demonware.net=127.0.0.1"
		"&&cod7-stun.us.demonware.net=127.0.0.1&&cod7-stun.eu.demonware.net=127.0.0.1"
		"&&cod7-stun.jp.demonware.net=127.0.0.1&&cod7-stun.au.demonware.net=127.0.0.1&&stun.us.demonware.net=127.0.0.1"
		// Modern Warfare 2
		"&&mw2-ps3-auth.mmp3.demonware.net=127.0.0.1&&mw2-ps3-lsg.live.mmp3.demonware.net=127.0.0.1"
		"&&mw2-stun.us.demonware.net=127.0.0.1&&mw2-stun.eu.demonware.net=127.0.0.1"
		"&&auth1.ps3.iw4.iwnet.infinityward.com=127.0.0.1&&blob1.ps3.iw4.iwnet.infinityward.com=127.0.0.1"
		"&&match1.ps3.iw4.iwnet.infinityward.com=127.0.0.1&&ip1.ps3.iw4.iwnet.infinityward.com=127.0.0.1"
		"&&log1.ps3.iw4.iwnet.infinityward.com=127.0.0.1&&team1.ps3.iw4.iwnet.infinityward.com=127.0.0.1"
		"&&web1.ps3.iw4.iwnet.infinityward.com=127.0.0.1";

	struct preset
	{
		const char* bridge; // uscl-bridge host <preset>
		bool demonware;
	};

	// Games that need more than RPCN: which ports the session carries and whether our Demonware runs
	preset presetFor(const QString& serial)
	{
		static const QStringList bo2 = {"BLUS31011", "BLES01717", "BLES01718", "BLES01719", "BLES01720"};
		static const QStringList mw2 = {"BLUS30377", "BLES00683", "BLES00684", "BLES00685", "BLES00686", "BLES00687"};
		static const QStringList skate3 = {"BLUS30464", "BLES00760"};
		if (bo2.contains(serial) || mw2.contains(serial)) // the same uscl-dw ports
			return {"bo2", true};
		if (skate3.contains(serial))
			return {"skate3", false};
		return {"", false};
	}

	void copyMissing(const QString& from, const QString& to)
	{
		QDirIterator it(from, QDir::Files, QDirIterator::Subdirectories);
		while (it.hasNext())
		{
			const QString src = it.next();
			const QString dst = to + "/" + QDir(from).relativeFilePath(src);
			if (!QFileInfo::exists(dst))
			{
				QDir().mkpath(QFileInfo(dst).absolutePath());
				QFile::copy(src, dst);
			}
		}
	}

	QString readFile(const QString& path)
	{
		QFile f(path);
		return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString();
	}

	bool writeFile(const QString& path, const QString& text)
	{
		QDir().mkpath(QFileInfo(path).absolutePath());
		QFile f(path);
		return f.open(QIODevice::WriteOnly | QIODevice::Truncate) && f.write(text.toUtf8()) >= 0;
	}

	QString quoted(const QString& value)
	{
		static const QRegularExpression plain("^[A-Za-z0-9_./ -]*$");
		if (plain.match(value).hasMatch() && !value.startsWith(' ') && !value.endsWith(' '))
			return value;
		QString v = value;
		return '"' + v.replace("\\", "\\\\").replace("\"", "\\\"") + '"';
	}
}

Online::Online(QObject* parent)
	: QObject(parent)
{
	connect(&m_friends_proc, &QProcess::finished, this, [this](int code) {
		const QByteArray out = m_friends_proc.readAllStandardOutput();
		const QJsonDocument doc = QJsonDocument::fromJson(out.mid(out.indexOf('[')));
		m_friends.clear();
		m_friends_error.clear();
		if (code != 0 || !doc.isArray())
		{
			m_friends_error = tr("Couldn't reach Steam. Is Steam running and signed in?");
		}
		else
		{
			// friends in a USCL session first, then by name
			QVariantList in_session, others;
			for (const QJsonValue& v : doc.array())
				(v["session"].isString() ? in_session : others).push_back(v.toObject().toVariantMap());
			const auto by_name = [](const QVariant& a, const QVariant& b) {
				return QString::localeAwareCompare(a.toMap()["name"].toString(), b.toMap()["name"].toString()) < 0;
			};
			std::sort(in_session.begin(), in_session.end(), by_name);
			std::sort(others.begin(), others.end(), by_name);
			m_friends = in_session + others;
		}
		emit friendsChanged();
	});
}

Online::~Online()
{
	stop();
	for (QProcess* p : m_services)
	{
		if (p)
			p->waitForFinished(3000);
	}
}

QString Online::toolPath(const QString& name)
{
	// USCL_TOOLS: where the servers are when running from a build folder
	const QString dir = qEnvironmentVariable("USCL_TOOLS", QCoreApplication::applicationDirPath());
#ifdef _WIN32
	return dir + "/" + name + ".exe";
#else
	return dir + "/" + name;
#endif
}

QString Online::dataDir()
{
	return GameModel::rpcs3ConfigDir() + "uscl";
}

QString Online::templateDir()
{
	// Linux packages: usr/bin + usr/share/uscl. Windows: uscl-data next to the executable.
	for (const QString& dir : {QCoreApplication::applicationDirPath() + "/../share/uscl", QCoreApplication::applicationDirPath() + "/uscl-data"})
	{
		if (QFileInfo(dir).isDir())
			return dir;
	}
	return {};
}

QString Online::rpcs3File(const QString& name)
{
#ifdef _WIN32
	return GameModel::rpcs3ConfigDir() + "config/" + name;
#else
	return GameModel::rpcs3ConfigDir() + name;
#endif
}

// ---- names ----

bool Online::validName(const QString& name)
{
	static const QRegularExpression re("^[A-Za-z0-9_-]{3,16}$");
	return re.match(name).hasMatch();
}

QString Online::sanitizeName(const QString& persona)
{
	// RPCN names: 3-16 of A-Z a-z 0-9 - _
	QString out;
	for (const QChar c : persona.trimmed())
	{
		if (c.isSpace())
			out += '_';
		else if ((c.unicode() < 128 && c.isLetterOrNumber()) || c == '-' || c == '_')
			out += c;
	}
	out.replace(QRegularExpression("_+"), "_");
	out = out.left(16);
	while (out.endsWith('_') && out.size() > 3)
		out.chop(1);
	if (out.size() < 3)
		out = (out + "Player").left(16);
	return out;
}

QString Online::personaFromVdf(const QByteArray& vdf)
{
	// loginusers.vdf: "users" { "<steamid>" { "PersonaName" "..." "MostRecent" "1" "timestamp" "..." } ... }
	static const QRegularExpression user(R"re("\d+"\s*\{([^{}]*)\})re");
	static const QRegularExpression field(R"re("(\w+)"\s+"((?:[^"\\]|\\.)*)")re");
	QString best;
	qint64 best_time = -1;
	auto it = user.globalMatch(QString::fromUtf8(vdf));
	while (it.hasNext())
	{
		QString name;
		qint64 time = 0;
		bool recent = false;
		auto f = field.globalMatch(it.next().captured(1));
		while (f.hasNext())
		{
			const auto m = f.next();
			const QString key = m.captured(1).toLower();
			if (key == "personaname")
				name = m.captured(2).replace("\\\"", "\"").replace("\\\\", "\\");
			else if (key == "timestamp")
				time = m.captured(2).toLongLong();
			else if (key == "mostrecent")
				recent = m.captured(2) == "1";
		}
		if (recent)
			time = std::numeric_limits<qint64>::max();
		if (!name.isEmpty() && time > best_time)
		{
			best = name;
			best_time = time;
		}
	}
	return best;
}

QString Online::steamName() const
{
	QStringList candidates;
#ifdef _WIN32
	const QString steam = QSettings("HKEY_CURRENT_USER\\Software\\Valve\\Steam", QSettings::NativeFormat).value("SteamPath").toString();
	if (!steam.isEmpty())
		candidates << steam + "/config/loginusers.vdf";
	candidates << "C:/Program Files (x86)/Steam/config/loginusers.vdf";
#else
	candidates << QDir::homePath() + "/.local/share/Steam/config/loginusers.vdf" << QDir::homePath() + "/.steam/steam/config/loginusers.vdf"
	           << QDir::homePath() + "/.var/app/com.valvesoftware.Steam/data/Steam/config/loginusers.vdf";
#endif
	for (const QString& path : candidates)
	{
		QFile f(path);
		if (f.open(QIODevice::ReadOnly))
		{
			const QString persona = personaFromVdf(f.readAll());
			if (!persona.isEmpty())
				return sanitizeName(persona);
		}
	}
	return {};
}

// ---- RPCS3 settings ----

QString Online::yamlValue(const QString& yml, const QString& section, const QString& key)
{
	const QStringList lines = yml.split('\n');
	const QString indent = section.isEmpty() ? QString() : QStringLiteral("  ");
	bool in_section = section.isEmpty();
	for (const QString& line : lines)
	{
		if (!section.isEmpty() && !line.startsWith(' ') && !line.isEmpty())
			in_section = line == section + ":";
		if (in_section && line.startsWith(indent + key + ":") && (indent.isEmpty() || !line.startsWith(indent + " ")))
		{
			QString v = line.mid(indent.size() + key.size() + 1).trimmed();
			if (v.size() >= 2 && v.startsWith('"') && v.endsWith('"'))
				v = v.mid(1, v.size() - 2).replace("\\\"", "\"").replace("\\\\", "\\");
			return v;
		}
	}
	return {};
}

// Sets one key of RPCS3's YAML (top level, or inside a top-level section), keeping every other line as it is
QString Online::setYamlValue(const QString& yml, const QString& section, const QString& key, const QString& value)
{
	QStringList lines = yml.split('\n');
	if (!lines.isEmpty() && lines.last().isEmpty())
		lines.removeLast();
	const QString indent = section.isEmpty() ? QString() : QStringLiteral("  ");
	const QString entry = indent + key + ": " + quoted(value);

	qsizetype begin = 0, end = lines.size();
	if (!section.isEmpty())
	{
		begin = lines.indexOf(section + ":");
		if (begin < 0)
		{
			lines << section + ":" << entry;
			return lines.join('\n') + '\n';
		}
		begin++;
		end = begin;
		while (end < lines.size() && (lines[end].startsWith(' ') || lines[end].isEmpty()))
			end++;
	}
	for (qsizetype i = begin; i < end; i++)
	{
		if (lines[i].startsWith(indent + key + ":") && (indent.isEmpty() || !lines[i].startsWith(indent + " ")))
		{
			lines[i] = entry;
			return lines.join('\n') + '\n';
		}
	}
	lines.insert(end, entry);
	return lines.join('\n') + '\n';
}

bool Online::setup(const QString& name)
{
	if (!validName(name))
		return false;

	QString cfg = readFile(rpcs3File("config.yml"));
	cfg = setYamlValue(cfg, "Net", "Internet enabled", "Connected");
	cfg = setYamlValue(cfg, "Net", "PSN status", "RPCN");
	cfg = setYamlValue(cfg, "Net", "Bind address", "0.0.0.0");
	cfg = setYamlValue(cfg, "Net", "IP swap list", swap_list);

	// RPCN account on the session's server (127.0.0.1, through the Steam session); created on first login
	QString rpcn = readFile(rpcs3File("rpcn.yml"));
	QString hosts = yamlValue(rpcn, {}, "Hosts");
	if (hosts.isEmpty())
		hosts = "Official RPCN Server|np.rpcs3.net";
	if (!hosts.contains("USCL|127.0.0.1"))
		hosts += "|||USCL|127.0.0.1";
	if (yamlValue(rpcn, {}, "Version").isEmpty())
		rpcn = setYamlValue(rpcn, {}, "Version", "2");
	rpcn = setYamlValue(rpcn, {}, "Host", "127.0.0.1");
	rpcn = setYamlValue(rpcn, {}, "NPID", name);
	if (yamlValue(rpcn, {}, "Password").isEmpty())
		rpcn = setYamlValue(rpcn, {}, "Password", QString::number(QRandomGenerator::system()->generate64(), 36));
	rpcn = setYamlValue(rpcn, {}, "Hosts", hosts);

	return writeFile(rpcs3File("config.yml"), cfg) && writeFile(rpcs3File("rpcn.yml"), rpcn);
}

// ---- sessions ----

void Online::setState(const QString& state, const QString& message)
{
	m_state = state;
	m_message = message;
	if (state == "off" || state == "starting" || state == "waiting")
	{
		m_session.clear();
		m_players.clear();
		m_host_game.clear();
	}
	emit stateChanged();
}

bool Online::start(service s, const QString& tool, const QStringList& args, const QString& dir, const QProcessEnvironment& env)
{
	QProcess*& p = m_services[s];
	if (!p)
	{
		p = new QProcess(this);
		p->setProcessChannelMode(QProcess::MergedChannels);
		connect(p, &QProcess::readyRead, this, [this, p, s] {
			while (p->canReadLine())
			{
				const QString line = QString::fromUtf8(p->readLine()).trimmed();
				// our Demonware's code check (BO2's own anticheat): "anticheat: <name> failed the game code check ..."
				static const QRegularExpression failed(R"(anticheat: (\S+) failed the game code check)");
				if (const auto m = failed.match(line); s == demonware && m.hasMatch())
					emit notice(tr("%1 was removed: their game code doesn't match yours.").arg(m.captured(1)));
				// "anticheat: <name> sent impossible stats (...)": the upload was refused, their saved stats stay
				static const QRegularExpression stats(R"(anticheat: (\S+) sent impossible stats)");
				if (const auto m = stats.match(line); s == demonware && m.hasMatch())
					emit notice(tr("%1 tried to save impossible stats. They were refused.").arg(m.captured(1)));
				if (s != bridge)
					continue;
				static const QRegularExpression re(R"(session (\d+) \((hosting|joined)\))");
				if (const auto m = re.match(line); m.hasMatch())
				{
					m_session = m.captured(1);
					m_state = m.captured(2);
					m_message.clear();
					emit stateChanged();
				}
				else if (line.startsWith("uscl-bridge: players "))
				{
					m_players = QJsonDocument::fromJson(line.mid(21).toUtf8()).array().toVariantList();
					emit stateChanged();
				}
				else if (line.startsWith("uscl-bridge: game "))
				{
					m_host_game = line.mid(18).trimmed();
					emit stateChanged();
				}
				else if (line == "FAIL: the host removed you from the session")
				{
					m_message = tr("The host removed you from the session.");
				}
				else if (line.startsWith("FAIL:") || line.startsWith("Steam init failed") || line.startsWith("bind "))
				{
					m_message = line.startsWith("FAIL:") ? line.mid(5).trimmed() : line;
				}
			}
		});
		connect(p, &QProcess::finished, this, [this, s] {
			if (m_state == "off")
				return;
			// one part stopped on its own: end the session so nothing is left half running
			const QString why = m_message.isEmpty()
				? (s == bridge ? tr("The Steam connection closed.") : s == rpcn ? tr("The online server stopped.") : tr("The Call of Duty server stopped."))
				: m_message;
			stop();
			setState("off", why);
		});
	}
	if (p->state() != QProcess::NotRunning)
		return true;
	p->setWorkingDirectory(dir);
	p->setProcessEnvironment(env);
	p->start(tool, args);
	if (!p->waitForStarted(5000))
	{
		stop();
		setState("off", tr("Couldn't start %1.").arg(QFileInfo(tool).fileName()));
		return false;
	}
	return true;
}

void Online::host(const QString& serial, const QString& title, const QString& path)
{
	if (m_state != "off")
		return;
	const preset p = presetFor(serial);
	const QString data = dataDir();
	if (const QString templates = templateDir(); !templates.isEmpty())
	{
		copyMissing(templates + "/rpcn", data + "/rpcn");
		copyMissing(templates + "/dw", data + "/dw");
	}
	m_game = title;
	setState("starting");

	// RPCN needs a TLS certificate of its own; make one the first time this PC hosts
	if (!QFileInfo::exists(data + "/rpcn/cert.pem"))
	{
		QProcess gen;
		gen.setWorkingDirectory(data + "/rpcn");
		gen.start(toolPath("uscl-rpcn"), {"--cert-gen"});
		if (!gen.waitForFinished(30000) || !QFileInfo::exists(data + "/rpcn/cert.pem"))
		{
			setState("off", tr("Couldn't set up the online server (certificate)."));
			return;
		}
	}

	const QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
	QProcessEnvironment rpcn_env = env;
	rpcn_env.insert("USCL_LOCAL", "1");
	if (!start(rpcn, toolPath("uscl-rpcn"), {}, data + "/rpcn", rpcn_env))
		return;
	if (p.demonware)
	{
		QProcessEnvironment dw_env = env;
		dw_env.insert("DW_ADDR", "127.0.0.1:3074");
		dw_env.insert("DW_STUN_ADDR", "127.0.0.1:3074");
		dw_env.insert("DW_STUN_ALT_ADDR", "127.0.0.1:3077");
		dw_env.insert("DW_STUN_SELF_ADDR", "127.0.0.1:39001"); // the host is slot 1
		dw_env.insert("RUST_LOG", "info");
		// BO2's built-in anticheat: everyone's game gets the same code checks; this PC's answers are the reference
		dw_env.insert("DW_TRUSTED_USER", yamlValue(readFile(rpcs3File("rpcn.yml")), {}, "NPID"));
		if (!start(demonware, toolPath("dw-server"), {}, data + "/dw", dw_env))
			return;
	}
	QStringList args{"host"};
	if (*p.bridge)
		args << p.bridge;
	QProcessEnvironment bridge_env = env;
	QFile::remove(data + "/kicks.txt"); // kicks last one session
	bridge_env.insert("USCL_BANS", data + "/bans.txt");
	bridge_env.insert("USCL_KICKS", data + "/kicks.txt");
	if (!serial.isEmpty())
		bridge_env.insert("USCL_GAME", serial + " " + GameModel::gameHash(path, serial));
	start(bridge, toolPath("uscl-bridge"), args, QCoreApplication::applicationDirPath(), bridge_env);
}

void Online::join(const QString& session)
{
	if (m_state != "off")
		return;
	m_game.clear();
	setState(session.isEmpty() ? "waiting" : "starting");
	QStringList args{"join"};
	if (!session.isEmpty())
		args << session;
	start(bridge, toolPath("uscl-bridge"), args, QCoreApplication::applicationDirPath(), QProcessEnvironment::systemEnvironment());
}

namespace
{
	// "<steam id> <name>" per line
	bool appendLine(const QString& file, const QString& id, const QString& name)
	{
		static const QRegularExpression digits("^\\d{1,20}$");
		if (!digits.match(id).hasMatch())
			return false;
		QDir().mkpath(QFileInfo(file).absolutePath());
		QFile f(file);
		return f.open(QIODevice::Append) && f.write((id + " " + QString(name).simplified() + "\n").toUtf8()) > 0;
	}
}

void Online::kick(const QString& id, const QString& name)
{
	appendLine(dataDir() + "/kicks.txt", id, name);
}

void Online::ban(const QString& id, const QString& name)
{
	const QVariantList current = bans();
	if (std::any_of(current.begin(), current.end(), [&](const QVariant& b) { return b.toMap()["id"] == id; }))
		return;
	if (appendLine(dataDir() + "/bans.txt", id, name))
		emit bansChanged();
}

void Online::unban(const QString& id)
{
	QFile f(dataDir() + "/bans.txt");
	if (!f.open(QIODevice::ReadOnly))
		return;
	QStringList keep;
	for (const QString& line : QString::fromUtf8(f.readAll()).split('\n', Qt::SkipEmptyParts))
	{
		if (line.section(' ', 0, 0) != id)
			keep << line;
	}
	f.close();
	if (f.open(QIODevice::WriteOnly | QIODevice::Truncate))
	{
		f.write((keep.join('\n') + (keep.isEmpty() ? "" : "\n")).toUtf8());
		emit bansChanged();
	}
}

QVariantList Online::bans() const
{
	QVariantList out;
	QFile f(dataDir() + "/bans.txt");
	if (f.open(QIODevice::ReadOnly))
	{
		for (const QString& line : QString::fromUtf8(f.readAll()).split('\n', Qt::SkipEmptyParts))
		{
			if (!line.startsWith('#'))
				out << QVariantMap{{"id", line.section(' ', 0, 0)}, {"name", line.section(' ', 1)}};
		}
	}
	return out;
}

void Online::stop()
{
	const bool was_on = m_state != "off";
	m_state = "off";
	for (QProcess* p : m_services)
	{
		if (p && p->state() != QProcess::NotRunning)
		{
			p->terminate();
			if (!p->waitForFinished(2000))
				p->kill();
		}
	}
	if (was_on)
		setState("off");
}

// ---- friends ----

void Online::refreshFriends()
{
	if (friendsLoading())
		return;
	m_friends_proc.start(toolPath("uscl-bridge"), {"friends"});
	emit friendsChanged();
}

// ---- Test Connection ----

void Online::setTest(int i, const QString& result, const QString& detail)
{
	QVariantMap t = m_tests[i].toMap();
	t["result"] = result;
	t["detail"] = detail;
	m_tests[i] = t;
	emit testsChanged();
}

void Online::testConnection()
{
	if (m_test_proc.state() != QProcess::NotRunning)
		return;
	m_tests.clear();
	for (const QString& name : {tr("USCL files"), tr("Online settings"), tr("Network ports"), tr("Steam")})
		m_tests << QVariantMap{{"name", name}, {"result", "running"}, {"detail", QString()}};
	emit testsChanged();

	// 1. our programs and their data are where they should be
	QStringList missing;
	for (const QString& tool : {"uscl-rpcn", "dw-server", "uscl-bridge"})
	{
		if (!QFileInfo(toolPath(tool)).isExecutable())
			missing << tool;
	}
	if (templateDir().isEmpty())
		missing << tr("server data");
	setTest(0, missing.isEmpty() ? "ok" : "fail", missing.isEmpty() ? tr("Everything is installed") : tr("Missing: %1. Reinstalling USCL fixes this.").arg(missing.join(", ")));

	// 2. RPCS3 is set up for USCL
	const QString cfg = readFile(rpcs3File("config.yml")), rpcn_cfg = readFile(rpcs3File("rpcn.yml"));
	const QString npid = yamlValue(rpcn_cfg, {}, "NPID");
	const bool settings_ok = yamlValue(cfg, "Net", "PSN status") == "RPCN" && yamlValue(cfg, "Net", "Internet enabled") == "Connected" &&
	                         yamlValue(rpcn_cfg, {}, "Host") == "127.0.0.1" && validName(npid);
	setTest(1, settings_ok ? "ok" : "fail", settings_ok ? tr("Online name: %1").arg(npid) : tr("Not set up yet: Settings → Online Name"));

	// 3. nothing else holds the ports a session needs (our own session holds them while it runs)
	if (m_state != "off")
	{
		setTest(2, "ok", tr("In use by your session"));
	}
	else
	{
		QStringList busy;
		for (const quint16 port : {quint16(31313), quint16(31413), quint16(3074)})
		{
			QTcpServer t;
			if (!t.listen(QHostAddress::LocalHost, port))
				busy << QString("TCP %1").arg(port);
		}
		for (const quint16 port : {quint16(3657), quint16(3757), quint16(3074), quint16(3077), quint16(3078)})
		{
			QUdpSocket u;
			if (!u.bind(QHostAddress::LocalHost, port))
				busy << QString("UDP %1").arg(port);
		}
		setTest(2, busy.isEmpty() ? "ok" : "fail", busy.isEmpty() ? tr("Free") : tr("Another program is using %1. Close it (or an old USCL) and try again.").arg(busy.join(", ")));
	}

	// 4. Steam: sign in, create a lobby and find it again (uscl-bridge selftest)
	m_test_proc.disconnect(this);
	connect(&m_test_proc, &QProcess::finished, this, [this](int code) {
		const QString out = QString::fromUtf8(m_test_proc.readAll());
		static const QRegularExpression who("signed in as (.+) \\(\\d+\\)");
		const QString name = who.match(out).captured(1);
		if (code == 0)
			setTest(3, "ok", tr("Signed in as %1; sessions work").arg(name));
		else if (out.contains("Steam init failed"))
			setTest(3, "fail", tr("Steam isn't running or isn't signed in"));
		else
			setTest(3, "fail", tr("Steam is running but sessions failed. Try again in a minute."));
	});
	m_test_proc.setProcessChannelMode(QProcess::MergedChannels);
	m_test_proc.start(toolPath("uscl-bridge"), {"selftest"});
	if (!m_test_proc.waitForStarted(5000))
		setTest(3, "fail", tr("Couldn't start uscl-bridge"));
}
