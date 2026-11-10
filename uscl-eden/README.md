# USCL Eden integration

Source: https://git.eden-emu.dev/eden-emu/eden.git

Pinned base: **58c1e20ee58efa3900ba616207d460886214480b** (`v0.2.1`). Eden is GPL-3.0-or-later with additional per-file notices; retain its licenses and corresponding source when distributing a build. No console keys, firmware, or games are included here.

Apply the `git format-patch` files in order with `git am patches/*.patch` from an Eden checkout at the pinned base.

- 0001 adds missing standard/fmt includes for the current Linux toolchain.
- 0002 initializes networking in the SDL frontend and waits for an actual room join before starting an explicitly online launch. The stock frontend advertised `--multiplayer` but never called `Network::Init()`, causing `Could not access RoomMember`. Two patched SDL instances joined a real room; graphical gameplay in that frontend remains unresolved.
- 0003 adds `--uscl-room NAME@127.0.0.1:PORT` to the graphical frontend. It validates the private loopback endpoint, waits up to 15 seconds for connection, and exits unsuccessfully if it cannot join. Startup failures flush the log and exit before a game starts, avoiding an upstream partially initialized shutdown hang. Automatic joins keep the room-chat window closed so it cannot take focus from fullscreen play. Offline launches use the normal `-f -g <game>` options. Nintendo's online service is not used.

- 0004 adds a headless, loopback-only private-service client probe with a three-second deadline and bounded JSON frames. It negotiates USCL capabilities without loading a profile or booting a game. It does not implement the native Online Play menu. See [the development backend](../uscl-switch-online/README.md).
- 0005 adds an off-by-default NSD diagnostic route for MK8 Deluxe's exact observed hostname template and title ID, enabled only by `USCL_MK8_NATIVE_DIAGNOSTIC=1`. Real native-menu attempts reached a local TCP 443 observer; diagnostic TLS exposed WebSocket upgrade headers before strict nonce validation failed with game error 2306-0502. It supplies no login or matchmaking service. See [the evidence and reproduction](../uscl-switch-online/NATIVE-DIAGNOSTIC.md).

## Linux development build

Run `tools/dev-scripts/build-switch-core.sh` from the USCL repository after installing Eden's native build dependencies, including Qt Widgets, Charts, Concurrent and Network, SDL2, FFmpeg, OpenSSL, and a C++20 toolchain. Dependency sources not supplied by the system are downloaded by Eden's pinned CMake configuration. `USCL_EDEN_SOURCE` can select an existing checkout; `USCL_BUILD_JOBS` sets compilation parallelism.

The helper installs a local development core under the ignored `build/switch-core` folder. It is not a portable distribution package. `switch-preview.sh` selects this graphical core automatically when present; the regular installed USCL applications remain separate.

For a manually configured development shell, set `USCL_SWITCH_CORE` to this build's `bin/eden` wrapper and `USCL_SWITCH_FRONTEND=qt`. Continue to use the matching `eden-room` server. The graphical settings application may use the same patched Eden build.

See [Switch setup](../docs/SWITCH.md) and [Mario Kart notes](../games/0100152000022000.md) for test results and remaining limits.
