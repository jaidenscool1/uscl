#include "discordparty.h"
#include "dlc.h"
#include "gamepad.h"
#include "games.h"
#include "gameupdates.h"
#include "launcher.h"
#include "padconfig.h"
#include "gameart.h"
#include "online.h"
#include "system.h"
#include "updater.h"
#include "switchcontent.h"
#include "switchcore.h"

#include <QFontDatabase>
#include <QGuiApplication>
#include <QIcon>
#include <QScreen>
#include <QSettings>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQmlExpression>
#include <QQuickItem>
#include <QQuickWindow>
#include <QKeyEvent>
#include <QBuffer>
#include <QCryptographicHash>
#include <QDir>
#include <QTemporaryDir>
#include <QTemporaryFile>
#include <QEventLoop>
#include <QTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QtEndian>

#include <cstdio>
#include <memory>

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
				std::fprintf(stderr, "FAIL %s\n", what); // not qWarning: a Windows GUI exe sends that to the debugger only
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
		QByteArray rpc = DiscordParty::frame(1, {{"cmd", "A"}}) + DiscordParty::frame(3, {{"n", 2}});
		const QByteArray tail = rpc.right(3);
		rpc.chop(3); // second frame cut short: comes out once the rest arrives
		auto frames = DiscordParty::takeFrames(rpc);
		const bool first_only = frames.size() == 1 && frames[0].first == 1 && frames[0].second.value("cmd") == "A";
		rpc += tail;
		frames = DiscordParty::takeFrames(rpc);
		check(first_only && frames.size() == 1 && frames[0].first == 3 && frames[0].second.value("n") == 2 && rpc.isEmpty(), "Discord RPC frames");
		auto ids = [](const QVariantList& l) { QStringList out; for (const QVariant& v : l) out << v.toMap().value("id").toString(); return out; };
		const QVariantList servers{QVariantMap{{"id", "a"}}, QVariantMap{{"id", "b"}}, QVariantMap{{"id", "c"}}, QVariantMap{{"id", "d"}}};
		check(ids(DiscordParty::sortedServers(servers, "c", {"d", "b"})) == QStringList{"c", "d", "b", "a"} &&
		      ids(DiscordParty::sortedServers(servers, "", {})) == QStringList{"a", "b", "c", "d"}, "Discord server order");
		{
			// the USCL ticker, from the tracker built into the app (GAMES.md): one row of each status
			const auto level = [](const QString& serial) { return Online::capability(serial).value("level").toString(); };
			check(level("BLUS31011") == "online" && level("BLUS31426") == "beta" && level("BLUS31436") == "soon", "online ticker from GAMES.md");
			check(level("0400C3F00006E000") == "offline" && level("0100152000022000") == "soon", "online ticker for Switch rows");
			check(level("0100AAAA00000000") == "beta" && Online::capability("0100AAAA00000000").value("detail").toString().contains("tested"),
			      "untracked game with an online route is an untested beta");
			check(level("BLUS99999") == "offline" && level("") == "offline", "untracked PS3 game is offline");
			check(Online::capability("BLUS1", "| Game | [BLUS1](games/BLUS1.md) | 1.0 | PSN | ✅ plays | next |").value("level") == "online" &&
			      Online::capability("BLUS2", "| Game | BLUS2 | own | ❌ none |").value("level") == "offline", "ticker row formats");
		}
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

		check(Launcher::loadingStyle("not-a-color", "white", "red", "black").isEmpty(), "invalid loading palette rejected");
		{
			QTemporaryDir missing;
			const QByteArray old = qgetenv("USCL_RPCS3");
			const bool was_set = qEnvironmentVariableIsSet("USCL_RPCS3");
			qputenv("USCL_RPCS3", missing.filePath("missing-core").toUtf8());
			Launcher failed_launch;
			QEventLoop loop;
			bool failed_start = false, hidden = false;
			QObject::connect(&failed_launch, &Launcher::gameShownChanged, &loop, [&] { hidden = !failed_launch.gameShown(); });
			QObject::connect(&failed_launch, &Launcher::finished, &loop, [&](bool crashed, const QString&) { failed_start = crashed; loop.quit(); });
			QTimer::singleShot(3000, &loop, &QEventLoop::quit);
			check(failed_launch.startGame("unused", "Missing", false), "failed core launch can enter starting state");
			loop.exec();
			check(failed_start && hidden && !failed_launch.running() && !failed_launch.starting() && !failed_launch.gameShown(), "failed launch clears themed handoff state");
			if (was_set) qputenv("USCL_RPCS3", old); else qunsetenv("USCL_RPCS3");
		}
		const QString style = Launcher::loadingStyle("#073ba0", "#ffffff", "#79c9ff", "#114ca9");
		check(style.contains("#073ba0") && style.contains("#ffffff") && style.contains("#79c9ff") &&
		      style.contains("#114ca9") && style.contains("QProgressBar::chunk"), "loading stylesheet uses validated palette colors");

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
		{
			// other emulators: pads in order become players 1, 2; motion pads drive the Wii Remote's sensors
			PadConfig::Pad ds{"PS5 Controller", "050000004c050000e60c000000810000", 0, 0, false, true};
			PadConfig::Pad xb{"Xbox Series X Controller", "030000005e040000130b000011050000", 0, 0, false, false};
			const QString wii = PadConfig::dolphinIni("[Wiimote1]\nDevice = Old\nSource = 1\n[Wiimote2]\nSource = 1\n[BalanceBoard]\nSource = 0\n", "Wiimote", {ds, xb});
			check(wii.count("[Wiimote1]") == 1 && wii.contains("[Wiimote1]\nDevice = SDL/0/PS5 Controller\nSource = 1\n") &&
			      wii.contains("[Wiimote2]\nDevice = SDL/0/Xbox Series X Controller\nSource = 1\n") && wii.contains("[Wiimote3]\nSource = 0\n") &&
			      wii.contains("[BalanceBoard]\nSource = 0") && !wii.contains("Old") && wii.contains("Extension = toggle(`Thumb L`)") && wii.contains("Rumble/Motor = `Motor L` | `Motor R`") &&
			      wii.contains("Nunchuk/Swing/Forward = `Shoulder L`") &&
			      wii.section("[Wiimote2]", 1).contains("Swing/Right = max(0, 2 * (`Right X+` - smooth(`Right X+`, 0.3)) - 1)") &&
			      wii.section("[Wiimote2]", 1).contains("Tilt/Backward = `Left Y+`") && !wii.section("[Wiimote2]", 0, 0).contains("Swing/Right") &&
			      wii.section("[Wiimote2]", 0, 0).contains("IMUGyroscope/Pitch Up = `Gyro Pitch Up`") &&
			      wii.section("[Wiimote2]", 0, 0).contains("IMUIR/Enabled = False\nIR/Up = `Right Y-`") &&
			      !wii.section("[Wiimote2]", 1).contains("IMUGyroscope") && wii.section("[Wiimote2]", 1).contains("IR/Up = `Right Y-`"), "Dolphin Wii Remotes");
			const QString gc = PadConfig::dolphinIni("[GCPad2]\nDevice = Keep\n", "GCPad", {ds});
			check(gc.contains("[GCPad1]\nDevice = SDL/0/PS5 Controller\n") && gc.contains("[GCPad2]\nDevice = Keep") && !gc.contains("[GCPad3]"), "Dolphin GameCube pads");
			const QString cemu = PadConfig::cemuProfile(ds, true);
			check(cemu.contains("<uuid>0_050000004c050000e60c000000810000</uuid>") && cemu.contains("<type>Wii U GamePad</type>") &&
			      cemu.contains("<mapping>1</mapping>\n\t\t\t\t<button>1</button>") && cemu.contains("<motion>true</motion>") &&
			      PadConfig::cemuProfile(xb, false).contains("<mapping>11</mapping>\n\t\t\t\t<button>5</button>"), "Cemu profiles");
			check(PadConfig::azaharButton(0, false).endsWith("button:1") && PadConfig::azaharButton(0, true).endsWith("button:0") &&
			      PadConfig::azaharButton(14, false).contains("axis:4") && PadConfig::azaharButton(12, false).isEmpty() &&
			      PadConfig::azaharButton(0, false).contains("maptype:all"), "Azahar buttons");
		}
		{
			// game art: libretro names match our titles and file names; ids come out of Wii / 3DS game files
			check(GameArt::normName("Legend of Zelda, The - A Link Between Worlds (USA) (En,Fr,Es)") == GameArt::normName("Legend of Zelda: A Link Between Worlds") &&
			      GameArt::normName("Pokémon X (USA)") == GameArt::normName("Pokemon X"), "art name matching");
			const auto names = GameArt::parseListing("<a href=\"Mario%20Kart%207%20%28Europe%29.png\">x</a><a href=\"Mario%20Kart%207%20%28USA%29.png\">x</a><a href=\"../\">up</a>");
			check(names.size() == 1 && names.value(GameArt::normName("Mario Kart 7")) == "Mario Kart 7 (USA)", "art listing (prefers USA)");
			QTemporaryFile wbfs, cci;
			if (wbfs.open() && cci.open())
			{
				QByteArray w(0x206, '\0');
				w.replace(0, 4, "WBFS"); w.replace(0x200, 6, "SP2E01");
				wbfs.write(w); wbfs.flush();
				QByteArray c(0x600, '\0');
				c.replace(0x100, 4, "NCSD"); c[0x120] = 1; // partition 0 at 1 * 0x200
				c.replace(0x300, 4, "NCCH"); c.replace(0x350, 10, "CTR-P-AMKE");
				cci.write(c); cci.flush();
				check(GameArt::gameId(wbfs.fileName(), "Wii") == "SP2E01" && GameArt::gameId(cci.fileName(), "3DS") == "AMKE" &&
				      GameArt::gameId(wbfs.fileName(), "3DS").isEmpty(), "Wii / 3DS game ids");
			}
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
			// installed PS4 game: sce_sys/param.sfo next to eboot.bin
			QTemporaryDir dir;
			QDir(dir.path()).mkpath("sce_sys");
			QFile sfo4(dir.filePath("sce_sys/param.sfo")), eboot(dir.filePath("eboot.bin"));
			sfo4.open(QIODevice::WriteOnly);
			sfo4.write(QByteArray(sfo).replace("BLUS12345", "CUSA12345"));
			sfo4.close();
			eboot.open(QIODevice::WriteOnly);
			eboot.write("ps4");
			eboot.close();
			const QString want = QString::fromLatin1(QCryptographicHash::hash("ps4", QCryptographicHash::Sha256).toHex().left(16));
			check(GameModel::gameHash(dir.path(), "CUSA12345") == want, "game hash (PS4 eboot.bin)");
			check(GameModel::isPs4Serial("CUSA12345") && !GameModel::isPs4Serial("BLUS12345"), "PS4 serials");
		}

		{
			// DLC headers: NPD (.edat) with license type, PKG
			QByteArray npd = QByteArray("NPD\0\0\0\0\4\0\0\0\2\0\0\0\0", 16) + "UP0002-BLUS31011_00-CODBLOPS2PATCH06" + QByteArray(12, '\0');
			check(Dlc::contentId(npd) == "UP0002-BLUS31011_00-CODBLOPS2PATCH06" && Dlc::licenseType(npd) == 2, "DLC .edat header");
			QByteArray pkg = QByteArray("\x7FPKG", 4) + QByteArray(0x2C, '\0') + "UP0002-BLUS31011_00-CODBLOPS2PATCH06" + QByteArray(12, '\0');
			check(Dlc::contentId(pkg) == "UP0002-BLUS31011_00-CODBLOPS2PATCH06" && Dlc::licenseType(pkg) == 0, "DLC .pkg header");
			check(Dlc::contentId(QByteArray("NPD\0", 4) + QByteArray(60, 'x')).isEmpty(), "DLC header without a content ID");
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
			// online only on the newest update
			GameUpdates::setLatest("TEST00001", "01.03");
			check(GameUpdates::requiredVersion("TEST00001", "01.00") == "01.03" && GameUpdates::requiredVersion("TEST00001", "01.03").isEmpty() &&
			      GameUpdates::requiredVersion("TEST00002", "01.00").isEmpty(), "online needs the newest game update");
			QSettings().remove("latest_update/TEST00001");
		}

		{
			QTemporaryDir root;
			const QByteArray previous = qgetenv("XDG_CONFIG_HOME");
			const bool was_set = qEnvironmentVariableIsSet("XDG_CONFIG_HOME");
			const QByteArray old_profile = qgetenv("USCL_SWITCH_PROFILE");
			const bool profile_set = qEnvironmentVariableIsSet("USCL_SWITCH_PROFILE");
			const QByteArray old_frontend = qgetenv("USCL_SWITCH_FRONTEND");
			const bool frontend_set = qEnvironmentVariableIsSet("USCL_SWITCH_FRONTEND");
			qputenv("USCL_SWITCH_FRONTEND", "sdl");
			qputenv("XDG_CONFIG_HOME", root.path().toUtf8());
			qputenv("USCL_SWITCH_PROFILE", root.filePath("profile").toUtf8());
			const auto write = [&](const QString& name, const QByteArray& bytes) {
				const QString path = root.filePath("games/" + name);
				QDir().mkpath(QFileInfo(path).absolutePath());
				QFile f(path);
				check(f.open(QIODevice::WriteOnly) && f.write(bytes) == bytes.size(), "Switch fixture");
				return path;
			};
			QByteArray nsp(48, '\0');
			nsp.replace(0, 4, "PFS0");
			put32(nsp, 4, 1);
			put32(nsp, 8, 8);
			const QString kart = write("Mario Kart 8 Deluxe [0100152000022000][v0].nsp", nsp);
			write("Mario Kart Update [0100152000022800].nsp", nsp);
			write("Mario Kart DLC [0100152000023000].nsp", nsp);
			write("broken.nsp", "PFS0");
			write("archive.nsp.rar", nsp);
			QByteArray nro(0x80, '\0');
			nro.replace(0x10, 4, "NRO0");
			const QString homebrew = write("sample.nro", nro);
			const QString moved_homebrew = write("moved/sample.nro", nro);
			check(SwitchCore::describe(homebrew).value("serial") == SwitchCore::describe(moved_homebrew).value("serial"), "Switch fallback identity survives relocation");
			const auto found = GameModel::findGames(root.filePath("games"));
			check(found.size() == 2 && found.value("0100152000022000") == kart, "Switch scan excludes archives, updates, DLC and truncated containers");
			check(SwitchCore::describe(kart).value("console") == "Switch" && SwitchCore::describe(kart).value("title") == "Mario Kart 8 Deluxe", "Switch description");
			{
				// the other consoles, from their headers: a Wii disc and a DS cartridge
				QByteArray wii(0x60, '\0');
				wii.replace(0, 6, "RSPE01");
				wii.replace(0x18, 4, QByteArray("\x5D\x1C\x9E\xA3", 4));
				wii.replace(0x20, 10, "Wii Sports");
				QByteArray nds(0x200, '\0');
				nds.replace(0xC, 4, "AMCE");
				nds.replace(0x15C, 2, QByteArray("\x56\xCF", 2));
				const QVariantMap disc = GameModel::emuGame(write("emu/wii sports (USA) [!].iso", wii));
				const QVariantMap cart = GameModel::emuGame(write("emu/Mario Kart DS (USA).nds", nds));
				check(disc.value("console") == "Wii" && disc.value("serial") == "Wii-RSPE01" && disc.value("title") == "Wii Sports", "Wii disc from its header");
				check(cart.value("console") == "DS" && cart.value("serial") == "DS-AMCE" && cart.value("title") == "Mario Kart DS", "DS cartridge from its header");
				check(GameModel::emuGame(kart).isEmpty() && GameModel::isEmuSerial("3DS-CTRPAMKE") && !GameModel::isEmuSerial("BLUS31011"), "emulator game ids");
				QDir(root.filePath("games/emu")).removeRecursively();
			}
			check(SwitchCore::isSerial("0100152000022000") && !SwitchCore::isSerial("BLUS31426"), "console ID separation");
			check(Online::supports("0100152000022000") && !Online::supports("0400C3F00006E000"), "Switch games (not Switch 2) support automatic USCL session hosting");
			{
				// Switch 2: extracted dumps (exefs/main + main.npdm); a full update dump wins over base
				const auto dump = [&](const QString& dir, quint64 id) {
					QByteArray npdm(0x100, '\0');
					npdm.replace(0, 4, "META");
					put32(npdm, 0x70, 0x80);
					npdm.replace(0x80, 4, "ACI0");
					put32(npdm, 0x90, quint32(id));
					put32(npdm, 0x94, quint32(id >> 32));
					write("ns2/" + dir + "/exefs/main.npdm", npdm);
					return write("ns2/" + dir + "/exefs/main", QByteArray("NSO0").leftJustified(0x100, '\0'));
				};
				const QString world = dump("Kart World [ROM]/base", 0x0400C3F00006E000);
				dump("SwitchSports_v1.1.0/base", 0x04008E001F098000);
				dump("SwitchSports_v1.1.0/updates/1", 0x04008E001F098000);
				const QString sports = dump("SwitchSports_v1.1.0/updates/65536", 0x04008E001F098000);
				write("ns2/stray/exefs/main", "NSO0");
				const auto ns2 = GameModel::findGames(root.filePath("games/ns2"));
				check(ns2.size() == 2 && ns2.value("0400C3F00006E000") == world && ns2.value("04008E001F098000") == sports,
				      "Switch 2 scan finds extracted dumps, newest update only");
				check(SwitchCore::describe(world).value("title") == "Kart World" && SwitchCore::describe(sports).value("title") == "Switch Sports",
				      "Switch 2 dump titles");
				check(SwitchCore::isSwitch2(world) && !SwitchCore::isSwitch2(kart), "Switch 2 detection");
				qputenv("USCL_SWITCH2_CORE", "/opt/eglantine");
				check(SwitchCore::executable(false, world) == "/opt/eglantine" && SwitchCore::executable(false, kart) != "/opt/eglantine",
				      "Switch 2 games use their own core");
				check(SwitchCore::arguments(world, true, "P") == QStringList{"-f", "-g", world}, "Switch 2 launch arguments");
				check(SwitchCore::prerequisiteError(world).contains("Eglantine"), "Switch 2 reports a missing core");
				qunsetenv("USCL_SWITCH2_CORE");
				{
					// Nothing chosen: games run in the graphical eden (the tested one), not eden-cli, which shows a blank window
					QSettings settings;
					QMap<QString, QVariant> saved;
					for (const QString& key : {"switch/frontend", "switch/USCL_SWITCH_CORE"})
						if (settings.contains(key)) saved.insert(key, settings.value(key)), settings.remove(key);
					const QByteArray old_core = qgetenv("USCL_SWITCH_CORE"), old_gui = qgetenv("USCL_SWITCH_GUI");
					const bool core_set = qEnvironmentVariableIsSet("USCL_SWITCH_CORE"), gui_set = qEnvironmentVariableIsSet("USCL_SWITCH_GUI");
					qunsetenv("USCL_SWITCH_FRONTEND");
					qunsetenv("USCL_SWITCH_CORE");
					const QString gui = write("bin/eden", "#!/bin/sh\n");
					QFile(gui).setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
					qputenv("USCL_SWITCH_GUI", gui.toUtf8());
					check(SwitchCore::executable(false, kart) == gui && SwitchCore::arguments(kart, false, "P") == QStringList{"-f", "-g", kart},
					      "Switch games default to the graphical eden");
					check(SwitchCore::arguments(kart, true, "Player").contains("--uscl-room"), "default Switch core joins rooms the patched way");
					qputenv("USCL_SWITCH_CORE", "/opt/eden-cli");
					check(SwitchCore::executable(false, kart) == "/opt/eden-cli" && SwitchCore::arguments(kart, false, "P").contains("--fullscreen"),
					      "a chosen Switch core keeps the CLI frontend");
					settings.setValue("switch/frontend", "sdl");
					qunsetenv("USCL_SWITCH_CORE");
					check(!SwitchCore::arguments(kart, false, "P").contains("-g"), "a chosen SDL frontend is kept");
					settings.remove("switch/frontend");
					for (auto it = saved.cbegin(); it != saved.cend(); ++it) settings.setValue(it.key(), it.value());
					if (core_set) qputenv("USCL_SWITCH_CORE", old_core); else qunsetenv("USCL_SWITCH_CORE");
					if (gui_set) qputenv("USCL_SWITCH_GUI", old_gui); else qunsetenv("USCL_SWITCH_GUI");
					qputenv("USCL_SWITCH_FRONTEND", "sdl");
					QFile::remove(gui);
				}
				QDir(root.filePath("games/ns2")).removeRecursively(); // the scans below count the Switch 1 fixtures only
			}
			{
				check(Online::bridgeSession("uscl-bridge: session 12345 code ABC234 (hosting); give friends this number or code") == QStringList{"12345", "hosting"}, "current bridge hosting starts the waiting game");
				check(Online::bridgeSession("uscl-bridge: session 12345 code ABC234 (joined);") == QStringList{"12345", "joined"} &&
				      Online::bridgeSession("uscl-bridge: session 12345 (hosting)") == QStringList{"12345", "hosting"} &&
				      Online::bridgeSession("uscl-bridge: looking for a friend's USCL session").isEmpty(), "bridge joins, legacy output and waiting messages");
				const QString target = root.filePath("linked-library");
				QDir().mkpath(target);
				QFile::copy(kart, target + "/Linked Kart [0100000000006000].NSP");
#ifdef _WIN32
				QProcess junction;
				junction.start("cmd.exe", {"/c", "mklink", "/J", QDir::toNativeSeparators(root.filePath("games/library-link")), QDir::toNativeSeparators(target)});
				check(junction.waitForFinished() && junction.exitCode() == 0, "Windows library junction fixture");
#else
				check(QFile::link(target, root.filePath("games/library-link")), "linked library fixture");
				check(QFile::link(root.filePath("games"), target + "/cycle"), "linked library cycle fixture");
#endif
				const auto linked = GameModel::findGames(QUrl::fromLocalFile(root.filePath("games/library-link")).toString());
				check(linked.contains("0100000000006000"), "game scan accepts local file URLs");
				check(GameModel::findGames(root.filePath("games")).contains("0100000000006000"), "parent scan discovers games inside child junctions without following cycles forever");
				QDir().rmdir(root.filePath("games/library-link"));
				QFile::remove(root.filePath("games/library-link"));
				const auto oldAppData = qgetenv("APPDATA");
				const bool hadAppData = qEnvironmentVariableIsSet("APPDATA");
				qputenv("APPDATA", root.filePath("games/key-discovery").toUtf8());
				const QString prod = write("key-discovery/eden/keys/prod.keys", "header_key = " + QByteArray(64, '1') + "\r\n");
				const QString title = write("key-discovery/eden/keys/title.keys", QByteArray(32, '1') + " = " + QByteArray(32, '2') + "\r\n");
				const auto discovered = SwitchCore::findConsoleFiles({root.filePath("games/empty-game-drive")});
				check(discovered.value("titleKeyFiles").toStringList().contains(title), "selected games folder also searches emulator profiles");
				QString error;
				SwitchCore::installConsoleFiles({{"keys", prod}, {"master", -1}, {"titleKeys", title}}, &error);
				check(error.isEmpty() && QFileInfo::exists(SwitchCore::dataDir() + "keys/prod.keys"), "automatic install accepts header keys without master keys");
				write("key-discovery/eden/keys/title.keys", QByteArray(32, '3') + " = " + QByteArray(32, '4') + "\n");
				SwitchCore::installConsoleFiles({{"keys", prod}, {"master", -1}, {"titleKeys", title}}, &error);
				QFile installed(SwitchCore::dataDir() + "keys/title.keys");
				check(error.isEmpty() && installed.open(QIODevice::ReadOnly) && installed.readAll().count('=') == 2, "same-generation title keys refresh without losing previous games");
				installed.close();
				for (const QString& core : {SwitchCore::dataDir(), SwitchCore::profileDir() + "data/eglantine/"}) QDir(core + "keys").removeRecursively();
				if (hadAppData) qputenv("APPDATA", oldAppData); else qunsetenv("APPDATA");
				QDir(root.filePath("games/key-discovery")).removeRecursively();
			}
			const auto args = SwitchCore::arguments(kart, true, "A@:B");
			check(args.contains("--game") && args.last() == kart && args.contains("A__B@127.0.0.1:24872") &&
			      !SwitchCore::arguments(kart, false, "Player").contains("--multiplayer"), "Switch launch and room arguments");
			qputenv("USCL_SWITCH_FRONTEND", "qt");
			const auto qt_args = SwitchCore::arguments(kart, true, "TestPlayer");
			check(qt_args.contains("--uscl-room") && qt_args.contains("-g") && !qt_args.contains("--config") &&
			      qt_args.last() == kart && !SwitchCore::arguments(kart, false, "Player").contains("--uscl-room"), "Switch Qt room and offline launch arguments");
			qputenv("USCL_SWITCH_FRONTEND", "sdl");
			{
				const QString sdl_path = args.at(args.indexOf("--config") + 1);
				const QString gui_path = QFileInfo(sdl_path).absolutePath() + "/qt-config.ini";
				QDir().mkpath(QFileInfo(gui_path).absolutePath());
				QSettings gui(gui_path, QSettings::IniFormat);
				gui.setValue("Controls/player_0_button_a", "engine:keyboard,code:67");
				gui.setValue("Controls/player_0_button_a/default", true);
				gui.setValue("Controls/player_0_button_l", "engine:sdl,guid:test-device,button:4");
				gui.setValue("Controls/player_0_lstick", "engine:analog_from_button,up:engine$0keyboard$1code$016777235,down:engine$0keyboard$1code$083,modifier:engine$0keyboard$1code$016777248");
				gui.setValue("Renderer/use_asynchronous_gpu_emulation", true);
				gui.sync();
				check(SwitchCore::prepareLaunchConfig().isEmpty(), "Switch SDL configuration generated");
				QSettings sdl(sdl_path, QSettings::IniFormat);
				check(sdl.value("Controls/player_0_button_a").toString().contains("code:6") &&
				      !sdl.value("Controls/player_0_button_a/default").toBool() &&
				      sdl.value("Controls/player_0_lstick").toString().contains("code$082") &&
				      sdl.value("Controls/player_0_lstick").toString().contains("code$022") &&
				      sdl.value("Controls/player_0_lstick").toString().contains("code$0225"), "Switch converts Qt buttons and nested keyboard bindings to SDL scancodes");
				check(sdl.value("Controls/player_0_button_l").toString().contains("guid:test-device") &&
				      sdl.value("Renderer/use_asynchronous_gpu_emulation").toBool(), "Switch preserves controller identity and renderer settings");
				check(gui.value("Controls/player_0_button_a").toString() == "engine:keyboard,code:67", "Switch preserves graphical frontend controls");
			}
			check(!SwitchCore::importKeys(kart).isEmpty(), "Switch rejects a game as a key file");
			check(!SwitchCore::importFirmware(root.filePath("games")).isEmpty(), "Switch rejects firmware folder without NCAs");
			{
				const QString core = SwitchCore::dataDir();
				const QString registered = core + "nand/system/Contents/registered/";
				QDir().mkpath(registered);
				QFile oldFirmware(registered + "old.nca");
				check(oldFirmware.open(QIODevice::WriteOnly) && oldFirmware.write("old firmware") == 12, "firmware preservation fixture");
				oldFirmware.close();
				QFile version(core + "firmware-version.txt");
				check(version.open(QIODevice::WriteOnly) && version.write("1.0.0") == 5, "firmware version preservation fixture");
				version.close();
				QString error;
				SwitchCore::installConsoleFiles({{"firmware", root.filePath("games")}, {"version", "2.0.0"}, {"master", -1}}, &error);
				check(!error.isEmpty() && oldFirmware.open(QIODevice::ReadOnly) && oldFirmware.readAll() == "old firmware" &&
				      version.open(QIODevice::ReadOnly) && version.readAll() == "1.0.0", "failed firmware import preserves installed files and version");
				oldFirmware.close();
				version.close();
				write("new-firmware/new.nca", "new firmware");
				error.clear();
				SwitchCore::installConsoleFiles({{"firmware", root.filePath("games/new-firmware")}, {"version", "2.0.0"}, {"master", -1}}, &error);
				check(error.isEmpty() && !QFileInfo::exists(registered + "old.nca") && QFileInfo::exists(registered + "new.nca") &&
				      version.open(QIODevice::ReadOnly) && version.readAll() == "2.0.0", "successful firmware replacement contains only the new version");
				version.close();
				QDir(root.filePath("games/new-firmware")).removeRecursively();
				for (const QString& installedCore : {core, SwitchCore::profileDir() + "data/eglantine/"})
				{
					QDir(installedCore + "nand/system/Contents/registered").removeRecursively();
					QFile::remove(installedCore + "firmware-version.txt");
				}
			}
#ifndef _WIN32
			{
				const QByteArray old_core = qgetenv("USCL_SWITCH_CORE");
				const bool core_set = qEnvironmentVariableIsSet("USCL_SWITCH_CORE");
				const QString program = write("fake-core", "#!/bin/sh\nprintf '%s\\n' \"$@\"\nprintf 'DATA=%s\\n' \"$XDG_DATA_HOME\"\nprintf 'ONLINE=%s\\n' \"$USCL_ONLINE\"\nprintf 'THEME=%s\\n' \"$USCL_THEME\"\nprintf 'STYLE=%s\\n' \"$USCL_LOADING_STYLE\"\nprintf 'COLORS=%s\\n' \"$USCL_LOADING_COLORS\"\n");
				QFile::setPermissions(program, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
				qputenv("USCL_SWITCH_CORE", program.toUtf8());
				const QString test_keys = write("test-prod.keys", "header_key = " + QByteArray(64, '0') + '\n');
				check(SwitchCore::importKeys(test_keys).isEmpty() && SwitchCore::prerequisiteError(kart).isEmpty(),
				      "Switch games can reach the core without a blanket firmware requirement");
				// a game whose ticket asks for master key 0c (rights ID ends in key generation 0d), keys up to 0b
				QByteArray future(16 + 24 + 40, '\0');
				future.replace(0, 4, "PFS0");
				put32(future, 4, 1);
				put32(future, 8, 40);
				future.replace(16 + 24, 36, "0100a00000000000000000000000000d.tik");
				const QString needs = write("Future Game [0100A00000000000].nsp", future);
				QByteArray keys;
				for (int i = 0; i <= 0x0b; i++)
					keys += QString("master_key_%1 = %2\n").arg(i, 2, 16, QChar('0')).arg(QString(32, '1')).toLatin1();
				check(SwitchCore::importKeys(write("old-prod.keys", keys)).isEmpty() && SwitchCore::prerequisiteError(needs).contains("master key 0c") &&
				      SwitchCore::prerequisiteError(needs).contains("13.0.0"), "Switch says which newer keys a game needs");
				keys += "master_key_0c = " + QByteArray(32, '2') + '\n';
				QFile version(SwitchCore::dataDir() + "firmware-version.txt");
				check(version.open(QIODevice::WriteOnly) && version.write("12.1.0") == 6, "Switch firmware version fixture");
				version.close();
				check(SwitchCore::importKeys(write("new-prod.keys", keys)).isEmpty() &&
				      SwitchCore::prerequisiteError(needs).contains("13.0.0 or newer; 12.1.0 is installed"), "Switch says which firmware a game needs");
				QFile::remove(version.fileName());
				QFile::remove(needs);
				check(SwitchCore::importKeys(test_keys).isEmpty(), "Switch test keys restored");
				{
					// Installed update / DLC (SwitchContent): AES against FIPS-197 and IEEE 1619, then a NAND of content
					// metas encrypted the way Nintendo's are. (Checked once against Python's cryptography as well.)
					const auto hex = [](const char* s) { return QByteArray::fromHex(s); };
					check(SwitchContent::aesEncrypt(hex("000102030405060708090a0b0c0d0e0f"), hex("00112233445566778899aabbccddeeff")) == hex("69c4e0d86a7b0430d8cdb78070b4c55a") &&
					      SwitchContent::aesDecrypt(hex("000102030405060708090a0b0c0d0e0f"), hex("69c4e0d86a7b0430d8cdb78070b4c55a")) == hex("00112233445566778899aabbccddeeff"), "AES-128 known answer");
					check(SwitchContent::xts(QByteArray(32, '\0'), QByteArray(0x200, '\0'), 0, true).left(32) ==
					      hex("917cf69ebd68b2ec9b9fe9a3eadda692cd43d2f59598ed858c02c2652fbf922e"), "AES-XTS known answer");
					QByteArray header_key(32, '\0'), kaek(16, '\0'), section_key(16, '\0');
					for (int i = 0; i < 32; i++) header_key[i] = char(i);
					for (int i = 0; i < 16; i++) kaek[i] = char(0x40 + i), section_key[i] = char(0x80 + i);
					// a content meta for program `id`: header (XTS), key area, section 0 = PFS0 holding the CNMT (CTR)
					const auto meta = [&](quint64 id, quint32 version, quint8 type) {
						QByteArray h(0xC00, '\0');
						h.replace(0x200, 4, "NCA3");
						h[0x205] = 1, h[0x220] = 6;   // key generation 6: key_area_key_application_05
						qToLittleEndian(id, h.data() + 0x210);
						QByteArray cnmt(0x40, '\0');
						qToLittleEndian(id, cnmt.data());
						qToLittleEndian(version, cnmt.data() + 8);
						cnmt[0xC] = char(type);
						const QByteArray names("x.cnmt\0\0\0\0\0\0\0\0\0\0", 16);
						QByteArray pfs("PFS0", 4);
						pfs += QByteArray(12 + 0x18, '\0');
						put32(pfs, 4, 1), put32(pfs, 8, names.size()), put32(pfs, 0x18, cnmt.size());
						pfs += names + cnmt;
						qToLittleEndian(quint32(6), h.data() + 0x240);   // section at 0xC00, the PFS0 0x203 into it
						qToLittleEndian(quint64(0x203), h.data() + 0x440);
						qToLittleEndian(quint64(pfs.size()), h.data() + 0x448);
						h[0x402] = 1, h[0x403] = 2, h[0x404] = 3;
						for (int i = 0; i < 8; i++) h[0x540 + i] = char(i + 1);
						QByteArray area(0x10, '\0');
						for (int i = 0; i < 3; i++) area += SwitchContent::aesEncrypt(kaek, i == 1 ? section_key : QByteArray(16, '\0'));
						h.replace(0x300, 0x40, area);
						QByteArray counter(16, '\0');
						for (int i = 0; i < 8; i++) counter[i] = char(8 - i);
						qToBigEndian(quint64(0xC00 >> 4), counter.data() + 8);
						return SwitchContent::xts(header_key, h, 0, true) + SwitchContent::ctr(section_key, counter, QByteArray(0x203, '\0') + pfs);
					};
					const QString nand = SwitchCore::dataDir() + "nand/user/Contents/registered/";
					const auto install = [&](const QString& name, const QByteArray& nca) {
						QDir().mkpath(QFileInfo(nand + name).absolutePath());
						QFile f(nand + name);
						check(f.open(QIODevice::WriteOnly) && f.write(nca) == nca.size(), "Switch NAND fixture");
					};
					QFile prod(SwitchCore::dataDir() + "keys/prod.keys");
					const QByteArray real_keys = prod.open(QIODevice::ReadOnly) ? prod.readAll() : QByteArray();
					prod.close();
					check(prod.open(QIODevice::WriteOnly) && prod.write("header_key = " + header_key.toHex() + "\nkey_area_key_application_05 = " + kaek.toHex() + "\n") > 0, "Switch content keys fixture");
					prod.close();
					const QString kart_id = "0100152000022000";
					check(SwitchContent::installed(kart, kart_id).build == "base" && GameModel::switchContent(kart, kart_id) == "No update", "Switch game without update or DLC");
					install("00000012/aa11.cnmt.nca", meta(0x0100152000022800, 524288, 0x81));
					install("000000ab/bb22.cnmt.nca/00", meta(0x0100152000023001, 0, 0x82));   // a split NCA folder
					install("cc33.cnmt.nca", meta(0x0100AAAA00000800, 65536, 0x81));           // another game's update
					const QString with_update = GameModel::gameHash(kart, kart_id);
					check(with_update.size() == 16 && GameModel::switchContent(kart, kart_id) == "Update v524288, 1 DLC", "Switch installed update and DLC");
					QFile::remove(nand + "00000012/aa11.cnmt.nca");   // Eden replaces the old update's content with the new one
					install("00000034/ee55.cnmt.nca", meta(0x0100152000022800, 589824, 0x81));
					check(GameModel::gameHash(kart, kart_id) != with_update && SwitchContent::installed(kart, kart_id).update == "v589824", "Switch build changes with the update");
					check(SwitchContent::installed("/d/Sports/updates/65536/exefs/main", {}).update == "v65536" &&
					      SwitchContent::installed("/d/Sports/base/exefs/main", {}).build == "base", "Switch dump runs its update folder");
					QDir(SwitchCore::dataDir() + "nand/user").removeRecursively();
					check(prod.open(QIODevice::WriteOnly) && prod.write(real_keys) == real_keys.size(), "Switch test keys restored after content test");
					prod.close();
				}
#ifdef __linux__
				const QByteArray old_driver = qgetenv("SDL_VIDEODRIVER"), old_display = qgetenv("DISPLAY");
				const bool driver_set = qEnvironmentVariableIsSet("SDL_VIDEODRIVER"), display_set = qEnvironmentVariableIsSet("DISPLAY");
				qputenv("SDL_VIDEODRIVER", "wayland");
				qputenv("DISPLAY", ":99");
				check(SwitchCore::environment().value("SDL_VIDEODRIVER") == "x11", "Switch SDL2 uses XWayland when desktop forces Wayland");
				qputenv("SDL_VIDEODRIVER", "dummy");
				check(SwitchCore::environment().value("SDL_VIDEODRIVER") == "dummy", "Switch preserves other video driver choices");
				const QByteArray old_platform = qgetenv("QT_QPA_PLATFORM");
				const bool platform_set = qEnvironmentVariableIsSet("QT_QPA_PLATFORM");
				qputenv("USCL_SWITCH_FRONTEND", "qt");
				qputenv("QT_QPA_PLATFORM", "wayland;xcb");
				check(SwitchCore::environment().value("QT_QPA_PLATFORM") == "xcb", "Switch Qt core uses XWayland on a forced Wayland desktop");
				qputenv("QT_QPA_PLATFORM", "offscreen");
				check(SwitchCore::environment().value("QT_QPA_PLATFORM") == "offscreen", "Switch preserves explicit non-Wayland Qt platforms");
				qputenv("USCL_SWITCH_FRONTEND", "sdl");
				if (platform_set) qputenv("QT_QPA_PLATFORM", old_platform); else qunsetenv("QT_QPA_PLATFORM");
				if (driver_set) qputenv("SDL_VIDEODRIVER", old_driver); else qunsetenv("SDL_VIDEODRIVER");
				if (display_set) qputenv("DISPLAY", old_display); else qunsetenv("DISPLAY");
#endif
				Launcher launcher;
				launcher.setLoadingTheme("ps4", "#073ba0", "white", "#79c9ff", "#114ca9");
				QEventLoop loop;
				bool completed = false;
				QObject::connect(&launcher, &Launcher::finished, &loop, [&](bool crashed, const QString& details) {
					completed = !crashed && details.contains("--multiplayer") && details.contains("TestPlayer@127.0.0.1:24872") &&
					            details.contains("DATA=" + SwitchCore::profileDir() + "data") && details.contains("ONLINE=") &&
					            details.contains("THEME=ps4") && details.contains("STYLE=QWidget") && details.contains("COLORS=073ba0,ffffff,79c9ff") && !details.contains("--no-gui");
					loop.quit();
				});
				QTimer::singleShot(3000, &loop, &QEventLoop::quit);
				const auto oldTools = qgetenv("USCL_TOOLS"), oldRoom = qgetenv("USCL_SWITCH_ROOM");
				const bool hadTools = qEnvironmentVariableIsSet("USCL_TOOLS"), hadRoom = qEnvironmentVariableIsSet("USCL_SWITCH_ROOM");
				const QString bridge = write("uscl-bridge", "#!/bin/sh\nprintf 'uscl-bridge: session 12345 code ' \nsleep 0.05\nprintf 'ABC234 (hosting); give friends this number or code\\n'\nread line\n");
				const QString room = write("fake-room", "#!/bin/sh\nread line\n");
				for (const QString& executable : {bridge, room}) QFile::setPermissions(executable, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
				qputenv("USCL_TOOLS", QFileInfo(bridge).absolutePath().toUtf8());
				qputenv("USCL_SWITCH_ROOM", room.toUtf8());
				Online session;
				bool sessionLaunched = false;
				QObject::connect(&session, &Online::stateChanged, &loop, [&] {
					if (session.state() == "hosting" && !sessionLaunched)
						sessionLaunched = launcher.startGame(homebrew, "Homebrew", true, "Switch", "TestPlayer");
				});
				session.host("0100152000022000", "Mario Kart fixture", homebrew, "Switch");
				loop.exec();
				check(sessionLaunched && session.state() == "hosting", "current bridge output transitions to hosting and launches the emulator");
				session.stop();
				if (hadTools) qputenv("USCL_TOOLS", oldTools); else qunsetenv("USCL_TOOLS");
				if (hadRoom) qputenv("USCL_SWITCH_ROOM", oldRoom); else qunsetenv("USCL_SWITCH_ROOM");
				check(completed && !launcher.running() && !launcher.starting(), "Switch core receives profile, room arguments and loading palette; process state resets");
				// Other native launch routes use the same child-only theme contract without real emulators.
				for (const auto& route : {qMakePair(QString("Wii"), QByteArray("USCL_DOLPHIN")),
				                          qMakePair(QString("3DS"), QByteArray("USCL_AZAHAR")),
				                          qMakePair(QString("DS"), QByteArray("USCL_MELONDS"))})
				{
					const bool set = qEnvironmentVariableIsSet(route.second.constData());
					const QByteArray old = qgetenv(route.second.constData());
					qputenv(route.second.constData(), program.toUtf8());
					Launcher other;
					other.setLoadingTheme("switch2", "#ebebeb", "#2a2d28", "#39e3d2", "white");
					QEventLoop route_loop;
					bool passed = false;
					QObject::connect(&other, &Launcher::finished, &route_loop, [&](bool crashed, const QString& details) {
						passed = !crashed && details.contains("THEME=switch2") && details.contains("COLORS=ebebeb,2a2d28,39e3d2");
						route_loop.quit();
					});
					QTimer::singleShot(3000, &route_loop, &QEventLoop::quit);
					check(other.startEmu("unused", "Fixture", route.first), "native fake core launch");
					route_loop.exec();
					check(passed && !other.gameShown(), "native routes receive theme and clear handoff on exit");
					if (set) qputenv(route.second.constData(), old); else qunsetenv(route.second.constData());
				}
				{
					const bool set = qEnvironmentVariableIsSet("USCL_RPCS3");
					const QByteArray old = qgetenv("USCL_RPCS3");
					qputenv("USCL_RPCS3", program.toUtf8());
					Launcher classic;
					classic.setLoadingTheme("ps4", "#073ba0", "white", "#79c9ff", "#114ca9");
					QEventLoop classic_loop;
					bool passed = false;
					QObject::connect(&classic, &Launcher::finished, &classic_loop, [&](bool crashed, const QString& details) {
						passed = !crashed && details.contains("THEME=\nSTYLE=\nCOLORS=");
						classic_loop.quit();
					});
					QTimer::singleShot(3000, &classic_loop, &QEventLoop::quit);
					check(classic.openClassic(), "classic fake core launch");
					classic_loop.exec();
					check(passed && !classic.running(), "classic tools do not receive native loading override");
					for (const bool stopping : {false, true})
					{
						const QString fake = write(stopping ? "fake-stop" : "fake-crash", stopping ? "#!/bin/sh\nread line\n" : "#!/bin/sh\nexit 9\n");
						QFile::setPermissions(fake, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
						qputenv("USCL_RPCS3", fake.toUtf8());
						Launcher lifecycle;
						QEventLoop life_loop;
						bool finished = false;
						QObject::connect(&lifecycle, &Launcher::finished, &life_loop, [&](bool crashed, const QString&) {
							finished = crashed != stopping;
							life_loop.quit();
						});
						QTimer::singleShot(3000, &life_loop, &QEventLoop::quit);
						check(lifecycle.startGame("unused", "Fixture", false), "lifecycle fake core starts");
						if (stopping) QTimer::singleShot(50, &lifecycle, &Launcher::stop);
						life_loop.exec();
						check(finished && !lifecycle.running() && !lifecycle.gameShown(), "crash/stop clears themed handoff with correct exit status");
					}
					if (set) qputenv("USCL_RPCS3", old); else qunsetenv("USCL_RPCS3");
				}
				if (core_set) qputenv("USCL_SWITCH_CORE", old_core); else qunsetenv("USCL_SWITCH_CORE");
			}
#endif
			GameModel model;
			const int baseline = model.rowCount();
			int added = -1, known = -1;
			bool scan_error = false;
			const auto scan = [&] {
				QEventLoop loop;
				QTimer timeout;
				timeout.setSingleShot(true);
				auto done = QObject::connect(&model, &GameModel::scanFinished, &loop, [&](int a, int k, int) { added = a; known = k; loop.quit(); });
				auto error = QObject::connect(&model, &GameModel::scanFailed, &loop, [&](const QString&) { scan_error = true; });
				QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
				timeout.start(5000);
				model.scanFolder(root.filePath("games"));
				loop.exec();
				check(!model.scanning(), "Switch asynchronous scan completes");
				QObject::disconnect(done);
				QObject::disconnect(error);
			};
			scan();
			check(added == 2 && known == 0 && model.rowCount() == baseline + 2 && !scan_error, "Switch scan persists and reloads");
			scan();
			check(added == 0 && known == 2, "Switch repeat scan deduplicates");
			model.remove(SwitchCore::describe(homebrew).value("serial").toString());
			check(model.rowCount() == baseline + 1 && QFileInfo::exists(homebrew), "Switch removal preserves game files");
			const QString library = SwitchCore::profileDir() + "games.json";
			QFile::remove(library);
			QDir().mkpath(library);
			scan();
			check(added == 0 && scan_error, "Switch persistence failure reports no games added");
			if (was_set) qputenv("XDG_CONFIG_HOME", previous); else qunsetenv("XDG_CONFIG_HOME");
			if (profile_set) qputenv("USCL_SWITCH_PROFILE", old_profile); else qunsetenv("USCL_SWITCH_PROFILE");
			if (frontend_set) qputenv("USCL_SWITCH_FRONTEND", old_frontend); else qunsetenv("USCL_SWITCH_FRONTEND");
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
	QGuiApplication::setWindowIcon(QIcon(":/qt/qml/Uscl/xmb/avatars/uscl.png"));   // the USCL orb (tools/xmb_assets.py icons)
	QGuiApplication::setDesktopFileName("uscl");   // Wayland docks take the icon from uscl.desktop

	if (app.arguments().contains("--self-test"))
		return selfTest();
	if (const int option = app.arguments().indexOf("--scan-games"); option >= 0)
	{
		if (option + 1 >= app.arguments().size() || !QFileInfo(app.arguments()[option + 1]).isDir())
		{
			std::fprintf(stderr, "usage: uscl --scan-games <folder>\n");
			return 2;
		}
		GameModel model;
		QEventLoop loop;
		QString problem;
		int added = 0, known = 0, encrypted = 0, archives = 0;
		QString switch_installed, switch_error;
		QStringList switch_too_new;
		QObject::connect(&model, &GameModel::scanFinished, &loop, [&](int a, int k, int e, int z) { added = a; known = k; encrypted = e; archives = z; });
		// the scan ends with the Switch keys / system software search; done once that reports
		QObject::connect(&model, &GameModel::switchFilesDone, &loop, [&](const QString& did, const QStringList& tooNew, const QString& error) {
			switch_installed = did, switch_too_new = tooNew, switch_error = error;
			loop.quit();
		});
		QObject::connect(&model, &GameModel::scanFailed, &loop, [&](const QString& error) { problem = error; });
		model.scanFolder(app.arguments()[option + 1]);
		loop.exec();
		QJsonArray games;
		for (int row = 0; row < model.rowCount(); ++row)
			games << QJsonObject::fromVariantMap(model.get(row));
		const QByteArray result = QJsonDocument(QJsonObject{{"added", added}, {"known", known}, {"encrypted", encrypted}, {"archives", archives}, {"error", problem}, {"games", games},
		    {"switchInstalled", switch_installed}, {"switchTooNew", QJsonArray::fromStringList(switch_too_new)}, {"switchError", switch_error}}).toJson();
		std::fwrite(result.constData(), 1, result.size(), stdout);
		return problem.isEmpty() ? 0 : 1;
	}

	// uscl --list-games: load the real library (games.yml + emu_games.json) and print it grouped by console.
	// A headless way to check what the home screen's console columns will show.
	if (app.arguments().contains("--list-games"))
	{
		GameModel games;
		games.reload();
		QMap<QString, QStringList> byConsole;
		for (int i = 0; i < games.rowCount(); i++)
		{
			const QVariantMap g = games.get(i);
			byConsole[g.value("console").toString()] << g.value("title").toString();
		}
		for (const QString& c : {QStringLiteral("PS3"), QStringLiteral("PS4"), QStringLiteral("Wii"),
		                         QStringLiteral("WiiU"), QStringLiteral("3DS"), QStringLiteral("Switch"), QStringLiteral("Xbox360")})
			if (byConsole.contains(c))
			{
				std::printf("== %s (%d) ==\n", qPrintable(c), int(byConsole[c].size()));
				for (const QString& t : byConsole[c])
					std::printf("   %s\n", qPrintable(t));
			}
		return 0;
	}

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

	const bool smoke_test = app.arguments().contains("--qml-smoke-test");
	std::unique_ptr<QTemporaryDir> smoke_dir;
	QString smoke_theme = "ps3";
	if (smoke_test)
	{
		// QML smoke tests must never alter a player's preferences or library.
		smoke_dir = std::make_unique<QTemporaryDir>();
		if (!smoke_dir->isValid()) return 1;
		qputenv("XDG_CONFIG_HOME", smoke_dir->path().toUtf8());
		QSettings::setDefaultFormat(QSettings::IniFormat);
		QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, smoke_dir->path());
		const int theme_arg = app.arguments().indexOf("--theme");
		if (theme_arg >= 0 && theme_arg + 1 < app.arguments().size()) smoke_theme = app.arguments()[theme_arg + 1];
		QSettings settings;
		settings.setValue("theme", smoke_theme);
		settings.setValue("setupDone", true);
        settings.setValue("librarySort", "titleDescending");
		settings.setValue("bootScreen", false);
		settings.setValue("autoUpdate", false);
		settings.setValue("sounds", false);
		settings.sync();
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

	GameUpdates gameUpdates;
	Dlc dlc;
	DiscordParty party;
	QQmlApplicationEngine engine;
	engine.rootContext()->setContextProperty("games", &games);
	engine.rootContext()->setContextProperty("gamepad", &gamepad);
	engine.rootContext()->setContextProperty("launcher", &launcher);
	engine.rootContext()->setContextProperty("system", &system);
	engine.rootContext()->setContextProperty("online", &online);
	engine.rootContext()->setContextProperty("updater", &updater);
	engine.rootContext()->setContextProperty("gameUpdates", &gameUpdates);
	engine.rootContext()->setContextProperty("dlc", &dlc);
	engine.rootContext()->setContextProperty("party", &party);
	engine.rootContext()->setContextProperty("windowed", app.arguments().contains("--windowed"));
	engine.rootContext()->setContextProperty("smokeTest", smoke_test);
	QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed, &app, [] { QCoreApplication::exit(1); }, Qt::QueuedConnection);
	engine.loadFromModule("Uscl", "Main");
	if (smoke_test && !engine.rootObjects().isEmpty())
	{
        const QString soundTheme = smoke_theme == "ps3" ? "xmb" : smoke_theme;
        for (const QString& cue : {"cursor", "category", "select", "back", "cancel", "error", "notify", "startup"})
        {
            const QString resource = ":/qt/qml/Uscl/sounds/" + soundTheme + "/" + cue + ".wav";
            if (!QFile::exists(resource) || system.soundUrl(cue, smoke_theme) != "qrc" + resource)
            {
                std::fprintf(stderr, "qml-smoke-test: missing or incorrect themed sound\n");
                return 1;
            }
        }
        // Isolated smoke profile: test global and per-theme override priority without player files.
        const QString sounds = GameModel::rpcs3ConfigDir() + "sounds/";
        QDir().mkpath(sounds + smoke_theme);
        const QString global = sounds + "cursor.wav", themed = sounds + smoke_theme + "/cursor.wav";
        QFile globalFile(global), themeFile(themed);
        if (!globalFile.open(QIODevice::WriteOnly)) return 1;
        globalFile.close();
        if (system.soundUrl("cursor", smoke_theme) != QUrl::fromLocalFile(global).toString()) return 1;
        if (!themeFile.open(QIODevice::WriteOnly)) return 1;
        themeFile.close();
        if (system.soundUrl("cursor", smoke_theme) != QUrl::fromLocalFile(themed).toString()) return 1;
        QFile::remove(global); QFile::remove(themed);
        if (system.soundUrl("cursor", "unknown") != "qrc:/qt/qml/Uscl/sounds/xmb/cursor.wav" ||
            !system.soundUrl("../cursor", smoke_theme).isEmpty()) return 1;
		const QString active = engine.rootObjects().first()->property("activeTheme").toString();
		if (active != smoke_theme)
		{
			std::fprintf(stderr, "qml-smoke-test: requested %s, loaded %s\n", qPrintable(smoke_theme), qPrintable(active));
			return 1;
		}
		auto* root = engine.rootObjects().first();
		auto* stage = root->findChild<QQuickItem*>("consoleStage");
		if (!stage) return 1;
		for (const QSize size : {QSize(1280, 720), QSize(800, 600), QSize(2560, 1080)})
		{
			root->setProperty("width", size.width());
			root->setProperty("height", size.height());
			QCoreApplication::processEvents();
			if (stage->x() < -0.1 || stage->y() < -0.1 || stage->width() * stage->scale() > size.width() + 0.1 || stage->height() * stage->scale() > size.height() + 0.1)
			{
				std::fprintf(stderr, "qml-smoke-test: clipped stage at %dx%d (root %dx%d, stage %.1f,%.1f %.1fx%.1f scale %.4f)\n", size.width(), size.height(), root->property("width").toInt(), root->property("height").toInt(), stage->x(), stage->y(), stage->width(), stage->height(), stage->scale());
				return 1;
			}
		}
		root->setProperty("width", 1280);
		root->setProperty("height", 720);
		std::fprintf(stderr, "qml-smoke-test: loaded %s\n", qPrintable(active));
		QTimer::singleShot(800, &app, [root, active, &app] {
			auto* navigation = root->findChild<QQuickItem*>("homeNavigation");
			auto* page = root->findChild<QQuickItem*>("settingsPage");
			auto* options = root->findChild<QQuickItem*>("themeOptions");
			auto* window = qobject_cast<QQuickWindow*>(root);
			const auto fail = [&app](const char* reason) {
				std::fprintf(stderr, "qml-smoke-test: keyboard flow failed: %s\n", reason);
				app.exit(1);
			};
			if (!navigation || !page || !options || !window) { fail("missing control"); return; }
            const auto soundBindingsOk = [root]() {
                const QString theme = root->property("activeTheme").toString();
                const QString folder = theme == "ps3" ? "xmb" : theme;
                const QStringList ids = {"sCursor", "sCategory", "sSelect", "sBack", "sError", "sNotify", "sStartup"};
                const QStringList cues = {"cursor", "category", "select", "back", "error", "notify", "startup"};
                for (int i = 0; i < ids.size(); ++i)
                {
                    auto* effect = root->findChild<QObject*>(ids[i]);
                    if (!effect || effect->property("source").toUrl().toString() !=
                        "qrc:/qt/qml/Uscl/sounds/" + folder + "/" + cues[i] + ".wav" ||
                        effect->property("volume").toDouble() != root->property("menuSoundGain").toDouble()) return false;
                }
                return true;
            };
            if (!soundBindingsOk()) { fail("initial sound palette or volume"); return; }
            auto* prefs = root->findChild<QObject*>("preferences");
            if (!prefs) { fail("missing sound preferences"); return; }
            prefs->setProperty("sounds", true);
            for (const int volume : {0, 25, 50, 75, 100})
            {
                prefs->setProperty("menuVolume", volume);
                const double expected = (volume / 100.0) * (volume / 100.0);
                if (root->property("menuSoundGain").toDouble() != expected || !soundBindingsOk())
                { fail("menu volume adjustment"); return; }
            }
            prefs->setProperty("sounds", false);
            if (root->property("menuSoundGain").toDouble() != 0 || !soundBindingsOk())
            { fail("menu mute"); return; }

			const auto key = [window](int code) {
				QKeyEvent press(QEvent::KeyPress, code, Qt::NoModifier);
				QKeyEvent release(QEvent::KeyRelease, code, Qt::NoModifier);
				QCoreApplication::sendEvent(window, &press);
				QCoreApplication::sendEvent(window, &release);
			};
            const auto openThemeSettings = [root, navigation, page, &key]() {
                if (root->property("activeTheme").toString() == "ps3") {
                    navigation->setProperty("cat", 1);
                    QMetaObject::invokeMethod(navigation, "setItem", Q_ARG(QVariant, QVariant(5)));
                } else {
                    QMetaObject::invokeMethod(root, "openConsoleSection", Q_ARG(QVariant, QVariant("settings")));
                    page->setProperty("current", 5);
                }
                key(Qt::Key_Return);
                return page->isVisible();
            };
            const auto returnHome = [page, &key]() {
                for (int i = 0; i < 4 && page->isVisible(); ++i) key(Qt::Key_Escape);
            };
            // Exercise the real dashboard -> root -> page connections, including empty libraries.
            auto* dashboard = root->findChild<QQuickItem*>("consoleDashboard");
            if (!dashboard) { fail("missing console dashboard"); return; }
            for (const QString section : {QStringLiteral("library"), QStringLiteral("settings"),
                                          QStringLiteral("controllers"), QStringLiteral("sound"), QStringLiteral("profile")})
            {
                if (!QMetaObject::invokeMethod(dashboard, "sectionRequested", Q_ARG(QString, section)) ||
                    !page->isVisible() || page->property("heading").toString().isEmpty())
                { fail("dashboard section routing"); return; }
                key(Qt::Key_Down);
                key(Qt::Key_Up);
                returnHome();
                if (page->isVisible()) { fail("dashboard section back navigation"); return; }
            }
            // The previous smoke flow used an empty library. Exercise loaded models and
            // nested-page restoration through Main's real routing with inert synthetic rows.
            if (active != "ps3")
            {
                QQmlExpression audit(qmlContext(root), root, QStringLiteral(R"JS(
                    (function() {
                        var original = cats, originalSort = page.sortMode;
                        try {
                            if (originalSort !== "titleDescending") throw new Error("saved library sort not loaded");
                            page.setSort("library");
                            var copy = cats.slice(), rows = [];
                            for (var i=0; i<53; ++i) rows.push({gameId:"UI-AUDIT-"+i, title:"Audit game "+i, console:i%2 ? "PS4":"PS3", disabled:true});
                            copy[gamesCat] = {id:"games",name:"Games",icon:original[gamesCat].icon,items:rows}; cats = copy;
                            openConsoleSection("library");
                            if (!page.modernSoftware || page.displayRows.length !== 53) throw new Error("loaded library model");
                            page.current = page.gridColumns === 1 ? 51 : 49; page.input("down");
                            if (page.current !== 52) throw new Error("short final library row");
                            page.selectFilter(2); page.current = 12;
                            var selected = page.cur.gameId;
                            openConsoleSection("sound"); popPage();
                            if (page.filters[page.filterIndex] !== "PS4" || page.cur.gameId !== selected) throw new Error("library filter/selection restoration");
                            page.sortRequested();
                            if (!options.open || options.options.length !== 4) throw new Error("sort menu routing");
                            options.current = 1; options.input("accept");
                            if (page.sortMode !== "title" || prefs.librarySort !== "title" || page.cur.gameId !== selected) throw new Error("sort choice persistence/selection");
                            openConsoleSection("sound"); popPage();
                            if (page.sortMode !== "title" || page.cur.gameId !== selected) throw new Error("sorted nested return");
                            return true;
                        } finally { closePages(); cats = original; page.setSort(originalSort); }
                    })()
                )JS"));
                if (!audit.evaluate().toBool() || audit.hasError())
                {
                    if (audit.hasError()) std::fprintf(stderr, "qml-smoke-test: %s\n", qPrintable(audit.error().description()));
                    fail("loaded library navigation/restoration"); return;
                }
            }
            // Open every settings section through actual key routing. These actions
            // display menus only; no setting, install, emulator or network action is selected.
            for (int sectionIndex = 0; sectionIndex < 10; ++sectionIndex)
            {
                QMetaObject::invokeMethod(root, "openConsoleSection", Q_ARG(QVariant, QVariant("settings")));
                page->setProperty("current", sectionIndex);
                key(Qt::Key_Return);
                if (!page->isVisible() || page->property("heading").toString().isEmpty())
                { fail("settings section did not open"); return; }
                key(Qt::Key_Escape);
                if (!page->isVisible() || page->property("current").toInt() != sectionIndex)
                { fail("settings section selection restoration"); return; }
                returnHome();
            }
            if (active != "ps3")
            {
                key(Qt::Key_Home);
                if (!dashboard->property("guideOpen").toBool()) { fail("home button quick menu"); return; }
                key(Qt::Key_Escape);
                if (dashboard->property("guideOpen").toBool()) { fail("quick menu back navigation"); return; }
            }
            QMetaObject::invokeMethod(root, "togglePin", Q_ARG(QVariant, QVariant("USCL-UI-TEST-PIN")));
            if (!prefs->property("pinnedGames").toString().contains("USCL-UI-TEST-PIN"))
            { fail("pin persistence"); return; }
            QMetaObject::invokeMethod(root, "togglePin", Q_ARG(QVariant, QVariant("USCL-UI-TEST-PIN")));
            if (prefs->property("pinnedGames").toString() != "[]") { fail("unpin persistence"); return; }
            if (!openThemeSettings()) { fail("theme settings selection was not applied"); return; }
			if (!page->isVisible()) { fail("theme page did not open"); return; }
			key(Qt::Key_Return);
			if (!options->property("open").toBool()) { fail("theme choices did not open"); return; }
			const int original_option = options->property("current").toInt();
			const int direction = original_option == 0 ? Qt::Key_Down : Qt::Key_Up;
			key(direction);
			key(Qt::Key_Return); // Switch looks while the theme page is open.
			if (options->property("open").toBool()) { fail("theme choice did not close"); return; }
			if (root->property("activeTheme").toString() == active) { fail("theme did not change"); return; }
            if (!soundBindingsOk()) { fail("sound palette did not follow theme switch"); return; }
            returnHome();
            if (page->isVisible()) { fail("back did not return home"); return; }
            if (!openThemeSettings()) { fail("theme settings could not reopen after changing themes"); return; }
			key(Qt::Key_Return);
			if (!options->property("open").toBool()) { fail("theme choices did not reopen"); return; }
			key(direction == Qt::Key_Up ? Qt::Key_Down : Qt::Key_Up);
			key(Qt::Key_Return);
			if (root->property("activeTheme").toString() != active) { fail("original theme was not restored"); return; }
            returnHome();
            if (!soundBindingsOk()) { fail("restored sound palette"); return; }
            // The changelog is a main Settings entry in every theme and must work offline.
            QMetaObject::invokeMethod(root, "openConsoleSection", Q_ARG(QVariant, QVariant("changelog")));
            auto* log = root->findChild<QQuickItem*>("mainDialog");
            if (!log || !log->property("open").toBool() || !log->property("markdown").toBool() ||
                !log->property("text").toString().contains("# USCL Changelog") ||
                !log->property("text").toString().contains("## "))
            { fail("offline changelog did not open"); return; }
            QTimer::singleShot(100, &app, [log, window, &app] {
                const auto sendKey = [window](int code) {
                    QKeyEvent press(QEvent::KeyPress, code, Qt::NoModifier);
                    QKeyEvent release(QEvent::KeyRelease, code, Qt::NoModifier);
                    QCoreApplication::sendEvent(window, &press);
                    QCoreApplication::sendEvent(window, &release);
                };
                sendKey(Qt::Key_Down);
                if (!log->property("scrollable").toBool() || log->property("scrollPosition").toDouble() <= 0)
                { std::fprintf(stderr, "qml-smoke-test: changelog did not scroll\n"); app.exit(1); return; }
                sendKey(Qt::Key_Escape);
                if (log->property("open").toBool())
                { std::fprintf(stderr, "qml-smoke-test: changelog did not close\n"); app.exit(1); return; }
                std::fprintf(stderr, "qml-smoke-test: offline changelog passed\n");
                app.exit(0);
            });
			std::fprintf(stderr, "qml-smoke-test: keyboard flow passed\n");
		});
		// Finish only after the final asynchronous assertion, not an elapsed-time guess.
        QTimer::singleShot(10000, &app, [&app] {
            std::fprintf(stderr, "qml-smoke-test: timed out before completion\n");
            app.exit(1);
        });
	}
	return app.exec();
}
