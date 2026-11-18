# USCL Changelog

## Unreleased

These changes are included in this development branch. They are not a list of published releases.

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
