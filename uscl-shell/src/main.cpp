#include "gamepad.h"
#include "games.h"
#include "gameupdates.h"
#include "launcher.h"
#include "online.h"
#include "system.h"
#include "updater.h"

#include <QFontDatabase>
#include <QGuiApplication>
#include <QScreen>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QBuffer>
#include <QCryptographicHash>
#include <QDir>
#include <QTemporaryDir>
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
		check(Online::sanitizeName("Player One") == "Player_One", "name: spaces");
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

		check(System::scaleFor(true, 12000, 2160) == 300 && System::scaleFor(true, 8000, 2160) == 200 && System::scaleFor(true, 8000, 1080) == 150 &&
		      System::scaleFor(false, 16000, 2160) == 100 && System::scaleFor(true, 2000, 1080) == 100, "resolution scale");
		{
			const QString first = Launcher::inputYml("", {"Pad A", "Pad A"});
			check(first.contains("Player 1 Input:\n  Handler: SDL\n  Device: \"Pad A 1\"") && first.contains("Device: \"Pad A 2\"") &&
			      first.contains("Player 7 Input:\n  Handler: \"Null\""), "input config from scratch");
			// a player already on a controller keeps its mapping, only the device changes
			const QString custom = QString(first).replace("Cross: South", "Cross: North");
			const QString next = Launcher::inputYml(custom, {"Pad B"});
			check(next.contains("Device: \"Pad B 1\"") && next.contains("Cross: North") && next.contains("Device: \"Pad A 2\""), "input config keeps mapping");
		}

		check(Updater::compareVersions("v0.4.0", "0.3.9") > 0 && Updater::compareVersions("0.3.0", "0.3.0-test") > 0 &&
		      Updater::compareVersions("v0.3.0-test", "0.3.0") < 0 && Updater::compareVersions("v1.0", "1.0.0") == 0 &&
		      Updater::compareVersions("0.10.0", "0.9.9") > 0, "version compare");
		{
			// RFC 8032 Ed25519 test 1 (empty message): the real key check uses the same function
			const QByteArray pk = QByteArray::fromHex("d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a");
			const QByteArray sig = QByteArray::fromHex("e5564300c360ac729086e2cc806e828a84877f1eb8e5d974d873e065224901555fb8821590a33bacc61e39701cf9b46bd25bf5f0595bbe24655141438e7a100b");
			QByteArray bad = sig;
			bad[10] = char(bad[10] ^ 1);
			check(Updater::signatureOk({}, sig, pk) && !Updater::signatureOk({}, bad, pk) && !Updater::signatureOk("x", sig, pk), "update signature");
		}

		{
			QTemporaryDir dir;
			QDir().mkpath(dir.filePath("PS3_GAME/USRDIR"));
			QFile eboot(dir.filePath("PS3_GAME/USRDIR/EBOOT.BIN"));
			check(eboot.open(QIODevice::WriteOnly) && eboot.write("eboot") == 5, "temp EBOOT.BIN");
			eboot.close();
			const QString want = QString::fromLatin1(QCryptographicHash::hash("eboot", QCryptographicHash::Sha256).toHex().left(16));
			check(GameModel::gameHash(dir.path(), "BLUS99999") == want && GameModel::gameHash(eboot.fileName(), "BLUS99999") == want &&
			          GameModel::gameHash(dir.filePath("PS3_GAME"), "") == want,
			      "game hash (disc folder, EBOOT, PS3_GAME)");
			QTemporaryDir empty;
			check(GameModel::gameHash(empty.path(), "BLUS99999").isEmpty(), "game hash without EBOOT.BIN");
		}

		{
			// Sony's update list, newest first on the wire; we install oldest first
			const QByteArray xml = "<?xml version='1.0'?><titlepatch titleid=\"BLUS1\"><tag name=\"T\">"
			                       "<package version=\"01.19\" size=\"9\" sha1sum=\"0AAD7F0A4777F6CE82A3687D8EBC0F8D37BFF6A9\" url=\"http://x/b.pkg\"><paramsfo><TITLE>x</TITLE></paramsfo></package>"
			                       "<package version=\"01.09\" size=\"8\" sha1sum=\"a3557b87227c2c65193ecb2e9d637ed8da6e731e\" url=\"http://x/a.pkg\"/></tag></titlepatch>";
			const auto list = GameUpdates::parseList(xml);
			check(list.size() == 2 && list[0].version == "01.09" && list[1].version == "01.19" && list[1].sha1.startsWith("0aad") && list[1].size == 9,
			      "game update list");
			check(GameUpdates::newer("01.19", "01.09") && GameUpdates::newer("01.00", "") && !GameUpdates::newer("01.09", "01.19"), "game update versions");
			// SHA-1 of all but the last 32 bytes
			QByteArray pkg = "package body";
			pkg += QByteArray(32, 'x');
			QBuffer b(&pkg);
			b.open(QIODevice::ReadOnly);
			check(GameUpdates::packageSha1(b, pkg.size()) == QCryptographicHash::hash("package body", QCryptographicHash::Sha1).toHex(), "game update hash");
		}

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

	if (app.arguments().contains("--hardware"))
	{
		// what Automatic resolution scale sees on this PC
		System sys;
		const QScreen* screen = QGuiApplication::primaryScreen();
		const int height = screen ? qRound(screen->size().height() * screen->devicePixelRatio()) : 1080;
		std::printf("graphics: %s (%s, %d MB)\nscreen height: %d\nresolution scale: %d%%\n", qPrintable(sys.gpu().isEmpty() ? "none found" : sys.gpu()),
		            sys.discreteGpu() ? "discrete" : "integrated", sys.vramMB(), height, sys.recommendedScale(height));
		return 0;
	}

	for (const char* font : {"MaterialDesignIcons.ttf", "NotoSans-Regular.ttf", "NotoSans-Light.ttf", "Roboto-Thin.ttf"})
		QFontDatabase::addApplicationFont(QString(":/qt/qml/Uscl/fonts/") + font);
	QGuiApplication::setFont(QFont("Noto Sans"));

	GameModel games;
	Gamepad gamepad;
	Launcher launcher;
	System system;
	Online online;
	Updater updater;

	QQmlApplicationEngine engine;
	engine.rootContext()->setContextProperty("games", &games);
	engine.rootContext()->setContextProperty("gamepad", &gamepad);
	engine.rootContext()->setContextProperty("launcher", &launcher);
	engine.rootContext()->setContextProperty("system", &system);
	engine.rootContext()->setContextProperty("online", &online);
	engine.rootContext()->setContextProperty("updater", &updater);
	GameUpdates gameUpdates;
	engine.rootContext()->setContextProperty("gameUpdates", &gameUpdates);
	engine.rootContext()->setContextProperty("windowed", app.arguments().contains("--windowed"));
	QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed, &app, [] { QCoreApplication::exit(1); }, Qt::QueuedConnection);
	engine.loadFromModule("Uscl", "Main");
	return app.exec();
}
