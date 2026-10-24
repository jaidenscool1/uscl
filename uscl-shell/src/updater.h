#pragma once

#include <QFile>
#include <QNetworkAccessManager>
#include <QObject>

// USCL updates, like RPCS3's updater: asks GitHub for the newest release of the public releases repo
// (USCL_RELEASES), and when it's newer than this build, downloads the installer (Windows) or AppImage (Linux),
// checks its SHA-256 when the release has one, and installs it. The home screen asks the player first.
class Updater : public QObject
{
	Q_OBJECT
	// idle, checking, current, available, downloading, ready, error, unsupported
	Q_PROPERTY(QString state READ state NOTIFY changed)
	Q_PROPERTY(QString latest READ latest NOTIFY changed)
	Q_PROPERTY(QString notes READ notes NOTIFY changed)
	Q_PROPERTY(double progress READ progress NOTIFY changed)
	Q_PROPERTY(QString message READ message NOTIFY changed)

public:
	explicit Updater(QObject* parent = nullptr);

	QString state() const { return m_state; }
	QString latest() const { return m_latest; }
	QString notes() const { return m_notes; }
	double progress() const { return m_progress; }
	QString message() const { return m_message; }

	// testing: also offer pre-releases
	Q_INVOKABLE void check(bool testing);
	Q_INVOKABLE void download();
	// runs the installer / swaps the AppImage, then quits so the new version can start
	Q_INVOKABLE void install();

	// <0 a older, 0 same, >0 a newer; "v0.4.0" > "0.3.9" > "0.3.0-test" < "0.3.0"
	static int compareVersions(const QString& a, const QString& b);
	static QString assetName();

signals:
	void changed();

private:
	void setState(const QString& state, const QString& message = {});

	QNetworkAccessManager m_net;
	QString m_state = "idle", m_latest, m_notes, m_message;
	double m_progress = 0;
	QString m_url, m_sha_url, m_expected_sha;
	qint64 m_size = 0;
	QFile m_file;
};
