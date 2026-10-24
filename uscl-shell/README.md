# uscl-shell: the USCL home screen

The XMB-style home screen USCL opens to, made for a controller (see `docs/CONSOLE-UI.md`). It's Qt Quick (QML)
plus SDL3 for controllers. Games run in our RPCS3 build as a separate process (`rpcs3 --no-gui --fullscreen`),
so a crash brings you back here. Settings → All Settings opens the classic RPCS3 window.

## What's in it
- **First start:** setup asks for an online name (your Steam name, read from Steam's `loginusers.vdf`, is the
  default), installs the PS3 system software (from a `PS3UPDAT.PUP` it finds, or downloaded from Sony's update
  server) and finds your games. Settings → Run Setup Again repeats it.
- **Game:** your games; △ for Start / Host an online session / Information. Add Games walks a folder and
  everything inside it (disc folders, ISOs, installed games) and adds what it finds to RPCS3's `games.yml`.
- **Network:** host a session for a game (the right servers start by themselves: our Demonware for Black Ops II),
  join by Steam invite, leave, and Test Connection (USCL files, online settings, ports, a Steam lobby round trip).
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
- **Controllers:** when a game starts, every connected controller becomes a player in RPCS3 (Settings →
  Accessory Settings → Automatic Controller Setup). A player already on a controller only gets its device updated,
  so custom button mappings stay.
- **Resolution scale:** Automatic (Settings → Game Settings → Resolution Scale) picks it from the best Vulkan
  graphics card and the screen: integrated or under 3.5 GB 100%, under 6 GB 150%, under 10 GB 200%, else 300%,
  never sharper than the screen (1080p 150%, 1440p 200%, 4K 300%). `uscl --hardware` prints what it sees.

## Assets
Personal sounds: WAVs in `<settings folder>/sounds/` (cursor, select, category, back, error, notify, startup)
replace the built-in ones on that PC only; they are never packaged.

Sounds: the player's own XMB sounds (JStationX `resource.uisounds.xmb`, synthesized originals). Fonts: Noto Sans,
Roboto Thin (licences in `fonts/`), Material Design Icons (Pictogrammers, free licence). The background is drawn
by the shader; no console maker's artwork is included.
