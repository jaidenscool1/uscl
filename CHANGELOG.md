# USCL Changelog

## Unreleased

These changes are included in this development branch. They are not a list of published releases.

### Console themes

- Added PS4, PS5, Xbox 360 Classic, Xbox 360 2012, Xbox One, Switch and Switch 2 themes alongside PS3.
- Added console-inspired game shelves, blades and tile dashboards, with matching settings, dialogs, notifications and button hints.
- Kept game launch/resume, friends, parties, DLC, game updates and USCL update controls available in every theme.
- Improved selection restoration, mouse actions, long-dialog scrolling and layouts in 4:3 and ultrawide windows.
- Show running games once in modern libraries, with a Resume action.

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
