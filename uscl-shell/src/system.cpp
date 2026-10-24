#include "system.h"
#include "games.h"

#include <QFile>
#include <QFileInfo>

QString System::onlineName() const
{
	// RPCS3's rpcn.yml: "NPID: name"; RPCS3 makes up "RPCS3_..." until the player picks one
	QFile f(GameModel::rpcs3ConfigDir() +
#ifdef _WIN32
		"config/"
#endif
		"rpcn.yml");
	if (!f.open(QIODevice::ReadOnly))
		return {};
	for (const QByteArray& line : f.readAll().split('\n'))
	{
		if (line.startsWith("NPID:"))
		{
			const QString name = QString::fromUtf8(line.mid(5)).trimmed();
			return name.startsWith("RPCS3_") ? QString() : name;
		}
	}
	return {};
}

bool System::firmwareInstalled() const
{
	// what RPCS3's firmware installer puts in place first
	return QFileInfo::exists(GameModel::rpcs3ConfigDir() + "dev_flash/vsh/module/vsh.self");
}
