# Handoff (2026-10-23)

Start here if you're picking USCL up. Background: `PLAN.md` (design), `GAMES.md` (per-game status),
`AGENTS.md` (workflow), `games/<serial>.md` (details). Everything below is pushed on
`agent/jaiden-pc/steam-tunnel` (PR #4).

## Merge order
PRs #3 → #1 → #4 → #5 (all approved by the other agent). Then retarget #5 and later work to `main`.

## Where each piece stands
| Piece | State | Next step |
|---|---|---|
| Session tunnel (`uscl-bridge host` / `join`) | RPCN login over Steam works across two PCs (Linux host, Windows member). Survives Steam dropping the lobby. Joins only lobbies tagged `uscl=session` | a real game join across PCs |
| Port forwards (`host skate3`, `host tcp:…,udp:…`) | written, unit-tested, never run live | first live test with Skate 3 |
| uscl-rpcn (patches 0001–0004) | account auto-create, P2P addresses for the tunnel, auto-friend | – |
| Minecraft (BLUS31426) | two RPCS3 copies on one PC join the same world (normal RPCN) | the same join from a second PC over Steam |
| Skate 3 (BLUS30464) | community server via the bridge, untested; joiner's black screen unsolved | black screen first |
| Black Ops II (BLUS31011) | our Demonware (`uscl-dw`, patches 0001–0004): PS3 login, lobby, stats, playlists, matchmaking, NAT discovery: two copies played one match on one PC (`tools/autotest/`) | two PCs over the tunnel; service 32, PooledStorage |

### Black Ops II: read this before continuing
Solved 2026-10-23 (part 4 of the notes): the missing checks were the wad's `playlists_mp.info` and `leagues.info`.
What follows is the history.
The menu's "Connecting" popup waits for `IsDemonwareFetchingDone` (+ signed in). Its status comes from a bitmask
of online checks built by `00507950` (required mask `0x1bee` for multiplayer). Find which checks are still false
with our server and make the server provide what they need. Ruled out: NAT relay key, already-connected state,
Teams/Twitter/Facebook. This session stopped here because an automated safety check interrupted the analysis twice
at this exact step. Keep the work to what a private server for an owned copy needs; don't work around safeguards.
Alternatives if it stays stuck: the open-bitdemon-emulator community, or BO2's LAN Party carried by the tunnel.

## Tools in this repo
- `np-profile.py` — what PSN libraries a game uses (from RPCS3.log): FULL / PARTIAL / NONE.
- `unself.py` — decrypt PS3 SELFs headless; keys read from an RPCS3 checkout at run time. `--klic` for a game's
  extra executables (BO2: read 16 bytes at the address the game passes to `sceNpDrmIsAvailable`).
- `tools/ghidra/` — headless Ghidra scripts and how to set the PS3 TOC (essential).
- uscl-rpcs3 0002/0003 (debug build, see `uscl-rpcs3/README.md`): logpoints (`USCL_LOGPOINTS`), memory dumps
  (`!` logpoints + `USCL_MEMDUMP_DIR`), a GDB server that survives disconnects (memory read/write with `gdb`).
  The bundled LLVM doesn't build on Arch with GCC 16: the debug build is interpreter-only (`build-nollvm`).
- Network traces: start RPCS3 under `strace -f --seccomp-bpf -tt -e trace=connect,sendto,recvfrom,sendmsg,recvmsg
  -s 48 -xx -o net.strace` (ptrace_scope 1 allows tracing a child you start).
- A game's PSN imports without touching the real setup: boot it 45 s with a separate `XDG_CONFIG_HOME` whose
  `config.yml` has `Internet enabled: Disconnected`, then `np-profile.py` on that log.

## Local state on jaiden-pc (not in git)
- Forks: `~/src/rpcs3-uscl` (branch `uscl`, debug build in `build-nollvm`), `~/src/rpcn-uscl/.claude/worktrees/uscl`,
  `~/src/obde-uscl` (branch `uscl`, the Demonware fork). The repo's patch folders are the shareable copies.
- `~/.local/share/uscl/`: `rpcn/` (RPCN data; `rpcn.cfg` Port **31313 = normal mode** for local tests, **31413 +
  `USCL_LOCAL=1`** for the tunnel), `dw/` (dw-server data: `storage/publisher/18396/online_tu19_mp_english.wad`,
  the 16-byte file BO2 needs; `largeheatmap.raw.off` is disabled on purpose), RPCS3 configs `rpcs3-test`
  (uscltest, Minecraft 1.84), `rpcs3-test2` (uscltest2, bound to 127.0.0.2), `rpcs3-bo2` (BO2 1.19, IP swap list to
  our Demonware, a debug-only patch that disables BO2's hang watchdog), `rpcs3-probe` (network off).
- Run our Demonware: `cd ~/.local/share/uscl/dw && DW_AUTH_ADDR=127.0.0.1:3074 DW_LOBBY_ADDR=127.0.0.2:3074
  RUST_LOG=info ~/src/obde-uscl/target/release/dw-server`.
- Updates: `~/Downloads/GAME_DUMPS/updates/<serial>/` (Sony PKGs, SHA-1 verified; Sony's hash skips the last 32
  bytes).
