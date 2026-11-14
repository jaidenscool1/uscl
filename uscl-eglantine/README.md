# uscl-eglantine: Nintendo Switch 2 (Eglantine)

| | |
|---|---|
| **Console** | Nintendo Switch 2 (USCL routes program IDs starting with `04` here; Switch games stay on [uscl-eden](../uscl-eden/README.md)) |
| **Upstream** | [Eglantine-Community/Eglantine](https://github.com/Eglantine-Community/Eglantine), branch `nx2-experimental`, the line its daily builds ship (GPL-3.0-or-later; an Eden fork with NX2/Switch 2 support). `nx2`, the default branch, lags it by hundreds of commits |
| **Pinned base** | `09cc3d1dc` (`nx2-experimental`, 2026-11-14) |
| **Our changes** | `patches/0001`–`0002`, applied with `git am`. We keep them on our fork only (no upstream PRs) |
| **Ships in** | not yet: build the core yourself (below) |
| **Online** | offline only; Eglantine has no `--uscl-room` yet (port uscl-eden 0003 next) |
| **Status** | Mario Kart World: boots from the USCL home screen, controller auto-mapped, Grand Prix race played. Nintendo Switch Sports Resort v1.1.0: boots past the safety screen, then freezes in the island flyover every time (emulator-side) |

No console keys, firmware or games are included. USCL keeps the core's data in `<profile>/data/eglantine` (`keys/prod.keys`, firmware NCAs in `nand/system/Contents/registered/`).

## Keeping up with upstream

The working fork is `~/src/eglantine`, branch `uscl` (origin = upstream). `python3 tools/emu-sync/emu-sync.py check eglantine` reports new upstream commits and whether any touch a file we changed; `apply --build eglantine` merges the clean ones and rebuilds `build-uscl`. After a merge, regenerate this folder: cherry-pick our commits onto `origin/nx2-experimental` in a scratch worktree, check `git diff uscl HEAD` is empty, and `git format-patch origin/nx2-experimental..HEAD`.

## Patches
- 0001 ports uscl-eden 0012 to Eglantine's SDL3 input driver: with `USCL_AUTO_PADS` set (the home screen sets it when Automatic Controller Setup is on) every connected game controller becomes players 1–8 in connection order, mapped with Eglantine's own per-controller defaults. Without it, player 1 stays on the keyboard.
- 0002 skips a wasted texture upload: when the copies that build a joined image already write every level and layer (Mario Kart World joins three GPU-written 2048×2048 depth targets into a 3-layer array every few seconds), JoinImages no longer unswizzles and uploads the old guest-memory contents first. Mario Kart World race on an RTX 4060: those uploads drop from ~40 per run to 1, about +4% average FPS in an A/B, no visual change.

## Build (Linux)

Same dependencies as uscl-eden (Qt 6 incl. Charts, SDL3 is bundled, FFmpeg, OpenSSL, clang). On a checkout of the pinned base (or `uscl` in the working fork, which already has them):

```
git am <uscl>/uscl-eglantine/patches/*.patch
cmake -S . -B build-uscl -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ \
  -DENABLE_QT=ON -DYUZU_CMD=ON -DYUZU_TESTS=OFF -DBUILD_TESTING=OFF -DYUZU_ROOM=OFF -DYUZU_ROOM_STANDALONE=OFF \
  -DENABLE_WEB_SERVICE=OFF -DENABLE_UPDATE_CHECKER=OFF -DUSE_DISCORD_PRESENCE=OFF -DYUZU_USE_BUNDLED_FFMPEG=OFF \
  -DYUZU_DISABLE_LLVM=ON -DVulkanHeaders_FORCE_BUNDLED=ON
cmake --build build-uscl --target yuzu yuzu-cmd
```

Without a system Qt Charts, add `-DQt6Charts_DIR=<prefix>/lib/cmake/Qt6Charts -DCMAKE_PREFIX_PATH=<prefix>` pointing at an extracted `qt6-charts` package in a lasting location (the binary's RUNPATH keeps that path). Put `build-uscl/bin/eglantine` on PATH as `eglantine` or set `USCL_SWITCH2_CORE`. The upstream release AppImage also runs, but without 0001 (keyboard only).
