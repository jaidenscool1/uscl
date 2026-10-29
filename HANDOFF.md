# Handoff (updated 2026-10-28)

Start here if you're picking USCL up. Background: `PLAN.md` (design), `GAMES.md` (per-game status),
`AGENTS.md` (workflow), `games/<serial>.md` (details). Everything below is pushed on
`agent/jaiden-pc/p2p-ports` (PR #7; newest state: "Pick up here" at the end).

## Picking this up on another machine / account
```
git clone https://github.com/jaidenscool1/uscl.git && cd uscl
git checkout agent/jaiden-pc/p2p-ports
```
Everything is in this branch except what can't be public:
- **Update signing key** (`~/.config/uscl-signing/update-key.pem`): stays on jaiden-pc. Releases must be signed
  there (`tools/dev-scripts/release.sh`); without it, build and test only.
- **Discord Social SDK** (licence forbids redistribution): download 1.10.x from the Discord developer portal
  (app "USCL", ID `1563994423862231191`, Public Client on, redirect `http://127.0.0.1/callback`) and extract to
  `~/src/discord-social-sdk/sdk/discord_social_sdk`, or set `USCL_DISCORD_SDK`. Without it party chat is just
  hidden. CI gets it from the `ci-cache` draft release (`discord-social-sdk-*.zip`).
- **Game files, licences, decrypted ELFs, PS3 sound rips**: never in git. Bring your own dumps.
- **The forks' working trees** (RPCS3, RPCN, Demonware): rebuild them from upstream + the repo's patch folders
  (`uscl-rpcs3/patches`, `uscl-rpcn/patches`, `uscl-dw/patches`).

**GitHub budget:** batch commits, push once per chunk, and ask the owner before tagging a `v*-test` release
(each tag runs the Windows installer build). `build.yml` only runs for `uscl-bridge/**` changes. The installer
build caches RPCS3 and the servers in the `ci-cache` draft release (keyed by source hashes, old ones pruned), so
an unchanged RPCS3 isn't rebuilt. Latest release: `v0.3.3-test`.

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
| Black Ops II (BLUS31011) | **a full match between two PCs over Steam (2026-10-27, Linux host + Windows member, PR #7)**: our Demonware (`uscl-dw` 0001–0014), Find Match, quality test, game traffic through the tunnel | names missing in the pre-match lobby (fine in game); invites / join-friend still refused by the game; graphical glitches and CPU load (per-game RPCS3 settings) |
| Modern Warfare 2 (BLUS30377) | our Demonware (uscl-dw 0013, bit-packed MW2 lobby); two copies on one PC played one match | two-PC test (same tunnel path as BO2, which now works) |
| Gotham City Impostors (NPUB30252) | boots with the full-game unlock; online is GameSpy (`sirenps3`) | `games/NPUB30252.md` |
| USCL home screen (`uscl-shell`) | XMB, Add Games (finds decrypted folders, says when a game is still encrypted), per-game update check + install from Sony's update server (Game Updates), Friends tab drawn like the PS3's with Steam avatars/levels, Network column opens on RPCN / USCL status (no Host / Join: games go online when started), Party column drives the Discord app on the PC over its local RPC socket (`discordparty.cpp`): the voice chat you are in, who is there and talking, your servers and who is in their voice channels, join / disconnect / mute / deafen. Sign-in once via the Social SDK with the rpc scopes, which Discord grants only to the app owner and App Testers until it approves the app, starts Steam minimized; PS in a game shows the home screen while the game keeps running (Resume / Close Game, "close the running game?" when starting another; RPCS3 0008) | PS button still opens Steam Big Picture (Steam setting "Guide Button Focuses Steam"); two-PC test |
| Windows installer | Inno Setup, per user; offers Steam / Discord when missing; self-update signed with the offline key; RPCS3 patch 0007 removes the "unofficial build" warning and RPCS3's own updater | – |

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
- Run our Demonware: `cd ~/.local/share/uscl/dw && DW_ADDR=127.0.0.1:3074 DW_STUN_ADDR=127.0.0.1:3074
  DW_STUN_ALT_ADDR=127.0.0.1:3077 RUST_LOG=info ~/src/obde-uscl/target/release/dw-server` (configs rpcs3-bo2/a/b point
  every Demonware name at 127.0.0.1; behind the tunnel the stun goes 100 up, `DW_STUN_ADDR=127.0.0.1:3174 DW_STUN_ALT_ADDR=127.0.0.1:3177`, because the bridge owns 3074/3077/3078).
- Updates: `~/Downloads/GAME_DUMPS/updates/<serial>/` (Sony PKGs, SHA-1 verified; Sony's hash skips the last 32
  bytes).
- Test profiles for the USCL AppImage build: `~/.config/uscl/` (dev_hdd0, licences in `home/00000001/exdata`),
  log `~/.cache/uscl/RPCS3.log`. Its pad config is empty until set in the GUI.
- `tools/dev-scripts/`: the helper scripts used for the tests above (paths assume this PC).
- A second Windows PC is on the LAN for the two-PC BO2/MW2 test: it needs OpenSSH Server enabled first.

## State at end of 2026-10-27 (PR #7, not yet tested together)
- Online like a PS3 (PLAN.md phase 7 a+b): starting a supported game opens the player's own session; joining a friend
  leaves it. Built and self-tested; the two-PC test is still to do.
- Bridge: a member's new RPCN login replaces their old connection on the host (fixes "user already connected").
- RPCN `HostIPv6` is now `::ffff:127.0.0.1` (packaging). `::1` meant the signaling ping to 127.0.0.1 never reached
  RPCN, so whoever hosted from an installed USCL got an RPCN error at Multiplayer. The shell now refreshes
  `rpcn.cfg` on every host so old copies get fixed.
- Hosting needs the shell, bridge and dw-server from the same version: a v0.3.3 shell starts dw-server on the ports the
  new bridge owns (3074/3077/3078), so BO2's NAT check fails. The Windows test PC needs this branch's installer.
- Emblem editor still says "error accessing the file manager" (parked; see games/BLUS31011.md). Still open:
  "Matched Player" names, CPU load for all games.

## Pick up here: state at end of 2026-10-28 (PR #7 branch, pushed with `[skip ci]`)
**Done and tested (two PCs over Steam, Linux host + Windows member):**
- BO2 PS4 (CUSA57548, `games/CUSA57548.md`) online on our shadPS4 fork (`uscl-shadps4`), shadNet fork
  (`uscl-shadnet`) and uscl-dw 0019 (auth3 sign-in, title 2430). Bridge preset `bo2ps4`. Public FFA matches
  host <-> member, back to back, stats saved.
- USCL app runs PS4 games: `uscl/ps4_games.yml` list, launcher starts shadPS4 (`USCL_SHADPS4` or next to the app)
  in `<USCL>/uscl/shadps4` (`Online::preparePs4` writes users.json), hosting starts shadNet + dw auth3. A member
  joined an app-hosted session (2 matches). Linux: the app's servers die with it (`PR_SET_PDEATHSIG`).
- DLC page in the app (`src/dlc.cpp`): lists a PS3 game's DLC (installed / licensed), installs the player's own
  `.pkg` / `.rap` files.
- Bridge: `USCL_DUMP_UDP=<file>` records game <-> game packets (developer capture, off by default).
- shadPS4 fork: HLE `strncpy_s` honours `count` on Linux/macOS; PS4 objective banner "garbling" is BO2's decode
  animation (not a bug).

**Open, in the order the owner asked for:**
1. **Emblem editor (PS3 BO2):** the popup appears right when opening Barracks -> Create Emblem. Round-3 notes in
   `games/BLUS31011.md`: the slots check (`0x0051ce2c`) sends `valid = 1` with the state read earlier, so the popup
   comes from another `valid` event of the File Manager menus. Next: break in GDB on the senders of the File
   Manager events while opening Create Emblem.
2. **USCL home screen work (#4):** agree on the approach with the owner first.
3. **BO2 PS4 friends / invites (#10):** the in-game friends list is empty in the shipped PS4 game itself
   (stubbed calls, `games/CUSA57548.md`); joining friends on PS4 goes through PlayerSessions, which shadPS4 stubs.
- PS3 <-> PS4 crossplay: parked (it would need the bridge inside the games' encrypted peer connection).
- Windows build of the app with shadPS4/shadNet: needs a CI run (owner's go-ahead; Actions budget).
- RPCS3 CPU load / per-game config: owner said leave for now.

**Local only (jaiden-pc), rebuild from the patch folders elsewhere:** shadPS4 fork `~/src/shadps4-uscl` (base
3eecea9 + `uscl-shadps4/patches`), shadNet fork `~/src/shadnet-uscl` (6f534a1 + `uscl-shadnet/patches`, needs
QtHttpServer: `~/.local/qt6-extra`), Demonware fork `~/src/obde-uscl/.claude/worktrees/anticheat` (39391e7 +
`uscl-dw/patches` 0001-0019). Reverse-engineering folders `~/src/bo2-re` (PS3) and `~/src/bo2-ps4-re` (PS4: Ghidra
projects, fastfile unpacker, 2-PC kit and TESTLOG.md). Windows test PC: `C:\USCL-PS4` with an agent mailbox in
`C:\USCL-PS4\agents` (to-pete.md / from-pete.md, last message 12).

## Update 2026-10-29: BO2 PS3 <-> PS4 crossplay groundwork (all pushed, `[skip ci]`)
**Done and tested:**
- "Plain peer data" game patches: the games' peer DTLS DATA is sent unencrypted (the Steam tunnel encrypts between
  PCs) so the bridge can translate it. PS4: `uscl-shadps4/mk_mm_patch.py`. PS3: RPCS3 fork patch 0009 (USCL's own
  patches are built in, keyed by PPU hash, and apply also with `USCL_ONLINE`).
- Two PCs over Steam (Linux host + Windows member), patched on both sides: **PS4** public FFA on Raid (02:59) and
  **PS3** Team Deathmatch on Yemen (05:39) both ran, full bars, traffic readable in the bridge's `USCL_DUMP_UDP`.
  On one PC two PS3 copies can't start a match (host load stalls; the unpatched official build fails the same).
- Protocol findings in `games/CUSA57548.md`: DTLS layer identical on both builds; payload frame `u16 0x0457`
  (native order) + flags; PS3 adds an RFC 1071 checksum (verified on all 7064 PS3 packets of the two-PC capture,
  lobby and in-game); same out-of-band command set; party command tables mapped; Huffman table identical.
- Bridge (`uscl-bridge/src/crossplay.rs`, wired into delivery): the receiving bridge knows its game's build from the
  preset (`bo2` = PS3, `bo2ps4` = PS4) and the sender's from the frame byte order, and translates the frame.
  Same-build traffic is untouched.

**Crossplay status (2026-10-29 07:10, two PCs: PS3 host on Linux, PS4 member on Pete's PC):** the whole lobby
works (join, party state, profiles, heartbeats, the host's `go`), the PS4 connects to the PS3 server
(connectResponse, stats) and loads the map, then disconnects itself while parsing the gamestate (in-game layer).
Pieces, all pushed: uscl-dw 0020 (security ID), RPCS3 0009/0010 (plain peer data, little-endian messages, built
in), `uscl-shadps4/mk_mm_patch.py` (plain peer data, PS3 party layout), `uscl-bridge/src/crossplay.rs` (frame,
QoS, joinParty, connect; bridges announce their build in lobby member data and translate what they receive).
Details and addresses: `games/CUSA57548.md`. Test scripts (job tmp, not in git): host_x.sh (servers + bridge with
`USCL_GAME=BLUS31011`), round.sh (one full round incl. Pete's PS4 via scheduled tasks + key helper).
**Next for crossplay (in-game):** make the PS4's netfield tables PS3's (order, bits, encoder ids; offsets by field
name) as a PS4 data patch, then update the PS4 netcode constant in crossplay.rs (it is the tables' checksum); then
whatever the gamestate shows next (configstring / asset indexes).

**Next for crossplay (older notes):** uscl-dw must let PS3 (T6Ps3) and PS4 (title 2430) sessions find each other; protocol
2093 vs 2097 in the connect/lobby messages; per-message translation of binary messages (`ph`, `pseg`, `b`,
`profiles`, ...; handler pairs in `games/CUSA57548.md`), then in-game snapshots (netfield order/encoder ids, asset
ids). Captures for that: RE folder `crossplay/ps3-2pc-plain.udp`, `ps4-2pc-plain.udp` (bridge dump format).
- Pete's PC: `%LOCALAPPDATA%\Programs\USCL\patches\imported_patch.yml` holds the PS3 patch (inactive; enabling it =
  `config\patch_config.yml` entry, see message 14/17 in `C:\USCL-PS4\agents`). His RPCS3 needs
  `Shader Mode: "Async Recompiler (multi-threaded)"` when started outside the app (interpreter precompile stalls).
