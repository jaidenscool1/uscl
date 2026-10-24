# uscl-shell: the USCL home screen

The XMB-style home screen USCL opens to, made for a controller (see `docs/CONSOLE-UI.md`). It's Qt Quick (QML)
plus SDL3 for controllers. Games run in our RPCS3 build as a separate process (`rpcs3 --no-gui --fullscreen`),
so a crash brings you back here. Settings → All Settings opens the classic RPCS3 window.

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
USCL_RPCS3=<path to rpcs3> uscl-shell/build/uscl --windowed      # --self-test checks the PARAM.SFO/ISO/games.yml readers
```
Needs Qt 6.5+ (Quick, Multimedia) and SDL3. Without `USCL_RPCS3` it looks for `rpcs3` next to itself, as
packaged. Games come from RPCS3's `games.yml` and `dev_hdd0/game` in USCL's settings folder; titles and icons
are read from each game's `PARAM.SFO` / `ICON0.PNG`, inside ISOs too.

`shaders/wave.frag.qsb` is compiled from `wave.frag` (command in CMakeLists.txt); rebuild it after changing
the shader.

## Assets
Sounds: the player's own XMB sounds (JStationX `resource.uisounds.xmb`, synthesized originals). Fonts: Noto Sans,
Roboto Thin (licences in `fonts/`), Material Design Icons (Pictogrammers, free licence). The background is drawn
by the shader; no console maker's artwork is included.
