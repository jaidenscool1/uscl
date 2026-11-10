# Switch emulation

USCL discovers extracted `.nsp`, `.xci`, and `.nro` games in Add Games and launches them with an external Eden core. Switch games use a separate library, emulator profile, controller configuration, saves, and firmware. RPCS3's settings and Sony's game-update service are not used for Switch games.

## Setup

1. For Mario Kart with friends, use the **USCL-patched Eden graphical build** and its matching `eden-room` server. The pinned base is **v0.2.1**; source patches and a Linux build helper are in [uscl-eden](../uscl-eden/README.md). In Settings → Switch Emulation → Choose Switch Emulator, select that build's `eden` executable. This also selects it for controller/update settings. The alternative CLI option supports `eden-cli` builds, but the upstream v0.2.1 CLI fails room joining and its updated-game rendering is unresolved in this environment. USCL's desktop development preview selects the locally built graphical core automatically.
2. Import your console's `prod.keys` using Settings → Switch Emulation. Import an extracted Switch firmware directory if the game needs it; USCL does not block every game merely because firmware is absent. Imported files stay in its Switch profile. A blank key template or firmware from another console platform cannot replace those files. USCL does not download keys or firmware.
3. Extract game archives before using Add Games. Split RAR volumes must be extracted together. Recognized base-game containers appear in the library; filename-tagged update and DLC containers are excluded. Install those through Controllers, Updates and DLC, which opens Eden's settings application.
4. Configure controllers in Eden, then start the game from USCL. The graphical frontend reads native Qt settings; alternative CLI launches receive a separate `sdl2-config.ini` with translated keyboard bindings, preserving the settings application's file. Homebrew NRO files can launch without USCL's commercial-game key/firmware prerequisite checks.

Hold Guide/PS for 1.5 seconds to close a running Switch game and return to USCL. The external Switch core does not implement RPCS3's in-game home-screen overlay.

Environment overrides: `USCL_SWITCH_CORE`, `USCL_SWITCH_GUI`, `USCL_SWITCH_ROOM` are executable paths, not shell command strings. `USCL_SWITCH_FRONTEND=qt` selects the patched graphical command-line contract when configuring paths through the environment. `USCL_SWITCH_PROFILE` overrides the profile directory. Otherwise it is `<USCL settings>/uscl/switch`. On Linux the core's XDG config/data/cache directories all live beneath that profile; on Windows its APPDATA is isolated there. Do not use an Eden Windows installation with a portable `user` folder because that overrides APPDATA.

This implementation is an external-core integration. Release installers do not yet bundle Eden. Distributing a core later requires its runtime dependencies, license notices and corresponding-source obligations to be handled alongside packaging.

## Friends over Steam

1. Use the same Eden release and game updates/DLC on every PC. Every player has their own game and console files.
2. From the game's USCL options, select Host Online Session. USCL starts an unlisted Eden room bound to `127.0.0.1:24872` and a Steam bridge with the `switch` preset. No RPCN or Demonware service starts for this route.
3. Friends join the USCL session through the Friends tab or an existing Steam invite, and wait for the session to show connected.
4. Everyone starts the Switch game from USCL. The patched graphical core receives `--uscl-room <nickname>@127.0.0.1:24872`; an alternative SDL core receives `--multiplayer` instead. A requested graphical room join must succeed before the game boots. The host connects directly to the room. Members connect through the bridge's UDP forward to the host's room. Normal Steam session kick/ban handling applies to relayed traffic.
5. In **Mario Kart 8 Deluxe**, choose **Wireless Play**. One player creates the in-game room; the others join it. The Eden transport room and the game's own race room are separate stages.

This uses emulated local wireless (LDN), not Nintendo's Online Play service. It supports games with that multiplayer mode; it is not a blanket promise for all Switch games. The room transport is ENet over forwarded UDP, leaving retransmission and ordering to ENet. The host must remain connected.

The title ID in a bracketed filename is used as the library/session ID; without one, a size/header fingerprint provides a location-independent fallback. Titles and IDs are not decrypted from game contents. Installed update/DLC versions are not yet verified automatically, so match them manually. Switch game hashes do not use RPCS3's EBOOT hash check.

## Development preview and validation

On Linux, `tools/dev-scripts/switch-preview.sh` builds and launches a separate preview with isolated settings. `--build-only` builds, scans the local extracted-game folder if present, and runs the shell self-test. Set `USCL_SWITCH_GAMES` to scan a different directory. The preview selects `build/switch-core/bin/eden` when present and locates the matching room server in `~/Applications`. It supplies the local Steam runtime library path for the unbundled bridge.

`uscl --scan-games <folder>` performs the same import as Add Games, saves the library, prints JSON, and exits without starting Steam or the GUI. Set the profile/XDG variables first when testing.

Validated on Linux after integrating with the current Steam-tunnel branch: shell build, QML startup smoke test and self-test; Switch discovery, exclusion of known updates/DLC and malformed NSP headers, persistence, repeat scans, removal without deleting game files, failed-save reporting, and a mock core process receiving the isolated profile and automatic room arguments; all 21 Rust bridge unit tests including the Switch UDP preset; actual Eden v0.2.1 room process staying alive and owning loopback UDP 24872.

Mario Kart tests on November 10 passed a two-instance local Wireless Play race, course voting and keyboard driving with update 4.0.1 and usable firmware 22.5.0 in the patched graphical frontend. Remote kart movement was visible on the other instance. Neither test log contained a critical error or CPU exception. Firmware 22.5.0 is tested sufficient, not a proven minimum; the game's declared console requirement is 23.0.1. A complete race finish, physical controllers, a two-PC Steam relay race and Windows/macOS remain unverified. See the [game notes](../games/0100152000022000.md). On Linux, USCL chooses X11/XWayland for forced Wayland SDL settings and the tested graphical core when an X11 display is available. The graphical fallback is also covered by a self-test.

Protocol/CLI references: [Eden command line](https://git.eden-emu.dev/eden-emu/eden/src/tag/v0.2.1/docs/user/CommandLine.md), [multiplayer](https://git.eden-emu.dev/eden-emu/eden/src/tag/v0.2.1/docs/user/Multiplayer.md), and the v0.2.1 SDL frontend's `--help` output. The documented CLI options were checked against the downloaded release, not assumed from another emulator.
