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

	// RPCS3's settings folder: ~/.config/uscl on Linux, the folder of rpcs3.exe on Windows
	static QString rpcs3ConfigDir();

	// Exposed for the self-test
	static QMap<QString, QString> readSfo(const QByteArray& sfo);
	static QByteArray readIsoFile(const QString& iso, const QStringList& path);
	static QMap<QString, QString> readGamesYml(const QByteArray& yml);

signals:
	void countChanged();

private:
	bool describe(const QString& path, Game& game) const;

	QVector<Game> m_games;
};
