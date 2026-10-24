#pragma once

#include <QFile>
#include <QNetworkAccessManager>
#include <QObject>
#include <QProcess>

// Small facts about this PC's USCL setup, and the PS3 system software (firmware) install
class System : public QObject
{
	Q_OBJECT
	Q_PROPERTY(QString onlineName READ onlineName NOTIFY changed)
	Q_PROPERTY(bool firmwareInstalled READ firmwareInstalled NOTIFY changed)
	Q_PROPERTY(bool firmwareBusy READ firmwareBusy NOTIFY firmwareChanged)
	Q_PROPERTY(double firmwareProgress READ firmwareProgress NOTIFY firmwareChanged)
	Q_PROPERTY(QString firmwareStatus READ firmwareStatus NOTIFY firmwareChanged)

public:
	using QObject::QObject;

	QString onlineName() const;
	bool firmwareInstalled() const;
	bool firmwareBusy() const { return m_busy; }
	double firmwareProgress() const { return m_progress; }
	QString firmwareStatus() const { return m_status; }

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

private:
	void setFirmware(bool busy, double progress, const QString& status);

	QNetworkAccessManager m_net;
	QProcess m_install;
	QFile m_download;
	bool m_busy = false;
	double m_progress = 0;
	QString m_status;
};
