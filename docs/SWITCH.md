# Switch emulation

USCL discovers extracted `.nsp`, `.xci`, and `.nro` games in Add Games and launches them with Eden's SDL frontend (`eden-cli`). Switch games use a separate library, emulator profile, controller configuration, saves, and firmware. RPCS3's settings and Sony's game-update service are not used for Switch games.

## Setup

1. Install Eden and its matching `eden-room` server from [the Eden project](https://eden-emu.dev/downloads/). The initial integration was checked against **v0.2.1**. Native `eden-cli`, `eden`, and `eden-room` executables can sit beside USCL or on PATH. In Settings → Switch Emulation you can select them individually. An Eden Linux AppImage can be selected for both the core and settings application; USCL invokes its `eden-cli` entry point for games.
2. Import your console's `prod.keys` using Settings → Switch Emulation. Import an extracted Switch firmware directory if the game needs it; USCL does not block every game merely because firmware is absent. Imported files stay in its Switch profile. A blank key template or firmware from another console platform cannot replace those files. USCL does not download keys or firmware.
3. Extract game archives before using Add Games. Split RAR volumes must be extracted together. Recognized base-game containers appear in the library; filename-tagged update and DLC containers are excluded. Install those through Controllers, Updates and DLC, which opens Eden's settings application.
4. Configure controllers in Eden, then start the game from USCL. The SDL frontend reads the same `qt-config.ini` as the settings application. Homebrew NRO files can launch without USCL's commercial-game key/firmware prerequisite checks.

Hold Guide/PS for 1.5 seconds to close a running Switch game and return to USCL. The external Switch core does not implement RPCS3's in-game home-screen overlay.

Environment overrides: `USCL_SWITCH_CORE`, `USCL_SWITCH_GUI`, `USCL_SWITCH_ROOM` are executable paths, not shell command strings. `USCL_SWITCH_PROFILE` overrides the profile directory. Otherwise it is `<USCL settings>/uscl/switch`. On Linux the core's XDG config/data/cache directories all live beneath that profile; on Windows its APPDATA is isolated there. Do not use an Eden Windows installation with a portable `user` folder because that overrides APPDATA.

This implementation is an external-core integration. Release installers do not yet bundle Eden. Distributing a core later requires its runtime dependencies, license notices and corresponding-source obligations to be handled alongside packaging.

## Friends over Steam

1. Use the same Eden release and game updates/DLC on every PC. Every player has their own game and console files.
2. From the game's USCL options, select Host Online Session. USCL starts an unlisted Eden room bound to `127.0.0.1:24872` and a Steam bridge with the `switch` preset. No RPCN or Demonware service starts for this route.
3. Friends join the USCL session through the Friends tab or an existing Steam invite, and wait for the session to show connected.
4. Everyone starts the Switch game from USCL. The core receives `--multiplayer <nickname>@127.0.0.1:24872`. The host connects directly to the room. Members connect through the bridge's UDP forward to the host's room. Normal Steam session kick/ban handling applies to relayed traffic.
5. In **Mario Kart 8 Deluxe**, choose **Wireless Play**. One player creates the in-game room; the others join it. The Eden transport room and the game's own race room are separate stages.

This uses emulated local wireless (LDN), not Nintendo's Online Play service. It supports games with that multiplayer mode; it is not a blanket promise for all Switch games. The room transport is ENet over forwarded UDP, leaving retransmission and ordering to ENet. The host must remain connected.

The title ID in a bracketed filename is used as the library/session ID; without one, a size/header fingerprint provides a location-independent fallback. Titles and IDs are not decrypted from game contents. Installed update/DLC versions are not yet verified automatically, so match them manually. Switch game hashes do not use RPCS3's EBOOT hash check.

## Development preview and validation

On Linux, `tools/dev-scripts/switch-preview.sh` builds and launches a separate preview with isolated settings. `--build-only` builds, scans the local extracted-game folder if present, and runs the shell self-test. Set `USCL_SWITCH_GAMES` to scan a different directory. On this PC the defaults find the separately installed Eden AppImage/server in `~/Applications`.

`uscl --scan-games <folder>` performs the same import as Add Games, saves the library, prints JSON, and exits without starting Steam or the GUI. Set the profile/XDG variables first when testing.

Validated on Linux after integrating with the current Steam-tunnel branch: shell build, QML startup smoke test and self-test; Switch discovery, exclusion of known updates/DLC and malformed NSP headers, persistence, repeat scans, removal without deleting game files, failed-save reporting, and a mock core process receiving the isolated profile and automatic room arguments; all 21 Rust bridge unit tests including the Switch UDP preset; actual Eden v0.2.1 room process staying alive and owning loopback UDP 24872.

Mario Kart boot tests on November 9–10 reached its title screen. The supplied update is identified as 4.0.1, but testing stalled before a race (at Mii selection in the graphical frontend). Physical controllers and multiplayer remain unverified. See the [game notes](../games/0100152000022000.md). Shared Qt/SDL keyboard bindings also need validation; their keyboard code formats differ. On Linux, USCL now chooses X11/XWayland if the desktop forces Wayland and an X11 display is available, because the tested SDL2 bundle rejected that forced driver.

Protocol/CLI references: [Eden command line](https://git.eden-emu.dev/eden-emu/eden/src/tag/v0.2.1/docs/user/CommandLine.md), [multiplayer](https://git.eden-emu.dev/eden-emu/eden/src/tag/v0.2.1/docs/user/Multiplayer.md), and the v0.2.1 SDL frontend's `--help` output. The documented CLI options were checked against the downloaded release, not assumed from another emulator.
