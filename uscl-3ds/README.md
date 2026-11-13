# uscl-3ds: Nintendo 3DS (Azahar)

| | |
|---|---|
| **Console** | Nintendo 3DS |
| **Emulator** | [Azahar](https://github.com/azahar-emu/azahar) (GPL-2.0), the maintained Citra successor, used **unchanged**: the Flathub app `org.azahar_emu.Azahar` (tested 2126.2) |
| **Our changes** | none to Azahar. USCL starts it (`uscl-shell` `Launcher::startEmu`), sets up its controller (`uscl-shell/src/padconfig.cpp`) and carries its rooms (`uscl-bridge` preset `3ds`, UDP 24872) |
| **Online** | Azahar rooms (local wireless / Download Play) over the Steam tunnel; Nintendo Network online later (Pretendo / NEX) |
| **Status** | games run in Azahar; rooms over Steam are next |

## How online works
Two backends, like the rest of USCL:

1. **Local multiplayer and Download Play → Azahar rooms (ready to try).** Each player is an emulated 3DS talking
   through a **room server** anyone can host (`azahar-room`, ENet, **UDP 24872**). It carries the game's
   local-wireless session, so any game with local or Download Play multiplayer works: Mario Kart 7 local races,
   Luigi's Mansion: Dark Moon co-op, Pokémon local trades and battles, NSMB2 co-op. `uscl-bridge host 3ds` forwards
   the room over the Steam lobby. Azahar requires the same game and Azahar version for everyone, and cheats off.
2. **Nintendo Network online → Pretendo / NEX (later).** Nintendo's matchmaking (MK7 worldwide, Pokémon GTS, Wonder
   Trade, Battle Spot, Festival Plaza) went dark in April 2024. Reviving it means a Pretendo Network (Quazal NEX)
   stand-in, the same shape as our Demonware/Blaze work. Not wired into USCL yet.

## Controllers
The 3DS is one player per console, so player 1's console takes **any connected controller** (USB or Bluetooth):
USCL writes Azahar's active profile with "all controllers" bindings when a 3DS game starts. Buttons go by position;
for Switch Pro / Joy-Con pads USCL uses Azahar's Nintendo layout table so the labels match too. The circle pad and
C-stick are the two sticks. 3DS motion stays on Azahar's default (mouse); few games use it. In a room, each player
uses their own PC's controller.

## Games (2026-11-07)
| Game | Multiplayer | USCL path |
|---|---|---|
| Mario Kart 7 | local races + worldwide online | rooms (local); Pretendo for worldwide |
| Luigi's Mansion: Dark Moon | co-op (ScareScraper), local + online | rooms (local co-op) |
| Pokémon X / Y | trade/battle (local + GTS) | rooms (local); Pretendo for GTS |
| Pokémon Omega Ruby | trade/battle (local + online) | rooms (local); Pretendo for GTS |
| Pokémon Ultra Sun / Ultra Moon | trade/battle + Festival Plaza | rooms (local); Pretendo for online |
| New Super Mario Bros. 2 | co-op (local) + Coin Rush online | rooms (local co-op) |
| Super Mario 3D Land | single-player | runs, no online |
| Zelda: Ocarina of Time 3D | single-player | runs, no online |
| Zelda: Majora's Mask 3D | single-player | runs, no online |
| Zelda: A Link Between Worlds | StreetPass only | runs, no online multiplayer |

## Status (2026-11-12)
- Azahar 2126.2 installed; `azahar-room` ships with it for hosting; the GUI has Multiplayer → Host/Connect Room.
- **Graphics on NVIDIA + Wayland:** the Flatpak's Qt frontend can't get an OpenGL context ("OpenGL not available!"),
  even with the matching NVIDIA GL extension. USCL starts it with `LIBGL_ALWAYS_SOFTWARE=1`: the frontend uses Mesa's
  software GL while the game renders on Vulkan (`graphics_api=2`).
- **Games are decrypted:** the `.cci` dumps have the NCCH NoCrypto flag, so no 3DS AES keys or boot9 are needed.
- **Booting:** Azahar opens from USCL, but a game passed by path doesn't start by itself yet (it opens to the game
  list); pick the game there.
- `uscl-bridge` has the `3ds` preset (UDP 24872).

## Next
1. Boot a game by path from USCL (or work around the Flatpak argument quirk).
2. Two Azahar copies on one PC: host a room on 24872, join `127.0.0.1:24872`, confirm a local-multiplayer game (MK7
   or Luigi's Mansion co-op) syncs; then two PCs over Steam (`host 3ds` / `join`).
3. Host/join rooms from the home screen.
4. A Pretendo / NEX stand-in for the Nintendo Network features (separate, larger effort).

## Run it by hand
```
# host
azahar-room --port 24872 ...          # or Multiplayer → Host Room in the GUI
uscl-bridge host 3ds                    # forwards 24872 over the Steam lobby
# joiner
uscl-bridge join <session id>           # maps the host to 127.0.0.1:<slot>
# Azahar → Multiplayer → Connect → 127.0.0.1:<slot>
```
