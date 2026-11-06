# USCL console UI (plan, 2026-10-24)

Goal: USCL opens to a console-style home screen you drive with a controller, like a PS3. Every feature is still
there, but in a simpler format. The look comes from the player's JStationX Kodi skins on the TV server
(`ssh tv`, `/storage/.kodi/addons/skin.*`): XMB (PS3), PS4, Xbox 360 Classic and Xbox 360 (2012), plus the
controller service (`service.xmb.padkeys`), the XMB helper (`service.xmb.startup`) and the settings script
(`script.xmbsettings`).

## What we're copying from JStationX
- **Home:** a horizontal category bar with a vertical item column under the selected category. Columns slide
  when you go deeper or come back, and the parent folder's icon stays as a breadcrumb.
- **Status pill, top right:** avatar, network, notifications, downloads, battery, clock.
- **Lists:** PS4-style settings lists with a subtitle per row, and a button-hint bar at the bottom
  (`✕ Enter  ○ Back`, user on the right).
- **Controls:** ✕ select, ○ back, △ options, □ info, L1/R1 page up/down. Every move plays a sound: cursor,
  category, select, back, cancel, error, notify, plus a startup chime.
- **Themes:** one switch between the four looks, and a background colour for the XMB look.

## How it fits together
```
USCL (one install, one icon)
├── uscl          console shell, Qt Quick (QML), full screen, controller first   ← new
├── rpcs3         PS3 core: our RPCS3 build; also the "Classic mode" window with every RPCS3 setting
├── uscl-bridge   Steam session tunnel
├── uscl-rpcn     RPCN for the session
└── dw-server     Demonware (Call of Duty)
```
- **Games run as a child process** (`rpcs3 --no-gui --fullscreen <game>`; RPCS3 already supports this). If a game
  crashes, you land back on the home screen with a message instead of losing the whole app. The shell watches
  the controller the whole time: holding the PS/Guide button brings up "Quit game / Back to home".
- **Built for more consoles from day one** (see PLAN.md): a game entry says which core runs it. Later cores
  would be Xenia (Xbox 360) and shadPS4 (PS4); no PS5 emulator is usable yet, so the slot stays empty. The
  online layer (bridge, servers) doesn't care which core is running.
- **Controller input:** SDL3, which our RPCS3 build already uses, so any pad works the same everywhere
  (DualSense, DualShock, Xbox, Switch, Steam Deck). Qt has no gamepad support of its own in Qt 6.
- **Themes are QML packs** over one shared model (categories → items → actions). Every theme gets every feature
  for free; a theme only decides how things look and sound. XMB comes first, then PS4, then the two Xbox looks.
- **Online play needs no buttons:** the USCL panel's logic moves into the shell. Starting a game that needs our
  servers starts them, a Steam invite from a friend joins their session, and a Friends column shows Steam
  friends who are in USCL sessions.
- **Classic mode:** Settings → Advanced opens today's RPCS3 window, so nothing is ever out of reach.

## XMB layout for USCL
| Category | Items |
|---|---|
| Users | profile (online name and avatar, both taken from Steam by default) |
| Settings | Display, Sound, Controllers, Online (incl. Test Connection), Themes, System (firmware, updates, storage), Advanced (Classic mode) |
| Game | games found by the folder scan, sorted by console; Add Games (the recursive scan we built) |
| Network | Online sessions, Test Connection |
| Friends | Steam friends, invites, join |

## Making it work on any PC
1. **First-run setup, controller only:**
   - check the controller
   - download and install the PS3 firmware (from Sony's official update server, through RPCS3's installer)
   - pick game folders (recursive scan)
   - check Steam is running
   - online name, defaulting to the Steam name
2. **Auto-configuration:**
   - check the GPU (Vulkan) and CPU (AVX2/AVX-512, core count) once and pick settings to match
   - per-game settings, best source first: our own tested profiles (from the game notes in `games/`), then
     RPCS3's config database, then defaults
3. **Test Connection** (like the PS4 settings item): Steam signed in, bridge starts, servers start and answer,
   and a round trip to a friend over Steam.
4. **Crash safety:** games run in their own process; logs are kept and readable in a viewer from the home
   screen.
5. **Updates:** USCL updates itself from our GitHub releases (AppImage on Linux, installer on Windows), so
   patches and servers ship together.
6. **Test machines:**
   - this Linux PC
   - the player's Windows PC with a second Steam account (this also finally allows the two-PC Black Ops II
     test)
   - dysco's PC
   - CI builds for Windows and Linux on every release
   - automated UI runs with `tools/autotest` (virtual pads, clicks, screenshots)

## Assets
- **Fonts:** JStationX uses Noto Sans and Roboto (open licences), which can ship.
- **Sounds:** the XMB sounds are the player's own synthesized originals, which can ship.
- **Sony and Microsoft artwork:** logos, button glyph art, the PS3 wave video and console fonts can't go in a
  public download. Themes draw their own icons and waves, or load them from the player's own files.

## Phases
1. **Shell** (done 2026-10-24, `uscl-shell/`): full-screen QML window, XMB theme, game list from `games.yml`, launch and return through a child
   `rpcs3`, controller navigation with sounds and startup chime. AppImage and installer ship it as the default
   app; Classic mode is one item away.
2. **Settings and online** (done 2026-10-24; Steam name is the default online name): the main settings, first-run setup, the Online/Friends columns (replacing the USCL
   panel), Test Connection.
3. **Hardening:** auto-configuration, crash handling and log viewer, updater, Windows PC tests.
4. **More looks:** the PS4 theme, then Xbox 360 Classic and Metro. More cores come later.
