#pragma once

#include <QList>
#include <QString>

// Controller setup for the emulators that keep their own controller settings (Dolphin, Azahar, Cemu).
// Every connected game controller, USB or Bluetooth, becomes a player in connection order. Buttons go by
// position (the right face button is the console's A wherever the pad's labels put it), so any pad's layout
// works. Pads with gyro + accelerometer (DualSense, DualShock 4, Switch Pro...) drive the console's motion.
namespace PadConfig
{
	struct Pad
	{
		QString name;      // SDL game controller name, e.g. "PS5 Controller"
		QString guid;      // SDL GUID
		int nameIndex = 0; // earlier pads with the same name (Dolphin's "SDL/<n>/<name>")
		int guidIndex = 0; // earlier pads with the same GUID (Cemu's "<n>_<guid>")
		bool nintendo = false; // Switch Pro / Joy-Con: SDL2's label layout (Azahar)
		bool motion = false;
	};

	QList<Pad> connected(); // needs SDL's gamepad subsystem (Gamepad starts it)

	// Pure text builders (self-tested)
	QString dolphinIni(const QString& current, const QString& kind, const QList<Pad>& pads); // kind: Wiimote, GCPad
	QString cemuProfile(const Pad& pad, bool gamepad); // gamepad: Wii U GamePad, else Pro Controller
	QString azaharButton(int button, bool nintendo);   // 3DS button index (Settings::NativeButton) -> param

	bool writeDolphin(const QList<Pad>& pads, const QString& configDir); // the Dolphin's Config folder
	bool writeAzahar(const QList<Pad>& pads, const QString& iniPath); // the Azahar qt-config.ini in use
	bool writeCemu(const QList<Pad>& pads);
}
