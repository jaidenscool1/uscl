# uscl-3ds: Nintendo 3DS (Azahar)

| | |
|---|---|
| **Console** | Nintendo 3DS |
| **Emulator** | [Azahar](https://github.com/azahar-emu/azahar) (GPL-2.0) 2126.2, the maintained Citra successor. Offline: the Flathub app `org.azahar_emu.Azahar` is enough. Online: our build (2126.2 + `patches/`, `tools/dev-scripts/build-azahar.sh`) |
| **Our changes** | `patches/0001` joins a room at startup (`USCL_ROOM`); `0002` makes that work for real players (valid nickname, no chat popup, a booted `.cci` counts as the inserted card so Download Play works); `0003` answers the Nintendo Network login with a server USCL picks (`USCL_NEX`); `0004` names the console after the player; `0005` follows the USCL loading palette for shell launches. USCL starts it (`uscl-shell` `Launcher::startEmu`), sets up its controller (`uscl-shell/src/padconfig.cpp`) and carries its rooms (`uscl-bridge` preset `3ds`, UDP 24872) |
| **Online** | Azahar rooms (local wireless / Download Play) over the Steam tunnel; Nintendo Network online later (Pretendo / NEX) |
| **Ships in** | both installers: `azahar` + `azahar-room` (Windows: `Azahar\`) |
| **Status** | 2026-11-14: **Mario Kart 7 local race between two Azahar copies in one room works** (one PC). Steam two-PC test next |

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

## Status (2026-11-14)
- **Rooms work.** One PC, our Azahar twice (two profiles), `azahar-room` on 24872, both joined with `USCL_ROOM`:
  Mario Kart 7 → Local Multiplayer → one creates a room, the other sees it, joins, the host starts, and a 2-player
  50cc Grand Prix race runs with each side seeing the other kart. Each copy drove from its own virtual pad.
- **Mario Kart 7's local rooms are Download Play** (`dlp:SRVR` on the host, `dlp:FKCL` scanning on the others, even
  when everyone has the card). Azahar only finds the DLP child (NCSD partition 2) on an *inserted* cartridge, and a
  `.cci` booted from the command line wasn't one, so the host advertised nothing and the others filtered it out
  ("did not match title id filter"). Fixed in `0002`.
- **Nicknames:** the room only takes 4-20 of `[ a-zA-Z0-9._-]` (`room.cpp IsValidNickname`); anything else is refused as
  "Username is already in use". `0002` maps `USCL_ROOM`'s name onto that (others → `_`, padded to 4).
- **Booting by path works** with our build (the old "opens to the game list" was the Flatpak).
- **Graphics on NVIDIA + Wayland:** our Azahar runs under XWayland (`QT_QPA_PLATFORM=xcb`, set by the launcher) with
  OpenGL: native Wayland Qt renders a black screen. Vulkan didn't work on this PC on 2026-11-14 because the driver
  refused new GPU channels (`NVRM ... NV_ERR_STATE_IN_USE` in the kernel log, also for the Flatpak and `vulkaninfo`
  while another GL client ran); after 4 days of uptime, so likely a reboot cures it. Same reason a second GL Azahar
  segfaulted in `libnvidia-glcore`; the second copy in the test ran on Mesa (`LIBGL_ALWAYS_SOFTWARE=1
  __GLX_VENDOR_LIBRARY_NAME=mesa`, about 65-100 % speed).
- **Games are decrypted:** the `.cci` dumps have the NCCH NoCrypto flag, so no 3DS AES keys or boot9 are needed.
- **Nintendo Network (MK7 Online Multiplayer):** stock Azahar stops at "Connecting to the Internet..." because
  `frd:u RequestGameAuthentication` (0x28) / `GetGameAuthenticationData` (0x29), the NASC login that hands the game
  its NEX server address and token, aren't implemented. **`0003` answers them locally** (no Nintendo, no HTTPS):

  | Variable | Meaning |
  |---|---|
  | `USCL_NEX=<host>:<port>` | NEX authentication server the game is sent to (unset: NASC 110 "server no longer available") |
  | `USCL_NEX_TOKEN` | NASC token handed to the game (default `uscl`) |
  | `USCL_NEX_PASSWORD` | `GetMyPassword`: the NEX/Kerberos password the game logs in with |
  | `USCL_NEX_PID` | principal id in `GetMyFriendKey` (Azahar's is 0) |

  Tested 2026-11-14: MK7 asks for **game server `0x00030600`**, takes `127.0.0.1:60000`, reads the password and sends
  **PRUDP v0 SYNs** there (`af a1 40 00 …`, ten tries over ~30 s), then logs out and returns to its menu. So the
  emulator side of the login works; what's missing is a NEX server answering.
- **NEX server:** Pretendo's open-source [mario-kart-7](https://github.com/PretendoNetwork/mario-kart-7) server
  (Go, nex-go; access key `6181dff1`, NEX 2.4.3) has the authentication + secure servers and matchmaking. It needs
  Postgres and Pretendo's account gRPC, which it only asks for NEX passwords; `nex/0001` (against upstream 1f00a0c)
  makes it return `USCL_NEX_PASSWORD` instead. Built locally (`~/src/pretendo-mk7`, Postgres 18 extracted to
  `~/.local/pg18`), but **not run yet**: starting it was held for the player's go-ahead. Its settings:
  `PN_MK7_POSTGRES_URI`, `PN_MK7_AUTHENTICATION_SERVER_PORT`, `PN_MK7_SECURE_SERVER_HOST`/`_PORT`,
  `PN_MK7_ACCOUNT_GRPC_HOST`/`_PORT` (any value with `USCL_NEX_PASSWORD` set; the client doesn't connect up front).
- **New Super Mario Bros. 2 co-op:** Luigi's copy finds Mario's (listed by console name, see `0004`), joins, both
  play the co-op intro and reach the World 1 map in sync, then "Wireless communication has been interrupted" at
  every level load. Each time the GPU copy had crashed in `libnvidia-glcore` at that moment (or stalled on shader
  compiles), so on this PC it's inconclusive; retest on a healthy GPU / two PCs.

## Next
1. Two PCs over Steam: `uscl-bridge host 3ds` / `join` (the preset is the Switch room's, already tested with Eden).
   Needs our Azahar on the other PC (Windows build of 2126.2 + patches).
2. A Mii: MK7 asks for one at every start and the race shows "Player"; Azahar can import a Mii or use the Mii
   Maker applet (needs system files).
3. Nintendo Network: run the MK7 NEX server against `USCL_NEX=127.0.0.1:<auth port>` and see how far MK7's login
   gets (Kerberos ticket, secure server, matchmaking); then a `3ds-nex` bridge preset (the auth + secure UDP ports)
   and unique `USCL_NEX_PID`s per player (e.g. from the Steam id).

## Run it by hand
```
tools/dev-scripts/build-azahar.sh       # -> build/azahar/{azahar,azahar-room}
# host
build/azahar/azahar-room --room-name "USCL 3DS" --port 24872 --max_members 8
uscl-bridge host 3ds                      # forwards 24872 over the Steam lobby
USCL_ROOM=<name>@127.0.0.1:24872 QT_QPA_PLATFORM=xcb build/azahar/azahar <game.cci>
# joiner
uscl-bridge join <session id>             # listens on 127.0.0.1:24872 for the host's room
USCL_ROOM=<name>@127.0.0.1:24872 QT_QPA_PLATFORM=xcb build/azahar/azahar <game.cci>
```
The home screen does all of this when the Azahar pair is next to `uscl` or set with `USCL_AZAHAR` /
`USCL_AZAHAR_ROOM`. Two copies on one PC: give each its own `XDG_CONFIG_HOME` / `XDG_DATA_HOME` (separate console
IDs and MACs), and pin each profile's buttons to one pad (`engine:sdl,guid:<guid>,port:<n>,...` instead of
`maptype:all`).

## Theme-aware loading UI

Patch 0005 applies the active shell palette only to the native loading widget and its progress bar.
Standalone launches keep upstream styling; compilation, game artwork, progress and fade completion are
unchanged. Rebuild Azahar for this support. See [coverage and verification](../docs/LOADING-THEMES.md).

## USCL release integration

The v0.5.2-alpha release line integrates the reviewed installer/library work and
console themes. Use the [release page](https://github.com/jaidenscool1/uscl/releases/tag/v0.5.2-alpha)
for published assets and the [fork matrix](../README.md#fork-maintenance) for build pins.
This does not update the upstream base or establish new gameplay/online support
for this fork. Patch changes require rebuilding the corresponding core; updating
the shell alone does not update a separately installed emulator.

Switch 2 CPU/GPU translation, game evidence and pending work are tracked in
[SWITCH2-PROGRESS.md](../docs/SWITCH2-PROGRESS.md).
