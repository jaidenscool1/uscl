# uscl-dolphin: Wii and GameCube (Dolphin)

| | |
|---|---|
| **Consoles** | Wii, GameCube |
| **Emulator** | [Dolphin](https://github.com/dolphin-emu/dolphin) (GPL-2.0+), used **unchanged**: the Flathub app `org.DolphinEmu.dolphin-emu` (tested 2609a) |
| **Our changes** | none to Dolphin. USCL starts it (`uscl-shell` `Launcher::startEmu`), sets up its controllers (`uscl-shell/src/padconfig.cpp`) and carries NetPlay (`uscl-bridge` preset `dolphin`, UDP 2626) |
| **Online** | Dolphin NetPlay over the Steam tunnel |
| **Status** | Wii Sports (`SP2E01`) boots from USCL with two controllers; NetPlay over Steam is next |

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
... (with Nunchuk) and GameCube pad 1, 2, ... in the order it was connected. Buttons go by position.

- **Pads with motion sensors** (PS5 DualSense, PS4 DualShock 4, Switch Pro): the pad's gyro and accelerometer *are*
  the Wii Remote's. Swing, tilt and point by moving it; R3 recenters the pointer. Dolphin reads them natively
  through SDL (no DSU server). Wii Sports is mostly accelerometer swings, so it maps naturally; tune sensitivity in
  Dolphin if needed.
- **Other pads:** point with the right stick, shake with R1.
- **Other motion sources** (set up in Dolphin yourself): a real Wii Remote by Bluetooth passthrough (most accurate,
  MotionPlus detected) or as a "Real Wii Remote", or a phone's gyro through DSU/cemuhook.

NetPlay sends inputs, it doesn't give you a remote: each player supplies their own motion source.

## Status (2026-11-12)
- Dolphin 2609a boots `SP2E01` to the game-select menu (Vulkan). See [games/SP2E01.md](../games/SP2E01.md).
- USCL starts Wii games in Dolphin and writes the controller setup: tested with a DualSense Edge + Xbox 360 pad,
  which Dolphin binds as `SDL/0/DualSense Edge Wireless Controller` (Wii Remote 1) and `SDL/0/Xbox 360 Controller`
  (Wii Remote 2).
- `uscl-bridge` has the `dolphin` preset (UDP 2626).

## Next
1. One PC, two Dolphin copies: host NetPlay, join `127.0.0.1:2626`, confirm Wii Sports Tennis/Boxing stays in sync
   with Wii Remote motion exchanged (Wii NetPlay with motion can desync: measure it).
2. Host/join NetPlay from the home screen, like the PS3 games.
3. Two PCs over the Steam tunnel (`host dolphin` / `join <session>`).

## Run it by hand today
```
# host
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
