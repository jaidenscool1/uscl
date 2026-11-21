# USCL: Ultimate Steam Compatibility Layer

**Play your console games online with your Steam friends.** USCL is one console-style app for your PC: a
controller-first home screen, emulators set up for you, and small servers that stand in for the consoles' old
online services. You and your friends connect through Steam, so nobody has to open ports.

PS3 came first. USCL now also runs PS4, Switch, Wii, GameCube, 3DS, Wii U, Switch 2 and Xbox 360 games, at different stages
(see [Platforms](#platforms)). The goal is every console, all from the same home screen.

> **Status: early testing.** Several games already play online in tests; playing between two PCs over Steam is
> being tested now. Expect bugs.

**Contents:** [What you get](#what-you-get) · [Platforms](#platforms) · [Get it](#get-it) ·
[Controllers](#controllers) · [Games](#games) · [How online works](#how-online-works) ·
[Switch 2 progress](docs/SWITCH2-PROGRESS.md) · [Fork maintenance](#fork-maintenance) · [Repository map](#repository-map) · [Branches](#branches) · [Safety](#safety) · [Credits](#credits)

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
| **Wii / GameCube** | Dolphin, our build ([`uscl-dolphin`](uscl-dolphin/)) | Dolphin NetPlay over Steam | 🟡 Wii Sports tested between Linux and Windows over Steam; Friends-list joining still needs testing |
| **Nintendo 3DS** | Azahar, our build ([`uscl-3ds`](uscl-3ds/) patches) | Azahar rooms over Steam | 🟡 Mario Kart 7 local race between two copies in one room; home screen hosts the room; two PCs next |
| **Wii U** | Cemu (no fork yet) | later: a Pretendo / NEX stand-in | ⚪ starts Cemu from USCL (untested) |
| **Xbox 360** | Xenia (Wine on Linux) | later | ⚪ experimental launch |
| **Nintendo Switch 2** | Eglantine, our build ([`uscl-eglantine`](uscl-eglantine/)) | none yet | 🟠 Mario Kart World plays offline; online room integration pending |
| **Nintendo DS** | melonDS, our build ([`uscl-melonds`](uscl-melonds/)) | per-game agent + LAN link over Steam | 🟠 HeartGold tested with two local copies; agent launch only, no home-screen launch yet |
| **More** | | | planned: see [PLAN.md](PLAN.md) |

🟢 works online in tests · 🟡 online works on one PC or between two in early tests · 🟠 runs, online not yet ·
⚪ runs, online not started.

Where we change an emulator, we keep our changes as patch files against a pinned upstream commit (the `uscl-*`
folders). The packaging recipes include those builds; actual contents depend on the release and, on Linux,
which local builds were available when packaging. Cemu and Xenia have no maintained USCL patch set here; on
Linux Cemu uses its Flathub app and Xenia uses Wine.

## Get it
Download the newest version from **[Releases](https://github.com/jaidenscool1/uscl/releases)**.

| PC | File | How |
|---|---|---|
| Windows 10/11 | `USCL-Setup.exe` | Run it. No admin rights needed. USCL goes in the Start menu. |
| Linux (any x86-64 distro) | `USCL-x86_64.AppImage` + `install.sh` | Put both in one folder and run `bash install.sh`: USCL goes in your app menu. Or just make the AppImage executable and run it. |
| macOS | | not supported yet |

You'll need **Steam running and signed in**, and **your own copies of your games** (disc folders, ISOs, installed
games, or the console's game files). USCL doesn't include any games, console keys or firmware. On first start it
walks you through the rest.

The installer recipes include our builds of: RPCS3 (PS3), shadPS4 (PS4), Dolphin (Wii,
GameCube), Azahar (3DS), Eden (Switch), Eglantine (Switch 2) and melonDS (DS). Switch and Switch 2 need your own
console keys and firmware ([docs/SWITCH.md](docs/SWITCH.md)); Wii U uses Cemu from Flathub (Linux). Check the
release notes for the binaries actually included. DS currently starts through its per-game agent.

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
| Switch 2 | Mario Kart World (`0400C3F00006E000`) | none yet (Eglantine has no room support) | 🟠 plays offline from the home screen; racing ~41–42 fps ([`uscl-eglantine`](uscl-eglantine/)) |
| Switch 2 | Nintendo Switch Sports Resort (`04008E001F098000`) | none yet | 🟠 boots, then freezes in the emulator ([`uscl-eglantine`](uscl-eglantine/)) |
| Wii | Wii Sports + Wii Sports Resort (`SP2E01`) | Dolphin NetPlay | 🟠 boots; NetPlay test next ([notes](games/SP2E01.md)) |
| 3DS | Mario Kart 7, Pokémon X/Y, Luigi's Mansion 2, ... | Azahar rooms | 🟡 MK7 two copies raced in one room ([`uscl-3ds`](uscl-3ds/)) |

## How online works
Old console games expect servers that are gone. USCL uses one of two ways, depending on the game:

1. **Stand-in servers** for games that had real online play. The host's PC runs them: [RPCN](uscl-rpcn/) for PSN
   rooms, friends and invites, [Demonware](uscl-dw/) for Call of Duty, [Blaze](uscl-blaze/) for EA games,
   [shadNet](uscl-shadnet/) for PS4 sign-in.
2. **The emulator's own netplay** for games that only had local or couch multiplayer: Dolphin NetPlay (Wii/GC),
   Azahar rooms (3DS), Eden rooms (Switch local wireless).

Either way, [`uscl-bridge`](uscl-bridge/) carries the traffic between the host's PC and their friends' over a Steam
connection. The servers only listen on your own PC, and nothing leaves Steam's network except the game itself.

Read the bundled [changelog](CHANGELOG.md) in **Settings → USCL Changelog** in any theme, even offline.
The v0.5.5-alpha release line adds automatic Switch keys and firmware, the Switch launch fix and the USCL online ticker.
See [release assets and notes](https://github.com/jaidenscool1/uscl/releases/tag/v0.5.5-alpha).
The [Switch 2 tracker](docs/SWITCH2-PROGRESS.md) separates the release baseline from pending CPU/GPU translation work.

## Fork maintenance

This branch contains build recipes and patch sets for the following emulator bases. These are the revisions
used by the recipes, not a claim that they are the latest upstream versions or already published releases.

| Emulator | Pinned base | USCL patch documentation | Upstream update handling |
|---|---|---|---|
| RPCS3 | `eee3f0f` | [PS3](uscl-rpcs3/README.md) | `emu-sync` tracks `master` in a separate development clone |
| shadPS4 | `3eecea94ddaca360b19f4b800523b609a8b2ffcc` | [PS4](uscl-shadps4/README.md) | `emu-sync` tracks `main` |
| Dolphin | `2609a` | [Wii / GameCube](uscl-dolphin/README.md) | Manual pinned-base update; preserve NetPlay version compatibility |
| Azahar | `2126.2` | [3DS](uscl-3ds/README.md) | Manual pinned-base update; preserve room version compatibility |
| Eden | `v0.2.1` (`58c1e20ee58efa3900ba616207d460886214480b`) | [Switch](uscl-eden/README.md) | Manual pinned-base update |
| Eglantine | `30181ba05b484a4beefa838457b02d6ab5aa7baa` | [Switch 2](uscl-eglantine/README.md) | `emu-sync` tracks `nx2-experimental` |
| melonDS | `497aa6cdd26f45f8064b78916b00a773eab4ded7` | [DS](uscl-melonds/README.md) | Manual pinned-base update |

Sources: [Windows emulator manifest](packaging/windows/emulators.sh),
[RPCS3 / server build pins](.github/workflows/windows-installer.yml), and the linked patch documentation.
Linux packaging consumes prebuilt local cores through [build-linux.sh](packaging/build-linux.sh); it does not
fetch these pins itself. The [upstream checker](tools/emu-sync/README.md) also covers RPCN, shadNet and
open-bitdemon. Updating a development clone does not regenerate this repository's patches, change build pins,
rebuild installers or publish an update. Those steps need a reviewed change and build/game verification.

The home screen has eight selectable themes: PS3, PS4, PS5, Xbox 360 Classic, Xbox 360 2012, Xbox One,
Switch and Switch 2. Each separates software from console-style system menus; a full library provides console filters,
a persistent sidebar for Add Games, updates and help. Libraries offer saved sorting by title or console and preserve their console filter and selected game when returning from nested menus. Mouse, wheel and keyboard navigation share the same visible selection. Xbox One includes persistent game pins. PS5 Control Center cards show live library previews, and long Nintendo HOME titles scroll within a bounded region. Selected tiles resize smoothly, quick menus slide in, and game backdrops crossfade after loading. All share USCL's settings and update controls. A theme changes the interface;
it does not add emulation of its console (there is no PS5 or Xbox One emulator integration here).
See the [online screenshot/video reference audit](docs/CONSOLE-REFERENCES.md),
[theme behavior, checks and remaining manual verification](docs/CONSOLE-THEMES.md) and
[emulator loading-screen coverage](docs/LOADING-THEMES.md).

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
| [`uscl-dolphin`](uscl-dolphin/) | Wii / GameCube: Dolphin, patched startup and NetPlay integration |
| [`uscl-3ds`](uscl-3ds/) | 3DS: Azahar, patched room startup and integration |
| [`uscl-eglantine`](uscl-eglantine/) | Switch 2: Eglantine, offline integration and performance patches |
| [`uscl-melonds`](uscl-melonds/) | DS: melonDS, patched wireless link and local debugger for per-game agents |
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
Branch documentation can differ while changes are under review. This theme work targets
`agent/jaiden-pc/all-emulators`; see the pull request for its current base. General branch roles:

| Branch | What's on it |
|---|---|
| `main` | Stable code. Changes only arrive through reviewed pull requests. |
| `dev`, integration branches | Work combined for testing before review and release. |
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
