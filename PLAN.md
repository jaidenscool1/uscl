# USCL: The Ultimate Steam Compatibility Layer

Goal: PS3 games (your own dumps) running in RPCS3 use Steam for friends, invites, lobbies and P2P, without
patching each game. Status: Steam bridge service working (see Phases).

## How it works
PS3 games never talk to PSN directly. They call Sony's libraries (`sceNp*`). RPCS3 already answers those
calls itself, backed by RPCN (its PSN replacement). USCL makes Steam the network between players:

```
                       every player's PC                                       host's PC (Steam lobby owner)
game ─sceNp*─▶ RPCS3 ─TCP 31313/UDP 3657─▶ uscl-bridge ══ Steam (Valve relay) ══▶ uscl-bridge ─▶ uscl-rpcn (31413/3757)
                 ▲  game UDP to 127.0.0.1:39000+slot      │                  ▲
                 └───────────────────────────────────────┴── straight to that member over Steam
```

- **Session** (decided 2026-10-22): a Steam lobby (up to 250). Its owner is the host and runs uscl-rpcn; every
  member's bridge carries RPCS3's RPCN connection to it. All of RPCN (rooms, friends, presence, invites,
  scores, TUS) works unchanged for every game RPCN supports, with any number of players the game allows.
- **P2P**: each member gets a slot. To every RPCS3, member `s` is `127.0.0.1:39000+s`: the host's bridge sends
  that member's RPCN UDP from that port (so RPCN hands it out as their address), and every bridge listens on
  it and relays to that member. Game traffic goes member to member, not through the host. No port forwarding.
- **Works everywhere RPCS3 runs with Steam**: only 127.0.0.1 and ports, no per-OS networking (Windows, Linux,
  macOS, Steam Deck). Official RPCS3 works too; uscl-rpcs3 only adds game fixes and invite handling.
- **Trade-off**: the session ends when the host leaves (the host is usually the world host anyway). A host-less
  version (each player's uscl-rpcn owning its rooms, mirrored in lobbies) needs most of RPCN's room code
  rebuilt across PCs; it stays a later option if host loss becomes a real problem.
- **No Steam** (Android forks, BSD): the bridge's Steam transport could get a plain UDP sibling later.
- **uscl-rpcs3** (decided 2026-10-22): our own RPCS3 build, patches in `uscl-rpcs3/`. Only for what the server
  can't do: per-game sceNp fixes, an NP call trace for working out each game, and turning Steam invites into
  in-game invites. New RPCS3↔RPCN messages go at the end of the RPCN protocol's command/notification lists,
  so official RPCS3 keeps working against uscl-rpcn without the USCL extras.
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
| PARTIAL | PSN only for sign-in/invites; matchmaking on the publisher's servers | Skate 3 (BLUS30464), Black Ops II (BLUS31011): Basic, Manager, Lookup, no Matching2 |
| NONE | no PSN multiplayer | Spider-Man: Shattered Dimensions / Edge of Time |

PARTIAL games still need a community server for that publisher (EA's Blaze for Skate 3, Battlefield, FIFA;
Activision's Demonware for Call of Duty; Rockstar's own for GTA/RDR). Steam can only add friends/invites on top.
Where one exists, the host can run it and the session tunnel can carry its ports, so nobody port-forwards
(`games/BLUS30464.md`).

First three games: Minecraft (FULL), Skate 3 and Black Ops II (PARTIAL). Notes per game in `games/<serial>.md`.

### The "top 10" list
The live list of games, their status and what's next is in `GAMES.md`.
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
   *Windows (2026-10-22):* same three commands PASS on Windows 11 with real Steam. There the socket is TCP
   `127.0.0.1:47321` (`USCL_BRIDGE_ADDR` overrides it), since Rust's std has no Unix sockets on Windows. Build
   with the rustup GNU toolchain (`x86_64-pc-windows-gnu`, no Visual Studio needed) and copy
   `target/<profile>/build/steamworks-sys-*/out/steam_api64.dll` next to `uscl-bridge.exe`.
3. **uscl-rpcn**: local RPCN server fork with Steam-backed distributed rooms + the UDP
   relay, tested with Minecraft (a dump the player made). Steps: (a) stock RPCN running locally with an
   RPCS3 test config pointed at 127.0.0.1, so the player's real config is untouched; (b) auto-login; (c) room
   create/search/join through uscl-bridge; (d) signaling through the Steam relay. Done when two PCs join the
   same world through Steam.
   *(a) + (b) done 2026-10-22:* official RPCS3 + Minecraft PS3 logs in to the local uscl-rpcn with no
   registration (see `uscl-rpcn/README.md`).
   *(c) + (d) replaced by the session tunnel (2026-10-22):* `uscl-bridge host` / `join`. On one PC: Minecraft logs
   in through the bridge and the UDP signaling round trip works (host path). Next: two PCs in one world.
4. Basic (invites/presence) + Score for FULL games. Then friends/invites only for PARTIAL games like Skate 3.
4b. **Server over the tunnel** for PARTIAL games with a community server: the host runs the server, and the
   bridge forwards its TCP/UDP ports from every member's loopback. First: Skate 3 (`games/BLUS30464.md`).
5. Cross-play research (Minecraft PS3 ↔ 360). Player is dumping their own 360 copy (JTAG) for Xenia Canary.
6. **Blaze-over-Steam**: a local stand-in for EA's Blaze server on each player's PC. The game talks Blaze to
   localhost; the stand-in answers using Steam: login = Steam account, game manager = Steam lobbies (lobby owner
   acts as host, with host migration when they leave), invites = Steam invites, game traffic = Steam P2P.
   Persistent data (stats, teams, uploaded parks/clips) stays on a real server or is left out. Blaze is shared by
   most 2009–2013 EA PS3 titles (Skate 3, Battlefield 3, FIFA, Madden, NFS, Mass Effect 3), so one engine covers
   many games, with per-game components on top. Test game: Skate 3. Protocol reference: the Skate3BlazeServer
   community server, which has no license, so it's used as reference only unless its author agrees.
   Activision's Demonware (Call of Duty) would be a second engine of the same shape.
   **Goal (player, 2026-10-23):** online services that work like Steam's for every such game, bypassing the
   publisher's servers completely: our own stand-ins for Blaze (EA) and Demonware (Activision), backed by Steam.
   Black Ops II confirmed the need: Demonware is up but refuses RPCN tickets (`games/BLUS31011.md`).
