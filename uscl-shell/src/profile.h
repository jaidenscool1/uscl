#pragma once

#include <QElapsedTimer>
#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QSet>
#include <QTimer>
#include <QVariantMap>
#include <QVector>

// The player's USCL profile: play time per game and trophies from every emulator.
// PS3 trophies come from RPCS3 (dev_hdd0/home/00000001/trophy), PS4 trophies from our shadPS4 (user/trophy +
// user/home/<id>/trophy). The other consoles have no trophies of their own, so their games get USCL's six
// play-time trophies (usclTrophies). New unlocks are found by watching the emulator's files while a game runs.
// Friends see a short summary through Steam Rich Presence (presenceKeys, written to presence.json for
// `uscl-bridge presence`).
class Profile : public QObject
{
	Q_OBJECT
	// {seconds, bronze, silver, gold, platinum, earned, total, games: [{serial, title, console, seconds, last, earned, total}]}
	Q_PROPERTY(QVariantMap summary READ summary NOTIFY changed)

public:
	struct Trophy
	{
		int id = 0;
		QString name, detail;
		QChar grade = 'B'; // B, S, G, P
		bool hidden = false, unlocked = false;
		qint64 time = 0;   // unix seconds, 0 when unknown
		QString icon;      // file path
	};
	struct Set
	{
		QString key, title, console; // key: "PS3:NPWR01542_00"
		QVector<Trophy> trophies;
	};

	explicit Profile(QObject* parent = nullptr);

	QVariantMap summary() const { return m_summary; }

	// game: a GameModel entry {serial, title, console, path}; online: started in a USCL session
	Q_INVOKABLE void gameStarted(const QVariantMap& game, bool online);
	Q_INVOKABLE void gameStopped();
	// the running game went online through USCL (a session started or was joined after it)
	Q_INVOKABLE void sessionOnline();
	// the game's trophies for its trophy list: [{id, name, detail, grade, unlocked, time, icon}]
	Q_INVOKABLE QVariantList trophies(const QVariantMap& game);
	// {earned, total, bronze, silver, gold, platinum, seconds, last}
	Q_INVOKABLE QVariantMap gameSummary(const QVariantMap& game);

	// writes presenceFile(): what friends see through `uscl-bridge presence` (also after every change)
	Q_INVOKABLE void publish();
	static QString presenceFile();
	// Parsers and the presence format, exposed for the self-test
	static QMap<int, qint64> readTropusr(const QByteArray& dat);  // unlocked trophy id -> unix time
	static QVector<Trophy> readTrophyXml(const QByteArray& xml);   // TROPCONF.SFM, shadPS4 TROP*.XML and save files
	static QVector<Trophy> usclTrophies(const QJsonObject& record);
	static QMap<QString, QString> presenceKeys(const QVariantMap& summary, const QString& playing);
	static QVariantMap readPresence(const QVariantMap& keys);
	static QString setRoot(const QString& console); // where an emulator keeps its trophy sets
	static bool nativeTrophies(const QString& console) { return console == "PS3" || console == "PS4"; }
	// looks for new unlocks of the running game (every 2 seconds; the self-test calls it directly)
	void poll();

signals:
	void changed();
	// {name, detail, grade, icon, game, console}
	void unlocked(const QVariantMap& trophy);

private:
	QVector<Set> scanSets() const;
	Set readSet(const QString& console, const QString& dir) const;
	QStringList setsFor(const QVariantMap& game) const;

	void tick();
	void award(const QString& serial, int id);
	void save();
	void refresh();

	QJsonObject m_games; // serial -> {title, console, seconds, last, sets: [key], uscl: {id: time}}
	QVariantMap m_summary;
	QVariantMap m_game;  // the running game
	QElapsedTimer m_clock;
	QTimer m_tick, m_poll;
	QHash<QString, QSet<int>> m_seen;   // set key -> unlocked ids, while a game runs
	QHash<QString, qint64> m_stamps;    // set key -> when its unlocks last changed
};
