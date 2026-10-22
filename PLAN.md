# PS3 games online through Steam (via RPCS3)

Goal: PS3 games (your own dumps) running in RPCS3 use Steam for friends, invites, lobbies and P2P, without
patching each game. Status: design + game survey. No Steam code yet.

## How it works
PS3 games never talk to PSN directly. They call Sony's libraries (`sceNp*`). RPCS3 already answers those
calls itself, backed by RPCN (its PSN replacement). This project adds a second backend behind the same calls:

```
game ──sceNp* calls──▶ RPCS3 (np_handler) ──▶ RPCN             (today)
                                          └─▶ steam-bridge ──▶ Steam   (this project)
```

| PSN library | Steam stand-in |
|---|---|
| sceNpMatching2 rooms, room data, search | Steam lobbies + lobby data |
| sceNpMatching2 signaling (P2P) | SteamNetworkingMessages (Valve relay, no port forwarding) |
| sceNpBasic friends, presence, invites | Steam friends, Rich Presence, lobby invites / overlay "Join" |
| sceNpScore | Steam leaderboards |
| sceNpManager sign-in, NP ID | Steam account (persona name becomes the online ID) |
| sceNpTus (per-user cloud data) | none: needs a small server |
| sceNpTrophy | not feasible: Steam achievements need an App ID per game |

**steam-bridge** is a separate helper process. RPCS3 is GPL-2 and the Steamworks SDK is proprietary, so they
can't be linked into one program that gets shared. RPCS3 talks to the bridge over a local socket. That also
keeps the RPCS3 patch small: it is an alternative to the RPCN client, not a rewrite.

**Steam App ID:** Steam features need one. For personal testing, the public test ID 480 works. Anything shared
beyond the two of you needs its own ($100 Steamworks fee, and Valve has to accept it).

## Which games it covers
Boot a game to its menu, then run `./np-profile.py`. It reads the PSN imports from RPCS3.log:

| Verdict | Meaning | Example (profiled) |
|---|---|---|
| FULL | rooms + P2P through Sony: Steam can carry all of it | Minecraft PS3 Edition (BLUS31426): 27 Matching2 functions, plus Score and Basic |
| PARTIAL | PSN only for sign-in/invites; matchmaking on the publisher's servers | Skate 3 (BLUS30464): Basic, Manager, Lookup, no Matching2 |
| NONE | no PSN multiplayer | Spider-Man: Shattered Dimensions / Edge of Time |

PARTIAL games still need a community server for that publisher (EA's Blaze for Skate 3, Battlefield, FIFA;
Activision's Demonware for Call of Duty; Rockstar's own for GTA/RDR). Steam can only add friends/invites on top.

### The "top 10" list
Candidates (best-selling PS3 multiplayer titles in the US, from memory; nothing profiled yet): Call of Duty MW2,
MW3, Black Ops, Black Ops II; GTA IV / V; Battlefield 3; Red Dead Redemption; Uncharted 2 / 3; LittleBigPlanet
1 / 2; Gran Turismo 5; Killzone 2 / 3; FIFA. Most big third-party ones are expected to be PARTIAL
(publisher servers). Several Sony first-party titles are likely FULL or close to it. **Each verdict needs the
game dumped from your own disc/account and run through `np-profile.py`.** Don't trust this paragraph over
the tool.

## Cross-play with Xbox 360 (Xenia)
Not something the bridge can do on its own. For a PS3 and a 360 copy to play together:
1. both emulators must meet in the same lobby (Xenia Canary's netplay uses its own Xbox Live stand-in, so a
   second bridge, Xenia → Steam lobbies, is needed), **and**
2. the two builds must speak the same in-game protocol. The 360 one wraps traffic in Xbox networking (XNet/VDP)
   and the versions usually differ. No PS3 game was designed for this, so it works per game or not at all.

Best candidate: **Minecraft** (4J Studios made both editions from one codebase, and both got their last
updates around the same time). The test is to capture each edition's session traffic and compare.
Treat it as research, not a feature.

## Phases
1. **Survey** (now): profile every game you own. Pick the first FULL one (Minecraft).
2. **Bridge proof of concept**: steam-bridge with lobby create/join/list and Rich Presence, driven from a small
   test client. No RPCS3 changes yet.
3. **RPCS3 backend**: route sceNpMatching2 rooms + signaling to the bridge in an RPCS3 fork, using
   Minecraft. Done when two PCs join the same world through Steam with RPCN off.
4. Basic (invites/presence) + Score for FULL games. Then friends/invites only for PARTIAL games like Skate 3.
5. Cross-play research (Minecraft PS3 ↔ 360).
