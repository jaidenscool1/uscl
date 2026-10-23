# uscl-rpcs3

USCL's RPCS3 build: [RPCS3/rpcs3](https://github.com/RPCS3/rpcs3) (GPL-2.0) plus the patches in `patches/`.
Only changes the server side can't make go here: game-specific sceNp fixes, an NP call trace for working out
each game, and turning Steam invites into in-game invites. Rooms, friends, presence and P2P stay in uscl-rpcn.

## Build
```
git clone --recursive https://github.com/RPCS3/rpcs3.git && cd rpcs3
git checkout e53ac18            # the commit these patches were made against
git am ../patches/*.patch
```
Then build as in RPCS3's `BUILDING.md`.

## Patches
- 0001: send `CELL_SYSUTIL_NP_INVITATION_SELECTED` after `sceNpBasicRecvMessageCustom` picks an invite. Skate 3 waits for it
  before fading in after accepting an invite. Untested in game.
- 0002: logpoints for working out what a game does. `USCL_LOGPOINTS=<file>`, lines `PPU-<hash> <hex address> <name>`
  (the hash is the log's `PPU executable hash`). Needs `PPU Decoder: Interpreter (static)`. The first call logs name,
  LR and r3–r5 and the game carries on; a name starting with `*` logs every call; ending in `@rN` also logs the string
  rN points to. Unlike breakpoints it never pauses, so games with a hang watchdog keep running.

- 0003: the GDB server survives a client leaving (a closed connection or `D` used to be a fatal error that froze
  emulation; now the session ends and the game resumes). Reading/writing memory with `gdb` works with any decoder;
  breakpoints need the interpreter. A logpoint named `!...` writes all mapped game memory to `$USCL_MEMDUMP_DIR`
  (one file per range, named by start address) when hit.

## Building (Arch, 2026-10)
The bundled LLVM doesn't compile with GCC 16's libstdc++ and `llvm-ar` isn't installed, so the debugging build is
interpreter-only: `cmake -S . -B build-nollvm -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=clang
-DCMAKE_CXX_COMPILER=clang++ -DWITH_LLVM=OFF -DUSE_LTO=OFF -DUSE_SYSTEM_FFMPEG=ON && ninja -C build-nollvm`.
Set `SPU Decoder: Recompiler (ASMJIT)` for games run with it.
