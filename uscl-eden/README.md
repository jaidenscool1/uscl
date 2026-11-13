# uscl-eden: Nintendo Switch (Eden)

| | |
|---|---|
| **Console** | Nintendo Switch |
| **Upstream** | [eden-emu/eden](https://git.eden-emu.dev/eden-emu/eden.git) (GPL-3.0-or-later, with per-file notices: keep its licenses and corresponding source with any build you share) |
| **Pinned base** | `58c1e20ee58efa3900ba616207d460886214480b` (`v0.2.1`) |
| **Our changes** | `patches/0001`–`0012`, applied with `git am` |
| **Ships in** | not yet: build the core yourself (below); the installers don't bundle it |
| **Online** | Eden local-wireless rooms over the Steam tunnel (`uscl-bridge host switch`, UDP 24872); native Online Play in development ([uscl-switch-online](../uscl-switch-online/)) |
| **Status** | Mario Kart 8 Deluxe: two copies raced on one PC through a room ([setup](../docs/SWITCH.md), [notes](../games/0100152000022000.md)) |

USCL's Switch emulator. No console keys, firmware or games are included here; players import their own in
Settings → Switch Emulation.

## Patches
- 0001 adds missing standard/fmt includes for the current Linux toolchain.
- 0002 initializes networking in the SDL frontend and waits for an actual room join before starting an explicitly online launch. The stock frontend advertised `--multiplayer` but never called `Network::Init()`, causing `Could not access RoomMember`. Two patched SDL instances joined a real room; graphical gameplay in that frontend remains unresolved.
- 0003 adds `--uscl-room NAME@127.0.0.1:PORT` to the graphical frontend. Its network header is included after the VFS wrappers so MSVC builds avoid the `<windows.h>` `CreateFile` macros. It validates the private loopback endpoint, waits up to 15 seconds for connection, and exits unsuccessfully if it cannot join. Startup failures flush the log and exit before a game starts, avoiding an upstream partially initialized shutdown hang. Automatic joins keep the room-chat window closed so it cannot take focus from fullscreen play. Offline launches use the normal `-f -g <game>` options. Nintendo's online service is not used.
- 0004 adds a headless, loopback-only private-service client probe with a three-second deadline and bounded JSON frames. It negotiates USCL capabilities without loading a profile or booting a game. It does not implement the native Online Play menu. See [the development backend](../uscl-switch-online/README.md).
- 0005 adds an off-by-default diagnostic route for MK8 Deluxe's exact observed hostname templates and title ID, enabled only by `USCL_MK8_NATIVE_DIAGNOSTIC=1`. Both the game server host (`g2b309e01-%.s.n.srv.nintendo.net`) and the account hosts (`nncs1-%.n.n.srv.nintendo.net`, `nncs2-%.n.n.srv.nintendo.net`) resolve to loopback (127.0.0.1 and 127.0.0.2), through both NSD and the DNS resolver. It supplies no login or matchmaking service. See [the evidence and reproduction](../uscl-switch-online/NATIVE-DIAGNOSTIC.md).
- 0006 supplies a private local identity and loopback address under the same opt-in: the account service parses `USCL_MK8_NATIVE_CREDENTIAL_FILE` (`USCL_NATIVE_ID_V1` id plus 64-hex token) and returns that id/token where the game requests account identity, and NIFM reports 127.0.0.1 so the game dials the local adapter. It does not implement authentication or matchmaking itself.
- 0007 lets the UDP controller poll interval be overridden with `USCL_UDP_POLL_MS` (1–3000 ms; default 3000). The stock 3-second poll is too coarse to navigate the game's menus through an injected controller, so the native diagnostic route sets a finer interval.
- 0008 implements `ISslConnection::Peek` (OpenSSL backend), which MK8 Deluxe calls during the online room flow and which previously triggered an unimplemented-function assertion.
- 0009 turns off `SIO_UDP_CONNRESET` on Windows guest UDP sockets. Windows otherwise reports an ICMP port-unreachable from an earlier send as `WSAECONNRESET` on the next receive, which consoles and Linux ignore; MK8 Deluxe dropped its server connection on it.
- 0010 gives each MK8 native-diagnostic console its own loopback address (`USCL_MK8_NATIVE_LOCAL_ADDRESS=127.x.y.z`): guest sockets bound to 0.0.0.0, or unbound before connect/sendto, use it, so two local players send from different addresses like separate machines (Pia checks the source address).
- 0011 reports reserved or taken ports as `EADDRINUSE` instead of success. Windows refuses binds to ports it reserves for Hyper-V/WSL with `WSAEACCES`; MK8 Deluxe then advertised a port it never owned.
- 0012 maps connected controllers to players with `USCL_AUTO_PADS` set (the home screen sets it when Automatic Controller Setup is on): every SDL game controller, USB or Bluetooth, becomes players 1–8 in connection order, with Eden's own defaults per controller (button layout, Joy-Con pairs as one controller, motion). Wheels and flight sticks are skipped; with no controller the settings are left alone.

## Build

### Linux (development)

The patches apply in order with `git am patches/*.patch` on a checkout of the pinned base; the helpers below do that.
Run `tools/dev-scripts/build-switch-core.sh` from the USCL repository after installing Eden's native build dependencies, including Qt Widgets, Charts, Concurrent and Network, SDL2, FFmpeg, OpenSSL, and a C++20 toolchain. Dependency sources not supplied by the system are downloaded by Eden's pinned CMake configuration. `USCL_EDEN_SOURCE` can select an existing checkout; `USCL_BUILD_JOBS` sets compilation parallelism.

The helper installs a local development core under the ignored `build/switch-core` folder. It is not a portable distribution package. `switch-preview.sh` selects this graphical core automatically when present; the regular installed USCL applications remain separate.

For a manually configured development shell, set `USCL_SWITCH_CORE` to this build's `bin/eden` wrapper and `USCL_SWITCH_FRONTEND=qt`. Continue to use the matching `eden-room` server. The graphical settings application may use the same patched Eden build.

### Windows (development)

Install Visual Studio 2022 with **Desktop development with C++** (it supplies CMake and Ninja) and the [Vulkan SDK](https://vulkan.lunarg.com) (for `glslangValidator`), then run `tools\dev-scripts\build-switch-core.ps1` from PowerShell. Eden's MSVC configuration downloads its own Qt, SDL2, FFmpeg and OpenSSL. The script clones with `core.autocrlf=false` so the LF patches apply, and installs `eden.exe` with its DLLs to `build\switch-core\bin`. Windows Eden finds its user folder through the shell API, not `%APPDATA%`, so isolated instances use a portable `user` folder beside a copy of `eden.exe`.

For the two-player native Online Play test on one PC, run `tools\dev-scripts\switch-native-test.ps1 -Setup -Keys <prod.keys>` once, then `-Game <MK8 Deluxe file>`. It starts the room service, the adapter on 127.0.0.1:443 and two Eden instances with separate identities. Built and launched on Windows 10 with VS 2022 (MSVC 19.44) on 2026-11-11; the result is one static `eden.exe` needing only the VC++ runtime. The two-player game run is not yet done.

See [Switch setup](../docs/SWITCH.md) and [Mario Kart notes](../games/0100152000022000.md) for test results and remaining limits.
