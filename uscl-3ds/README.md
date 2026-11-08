# uscl-3ds

USCL's **Nintendo 3DS** platform, via **Azahar** (the maintained Citra successor; Citra itself was taken down in
2024). The 3DS counterpart to `uscl-dolphin` (Wii) and `uscl-rpcs3` (PS3). Like Dolphin, it needs **no emulator
fork** — Azahar's built-in multiplayer is the online layer and USCL just tunnels it over Steam.

## How 3DS online works here
Two separate backends, like the rest of USCL:

1. **Local multiplayer / Download Play → Azahar "rooms" (ready now).** Azahar's multiplayer runs each player as a
   unique emulated 3DS talking through a **room server** anyone can host (`azahar-room`, default **TCP 24872**). It
   carries the game's *local-wireless* session, so any game with local/Download-Play multiplayer works: **Mario
   Kart 7** local races, **Luigi's Mansion: Dark Moon** co-op (ScareScraper), **Pokémon** local trades/battles,
   **NSMB2** co-op. USCL forwards 24872 over the Steam lobby with `uscl-bridge host 3ds` — same mechanism as the PS3
   games and Dolphin NetPlay. Requirements Azahar imposes: identical game + identical Azahar version, cheats off
   (they desync a room).

2. **Nintendo Network *online* (worldwide) → Pretendo/NEX (future).** Features that used Nintendo's own matchmaking
   servers — MK7 **worldwide** races, Pokémon **GTS / Wonder Trade / Battle Spot / Festival Plaza** — went dark when
   Nintendo shut 3DS online in **April 2024**. Reviving those means a **Pretendo Network (Quazal NEX)** stand-in,
   the same shape as our Demonware/Blaze work but for Nintendo. Pretendo already revives some 3DS titles (MK7 online
   reportedly works); it's a future `uscl-pretendo`/NEX layer, not wired into USCL yet.

## The library (what's installed, and its online path)
From `~/Downloads/organized game dumps/3DS/*.cci` (profiled 2026-11-07):

| Game | Multiplayer | USCL path |
|---|---|---|
| Mario Kart 7 | local races + **worldwide online** | rooms now (local); Pretendo for worldwide |
| Luigi's Mansion: Dark Moon | **co-op** (ScareScraper), local + online | rooms now (local co-op) |
| Pokémon X / Y | trade/battle (local + GTS online) | rooms now (local); Pretendo for GTS |
| Pokémon Omega Ruby | trade/battle (local + online) | rooms now (local); Pretendo for GTS |
| Pokémon Ultra Sun / Ultra Moon | trade/battle + Festival Plaza | rooms now (local); Pretendo for online |
| New Super Mario Bros. 2 | **co-op** (local) + Coin Rush online | rooms now (local co-op) |
| Super Mario 3D Land | — single-player | runs on USCL, no online |
| Zelda: Ocarina of Time 3D | — single-player | runs on USCL, no online |
| Zelda: Majora's Mask 3D | — single-player | runs on USCL, no online |
| Zelda: A Link Between Worlds | StreetPass only (Shadow Link) | runs on USCL, no real online MP |

## Motion / input
3DS has no motion gameplay to speak of (it has a gyro, little-used); a **DualSense** or any pad maps directly to the
3DS buttons + circle pad + touch (right stick → stylus). No special motion setup, unlike Wii.

## Status (2026-11-07)
- Azahar **2126.2** installed (Flatpak `org.azahar_emu.Azahar`); `azahar-room` ships for hosting.
- `uscl-bridge` has a `3ds` forward preset (`tcp:24872`).
- **Open blocker on this machine:** Azahar's Qt frontend requires an OpenGL 4.3 context and throws *"OpenGL not
  available!"* under the flatpak on this NVIDIA (driver 615.78.08) + Wayland setup, even with the matching
  `GL.nvidia-615-78-08` extension installed and the game renderer set to Vulkan. Tried xcb and Zink; not yet solved.
  Next: the Azahar **AppImage** (uses host GL/Vulkan directly, bypassing the flatpak GL sandbox) is the likely fix,
  or a host OpenGL ICD fix. Boot-verification of the games is pending that.

## Next
1. Get Azahar rendering (AppImage or fix flatpak GL), boot each `.cci` to confirm it runs.
2. Two Azahar instances on one PC: host an `azahar-room` on 24872, join `127.0.0.1:24872`, confirm a local-MP game
   (MK7 or Luigi's Mansion co-op) syncs — then two PCs over the Steam tunnel (`host 3ds` / `join`).
3. Wire the room host/join into `uscl-shell`.
4. Pretendo/NEX stand-in for the worldwide-online features (separate, larger effort).

## Run it by hand (once rendering works)
```
# host
azahar-room --port 24872 ...          # or host a room in the GUI (Multiplayer -> Host Room)
uscl-bridge host 3ds                    # forwards 24872 over the Steam lobby
# joiner
uscl-bridge join <session id>           # maps the host to 127.0.0.1:<slot>
# Azahar -> Multiplayer -> Connect -> 127.0.0.1:<slot>
```
