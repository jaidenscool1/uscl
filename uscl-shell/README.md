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
| PS5 | KytyPS5 (experimental; a game folder with `eboot.bin` + `sce_sys/param.json`) | `launcher.cpp` `startEmu` |

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
- **Friends:** your Steam friends (`uscl-bridge friends`) with a status dot; ✕ on one offers Join Session and their profile card (`ProfileCard.qml`): a USCL tab (trophies, play time and latest games, shared with `uscl-bridge presence`) and a Steam tab (their Steam Community profile, read inside USCL).
- **Trophies and play time:** PS3/PS4 trophies from RPCS3/shadPS4, USCL play-time trophies for the other consoles, a popup in the theme's style when one unlocks (`profile.cpp`, `TrophyToast.qml`; see [themes](../docs/CONSOLE-THEMES.md#trophies-and-profiles)).
- **Settings:** online name, eight console themes (PS3, PS4, PS5, Xbox 360 Classic/2012, Xbox One, Switch and Switch 2), XMB colours, menu sounds, controllers, system software,
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

`shaders/tint.frag.qsb` is compiled from `tint.frag` (command in CMakeLists.txt); rebuild it after changing
the shader. Software rendering uses a gradient fallback for the XMB background.

## Console themes and verification

Choose a look in Settings → Theme Settings → Theme. Modern themes show games directly; the PS3 XMB keeps
console folders. All themes use the same launch, online, settings, DLC and update actions. On modern home
screens L1/R1 (Page Up/Down) changes category; on inner pages it jumps five rows. Xbox 360 Classic also uses
left/right to change blades and up/down to select. Mouse: click to focus, click again to open; right-click for
options. Long dialogs scroll with up/down or the wheel.

Build the UI tests with `-DUSCL_UI_TESTS=ON`, then run:

```
ctest --test-dir <build folder> --output-on-failure
python uscl-shell/tools/theme_smoke.py <path to built uscl>
```

`uscl --windowed --qml-tour <folder>` (with `QT_QPA_PLATFORM=offscreen`) walks every menu, settings page, dialog,
the profile card and the trophy popup in all eight themes, saves a screenshot of each in the folder and prints
QML warnings per step (`src/tour.cpp`). It runs on scratch settings with copies of your game lists.

The smoke test loads each theme with temporary preferences, checks the whole stage fits 16:9, 4:3 and
ultrawide windows, and rejects QML errors and missing packaged assets. See [theme details and previews](../docs/CONSOLE-THEMES.md).

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

## Emulator loading screens

Game launches use a themed shell handoff with game artwork and title. Rebuilt RPCS3, Eden, Eglantine and
Azahar cores also use the shell palette in native loading widgets; other native frontends keep their own UI.
The shell stops its indicator when inactive and adds no loading delay. See
[coverage, patch contract and verification](../docs/LOADING-THEMES.md).

## Changelog

Settings → USCL Changelog reads the bundled root [CHANGELOG.md](../CHANGELOG.md) offline in every theme.
Scroll with Up/Down, L1/R1 or the mouse wheel; Back closes it. Update that file when changes ship; keep
development changes under Unreleased until a release is assigned.

## Assets
Personal sounds: WAVs in `<settings folder>/sounds/` (cursor, select, category, back, error, notify, startup)
replace the built-in ones on that PC only; they are never packaged. Theme-specific files in
`<settings folder>/sounds/<theme>/` take priority over these global overrides. Each of the eight themes has
its own original built-in sound palette, selected automatically when the theme changes
([palette descriptions and folder IDs](../docs/CONSOLE-THEMES.md#original-audio)).

Built-in sounds: original stereo synthesis from `tools/menu_sounds.py` (no console recordings or sampled console audio). Run `python uscl-shell/tools/menu_sounds.py` from the repo root to regenerate them. Fonts: Noto Sans,
Roboto Thin (licences in `fonts/`), Material Design Icons (Pictogrammers, free licence). The background is drawn
by the shader; no console maker's artwork is included.
