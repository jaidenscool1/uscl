#include "discordparty.h"

#include <QSettings>

#ifdef USCL_DISCORD
#define DISCORDPP_IMPLEMENTATION
#include <discordpp.h>
#endif

#ifdef USCL_DISCORD

struct DiscordParty::Sdk
{
	std::shared_ptr<discordpp::Client> client = std::make_shared<discordpp::Client>();
};

bool DiscordParty::available()
{
	return true;
}

DiscordParty::DiscordParty(QObject* parent)
	: QObject(parent), m_sdk(std::make_unique<Sdk>())
{
	// the SDK hands results back on our thread when we pump it
	connect(&m_pump, &QTimer::timeout, this, [] { discordpp::RunCallbacks(); });
	m_pump.start(30);

	auto& c = *m_sdk->client;
	c.SetApplicationId(application_id);
	c.SetStatusChangedCallback([this](discordpp::Client::Status status, discordpp::Client::Error, int32_t) {
		if (status == discordpp::Client::Status::Ready)
		{
			m_my_name = QString::fromStdString((m_sdk->client->GetCurrentUserV2() ? m_sdk->client->GetCurrentUserV2()->DisplayName() : std::string()));
			setState("ready");
			joinParty();
		}
		else if (status == discordpp::Client::Status::Disconnected && m_state == "ready")
		{
			m_lobby = 0;
			setState("connecting", tr("Reconnecting to Discord..."));
		}
	});
	c.SetLobbyMemberAddedCallback([this](uint64_t lobby, uint64_t) { if (lobby == m_lobby) refreshMembers(); });
	c.SetLobbyMemberRemovedCallback([this](uint64_t lobby, uint64_t) { if (lobby == m_lobby) refreshMembers(); });

	// linked before: a new access token from the kept refresh token, then connect
	const QString refresh = QSettings().value("discord/refresh").toString();
	if (!refresh.isEmpty())
	{
		setState("connecting", tr("Connecting to Discord..."));
		c.RefreshToken(application_id, refresh.toStdString(),
		               [this](discordpp::ClientResult result, std::string access, std::string refresh_token, discordpp::AuthorizationTokenType, int32_t, std::string) {
			if (!result.Successful())
			{
				setState("error", tr("Discord sign-in expired. Link your account again."));
				return;
			}
			saveTokens(QString::fromStdString(access), QString::fromStdString(refresh_token));
			connectWithToken(QString::fromStdString(access));
		});
	}
}

DiscordParty::~DiscordParty()
{
	if (m_sdk && m_lobby)
		m_sdk->client->EndCalls([] {});
}

void DiscordParty::link()
{
	auto client = m_sdk->client;
	auto verifier = client->CreateAuthorizationCodeVerifier();
	discordpp::AuthorizationArgs args;
	args.SetClientId(application_id);
	args.SetScopes(discordpp::Client::GetDefaultCommunicationScopes());
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
				setState("error", tr("Discord sign-in failed: %1").arg(QString::fromStdString(result.Error())));
				return;
			}
			saveTokens(QString::fromStdString(access), QString::fromStdString(refresh));
			connectWithToken(QString::fromStdString(access));
		});
	});
}

void DiscordParty::connectWithToken(const QString& access)
{
	setState("connecting", tr("Connecting to Discord..."));
	m_sdk->client->UpdateToken(discordpp::AuthorizationTokenType::Bearer, access.toStdString(), [this](discordpp::ClientResult result) {
		if (result.Successful())
			m_sdk->client->Connect();
		else
			setState("error", tr("Discord sign-in failed: %1").arg(QString::fromStdString(result.Error())));
	});
}

void DiscordParty::joinParty()
{
	if (m_state != "ready" || m_session.isEmpty() || m_lobby)
		return;
	// every member of a USCL session derives the same secret from its id, so they all land in one lobby
	m_sdk->client->CreateOrJoinLobby(("uscl-party-" + m_session).toStdString(), [this](discordpp::ClientResult result, uint64_t lobby) {
		if (!result.Successful())
		{
			setState("ready", tr("Couldn't start the party: %1").arg(QString::fromStdString(result.Error())));
			return;
		}
		m_lobby = lobby;
		auto call = m_sdk->client->StartCall(lobby);
		call.SetSelfMute(m_muted);
		call.SetSpeakingStatusChangedCallback([this](uint64_t user, bool speaking) {
			m_speaking[user] = speaking;
			refreshMembers();
		});
		emit changed();
		refreshMembers();
	});
}

void DiscordParty::leaveParty()
{
	if (!m_lobby)
		return;
	const quint64 lobby = m_lobby;
	m_lobby = 0;
	m_speaking.clear();
	m_sdk->client->EndCall(lobby, [] {});
	m_sdk->client->LeaveLobby(lobby, [](discordpp::ClientResult) {});
	m_members.clear();
	emit changed();
	emit membersChanged();
}

void DiscordParty::refreshMembers()
{
	m_members.clear();
	if (auto lobby = m_sdk->client->GetLobbyHandle(m_lobby))
	{
		const quint64 me = m_sdk->client->GetCurrentUserV2() ? m_sdk->client->GetCurrentUserV2()->Id() : 0;
		for (uint64_t id : lobby->LobbyMemberIds())
		{
			QVariantMap m{{"id", QString::number(id)}, {"speaking", m_speaking.value(id)}, {"me", id == me}};
			if (auto user = m_sdk->client->GetUser(id))
			{
				m["name"] = QString::fromStdString(user->DisplayName());
				m["avatar"] = QString::fromStdString(user->AvatarUrl(discordpp::UserHandle::AvatarType::Png, discordpp::UserHandle::AvatarType::Png));
			}
			m_members << m;
		}
	}
	emit membersChanged();
}

void DiscordParty::setMuted(bool muted)
{
	m_muted = muted;
	m_sdk->client->SetSelfMuteAll(muted);
	emit changed();
}

#else // built without the Discord Social SDK

struct DiscordParty::Sdk
{
};

bool DiscordParty::available()
{
	return false;
}

DiscordParty::DiscordParty(QObject* parent)
	: QObject(parent)
{
}

DiscordParty::~DiscordParty() = default;
void DiscordParty::link() {}
void DiscordParty::connectWithToken(const QString&) {}
void DiscordParty::joinParty() {}
void DiscordParty::leaveParty() {}
void DiscordParty::refreshMembers() {}
void DiscordParty::setMuted(bool) {}

#endif

bool DiscordParty::linked() const
{
	return !QSettings().value("discord/refresh").toString().isEmpty();
}

void DiscordParty::unlink()
{
	leaveParty();
	QSettings().remove("discord");
	setState("off");
}

void DiscordParty::setSession(const QString& session)
{
	if (session == m_session)
		return;
	leaveParty();
	m_session = session;
	joinParty();
}

void DiscordParty::saveTokens(const QString& access, const QString& refresh)
{
	// shortcut: kept in the app's settings like other preferences; move to the OS keychain if that matters
	QSettings s;
	s.setValue("discord/refresh", refresh);
	Q_UNUSED(access)
	emit changed();
}

void DiscordParty::setState(const QString& state, const QString& message)
{
	m_state = state;
	m_message = message;
	emit changed();
}
