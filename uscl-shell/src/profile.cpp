#include "profile.h"
#include "games.h"

#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSaveFile>
#include <QUrl>
#include <QXmlStreamReader>
#include <QtEndian>

#include <algorithm>

namespace
{
	QString profileFile() { return GameModel::rpcs3ConfigDir() + "uscl/profile.json"; }

	QString gradeName(QChar g)
	{
		switch (g.unicode())
		{
		case 'P': return "platinum";
		case 'G': return "gold";
		case 'S': return "silver";
		default: return "bronze";
		}
	}

	// the same game under slightly different names: "Call of Duty®: Black Ops II" == "Call of Duty: Black Ops II"
	QString plainTitle(QString t)
	{
		static const QRegularExpression other("[^a-z0-9]");
		return t.toLower().remove(other);
	}

	// Trophy sets a game folder names: PS3 discs list theirs in PS3_GAME/TROPDIR, PS4 games in sce_sys/npbind.dat
	QStringList ownSets(const QString& console, const QString& path, const QString& serial)
	{
		QStringList out;
		QString dir = QFileInfo(path).isFile() ? QFileInfo(path).absolutePath() : path;
		if (console == "PS3")
		{
			if (dir.endsWith("/USRDIR")) dir = QFileInfo(dir).absolutePath(); // an EBOOT.BIN was added
			for (const QString& base : {dir + "/PS3_GAME/TROPDIR", dir + "/TROPDIR", GameModel::rpcs3ConfigDir() + "dev_hdd0/game/" + serial + "/TROPDIR"})
				for (const QString& id : QDir(base).entryList(QDir::Dirs | QDir::NoDotAndDotDot))
					out << "PS3:" + id;
		}
		else if (console == "PS4")
		{
			QFile f(dir + "/sce_sys/npbind.dat");
			static const QRegularExpression id("NPWR\\d{5}_\\d{2}");
			if (f.size() < (1 << 20) && f.open(QIODevice::ReadOnly))
				for (auto it = id.globalMatch(QString::fromLatin1(f.readAll())); it.hasNext();)
					out << "PS4:" + it.next().captured();
		}
		out.removeDuplicates();
		return out;
	}

	QString titleName(const QByteArray& xml)
	{
		QXmlStreamReader r(xml);
		while (!r.atEnd())
			if (r.readNext() == QXmlStreamReader::StartElement && r.name() == u"title-name")
				return r.readElementText().simplified();
		return {};
	}

	// When a set's unlocks last changed: RPCS3 rewrites TROPUSR.DAT, shadPS4 a user's <npcomm>.xml
	qint64 unlockStamp(const QString& console, const QString& dir)
	{
		if (console == "PS3")
			return QFileInfo(dir + "/TROPUSR.DAT").lastModified().toMSecsSinceEpoch();
		qint64 stamp = 0;
		const QString home = QFileInfo(dir).absolutePath() + "/../home";
		for (const QString& user : QDir(home).entryList(QDir::Dirs | QDir::NoDotAndDotDot))
			stamp = std::max(stamp, QFileInfo(home + '/' + user + "/trophy/" + QFileInfo(dir).fileName() + ".xml").lastModified().toMSecsSinceEpoch());
		for (const QFileInfo& f : QDir(dir + "/Xml").entryInfoList({"TROP*.XML", "TROP*.xml"}, QDir::Files))
			stamp = std::max(stamp, f.lastModified().toMSecsSinceEpoch());
		return stamp;
	}

	// USCL's play-time trophies, for games whose console has no trophies of its own: [id, grade, name, detail, hours]
	struct Milestone { int id; char grade; const char* name; const char* detail; int hours; };
	constexpr Milestone milestones[] = {
		{0, 'B', "First Steps", "Start the game in USCL", 0},
		{1, 'B', "Settling In", "Play for 1 hour", 1},
		{2, 'S', "Better Together", "Play in a USCL online session", -1},
		{3, 'S', "Regular", "Play for 5 hours", 5},
		{4, 'G', "Dedicated", "Play for 20 hours", 20},
		{5, 'P', "USCL Master", "Earn every other USCL trophy for this game", -1},
	};
}

Profile::Profile(QObject* parent)
	: QObject(parent)
{
	QFile f(profileFile());
	if (f.open(QIODevice::ReadOnly))
		m_games = QJsonDocument::fromJson(f.readAll()).object()["games"].toObject();
	m_tick.setInterval(60 * 1000);
	connect(&m_tick, &QTimer::timeout, this, &Profile::tick);
	m_poll.setInterval(2000);
	connect(&m_poll, &QTimer::timeout, this, &Profile::poll);
	refresh();
}

QString Profile::presenceFile()
{
	return GameModel::rpcs3ConfigDir() + "uscl/presence.json";
}

QString Profile::setRoot(const QString& console)
{
	return console == "PS3" ? GameModel::rpcs3ConfigDir() + "dev_hdd0/home/00000001/trophy"
	                        : GameModel::rpcs3ConfigDir() + "uscl/shadps4/user/trophy";
}

// ---- parsers ----

QMap<int, qint64> Profile::readTropusr(const QByteArray& dat)
{
	// RPCS3 Loader/TROPUSR.h: a 0x30 header, then tables of {type, entry size, 1, count, offset, 0}; table 6 has
	// one entry per trophy: {6, size, id, 0, trophy id, state (1 = unlocked), 0, 0, time1, time2}
	QMap<int, qint64> out;
	const auto u32 = [&](qsizetype at) { return at + 4 <= dat.size() ? qFromBigEndian<quint32>(dat.constData() + at) : 0u; };
	const auto u64 = [&](qsizetype at) { return at + 8 <= dat.size() ? qFromBigEndian<quint64>(dat.constData() + at) : 0ull; };
	if (u32(0) != 0x818F54AD)
		return out;
	const quint32 tables = std::min(u32(8), 16u);
	for (quint32 t = 0; t < tables; t++)
	{
		const qsizetype h = 0x30 + t * 32;
		if (u32(h) != 6)
			continue;
		const quint32 size = u32(h + 4), count = std::min(u32(h + 12), 1024u);
		const quint64 offset = u64(h + 16);
		for (quint32 i = 0; i < count; i++)
		{
			const quint64 e = offset + quint64(i) * (16 + size);
			if (e + 48 > quint64(dat.size()))
				break;
			if (u32(e + 20) != 1)
				continue;
			// CellRtcTick: microseconds since 0001-01-01
			const quint64 tick = u64(e + 32), epoch = 62135596800000000ull;
			out[int(u32(e + 16))] = tick > epoch ? qint64((tick - epoch) / 1000000) : 0;
		}
	}
	return out;
}

QVector<Profile::Trophy> Profile::readTrophyXml(const QByteArray& xml)
{
	QVector<Trophy> out;
	QXmlStreamReader r(xml);
	while (!r.atEnd())
	{
		r.readNext();
		if (!r.isStartElement())
			continue;
		if (r.name() == u"trophy")
		{
			const auto a = r.attributes();
			Trophy t;
			t.id = a.value("id").toInt();
			t.grade = a.value("ttype").isEmpty() ? QChar('B') : a.value("ttype").at(0).toUpper();
			t.hidden = a.value("hidden") == u"yes";
			t.unlocked = a.value("unlockstate") == u"true";
			t.time = a.value("timestamp").toLongLong();
			out.push_back(t);
		}
		else if (!out.isEmpty() && (r.name() == u"name" || r.name() == u"detail"))
		{
			const bool name = r.name() == u"name";
			(name ? out.last().name : out.last().detail) = r.readElementText().simplified();
		}
	}
	return out;
}

QVector<Profile::Trophy> Profile::usclTrophies(const QJsonObject& record)
{
	const QJsonObject got = record["uscl"].toObject();
	QVector<Trophy> out;
	for (const Milestone& m : milestones)
	{
		Trophy t;
		t.id = m.id;
		t.grade = QChar(m.grade);
		t.name = tr(m.name);
		t.detail = tr(m.detail);
		t.unlocked = got.contains(QString::number(m.id));
		t.time = got[QString::number(m.id)].toInteger();
		out.push_back(t);
	}
	return out;
}

Profile::Set Profile::readSet(const QString& console, const QString& dir) const
{
	Set s;
	s.console = console;
	s.key = console + ':' + QFileInfo(dir).fileName();
	if (console == "PS3")
	{
		QFile conf(dir + "/TROPCONF.SFM");
		if (!conf.open(QIODevice::ReadOnly))
			return {};
		const QByteArray xml = conf.readAll();
		s.trophies = readTrophyXml(xml);
		s.title = titleName(xml);
		QFile usr(dir + "/TROPUSR.DAT");
		const QMap<int, qint64> got = usr.open(QIODevice::ReadOnly) ? readTropusr(usr.readAll()) : QMap<int, qint64>{};
		for (Trophy& t : s.trophies)
		{
			t.unlocked = got.contains(t.id);
			t.time = got.value(t.id);
			t.icon = dir + QString("/TROP%1.PNG").arg(t.id, 3, 10, QChar('0'));
		}
		return s;
	}
	// shadPS4: definitions in <npcomm>/Xml/TROP.XML (or a language's TROP_01.XML ...), unlocks in each user's
	// home/<id>/trophy/<npcomm>.xml (older builds wrote unlockstate into the definitions)
	QStringList xmls = QDir(dir + "/Xml").entryList({"TROP*.XML", "TROP*.xml"}, QDir::Files, QDir::Name);
	if (xmls.isEmpty())
		return {};
	const QString def = xmls.contains("TROP.XML") ? "TROP.XML" : xmls.contains("TROP_01.XML") ? "TROP_01.XML" : xmls.first();
	QFile conf(dir + "/Xml/" + def);
	if (!conf.open(QIODevice::ReadOnly))
		return {};
	const QByteArray xml = conf.readAll();
	s.trophies = readTrophyXml(xml);
	s.title = titleName(xml);
	// shortcut: a trophy any of the four local players earned counts; split per player if USCL gets player profiles
	const QString home = QFileInfo(dir).absolutePath() + "/../home";
	for (const QString& user : QDir(home).entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name))
	{
		QFile save(home + '/' + user + "/trophy/" + QFileInfo(dir).fileName() + ".xml");
		if (!save.open(QIODevice::ReadOnly))
			continue;
		for (const Trophy& u : readTrophyXml(save.readAll()))
			for (Trophy& t : s.trophies)
				if (t.id == u.id && u.unlocked) { t.unlocked = true; t.time = u.time; }
	}
	for (Trophy& t : s.trophies)
		t.icon = dir + QString("/Icons/TROP%1.PNG").arg(t.id, 3, 10, QChar('0'));
	return s;
}

QVector<Profile::Set> Profile::scanSets() const
{
	QVector<Set> out;
	for (const QString& console : {QStringLiteral("PS3"), QStringLiteral("PS4")})
	{
		const QString root = setRoot(console);
		for (const QString& id : QDir(root).entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name))
			if (Set s = readSet(console, root + '/' + id); !s.key.isEmpty() && !s.trophies.isEmpty())
				out.push_back(s);
	}
	return out;
}

// ---- which sets belong to a game ----

QStringList Profile::setsFor(const QVariantMap& game) const
{
	const QString serial = game["serial"].toString(), console = game["console"].toString().isEmpty() ? "PS3" : game["console"].toString();
	QStringList keys;
	for (const QJsonValue& k : m_games.value(serial).toObject()["sets"].toArray())
		keys << k.toString();
	keys << ownSets(console, game["path"].toString(), serial);
	keys.removeDuplicates();
	if (!keys.isEmpty())
		return keys;
	// last resort: a set with the game's name
	const QString title = plainTitle(game["title"].toString());
	for (const Set& s : scanSets())
		if (s.console == console && plainTitle(s.title) == title)
			keys << s.key;
	return keys;
}

// ---- a running game ----

void Profile::gameStarted(const QVariantMap& game, bool online)
{
	if (!m_game.isEmpty())
		gameStopped();
	m_game = game;
	const QString serial = game["serial"].toString(), console = game["console"].toString().isEmpty() ? "PS3" : game["console"].toString();
	QJsonObject r = m_games.value(serial).toObject();
	r["title"] = game["title"].toString();
	r["console"] = console;
	r["last"] = QDateTime::currentSecsSinceEpoch();
	m_games[serial] = r;
	m_clock.start();
	m_tick.start();
	if (nativeTrophies(console))
	{
		// what's unlocked already, so only new unlocks are announced
		m_seen.clear();
		m_stamps.clear();
		for (const Set& s : scanSets())
		{
			if (s.console != console) continue;
			m_stamps[s.key] = unlockStamp(console, setRoot(console) + '/' + s.key.section(':', 1));
			for (const Trophy& t : s.trophies)
				if (t.unlocked) m_seen[s.key].insert(t.id);
		}
		m_poll.start();
	}
	else
	{
		award(serial, 0);
		if (online) award(serial, 2);
	}
	save();
}

void Profile::sessionOnline()
{
	const QString console = m_game.value("console").toString();
	if (!m_game.isEmpty() && !nativeTrophies(console.isEmpty() ? "PS3" : console))
		award(m_game.value("serial").toString(), 2);
}

void Profile::gameStopped()
{
	if (m_game.isEmpty())
		return;
	tick();
	if (m_poll.isActive())
		poll(); // the last unlock before quitting
	m_tick.stop();
	m_poll.stop();
	m_game.clear();
	m_seen.clear();
	m_stamps.clear();
	save();
}

void Profile::tick()
{
	if (m_game.isEmpty())
		return;
	const QString serial = m_game.value("serial").toString();
	QJsonObject r = m_games.value(serial).toObject();
	r["seconds"] = r["seconds"].toInteger() + m_clock.restart() / 1000;
	r["last"] = QDateTime::currentSecsSinceEpoch();
	m_games[serial] = r;
	const QString console = r["console"].toString();
	if (!nativeTrophies(console))
		for (const Milestone& m : milestones)
			if (m.hours > 0 && r["seconds"].toInteger() >= m.hours * 3600)
				award(serial, m.id);
	save();
}

void Profile::award(const QString& serial, int id)
{
	QJsonObject r = m_games.value(serial).toObject();
	QJsonObject got = r["uscl"].toObject();
	if (got.contains(QString::number(id)))
		return;
	got[QString::number(id)] = QDateTime::currentSecsSinceEpoch();
	r["uscl"] = got;
	m_games[serial] = r;
	const Trophy t = usclTrophies(r)[id];
	emit unlocked({{"name", t.name}, {"detail", t.detail}, {"grade", gradeName(t.grade)}, {"icon", QString()},
	               {"game", r["title"].toString()}, {"console", r["console"].toString()}});
	// every other one earned: the platinum
	if (id != 5 && got.size() == int(std::size(milestones)) - 1)
		award(serial, 5);
	save();
}

void Profile::poll()
{
	if (m_game.isEmpty())
		return;
	const QString serial = m_game.value("serial").toString(), console = m_game.value("console").toString().isEmpty() ? "PS3" : m_game.value("console").toString();
	const QString root = setRoot(console);
	bool learned = false;
	for (const QString& id : QDir(root).entryList(QDir::Dirs | QDir::NoDotAndDotDot))
	{
		const QString key = console + ':' + id;
		const qint64 stamp = unlockStamp(console, root + '/' + id);
		if (m_stamps.value(key, -1) == stamp)
			continue;
		m_stamps[key] = stamp;
		const Set s = readSet(console, root + '/' + id);
		QSet<int>& seen = m_seen[key];
		for (const Trophy& t : s.trophies)
		{
			if (!t.unlocked || seen.contains(t.id))
				continue;
			seen.insert(t.id);
			// a trophy unlocked while this game runs is this game's: remember the set for its trophy list
			QJsonObject r = m_games.value(serial).toObject();
			QJsonArray sets = r["sets"].toArray();
			if (!sets.contains(key)) { sets.append(key); r["sets"] = sets; m_games[serial] = r; }
			emit unlocked({{"name", t.name.isEmpty() ? tr("Hidden Trophy") : t.name}, {"detail", t.detail},
			               {"grade", gradeName(t.grade)}, {"icon", QFileInfo::exists(t.icon) ? QUrl::fromLocalFile(t.icon).toString() : QString()},
			               {"game", s.title.isEmpty() ? m_game.value("title").toString() : s.title}, {"console", console}});
			learned = true;
		}
	}
	if (learned)
		save();
}

// ---- reading ----

QVariantList Profile::trophies(const QVariantMap& game)
{
	QVector<Trophy> list;
	const QString console = game["console"].toString().isEmpty() ? "PS3" : game["console"].toString();
	if (!nativeTrophies(console))
		list = usclTrophies(m_games.value(game["serial"].toString()).toObject());
	else
		for (const QString& key : setsFor(game))
			if (Set s = readSet(key.section(':', 0, 0), setRoot(key.section(':', 0, 0)) + '/' + key.section(':', 1)); !s.key.isEmpty())
				list += s.trophies;
	QVariantList out;
	for (const Trophy& t : list)
	{
		const bool secret = t.hidden && !t.unlocked;
		out.push_back(QVariantMap{{"id", t.id}, {"name", secret ? tr("Hidden Trophy") : t.name}, {"detail", secret ? QString() : t.detail},
		                          {"grade", gradeName(t.grade)}, {"unlocked", t.unlocked}, {"time", t.time},
		                          {"icon", !secret && QFileInfo::exists(t.icon) ? QUrl::fromLocalFile(t.icon).toString() : QString()}});
	}
	return out;
}

QVariantMap Profile::gameSummary(const QVariantMap& game)
{
	QVariantMap s{{"earned", 0}, {"total", 0}, {"bronze", 0}, {"silver", 0}, {"gold", 0}, {"platinum", 0}};
	for (const QVariant& v : trophies(game))
	{
		const QVariantMap t = v.toMap();
		s["total"] = s["total"].toInt() + 1;
		if (t["unlocked"].toBool())
		{
			s["earned"] = s["earned"].toInt() + 1;
			s[t["grade"].toString()] = s[t["grade"].toString()].toInt() + 1;
		}
	}
	const QJsonObject r = m_games.value(game["serial"].toString()).toObject();
	s["seconds"] = r["seconds"].toInteger();
	s["last"] = r["last"].toInteger();
	return s;
}

void Profile::refresh()
{
	QVariantMap sum{{"seconds", 0}, {"bronze", 0}, {"silver", 0}, {"gold", 0}, {"platinum", 0}, {"earned", 0}, {"total", 0}};
	QVariantList games;
	const auto count = [&](const QVector<Trophy>& list, QVariantMap& g) {
		int earned = 0;
		for (const Trophy& t : list)
		{
			if (!t.unlocked) continue;
			earned++;
			sum[gradeName(t.grade)] = sum[gradeName(t.grade)].toInt() + 1;
		}
		g["earned"] = g["earned"].toInt() + earned;
		g["total"] = g["total"].toInt() + int(list.size());
		sum["earned"] = sum["earned"].toInt() + earned;
		sum["total"] = sum["total"].toInt() + int(list.size());
	};
	// every PS3/PS4 set on this PC counts, played through USCL or not; sets no game claimed yet get their own entry
	QHash<QString, Set> sets;
	for (const Set& s : scanSets())
		sets[s.key] = s;
	for (auto it = m_games.begin(); it != m_games.end(); ++it)
	{
		const QJsonObject r = it->toObject();
		QVariantMap g{{"serial", it.key()}, {"title", r["title"].toString()}, {"console", r["console"].toString()},
		              {"seconds", r["seconds"].toInteger()}, {"last", r["last"].toInteger()}, {"earned", 0}, {"total", 0}};
		if (!nativeTrophies(r["console"].toString()))
			count(usclTrophies(r), g);
		else
			for (const QString& key : setsFor({{"serial", it.key()}, {"title", r["title"]}, {"console", r["console"]}}))
				if (sets.contains(key)) count(sets.take(key).trophies, g);
		sum["seconds"] = sum["seconds"].toLongLong() + r["seconds"].toInteger();
		games.push_back(g);
	}
	for (const Set& s : std::as_const(sets))
	{
		QVariantMap g{{"serial", s.key}, {"title", s.title}, {"console", s.console}, {"seconds", 0}, {"last", 0}, {"earned", 0}, {"total", 0}};
		count(s.trophies, g);
		if (g["earned"].toInt()) games.push_back(g);
	}
	std::sort(games.begin(), games.end(), [](const QVariant& a, const QVariant& b) {
		const QVariantMap x = a.toMap(), y = b.toMap();
		return x["last"].toLongLong() != y["last"].toLongLong() ? x["last"].toLongLong() > y["last"].toLongLong()
		                                                        : x["title"].toString() < y["title"].toString();
	});
	sum["games"] = games;
	m_summary = sum;
	emit changed();
}

void Profile::publish()
{
	QJsonObject keys;
	const QMap<QString, QString> p = presenceKeys(m_summary, m_game.value("title").toString());
	for (auto it = p.begin(); it != p.end(); ++it)
		keys[it.key()] = it.value();
	QSaveFile f(presenceFile());
	QDir().mkpath(QFileInfo(f.fileName()).absolutePath());
	if (f.open(QIODevice::WriteOnly))
	{
		f.write(QJsonDocument(keys).toJson(QJsonDocument::Compact));
		f.commit();
	}
}

void Profile::save()
{
	QSaveFile f(profileFile());
	QDir().mkpath(QFileInfo(f.fileName()).absolutePath());
	if (f.open(QIODevice::WriteOnly))
	{
		f.write(QJsonDocument(QJsonObject{{"games", m_games}}).toJson(QJsonDocument::Compact));
		f.commit();
	}
	refresh();
	publish();
}

// ---- Rich Presence: Steam allows 20 keys of 255 bytes, so the profile is a summary and the 6 latest games ----

QMap<QString, QString> Profile::presenceKeys(const QVariantMap& summary, const QString& playing)
{
	QMap<QString, QString> keys;
	const QVariantList games = summary["games"].toList();
	keys["uscl_p"] = QString("1;%1;%2;%3;%4;%5;%6").arg(summary["seconds"].toLongLong() / 60).arg(summary["bronze"].toInt())
	                     .arg(summary["silver"].toInt()).arg(summary["gold"].toInt()).arg(summary["platinum"].toInt()).arg(games.size());
	keys["uscl_now"] = playing.left(120);
	for (int i = 0; i < 6; i++)
	{
		QString v;
		if (i < games.size())
		{
			const QVariantMap g = games[i].toMap();
			// the title last, so cutting to Steam's limit only shortens it
			v = QString("%1|%2|%3|%4|%5|").arg(g["console"].toString()).arg(g["seconds"].toLongLong() / 60).arg(g["earned"].toInt())
			        .arg(g["total"].toInt()).arg(g["last"].toLongLong() / 86400) + g["title"].toString();
			while (v.toUtf8().size() > 250) v.chop(1);
		}
		keys[QString("uscl_g%1").arg(i)] = v;
	}
	return keys;
}

QVariantMap Profile::readPresence(const QVariantMap& keys)
{
	const QStringList p = keys["uscl_p"].toString().split(';');
	if (p.size() < 7 || p[0] != "1")
		return {};
	QVariantList games;
	for (int i = 0; i < 6; i++)
	{
		const QString v = keys[QString("uscl_g%1").arg(i)].toString();
		const QStringList f = v.split('|');
		if (f.size() < 6)
			continue;
		games.push_back(QVariantMap{{"console", f[0]}, {"seconds", f[1].toLongLong() * 60}, {"earned", f[2].toInt()}, {"total", f[3].toInt()},
		                            {"last", f[4].toLongLong() * 86400}, {"title", v.section('|', 5)}});
	}
	return {{"seconds", p[1].toLongLong() * 60}, {"bronze", p[2].toInt()}, {"silver", p[3].toInt()}, {"gold", p[4].toInt()},
	        {"platinum", p[5].toInt()}, {"earned", p[2].toInt() + p[3].toInt() + p[4].toInt() + p[5].toInt()},
	        {"gameCount", p[6].toInt()}, {"playing", keys["uscl_now"].toString()}, {"games", games}};
}
