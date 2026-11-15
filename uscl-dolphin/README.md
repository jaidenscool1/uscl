# uscl-dolphin: Wii and GameCube (Dolphin)

| | |
|---|---|
| **Consoles** | Wii, GameCube |
| **Emulator** | [Dolphin](https://github.com/dolphin-emu/dolphin) (GPL-2.0+) 2609a with our patch (`patches/`), shipped in the AppImage; without it USCL plays Wii games offline in the Flathub app `org.DolphinEmu.dolphin-emu` |
| **Our changes** | `patches/0001`: `USCL_NETPLAY=host\|join` hosts/joins NetPlay without the setup dialog, the host starts once `USCL_NETPLAY_PLAYERS` are in. USCL starts it (`uscl-shell` `Launcher::startEmu`, own profile `uscl/dolphin`), sets up its controllers (`uscl-shell/src/padconfig.cpp`) and carries NetPlay (`uscl-bridge` preset `dolphin`, UDP 2626) |
| **Online** | Dolphin NetPlay over the Steam tunnel |
| **Status** | Two PCs over Steam work (Linux AppImage host, Windows joiner): Wii Sports Tennis with motion, no desync in 4 minutes |

## How online works: NetPlay
Most Wii games, Wii Sports included, never had online play: multiplayer meant passing Wii Remotes around one couch.
There is no publisher server to revive, so instead of a stand-in server (Demonware, Blaze) USCL uses **Dolphin
NetPlay**. NetPlay runs the same game in lockstep on every player's PC and exchanges their **controller input** each
frame, so any local-multiplayer game becomes online. The input it syncs is the full Wii Remote report (buttons, IR
pointer, accelerometer, MotionPlus), so motion works over NetPlay too: it's only input.

USCL's part is the transport. The host's NetPlay server (ENet, **UDP 2626**) is forwarded over the Steam lobby by
`uscl-bridge host dolphin`; the joiner connects NetPlay to `127.0.0.1:<slot>`, which the tunnel maps to the host,
the same way as every PS3 game. NetPlay's direct-connection mode is exactly what the tunnel provides, so no
Nintendo servers are involved. NetPlay itself requires an identical game dump, identical Dolphin version and synced
settings on both ends. It's lockstep, so latency shows up as input delay, not rubber-banding.

## Controllers and motion
With Automatic Controller Setup on (the default), every connected pad, USB or Bluetooth, becomes Wii Remote 1, 2,
... and GameCube pad 1, 2, ... in the order it was connected. Buttons go by position. The Nunchuk starts unplugged
and **L3 plugs it in** (L3 again unplugs it), like reaching for the real one when a game asks: Wii Sports' Tennis,
Baseball, Bowling and Golf refuse to start with a Nunchuk attached, Boxing asks for it.

- **Pads with motion sensors** (PS5 DualSense, PS4 DualShock 4, Switch Pro): the pad's gyro and accelerometer *are*
  the Wii Remote's. Swing, tilt and point by moving it; R3 recenters the pointer. Dolphin reads them natively
  through SDL (no DSU server). Wii Sports is mostly accelerometer swings, so it maps naturally; tune sensitivity in
  Dolphin if needed.
- **Other pads:** the right stick points; **flick** it to swing the Wii Remote (a held stick only points), the left
  stick tilts it, R1 shakes it. Tested in Wii Sports (2026-11-14, virtual Xbox pad):
  - Tennis ✅ flick up to toss, flick again to serve/hit; flick left/right for backhand/forehand.
  - Bowling ✅ hold the left stick down (remote raised) and R2 (B), flick the right stick down then up, let go of R2.
  - Boxing ✅ L3 plugs in the Nunchuk; L1 = left punch (Nunchuk), flick up = right punch.
  - Golf ❌ not yet: left stick up points the remote down and left/right raises the club, but no stick motion hits
    the ball so far. Use a motion pad.
  - Baseball and Wii Sports Resort: not tested yet.
- **All pads:** L1 is also the Nunchuk punch (one pad has one set of sensors, so the Nunchuk's motion is a button);
  rumble goes to the pad; the Wii Remote speaker plays through the PC; MotionPlus is attached (Resort needs it).
- **Other motion sources** (set up in Dolphin yourself): a real Wii Remote by Bluetooth passthrough (most accurate,
  MotionPlus detected) or as a "Real Wii Remote", or a phone's gyro through DSU/cemuhook.

NetPlay sends inputs, it doesn't give you a remote: each player supplies their own motion source.

## Build
```
git clone https://github.com/dolphin-emu/dolphin && cd dolphin && git checkout 2609a && git submodule update --init
# git apply, not git am: NetPlay only lets in players whose Dolphin has the same git commit (NetPlayServer.cpp)
git apply <uscl>/uscl-dolphin/patches/*.patch
# for the AppImage: Sys/ next to the binary, no FFmpeg (frame dumps) so the bundle stays small
cmake -S . -B build-appimage -G Ninja -DCMAKE_BUILD_TYPE=Release -DLINUX_LOCAL_DEV=ON -DENCODE_FRAMEDUMPS=OFF \
  -DENABLE_ANALYTICS=OFF -DENABLE_AUTOUPDATE=OFF && ninja -C build-appimage
```
`packaging/build-linux.sh` picks it up from `USCL_DOLPHIN_BUILD` (default `~/src/dolphin-uscl/build-appimage`). For
a dev run, point `USCL_DOLPHIN` at `build-appimage/Binaries/dolphin-emu`. NetPlay checks the Dolphin commit
(`git rev-parse HEAD`), so every build must be 2609a with the patch applied uncommitted; the title then reads
`2609a-dirty`. An official 2609a build has the same commit and can join too. On Windows USCL looks for `Dolphin\Dolphin.exe` next
to `uscl.exe` (the installer doesn't build it yet).

Windows: `cmake --preset ninja-release-x64 -DENABLE_ANALYTICS=OFF -DENABLE_AUTOUPDATE=OFF` then
`cmake --build build\release\x64` in a VS developer prompt; the result is `build\release\x64\Binaries\` (copy the
whole folder: Qt DLLs, `Sys\`). 2609a wants **Visual Studio 2026** (`Source/PCH/pch.h` stops older compilers). Our
2026-11-14 test build used VS 2022 17.14 (MSVC 19.44) with that check skipped (`#if ... && !defined(USCL_TRY_VS2022)`,
`set CL=/DUSCL_TRY_VS2022`) and compiled and ran fine; release builds should use VS 2026.

## Status (2026-11-14)
- **Two PCs over Steam work.** This PC: the AppImage's home screen, Wii Sports → Host Online Session → Start, our
  bundled Dolphin as host. Second PC (Windows 11): v0.4.1-test's `uscl-bridge.exe join <session>` and our Dolphin
  built for Windows, joining `127.0.0.1:2626`. NetPlay: "Pete has joined", codes and saves synchronized, the host
  auto-started the game, ping 2-7 ms through the tunnel. Played Wii Sports Tennis (1P) for 4 minutes, 159 swings
  (shake) plus pointer moves from the host's pad: **no "Possible desync" message** (NetPlay compares both sides'
  emulated time base every 60 frames).
- The session ended later when the joiner's own Steam client reconnected to Steam (its `connection_log.txt`):
  Steam takes you out of the lobby then, and the joiner's bridge took that for "the host left". Fixed in
  `uscl-bridge` (a member that finds itself out of the lobby joins it again).
- Not tested: joining from the Windows home screen's Friends list. That needs the host to show as "in a USCL
  session" in Steam, and during the test the joiner's Steam showed the host as not playing / offline.
- The AppImage ships our Dolphin (`dolphin-emu` + `Sys/`), so Wii online works without a dev setup on Linux.
- 2026-11-12: two patched Dolphins hosted/joined and auto-started Wii Sports on one PC; the home screen hosted a
  Steam session and started Dolphin as host, a second copy joined on 127.0.0.1:2626.
- Dolphin 2609a boots `SP2E01` to the game-select menu (Vulkan). See [games/SP2E01.md](../games/SP2E01.md).
- USCL starts Wii games in Dolphin and writes the controller setup: tested with a DualSense Edge + Xbox 360 pad,
  which Dolphin binds as `SDL/0/DualSense Edge Wireless Controller` (Wii Remote 1) and `SDL/0/Xbox 360 Controller`
  (Wii Remote 2).
- `uscl-bridge` has the `dolphin` preset (UDP 2626).

## Next
1. Windows installer: build our Dolphin (CMake preset above, VS 2026) into it as `Dolphin\`.
2. Join from the Friends list on both OSes (Steam presence of the host's bridge, see Status).
3. A two-player match (both PCs with pads), and real motion pads (DualSense) instead of shake buttons.

## Run it by hand today
```
# host (any Dolphin 2609a; the patch only skips these dialogs)
dolphin-emu                                   # NetPlay → Host → the game, port 2626
uscl-bridge host dolphin                      # forwards 2626 over the Steam lobby; prints the session id
# joiner
uscl-bridge join <session id>                 # maps the host to 127.0.0.1:<slot>
dolphin-emu                                   # NetPlay → Connect → 127.0.0.1:<slot>
```

## Later: the Wii Sports games that had online play
- **Wii Sports Club** (Wii U, 2013) used Nintendo Network. Reviving it means a Quazal NEX stand-in (the same shape
  as our Demonware/Blaze work); Pretendo Network doesn't support its online play yet. A future Cemu + NEX layer.
- **Nintendo Switch Sports** (2022) runs on live Nintendo Switch Online, so there's nothing dead to replace. Its
  local wireless could go through Eden rooms like Mario Kart ([uscl-eden](../uscl-eden/)).
