#pragma once

#include <QObject>

// Small facts about this PC's USCL setup for the home screen
class System : public QObject
{
	Q_OBJECT
	Q_PROPERTY(QString onlineName READ onlineName NOTIFY changed)
	Q_PROPERTY(bool firmwareInstalled READ firmwareInstalled NOTIFY changed)

public:
	using QObject::QObject;

	QString onlineName() const;
	bool firmwareInstalled() const;

	// after Classic mode closes: settings may have changed there
	Q_INVOKABLE void refresh() { emit changed(); }

signals:
	void changed();
};
