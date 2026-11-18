# uscl-eglantine: Nintendo Switch 2 (Eglantine)

| | |
|---|---|
| **Console** | Nintendo Switch 2 (USCL routes program IDs starting with `04` here; Switch games stay on [uscl-eden](../uscl-eden/README.md)) |
| **Upstream** | [Eglantine-Community/Eglantine](https://github.com/Eglantine-Community/Eglantine), branch `nx2-experimental`, the line its daily builds ship (GPL-3.0-or-later; an Eden fork with NX2/Switch 2 support). `nx2`, the default branch, lags it by hundreds of commits |
| **Pinned base** | `30181ba05` (`nx2-experimental`, 2026-11-15) |
| **Our changes** | `patches/0001`–`0009`, applied with `git am`. We keep them on our fork only (no upstream PRs) |
| **Ships in** | both installers (Windows: `Eglantine\`); the AppImage's is the optimized build without `-march=native` |
| **Online** | offline only; Eglantine has no `--uscl-room` yet (port uscl-eden 0003 next) |
| **Status** | Mario Kart World: boots from the USCL home screen, controller auto-mapped, Grand Prix races play; pre-race scenes 60 fps, racing ~41–42 fps with the optimized build (below). Nintendo Switch Sports Resort v1.1.0: boots and plays its intro at 60 fps, then freezes (a GPU/present deadlock, see Open issues) |

No console keys, firmware or games are included. USCL keeps the core's data in `<profile>/data/eglantine` (`keys/prod.keys`, firmware NCAs in `nand/system/Contents/registered/`).

## Performance (Mario Kart World, i5-10600K + RTX 4060 8 GB, Linux, NVIDIA 615.78)

Grand Prix race driven by the debug server, fps averaged over 5 s chunks. "Racing" is the part after the start (the game picks 60 or 30 fps per frame; it drops toward 30 when a frame misses 16.7 ms).

| Build | Pre-race | Racing | Frames over 100 ms per run (boot + menus + race) |
|---|---|---|---|
| 2026-11-14: patches 0001–0006 on `25fc116c3` | not measured separately | 28–35 | 20–26 |
| 2026-11-15: + upstream `30181ba05` + 0007, plain Release build | 60 | 35.4–35.6 | 16–22 |
| same, built `-march=native` + PGO + ThinLTO for video_core (0008, recipe below) | 60 | **41.2–42.0** | 15–21 |

Where a racing frame's time goes now (perf, 20 s of racing on the optimized build): the GPU back thread, which runs the game's GPU methods and records the host draws, is ~82% busy (~8% of that spin-waiting), about 18.5 ms per frame against 16.7 ms for 60 fps. The emulated CPU cores are ~50% busy each, the GPU front thread ~59% and the host GPU ~35–40%. The back thread's time is spread thinly: draw setup is ~59% of it (binding textures, uniform/storage buffers and descriptors per draw), with no single function above a few percent. 60 fps racing needs that thread (and then the game's own CPU work, ~14–18 ms per frame in the JIT) under 16.7 ms.

Tried and not kept (measured in the same race): `EDEN_NX2_FRAMES_AHEAD=0` + `EDEN_NX2_BACK_WAIT=1` (racing 22–26 fps), `EDEN_NX2_FRAMES_AHEAD=1` (racing better, 38–48, but pre-race 33–46 instead of 60, and 1 of 2 runs hung when a movie ended), `EDEN_NX2_IMAGE_ALLOCATION_REUSE=1` (no measurable change), small images sub-allocated instead of NVIDIA's preferred dedicated memory (racing −1 fps, more hitches, VRAM peak 7.9 GB instead of ~6.4 GB), and whole-program ThinLTO (see 0008). Mario Kart World's upstream preset turns reactive flushing off: 75× fewer trapped CPU reads, no fps change; it is a per-game setting, so it is not applied.

## Keeping up with upstream

The working fork is `~/src/eglantine`, branch `uscl` (origin = upstream). `python3 tools/emu-sync/emu-sync.py check eglantine` reports new upstream commits and whether any touch a file we changed; `apply --build eglantine` merges the clean ones and rebuilds `build-uscl`. After a merge, regenerate this folder: cherry-pick our commits onto `origin/nx2-experimental` in a scratch worktree, check `git diff uscl HEAD` is empty, and `git format-patch origin/nx2-experimental..HEAD`. Applying this folder with `git am` to the pinned base gives a tree identical to `uscl` (checked 2026-11-15).

## Patches
- 0001 ports uscl-eden 0012 to Eglantine's SDL3 input driver: with `USCL_AUTO_PADS` set (the home screen sets it when Automatic Controller Setup is on) every connected game controller becomes players 1–8 in connection order, mapped with Eglantine's own per-controller defaults. Without it, player 1 stays on the keyboard.
- 0002 skips a wasted texture upload: when the copies that build a joined image already write every level and layer (Mario Kart World joins three GPU-written 2048×2048 depth targets into a 3-layer array every few seconds), JoinImages no longer unswizzles and uploads the old guest-memory contents first. Mario Kart World race on an RTX 4060: those uploads drop from ~40 per run to 1, about +4% average FPS in an A/B, no visual change.
- 0003 fixes most of the stutter: upload staging and the stream buffer move to host-cached memory on NVIDIA's proprietary driver. Uploads unswizzle into staging in 16-byte pieces; into write-combined/BAR memory that ran at ~270 MB/s in the emulator (RTX 4060, PCIe 3.0), stalling the GPU thread 10–20 ms per streamed texture. Cached: ~8 GB/s. Mario Kart World race: ~23 → 30–44 fps, p99 frame time ~140 → ~50–70 ms, frames over 50 ms per 10 s 142 → 26–37. `USCL_CACHED_STAGING=0/1` forces it off/on (other drivers keep upstream's choice).
- 0004 unswizzles textures over 2 MiB on several threads: output lines are independent, so the GPU thread splits them across a small pool (a third of the host's threads, 1–6, so smaller CPUs keep theirs for the emulated cores). Byte-identical output (round-trip test); Mario Kart World race +10% average FPS (38 → 42–43).
- 0005 drops a per-block cost in the CPU JIT: with perf maps off (the normal case) every compiled block still took a global lock, looked up `PERF_BUILDID_DIR` and formatted the block's name, with all cores compiling at once while a game boots. 17.8 → 16.8 µs per block.
- 0006 shares the JIT's code cache between a process's cores. Each core had its own and compiled every block it ran: Mario Kart World compiled 1.55M blocks for 511k distinct ones, and its boot stutter was almost all compilation. Per-core values the code used to embed are read from each core's state, blocks link through 8-byte slots set with atomic stores (no live instruction is rewritten), each core keeps its own fast-dispatch table, and invalidation keeps the code space. Mario Kart World, boot to race: 510k blocks compiled instead of 1.55M, frames over 100 ms 20–26 instead of 29–40; Switch Sports Resort runs as before. `USCL_SHARED_JIT=0` gives every core its own cache again.
- 0007 stops sparse texture binds from stalling the GPU thread. Every `vkQueueBindSparse` (the zero block, tiles and layers) waited for a fence on the CPU, i.e. until the graphics queue drained: ~33 ms per bind in Mario Kart World's terrain streaming (a GPU-side wait measured ~5 ms per bind). The bind now signals a semaphore that the scheduler attaches, at every stage, to the next batch it submits, whatever submits it, and keeps until that batch completes. An earlier version waited for the semaphore only in an explicit flush after the uploads; a staging request that flushed between the bind and the uploads then sent them unprotected, which matches the corrupt textures seen with the frame spike recorder on. None of the 48 runs logged since (recorder off) shows the corruption's `Invalid MSAA mode` assert or a broken texture in the screenshots checked.
- 0008 adds `USCL_VIDEO_CORE_LTO` (CMake option, off by default): ThinLTO for `video_core` only, the GPU threads' code. A whole-program ThinLTO build crashed Switch Sports Resort once on `this == GetCurrentThreadPointer(kernel)`: the core's guest threads are fibers that resume on other host threads, and a `thread_local` read inlined across files can be reused from before the switch. The GPU threads have no fibers.

Settings used on the test PC (per profile, not set by USCL): GPU accuracy Low (Mario Kart World racing ~25 → ~33 fps on the 2026-11-14 build, renders correctly), docked mode, 16× anisotropic filtering, SMAA. Leave `EDEN_NX2_SPIKES` (the frame spike recorder) off outside debugging (see 0007).

## Open issues

- **Switch Sports Resort freezes** (every run; where depends on timing). GPU channels 1, 2 and 4 stop on acquires of NVN2 queue semaphores (device `0x8029008`, `0x8029010`, `0x8029014`, 6–12 values ahead of memory). No GPU method references those words (`EDEN_NX2_SEM_WATCH`), and the `nx2:vi2` present releases only syncpoints, so the game's own CPU code writes them; meanwhile its render thread waits in the present for the previous frame's GPU fence, which needs those releases, and every other game thread blocks behind it (only audio keeps calling services). Upstream already copies each command list when it is submitted (`EDEN_NX2_PB_SNAPSHOT`, on by default); the present waits so the game cannot rewrite the *data* those commands read (constant buffers, vertex data) before the emulated GPU has used it, and Switch Sports needs that present to return before its previous frame can finish. Triggers seen: the intro movie ending (decoder stop/finalize, then `nx2:gsv` 903, which is the decoder context's teardown and is auto-stubbed harmlessly) and, past the movie, a burst of shader pipeline builds in the island tour. `EDEN_NX2_VDEC_EOS_RELEASE_NOW=1` (completes fences for frames that will never be decoded) got it past the movie in one run, not reliably, and not past the later freeze; it stays off because upstream keeps it off for other games. Not fixed by: `EDEN_NX2_FRAMES_AHEAD=1`, `EDEN_NX2_SPLIT=0`, `EDEN_NX2_FENCE_BATCH=0`, GPU accuracy High, waiting for the frame before the previous one (froze at boot). Mario Kart World hit the same hang once in two `FRAMES_AHEAD=1` runs, never in default runs. A real fix is a non-blocking present: keep the data a pending frame reads (copy-on-write) so the present can return. Probes are on the fork's `uscl-nsr` branch.
- **Mario Kart World racing below 60 fps:** the GPU back thread (see Performance). 1–1.8 s hitches remain when the game boots and when a race loads (one very large first frame of ordinary per-draw work, texture creation and shader builds).

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

### Optimized build (what the racing numbers above use)

Tuned for the CPU it is built on (`-march=native`: don't copy the binary to an older CPU), profile-guided (PGO), ThinLTO for `video_core` only; needs clang and lld. PGO takes two builds:

1. Instrumented build: the configure line above in another directory with `-DCMAKE_C_FLAGS="-march=native -fprofile-generate" -DCMAKE_CXX_FLAGS="-march=native -fprofile-generate" -DCMAKE_EXE_LINKER_FLAGS="-fuse-ld=lld -fprofile-generate"`.
2. Train it: run a game from it with `LLVM_PROFILE_FILE=<dir>/eg-%p.profraw` (no `%c`: continuous-mode files came out unreadable) and quit normally so the profile is written. Ours is one Mario Kart World Grand Prix race. The instrumented build fails to decompress Switch Sports Resort's code (`DecompressDataZBIC`); the final build loads it normally.
3. `llvm-profdata merge -o eglantine.profdata <dir>/*.profraw` (`llvm-profdata` is in Arch's `llvm` package).
4. Final build: the configure line above plus `-DUSCL_VIDEO_CORE_LTO=ON -DCMAKE_C_FLAGS="-march=native -fprofile-use=<path>/eglantine.profdata -Wno-profile-instr-unprofiled -Wno-profile-instr-out-of-date -Wno-backend-plugin"`, the same for `CMAKE_CXX_FLAGS`, and `-DCMAKE_EXE_LINKER_FLAGS="-fuse-ld=lld -flto=thin -fprofile-use=<path>/eglantine.profdata"`. CMake's `ENABLE_LTO` is not used: it insists on `llvm-ar`, and it would put the core under LTO too (0008).

A profile made before later source changes still applies to the unchanged code (clang warns about the rest, silenced above); retrain after large upstream merges.

## Theme-aware loading UI

Shell-launched games receive the selected USCL palette for their native loading UI. The new loading patch
changes colors only: progress values, compilation, game artwork and the existing loading lifecycle stay
under the emulator’s control. Standalone launches retain the upstream appearance. See
[loading UI coverage and verification](../docs/LOADING-THEMES.md). Rebuild the patched core to use this feature.

## USCL release integration

The v0.5.2-alpha release line integrates the reviewed installer/library work and
console themes. Use the [release page](https://github.com/jaidenscool1/uscl/releases/tag/v0.5.2-alpha)
for published assets and the [fork matrix](../README.md#fork-maintenance) for build pins.
This does not update the upstream base or establish new gameplay/online support
for this fork. Patch changes require rebuilding the corresponding core; updating
the shell alone does not update a separately installed emulator.

Switch 2 CPU/GPU translation, game evidence and pending work are tracked in
[SWITCH2-PROGRESS.md](../docs/SWITCH2-PROGRESS.md).

For v0.5.2-alpha, Linux retains the signed v0.5.1-alpha native core and therefore
uses the shell launch theme without the new native loading colors. Windows builds
the loading-color patch from the release manifest. See [packaging provenance](../packaging/README.md#v052-alpha-linux-provenance).
