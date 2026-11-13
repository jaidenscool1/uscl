# USCL: Ultimate Steam Compatibility Layer

**Play your console games online with your Steam friends.** USCL is one console-style app for your PC: a
controller-first home screen, emulators set up for you, and small servers that stand in for the consoles' old
online services. You and your friends connect through Steam, so nobody has to open ports.

PS3 came first. USCL now also runs PS4, Switch, Wii, GameCube, 3DS, Wii U and Xbox 360 games, at different stages
(see [Platforms](#platforms)). The goal is every console, all from the same home screen.

> **Status: early testing.** Several games already play online in tests; playing between two PCs over Steam is
> being tested now. Expect bugs.

**Contents:** [What you get](#what-you-get) · [Platforms](#platforms) · [Get it](#get-it) ·
[Controllers](#controllers) · [Games](#games) · [How online works](#how-online-works) ·
[Repository map](#repository-map) · [Branches](#branches) · [Safety](#safety) · [Credits](#credits)

## What you get
- 🎮 **One home screen for every console.** Controller first: games, friends, settings, all from the couch.
- ⚙️ **Sets itself up.** Uses your Steam name, installs the PS3 system software for you, finds your games, sets up
  every connected controller and picks the sharpest picture your PC can handle.
- 🌐 **Online with Steam friends.** Host a session for a game and your friends join from their Friends list.
  Test Connection checks that everything is ready.
- 🛡️ **Keeps cheaters out.** Hosts can kick or ban players by Steam account, games in a session run without cheat
  patches, and everyone's game files must match the host's.
- 🔄 **Updates itself.** When a new version is out, USCL asks before installing it. Every update is signed, and
  USCL refuses any file that isn't.

## Platforms
| Console | Emulator | Online through | Where it is |
|---|---|---|---|
| **PlayStation 3** | RPCS3, our build ([`uscl-rpcs3`](uscl-rpcs3/)) | stand-in servers: [RPCN](uscl-rpcn/) (PSN), [Demonware](uscl-dw/) (Call of Duty), [Blaze](uscl-blaze/) (EA) | 🟢 main platform: Black Ops II, MW2, Minecraft online in tests |
| **PlayStation 4** | shadPS4, our build ([`uscl-shadps4`](uscl-shadps4/)) | [shadNet](uscl-shadnet/) + our Demonware | 🟡 Black Ops II played between two PCs; PS3 ↔ PS4 crossplay in progress |
| **Nintendo Switch** | Eden, our build ([`uscl-eden`](uscl-eden/)) | Eden local-wireless rooms over Steam | 🟡 Mario Kart 8 Deluxe races two copies on one PC ([setup](docs/SWITCH.md)) |
| **Wii / GameCube** | Dolphin ([`uscl-dolphin`](uscl-dolphin/), no fork) | Dolphin NetPlay over Steam | 🟠 games launch from USCL; NetPlay over Steam next |
| **Nintendo 3DS** | Azahar ([`uscl-3ds`](uscl-3ds/), no fork) | Azahar rooms over Steam | 🟠 Azahar opens from USCL (the game is picked in Azahar for now); rooms over Steam next |
| **Wii U** | Cemu (no fork yet) | later: a Pretendo / NEX stand-in | ⚪ starts Cemu from USCL (untested) |
| **Xbox 360** | Xenia (Wine on Linux) | later | ⚪ experimental launch |
| **More** | | | planned: see [PLAN.md](PLAN.md) |

🟢 works online in tests · 🟡 online works on one PC or between two in early tests · 🟠 runs, online not yet ·
⚪ runs, online not started.

Where we change an emulator, we keep our changes as patch files against a pinned upstream commit (the `uscl-*`
folders). Dolphin, Azahar and Cemu are used unchanged: on Linux USCL starts their Flathub apps, so install those
yourself for now.

## Get it
Download the newest version from **[Releases](https://github.com/jaidenscool1/uscl/releases)**.

| PC | File | How |
|---|---|---|
| Windows 10/11 | `USCL-Setup.exe` | Run it. No admin rights needed. USCL goes in the Start menu. |
| Linux | `USCL-x86_64.AppImage` | Make it executable and run it. To add it to your app menu: [`packaging/linux/install.sh`](packaging/linux/install.sh) `USCL-x86_64.AppImage` |

You'll need **Steam running and signed in**, and **your own copies of your games** (disc folders, ISOs, installed
games, or the console's game files). USCL doesn't include any games, console keys or firmware. On first start it
walks you through the rest.

The installers include everything PS3 needs. For other consoles: PS4 is in the Linux AppImage when it is built with
shadPS4 (not in the Windows installer yet);
Switch needs the patched Eden core ([docs/SWITCH.md](docs/SWITCH.md)) and your own console keys; Wii, GameCube, 3DS
and Wii U use Dolphin, Azahar and Cemu from Flathub (Linux).

## Controllers
Any controller works, USB or Bluetooth: DualSense, DualShock, Xbox, Switch Pro, Joy-Cons, and other pads SDL
knows. When a game starts, **every connected controller becomes player 1, 2, ... in the order you connected them**,
in every emulator (Settings → Automatic Controller Setup, on by default).

- Buttons go **by position**: the console's confirm button is always the same physical button, whatever the
  labels on your pad say.
- **Motion:** a pad with a gyro (DualSense, DualShock 4, Switch Pro) is the Wii Remote's motion and pointer in
  Wii games (R3 recenters the pointer), and the Switch / Wii U motion controls.
- Home screen: ✕/A select · ○/B back · △/Y options · hold PS/Guide to quit a game.

How each emulator is set up: [`uscl-shell`](uscl-shell/README.md#automatic-setup-on-every-pc).

## Games
The full tracker, with what's blocking each game, is [GAMES.md](GAMES.md). Notes per game are in [`games/`](games/).

### PlayStation 3
Both status columns update live: **USCL online** from this repo's [`status`](https://github.com/jaidenscool1/uscl/tree/status)
branch, **Runs in RPCS3** from [RPCS3's compatibility list](https://rpcs3.net/compatibility) (how well it plays
offline). Click a badge for details.

| Game | Serial | Online through | USCL online | Runs in RPCS3 |
|---|---|---|---|---|
| Call of Duty: Black Ops II | `BLUS31011` | Demonware (our own server) | [![USCL](https://img.shields.io/endpoint?url=https%3A%2F%2Fraw.githubusercontent.com%2Fjaidenscool1%2Fuscl%2Fstatus%2Fbadges%2FBLUS31011.json)](games/BLUS31011.md) | [![RPCS3](https://img.shields.io/badge/dynamic/json?url=https%3A%2F%2Frpcs3.net%2Fcompatibility%3Fapi%3Dv1%26g%3DBLUS31011&query=%24.results.BLUS31011.status&label=RPCS3&color=blue)](https://rpcs3.net/compatibility?g=BLUS31011) |
| Minecraft: PlayStation 3 Edition | `BLUS31426` | PSN rooms (RPCN) | [![USCL](https://img.shields.io/endpoint?url=https%3A%2F%2Fraw.githubusercontent.com%2Fjaidenscool1%2Fuscl%2Fstatus%2Fbadges%2FBLUS31426.json)](games/BLUS31426.md) | [![RPCS3](https://img.shields.io/badge/dynamic/json?url=https%3A%2F%2Frpcs3.net%2Fcompatibility%3Fapi%3Dv1%26g%3DBLUS31426&query=%24.results.BLUS31426.status&label=RPCS3&color=blue)](https://rpcs3.net/compatibility?g=BLUS31426) |
| Call of Duty: Modern Warfare 2 | `BLUS30377` | Demonware | [![USCL](https://img.shields.io/endpoint?url=https%3A%2F%2Fraw.githubusercontent.com%2Fjaidenscool1%2Fuscl%2Fstatus%2Fbadges%2FBLUS30377.json)](games/BLUS30377.md) | [![RPCS3](https://img.shields.io/badge/dynamic/json?url=https%3A%2F%2Frpcs3.net%2Fcompatibility%3Fapi%3Dv1%26g%3DBLUS30377&query=%24.results.BLUS30377.status&label=RPCS3&color=blue)](https://rpcs3.net/compatibility?g=BLUS30377) |
| Skate 3 | `BLUS30464` | EA Blaze + PSN invites | [![USCL](https://img.shields.io/endpoint?url=https%3A%2F%2Fraw.githubusercontent.com%2Fjaidenscool1%2Fuscl%2Fstatus%2Fbadges%2FBLUS30464.json)](games/BLUS30464.md) | [![RPCS3](https://img.shields.io/badge/dynamic/json?url=https%3A%2F%2Frpcs3.net%2Fcompatibility%3Fapi%3Dv1%26g%3DBLUS30464&query=%24.results.BLUS30464.status&label=RPCS3&color=blue)](https://rpcs3.net/compatibility?g=BLUS30464) |
| Midnight Club: Los Angeles | `BLUS30442` | Rockstar's own + PSN | [![USCL](https://img.shields.io/endpoint?url=https%3A%2F%2Fraw.githubusercontent.com%2Fjaidenscool1%2Fuscl%2Fstatus%2Fbadges%2FBLUS30442.json)](games/BLUS30442.md) | [![RPCS3](https://img.shields.io/badge/dynamic/json?url=https%3A%2F%2Frpcs3.net%2Fcompatibility%3Fapi%3Dv1%26g%3DBLUS30442&query=%24.results.BLUS30442.status&label=RPCS3&color=blue)](https://rpcs3.net/compatibility?g=BLUS30442) |
| Spider-Man: Shattered Dimensions | `BLUS30499` | single-player | [![USCL](https://img.shields.io/endpoint?url=https%3A%2F%2Fraw.githubusercontent.com%2Fjaidenscool1%2Fuscl%2Fstatus%2Fbadges%2FBLUS30499.json)](GAMES.md) | [![RPCS3](https://img.shields.io/badge/dynamic/json?url=https%3A%2F%2Frpcs3.net%2Fcompatibility%3Fapi%3Dv1%26g%3DBLUS30499&query=%24.results.BLUS30499.status&label=RPCS3&color=blue)](https://rpcs3.net/compatibility?g=BLUS30499) |
| Spider-Man: Edge of Time | `BLUS30760` | single-player | [![USCL](https://img.shields.io/endpoint?url=https%3A%2F%2Fraw.githubusercontent.com%2Fjaidenscool1%2Fuscl%2Fstatus%2Fbadges%2FBLUS30760.json)](GAMES.md) | [![RPCS3](https://img.shields.io/badge/dynamic/json?url=https%3A%2F%2Frpcs3.net%2Fcompatibility%3Fapi%3Dv1%26g%3DBLUS30760&query=%24.results.BLUS30760.status&label=RPCS3&color=blue)](https://rpcs3.net/compatibility?g=BLUS30760) |

<details><summary><b>Next up on PS3</b> (EA Sports, more Call of Duty, Battlefield, LittleBigPlanet, Killzone)</summary>

| Game | Serial | Online through | USCL online | Runs in RPCS3 |
|---|---|---|---|---|
| NHL 15 | `BLUS31436` | EA Blaze 3 | logs in to our server ([notes](games/BLUS31436.md)) | [![RPCS3](https://img.shields.io/badge/dynamic/json?url=https%3A%2F%2Frpcs3.net%2Fcompatibility%3Fapi%3Dv1%26g%3DBLUS31436&query=%24.results.BLUS31436.status&label=RPCS3&color=blue)](https://rpcs3.net/compatibility?g=BLUS31436) |
| Call of Duty: Black Ops | `BLUS30591` | Demonware | [![USCL](https://img.shields.io/endpoint?url=https%3A%2F%2Fraw.githubusercontent.com%2Fjaidenscool1%2Fuscl%2Fstatus%2Fbadges%2FBLUS30591.json)](games/BLUS30591.md) | [![RPCS3](https://img.shields.io/badge/dynamic/json?url=https%3A%2F%2Frpcs3.net%2Fcompatibility%3Fapi%3Dv1%26g%3DBLUS30591&query=%24.results.BLUS30591.status&label=RPCS3&color=blue)](https://rpcs3.net/compatibility?g=BLUS30591) |
| Call of Duty: Modern Warfare 3 | `BLUS30838` | Demonware | [![USCL](https://img.shields.io/endpoint?url=https%3A%2F%2Fraw.githubusercontent.com%2Fjaidenscool1%2Fuscl%2Fstatus%2Fbadges%2FBLUS30838.json)](GAMES.md) | [![RPCS3](https://img.shields.io/badge/dynamic/json?url=https%3A%2F%2Frpcs3.net%2Fcompatibility%3Fapi%3Dv1%26g%3DBLUS30838&query=%24.results.BLUS30838.status&label=RPCS3&color=blue)](https://rpcs3.net/compatibility?g=BLUS30838) |
| Call of Duty: Ghosts | `BLUS31270` | Demonware | [![USCL](https://img.shields.io/endpoint?url=https%3A%2F%2Fraw.githubusercontent.com%2Fjaidenscool1%2Fuscl%2Fstatus%2Fbadges%2FBLUS31270.json)](GAMES.md) | [![RPCS3](https://img.shields.io/badge/dynamic/json?url=https%3A%2F%2Frpcs3.net%2Fcompatibility%3Fapi%3Dv1%26g%3DBLUS31270&query=%24.results.BLUS31270.status&label=RPCS3&color=blue)](https://rpcs3.net/compatibility?g=BLUS31270) |
| Call of Duty 4: Modern Warfare | `BLUS30072` | Demonware | [![USCL](https://img.shields.io/endpoint?url=https%3A%2F%2Fraw.githubusercontent.com%2Fjaidenscool1%2Fuscl%2Fstatus%2Fbadges%2FBLUS30072.json)](GAMES.md) | [![RPCS3](https://img.shields.io/badge/dynamic/json?url=https%3A%2F%2Frpcs3.net%2Fcompatibility%3Fapi%3Dv1%26g%3DBLUS30072&query=%24.results.BLUS30072.status&label=RPCS3&color=blue)](https://rpcs3.net/compatibility?g=BLUS30072) |
| Battlefield 3 | `BLUS30762` | EA Blaze | [![USCL](https://img.shields.io/endpoint?url=https%3A%2F%2Fraw.githubusercontent.com%2Fjaidenscool1%2Fuscl%2Fstatus%2Fbadges%2FBLUS30762.json)](GAMES.md) | [![RPCS3](https://img.shields.io/badge/dynamic/json?url=https%3A%2F%2Frpcs3.net%2Fcompatibility%3Fapi%3Dv1%26g%3DBLUS30762&query=%24.results.BLUS30762.status&label=RPCS3&color=blue)](https://rpcs3.net/compatibility?g=BLUS30762) |
| Battlefield: Bad Company 2 | `BLUS30458` | EA Blaze | [![USCL](https://img.shields.io/endpoint?url=https%3A%2F%2Fraw.githubusercontent.com%2Fjaidenscool1%2Fuscl%2Fstatus%2Fbadges%2FBLUS30458.json)](GAMES.md) | [![RPCS3](https://img.shields.io/badge/dynamic/json?url=https%3A%2F%2Frpcs3.net%2Fcompatibility%3Fapi%3Dv1%26g%3DBLUS30458&query=%24.results.BLUS30458.status&label=RPCS3&color=blue)](https://rpcs3.net/compatibility?g=BLUS30458) |
| LittleBigPlanet (Game of the Year) | `BCUS98208` | PSN | [![USCL](https://img.shields.io/endpoint?url=https%3A%2F%2Fraw.githubusercontent.com%2Fjaidenscool1%2Fuscl%2Fstatus%2Fbadges%2FBCUS98208.json)](GAMES.md) | [![RPCS3](https://img.shields.io/badge/dynamic/json?url=https%3A%2F%2Frpcs3.net%2Fcompatibility%3Fapi%3Dv1%26g%3DBCUS98208&query=%24.results.BCUS98208.status&label=RPCS3&color=blue)](https://rpcs3.net/compatibility?g=BCUS98208) |
| LittleBigPlanet 2 | `BCUS98245` | PSN | [![USCL](https://img.shields.io/endpoint?url=https%3A%2F%2Fraw.githubusercontent.com%2Fjaidenscool1%2Fuscl%2Fstatus%2Fbadges%2FBCUS98245.json)](GAMES.md) | [![RPCS3](https://img.shields.io/badge/dynamic/json?url=https%3A%2F%2Frpcs3.net%2Fcompatibility%3Fapi%3Dv1%26g%3DBCUS98245&query=%24.results.BCUS98245.status&label=RPCS3&color=blue)](https://rpcs3.net/compatibility?g=BCUS98245) |
| Killzone 2 | `BCUS98116` | PSN | [![USCL](https://img.shields.io/endpoint?url=https%3A%2F%2Fraw.githubusercontent.com%2Fjaidenscool1%2Fuscl%2Fstatus%2Fbadges%2FBCUS98116.json)](GAMES.md) | [![RPCS3](https://img.shields.io/badge/dynamic/json?url=https%3A%2F%2Frpcs3.net%2Fcompatibility%3Fapi%3Dv1%26g%3DBCUS98116&query=%24.results.BCUS98116.status&label=RPCS3&color=blue)](https://rpcs3.net/compatibility?g=BCUS98116) |
</details>

**USCL online:** 🟢 works online between PCs · 🟡 testing (works in a local test) · 🟠 in progress ·
⚪ next up / not started · ⚫ the game has no online mode. **Runs in RPCS3:** *Playable* start to finish ·
*Ingame* gets in but has problems · *Intro* / *Loadable* doesn't really play yet.

### Other consoles
| Console | Game | Online through | Where it is |
|---|---|---|---|
| PS4 | Call of Duty: Black Ops II (`CUSA57548`) | shadNet + our Demonware | 🟡 two PCs played matches; PS3 crossplay in progress ([notes](games/CUSA57548.md)) |
| Switch | Mario Kart 8 Deluxe (`0100152000022000`) | Eden room (Wireless Play) | 🟡 two copies raced on one PC ([notes](games/0100152000022000.md)) |
| Wii | Wii Sports + Wii Sports Resort (`SP2E01`) | Dolphin NetPlay | 🟠 boots; NetPlay test next ([notes](games/SP2E01.md)) |
| 3DS | Mario Kart 7, Pokémon X/Y, Luigi's Mansion 2, ... | Azahar rooms | 🟠 runs in Azahar; room test next ([`uscl-3ds`](uscl-3ds/)) |

## How online works
Old console games expect servers that are gone. USCL uses one of two ways, depending on the game:

1. **Stand-in servers** for games that had real online play. The host's PC runs them: [RPCN](uscl-rpcn/) for PSN
   rooms, friends and invites, [Demonware](uscl-dw/) for Call of Duty, [Blaze](uscl-blaze/) for EA games,
   [shadNet](uscl-shadnet/) for PS4 sign-in.
2. **The emulator's own netplay** for games that only had local or couch multiplayer: Dolphin NetPlay (Wii/GC),
   Azahar rooms (3DS), Eden rooms (Switch local wireless).

Either way, [`uscl-bridge`](uscl-bridge/) carries the traffic between the host's PC and their friends' over a Steam
connection. The servers only listen on your own PC, and nothing leaves Steam's network except the game itself.

## Repository map
| Folder | What it is |
|---|---|
| **The app** | |
| [`uscl-shell`](uscl-shell/) | The home screen (Qt Quick + SDL3): games, friends, settings, starts the emulators |
| [`uscl-bridge`](uscl-bridge/) | Steam tunnel (sessions, ports, invites, bans) and the PS3 ↔ PS4 crossplay translator |
| [`packaging`](packaging/) | Linux AppImage, Windows installer, signed releases |
| **Emulators** (our patches on a pinned upstream) | |
| [`uscl-rpcs3`](uscl-rpcs3/) | PS3: RPCS3 |
| [`uscl-shadps4`](uscl-shadps4/) | PS4: shadPS4 |
| [`uscl-eden`](uscl-eden/) | Switch: Eden |
| [`uscl-dolphin`](uscl-dolphin/) | Wii / GameCube: Dolphin (unchanged, platform notes) |
| [`uscl-3ds`](uscl-3ds/) | 3DS: Azahar (unchanged, platform notes) |
| **Online services** (stand-ins for the old servers) | |
| [`uscl-rpcn`](uscl-rpcn/) | PSN (PS3): RPCN |
| [`uscl-shadnet`](uscl-shadnet/) | PSN (PS4): shadNet |
| [`uscl-dw`](uscl-dw/) | Activision Demonware (Call of Duty) |
| [`uscl-blaze`](uscl-blaze/) | EA Blaze (Skate 3, NBA Jam, NHL, Madden) |
| [`uscl-switch-online`](uscl-switch-online/) | Switch online service (in development) |
| **Notes and tools** | |
| [`GAMES.md`](GAMES.md), [`games/`](games/) | Game tracker and per-game notes |
| [`PLAN.md`](PLAN.md), [`docs/`](docs/) | Plan, console UI and Switch setup |
| [`tools/`](tools/) | Test automation, dev scripts, emulator update checks ([`emu-sync`](tools/emu-sync/)), Ghidra helpers |

## Branches
Every branch has this same README. What each one is for:

| Branch | What's on it |
|---|---|
| `main` | Stable code. Changes only arrive through reviewed pull requests. |
| `dev`, `agent/jaiden-pc/steam-tunnel` | Where finished work is combined before it goes to `main` (steam-tunnel is the current one). |
| `agent/<name>/<topic>` | Work in progress, one branch per topic, each with a pull request (e.g. `agent/dysco-pc/windows-bridge`). |
| `status` | Only the game status badges above. |

## Safety
Only the owner can publish releases, and each one is signed with a key that never touches GitHub. USCL checks that
signature before it installs an update. To report a security problem privately, see [SECURITY.md](SECURITY.md).

## Credits
- Emulators: [RPCS3](https://github.com/RPCS3/rpcs3) (GPL-2.0), [shadPS4](https://github.com/shadps4-emu/shadPS4)
  (GPL-2.0), [Eden](https://git.eden-emu.dev/eden-emu/eden) (GPL-3.0), [Dolphin](https://github.com/dolphin-emu/dolphin)
  (GPL-2.0), [Azahar](https://github.com/azahar-emu/azahar) (GPL-2.0), [Cemu](https://github.com/cemu-project/Cemu)
  (MPL-2.0), [Xenia](https://github.com/xenia-canary/xenia-canary) (BSD)
- Servers: [RPCN](https://github.com/RipleyTom/rpcn) (AGPL-3.0), [shadNet](https://github.com/shadps4-emu/shadNet)
  (GPL-2.0), [open-bitdemon-emulator](https://github.com/Laupetin/open-bitdemon-emulator) (AGPL-3.0),
  [Skate3BlazeServer](https://github.com/skate6743/Skate3BlazeServer)
- Home screen art from the JStationX Kodi theme; [Material Icons](https://fonts.google.com/icons) (Apache-2.0);
  Noto Sans and Roboto (OFL / Apache-2.0); [TweetNaCl](https://tweetnacl.cr.yp.to) (public domain)

USCL isn't affiliated with Sony, Nintendo, Microsoft, Valve, Activision, EA or any emulator team.
