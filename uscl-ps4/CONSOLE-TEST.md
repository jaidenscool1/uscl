# Hardware validation: pending

The native package is an unverified preview, not a finished PC-equivalent port.
Do not mark any item passed without observing it on the PS4. No live console
connection was supplied during the build.

## First launch

1. Record PS4 model, firmware **9.00**, exact GoldHEN version, and the package SHA-256.
2. Copy the USCL PKG to an exFAT USB drive. Enable GoldHEN as usual, then install
   through its Package Installer. Record any installation error code verbatim.
3. Open **USCL Native Preview**. Check the 1080p screen, text, category icons,
   D-pad/stick navigation, Cross, Triangle rescan, Options help and Circle exit.
4. If startup fails, collect `/data/uscl/startup.log` over GoldHEN FTP if it exists.
   A failure before credential setup may require the console's crash information.
5. Select an already installed PS4 application. Check both the launch request and
   the application's visible start. Test returning through the PS4 home screen.
6. Check controller disconnect/reconnect, sign-in changes and external storage.

## PS3

Install the separate upstream **RPCS3 PORT FOR PS4 v0.40** package (`RPCS30010`).
First start it directly and install your own PS3 firmware with its firmware UI.
Use its documentation to prepare your own games and any required licenses.

USCL scans `/data/rpcs3/games`, `/data/rpcs3/dev_hdd0/game`, and
`/mnt/usb0/PS3/GAMES` / `/mnt/usb1/PS3/GAMES`, through six subdirectory levels.
Disc folders need `PS3_GAME/PARAM.SFO` and `PS3_GAME/USRDIR/EBOOT.BIN`; HDD titles
need `PARAM.SFO` with category HG and `USRDIR/EBOOT.BIN`. Update-only category GD
is excluded. ISO entries use their filename; discovery does not prove decryptability.

1. Verify one game boots directly in the upstream emulator first.
2. Open USCL, rescan and choose it in PS3. Check `/data/rpcs3/boot.txt` contains
   exactly its console path. Confirm the same game actually starts.
3. Test controller input, graphics, audio, a save/load cycle, 15 minutes of play
   and a clean exit. Then test a second game.
4. Collect `/data/rpcs3/startup.log`, `/data/rpcs3/log/RPCS3.log` and
   `/data/rpcs3/log/mesa.log` on failure. Keep personal data out of public reports.

## Switch

Install the separate upstream **NX on Orbis v0.1.0** package (`EDPS00001`).
Its author tested only 12.02 on PS4 Pro, so first establish that it boots at all
on your 9.00 console. Its bundled Homebrew Menu can test CPU/GPU/input without
game keys. Follow the upstream installation guide for your own Switch keys and
firmware; the PS4's firmware does not replace Switch firmware.

USCL scans `/data/edenps4/roms`, `/mnt/usb0/Switch` and `/mnt/usb1/Switch`, through
six subdirectory levels. Supported discovery extensions: NSP, XCI and NRO.

1. Boot the upstream Homebrew Menu directly first, then a game directly.
2. Open USCL, rescan and select that game in Switch. Check
   `/data/edenps4/game.txt` names it and that the correct game starts.
3. Check input, colors, sound, saves and measured frame rate during real gameplay.
   The upstream MK8D result was 13–16 fps with incorrect colors, not PC parity.
4. Collect `/data/edenps4/boot.log`, `/data/edenps4/mesa.log` and
   `/data/edenps4/log/eden_log.txt` on failure.

## Result record

For each test record: package checksum, console model/firmware/GoldHEN, application
and game versions, observed result, error code, and relevant trimmed log lines.
Installation, launcher boot, native PS4 launch, PS3 handoff, Switch handoff and
gameplay are separate results. Multiplayer and PC parity remain unimplemented
even if all the above hardware tests pass.
