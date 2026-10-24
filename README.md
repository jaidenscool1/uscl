# USCL: Ultimate Steam Compatibility Layer

**Play PS3 games online with your Steam friends.** USCL is a PS3 console on your PC: a PS3-style home screen
built for a controller, our own build of the [RPCS3](https://rpcs3.net) emulator, and small servers that stand in
for the PS3's old online services. You and your friends connect through Steam, so nobody has to open ports.

> **Status: early testing.** Some games already work online in local tests; playing between two PCs over Steam
> is being tested now. Expect bugs.

## What you get
- 🎮 **A home screen like the PS3's.** Controller first: games, friends, settings, all from the couch.
- ⚙️ **Sets itself up.** It uses your Steam name, installs the PS3 system software for you, finds your games,
  sets up every connected controller and picks the sharpest picture your PC can handle.
- 🌐 **Online with Steam friends.** Host a session for a game and your friends join from their Friends list.
  Test Connection checks that everything is ready.
- 🔄 **Updates itself.** When a new version is out, USCL asks before installing it. Every update is signed, and
  USCL refuses any file that isn't.

## Get it
Download the newest version from **[Releases](https://github.com/jaidenscool1/uscl/releases)**.

| PC | File | How |
|---|---|---|
| Windows 10/11 | `USCL-Setup.exe` | Run it. No admin rights needed. USCL goes in the Start menu. |
| Linux | `USCL-x86_64.AppImage` | Make it executable and run it. To add it to your app menu, use [`install.sh`](https://github.com/jaidenscool1/uscl/blob/agent/jaiden-pc/steam-tunnel/packaging/linux/install.sh): `./install.sh USCL-x86_64.AppImage` |

You'll need **Steam running and signed in**, and **your own copies of your games** (disc folders, ISOs or
installed games). USCL doesn't include any games. On first start it walks you through the rest.

**Controls:** ✕/A select · ○/B back · △/Y options · hold PS/Guide to quit a game.

## Games
Both status columns update live:
- **USCL online** shows how far online play has got in USCL. It comes from this repo's
  [`status`](https://github.com/jaidenscool1/uscl/tree/status) branch.
- **Runs in RPCS3** comes straight from [RPCS3's compatibility list](https://rpcs3.net/compatibility) and shows
  how well the game plays offline in the emulator.

Click a badge for details.

### Supported now / being worked on
| Game | Serial | Online through | USCL online | Runs in RPCS3 |
|---|---|---|---|---|
| Call of Duty: Black Ops II | `BLUS31011` | Demonware (our own server) | [![USCL](https://img.shields.io/endpoint?url=https%3A%2F%2Fraw.githubusercontent.com%2Fjaidenscool1%2Fuscl%2Fstatus%2Fbadges%2FBLUS31011.json)](https://github.com/jaidenscool1/uscl/blob/agent/jaiden-pc/steam-tunnel/GAMES.md) | [![RPCS3](https://img.shields.io/badge/dynamic/json?url=https%3A%2F%2Frpcs3.net%2Fcompatibility%3Fapi%3Dv1%26g%3DBLUS31011&query=%24.results.BLUS31011.status&label=RPCS3&color=blue)](https://rpcs3.net/compatibility?g=BLUS31011) |
| Minecraft: PlayStation 3 Edition | `BLUS31426` | PSN rooms (RPCN) | [![USCL](https://img.shields.io/endpoint?url=https%3A%2F%2Fraw.githubusercontent.com%2Fjaidenscool1%2Fuscl%2Fstatus%2Fbadges%2FBLUS31426.json)](https://github.com/jaidenscool1/uscl/blob/agent/jaiden-pc/steam-tunnel/GAMES.md) | [![RPCS3](https://img.shields.io/badge/dynamic/json?url=https%3A%2F%2Frpcs3.net%2Fcompatibility%3Fapi%3Dv1%26g%3DBLUS31426&query=%24.results.BLUS31426.status&label=RPCS3&color=blue)](https://rpcs3.net/compatibility?g=BLUS31426) |
| Skate 3 | `BLUS30464` | EA Blaze + PSN invites | [![USCL](https://img.shields.io/endpoint?url=https%3A%2F%2Fraw.githubusercontent.com%2Fjaidenscool1%2Fuscl%2Fstatus%2Fbadges%2FBLUS30464.json)](https://github.com/jaidenscool1/uscl/blob/agent/jaiden-pc/steam-tunnel/GAMES.md) | [![RPCS3](https://img.shields.io/badge/dynamic/json?url=https%3A%2F%2Frpcs3.net%2Fcompatibility%3Fapi%3Dv1%26g%3DBLUS30464&query=%24.results.BLUS30464.status&label=RPCS3&color=blue)](https://rpcs3.net/compatibility?g=BLUS30464) |
| Midnight Club: Los Angeles | `BLUS30442` | Rockstar's own + PSN | [![USCL](https://img.shields.io/endpoint?url=https%3A%2F%2Fraw.githubusercontent.com%2Fjaidenscool1%2Fuscl%2Fstatus%2Fbadges%2FBLUS30442.json)](https://github.com/jaidenscool1/uscl/blob/agent/jaiden-pc/steam-tunnel/GAMES.md) | [![RPCS3](https://img.shields.io/badge/dynamic/json?url=https%3A%2F%2Frpcs3.net%2Fcompatibility%3Fapi%3Dv1%26g%3DBLUS30442&query=%24.results.BLUS30442.status&label=RPCS3&color=blue)](https://rpcs3.net/compatibility?g=BLUS30442) |
| Spider-Man: Shattered Dimensions | `BLUS30499` | single-player | [![USCL](https://img.shields.io/endpoint?url=https%3A%2F%2Fraw.githubusercontent.com%2Fjaidenscool1%2Fuscl%2Fstatus%2Fbadges%2FBLUS30499.json)](https://github.com/jaidenscool1/uscl/blob/agent/jaiden-pc/steam-tunnel/GAMES.md) | [![RPCS3](https://img.shields.io/badge/dynamic/json?url=https%3A%2F%2Frpcs3.net%2Fcompatibility%3Fapi%3Dv1%26g%3DBLUS30499&query=%24.results.BLUS30499.status&label=RPCS3&color=blue)](https://rpcs3.net/compatibility?g=BLUS30499) |
| Spider-Man: Edge of Time | `BLUS30760` | single-player | [![USCL](https://img.shields.io/endpoint?url=https%3A%2F%2Fraw.githubusercontent.com%2Fjaidenscool1%2Fuscl%2Fstatus%2Fbadges%2FBLUS30760.json)](https://github.com/jaidenscool1/uscl/blob/agent/jaiden-pc/steam-tunnel/GAMES.md) | [![RPCS3](https://img.shields.io/badge/dynamic/json?url=https%3A%2F%2Frpcs3.net%2Fcompatibility%3Fapi%3Dv1%26g%3DBLUS30760&query=%24.results.BLUS30760.status&label=RPCS3&color=blue)](https://rpcs3.net/compatibility?g=BLUS30760) |

### Next up
| Game | Serial | Online through | USCL online | Runs in RPCS3 |
|---|---|---|---|---|
| Call of Duty: Modern Warfare 2 | `BLUS30377` | Demonware | [![USCL](https://img.shields.io/endpoint?url=https%3A%2F%2Fraw.githubusercontent.com%2Fjaidenscool1%2Fuscl%2Fstatus%2Fbadges%2FBLUS30377.json)](https://github.com/jaidenscool1/uscl/blob/agent/jaiden-pc/steam-tunnel/GAMES.md) | [![RPCS3](https://img.shields.io/badge/dynamic/json?url=https%3A%2F%2Frpcs3.net%2Fcompatibility%3Fapi%3Dv1%26g%3DBLUS30377&query=%24.results.BLUS30377.status&label=RPCS3&color=blue)](https://rpcs3.net/compatibility?g=BLUS30377) |
| Call of Duty: Black Ops | `BLUS30591` | Demonware | [![USCL](https://img.shields.io/endpoint?url=https%3A%2F%2Fraw.githubusercontent.com%2Fjaidenscool1%2Fuscl%2Fstatus%2Fbadges%2FBLUS30591.json)](https://github.com/jaidenscool1/uscl/blob/agent/jaiden-pc/steam-tunnel/GAMES.md) | [![RPCS3](https://img.shields.io/badge/dynamic/json?url=https%3A%2F%2Frpcs3.net%2Fcompatibility%3Fapi%3Dv1%26g%3DBLUS30591&query=%24.results.BLUS30591.status&label=RPCS3&color=blue)](https://rpcs3.net/compatibility?g=BLUS30591) |
| Call of Duty: Modern Warfare 3 | `BLUS30838` | Demonware | [![USCL](https://img.shields.io/endpoint?url=https%3A%2F%2Fraw.githubusercontent.com%2Fjaidenscool1%2Fuscl%2Fstatus%2Fbadges%2FBLUS30838.json)](https://github.com/jaidenscool1/uscl/blob/agent/jaiden-pc/steam-tunnel/GAMES.md) | [![RPCS3](https://img.shields.io/badge/dynamic/json?url=https%3A%2F%2Frpcs3.net%2Fcompatibility%3Fapi%3Dv1%26g%3DBLUS30838&query=%24.results.BLUS30838.status&label=RPCS3&color=blue)](https://rpcs3.net/compatibility?g=BLUS30838) |
| Battlefield 3 | `BLUS30762` | EA Blaze | [![USCL](https://img.shields.io/endpoint?url=https%3A%2F%2Fraw.githubusercontent.com%2Fjaidenscool1%2Fuscl%2Fstatus%2Fbadges%2FBLUS30762.json)](https://github.com/jaidenscool1/uscl/blob/agent/jaiden-pc/steam-tunnel/GAMES.md) | [![RPCS3](https://img.shields.io/badge/dynamic/json?url=https%3A%2F%2Frpcs3.net%2Fcompatibility%3Fapi%3Dv1%26g%3DBLUS30762&query=%24.results.BLUS30762.status&label=RPCS3&color=blue)](https://rpcs3.net/compatibility?g=BLUS30762) |
| Call of Duty: Ghosts | `BLUS31270` | Demonware | [![USCL](https://img.shields.io/endpoint?url=https%3A%2F%2Fraw.githubusercontent.com%2Fjaidenscool1%2Fuscl%2Fstatus%2Fbadges%2FBLUS31270.json)](https://github.com/jaidenscool1/uscl/blob/agent/jaiden-pc/steam-tunnel/GAMES.md) | [![RPCS3](https://img.shields.io/badge/dynamic/json?url=https%3A%2F%2Frpcs3.net%2Fcompatibility%3Fapi%3Dv1%26g%3DBLUS31270&query=%24.results.BLUS31270.status&label=RPCS3&color=blue)](https://rpcs3.net/compatibility?g=BLUS31270) |
| Call of Duty 4: Modern Warfare | `BLUS30072` | Demonware | [![USCL](https://img.shields.io/endpoint?url=https%3A%2F%2Fraw.githubusercontent.com%2Fjaidenscool1%2Fuscl%2Fstatus%2Fbadges%2FBLUS30072.json)](https://github.com/jaidenscool1/uscl/blob/agent/jaiden-pc/steam-tunnel/GAMES.md) | [![RPCS3](https://img.shields.io/badge/dynamic/json?url=https%3A%2F%2Frpcs3.net%2Fcompatibility%3Fapi%3Dv1%26g%3DBLUS30072&query=%24.results.BLUS30072.status&label=RPCS3&color=blue)](https://rpcs3.net/compatibility?g=BLUS30072) |
| Battlefield: Bad Company 2 | `BLUS30458` | EA Blaze | [![USCL](https://img.shields.io/endpoint?url=https%3A%2F%2Fraw.githubusercontent.com%2Fjaidenscool1%2Fuscl%2Fstatus%2Fbadges%2FBLUS30458.json)](https://github.com/jaidenscool1/uscl/blob/agent/jaiden-pc/steam-tunnel/GAMES.md) | [![RPCS3](https://img.shields.io/badge/dynamic/json?url=https%3A%2F%2Frpcs3.net%2Fcompatibility%3Fapi%3Dv1%26g%3DBLUS30458&query=%24.results.BLUS30458.status&label=RPCS3&color=blue)](https://rpcs3.net/compatibility?g=BLUS30458) |
| LittleBigPlanet (Game of the Year) | `BCUS98208` | PSN | [![USCL](https://img.shields.io/endpoint?url=https%3A%2F%2Fraw.githubusercontent.com%2Fjaidenscool1%2Fuscl%2Fstatus%2Fbadges%2FBCUS98208.json)](https://github.com/jaidenscool1/uscl/blob/agent/jaiden-pc/steam-tunnel/GAMES.md) | [![RPCS3](https://img.shields.io/badge/dynamic/json?url=https%3A%2F%2Frpcs3.net%2Fcompatibility%3Fapi%3Dv1%26g%3DBCUS98208&query=%24.results.BCUS98208.status&label=RPCS3&color=blue)](https://rpcs3.net/compatibility?g=BCUS98208) |
| LittleBigPlanet 2 | `BCUS98245` | PSN | [![USCL](https://img.shields.io/endpoint?url=https%3A%2F%2Fraw.githubusercontent.com%2Fjaidenscool1%2Fuscl%2Fstatus%2Fbadges%2FBCUS98245.json)](https://github.com/jaidenscool1/uscl/blob/agent/jaiden-pc/steam-tunnel/GAMES.md) | [![RPCS3](https://img.shields.io/badge/dynamic/json?url=https%3A%2F%2Frpcs3.net%2Fcompatibility%3Fapi%3Dv1%26g%3DBCUS98245&query=%24.results.BCUS98245.status&label=RPCS3&color=blue)](https://rpcs3.net/compatibility?g=BCUS98245) |
| Killzone 2 | `BCUS98116` | PSN | [![USCL](https://img.shields.io/endpoint?url=https%3A%2F%2Fraw.githubusercontent.com%2Fjaidenscool1%2Fuscl%2Fstatus%2Fbadges%2FBCUS98116.json)](https://github.com/jaidenscool1/uscl/blob/agent/jaiden-pc/steam-tunnel/GAMES.md) | [![RPCS3](https://img.shields.io/badge/dynamic/json?url=https%3A%2F%2Frpcs3.net%2Fcompatibility%3Fapi%3Dv1%26g%3DBCUS98116&query=%24.results.BCUS98116.status&label=RPCS3&color=blue)](https://rpcs3.net/compatibility?g=BCUS98116) |

**USCL online:** 🟢 works online between PCs · 🟡 testing (works in a local test) · 🟠 in progress ·
⚪ next up / not started · ⚫ the game has no online mode.

**Runs in RPCS3:** *Playable* means start to finish with no major problems · *Ingame* means it gets into the game
but has problems · *Intro* and *Loadable* mean it doesn't really play yet.

The full tracker, with what's blocking each game, is in [GAMES.md](https://github.com/jaidenscool1/uscl/blob/agent/jaiden-pc/steam-tunnel/GAMES.md).

## How online works
PS3 games expect Sony's and the publishers' old servers. USCL runs stand-ins for those servers on the host's PC:
- [RPCN](https://github.com/RipleyTom/rpcn) for PSN rooms, friends and invites.
- A [Demonware server](https://github.com/Laupetin/open-bitdemon-emulator) for Call of Duty.

The host's friends reach them through a Steam connection. Those servers only listen on your own PC, and nothing
leaves Steam's network except the game itself.

## Branches
Every branch has this same README. What each one is for:

| Branch | What's on it |
|---|---|
| `main` | Stable code. Changes only arrive through reviewed pull requests. |
| `dev` | Where finished work is combined before it goes to `main`. |
| `agent/jaiden-pc/steam-tunnel` | Current work: home screen, Steam tunnel, installers, updater. |
| `agent/jaiden-pc/collab-setup` | Project setup for the people working on USCL. |
| `agent/dysco-pc/windows-bridge` | The Steam bridge on Windows. |
| `agent/dysco-pc/windows-rpcn` | RPCN on Windows. |
| `status` | Only the game status badges above. |

## Safety
Only the owner can publish releases, and each one is signed with a key that never touches GitHub. USCL checks that
signature before it installs an update. To report a security problem privately, see [SECURITY.md](https://github.com/jaidenscool1/uscl/blob/agent/jaiden-pc/steam-tunnel/SECURITY.md).

## Credits
- [RPCS3](https://github.com/RPCS3/rpcs3) (GPL-2.0)
- [RPCN](https://github.com/RipleyTom/rpcn)
- [open-bitdemon-emulator](https://github.com/Laupetin/open-bitdemon-emulator)
- Home screen art from the JStationX Kodi theme
- [Material Icons](https://fonts.google.com/icons) (Apache-2.0)
- Noto Sans and Roboto (OFL / Apache-2.0)
- [TweetNaCl](https://tweetnacl.cr.yp.to) (public domain)

USCL isn't affiliated with Sony, Valve, Activision, EA or the RPCS3 team.
