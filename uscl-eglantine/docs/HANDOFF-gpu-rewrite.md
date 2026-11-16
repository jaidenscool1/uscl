# Eglantine GPU back-thread rewrite: handoff (start here)

Written 2026-11-16 by the session that did the Switch 2 perf push. Long history:
`~/.claude-cli2/projects/-home-main/memory/eglantine-perf.md` and `HANDOFF-perf.md` (same folder as this file).

## Goal
Mario Kart World (MKW) racing above 60 fps, and every Switch 2 game faster, by making the GPU **back thread**
process draws faster. User rules: emulator-wide changes only (shared paths, no per-game hacks); commit locally,
**no GitHub pushes** until the user says so (CI budget); don't prompt the user when they're away; building
llvm-bolt was denied by the permission system (needs the user's OK).

## Where things stand (all measured, RTX 4060 8 GB + i5-10600K, 6C/12T, Linux, NVIDIA 615.78)
- Fork `~/src/eglantine`, branch `uscl` = upstream nx2-experimental `30181ba05` + USCL patches 0001-0011
  (last: `58089d11d` staging release). Optimized build `~/src/eglantine/build-uscl` (PGO retrained 2026-11-15
  on 0001-0010, `-march=native`, ThinLTO for video_core) is what USCL launches.
- USCL repo `~/src/ps3-steam-np/.claude/worktrees/switch2-eglantine`, branch `agent/jaiden-pc/switch2-grace`
  (PR #18 open): local commits e7478f0 (0010 + docs), 787cf3f (0011 + PGO numbers). NOT pushed.
- Experiments: worktree `~/src/eglantine/.claude/worktrees/perf` (dirs: `build` plain, `build-pgo`,
  `build-pgogen` instrumented). Branch `uscl-backcore` there: see "Pending" below.
- MKW: pre-race 60 fps; racing **~47-48 fps** (2-min races, interleaved A/B); Switch Sports Resort 60 fps
  (freeze fixed by 0010; Tennis waits for a motion swing - no motion input mapped).

## The bottleneck (why the rewrite)
- During racing the back thread (`GPUBack`) is ~91% CPU busy (perf stat); front (`GPU`) 51%; guest cores ~47%;
  host GPU ~40%. The back thread is the frame's critical path.
- ~100k draws/s, all real (EDEN_NX2_DRAW_STATS: 0 empty, 0 skipped), all on GPU channel 1, in strict order.
  ~4.9 us of back time per draw. Probes (EDEN_NX2_BIND_STATS=1; note `Nx2DrawStats::OnDraw()` is NOT called
  upstream - add it at the top of `RasterizerVulkan::Draw` to get the report every 20k draws):
  small uniform binds 6.3/draw x 203 ns = 1.27 us (stream reuse hits 66%), VisitImageView 6.4/draw x 123 ns
  = 0.78 us (descriptor unchanged 99.4%; cost = PrepareImage cache misses), FindBuffer ~0.27, locks ~0.33,
  samplers 0.14. The rest: method processing ~9-10% of back (3.4M 3D register writes/s), pipeline lookup,
  descriptor push, render targets, driver ~4%.
- perf (LBR) self time of GPUBack is FLAT and cache-miss bound (no function above ~7%). Per-function
  micro-optimizations measured inside noise (see "Rejected").
- Front/back drains (EDEN_NX2_GPU_PROFILE=1): front waits ~235 ms/s for the back at "list writers" (~5000/s,
  GPU-generated command lists from compute culling) and "acquire trigger" (~2500/s). Back idles ~8% because of
  them. They are real data dependencies and already minimal.
- One channel does all draws, so per-channel threads gain nothing (measured).

## Rewrite directions (my recommendation first)
1. **Incremental (dirty-driven) per-draw processing.** Today every draw re-resolves every binding (uniform
   buffers per stage, storage/texture buffers, each texture handle -> image view -> PrepareImage, samplers,
   render targets, pipeline lookup). Make per-draw work proportional to what changed since the previous draw:
   keep the previous draw's resolved bindings and recompute only slots whose inputs changed (registers via the
   existing dirty flags; guest memory via the memory tracker / page write tracking; cache invalidations via a
   generation counter bumped when images/buffers are created, deleted, aliased or CPU-modified - texture cache
   sets ImageFlagBits::CpuModified/Alias in ~15 places, so wrap those). Upstream already has narrow versions:
   Nx2Perf BindMemo, BankOncePerFrame, PipelineFastCache, HandlePageCache. Validate with the existing
   `Nx2Perf::Verify()` pattern (EDEN_NX2_PERF_VERIFY=1: compute both ways, log differences, use the old answer).
   FIRST STEP: measure how much actually changes between consecutive draws (per stage: uniform binding
   address/size, texture handles, TIC/TSC words, storage bindings). The BIND_STATS sites StageBindUniform/
   StageBindStorage/StageBindTexture exist in nx2_draw_stats.h but are not wired: wire them. If most draws
   change only uniforms/vertex offsets, the gain is large; if bindings change every draw, go to 2.
2. **Two-stage back pipeline inside channel 1**: stage A resolves (caches, uploads, state), stage B records
   Vulkan commands + descriptor pushes on another core. Measured earlier: only ~6% of back time is movable
   (pipeline selection, descriptors, dynamic state, recording); the rest reads guest memory or the caches in
   command order. Lower ceiling than 1.
3. Batch consecutive draws with identical state into multi-draw/indirect recording (cuts driver + record
   cost, ~4-8%), best done on top of 1.

## Key code (fork, `src/`)
- `video_core/gpu_split.{h,cpp}` + repo-root `NX2_GPU_THREAD_SPLIT.md`: front/back split, ring (`Execute`
  :516, `Publish` :234, `Drain` :247, `BackLoop`), DrainSite list.
- `video_core/renderer_vulkan/vk_rasterizer.cpp`: `PrepareDraw` :837, `Draw` :1261, `DispatchCompute` :1855.
- `vk_graphics_pipeline.cpp`: `MakeConfigureSpecFunc` / Configure (per-draw binding resolution).
- `video_core/texture_cache/texture_cache.h`: `FillImageViews` :717, `VisitImageView` ~:1220,
  `PrepareImage`, `ReplaceColorFeedbackViews`.
- `video_core/buffer_cache/buffer_cache.h`: `BindHostStageBuffers` :654, `BindHostGraphicsUniformBuffer`
  :1586, `UpdateGraphicsBuffers`; `uniform_stream_reuse.h` (small uniform reuse).
- `video_core/engines/maxwell_3d.cpp`: `CallMethodFast` :529, `ProcessDirtyRegisters` :354.
- Instrumentation: `nx2_profile.h` (EDEN_NX2_GPU_PROFILE=1 per-second zones per thread),
  `nx2_draw_stats.h` (EDEN_NX2_BIND_STATS=1), `nx2_perf.h` (opt switches + Verify), EDEN_NX2_TIMELINE=1.

## How to measure (harness: `/home/main/.claude-cli2/jobs/06fda08d/tmp`, durable copy in this folder)
- `RUN_BIN=<bin> BENCH_FRAMES=6000 python3 -I runs.py a b:ENV=1 ...` = one long MKW Grand Prix race per
  label; racing fps = mean of `chunks[5:]`. runs.py's default binary is the PLAIN perf build. Noise is +-3 fps
  per run: only trust >= 2 interleaved pairs; prefer an env toggle inside one binary for A/B.
- `./ab_bins.sh lA binA lB binB 2` (interleaved binaries), `./mkw.sh <label> <bin>` (race + screenshot +
  garbage-command count), `RUN_BIN=<bin> python3 -I nsr.py <label> 300 25 --press-after 40` (Switch Sports;
  regression check after every accepted change), `prof_race.sh` (perf LBR), `stat_race.sh` (per-thread CPU).
- bench.py finds the emulator by `pgrep -x eglantine`: A/B binaries must be named `eglantine`.
- NEVER wait with `pgrep -f "<pattern>"` when the pattern is in your own command line (it matches itself:
  that stalled a test for 6 hours). Run sequentially or wait on a PID.
- Stop emulators with SIGTERM and give them 45 s (the scripts do): SIGKILLed emulators leaked GPU driver
  channels (NVRM NV_ERR_STATE_IN_USE, desktop glitches) until a reboot.
- Patch export: add the commit to COMMITS in `export_patches.sh`, run it, copy the new patch into
  `uscl-eglantine/patches`, `verify_am.sh` must print an empty diff; update docs; commit locally.

## Rejected (don't redo)
Keeping staging buffers 600 frames (worse; upstream's 0011 is the right fix), in-place small uniform compare,
guest memory huge pages, -fsplit-machine-functions + ext-TSP layout, image allocation reuse (pool cleared under
VRAM pressure), present deadlock breakers, VRAM budget changes, EDEN_NX2_FRAMES_AHEAD/BACK_WAIT variants,
small-image suballocation, per-channel back threads.

## Pending from the last session
- Branch `uscl-backcore` (perf worktree): `common/thread.cpp` `StartLinuxBackCore()` gives GPUBack a
  physical core of its own (USCL_BACK_CORE=1 default, =2 also the front, =0 off). External taskset test:
  48.7 vs 45.3 fps (+7.5%). In-emulator A/B result: see the end of this file. If accepted: ff into `uscl`,
  rebuild build-uscl, MKW + NSR check, export as USCL patch 0012, docs, commit locally.

## Result of the uscl-backcore A/B (2026-11-16 10:30, plain build, 6000-frame races, racing fps / min)
off (USCL_BACK_CORE=0): 40.7 / 33.8, 42.0 / 34.6. back only (=1, the branch default): 40.6 / 34.1,
40.7 / 35.6 -> no gain (the external taskset test's +7.5% on build-uscl did not reproduce). back + front
each on their own core (=2): 42.5 / 38.5, 43.0 / 38.8 -> ~+3% and clearly higher minimums, but inside noise.
NOT merged. Next: if wanted, make =2 the branch default and A/B it on an optimized (PGO) build of the branch
vs build-uscl (2+ pairs); merge only if it holds. Otherwise drop the branch.

## Rewrite log (session started 2026-11-16 ~10:20)
- Branch `uscl-gpurw` (perf worktree, off `uscl` 58089d11d). Step 1 probes (uncommitted -> PROBE commit):
  `Nx2DrawStats::OnDraw()` at the top of `RasterizerVulkan::Draw`; `StageBindUniform/Storage/Texture` timed
  in `BufferCache::BindHostStageBuffers` (hit = the stage's enabled mask + every slot's address/size equal
  the previous draw's); new sites `StageHandles` (stage's TIC indices + sampler ids = previous draw's),
  `StageHandlesPipe` (= this pipeline's previous use), `PipelineSame`, `DrawAllSame` (every probe hit).
  Run: `EDEN_NX2_BIND_STATS=1` -> log line "nx2 draw probes" every 20k draws.
- uscl-backcore re-test: new worktree `~/src/eglantine/.claude/worktrees/backcore` (branch uscl-backcore),
  `build-pgo` configured with exactly build-uscl's options (march=native + PGO profile + video_core ThinLTO).

### Step 1 result: what changes between consecutive draws (MKW racing, plain build, 2026-11-16 10:40)
Probe commit 6901f96cb on uscl-gpurw. `python3 -I <job tmp>/probes.py run-stats1.log 0.35 1` (508 reports):
- pipeline = previous draw's: **30%**. Draws where NOTHING changed (pipeline, uniform/storage bindings,
  handles): **1.9%**.
- per stage: uniform bindings (mask + address/size per slot) = previous draw's **18%**; storage 53%;
  texture/image handles + samplers = previous draw's **26%**, = this pipeline's previous use **8.5%**.
- per resource (much higher): TIC descriptor unchanged 97.7% (VisitImageView), TSC 100%, FindBuffer 100%
  hits, small uniform bytes = this slot's previous bind 63% (at a different address 82% of the time).
=> Draw-level / stage-level "skip if unchanged" (handoff direction 1 as written) has a ~2-25% hit ceiling:
   NOT worth building. Every draw binds new uniforms at new addresses and new textures.

### Where GPUBack time goes (perf LBR, plain build, no profiler; `classify.py` / `children.py` in job tmp)
% of GPUBack samples (13% of them are the idle spin in BackLoop):
- Draws 57%: Configure 41% = uniform binds 12.1% (BindSmallUniformReusing 7.0%, mostly libc
  memcpy/memcmp; self 2.2%; IsRegionGpuModified 0.9%), VisitImageView 7.6% (CreateImageView->InsertImage
  2.9% = NEW textures, real work; PrepareImage 1.9%; DescriptorTable::Read 1.4%; self+map 1.3%),
  UpdateGraphicsBuffers 5.2% (vertex 2.6, index 0.9), BindHostGeometryBuffers 4.9% (vertex 2.7, index 2.1),
  PushImageDescriptors 2.0, UpdateRenderTargets 1.8, storage ~2.7; pipeline lookup 4.1%; rest of Draw ~9%.
- Method processing ~20%: Execute self 4.9, CallMethodFast 4.4, inline upload ProcessData 2.7,
  CallStateMethodRun 2.6, CallConstBufferWords 2.1, ProcessCBBind 2.0. Compute 6.3%, SignalSyncPoint 2.6%.
- NOTE: EDEN_NX2_GPU_PROFILE inflates "back replay" (~2.9M scopes/s) and its slow method path crashes
  (QueryCacheBase::CounterReport SIGSEGV ~20 s). Use EDEN_NX2_PROFILE_FAST=1 with it (probe commit).
- 60 fps needs back time per frame cut ~25-30%. No single item is above 12%.

### Revised design (decision, 2026-11-16)
Memoize per RESOURCE, not per draw: keep each lookup's answer keyed by the resource identity (TIC index,
buffer address, uniform slot) with a validity token (generation counters bumped by the events that can
change the answer), so a lookup touches 1-2 cache lines instead of 5-10. Order by addressable share:
 1. Uniform binds (12%): fewer passes over the bytes; bind without copying where it is provably safe.
 2. Vertex/index buffer update+bind (~10%): memo per slot (address,size)->(buffer, offset), skip
    re-binding unchanged slots (FindBuffer already 100% hits: cost is the walk + Synchronize).
 3. VisitImageView cached path (~4.6%): per-TIC-index compact entry {view id, image generation, frame};
    skip PrepareImage when no image changed state (generation) and touch the LRU once per frame.
 4. Method processing (~20%): front-side coalescing of repeated register writes between draws.
Each step behind an Nx2Perf switch + Verify path; accept only with >= 2 interleaved long-race wins.

### REJECTED (measured): binding small uniforms live from guest memory (no copy)
Probe EDEN_NX2_UBO_LIVE_PROBE (uncommitted probe on uscl-gpurw, uniform_stream_reuse.h UniformLiveProbe):
hash each small uniform bind's bytes, re-check later. MKW race, 26-30M binds: 95% of host pointers 256-aligned
(importable), BUT bytes differ at command buffer SUBMIT for 10.6% of binds (100% of those on pages written by
inline GPU writes, WriteBlockCached/WriteWordsCached = 13% of binds), and at host GPU completion (tick read at
every bind) for 11.7% - of which 3.2% (= 0.37% of all binds, ~50/frame) are on pages NO inline write touched:
the guest CPU rewrites them before the host GPU finished (guest fences run ahead of host GPU completion).
=> A live import would show wrong uniforms; even "import unless inline-written" is unsafe. Uniforms stay copied.

### ACCEPTED: USCL_BACK_CORE=2 (2026-11-16 ~12:10)
PGO build (worktree `backcore`, build-pgo = build-uscl's flags), 6000-frame races ABBA:
off 48.5 (min 39.2), =2 49.7 (41.2), =2 49.7 (41.4), off 48.8 (38.2) -> wins both pairs, +2.2%, minimums +2-3.
NSR (nsr.py 300 25 --press-after 40): 59.8 fps to 318 s, Tennis screen, not stuck.
Branch `uscl-backcore` = d6b5e1574 (default now 2, final message). NOT merged into `uscl` (that is the user's
main checkout; build-uscl not rebuilt, no USCL patch 0012 yet). To ship: `git -C ~/src/eglantine merge --ff-only
uscl-backcore`, rebuild build-uscl, export 0012 (export_patches.sh COMMITS += d6b5e1574), verify_am.sh, docs.

### New tool: in-race A/B of one Nx2Perf switch (low noise)
`EDEN_NX2_AB=<switch name>` (probe commit on uscl-gpurw): flips the switch every 20k draws and logs cycles per
draw spent in RasterizerVulkan::Draw with it on vs off ("nx2 A/B ..." every 400k draws). Both sides see the same
scenes, so a 1-2% change is visible (fps pairs are +-6% per run). Only for switches read via Nx2Perf::On at use
and that act inside Draw. Use it to pick candidates; fps pairs still decide acceptance.

### image_view_memo (WIP commit on uscl-gpurw, Nx2Perf switch, default on)
VisitImageView cached path: per-TIC-index memo {view, image-state epoch, frame}; skip PrepareImageView when the
view was prepared this frame and no image gained CpuModified/Alias or was deleted since (global
g_image_state_epoch in image_base.h, bumped at all 13 flag sites + DeleteImage). Verify run: 0 memo errors.
In-race A/B: -2.2% Draw cycles/draw (20334 vs 20785, 14M draws) = ~1.2% of back time. fps pairs: see below.

### Other small items looked at (not done)
- MemoryManager::FlushCaching per draw = 5.3% of GPUBack (InnerInvalidation -> BufferCache::WriteMemory 2.2%,
  SpinRecursiveMutex 0.7%): inline cb writes marked CPU-modified each draw. Pages are mostly registered
  (uniform rings), so a "skip unregistered" filter would rarely apply.
- UpdateRenderTargets clean path re-prepares RTs with is_modification each draw (bumps modification_tick that
  orders overlapping-image copies): skipping changes semantics, left alone.
- Method processing (~20%) is volume (~3.4M ring entries/s, ~59 ns each), handlers already lean.

### Session result (2026-11-16 ~13:30)
Ship stack `uscl-gpurw-ship` (worktree backcore, = uscl + d6b5e1574 backcore=2 + f4d0f091e image_view_memo, PGO
build-pgo) vs build-uscl, ab_bins.sh 2 pairs, 6000 frames: uscl 48.3 / 45.4, ship 49.8 / 54.3 (mins 41.5/37.7 vs
32.3/40.8). Wins both; true size ~+3-4% (pair 2 is mostly noise). image_view_memo fps pairs (plain build,
env toggle): off 42.6 / 42.5, on 43.0 / 45.4. NSR fine on both changes.
Still to do to ship: ff `uscl` to uscl-gpurw-ship, rebuild build-uscl, export USCL patches 0012 (backcore) +
0013 (image_view_memo), verify_am.sh, docs, commit locally (no push).
Next candidates (screen with EDEN_NX2_AB first): uniform small-bind passes (12%), FlushCaching/InnerInvalidation
(5%), pipeline lookup (4%), vertex/index bind (~5%). 60 fps needs ~25-30% less back time: per-resource
memos alone look like ~10%; the rest needs moving work off the back thread (two-stage back) - design not started.
