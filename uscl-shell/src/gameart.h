#pragma once

#include <QMap>
#include <QNetworkAccessManager>
#include <QObject>
#include <QStringList>

// Icons and preview pictures for games that carry none (Switch, Wii, GameCube, 3DS, Wii U, Xbox 360), downloaded
// once into the cache: Switch from api.nlib.cc by title id, Wii/3DS covers from GameTDB by the id in the game file,
// the rest (and previews) from libretro's thumbnail server by the game's name. A miss is remembered, not retried.
class GameArt : public QObject
{
	Q_OBJECT

public:
	struct Want
	{
		QString path, title, console, serial;
	};

	explicit GameArt(QObject* parent = nullptr);

	// file URL of the cached icon / preview ("icon" or "pic") for the game at path, or empty
	static QString cached(const QString& path, const QString& kind);
	// downloads what's missing; updated() once something arrived and the queue is empty
	void fetch(const QList<Want>& games);

	// Exposed for the self-test
	static QString gameId(const QString& path, const QString& console); // Wii disc id / 3DS product code
	static QString normName(QString name);
	static QMap<QString, QString> parseListing(const QString& html); // normalized name -> libretro file name

signals:
	void updated();

private:
	struct Job
	{
		Want game;
		QString kind;
		QStringList urls;
		bool listed = false;   // libretro candidates added
		bool netError = false; // a request failed (offline?): don't remember this as "no art"
	};
	void next();
	void tryNext(Job job);
	QStringList libretroUrls(const Job& job);

	QNetworkAccessManager m_net;
	QList<Job> m_jobs;
	QMap<QString, QMap<QString, QString>> m_listings; // "<system>/<kind dir>" -> names; empty map = unavailable
	bool m_busy = false, m_got = false;
};
