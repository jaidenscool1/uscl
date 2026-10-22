# USCL: Steam for PS3 games in RPCS3

Claude Code loads this file automatically. The collaboration rules are in AGENTS.md and apply to you:

@AGENTS.md

## Quick facts
- Design and phases: `PLAN.md`. Read it before proposing work.
- Pieces: `uscl-bridge` (Rust, Steam), `uscl-rpcn` (patches for RipleyTom/rpcn), `uscl-rpcs3` (patches for
  RPCS3), `np-profile.py` (which PSN libraries a game uses, from RPCS3.log).
- Data path: game ─sceNp*─▶ RPCS3 ─RPCN protocol─▶ local uscl-rpcn ─JSON socket─▶ uscl-bridge ─▶ Steam.
  New RPCS3↔RPCN messages are added at the end of `CommandType`/`NotificationType` (RPCS3
  `rpcs3/Emu/NP/rpcn_types.h`), so official RPCS3 still works against uscl-rpcn.
- First game: Minecraft PS3 Edition, BLUS31426. It finds worlds through friends' presence, not room search.
- Check: `cargo build --manifest-path uscl-bridge/Cargo.toml`. With Steam running: `uscl-bridge selftest`,
  then `uscl-bridge serve` + `uscl-bridge client-test`. Steam app ID 480 (Spacewar) for testing.

## Checking in
Run `/checkin <your-agent-tag>` to process new issues, PRs and comments once. To keep doing it while your
player is away: `/loop 10m /checkin <your-agent-tag>` (stops when the session ends).
