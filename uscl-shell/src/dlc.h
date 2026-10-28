#pragma once

#include <QObject>
#include <QProcess>
#include <QVariantList>

// A PS3 game's downloadable content as RPCS3 has it: content (.edat with an NPD header naming its content ID) in
// the game's folder or exdata, and licenses (<content ID>.rap in exdata). Lists what's there and what doesn't work
// yet (content without its license, a license without content), and installs the player's own files: a .pkg
// through RPCS3, a .rap into exdata. Nothing is downloaded.
class Dlc : public QObject
{
	Q_OBJECT
	Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)

public:
	explicit Dlc(QObject* parent = nullptr);

	bool busy() const { return m_install.state() != QProcess::NotRunning; }

	// [{id, name, content, licensed}] for this game, sorted by content ID
	Q_INVOKABLE static QVariantList list(const QString& serial);
	// .pkg and .rap files in a folder: [{name, path}]
	Q_INVOKABLE static QVariantList files(const QString& path);
	// installs a .pkg (RPCS3 --installpkg) or a .rap; the answer comes with finished()
	Q_INVOKABLE void install(const QString& path);

	// the content ID in an NPD header (.edat), or in a .pkg's header; empty if it isn't one
	static QString contentId(const QByteArray& head);
	// NPD license type: 1 network, 2 local (needs a .rap), 3 free; 0 if not an NPD header
	static int licenseType(const QByteArray& head);

signals:
	void busyChanged();
	void finished(bool ok, const QString& message);

private:
	QProcess m_install;
};
