# USCL: The Ultimate Steam Compatibility Layer

Goal: PS3 games (your own dumps) running in RPCS3 use Steam for friends, invites, lobbies and P2P, without
patching each game **and without a modified RPCS3**. Status: Steam bridge service working (see Phases).

## How it works
PS3 games never talk to PSN directly. They call Sony's libraries (`sceNp*`). RPCS3 already answers those
calls itself, backed by RPCN (its PSN replacement). RPCS3 lets you pick the RPCN host, so USCL runs a local
RPCN-compatible server on each player's PC and points RPCS3 at 127.0.0.1:

```
game ─sceNp*─▶ official RPCS3 ─RPCN protocol─▶ uscl-rpcn (local) ─JSON socket─▶ uscl-bridge ─▶ Steam
                     ▲                                                              │
                     └──── UDP game traffic ◀── local relay ports ◀── Steam P2P ◀───┘
```

- **uscl-rpcn**: a fork of the RPCN server (RipleyTom/rpcn, Rust, AGPL-3.0; the fork stays AGPL). Login
  accepts the local player automatically (identity = Steam account). Rooms are distributed: the room
  creator's uscl-rpcn is authoritative. Room external data and the member list live in the Steam lobby. Ops
  from other members (set member data, room messages) travel as lobby chat messages to the owner.
- **Signaling**: RPCN normally hands RPCS3 the peer's real IP:port. uscl-rpcn hands out a local relay
  address (127.0.0.1:port per peer). uscl-bridge forwards those UDP packets over SteamNetworkingMessages,
  through Valve's relay, so no port forwarding is needed.
- Neither RPCS3 nor RPCN code links Steam: the GPL/AGPL parts and the proprietary Steamworks SDK only meet
  over a local socket.

| PSN library | Steam stand-in |
|---|---|
| sceNpMatching2 rooms, room data, search | Steam lobbies + lobby data |
| sceNpMatching2 signaling (P2P) | SteamNetworkingMessages (Valve relay, no port forwarding) |
| sceNpBasic friends, presence, invites | Steam friends, Rich Presence, lobby invites / overlay "Join" |
| sceNpScore | Steam leaderboards |
| sceNpManager sign-in, NP ID | Steam account (persona name becomes the online ID) |
| sceNpTus (per-user cloud data) | none: needs a small server |
| sceNpTrophy | not feasible: Steam achievements need an App ID per game |

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
1. **Survey**: profile every game you own. Pick the first FULL one (Minecraft). Done for the 4 owned games.
2. **Bridge proof of concept**: uscl-bridge with lobby create/join/list and Rich Presence, driven from a small
   test client. No RPCS3 changes yet. *Started:* `uscl-bridge selftest` PASSes against real Steam (create
   lobby, lobby data, filtered search, owner/member, Rich Presence). A new lobby takes ~2 s to show up in
   search, so room searches need a retry or a short delay.
   *Done:* `uscl-bridge serve` (local JSON socket: create/search/join/leave rooms, room data, presence, pushed
   member/room-data/join-request events). `uscl-bridge client-test` PASSes through the socket on real Steam.
3. **uscl-rpcn** (no RPCS3 fork): local RPCN server fork with Steam-backed distributed rooms + the UDP
   relay, tested with Minecraft (a dump the player made). Steps: (a) stock RPCN running locally with an
   RPCS3 test config pointed at 127.0.0.1, so the player's real config is untouched; (b) auto-login; (c) room
   create/search/join through uscl-bridge; (d) signaling through the Steam relay. Done when two PCs join the
   same world through Steam.
4. Basic (invites/presence) + Score for FULL games. Then friends/invites only for PARTIAL games like Skate 3.
5. Cross-play research (Minecraft PS3 ↔ 360). Player is dumping their own 360 copy (JTAG) for Xenia Canary.
6. **Blaze-over-Steam**: a local stand-in for EA's Blaze server on each player's PC. The game talks Blaze to
   localhost; the stand-in answers using Steam: login = Steam account, game manager = Steam lobbies (lobby owner
   acts as host, with host migration when they leave), invites = Steam invites, game traffic = Steam P2P.
   Persistent data (stats, teams, uploaded parks/clips) stays on a real server or is left out. Blaze is shared by
   most 2009–2013 EA PS3 titles (Skate 3, Battlefield 3, FIFA, Madden, NFS, Mass Effect 3), so one engine covers
   many games, with per-game components on top. Test game: Skate 3. Protocol reference: the Skate3BlazeServer
   community server, which has no license, so it's used as reference only unless its author agrees.
   Activision's Demonware (Call of Duty) would be a second engine of the same shape.
