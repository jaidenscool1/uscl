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
Everything on 127.0.0.1 (works behind the session tunnel on every OS):
```
DW_ADDR=127.0.0.1:3074 DW_STUN_ADDR=127.0.0.1:3074 DW_STUN_ALT_ADDR=127.0.0.1:3077 RUST_LOG=info target/release/dw-server
```
RPCS3 (Black Ops II; MW2's names are in `games/BLUS30377.md`): `Network → IP swap list` (auth, lobby and the NAT
discovery names):
`ops2-ps3-auth.prod.demonware.net=127.0.0.1&&ops2-ps3-lobby.prod.demonware.net=127.0.0.1&&cod7-stun.us.demonware.net=127.0.0.1&&cod7-stun.eu.demonware.net=127.0.0.1&&cod7-stun.jp.demonware.net=127.0.0.1&&cod7-stun.au.demonware.net=127.0.0.1&&stun.us.demonware.net=127.0.0.1`
Without the stun names BO2 still asks Activision for its NAT type. RPCN must be the one the player logs in to (the
login uses its ticket). The older two-address setup (`DW_AUTH_ADDR` / `DW_LOBBY_ADDR`) still works.

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
- 0008: `DW_ADDR`: auth and lobby on one address (routed by the first message); stun binds only what it answers from.
- 0009: `DW_STUN_SELF_ADDR`: the tunnel host's own game is reported at its slot address (see `games/BLUS31011.md`).
- 0010: PooledStorage (58) answered by the logging stub (BO2 calls it at match start, probably Theater uploads).
- 0011: Anticheat: BO2's code checks (push type 19) every `DW_AC_INTERVAL` s (300), judged against the answers of
  `DW_TRUSTED_USER` (the host); a player whose game code differs is refused (see `games/BLUS31011.md`). Lobby
  servers can send push messages after a reply (`PushSource`).
- 0012: Stats check: an `mpstatsCompressed` upload with prestige/rank/XP past the maximum, a prestige jump or more XP
  than one match gives is refused (layout: `ddl_mp/stats.ddl` v244, see `games/BLUS31011.md`).
- 0013: Modern Warfare 2 (title 5280, `lobby/mw2.rs`): its older Demonware (batched frames, bit-packed tasks and
  replies), publisher files by id, matchmaking and the bandwidth test (see `games/BLUS30377.md`).

## Status
Modern Warfare 2 1.14: login, playlists and matchmaking work; two copies played the same match (2026-10-25).
Publisher files `playlists.patch3`, `motd-english.txt` and `messageoftheday.info` from `mw2-files/` go in
`storage/publisher/5280/`. RPCS3's swap list also needs MW2's names (the USCL panel sets them).

Black Ops II 1.19: login, lobby, stats and **Play Online** work (2026-10-23). Publisher files go in
`storage/publisher/<title id>/`. BO2 needs `online_tu19_mp_english.wad` holding `leagues.info` and
`playlists_mp.info`, or Play Online times out (see `games/BLUS31011.md`, part 4). Build it from `bo2-wad/`:
`bo2-wad/mkwad.py storage/publisher/18396/online_tu19_mp_english.wad bo2-wad/leagues.info bo2-wad/playlists_mp.info`. Don't serve `largeheatmap.raw`: any file we've
tried makes BO2 drop the lobby connection right after, every 30 s. Missing services show as
`Tried to call unavailable service ...` in the log.
