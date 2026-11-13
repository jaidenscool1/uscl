# uscl-shell: the USCL home screen

The XMB-style home screen USCL opens to, made for a controller (see `docs/CONSOLE-UI.md`). It's Qt Quick (QML)
plus SDL3 for controllers. Each game runs in its emulator as a separate process, so a crash brings you back here:

| Console | Emulator it starts | Code |
|---|---|---|
| PS3 | our RPCS3 (`rpcs3 --no-gui --fullscreen`) | `launcher.cpp` `startGame` |
| PS4 | our shadPS4 | `launcher.cpp` `startGame` |
| Switch | our Eden core ([docs/SWITCH.md](../docs/SWITCH.md)) | `switchcore.cpp` |
| Wii / GameCube, 3DS, Wii U | Dolphin, Azahar, Cemu (Flathub apps) | `launcher.cpp` `startEmu` |
| Xbox 360 | Xenia under Wine (experimental) | `launcher.cpp` `startEmu` |

Settings → All Settings opens the classic RPCS3 window.

## What's in it
- **First start:** setup asks for an online name (your Steam name, read from Steam's `loginusers.vdf`, is the
  default), installs the PS3 system software (from a `PS3UPDAT.PUP` it finds, or downloaded from Sony's update
  server) and finds your games. Settings → Run Setup Again repeats it.
- **Game:** one entry per console you have games for (PlayStation 3, PS4, Switch, Wii, Wii U, 3DS, Xbox 360); ✕ opens
  that console's games, △ on a game for Start / Host an online session / Information. The focused game's picture
  (PIC1.PNG) shows behind the menus. Games with no icon or picture of their own (everything but PS3/PS4) get them
  downloaded once into the cache (`gameart.cpp`: Switch from api.nlib.cc by title id, Wii/3DS covers from GameTDB by
  the id in the game file, the rest and the pictures from libretro's thumbnails by name). Add Games walks a folder and
  everything inside it (disc folders, ISOs, installed games, Switch `.nsp`/`.xci`) and adds what it finds: PS3
  games to RPCS3's `games.yml`, PS4 games to `uscl/ps4_games.yml`, Switch games to the Switch library. Wii, 3DS,
  Wii U and Xbox 360 games are listed in `emu_games.json` next to `games.yml`.
- **Network:** host a session for a game (the right servers start by themselves: our Demonware for Black Ops II),
  join by Steam invite, leave, and Test Connection (USCL files, online settings, ports, a Steam lobby round trip).
  The host sees who's in the session and can kick (this session) or ban (every session this PC hosts, by Steam
  ID: `<settings>/uscl/bans.txt`); Banned Players lists and lifts bans. Games started in a session run with
  `USCL_ONLINE` (no game patches or debugger), and a joining player's game must match the host's (same
  EBOOT.BIN, so the same update) before it starts.
- **Friends:** your online Steam friends (`uscl-bridge friends`); ✕ on one in a USCL session joins it.
- **Settings:** online name, theme (the 14 JStationX colours), menu sounds, controllers, system software,
  All Settings (the classic RPCS3 window).

Online play used to live in the classic window's USCL > Online panel; the home screen does the same things
(same RPCS3 settings, same data folder), so either works.

## Controls
| Pad | Keyboard | |
|---|---|---|
| D-pad / left stick | arrows | move |
| ✕ / A | Enter | select |
| ○ / B | Esc, Backspace | back |
| △ / Y, Start | Space | options (games: Start, Information) |
| □ / X | I | information |
| L1 / R1 | Page Up / Down | jump 5 rows |
| hold PS / Guide | | quit the running game |

## Build and run
```
cmake -S uscl-shell -B uscl-shell/build -G Ninja -DCMAKE_BUILD_TYPE=Release && ninja -C uscl-shell/build
USCL_RPCS3=<path to rpcs3> USCL_TOOLS=<folder with uscl-rpcn, dw-server, uscl-bridge> uscl-shell/build/uscl --windowed      # --self-test checks the PARAM.SFO/ISO/games.yml readers
```
Needs Qt 6.5+ (Quick, Multimedia, Network) and SDL3. Without `USCL_RPCS3` it looks for `rpcs3` next to itself, as
packaged. Games come from RPCS3's `games.yml` and `dev_hdd0/game` in USCL's settings folder; titles and icons
are read from each game's `PARAM.SFO` / `ICON0.PNG`, inside ISOs too.

`shaders/wave.frag.qsb` is compiled from `wave.frag` (command in CMakeLists.txt); rebuild it after changing
the shader.

## Automatic setup on every PC
- **Controllers** (Settings → Accessory Settings → Automatic Controller Setup, on by default): when a game starts,
  every connected controller, USB or Bluetooth, becomes player 1, 2, ... in connection order, in every emulator.
  Buttons go by position (the console's confirm button is the same physical button on any pad); pads with a gyro
  drive motion where the console had it. `Launcher::prepareInput`, per console:

  | Console | What USCL does |
  |---|---|
  | PS3 | RPCS3 `input_configs/global/Default.yml`: players 1–7 on the SDL pads. A player already on a controller only gets its device updated, so custom button mappings stay. |
  | PS4 | nothing to write: shadPS4 gives users 1–4 (`users.json`) the pads in order |
  | Switch | sets `USCL_AUTO_PADS=1`; Eden maps the pads itself ([uscl-eden](../uscl-eden/) 0012) |
  | Wii / GameCube | Dolphin `WiimoteNew.ini` / `GCPadNew.ini`, players 1–4 (`padconfig.cpp`); gyro + accelerometer → Wii Remote motion and pointer |
  | 3DS | Azahar's active profile, any controller (one player per console) |
  | Wii U | Cemu `controllerProfiles/controller0.xml` (GamePad) and `controller1+.xml` (Pro Controller), motion on |
  | Xbox 360 | nothing to write: Wine gives XInput players 1–4 the pads in order |
- **Resolution scale:** Automatic (Settings → Game Settings → Resolution Scale) picks it from the best Vulkan
  graphics card and the screen: integrated or under 3.5 GB 100%, under 6 GB 150%, under 10 GB 200%, else 300%,
  never sharper than the screen (1080p 150%, 1440p 200%, 4K 300%). `uscl --hardware` prints what it sees.

## Assets
Personal sounds: WAVs in `<settings folder>/sounds/` (cursor, select, category, back, error, notify, startup)
replace the built-in ones on that PC only; they are never packaged.

Sounds: the player's own XMB sounds (JStationX `resource.uisounds.xmb`, synthesized originals). Fonts: Noto Sans,
Roboto Thin (licences in `fonts/`), Material Design Icons (Pictogrammers, free licence). The background is drawn
by the shader; no console maker's artwork is included.
