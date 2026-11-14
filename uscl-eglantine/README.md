# uscl-eglantine: Nintendo Switch 2 (Eglantine)

| | |
|---|---|
| **Console** | Nintendo Switch 2 (USCL routes program IDs starting with `04` here; Switch games stay on [uscl-eden](../uscl-eden/README.md)) |
| **Upstream** | [Eglantine-Community/Eglantine](https://github.com/Eglantine-Community/Eglantine), branch `nx2` (GPL-3.0-or-later; an Eden fork with NX2/Switch 2 support) |
| **Pinned base** | `0a2323e4faae8f03978dc722c9f0db451c2224f1` (`daily-20261114`) |
| **Our changes** | `patches/0001`, applied with `git am` |
| **Ships in** | not yet: build the core yourself (below) |
| **Online** | offline only; Eglantine has no `--uscl-room` yet (port uscl-eden 0003 next) |
| **Status** | Mario Kart World: boots from the USCL home screen, controller auto-mapped, Grand Prix race played. Nintendo Switch Sports Resort v1.1.0: boots past the safety screen, then freezes in the island flyover every time (emulator-side) |

No console keys, firmware or games are included. USCL keeps the core's data in `<profile>/data/eglantine` (`keys/prod.keys`, firmware NCAs in `nand/system/Contents/registered/`).

## Patches
- 0001 ports uscl-eden 0012 to Eglantine's SDL3 input driver: with `USCL_AUTO_PADS` set (the home screen sets it when Automatic Controller Setup is on) every connected game controller becomes players 1–8 in connection order, mapped with Eglantine's own per-controller defaults. Without it, player 1 stays on the keyboard.

## Build (Linux)

Same dependencies as uscl-eden (Qt 6 incl. Charts, SDL3 is bundled, FFmpeg, OpenSSL, clang). On a checkout of the pinned base:

```
git am <uscl>/uscl-eglantine/patches/*.patch
cmake -S . -B build-uscl -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ \
  -DENABLE_QT=ON -DYUZU_CMD=ON -DYUZU_TESTS=OFF -DBUILD_TESTING=OFF -DYUZU_ROOM=OFF -DYUZU_ROOM_STANDALONE=OFF \
  -DENABLE_WEB_SERVICE=OFF -DENABLE_UPDATE_CHECKER=OFF -DUSE_DISCORD_PRESENCE=OFF -DYUZU_USE_BUNDLED_FFMPEG=OFF \
  -DYUZU_DISABLE_LLVM=ON -DVulkanHeaders_FORCE_BUNDLED=ON
cmake --build build-uscl --target yuzu yuzu-cmd
```

Without a system Qt Charts, add `-DQt6Charts_DIR=<prefix>/lib/cmake/Qt6Charts -DCMAKE_PREFIX_PATH=<prefix>` pointing at an extracted `qt6-charts` package in a lasting location (the binary's RUNPATH keeps that path). Put `build-uscl/bin/eglantine` on PATH as `eglantine` or set `USCL_SWITCH2_CORE`. The upstream release AppImage also runs, but without 0001 (keyboard only).
