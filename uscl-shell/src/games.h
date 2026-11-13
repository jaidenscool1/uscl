#pragma once

#include <QAbstractListModel>
#include <QMap>
#include <QVector>

// PS3 and PS4 games keep their existing YAML lists; Switch uses a separate JSON library.
class GameModel : public QAbstractListModel
{
	Q_OBJECT
	Q_PROPERTY(int count READ rowCount NOTIFY countChanged)
	Q_PROPERTY(bool scanning READ scanning NOTIFY scanningChanged)

public:
	enum Role { TitleRole = Qt::UserRole + 1, SerialRole, PathRole, IconRole, ConsoleRole, VersionRole };

	struct Game
	{
		QString title, serial, path, icon, console;
		QString version; // the disc's, or the installed update's when newer: "01.19"
	};

	explicit GameModel(QObject* parent = nullptr);

	int rowCount(const QModelIndex& parent = {}) const override;
	QVariant data(const QModelIndex& index, int role) const override;
	QHash<int, QByteArray> roleNames() const override;

	Q_INVOKABLE void reload();
	Q_INVOKABLE QVariantMap get(int row) const;

	// Add Games: PS3 entries go to games.yml; Switch entries go to their separate library.
	// scanFinished reports persisted additions, known entries, and apparently encrypted PS3 ISOs.
	Q_INVOKABLE void scanFolder(const QString& path);
	// takes a game off the list (games.yml); its files stay where they are
	Q_INVOKABLE void remove(const QString& serial);
	bool scanning() const { return m_scanning; }
	// for the folder browser: drives / home, and the folders inside one
	Q_INVOKABLE static QVariantList roots();
	Q_INVOKABLE static QVariantList folders(const QString& path);
	// serial -> path of every game under a folder
	static QMap<QString, QString> findGames(const QString& path, int* encrypted = nullptr);
	static QString ymlLine(const QString& serial, const QString& path);

	// The executable the game boots (an installed update's, else the disc's), as a short SHA-256: online sessions
	// check that everyone runs the same game files. Empty when there's no EBOOT.BIN to read.
	Q_INVOKABLE static QString gameHash(const QString& path, const QString& serial);

	// RPCS3's settings folder: ~/.config/uscl on Linux, the folder of rpcs3.exe on Windows
	static QString rpcs3ConfigDir();
	// PS4 games run in our shadPS4, not RPCS3
	Q_INVOKABLE static bool isPs4Serial(const QString& serial);

	// Exposed for the self-test
	static QMap<QString, QString> readSfo(const QByteArray& sfo);
	static QByteArray readIsoFile(const QString& iso, const QStringList& path);
	static QMap<QString, QString> readGamesYml(const QByteArray& yml);

signals:
	void countChanged();
	void scanningChanged();
	void scanFinished(int added, int known, int encrypted);
	void scanFailed(const QString& message);

private:
	bool describe(const QString& path, Game& game) const;
	void loadEmuGames(); // append Wii/3DS games from uscl/emu_games.json (run via Dolphin/Azahar, not RPCS3)

	QVector<Game> m_games;
	bool m_scanning = false;
};
