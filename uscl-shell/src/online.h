#pragma once

#include <QObject>
#include <QProcess>
#include <QVariantList>

#include <array>

// Online play from the home screen: sets RPCS3 up for USCL, runs our servers and the Steam session tunnel
// (uscl-rpcn, dw-server, uscl-bridge, all next to this executable), lists Steam friends and tests the setup.
class Online : public QObject
{
	Q_OBJECT
	// off, starting, waiting (for a Steam invite), hosting, joined
	Q_PROPERTY(QString state READ state NOTIFY stateChanged)
	Q_PROPERTY(QString session READ session NOTIFY stateChanged)
	Q_PROPERTY(QString game READ game NOTIFY stateChanged)
	Q_PROPERTY(QString message READ message NOTIFY stateChanged)
	Q_PROPERTY(QVariantList friends READ friends NOTIFY friendsChanged)
	Q_PROPERTY(bool friendsLoading READ friendsLoading NOTIFY friendsChanged)
	Q_PROPERTY(QString friendsError READ friendsError NOTIFY friendsChanged)
	Q_PROPERTY(QVariantList tests READ tests NOTIFY testsChanged)
	Q_PROPERTY(QString steamName READ steamName CONSTANT)
	// everyone in the session: [{id (Steam ID), name, me}]
	Q_PROPERTY(QVariantList players READ players NOTIFY stateChanged)
	// joined: the host's game, "<serial> <gameHash>" (empty: the host didn't say)
	Q_PROPERTY(QString hostGame READ hostGame NOTIFY stateChanged)
	// players banned from sessions this PC hosts: [{id, name}]
	Q_PROPERTY(QVariantList bans READ bans NOTIFY bansChanged)

public:
	explicit Online(QObject* parent = nullptr);
	~Online() override;

	QString state() const { return m_state; }
	QString session() const { return m_session; }
	QString game() const { return m_game; }
	QString message() const { return m_message; }
	QVariantList friends() const { return m_friends; }
	bool friendsLoading() const { return m_friends_proc.state() != QProcess::NotRunning; }
	QString friendsError() const { return m_friends_error; }
	QVariantList tests() const { return m_tests; }
	QString steamName() const;
	QVariantList players() const { return m_players; }
	QString hostGame() const { return m_host_game; }
	QVariantList bans() const;

	// Online name + RPCS3 network settings (RPCN on the session tunnel, Demonware names to our server)
	Q_INVOKABLE bool setup(const QString& name);
	Q_INVOKABLE static bool validName(const QString& name);
	// serial: the game the session is for (picks which servers run); title is shown in the menu
	Q_INVOKABLE void host(const QString& serial, const QString& title, const QString& path);
	// empty session: wait for a Steam invite (or the one session a friend is in)
	Q_INVOKABLE void join(const QString& session);
	Q_INVOKABLE void stop();
	Q_INVOKABLE void refreshFriends();
	Q_INVOKABLE void testConnection();
	// Starts Steam in the tray (-silent: no window) when it isn't running, so sessions and friends work without
	// the Steam window popping up over the home screen
	Q_INVOKABLE static void startSteam();
	Q_INVOKABLE static bool steamRunning();
	// games that go online by themselves when started, like on a PS3 (each in the player's own session)
	Q_INVOKABLE static bool supports(const QString& serial);
	// host: take a player out of this session (kick) or out of every session this PC hosts (ban); bans are
	// keyed by Steam ID, so a new name doesn't get around them. The bridge applies both within 2 seconds.
	Q_INVOKABLE void kick(const QString& id, const QString& name);
	Q_INVOKABLE void ban(const QString& id, const QString& name);
	Q_INVOKABLE void unban(const QString& id);

	// Exposed for the self-test
	static QString sanitizeName(const QString& persona);
	static QString personaFromVdf(const QByteArray& vdf);
	static QString setYamlValue(const QString& yml, const QString& section, const QString& key, const QString& value);
	static QString yamlValue(const QString& yml, const QString& section, const QString& key);
	// Sets up shadPS4's portable folder (it uses ./user when that exists) for a PS4 game: settings and the USCL
	// game patches from the templates, player 1 signed in to shadNet as this player. Returns the folder.
	static QString preparePs4();

signals:
	void stateChanged();
	void friendsChanged();
	void testsChanged();
	void bansChanged();
	// something the host should see now, e.g. a player removed by the anticheat
	void notice(const QString& text);

private:
	enum service { rpcn, demonware, bridge, shadnet };
	bool start(service s, const QString& tool, const QStringList& args, const QString& dir, const QProcessEnvironment& env);
	void setState(const QString& state, const QString& message = {});
	void setTest(int i, const QString& result, const QString& detail);
	static QString toolPath(const QString& name);
	static QString dataDir();
	static QString templateDir();
	static QString rpcs3File(const QString& name);
	static QProcessEnvironment carryEnv(QProcessEnvironment env);

	std::array<QProcess*, 4> m_services{};
	QString m_state = "off", m_session, m_game, m_message, m_host_game;
	QVariantList m_players;
	QProcess m_friends_proc;
	QVariantList m_friends;
	QString m_friends_error;
	QVariantList m_tests;
	QProcess m_test_proc;
};
