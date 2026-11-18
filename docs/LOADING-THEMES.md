# Theme-aware emulator loading

## Launch and window handoff

Every emulator started through the home screen uses `GameLaunch.qml` behind its own window. It follows the
selected palette, shows the game icon/title, and uses console-inspired framing. The shell reports “Starting
emulator…” only while QProcess is starting, then “Emulator running”. Process start is not proof that a game
has finished loading; no artificial percentage, minimum delay or guessed ready timer is used.

The screen fades in/out without changing window flags or requesting focus. Its indicator stops when the
shell loses focus, when the process starts, or when the screen is hidden. Native loading and error windows
remain accessible. Process failure/exit returns through the existing launcher handling. Classic emulator
settings windows use the themed shell return screen but do not receive native loading overrides.

## Native loading coverage

| Emulator | Native support in this change |
|---|---|
| RPCS3 | Qt progress dialogs and renderer-owned boot/shader progress overlays; guest renderer dialogs retain their own colors |
| Eden | Graphical Qt loading widget and stage-specific progress bar |
| Eglantine | Graphical Qt loading widget and stage-specific progress bar |
| Azahar | Graphical Qt loading widget and stage-specific progress bar |
| Dolphin | Themed shell launch/return screen; no native shader/OSD or NetPlay-dialog changes |
| shadPS4 | Themed shell launch/return screen; Vulkan/ImGui and game-supplied splash artwork remain unchanged |
| melonDS | Themed shell launch/return screen for direct `startEmu` calls; the separate DS agent does not inherit a selected shell theme |
| Cemu / Xenia | Themed shell launch/return screen; external Flatpak/Wine UI is unchanged |

SDL/headless variants have no Qt loading widget to style. Native support requires rebuilding the relevant
patched emulator; the shell screen works with older cores. This does not replace a game's own loading
screens or console system software.

## Contract and patch format

Immediately before launching a game, QML snapshots its `ThemePalette` colors through
`Launcher::setLoadingTheme`. The launcher validates the theme ID and colors, then sets these variables on
the child process only:

- `USCL_THEME`: one of the eight shell theme IDs.
- `USCL_LOADING_STYLE`: a locally generated stylesheet for native loading widgets.
- `USCL_LOADING_COLORS`: exactly three six-digit RGB colors separated by commas (background, text, accent).
- `USCL_SHELL=1`: opts a game process into USCL integration.

These variables are cleared for classic/settings tools. No emulator configuration files, compiler settings,
shader caches, game files or firmware are modified by this styling contract. Invalid palettes fall back to
upstream styling. Selection changes apply to the next launch rather than restyling a running emulator.
Qt stylesheets are scoped to loading/progress widgets; no application-wide stylesheet is installed.

Patches are generated with `git format-patch` against the documented pinned bases and existing patch series.
The complete RPCS3 series is regenerated against `eee3f0f`, as required by `AGENTS.md`. The new patches are
RPCS3 0014, Eden 0013, Eglantine 0009 and Azahar 0005. Existing Windows cache keys include patch contents;
Linux packaging consumes local builds, so rebuild those cores before creating an AppImage.

## Verification

The shell UI suite covers all eight launch palettes, fade visibility and animation suspension. The core
self-test checks validated stylesheet generation, isolated fake Switch/Dolphin/Azahar/melonDS processes,
classic-tool override isolation, startup failure, nonzero exit and explicit stop. Full-app theme checks continue to verify all eight packaged themes.

All four patch series replay cleanly from their pinned bases. The six changed native C++ translation units
compile using their configured Linux compiler flags, and the RPCS3 RGB parser passes valid/malformed-input
checks. These checks run in isolated copies; developer emulator checkouts
and player profiles are not edited. These checks do not establish actual game boot timing, first-frame
smoothness, hardware controller behavior or driver-specific rendering. Real game launches and visual
acceptance on Linux and Windows remain required before describing all emulator loading paths as verified.
