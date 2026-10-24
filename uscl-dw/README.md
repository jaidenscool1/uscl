# uscl-dw: our Demonware

A Demonware (Activision) stand-in so Call of Duty games play online without Activision's servers. It is
[open-bitdemon-emulator](https://github.com/Laupetin/open-bitdemon-emulator) (Rust, AGPL-3.0) plus the patches
here, kept as patches like uscl-rpcn.

## Build
```
git clone https://github.com/Laupetin/open-bitdemon-emulator uscl-dw && cd uscl-dw
git checkout 39391e7 && git am /path/to/uscl/uscl-dw/patches/*.patch
cargo build --release      # target/release/dw-server
```

## Run (host, next to uscl-rpcn)
PS3 games use port 3074 for both the auth and the lobby server, on two host names:
```
DW_AUTH_ADDR=127.0.0.1:3074 DW_LOBBY_ADDR=127.0.0.2:3074 DW_STUN_ADDR=127.0.0.3:3074 DW_STUN_ALT_ADDR=127.0.0.4:3074 \
  RUST_LOG=info target/release/dw-server
```
RPCS3 (Black Ops II): `Network → IP swap list`:
`ops2-ps3-auth.prod.demonware.net=127.0.0.1&&ops2-ps3-lobby.prod.demonware.net=127.0.0.2&&cod7-stun.us.demonware.net=127.0.0.3&&cod7-stun.eu.demonware.net=127.0.0.3&&cod7-stun.jp.demonware.net=127.0.0.3&&cod7-stun.au.demonware.net=127.0.0.3&&stun.us.demonware.net=127.0.0.3`
(the stun names are NAT type discovery, needed before Play Online; without them BO2 still uses Activision's).
RPCN must be the one the player logs in to (the login uses its ticket). 127.0.0.2 works on Linux; other systems
need the bridge to carry it (not done yet).

## Patches
- 0001: PS3 login (`Ps3ForMmpRequest`) as Black Ops II does it: the key comes from the PSN/RPCN ticket's cookie
  (see `games/BLUS31011.md`). `DW_AUTH_ADDR` / `DW_LOBBY_ADDR`.
- 0002: Stats service (4): every task answers "no rows" and logs the request (BO2 calls task 11 at startup).
- 0003: Teams service (3) stub; listing/filtering a player's own files; an unknown service id gets "not available"
  instead of ending the session.
- 0004: Teams, Twitter and Facebook all answered by one logging stub (`lobby/stub`).
- 0005: Matchmaking service (21): hosts create/update/delete sessions, searches return other players' open
  sessions (in memory; the title's own fields are passed through unchanged).
- 0006: NAT discovery (`cod7-stun`, UDP): `DW_STUN_ADDR` / `DW_STUN_ALT_ADDR`, replies like the live servers.
- 0007: NAT discovery passes the open-NAT test (mode 3 answered from the second server's IP, port+1; also
  listens on port+1), so titles report NAT Open; matchmaking removes a host's sessions when its connection closes.

## Status
Black Ops II 1.19: login, lobby, stats and **Play Online** work (2026-10-23). Publisher files go in
`storage/publisher/<title id>/`. BO2 needs `online_tu19_mp_english.wad` holding `leagues.info` and
`playlists_mp.info`, or Play Online times out (see `games/BLUS31011.md`, part 4). Build it from `bo2-wad/`:
`bo2-wad/mkwad.py storage/publisher/18396/online_tu19_mp_english.wad bo2-wad/leagues.info bo2-wad/playlists_mp.info`. Don't serve `largeheatmap.raw`: any file we've
tried makes BO2 drop the lobby connection right after, every 30 s. Missing services show as
`Tried to call unavailable service ...` in the log.
