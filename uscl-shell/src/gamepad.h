#pragma once

#include <QElapsedTimer>
#include <QObject>
#include <QTimer>

// Every connected controller, through SDL3. Buttons by position, so ✕/A is "accept" and ○/B is "back" on any pad.
// A game's PS / Guide button reaches the home screen through RPCS3 (Launcher::homeRequested), not from here.
class Gamepad : public QObject
{
	Q_OBJECT
	Q_PROPERTY(int count READ count NOTIFY countChanged)
	Q_PROPERTY(QString name READ name NOTIFY countChanged)
	Q_PROPERTY(QStringList names READ names NOTIFY countChanged)

public:
	explicit Gamepad(QObject* parent = nullptr);
	~Gamepad() override;

	int count() const { return m_count; }
	QString name() const { return m_name; }
	QStringList names() const { return m_names; }

signals:
	// up, down, left, right, accept, back, options, info, l1, r1, home
	void pressed(const QString& button);
	void countChanged();

private:
	void poll();
	void direction(const QString& dir, bool down);
	void refreshPads();

	QTimer m_timer;
	QString m_held_dir;
	QElapsedTimer m_held_since;
	qint64 m_next_repeat = 0;
	int m_stick_x = 0, m_stick_y = 0; // -1, 0, 1
	int m_count = 0;
	QString m_name;
	QStringList m_names;
	bool m_ok = false;
};
