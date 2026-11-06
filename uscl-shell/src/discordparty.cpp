#include "discordparty.h"

#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>
#include <QSettings>
#include <QStandardPaths>
#include <QUrl>
#include <QtEndian>

#ifdef USCL_DISCORD
#define DISCORDPP_IMPLEMENTATION
#include <discordpp.h>
#endif

// rpc*: drive the Discord app over its local socket; guilds: list the servers. Changing these asks players to link again.
static const char* const scopes = "identify guilds rpc rpc.voice.read rpc.voice.write";

enum : int { op_handshake = 0, op_frame = 1, op_close = 2, op_ping = 3, op_pong = 4 };

QByteArray DiscordParty::frame(int op, const QJsonObject& body)
{
	const QByteArray json = QJsonDocument(body).toJson(QJsonDocument::Compact);
	QByteArray out(8, 0);
	qToLittleEndian<quint32>(op, out.data());
	qToLittleEndian<quint32>(json.size(), out.data() + 4);
	return out + json;
}

QList<QPair<int, QJsonObject>> DiscordParty::takeFrames(QByteArray& buf)
{
	QList<QPair<int, QJsonObject>> out;
	while (buf.size() >= 8)
	{
		const int op = qFromLittleEndian<quint32>(buf.constData());
		const qsizetype len = qFromLittleEndian<quint32>(buf.constData() + 4);
		if (buf.size() < 8 + len)
			break;
		out.append({op, QJsonDocument::fromJson(buf.mid(8, len)).object()});
		buf.remove(0, 8 + len);
	}
	return out;
}

// where the Discord app listens: a named pipe on Windows; a socket in the runtime/temp folder elsewhere (also the
// Flatpak and Snap Discords' folders)
static QStringList ipcNames()
{
	QStringList names;
#ifdef Q_OS_WIN
	// only pipes that exist: trying absent ones would block the menu
	for (int i = 0; i < 10; i++)
		if (QFileInfo::exists(QString("//./pipe/discord-ipc-%1").arg(i)))
			names << QString("discord-ipc-%1").arg(i);
#else
	QStringList dirs;
	for (const char* var : {"XDG_RUNTIME_DIR", "TMPDIR", "TMP", "TEMP"})
		if (qEnvironmentVariableIsSet(var))
			dirs << qEnvironmentVariable(var);
	dirs << "/tmp";
	for (const QString& dir : dirs)
		for (const char* sub : {"", "/app/com.discordapp.Discord", "/.flatpak/com.discordapp.Discord/xdg-run", "/snap.discord"})
			for (int i = 0; i < 10; i++)
				if (const QString p = QString("%1%2/discord-ipc-%3").arg(dir, sub).arg(i); QFileInfo::exists(p))
					names << p;
#endif
	return names;
}

#ifdef USCL_DISCORD
struct DiscordParty::Sdk
{
	std::shared_ptr<discordpp::Client> client = std::make_shared<discordpp::Client>();
	QTimer pump;
};
bool DiscordParty::available()
{
	return true;
}
#else // built without the Discord Social SDK
struct DiscordParty::Sdk
{
};
bool DiscordParty::available()
{
	return false;
}
#endif

DiscordParty::DiscordParty(QObject* parent)
	: QObject(parent), m_sdk(std::make_unique<Sdk>())
{
	if (!available())
		return;
#ifdef USCL_DISCORD
	// the SDK hands sign-in results back on our thread when we pump it
	connect(&m_sdk->pump, &QTimer::timeout, this, [] { discordpp::RunCallbacks(); });
	m_sdk->pump.start(30);
	m_sdk->client->SetApplicationId(application_id);
#endif
	connect(&m_ipc, &QLocalSocket::readyRead, this, [this] {
		m_buf += m_ipc.readAll();
		for (const auto& [op, msg] : takeFrames(m_buf))
			onFrame(op, msg);
	});
	connect(&m_ipc, &QLocalSocket::disconnected, this, [this] {
		m_pending.clear();
		m_voice_id.clear();
		m_members.clear();
		emit voiceChanged();
		if (linked() && m_state != "error")
			setState("noapp");
	});
	// Discord started or restarted later: look for it again
	m_retry.setInterval(5000);
	connect(&m_retry, &QTimer::timeout, this, &DiscordParty::tryConnect);

	if (linked())
	{
		setState("connecting", tr("Connecting to Discord..."));
		refreshToken([this] { m_retry.start(); tryConnect(); });
	}
}

DiscordParty::~DiscordParty() = default;

bool DiscordParty::linked() const
{
	return QSettings().value("discord/scopes").toString() == scopes && !QSettings().value("discord/refresh").toString().isEmpty();
}

void DiscordParty::link()
{
#ifdef USCL_DISCORD
	auto client = m_sdk->client;
	auto verifier = client->CreateAuthorizationCodeVerifier();
	discordpp::AuthorizationArgs args;
	args.SetClientId(application_id);
	args.SetScopes(scopes);
	args.SetCodeChallenge(verifier.Challenge());
	setState("linking", tr("Finish signing in to Discord in your browser..."));
	client->Authorize(args, [this, client, verifier](discordpp::ClientResult result, std::string code, std::string redirect) {
		if (!result.Successful())
		{
			setState("off", tr("Discord wasn't linked: %1").arg(QString::fromStdString(result.Error())));
			return;
		}
		client->GetToken(application_id, code, verifier.Verifier(), redirect,
		                 [this](discordpp::ClientResult result, std::string access, std::string refresh, discordpp::AuthorizationTokenType, int32_t, std::string) {
			if (!result.Successful())
			{
				setState("off", tr("Discord sign-in failed: %1").arg(QString::fromStdString(result.Error())));
				return;
			}
			saveTokens(QString::fromStdString(access), QString::fromStdString(refresh));
			setState("connecting", tr("Connecting to Discord..."));
			m_ipc.abort();
			m_retry.start();
			tryConnect();
		});
	});
#endif
}

void DiscordParty::refreshToken(std::function<void()> then)
{
#ifdef USCL_DISCORD
	const QString refresh = QSettings().value("discord/refresh").toString();
	m_sdk->client->RefreshToken(application_id, refresh.toStdString(),
	                            [this, then](discordpp::ClientResult result, std::string access, std::string refresh_token, discordpp::AuthorizationTokenType, int32_t, std::string) {
		if (!result.Successful())
		{
			setState("error", tr("Discord sign-in expired. Link your account again."));
			return;
		}
		saveTokens(QString::fromStdString(access), QString::fromStdString(refresh_token));
		then();
	});
#else
	Q_UNUSED(then)
#endif
}

void DiscordParty::unlink()
{
	m_retry.stop();
	m_ipc.abort();
	m_servers.clear();
	m_channels.clear();
	emit serversChanged();
	QSettings().remove("discord");
	setState("off");
}

void DiscordParty::startDiscord()
{
#ifdef Q_OS_WIN
	QProcess::startDetached(qEnvironmentVariable("LOCALAPPDATA") + "/Discord/Update.exe", {"--processStart", "Discord.exe"});
#elif defined(Q_OS_MACOS)
	QProcess::startDetached("open", {"-a", "Discord"});
#else
	if (!QStandardPaths::findExecutable("discord").isEmpty())
		QProcess::startDetached("discord", {"--start-minimized"});
	else
		QProcess::startDetached("flatpak", {"run", "com.discordapp.Discord", "--start-minimized"});
#endif
}

void DiscordParty::tryConnect()
{
	if (!linked() || m_ipc.state() != QLocalSocket::UnconnectedState)
		return;
	for (const QString& name : ipcNames())
	{
		m_ipc.connectToServer(name);
		if (m_ipc.waitForConnected(300))
		{
			m_buf.clear();
			m_ipc.write(frame(op_handshake, {{"v", 1}, {"client_id", QString::number(application_id)}}));
			setState("connecting", tr("Connecting to Discord..."));
			return;
		}
		m_ipc.abort();
	}
	if (m_state != "error")
		setState("noapp");
}

void DiscordParty::command(const QString& cmd, const QJsonObject& args, Reply reply, const QString& evt)
{
	const QString nonce = QString::number(++m_nonce);
	if (reply)
		m_pending[nonce] = reply;
	QJsonObject msg{{"cmd", cmd}, {"args", args}, {"nonce", nonce}};
	if (!evt.isEmpty())
		msg["evt"] = evt;
	m_ipc.write(frame(op_frame, msg));
}

void DiscordParty::onFrame(int op, const QJsonObject& msg)
{
	if (op == op_ping)
	{
		m_ipc.write(frame(op_pong, msg));
		return;
	}
	if (op == op_close)
	{
		// Discord refused us (wrong app, or it's shutting down)
		setState("error", tr("Discord closed the connection: %1").arg(msg.value("message").toString()));
		m_ipc.abort();
		return;
	}
	const QString nonce = msg.value("nonce").toString();
	const QJsonObject data = msg.value("data").toObject();
	if (!nonce.isEmpty() && msg.value("cmd").toString() != "DISPATCH")
	{
		const Reply reply = m_pending.take(nonce);
		if (reply)
			reply(data, msg.value("evt").toString() == "ERROR" ? data.value("message").toString() : QString());
		return;
	}
	const QString evt = msg.value("evt").toString();
	if (evt == "READY")
		authenticate(false);
	else if (evt == "VOICE_CHANNEL_SELECT")
		loadSelectedVoice();
	else if (evt == "VOICE_STATE_CREATE" || evt == "VOICE_STATE_UPDATE")
		upsertMember(data);
	else if (evt == "VOICE_STATE_DELETE")
	{
		const QString id = data.value("user").toObject().value("id").toString();
		m_members.removeIf([&](const QVariantMap& m) { return m.value("id") == id; });
		emit voiceChanged();
	}
	else if (evt == "SPEAKING_START" || evt == "SPEAKING_STOP")
	{
		for (QVariantMap& m : m_members)
			if (m.value("id") == data.value("user_id").toString())
				m["speaking"] = evt == "SPEAKING_START";
		emit voiceChanged();
	}
	else if (evt == "VOICE_SETTINGS_UPDATE")
	{
		m_muted = data.value("mute").toBool();
		m_deafened = data.value("deaf").toBool();
		emit changed();
	}
}

void DiscordParty::authenticate(bool retried)
{
	command("AUTHENTICATE", {{"access_token", QSettings().value("discord/access").toString()}}, [this, retried](const QJsonObject& data, const QString& error) {
		if (!error.isEmpty())
		{
			// the token may just have expired since we started: a fresh one, once
			if (!retried)
				refreshToken([this] { authenticate(true); });
			else
				setState("error", tr("Discord didn't accept USCL: %1").arg(error));
			return;
		}
		const QJsonObject user = data.value("user").toObject();
		m_my_id = user.value("id").toString();
		m_my_name = user.value("global_name").toString();
		if (m_my_name.isEmpty())
			m_my_name = user.value("username").toString();
		setState("ready");
		command("SUBSCRIBE", {}, {}, "VOICE_CHANNEL_SELECT");
		command("SUBSCRIBE", {}, {}, "VOICE_SETTINGS_UPDATE");
		command("GET_VOICE_SETTINGS", {}, [this](const QJsonObject& s, const QString&) {
			m_muted = s.value("mute").toBool();
			m_deafened = s.value("deaf").toBool();
			emit changed();
		});
		refreshServers();
		loadSelectedVoice();
	});
}

void DiscordParty::loadSelectedVoice()
{
	command("GET_SELECTED_VOICE_CHANNEL", {}, [this](const QJsonObject& channel, const QString&) { setVoiceChannel(channel); });
}

void DiscordParty::setVoiceChannel(const QJsonObject& channel)
{
	const QString id = channel.value("id").toString();
	if (id != m_voice_id)
	{
		// follow the new channel's voice events instead of the old one's
		for (const char* evt : {"VOICE_STATE_CREATE", "VOICE_STATE_UPDATE", "VOICE_STATE_DELETE", "SPEAKING_START", "SPEAKING_STOP"})
		{
			if (!m_voice_id.isEmpty())
				command("UNSUBSCRIBE", {{"channel_id", m_voice_id}}, {}, evt);
			if (!id.isEmpty())
				command("SUBSCRIBE", {{"channel_id", id}}, {}, evt);
		}
	}
	m_voice_id = id;
	m_voice_name = channel.value("name").toString();
	m_voice_guild = channel.value("guild_id").toString();
	// Discord doesn't say which servers were used last, so we remember the ones we see you in
	if (!m_voice_guild.isEmpty())
	{
		QSettings s;
		QStringList recent = s.value("discord/recentServers").toStringList();
		recent.removeAll(m_voice_guild);
		recent.prepend(m_voice_guild);
		s.setValue("discord/recentServers", recent.mid(0, 50));
	}
	sortServers();
	m_members.clear();
	for (const QJsonValue v : channel.value("voice_states").toArray())
		upsertMember(v.toObject());
	emit voiceChanged();
}

QVariantMap DiscordParty::memberFrom(const QJsonObject& state) const
{
	// state: Discord's voice state; its own mute/volume are ours for that person, voice_state's are theirs
	const QJsonObject user = state.value("user").toObject();
	const QJsonObject vs = state.value("voice_state").toObject();
	const QString id = user.value("id").toString();
	QString name = state.value("nick").toString();
	if (name.isEmpty())
		name = user.value("global_name").toString();
	if (name.isEmpty())
		name = user.value("username").toString();
	const QString avatar = user.value("avatar").toString();
	return {{"id", id}, {"name", name}, {"me", id == m_my_id},
	        {"avatar", avatar.isEmpty() ? QString() : QString("https://cdn.discordapp.com/avatars/%1/%2.png?size=128").arg(id, avatar)},
	        {"muted", vs.value("self_mute").toBool() || vs.value("mute").toBool()},
	        {"deafened", vs.value("self_deaf").toBool() || vs.value("deaf").toBool()},
	        {"localMuted", state.value("mute").toBool()}, {"volume", qRound(state.value("volume").toDouble(100))}, {"speaking", false}};
}

void DiscordParty::upsertMember(const QJsonObject& state)
{
	QVariantMap m = memberFrom(state);
	const QString id = m.value("id").toString();
	for (QVariantMap& old : m_members)
		if (old.value("id") == id)
		{
			m["speaking"] = old.value("speaking");
			old = m;
			emit voiceChanged();
			return;
		}
	m_members << m;
	emit voiceChanged();
}

QVariantMap DiscordParty::voice() const
{
	if (m_voice_id.isEmpty())
		return {};
	QString server;
	for (const QVariant& s : m_servers)
		if (s.toMap().value("id") == m_voice_guild)
			server = s.toMap().value("name").toString();
	QVariantList members;
	for (const QVariantMap& m : m_members)
		members << m;
	return {{"id", m_voice_id}, {"name", m_voice_name}, {"server", server}, {"members", members}};
}

void DiscordParty::refreshServers()
{
	if (m_state != "ready")
		return;
	command("GET_GUILDS", {}, [this](const QJsonObject& data, const QString& error) {
		if (!error.isEmpty())
			m_message = tr("Couldn't load your Discord servers: %1").arg(error);
		m_servers.clear();
		for (const QJsonValue g : data.value("guilds").toArray())
			m_servers << QVariantMap{{"id", g["id"].toString()}, {"name", g["name"].toString()}, {"icon", g["icon_url"].toString()}};
		sortServers();
		emit voiceChanged(); // the server name of the voice chat we're in
		emit changed();
	});
}

void DiscordParty::loadChannels(const QString& server)
{
	m_channels_wanted = server;
	m_channels_server.clear();
	m_channels.clear();
	emit serversChanged();
	command("GET_CHANNELS", {{"guild_id", server}}, [this, server](const QJsonObject& data, const QString& error) {
		if (server != m_channels_wanted)
			return;
		m_channels.clear();
		if (!error.isEmpty())
			m_channels << QVariantMap{{"error", error}};
		for (const QJsonValue c : data.value("channels").toArray())
			if (c["type"].toInt() == 2 || c["type"].toInt() == 13) // voice, stage
				m_channels << QVariantMap{{"id", c["id"].toString()}, {"name", c["name"].toString()}, {"members", QVariantList()}};
		m_channels_server = server;
		emit serversChanged();
		// who's in each: the channel list doesn't say
		for (const QVariant& c : std::as_const(m_channels))
		{
			const QString id = c.toMap().value("id").toString();
			command("GET_CHANNEL", {{"channel_id", id}}, [this, server, id](const QJsonObject& ch, const QString&) {
				if (server != m_channels_server)
					return;
				QVariantList members;
				for (const QJsonValue v : ch.value("voice_states").toArray())
					members << memberFrom(v.toObject());
				for (QVariant& c : m_channels)
					if (c.toMap().value("id") == id)
					{
						QVariantMap m = c.toMap();
						m["members"] = members;
						c = m;
					}
				emit serversChanged();
			});
		}
	});
}

void DiscordParty::joinVoice(const QString& channel)
{
	command("SELECT_VOICE_CHANNEL", {{"channel_id", channel}, {"force", true}}, [this](const QJsonObject& ch, const QString& error) {
		if (!error.isEmpty())
			setState(m_state, tr("Couldn't join: %1").arg(error));
		else
			setVoiceChannel(ch);
	});
}

void DiscordParty::leaveVoice()
{
	command("SELECT_VOICE_CHANNEL", {{"channel_id", QJsonValue::Null}}, [this](const QJsonObject&, const QString&) { setVoiceChannel({}); });
}

void DiscordParty::setMuted(bool muted)
{
	command("SET_VOICE_SETTINGS", {{"mute", muted}});
}

void DiscordParty::setDeafened(bool deafened)
{
	command("SET_VOICE_SETTINGS", {{"deaf", deafened}});
}

void DiscordParty::setUserVolume(const QString& user, int volume)
{
	command("SET_USER_VOICE_SETTINGS", {{"user_id", user}, {"volume", qBound(0, volume, 200)}});
	updateMember(user, "volume", qBound(0, volume, 200));
}

void DiscordParty::setUserMuted(const QString& user, bool muted)
{
	command("SET_USER_VOICE_SETTINGS", {{"user_id", user}, {"mute", muted}});
	updateMember(user, "localMuted", muted);
}

void DiscordParty::openProfile(const QString& user)
{
	QDesktopServices::openUrl(QUrl("discord://-/users/" + user));
}

// shows a change of ours at once everywhere that person is listed (Discord only sends events for our own voice chat)
void DiscordParty::updateMember(const QString& user, const QString& key, const QVariant& value)
{
	for (QVariantMap& m : m_members)
		if (m.value("id") == user)
			m[key] = value;
	for (QVariant& c : m_channels)
	{
		QVariantMap ch = c.toMap();
		QVariantList members = ch.value("members").toList();
		for (QVariant& m : members)
			if (m.toMap().value("id") == user)
			{
				QVariantMap mm = m.toMap();
				mm[key] = value;
				m = mm;
			}
		ch["members"] = members;
		c = ch;
	}
	emit voiceChanged();
	emit serversChanged();
}

QVariantList DiscordParty::sortedServers(QVariantList servers, const QString& current, const QStringList& recent)
{
	// the one you're in, then most recent first, then the rest in Discord's own order
	auto rank = [&](const QVariant& s) {
		const QString id = s.toMap().value("id").toString();
		if (id == current)
			return -1;
		const qsizetype i = recent.indexOf(id);
		return i < 0 ? int(recent.size()) : int(i);
	};
	std::stable_sort(servers.begin(), servers.end(), [&](const QVariant& a, const QVariant& b) { return rank(a) < rank(b); });
	return servers;
}

void DiscordParty::sortServers()
{
	m_servers = sortedServers(m_servers, m_voice_guild, QSettings().value("discord/recentServers").toStringList());
	emit serversChanged();
}

void DiscordParty::saveTokens(const QString& access, const QString& refresh)
{
	// shortcut: kept in the app's settings like other preferences; move to the OS keychain if that matters
	QSettings s;
	s.setValue("discord/access", access);
	s.setValue("discord/refresh", refresh);
	s.setValue("discord/scopes", scopes);
	emit changed();
}

void DiscordParty::setState(const QString& state, const QString& message)
{
	m_state = state;
	m_message = message;
	emit changed();
}
