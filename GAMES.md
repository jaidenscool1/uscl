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
| Call of Duty: Black Ops II (PS4, shadPS4) | [CUSA57548](games/CUSA57548.md) | update in `CUSA57548-UPDATE` | Demonware (title 2430, auth3 HTTPS) + shadNet | ✅ public Free-for-All matches between two PCs over Steam, also hosted from the USCL app (2026-10-28) | in-game friends / invites (PlayerSessions); Windows app build |
| Gotham City Impostors (PSN) | [NPUB30252](games/NPUB30252.md) | 1.00 trial + full-game unlock | GameSpy (`sirenps3`, shut down 2014) + PSN tickets | 🚧 full-game licence accepted; stuck at the title without GameSpy (2026-10-26) | answer the SSLv3 GameSpy login (`sirenps3.api.gamespy.net`) |
| NBA Jam | [BLUS30696](games/BLUS30696.md) | 1.00 | Blaze 2.11 + RPCN ticket | 🧪 two players matchmake into one lobby through our Blaze stand-in (`uscl-blaze`) and the relay carries game traffic both ways (2026-11-07) | clean two-PC test to see the match play; tunnel the relay over Steam |
| NHL 15 | [BLUS31436](games/BLUS31436.md) | 1.01 | Blaze 3.19 (ProtoSSL TLS) + RPCN ticket | 🚧 **logs in & connects to the EA servers** — full heat2 login solved (ConsoleLoginResponse + UserAuthenticated), online ticker plays, reaches "Retrieving data" (2026-11-09) | one diagnosed, server-fixable crash (`O_SG_TCKR` ticker group): skip the ticker (error for OsdkTicker2/2249) or RE its RegisterResponse |
| Madden NFL 25 | [BLUS31178](games/BLUS31178.md) | 1.00 | Blaze 13.3 (ProtoSSL TLS) + RPCN ticket | 🚧 shares NHL's now-solved heat2 login; blocked earlier by an unrelated RPCS3 FGGameSim PPU crash ~52s (2026-11-09) | use NHL 15 to finish the EA Sports flow first; then revisit Madden's emulation crash |
| Grand Theft Auto V | [BLUS31156](games/BLUS31156.md) | disc 1.01 (Online needs the last update) | own (Rockstar ROS, HTTPS 443) + PSN ticket | 🚧 profiled 2026-11-06: boots, signs in to RPCN, calls `prod.ros.rockstargames.com` | install the last title update, profile again; ROS stand-in after the EA games |

## Next up
Ordered by how much existing work each one reuses. Serials and services marked `?` are from memory: confirm them
with `np-profile.py` on your own dump before starting.

| Game | Serial | Online service | Reuses | Status |
|---|---|---|---|---|
| Call of Duty: Black Ops | [BLUS30591](games/BLUS30591.md) | Demonware (cod7, T5) | uscl-dw (BO2 LSG); needs T5 lobby | 🚧 Demonware title id = 5185 captured + added (patch 0021); hits the BO2-style LSG; live re-test pending dw restart (2026-11-07) |
| Call of Duty: Modern Warfare 3 | ? | Demonware ? | BO2's Demonware | 📋 |
| Call of Duty: Ghosts | ? | Demonware ? | BO2's Demonware | ⬜ |
| Call of Duty: World at War | ? | Demonware ? | BO2's Demonware | ⬜ |
| Call of Duty 4: Modern Warfare | ? | Demonware ? | BO2's Demonware | ⬜ |
| Battlefield 3 | ? | Blaze ? | the heat2 login (NHL/Madden) + Skate 3's Blaze work | 📋 |
| FIFA (2012–2014) | ? | Blaze ? | the heat2 login (NHL/Madden) + Skate 3's Blaze work | ⬜ |
| Need for Speed: Hot Pursuit / Most Wanted (2012) | ? | Blaze ? | the heat2 login (NHL/Madden) + Skate 3's Blaze work | ⬜ |
| Mass Effect 3 | ? | Blaze ? | the heat2 login (NHL/Madden) + Skate 3's Blaze work | ⬜ |
| LittleBigPlanet 1 / 2 | ? | ? | profile first | ⬜ |
| Uncharted 2 / 3 | ? | ? | profile first | ⬜ |
| Killzone 2 / 3 | ? | ? | profile first | ⬜ |
| Gran Turismo 5 | ? | ? | profile first | ⬜ |
| Midnight Club: Los Angeles | [BLUS30442](games/BLUS30442.md) | GameSpy (+ Sake) | GCI's GameSpy revival | ⬜ re-profiled 2026-11-07: GameSpy, not Rockstar |
| LittleBigPlanet 3 | [BCUS98362](games/BCUS98362.md) | PSN (Matching2) | RPCN directly, like Minecraft | 🧪 boots + connects to RPCN (2026-11-07); co-op via Matching2 should work, two-copy test next |
| NHL 15 | [BLUS31436](games/BLUS31436.md) | Blaze 3.19 (ProtoSSL) | Madden's Blaze 3.x work (`uscl-blaze`) | ⬜ profiled 2026-11-07: same 3.x codec blocker as Madden |
| GTA IV, Red Dead Redemption | ? | own (Rockstar) ? | GTA V's ROS work | ⬜ |

## No online multiplayer
| Game | Serial |
|---|---|
| Spider-Man: Shattered Dimensions | BLUS30499 |
| Spider-Man: Edge of Time | BLUS30760 |

## Routes (how much work a game is)
- **PSN only:** RPCN plus the session tunnel already carry it. Test it and fix small things. No reverse engineering.
- **Demonware / Blaze:** our own stand-in server for that service, shared by every game on it. The first game is
  slow (BO2 for Demonware in `uscl-dw`, Skate 3 / NBA Jam for Blaze in `uscl-blaze`); later ones add their title ID, server names and the extra services
  their menus call.
- **Community server exists:** the host runs it and the bridge forwards its ports (`uscl-bridge host <preset>`).
- **LAN / system link only:** one shared virtual-LAN feature in the bridge (not built yet).
- **Own publisher servers:** a new stand-in per publisher.

## Adding a game
1. Boot it to the menu, run `./np-profile.py` on the log, and add the row here with the service it shows.
2. If you need to look inside: `./unself.py <EBOOT.BIN> <out.elf>` (never commit game files).
3. Write `games/<serial>.md`: how it goes online, the plan, open questions. Keep both up to date as it moves.
