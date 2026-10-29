#pragma once

#include <QHash>
#include <QJsonObject>
#include <QLocalSocket>
#include <QObject>
#include <QTimer>
#include <QVariantList>
#include <functional>
#include <memory>

// Discord from the home screen: the Discord app on this PC, driven over its local RPC socket (discord-ipc-N), so the
// Party column shows the voice chat you're in, who's there and who's talking, your servers and their open voice
// chats, and joins, leaves, mutes and deafens like Discord itself. Nothing of our own: no lobbies, no calls.
// Linking (once, in the browser) uses the Discord Social SDK's sign-in; its token, with Discord's rpc scopes, then
// signs in the RPC connection, and is refreshed on start so later it connects by itself. Discord grants the rpc
// scopes only to the app's owner and testers (developer portal → App Testers) until it approves the app.
// Without the SDK in the build (USCL_DISCORD_SDK unset), available is false and nothing else happens.
class DiscordParty : public QObject
{
	Q_OBJECT
	Q_PROPERTY(bool available READ available CONSTANT)
	// off (not linked), linking, noapp (Discord isn't running), connecting, ready, error
	Q_PROPERTY(QString state READ state NOTIFY changed)
	Q_PROPERTY(QString message READ message NOTIFY changed)
	Q_PROPERTY(bool linked READ linked NOTIFY changed)
	Q_PROPERTY(QString myName READ myName NOTIFY changed)
	Q_PROPERTY(bool muted READ muted NOTIFY changed)
	Q_PROPERTY(bool deafened READ deafened NOTIFY changed)
	// the voice chat you're in: {id, name, server, members}; empty: none. A member: {id, name, avatar, speaking, muted
	// (by themselves or the server), deafened, localMuted (muted for you only), volume (0-200, for you only), me}
	Q_PROPERTY(QVariantMap voice READ voice NOTIFY voiceChanged)
	// your Discord servers: [{id, name, icon}], the one you're in first, then the ones you were in most recently
	Q_PROPERTY(QVariantList servers READ servers NOTIFY serversChanged)
	// voice channels of channelsServer, set once loaded (loadChannels): [{id, name, members}] or [{error}]
	Q_PROPERTY(QVariantList channels READ channels NOTIFY serversChanged)
	Q_PROPERTY(QString channelsServer READ channelsServer NOTIFY serversChanged)

public:
	explicit DiscordParty(QObject* parent = nullptr);
	~DiscordParty() override;

	static bool available();
	QString state() const { return m_state; }
	QString message() const { return m_message; }
	bool linked() const;
	QString myName() const { return m_my_name; }
	bool muted() const { return m_muted; }
	bool deafened() const { return m_deafened; }
	QVariantMap voice() const;
	QVariantList servers() const { return m_servers; }
	QVariantList channels() const { return m_channels; }
	QString channelsServer() const { return m_channels_server; }

	// opens Discord's authorize page in the browser, then connects to the Discord app
	Q_INVOKABLE void link();
	Q_INVOKABLE void unlink();
	// starts the Discord app (it connects once Discord is up)
	Q_INVOKABLE static void startDiscord();
	Q_INVOKABLE void refreshServers();
	Q_INVOKABLE void loadChannels(const QString& server);
	Q_INVOKABLE void joinVoice(const QString& channel);
	Q_INVOKABLE void leaveVoice();
	Q_INVOKABLE void setMuted(bool muted);
	Q_INVOKABLE void setDeafened(bool deafened);
	// what Discord's right-click on someone in a voice chat offers that its RPC allows: your volume and mute for
	// them, and their profile (in the Discord app)
	Q_INVOKABLE void setUserVolume(const QString& user, int volume);
	Q_INVOKABLE void setUserMuted(const QString& user, bool muted);
	Q_INVOKABLE static void openProfile(const QString& user);

	static constexpr quint64 application_id = 1563994423862231191ULL;

	// Exposed for the self-test: one RPC frame, and the frames whole in buf (taken out of it)
	static QByteArray frame(int op, const QJsonObject& body);
	static QList<QPair<int, QJsonObject>> takeFrames(QByteArray& buf);
	// the one you're in, then the recent ones (most recent first), then the rest in Discord's order
	static QVariantList sortedServers(QVariantList servers, const QString& current, const QStringList& recent);

signals:
	void changed();
	void voiceChanged();
	void serversChanged();

private:
	struct Sdk;
	using Reply = std::function<void(const QJsonObject& data, const QString& error)>;
	void setState(const QString& state, const QString& message = {});
	void saveTokens(const QString& access, const QString& refresh);
	void refreshToken(std::function<void()> then);
	void tryConnect();
	void onFrame(int op, const QJsonObject& msg);
	void command(const QString& cmd, const QJsonObject& args, Reply reply = {}, const QString& evt = {});
	void authenticate(bool retried);
	void loadSelectedVoice();
	void setVoiceChannel(const QJsonObject& channel);
	void upsertMember(const QJsonObject& state);
	QVariantMap memberFrom(const QJsonObject& state) const;
	void updateMember(const QString& user, const QString& key, const QVariant& value);
	void sortServers();

	std::unique_ptr<Sdk> m_sdk;
	QLocalSocket m_ipc;
	QByteArray m_buf;
	QTimer m_retry;
	QHash<QString, Reply> m_pending;
	quint64 m_nonce = 0;
	QString m_state = "off", m_message, m_my_name, m_my_id, m_channels_server, m_channels_wanted;
	bool m_muted = false, m_deafened = false;
	// the voice chat we're in
	QString m_voice_id, m_voice_name, m_voice_guild;
	QList<QVariantMap> m_members;
	QVariantList m_servers, m_channels;
};
