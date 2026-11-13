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
#include "switchcore.h"

#include <QFontDatabase>
#include <QGuiApplication>
#include <QScreen>
#include <QSettings>
#include <QQmlApplicationEngine>
#include <QQmlContext>
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
		{
			// other emulators: pads in order become players 1, 2; motion pads drive the Wii Remote's sensors
			PadConfig::Pad ds{"PS5 Controller", "050000004c050000e60c000000810000", 0, 0, false, true};
			PadConfig::Pad xb{"Xbox Series X Controller", "030000005e040000130b000011050000", 0, 0, false, false};
			const QString wii = PadConfig::dolphinIni("[Wiimote1]\nDevice = Old\nSource = 1\n[Wiimote2]\nSource = 1\n[BalanceBoard]\nSource = 0\n", "Wiimote", {ds, xb});
			check(wii.count("[Wiimote1]") == 1 && wii.contains("[Wiimote1]\nDevice = SDL/0/PS5 Controller\nSource = 1\n") &&
			      wii.contains("[Wiimote2]\nDevice = SDL/0/Xbox Series X Controller\nSource = 1\n") && wii.contains("[Wiimote3]\nSource = 0\n") &&
			      wii.contains("[BalanceBoard]\nSource = 0") && !wii.contains("Old") &&
			      wii.section("[Wiimote2]", 0, 0).contains("IMUGyroscope/Pitch Up = `Gyro Pitch Up`") &&
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
			check(SwitchCore::isSerial("0100152000022000") && !SwitchCore::isSerial("BLUS31426"), "console ID separation");
			check(Online::supports("0100152000022000"), "Switch games support automatic USCL session hosting");
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
#ifndef _WIN32
			{
				const QByteArray old_core = qgetenv("USCL_SWITCH_CORE");
				const bool core_set = qEnvironmentVariableIsSet("USCL_SWITCH_CORE");
				const QString program = write("fake-core", "#!/bin/sh\nprintf '%s\\n' \"$@\"\nprintf 'DATA=%s\\n' \"$XDG_DATA_HOME\"\nprintf 'ONLINE=%s\\n' \"$USCL_ONLINE\"\n");
				QFile::setPermissions(program, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
				qputenv("USCL_SWITCH_CORE", program.toUtf8());
				const QString test_keys = write("test-prod.keys", "header_key = " + QByteArray(64, '0') + '\n');
				check(SwitchCore::importKeys(test_keys).isEmpty() && SwitchCore::prerequisiteError(kart).isEmpty(),
				      "Switch games can reach the core without a blanket firmware requirement");
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
				QEventLoop loop;
				bool completed = false;
				QObject::connect(&launcher, &Launcher::finished, &loop, [&](bool crashed, const QString& details) {
					completed = !crashed && details.contains("--multiplayer") && details.contains("TestPlayer@127.0.0.1:24872") &&
					            details.contains("DATA=" + SwitchCore::profileDir() + "data") && details.contains("ONLINE=") && !details.contains("--no-gui");
					loop.quit();
				});
				QTimer::singleShot(3000, &loop, &QEventLoop::quit);
				check(launcher.startGame(homebrew, "Homebrew", true, "Switch", "TestPlayer"), "Switch core process starts");
				loop.exec();
				check(completed, "Switch core process receives isolated profile and room arguments");
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
		int added = 0, known = 0, encrypted = 0;
		QObject::connect(&model, &GameModel::scanFinished, &loop, [&](int a, int k, int e) { added = a; known = k; encrypted = e; loop.quit(); });
		QObject::connect(&model, &GameModel::scanFailed, &loop, [&](const QString& error) { problem = error; });
		model.scanFolder(app.arguments()[option + 1]);
		loop.exec();
		QJsonArray games;
		for (int row = 0; row < model.rowCount(); ++row)
			games << QJsonObject::fromVariantMap(model.get(row));
		const QByteArray result = QJsonDocument(QJsonObject{{"added", added}, {"known", known}, {"encrypted", encrypted}, {"error", problem}, {"games", games}}).toJson();
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
		                         QStringLiteral("WiiU"), QStringLiteral("3DS"), QStringLiteral("Xbox360")})
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
	Dlc dlc;
	engine.rootContext()->setContextProperty("dlc", &dlc);
	DiscordParty party;
	engine.rootContext()->setContextProperty("party", &party);
	engine.rootContext()->setContextProperty("windowed", app.arguments().contains("--windowed"));
	const bool smoke_test = app.arguments().contains("--qml-smoke-test");
	engine.rootContext()->setContextProperty("smokeTest", smoke_test);
	QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed, &app, [] { QCoreApplication::exit(1); }, Qt::QueuedConnection);
	engine.loadFromModule("Uscl", "Main");
	if (smoke_test)
		QTimer::singleShot(1500, &app, &QCoreApplication::quit);
	return app.exec();
}
