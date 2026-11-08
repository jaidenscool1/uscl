# uscl-dolphin

USCL's **Wii / GameCube** platform, the Dolphin counterpart to what `uscl-rpcs3` is for PS3 and
`uscl-shadps4` is for PS4. Unlike those, it needs **no emulator fork**: Dolphin's built-in **NetPlay** already
does everything the online layer needs, and USCL just carries its traffic over the Steam tunnel.

## The idea: online for games that never had it

The owner's installed title is **Wii Sports + Wii Sports Resort** (`SP2E01`, the 2010 dual-pack). Like almost every
early Wii game, it has **no online mode at all** — multiplayer was two-to-four people passing Wii Remotes on one
couch. There is no publisher server to revive (no Demonware/Blaze/Nintendo-Network equivalent), so the Demonware /
Blaze stand-in approach doesn't apply here.

Instead we use **Dolphin NetPlay**. NetPlay runs the same game in lockstep on every player's PC and exchanges their
**controller input** each frame — so *any* local-multiplayer game becomes online, Wii Sports included. The input
stream it syncs is the **full Wii Remote report**: buttons, IR pointer, accelerometer, and **MotionPlus** gyro. So
the motion you do on your own remote is sent to the other player's copy as input — motion "just works" over NetPlay,
because it is only input, not a special online mode the game has to support.

USCL's job is just the transport: Dolphin's NetPlay host listens on **TCP 2626**, and `uscl-bridge host dolphin`
forwards that one port over the Steam lobby (preset added in `uscl-bridge/src/tunnel.rs`). The joiner connects
Dolphin NetPlay to `127.0.0.1:<slot>`, which the tunnel maps to the host — the same mechanism as every PS3 game.

### Is Steam-tunnel online possible? Yes.
NetPlay's **direct-connection** mode is exactly what the tunnel provides (the host looks local to the joiner), so no
Nintendo servers and nothing to reverse. Requirements NetPlay imposes, not us: identical game dump, identical Dolphin
version, and synced settings on both ends (NetPlay enforces most of this). It is deterministic lockstep, so latency
shows up as input delay, not rubber-banding.

## Motion input (each player supplies their own)

NetPlay sends inputs; it does not give you a remote. Each player needs a motion source feeding their Dolphin. Any of
these works — whatever a player emits as the emulated Wii Remote is what NetPlay syncs:
- **PS5 DualSense (or PS4 DualShock 4) — the easy, recommended option.** It has a 3-axis gyro + accelerometer, the
  same sensors as a Wii Remote. Dolphin 2609a reads its IMU **natively** (USB or Bluetooth, via the SDL backend — no
  DSU server needed): emulated Wii Remote → *Motion Input* / *Motion Simulation* bound to the pad's gyro/accel, with
  the IR pointer on the gyro or right stick. Wii Sports (2006) is mostly accelerometer swings, so it maps naturally;
  expect to tune sensitivity/calibration for the right feel. A Switch Pro/Joy-Con IMU works the same way.
- **Real Wii Remote over Bluetooth passthrough** — the most accurate; Dolphin drives the PC Bluetooth adapter as the
  Wii's and MotionPlus is detected. (Dolphin → Controllers → Wii Remotes → *Passthrough*.)
- **Real Wii Remote as a plain Bluetooth controller** ("Real Wii Remote" source, continuous scan, press sync).
- **Phone gyro via DSU/cemuhook** — the fallback for a player with none of the above.

(Note: USCL's automated test harness can't exercise this — its virtual pads are synthetic button/stick gamepads with
no IMU — so the motion test is a real-hardware step, not something the harness drives.)

## The "newer, online" Wii Sports — the alternatives, and why not yet

- **Wii Sports Club** (Wii U, 2013) is the one that *did* have real online, over Nintendo Network. Reviving that
  means a **Quazal NEX / Rendez-vous** stand-in — the same shape as our Demonware/Blaze work but for Nintendo. The
  open **Pretendo Network** reimplements NN, but it **does not support Wii Sports Club online yet** (it appears only
  in Pretendo's Miiverse/"Juxtaposition" list, not the online-matchmaking game-support table). So this is a real
  future target (a `uscl-cemu` + Pretendo/NEX layer), not a ready path.
- **Nintendo Switch Sports** (2022) runs on **live** Nintendo Switch Online, so there's nothing dead to replace.
  Switch emulators (Ryujinx forks) don't emulate NSO; they offer **LDN "local wireless"**, which *could* be tunnelled
  the same way as NetPlay — but that's a separate, heavier platform (Switch emulation + a `uscl-ryujinx` LDN relay)
  and a much bigger lift than the Wii game already installed.

**Recommendation:** ship **Wii Sports (Dolphin NetPlay over the Steam tunnel)** first — it's the installed game, needs
no server work, and carries motion. Keep Wii Sports Club (Wii U / Pretendo-NEX) and Switch Sports (Ryujinx / LDN) as
later platforms once the Dolphin path is proven across two PCs.

## Status (2026-11-07)
- Dolphin **2609a** (Flatpak `org.DolphinEmu.dolphin-emu`) **boots `SP2E01`** to the game-select menu, Vulkan, with an
  emulated Wii Remote pointer. Platform confirmed on this machine. See `games/SP2E01.md`.
- `uscl-bridge` gained the `dolphin` forward preset (`tcp:2626`) + a parse test. Nothing else is needed in the bridge.

## Next
1. One PC, two Dolphin instances: host NetPlay on 2626, join `127.0.0.1:2626`, confirm Wii Sports Tennis/Boxing runs
   in sync with **Wii Remote motion** exchanged (the known risk: Wii NetPlay with motion can desync — measure it).
2. Wire it into `uscl-shell` / `uscl-bridge` so "host"/"join" starts Dolphin NetPlay like the PS3 games launch RPCS3.
3. Two-PC test over the Steam tunnel (`host dolphin` / `join <session>`), the same bar as the PS3 titles.

## Run it by hand today
```
# host
dolphin-emu                                   # NetPlay → Host → game SP2E01, port 2626, Connect
uscl-bridge host dolphin                       # forwards 2626 over the Steam lobby; prints the session id
# joiner
uscl-bridge join <session id>                  # maps the host to 127.0.0.1:<slot>
dolphin-emu                                    # NetPlay → Connect → 127.0.0.1:<slot>
```
