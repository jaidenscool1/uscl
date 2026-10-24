#pragma once

#include <QFile>
#include <QNetworkAccessManager>
#include <QObject>
#include <QProcess>
#include <QTimer>

// Small facts about this PC's USCL setup, and the PS3 system software (firmware) install
class System : public QObject
{
	Q_OBJECT
	Q_PROPERTY(QString onlineName READ onlineName NOTIFY changed)
	Q_PROPERTY(bool firmwareInstalled READ firmwareInstalled NOTIFY changed)
	Q_PROPERTY(bool firmwareBusy READ firmwareBusy NOTIFY firmwareChanged)
	Q_PROPERTY(double firmwareProgress READ firmwareProgress NOTIFY firmwareChanged)
	Q_PROPERTY(QString firmwareStatus READ firmwareStatus NOTIFY firmwareChanged)
	Q_PROPERTY(QString firmwareVersion READ firmwareVersion NOTIFY changed)
	// laptop battery for the clock box: -1 when this PC has none
	Q_PROPERTY(int battery READ battery NOTIFY statusChanged)
	Q_PROPERTY(bool charging READ charging NOTIFY statusChanged)
	// internet reachable: 1 yes, 0 no, -1 unknown
	Q_PROPERTY(int internet READ internet NOTIFY statusChanged)
	Q_PROPERTY(QString version READ version CONSTANT)

public:
	explicit System(QObject* parent = nullptr);

	QString onlineName() const;
	bool firmwareInstalled() const;
	bool firmwareBusy() const { return m_busy; }
	double firmwareProgress() const { return m_progress; }
	QString firmwareStatus() const { return m_status; }
	QString firmwareVersion() const;
	int battery() const { return m_battery; }
	bool charging() const { return m_charging; }
	int internet() const;
	static QString version();

	// RPCS3's config.yml, one key of a top-level section (Video, Audio, System, ...)
	Q_INVOKABLE QString rpcs3Value(const QString& section, const QString& key) const;
	Q_INVOKABLE bool setRpcs3Value(const QString& section, const QString& key, const QString& value);
	// System Information: one "label: value" per line
	Q_INVOKABLE QString info() const;

	// after Classic mode closes: settings may have changed there
	Q_INVOKABLE void refresh() { emit changed(); }

	// PS3UPDAT.PUP files in the usual places (Downloads, Desktop, home), newest first
	Q_INVOKABLE QStringList findFirmware() const;
	Q_INVOKABLE void installFirmware(const QString& pup);
	// from Sony's official update server, then install
	Q_INVOKABLE void downloadFirmware();

	static QString cdnUrl(const QByteArray& updatelist);

signals:
	void changed();
	void firmwareChanged();
	void statusChanged();

private:
	void setFirmware(bool busy, double progress, const QString& status);

	QNetworkAccessManager m_net;
	QProcess m_install;
	QFile m_download;
	bool m_busy = false;
	double m_progress = 0;
	QString m_status;
	QTimer m_poll;
	int m_battery = -1;
	bool m_charging = false;
	void pollStatus();
};
