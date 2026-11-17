# Console themes

Settings → Theme Settings → Theme offers eight persistent layouts. Unrecognized saved themes fall back
to PS3 XMB. A theme changes the shell, not the supported emulator list; PS5 and Xbox One are interface
styles, not emulator integrations.

## Layout targets and USCL actions

The modern themes no longer display the XMB's six categories as game tiles. Software, system shortcuts,
settings and the full library have separate navigation regions. Home strips contain up to 12 games and a
library entry. Add Games, updates and help remain available through the library sidebar. The library keeps
all games, including games beyond the home strip, with console filters and scrolling.

| Theme | Target and organization | Preview with synthetic entries |
|---|---|---|
| PS3 | Existing crossbar, console folders, remembered category selection and breadcrumb columns | [Home](images/theme-ps3.png) |
| PS4 | Mature 2016–2020 layout: 200 px neighboring tiles, a 330 px selected image inside a frame, a Start strip, function screen above and game information below | [Home](images/theme-ps4.png) |
| PS5 | Launch-era compact software strip, game backdrop, large game title, Play action and a bottom Control Center ribbon | [Home](images/theme-ps5.png) |
| Xbox 360 Classic | Launch-era four-blade structure, silver edges, colored active blade, gamer card, menu list and game/tray bar; inner libraries use a vertical list | [Home](images/theme-x360classic.png) |
| Xbox 360 2012 | October 2012 three-row, nine-tile geometry: three left tiles, central two-row hero, two lower wide tiles and three right tiles; Home/Social/Games/Settings pivots | [Home](images/theme-x360.png) |
| Xbox One | Original 2013 dark dashboard: Pins/Home/Library pivots, large game tile, tall library tile, stacked side tiles and bottom tiles | [Home](images/theme-xboxone.png) |
| Switch | Original dark HOME organization, square software row, separate profile, six circular system shortcuts and mint focus outline | [Home](images/theme-switch.png) |
| Switch 2 | Light HOME organization, rounded software cards and a grouped lower toolbar, including USCL party chat | [Home](images/theme-switch2.png) |

The screenshots use made-up titles and USCL icons, without personal profiles or game artwork. Installed
games use their own icons and backgrounds. Switch 2 cover corners remain rounded with opaque artwork on both renderers.
Nintendo tool glyphs are original vector drawings, with colors
that remain visible on the light toolbar. The status clock is separate from profile actions.

USCL actions replace console-only services in the corresponding regions. There are no pretend storefronts,
achievements, media apps or platform subscriptions. For example, the Xbox spotlight opens USCL's offline
changelog, Nintendo's import shortcut opens Add Games, and chat opens USCL's existing Party integration.
The original four-blade Media position becomes Library. These substitutions, original fonts/artwork and
remaining visual differences mean this is not a certified pixel-identical recreation.

Xbox One Pins are functional: a game's Options menu offers Pin to Home / Unpin from Home. Pins persist in
shell preferences and use additional dashboard pages when needed; unpinning or removing games clamps the
selection. Restoring menu defaults clears pins.

Additional captures: [PS4 function screen](images/theme-ps4-functions.png),
[PS5 Control Center](images/theme-ps5-quick.png), [Nintendo settings](images/theme-switch2-settings.png),
and [filtered library](images/theme-ps5-library.png).

## Navigation

| Context | Controls |
|---|---|
| PS3 home | Left/right category; up/down item; L1/R1 jumps five |
| PS4 home | Left/right software; Up opens functions; Down opens Information/Options actions; Back returns to software |
| PS5 home | Left/right software; Up focuses visible system shortcuts; Down focuses the game action; Home opens Control Center |
| Nintendo home | Left/right software; Up focuses My Page; Down focuses the lower system row; Back returns to software |
| Xbox 360 Classic | Left/right or L1/R1 switches blades; up/down selects a menu item or the game/tray bar |
| Xbox tile dashboards | Directional movement follows tile positions; Up at the top focuses pivots; L1/R1 changes pivot; side edges change pivot or pin page |
| Quick Menu / Control Center | Home opens/closes; arrows select; confirm opens the selected USCL section; Back closes |
| Library | Directional grid movement (vertical list in Blades); Left at the first column focuses filters/tools; Up/down selects a filter or tool; Right returns to games |
| Nintendo settings | Root list previews each section; Enter opens it; Left in a section focuses the persistent section sidebar; Right/Enter opens the selected section; Back returns |
| Other settings | Up/down item; confirm opens; Back/left returns; L1/R1 jumps five |
| Games | Confirm starts/resumes; Options/Space opens actions; Info/I opens information |
| Mouse | Click focuses, a second click activates; right-click opens game options; shortcuts activate directly; lists/grids support scrolling |
| Dialogs | Up/down or wheel scrolls long content; L1/R1 scrolls further; left/right selects buttons |

Keyboard Enter/Esc and the existing physical-button mapping continue to work. Theme changes retain the
active page. Back from a nested page restores its parent selection. Empty/live lists clamp selection.
The 1920×1080 stage fits both window axes, with margins at other aspect ratios. Home artwork is bounded
and library delegates are virtualized; hidden pin-page covers do not decode.

## Research and reference limits

The local JSXTV Kodi source (`kodi-xmb`) supplied measured PS4 and Xbox design notes and private retail
reference frames. Those were inspected for geometry, spacing, focus and transitions. The live Kodi server
was unreachable during this pass. No server configuration, private frames or owner-supplied audio was
copied into USCL.

Primary references used to check menu roles and era:

- [PS4 home-screen guide](https://manuals.playstation.net/document/en/ps4/basic/screens.html): function, content and information areas, Library and Up/Down navigation.
- [PS5 launch UI reveal](https://blog.playstation.com/2020/10/15/first-look-playstation-5s-next-generation-user-experience/) and [Control Center guide](https://www.playstation.com/en-us/support/games/customize-ps5-control-center/): game home and quick access.
- [Nintendo Switch HOME overview](https://en-americas-support.nintendo.com/app/answers/detail/a_id/22308): user profiles, software and system functions.
- [Nintendo Switch 2 HOME overview](https://en-americas-support.nintendo.com/app/answers/detail/a_id/68181) and [Nintendo's transfer guide screenshots](https://support.nintendo.com/jp/switch2/mastery/transfer/switch-to-switch2/index.html): rounded cards, grouped toolbar and status/profile placement.
- [Xbox's October 2012 dashboard announcement](https://news.xbox.com/en-us/2012/10/15/new-xbox-360-dashboard-update-starts-rolling-out-today/): the selected Metro era and content organization.
- [Xbox's November 2013 SmartGlass announcement](https://news.xbox.com/en-us/2013/11/19/xbox-one-xbox-smartglass/): original Xbox One pinning. The Kodi reference set covers Xbox 360 rather than Xbox One; Xbox One's detailed appearance remains less fully verified.

Blades measurements come from the local reference set's launch dashboard captures. Switch 2's ten toolbar
positions are mapped to available USCL functions; Switch uses the earlier six-shortcut arrangement.
The proprietary console fonts, platform network features and copyrighted system artwork are not bundled.

## Original audio

`uscl-shell/tools/menu_sounds.py` creates every built-in WAV from oscillators, deterministic noise,
envelopes and stereo echoes. Each theme selects its own complete eight-cue set immediately when changed:

| Theme | Original sound character |
|---|---|
| PS3 | Airy high ticks and a slow ensemble startup swell |
| PS4 | Lower, softer glass tones with a spacious tail |
| PS5 | Rounded, layered tones with wider stereo echoes |
| Xbox 360 Classic | Deep, resonant electronic pulses with falling pitch |
| Xbox 360 2012 | Tighter electronic taps and rising accents |
| Xbox One | Subdued low tones and restrained echoes |
| Switch | Very short, bright percussive clicks |
| Switch 2 | Fuller bright clicks with an extra upper partial |

These are original compositions, not transcriptions or console recordings. All menu actions, dialogs,
notifications and startup use the selected palette. Switching themes reloads the cues; startup plays on boot,
not on each theme change. Settings → Sound Settings → Menu Sound Volume applies a saved 0/25/50/75/100% control to every set,
including custom WAVs, independently of game volume. Menu Sounds Off also mutes currently playing cues.
The original mix targets active-window RMS of −32 dBFS for cursor, −30 for category, −26 for confirm/notify,
−29 for back/cancel/startup and −25 for error, with a 0.28 full-scale peak ceiling (about −11 dBFS).
This balances UI cues; these are design targets, not measured console output calibration. Actual listening
volume also depends on the OS mixer, output device and speakers. Console-level perceptual similarity still
needs listening comparison; automated checks cannot certify an exact sound match.
Custom `<settings>/sounds/<theme>/<cue>.wav` files take precedence, followed by global
`<settings>/sounds/<cue>.wav` overrides, then the built-in set. Theme folder IDs are `ps3`, `ps4`, `ps5`,
`x360classic`, `x360`, `xboxone`, `switch` and `switch2`.
Regenerate from the repo root with `python uscl-shell/tools/menu_sounds.py`. Generated audio is 48 kHz,
16-bit stereo, with headroom and a final fade to zero.

## Verification

```
cmake -S uscl-shell -B shell-build -G Ninja -DCMAKE_BUILD_TYPE=Release -DUSCL_UI_TESTS=ON
cmake --build shell-build
ctest --test-dir shell-build --output-on-failure
python uscl-shell/tools/theme_smoke.py shell-build/uscl
```

On Visual Studio use `ctest --test-dir shell-build -C Release --output-on-failure` and pass
`shell-build/Release/uscl.exe` to the Python script. Windows CI runs these suites and the core self-test.

The interaction suite covers empty/live lists, 240-game libraries, console filters, keyboard access to
library tools, every Xbox tile's directional reachability, multiple pin pages, Nintendo section navigation,
shortcut routing, launch/options/info actions, disabled rows, dialogs and launch screens. QML binding,
resource and JavaScript warnings fail these tests.

The compiled-shell smoke test loads all eight packaged themes with isolated temporary preferences. It
checks stage bounds at 1280×720, 800×600 and 2560×1080, real dashboard-to-page connections, Back navigation,
Home-key quick access, pin preference updates, switching/restoring themes, theme-specific sound bindings,
mute/volume settings and opening/scrolling/closing the offline changelog.

For synthetic captures, run `uscl-ui-tests -input uscl-shell/tools/tst_theme_previews.qml`. It writes home,
functions, quick-menu, settings, library and loading captures to the system temporary directory. The
fixture fails on QML/runtime/resource warnings. These screenshots exercise real UI components with synthetic
models; the compiled-shell smoke test separately verifies wiring to the real application models.

These checks do not prove successful emulator gameplay, network sessions, update installation, physical
controller behavior or exact perceptual sound matching. Those need the relevant cores, lawful game files,
accounts and output hardware. The UI test suite does not claim 100% console fidelity or zero possible bugs.

## In-app changelog

Every theme exposes Settings → USCL Changelog. The app bundles the root `CHANGELOG.md`, renders its
Markdown locally and supports controller and mouse scrolling. Development changes remain under Unreleased;
the dialog heading separately shows the installed USCL version. The full-app smoke suite opens, scrolls and
closes this menu entry in all eight themes.
