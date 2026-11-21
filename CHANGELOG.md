# USCL Changelog

## Unreleased

- Switch keys are found without importing them by hand. USCL now also looks on every other drive, including the Switch's SD card (`switch\prod.keys` from Lockpick_RCM). It accepts the names key files get on Windows (`prod.keys.txt`, `prod (1).keys`, `keys.txt`), checks each file's `header_key` against a real NCA on the PC and picks the keys that work. Starting a Switch game without usable keys searches once more and starts the game if they turn up.
- A USCL ticker next to every game shows how well it plays online: USCL Online, USCL Online Beta, USCL Online Soon or Offline. It's read from the game tracker built into the app, and the Information panel explains it.
- Switch: games now run in the bundled, patched graphical Eden (the tested one) by default. They used to start in `eden-cli`, which stopped on a black screen. A Switch emulator chosen in Settings → Switch Emulation is kept.
- Switch: show each game's installed update and DLC (read from Eden's NAND) in its Information panel, and check them when joining a session. A member whose update or DLC differs from the host's is told what the host has and what they have, before the game starts. Updates still come from the player's own dumps.

## 0.5.4-alpha — 2026-11-18

- Fix session readiness parsing after the bridge added short session codes; Switch games such as Mario Kart 8 Deluxe can launch after hosting connects.
- Offer Start Offline when a Switch session fails.
- Search Windows Known Folders, including redirected Downloads/Desktop/Documents, and keep searching other emulator profiles after Add Games.
- Follow linked game folders and Windows junctions with cycle detection; expose Known Folders in Add Games.
- Discover valid derived console-key dumps and merge newly found title keys without discarding existing game keys.
- Keep the v0.5.3 native core baseline and standalone Eden room server. No new two-PC gameplay result is claimed.

## 0.5.3-alpha — 2026-11-18

Console menus, original theme audio and safer firmware installation. The Linux
AppImage reuses the verified v0.5.1-alpha native cores with a rebuilt 0.5.3 shell;
native emulator loading colors require separately rebuilt cores on Linux.
Windows rebuilds cores from the release patch manifest. Switch 2 PR #18 and native
PS4 PR #19 remain outside this release.

- Include the standalone Eden room server in both installers so Switch hosting can start (PR #21).
- Preserve installed Switch firmware and version metadata when replacement import fails.
- Add a sourced Switch 2 CPU/GPU translation tracker, separating baseline and pending work.
- Update all maintained emulator/server fork READMEs and packaging notes.

### Console themes

- Added PS4, PS5, Xbox 360 Classic, Xbox 360 2012, Xbox One, Switch and Switch 2 themes alongside PS3.
- Added console-inspired game shelves, blades and tile dashboards, with matching settings, dialogs, notifications and button hints.
- Kept game launch/resume, friends, parties, DLC, game updates and USCL update controls available in every theme.
- Improved selection restoration, mouse actions, long-dialog scrolling and layouts in 4:3 and ultrawide windows.
- Show running games once in modern libraries, with a Resume action.
- Reorganized modern home screens into software, system shortcuts and a separate full library instead of six generic category shelves.
- Matched the PS4 function/content focus flow, Xbox 360 2012 nine-tile geometry, and Switch 2 grouped toolbar more closely to console references.
- Added Home-button quick access, console-filtered scrolling libraries and Nintendo's persistent settings-section sidebar.
- Added persistent Xbox One game pins, extra pin pages and Pin/Unpin actions.
- Fixed light-theme glyph contrast, hidden shortcut focus, selection clamping and directional access to dashboard tiles.
- Smoothed tile resizing, PS4 function-area travel and title arrival, quick-menu slides and modern page entrances.
- Refined PS5 strip and utility geometry from Sony's walkthrough; added functional Control Center cards and a game Options button.
- Matched Nintendo HOME card sizing, toolbar spacing, profile/status placement and Switch 2 multicolor focus outlines to official footage.
- Separated Xbox One's three-row Pins grid from its Home dashboard and added a news rail, with directional pivot/blade transitions.
- Refined PS4 Quick Menu typography, spacing, profile footer and focus frame from Sony's published screenshot.
- Added asynchronous game-backdrop crossfades that retain the previous image until the selected artwork is ready.
- Kept opaque game artwork inside Switch 2 rounded cards on Home and in the library.

- Fixed incomplete library rows, filter relayout and restoration of the selected game when returning from nested menus.
- Kept library tools visible, added selection/scroll indicators and prevented activation of a game scrolled offscreen.
- Removed Xbox focus-tile overlap, bounded long titles and settings descriptions, and staged PS4 function icons to avoid collisions during transitions.
- Settled reused backdrop buffers before loading new artwork and hid closing overlays when switching themes.

- Refined PS5 Control Center portrait-card proportions, preview/context/title hierarchy and bottom control spacing against Sony footage.
- Added live library-card previews and delayed scrolling for long Nintendo HOME titles; hidden or motion-disabled titles stay still.

- Added saved library sorting (original order, title A–Z/Z–A, or console then title), with mouse/controller access and selection preserved across reordering.
- Added a visible Sort control, readable console labels and updated navigation help.

### Emulator launch screens

- Added a themed shell launch/return screen for every home-screen emulator path, with game artwork and title.
- Added opt-in native loading colors for rebuilt RPCS3, Eden, Eglantine and Azahar cores.
- Kept real progress, shader compilation, cancellation and native window handoff under emulator control.
- Paused shell launch animation when the emulator has focus; no artificial loading delay is added.

### Theme audio

- Added eight original sound palettes: navigation, category changes, confirm, back, cancel, errors, notifications and startup.
- Sound palettes follow the selected theme automatically; no console recordings or sampled console sounds are bundled.
- Balanced navigation cues below confirmations and alerts, with headroom to prevent clipping.
- Added a saved Menu Sound Volume control in Sound Settings, separate from game volume.
- Added per-theme custom WAV overrides while retaining global custom sound support.

### Reliability and documentation

- Fixed backend lifetime ordering so shutdown bindings retain their update and party models.
- Added automated theme interaction checks and full-app smoke checks for all eight themes.
- Documented emulator patch sets, pinned build revisions, packaging limits and the upstream update workflow.
- Added this offline changelog to Settings in every theme.

### Verification still needed

- Console-by-console visual and audio comparison, including perceived volume on real output hardware.
- Physical controller checks, actual emulator launches, network sessions and installing real updates for the theme changes.

Themes change USCL's interface; they do not add PS5 or Xbox One emulator support.

## Earlier releases

Historical release notes are available on the USCL GitHub Releases page. This changelog does not reconstruct unverified release history.
