#pragma once

#include <QObject>
#include <QTimer>
#include <QVariantList>
#include <memory>

// Party chat over Discord (the Discord Social SDK), like the PS5's party: everyone in the same USCL session is put
// in one Discord lobby with a voice call. Players link their Discord account once (the browser opens Discord's
// authorize page); the tokens are kept and refreshed, so later it connects by itself. Without the SDK in the build
// (USCL_DISCORD_SDK unset), available is false and nothing else happens.
class DiscordParty : public QObject
{
	Q_OBJECT
	Q_PROPERTY(bool available READ available CONSTANT)
	// off, linking, connecting, ready, error
	Q_PROPERTY(QString state READ state NOTIFY changed)
	Q_PROPERTY(QString message READ message NOTIFY changed)
	Q_PROPERTY(bool linked READ linked NOTIFY changed)
	Q_PROPERTY(bool inParty READ inParty NOTIFY changed)
	Q_PROPERTY(bool muted READ muted NOTIFY changed)
	Q_PROPERTY(QString myName READ myName NOTIFY changed)
	// [{id, name, avatar, speaking, me}]
	Q_PROPERTY(QVariantList members READ members NOTIFY membersChanged)

public:
	explicit DiscordParty(QObject* parent = nullptr);
	~DiscordParty() override;

	static bool available();
	QString state() const { return m_state; }
	QString message() const { return m_message; }
	bool linked() const;
	bool inParty() const { return m_lobby != 0; }
	bool muted() const { return m_muted; }
	QString myName() const { return m_my_name; }
	QVariantList members() const { return m_members; }

	// opens Discord's authorize page in the browser, then connects
	Q_INVOKABLE void link();
	Q_INVOKABLE void unlink();
	// the party for this USCL session (its id); an empty id leaves the party
	Q_INVOKABLE void setSession(const QString& session);
	Q_INVOKABLE void setMuted(bool muted);

	static constexpr quint64 application_id = 1563994423862231191ULL;

signals:
	void changed();
	void membersChanged();

private:
	struct Sdk;
	void setState(const QString& state, const QString& message = {});
	void connectWithToken(const QString& access);
	void saveTokens(const QString& access, const QString& refresh);
	void joinParty();
	void leaveParty();
	void refreshMembers();

	std::unique_ptr<Sdk> m_sdk;
	QTimer m_pump;
	QString m_state = "off", m_message, m_my_name, m_session;
	quint64 m_lobby = 0;
	bool m_muted = false;
	QVariantList m_members;
	QMap<quint64, bool> m_speaking;
};
