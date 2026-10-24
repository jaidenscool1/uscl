#include "gamepad.h"
#include "games.h"
#include "launcher.h"
#include "online.h"
#include "system.h"

#include <QFontDatabase>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QTemporaryFile>
#include <QtEndian>

#include <cstdio>

namespace
{
	// Checks the readers against made-up data: uscl --self-test
	int selfTest()
	{
		int failed = 0;
		const auto check = [&](bool ok, const char* what) {
			if (!ok)
			{
				failed++;
				qWarning("FAIL %s", what);
			}
		};

		// PARAM.SFO with TITLE and TITLE_ID
		QByteArray sfo(20, '\0');
		const QByteArray keys = QByteArray("TITLE\0TITLE_ID\0", 15);
		const QByteArray values = QByteArray("Test Game\0", 10) + QByteArray("BLUS12345\0", 10);
		const auto put32 = [](QByteArray& b, int at, quint32 v) { qToLittleEndian(v, b.data() + at); };
		sfo.replace(0, 4, QByteArray("\0PSF", 4));
		put32(sfo, 8, 20 + 2 * 16);
		put32(sfo, 12, 20 + 2 * 16 + keys.size());
		put32(sfo, 16, 2);
		for (int i = 0; i < 2; i++)
		{
			QByteArray e(16, '\0');
			qToLittleEndian<quint16>(i ? 6 : 0, e.data());
			qToLittleEndian<quint16>(0x0204, e.data() + 2);
			put32(e, 4, 10);
			put32(e, 8, 10);
			put32(e, 12, i * 10);
			sfo += e;
		}
		sfo += keys + values;
		const auto params = GameModel::readSfo(sfo);
		check(params.value("TITLE") == "Test Game" && params.value("TITLE_ID") == "BLUS12345", "PARAM.SFO");

		const auto yml = GameModel::readGamesYml("BLUS1: /a/b c.iso\nBLUS2: '/it''s/x'\nBLUS3: \"/q\\\"/y\"\n");
		check(yml.value("BLUS1") == "/a/b c.iso" && yml.value("BLUS2") == "/it's/x" && yml.value("BLUS3") == "/q\"/y", "games.yml");

		// ISO 9660: root -> PS3_GAME -> PARAM.SFO, records as in a real image
		QByteArray iso(2048 * 20, '\0');
		const auto record = [&](int at, quint32 lba, quint32 size, const QByteArray& name, bool dir) {
			iso[at] = char(33 + name.size());
			put32(iso, at + 2, lba);
			put32(iso, at + 10, size);
			iso[at + 25] = dir ? 2 : 0;
			iso[at + 32] = char(name.size());
			iso.replace(at + 33, name.size(), name);
			return 33 + int(name.size()) + (name.size() % 2 == 0 ? 1 : 0);
		};
		iso.replace(16 * 2048 + 1, 5, "CD001");
		record(16 * 2048 + 156, 17, 2048, QByteArray(1, '\0'), true);
		int at = 17 * 2048;
		at += record(at, 17, 2048, QByteArray(1, '\0'), true);
		record(at, 18, 2048, "PS3_GAME", true);
		record(18 * 2048, 19, quint32(sfo.size()), "PARAM.SFO;1", false);
		iso.replace(19 * 2048, sfo.size(), sfo);
		QTemporaryFile tmp;
		check(tmp.open() && tmp.write(iso) == iso.size() && tmp.flush(), "temp file");
		check(GameModel::readIsoFile(tmp.fileName(), {"PS3_GAME", "PARAM.SFO"}) == sfo, "ISO 9660 read");
		check(GameModel::readIsoFile(tmp.fileName(), {"PS3_GAME", "ICON0.PNG"}).isEmpty(), "ISO 9660 missing file");

		// online names from Steam persona names
		check(Online::sanitizeName("Jeffery Epstein") == "Jeffery_Epstein", "name: spaces");
		check(Online::sanitizeName("ab") == "abPlayer", "name: too short");
		check(Online::sanitizeName("Zoë ★ the Great Destroyer") == "Zo_the_Great_Des", "name: symbols, length");
		check(Online::personaFromVdf("\"users\"\n{\n\t\"1\"\n\t{\n\t\t\"PersonaName\"\t\t\"Old\"\n\t\t\"timestamp\"\t\t\"5\"\n\t}\n"
		                             "\t\"2\"\n\t{\n\t\t\"PersonaName\"\t\t\"New One\"\n\t\t\"timestamp\"\t\t\"9\"\n\t}\n}\n") == "New One", "loginusers.vdf");

		// RPCS3 YAML edits keep everything else
		const QString cfg = "Audio:\n  Volume: 100\nNet:\n  Bind address: 1.2.3.4\n  PSN status: Disconnected\nVFS:\n  x: y\n";
		QString edited = Online::setYamlValue(cfg, "Net", "PSN status", "RPCN");
		edited = Online::setYamlValue(edited, "Net", "IP swap list", "a=1&&b=2");
		check(Online::yamlValue(edited, "Net", "PSN status") == "RPCN" && Online::yamlValue(edited, "Net", "IP swap list") == "a=1&&b=2" &&
		      Online::yamlValue(edited, "Net", "Bind address") == "1.2.3.4" && Online::yamlValue(edited, "VFS", "x") == "y" &&
		      Online::yamlValue(edited, "Audio", "Volume") == "100", "YAML section edit");
		check(Online::yamlValue(Online::setYamlValue("", "Net", "PSN status", "RPCN"), "Net", "PSN status") == "RPCN", "YAML new file");
		const QString rpcn = Online::setYamlValue("Version: 2\nNPID: old\n", {}, "NPID", "new_name");
		check(Online::yamlValue(rpcn, {}, "NPID") == "new_name" && Online::yamlValue(rpcn, {}, "Version") == "2", "YAML top level");

		check(System::cdnUrl("Dest=84;ImageVersion=1;CDN=http://x.y/a/PS3PATCH.PUP;\nDest=84;CDN=http://x.y/b/PS3UPDAT.PUP;CDN_Timeout=30;") ==
		      "http://x.y/b/PS3UPDAT.PUP", "Sony update list");
		check(GameModel::readGamesYml(GameModel::ymlLine("BLUS1", "/a/it's/b.iso").toUtf8()).value("BLUS1") == "/a/it's/b.iso", "games.yml write");

		std::fprintf(stderr, failed ? "self-test: %d failed\n" : "self-test: ok\n", failed);
		return failed ? 1 : 0;
	}
}

int main(int argc, char* argv[])
{
	QGuiApplication app(argc, argv);
	QGuiApplication::setOrganizationName("uscl");
	QGuiApplication::setApplicationName("shell");
	QGuiApplication::setApplicationDisplayName("USCL");

	if (app.arguments().contains("--self-test"))
		return selfTest();

	for (const char* font : {"MaterialDesignIcons.ttf", "NotoSans-Regular.ttf", "NotoSans-Light.ttf", "Roboto-Thin.ttf"})
		QFontDatabase::addApplicationFont(QString(":/qt/qml/Uscl/fonts/") + font);
	QGuiApplication::setFont(QFont("Noto Sans"));

	GameModel games;
	Gamepad gamepad;
	Launcher launcher;
	System system;
	Online online;

	QQmlApplicationEngine engine;
	engine.rootContext()->setContextProperty("games", &games);
	engine.rootContext()->setContextProperty("gamepad", &gamepad);
	engine.rootContext()->setContextProperty("launcher", &launcher);
	engine.rootContext()->setContextProperty("system", &system);
	engine.rootContext()->setContextProperty("online", &online);
	engine.rootContext()->setContextProperty("windowed", app.arguments().contains("--windowed"));
	QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed, &app, [] { QCoreApplication::exit(1); }, Qt::QueuedConnection);
	engine.loadFromModule("Uscl", "Main");
	return app.exec();
}
