#include "padconfig.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QSettings>
#include <SDL3/SDL.h>

#include <iterator>

namespace
{
	// The emulators are flatpaks (Launcher::startEmu)
	QString flatpakConfig(const QString& app) { return QDir::homePath() + "/.var/app/" + app + "/config/"; }

	QString readText(const QString& path)
	{
		QFile f(path);
		return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString();
	}

	bool writeText(const QString& path, const QString& text)
	{
		QDir().mkpath(QFileInfo(path).absolutePath());
		QSaveFile f(path);
		const QByteArray bytes = text.toUtf8();
		return f.open(QIODevice::WriteOnly) && f.write(bytes) == bytes.size() && f.commit();
	}

	// Set "key = value" in an INI section, adding the key or section when missing
	QString setIniValue(const QString& text, const QString& section, const QString& key, const QString& value)
	{
		QStringList lines = text.split('\n');
		int in = -1, insert = -1;
		for (int i = 0; i < lines.size(); i++)
		{
			const QString t = lines[i].trimmed();
			if (t.startsWith('['))
			{
				if (in >= 0)
					break;
				if (t == '[' + section + ']')
					in = insert = i + 1;
				continue;
			}
			if (in < 0)
				continue;
			if (t.section('=', 0, 0).trimmed() == key)
			{
				lines[i] = key + " = " + value;
				return lines.join('\n');
			}
			if (!t.isEmpty())
				insert = i + 1;
		}
		if (insert < 0)
		{
			while (!lines.isEmpty() && lines.last().isEmpty())
				lines.removeLast();
			lines << '[' + section + ']';
			insert = lines.size();
		}
		lines.insert(insert, key + " = " + value);
		return lines.join('\n');
	}

	const char* const wiimote_common = R"(Extension = Nunchuk
Buttons/A = `Button S`
Buttons/B = `Trigger R`
Buttons/1 = `Button W`
Buttons/2 = `Button N`
Buttons/- = Back
Buttons/+ = Start
Buttons/Home = `Button E`
D-Pad/Up = `Pad N`
D-Pad/Down = `Pad S`
D-Pad/Left = `Pad W`
D-Pad/Right = `Pad E`
Nunchuk/Buttons/C = `Shoulder L`
Nunchuk/Buttons/Z = `Trigger L`
Nunchuk/Stick/Up = `Left Y-`
Nunchuk/Stick/Down = `Left Y+`
Nunchuk/Stick/Left = `Left X-`
Nunchuk/Stick/Right = `Left X+`
)";
	// The pad's own motion sensors are the Wii Remote's: swing, tilt and point by moving it (R3 recenters the pointer)
	const char* const wiimote_motion = R"(IMUAccelerometer/Up = `Accel Up`
IMUAccelerometer/Down = `Accel Down`
IMUAccelerometer/Left = `Accel Left`
IMUAccelerometer/Right = `Accel Right`
IMUAccelerometer/Forward = `Accel Forward`
IMUAccelerometer/Backward = `Accel Backward`
IMUGyroscope/Pitch Up = `Gyro Pitch Up`
IMUGyroscope/Pitch Down = `Gyro Pitch Down`
IMUGyroscope/Roll Left = `Gyro Roll Left`
IMUGyroscope/Roll Right = `Gyro Roll Right`
IMUGyroscope/Yaw Left = `Gyro Yaw Left`
IMUGyroscope/Yaw Right = `Gyro Yaw Right`
IMUIR/Enabled = True
IMUIR/Recenter = `Thumb R`
)";
	// No motion sensors: point with the right stick, shake with R1
	const char* const wiimote_stick = R"(IR/Up = `Right Y-`
IR/Down = `Right Y+`
IR/Left = `Right X-`
IR/Right = `Right X+`
Shake/X = `Shoulder R`
Shake/Y = `Shoulder R`
Shake/Z = `Shoulder R`
)";
	const char* const gcpad = R"(Buttons/A = `Button S`
Buttons/B = `Button W`
Buttons/X = `Button E`
Buttons/Y = `Button N`
Buttons/Z = `Shoulder R`
Buttons/Start = Start
Main Stick/Up = `Left Y-`
Main Stick/Down = `Left Y+`
Main Stick/Left = `Left X-`
Main Stick/Right = `Left X+`
C-Stick/Up = `Right Y-`
C-Stick/Down = `Right Y+`
C-Stick/Left = `Right X-`
C-Stick/Right = `Right X+`
Triggers/L = `Trigger L`
Triggers/R = `Trigger R`
Triggers/L-Analog = `Trigger L`
Triggers/R-Analog = `Trigger R`
D-Pad/Up = `Pad N`
D-Pad/Down = `Pad S`
D-Pad/Left = `Pad W`
D-Pad/Right = `Pad E`
)";

	// Cemu (src/input/api/Controller.h Buttons2): kButtonN = SDL gamepad button N, then these
	enum : int { ZL_TRIGGER = 42, ZR_TRIGGER = 43, AXIS_XP = 38, AXIS_YP, ROT_XP, ROT_YP, AXIS_XN = 44, AXIS_YN, ROT_XN, ROT_YN };
	struct CemuEntry { int gamepad_id, pro_id, button; };
	// Cemu's own generic SDL default (by position: A = right face button) plus Home on the Guide button
	const CemuEntry cemu_map[] = {
		{1, 1, SDL_GAMEPAD_BUTTON_EAST}, {2, 2, SDL_GAMEPAD_BUTTON_SOUTH}, {3, 3, SDL_GAMEPAD_BUTTON_NORTH}, {4, 4, SDL_GAMEPAD_BUTTON_WEST},
		{5, 5, SDL_GAMEPAD_BUTTON_LEFT_SHOULDER}, {6, 6, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER}, {7, 7, ZL_TRIGGER}, {8, 8, ZR_TRIGGER},
		{9, 9, SDL_GAMEPAD_BUTTON_START}, {10, 10, SDL_GAMEPAD_BUTTON_BACK}, {27, 11, SDL_GAMEPAD_BUTTON_GUIDE},
		{11, 12, SDL_GAMEPAD_BUTTON_DPAD_UP}, {12, 13, SDL_GAMEPAD_BUTTON_DPAD_DOWN}, {13, 14, SDL_GAMEPAD_BUTTON_DPAD_LEFT}, {14, 15, SDL_GAMEPAD_BUTTON_DPAD_RIGHT},
		{15, 16, SDL_GAMEPAD_BUTTON_LEFT_STICK}, {16, 17, SDL_GAMEPAD_BUTTON_RIGHT_STICK},
		{17, 18, AXIS_YN}, {18, 19, AXIS_YP}, {19, 20, AXIS_XN}, {20, 21, AXIS_XP},
		{21, 22, ROT_YN}, {22, 23, ROT_YP}, {23, 24, ROT_XN}, {24, 25, ROT_XP},
	};

	// Azahar (SDL2 game controller numbers): 3DS a b x y up down left right l r start select debug gpio14 zl zr home power
	constexpr int SDL2_A = 0, SDL2_B = 1, SDL2_X = 2, SDL2_Y = 3;
	const int azahar_rest[] = {11, 12, 13, 14, 9, 10, 6, 4, -1, -1, -2, -3, 5, -1}; // -2/-3: ZL/ZR triggers
	const char* const azahar_buttons[] = {"button_a", "button_b", "button_x", "button_y", "button_up", "button_down",
		"button_left", "button_right", "button_l", "button_r", "button_start", "button_select", "button_debug",
		"button_gpio14", "button_zl", "button_zr", "button_home", "button_power"};
	// "maptype all": any connected controller drives the 3DS (one player per console)
	const QString azahar_any = "engine:sdl,api:controller,maptype:all,guid:0,port:0,";
}

QList<PadConfig::Pad> PadConfig::connected()
{
	QList<Pad> out;
	int n = 0;
	SDL_JoystickID* ids = SDL_GetGamepads(&n);
	for (int i = 0; i < n; i++)
	{
		Pad p;
		const char* name = SDL_GetGamepadNameForID(ids[i]);
		p.name = name ? QString::fromUtf8(name) : "Unknown";
		char guid[33];
		SDL_GUIDToString(SDL_GetGamepadGUIDForID(ids[i]), guid, sizeof(guid));
		p.guid = QString::fromLatin1(guid);
		for (const Pad& q : out)
		{
			p.nameIndex += q.name == p.name;
			p.guidIndex += q.guid == p.guid;
		}
		const SDL_GamepadType type = SDL_GetGamepadTypeForID(ids[i]);
		p.nintendo = type == SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_PRO || type == SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_LEFT
			|| type == SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_RIGHT || type == SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_PAIR;
		SDL_Gamepad* pad = SDL_GetGamepadFromID(ids[i]);
		p.motion = pad && SDL_GamepadHasSensor(pad, SDL_SENSOR_GYRO) && SDL_GamepadHasSensor(pad, SDL_SENSOR_ACCEL);
		out << p;
	}
	SDL_free(ids);
	return out;
}

QString PadConfig::dolphinIni(const QString& current, const QString& kind, const QList<Pad>& pads)
{
	// players with a pad get a fresh section; Wii Remotes without one are switched off
	auto managed = [&](const QString& header) {
		for (int i = 1; i <= 4; i++)
			if (header == QString("[%1%2]").arg(kind).arg(i))
				return i <= pads.size() || kind == "Wiimote";
		return false;
	};
	QStringList out;
	bool skip = false;
	for (const QString& line : current.split('\n'))
	{
		if (line.trimmed().startsWith('['))
			skip = managed(line.trimmed());
		if (!skip)
			out << line;
	}
	while (!out.isEmpty() && out.last().trimmed().isEmpty())
		out.removeLast();
	QString text = out.join('\n');
	if (!text.isEmpty())
		text += '\n';
	for (int i = 0; i < 4; i++)
	{
		const QString head = QString("[%1%2]\n").arg(kind).arg(i + 1);
		if (i >= pads.size())
		{
			if (kind == "Wiimote")
				text += head + "Source = 0\n";
			continue;
		}
		text += head + QString("Device = SDL/%1/%2\n").arg(pads[i].nameIndex).arg(pads[i].name);
		if (kind == "Wiimote")
			text += QString("Source = 1\n") + wiimote_common + (pads[i].motion ? wiimote_motion : wiimote_stick);
		else
			text += gcpad;
	}
	return text;
}

QString PadConfig::cemuProfile(const Pad& pad, bool gamepad)
{
	QString entries;
	for (const CemuEntry& e : cemu_map)
		entries += QString("\t\t\t<entry>\n\t\t\t\t<mapping>%1</mapping>\n\t\t\t\t<button>%2</button>\n\t\t\t</entry>\n")
			.arg(gamepad ? e.gamepad_id : e.pro_id).arg(e.button);
	QString name = pad.name;
	name.replace('&', "&amp;").replace('<', "&lt;").replace('>', "&gt;");
	return QString("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<emulated_controller>\n\t<type>%1</type>\n\t<controller>\n"
				   "\t\t<api>SDLController</api>\n\t\t<uuid>%2_%3</uuid>\n\t\t<display_name>%4</display_name>\n"
				   "\t\t<motion>%5</motion>\n\t\t<mappings>\n%6\t\t</mappings>\n\t</controller>\n</emulated_controller>\n")
		.arg(gamepad ? "Wii U GamePad" : "Wii U Pro Controller").arg(pad.guidIndex).arg(pad.guid).arg(name)
		.arg(pad.motion ? "true" : "false").arg(entries);
}

QString PadConfig::azaharButton(int button, bool nintendo)
{
	// by position: the 3DS A is the right face button (SDL2 calls it B on Xbox-style pads, A on Nintendo's)
	static const int xinput_face[] = {SDL2_B, SDL2_A, SDL2_Y, SDL2_X}, nintendo_face[] = {SDL2_A, SDL2_B, SDL2_X, SDL2_Y};
	const int b = button < 4 ? (nintendo ? nintendo_face : xinput_face)[button] : azahar_rest[button - 4];
	if (b == -1)
		return {};
	if (b < -1)
		return azahar_any + QString("axis:%1,direction:+,threshold:0.500000").arg(b == -2 ? 4 : 5);
	return azahar_any + QString("button:%1").arg(b);
}

bool PadConfig::writeDolphin(const QList<Pad>& pads, const QString& configDir)
{
	if (pads.isEmpty())
		return true; // keyboard players keep whatever is set
	const QString dir = configDir.endsWith('/') ? configDir : configDir + '/';
	QString core = readText(dir + "Dolphin.ini");
	for (int i = 0; i < pads.size() && i < 4; i++)
		core = setIniValue(core, "Core", QString("SIDevice%1").arg(i), "6"); // GameCube: standard controller
	return writeText(dir + "WiimoteNew.ini", dolphinIni(readText(dir + "WiimoteNew.ini"), "Wiimote", pads))
		&& writeText(dir + "GCPadNew.ini", dolphinIni(readText(dir + "GCPadNew.ini"), "GCPad", pads))
		&& writeText(dir + "Dolphin.ini", core);
}

bool PadConfig::writeAzahar(const QList<Pad>& pads, const QString& iniPath)
{
	if (pads.isEmpty())
		return true;
	QDir().mkpath(QFileInfo(iniPath).absolutePath());
	QSettings ini(iniPath, QSettings::IniFormat);
	const QString p = QString("Controls/profiles/%1/").arg(ini.value("Controls/profile", 0).toInt() + 1);
	auto set = [&](const QString& key, const QString& value) {
		ini.setValue(p + key, value);
		ini.setValue(p + key + "/default", false);
	};
	for (int i = 0; i < int(std::size(azahar_buttons)); i++)
		if (const QString param = azaharButton(i, pads[0].nintendo); !param.isEmpty())
			set(azahar_buttons[i], param);
	set("circle_pad", azahar_any + "axis_x:0,axis_y:1,deadzone:0.100000");
	set("c_stick", azahar_any + "axis_x:2,axis_y:3,deadzone:0.100000");
	set("input_maptype", "0"); // all controllers
	ini.sync();
	return ini.status() == QSettings::NoError;
}

bool PadConfig::writeCemu(const QList<Pad>& pads)
{
	const QString dir = flatpakConfig("info.cemu.Cemu") + "Cemu/controllerProfiles/";
	for (int i = 0; i < pads.size() && i < 8; i++)
		if (!writeText(dir + QString("controller%1.xml").arg(i), cemuProfile(pads[i], i == 0)))
			return false;
	return true;
}
