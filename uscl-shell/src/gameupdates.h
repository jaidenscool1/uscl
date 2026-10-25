#pragma once

#include <QFile>
#include <QMap>
#include <QNetworkAccessManager>
#include <QObject>
#include <QProcess>
#include <QStringList>
#include <QVariantMap>

// Game updates (patches) from Sony's update server, like the PS3's own Check for Update. Each game's list
// (<serial>-ver.xml) names every patch with its version, size, SHA-1 and download. A game is offered the ones newer
// than what's installed; installing downloads each, checks it against the SHA-1 and installs it through RPCS3,
// oldest first, since a PS3 patch builds on the ones before it.
class GameUpdates : public QObject
{
	Q_OBJECT
	// serial -> [{version, size}]: the updates each game doesn't have yet, oldest first
	Q_PROPERTY(QVariantMap available READ available NOTIFY changed)
	Q_PROPERTY(bool checking READ checking NOTIFY changed)
	Q_PROPERTY(bool busy READ busy NOTIFY progressChanged)
	Q_PROPERTY(double progress READ progress NOTIFY progressChanged)
	Q_PROPERTY(QString status READ status NOTIFY progressChanged)

public:
	struct Package
	{
		QString version, url, sha1;
		qint64 size = 0;
	};

	explicit GameUpdates(QObject* parent = nullptr);

	QVariantMap available() const;
	bool checking() const { return !m_to_check.isEmpty(); }
	bool busy() const { return m_busy; }
	double progress() const { return m_progress; }
	QString status() const { return m_status; }

	// games: [{serial, version}] with the version installed now; asks Sony about each, one at a time
	Q_INVOKABLE void check(const QVariantList& games);
	// downloads and installs every update these games don't have yet
	Q_INVOKABLE void install(const QStringList& serials);

	static QList<Package> parseList(const QByteArray& xml);
	// "01.19" > "01.09"; versions are MM.mm
	static bool newer(const QString& a, const QString& b);
	// Sony's sha1sum: SHA-1 of the package without its last 32 bytes (which repeat the hash)
	static QByteArray packageSha1(QIODevice& pkg, qint64 size);
	// the version RPCS3 has installed for a game (its update or installed copy), empty if none
	static QString installedVersion(const QString& serial);

signals:
	void changed();
	void progressChanged();
	// all done: how many updates went in, and why any didn't
	void finished(int installed, const QString& problem);

private:
	void checkNext();
	void installNext();
	void fail(const QString& problem);
	void setProgress(double progress, const QString& status);

	QNetworkAccessManager m_net;
	QMap<QString, QList<Package>> m_available;
	QList<QPair<QString, QString>> m_to_check; // serial, installed version
	QList<QPair<QString, Package>> m_queue;
	QFile m_file;
	QProcess m_install;
	bool m_busy = false;
	double m_progress = 0;
	QString m_status, m_problem;
	int m_total = 0, m_done = 0;
};
