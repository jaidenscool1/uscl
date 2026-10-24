#pragma once

#include <QAbstractListModel>
#include <QMap>
#include <QVector>

// The games RPCS3 knows about: games.yml (discs, ISOs, folders added with Open / Add Games) and games
// installed to dev_hdd0. Title, serial and icon come from each game's PARAM.SFO and ICON0.PNG.
class GameModel : public QAbstractListModel
{
	Q_OBJECT
	Q_PROPERTY(int count READ rowCount NOTIFY countChanged)
	Q_PROPERTY(bool scanning READ scanning NOTIFY scanningChanged)

public:
	enum Role { TitleRole = Qt::UserRole + 1, SerialRole, PathRole, IconRole, ConsoleRole };

	struct Game
	{
		QString title, serial, path, icon, console;
	};

	explicit GameModel(QObject* parent = nullptr);

	int rowCount(const QModelIndex& parent = {}) const override;
	QVariant data(const QModelIndex& index, int role) const override;
	QHash<int, QByteArray> roleNames() const override;

	Q_INVOKABLE void reload();
	Q_INVOKABLE QVariantMap get(int row) const;

	// Add Games: finds every PS3 game under a folder (disc folders, installed games, ISOs) and adds the new ones
	// to RPCS3's games.yml; scanFinished reports how many were new
	Q_INVOKABLE void scanFolder(const QString& path);
	// takes a game off the list (games.yml); its files stay where they are
	Q_INVOKABLE void remove(const QString& serial);
	bool scanning() const { return m_scanning; }
	// for the folder browser: drives / home, and the folders inside one
	Q_INVOKABLE static QVariantList roots();
	Q_INVOKABLE static QVariantList folders(const QString& path);
	// serial -> path of every game under a folder
	static QMap<QString, QString> findGames(const QString& path);
	static QString ymlLine(const QString& serial, const QString& path);

	// RPCS3's settings folder: ~/.config/uscl on Linux, the folder of rpcs3.exe on Windows
	static QString rpcs3ConfigDir();

	// Exposed for the self-test
	static QMap<QString, QString> readSfo(const QByteArray& sfo);
	static QByteArray readIsoFile(const QString& iso, const QStringList& path);
	static QMap<QString, QString> readGamesYml(const QByteArray& yml);

signals:
	void countChanged();
	void scanningChanged();
	void scanFinished(int added);

private:
	bool describe(const QString& path, Game& game) const;

	QVector<Game> m_games;
	bool m_scanning = false;
};
