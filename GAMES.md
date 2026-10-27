# Game tracker

US (BLUS/BCUS) PS3 games with online multiplayer: what plays through USCL today, what's being worked on, and
what's next. One row per game; details live in `games/<serial>.md`. Update the row in the same PR as the work.

**Status:** ✅ plays online through USCL · 🧪 works in a local test, not yet across PCs over Steam ·
🚧 in progress · 📋 next up · ⬜ not started · ❌ no online multiplayer

**Online service** (what the game talks to; decides how much work it is, see "Routes" below):
PSN = Sony only (rooms/matching through RPCN) · Demonware = Activision · Blaze = EA ·
own = the publisher's own servers · GameSpy (revived by OpenSpy) · ? = not profiled yet

## Supported / in progress
| Game | Serial | Version | Online service | Status | Next step |
|---|---|---|---|---|---|
| Minecraft: PlayStation 3 Edition | [BLUS31426](games/BLUS31426.md) | 1.84 | PSN (Matching2) | 🧪 two copies on one PC join the same world and see each other (2026-10-23) | the same join from a second PC over Steam |
| Skate 3 | [BLUS30464](games/BLUS30464.md) | 1.05 + online EBOOT | Blaze (community server) + PSN invites | 🚧 bridge carries the server's ports (`host skate3`), untested | fix the joiner's black screen, then a two-PC test |
| Call of Duty: Modern Warfare 2 | [BLUS30377](games/BLUS30377.md) | 1.14 | Demonware (older, its own lobby protocol) | 🧪 our Demonware (`uscl-dw` patch 0013): two copies on one PC matchmade and played the same Hardcore Free-for-all (2026-10-25); invites don't work in RPCS3 | two PCs over the Steam tunnel |
| Call of Duty: Black Ops II | [BLUS31011](games/BLUS31011.md) | 1.19 | Demonware + PSN friends/invites | ✅ a full match between two PCs over Steam (Linux host, Windows member, 2026-10-27): Find Match, quality test, game traffic through the tunnel | lobby player names; Join Friend / invites; per-game RPCS3 settings (glitches, CPU) |
| Gotham City Impostors (PSN) | [NPUB30252](games/NPUB30252.md) | 1.00 trial + full-game unlock | GameSpy (`sirenps3`, shut down 2014) + PSN tickets | 🚧 full-game licence accepted; stuck at the title without GameSpy (2026-10-26) | answer the SSLv3 GameSpy login (`sirenps3.api.gamespy.net`) |

## Next up
Ordered by how much existing work each one reuses. Serials and services marked `?` are from memory: confirm them
with `np-profile.py` on your own dump before starting.

| Game | Serial | Online service | Reuses | Status |
|---|---|---|---|---|
| Call of Duty: Black Ops | ? | Demonware ? | BO2's Demonware | 📋 |
| Call of Duty: Modern Warfare 3 | ? | Demonware ? | BO2's Demonware | 📋 |
| Call of Duty: Ghosts | ? | Demonware ? | BO2's Demonware | ⬜ |
| Call of Duty: World at War | ? | Demonware ? | BO2's Demonware | ⬜ |
| Call of Duty 4: Modern Warfare | ? | Demonware ? | BO2's Demonware | ⬜ |
| Battlefield 3 | ? | Blaze ? | Skate 3's Blaze work | 📋 |
| FIFA (2012–2014) | ? | Blaze ? | Skate 3's Blaze work | ⬜ |
| Need for Speed: Hot Pursuit / Most Wanted (2012) | ? | Blaze ? | Skate 3's Blaze work | ⬜ |
| Mass Effect 3 | ? | Blaze ? | Skate 3's Blaze work | ⬜ |
| LittleBigPlanet 1 / 2 | ? | ? | profile first | ⬜ |
| Uncharted 2 / 3 | ? | ? | profile first | ⬜ |
| Killzone 2 / 3 | ? | ? | profile first | ⬜ |
| Gran Turismo 5 | ? | ? | profile first | ⬜ |
| Midnight Club: Los Angeles | BLUS30442 | own (Rockstar) + PSN friends/invites, scores (profiled 2026-10-23: PARTIAL) | new service: find what its race lobbies talk to | ⬜ |
| GTA IV / GTA V, Red Dead Redemption | ? | own (Rockstar) ? | new service | ⬜ |

## No online multiplayer
| Game | Serial |
|---|---|
| Spider-Man: Shattered Dimensions | BLUS30499 |
| Spider-Man: Edge of Time | BLUS30760 |

## Routes (how much work a game is)
- **PSN only:** RPCN plus the session tunnel already carry it. Test it and fix small things. No reverse engineering.
- **Demonware / Blaze:** our own stand-in server for that service, shared by every game on it. The first game is
  slow (BO2 for Demonware, Skate 3 for Blaze); later ones add their title ID, server names and the extra services
  their menus call.
- **Community server exists:** the host runs it and the bridge forwards its ports (`uscl-bridge host <preset>`).
- **LAN / system link only:** one shared virtual-LAN feature in the bridge (not built yet).
- **Own publisher servers:** a new stand-in per publisher.

## Adding a game
1. Boot it to the menu, run `./np-profile.py` on the log, and add the row here with the service it shows.
2. If you need to look inside: `./unself.py <EBOOT.BIN> <out.elf>` (never commit game files).
3. Write `games/<serial>.md`: how it goes online, the plan, open questions. Keep both up to date as it moves.
