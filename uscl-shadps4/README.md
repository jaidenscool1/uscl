# uscl-shadps4: PlayStation 4 (shadPS4)

| | |
|---|---|
| **Console** | PlayStation 4 |
| **Upstream** | [shadps4-emu/shadPS4](https://github.com/shadps4-emu/shadPS4) (GPL-2.0) |
| **Pinned base** | `3eecea9` |
| **Our changes** | `patches/0001-uscl.patch`, one diff applied with `git apply` (becomes a `git am` series later) |
| **Ships in** | the Linux AppImage when built with it (`USCL_SHADPS4_BUILD`); not in the Windows installer yet |
| **Online services it talks to** | [uscl-shadnet](../uscl-shadnet/) (sign-in), [uscl-dw](../uscl-dw/) (Call of Duty) |
| **Status** | Black Ops II (`CUSA57548`) played between two PCs over Steam; PS3 ↔ PS4 crossplay in progress |

USCL's PS4 emulator. Sign-in goes to shadNet, Call of Duty traffic to our Demonware, both through `uscl-bridge`
like the PS3 games. Controllers: shadPS4 gives players 1–4 the connected pads in order (the app defines the four
users in `users.json`).

## Build
```
git clone --recursive https://github.com/shadps4-emu/shadPS4.git && cd shadPS4
git checkout 3eecea9            # the commit the patch was made against
git apply ../patches/0001-uscl.patch
```
Then build as in shadPS4's docs. `0001-uscl.patch` is one diff for now (`git apply`); it becomes a
`git format-patch` series when the fork is committed.

## Patch
- Event flags: a woken waiter leaves the queue at once (it used to match the next set again and eat bits; lost
  wakeups froze BO2's loading).
- POSIX semaphores in the PS4's 16-byte guest layout.
- Game modules the eboot needs (`sce_module/*.prx`) load with the game, as on a PS4 (BO2 uses libSceNpCppWebApi
  that way).
- libSceLibcInternal: `__cxa_guard_*`, `malloc`/`free`/... and the `sceLibcMspace*` family (NpToolkit2 shut down
  when they were stubs).
- `USCL_HOSTS=name=ip&&*.suffix=ip`: name lookups (resolver and HTTP) go to our servers, with certificate checks
  off for those names only. `USCL_BIND_ADDR`: wildcard binds use that address (127.0.0.2), so the bridge can own
  127.0.0.1's game ports.
- NpAuth id token `uscl:<npid>:<account>` for our Demonware's auth3.
- `USCL_LOG_IMPORTS=1` logs every import slot -> function (for working out a game).

## data/
Templates the USCL app copies to `<USCL folder>/shadps4` (shadPS4 uses `./user` when it exists):
- `user/config.json`: network on, shadNet at `127.0.0.1:31513` / web API `31515` (the bridge carries both to the
  host's shadNet).
- `user/patches/uscl/`: made at build time by `mk_mm_patch.py` (BO2 PS4 `CUSA57548`: public-match search gives up
  after 4 stages so a party hosts its own lobby, and the "online needs dedicated servers" check is skipped; game-to-game data is sent unencrypted, since the Steam
  tunnel encrypts it, so the bridge can translate it for PS3 crossplay).

The app writes `user/users.json` itself: player 1 is signed in to shadNet as the player's online name.
