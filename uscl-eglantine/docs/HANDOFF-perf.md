# Eglantine (Switch 2 emulator) performance push: handoff

Owner session: "uscl switch 2 emulator rebuild" (4ee498). Updated by it as work progresses (see "Log" at the end).
If that session stopped (usage limit) and you take over: read this file, then
`~/.claude-cli2/projects/-home-main/memory/eglantine-perf.md` (long history) and continue at "Next".

## Goal (from the user, 2026-11-15)
"cut the per draw setup cost and then continue improving until above 60fps is achieved", then "keep going until you
have hit the absolute limit of what we can do to improve this emulator". User is away: do NOT prompt / ask
questions; make reasonable calls and note them here. Optimizations must be emulator-wide (shared code paths,
not per-game hacks). Do NOT push to GitHub until the user says so (CI budget, and "don't push until games run
perfectly"); commit locally.

## Where things are
- Fork: `~/src/eglantine`, branch `uscl` (= USCL patches 0001-0010 on upstream nx2-experimental 30181ba05).
  Experiments in worktree `~/src/eglantine/.claude/worktrees/perf` (build dir `build`, plain RelWithDebInfo-ish;
  `cmake --build build --target yuzu`, ~2-5 min incremental).
- Optimized build USCL launches: `~/src/eglantine/build-uscl` (PGO + -march=native + video_core ThinLTO; rebuild
  `nice -n 5 cmake --build build-uscl --target yuzu`, LTO link is slow).
- USCL repo worktree: `~/src/ps3-steam-np/.claude/worktrees/switch2-eglantine`, branch
  `agent/jaiden-pc/switch2-grace` (PR #18 open). Local commit e7478f0 adds patch 0010 + docs, NOT pushed.
  New fork commits -> `export_patches.sh` (add the sha to COMMITS) -> copy new patch into
  `uscl-eglantine/patches` -> `verify_am.sh` must print an empty diff -> update docs -> commit locally.
- Harness (job tmp `/home/main/.claude-cli2/jobs/06fda08d/tmp`, durable copy in this folder):
  - `RUN_BIN=<bin> python3 -I runs.py a b:ENV=1 ...` MKW race per label (DEFAULT BIN = perf/build, plain!).
    Last 3 "chunks" values are racing fps.
  - `./mkw.sh <label> <bin>` race + end screenshot + garbage-command count.
  - `RUN_BIN=<bin> python3 -I nsr.py <label> 300 25 --press-after 40` Switch Sports (NSR_GAME=<path> for others).
  - `RUN_BIN=<bin> PERF_EXTRA="--call-graph lbr" sh prof_race.sh <label> ""` -> perf-<label>.data;
    `./perf report -i perf-<label>.data --comm GPUBack --no-children --percentage relative -g none --sort sym`.
  - launch.py has a watchdog (STUCK-<pid>.txt). Only one emulator at a time.

## Baseline (optimized build-uscl, 2026-11-15 evening)
MKW racing (first ~15 s of a race, 24 karts): ~38-46 fps; pre-race 60. NSR 60 fps everywhere tested.
Back thread (GPUBack) self time: BackLoop 17% (waiting), BindHostStageBuffers 6.7, Draw 6.3, Maxwell3D::CallMethod
5.1, FillImageViews 5.0, UpdateGraphicsBuffers 4.4, BindHostGeometryBuffers 3.1, CallStateMethodRun 2.8, libc ~5,
CurrentGraphicsPipeline 2.4, PushImageDescriptors 2.1, clock_gettime 1.8, nvidia driver ~3.

## Next
See Log.

## Log
- (4ee498) Profiling per-draw setup; deciding where the frame's critical path is (EDEN_NX2_TIMELINE=1).
- (4ee498) Racing: GPUBack 91% CPU (perf stat), front 51%, cores ~47% -> back thread is the limiter. BackLoop self
  17% of back samples = ~47% spin (pause) + ring replay cache misses. vkAllocateMemory 7.7% of back (staging 4.2%,
  dedicated images 3.4%). REJECTED: keep idle staging buffers 600 frames (USCL_STAGING_KEEP_FRAMES): profile share
  4.1%->0% but racing 2-4 fps WORSE in 2 interleaved long pairs (linear search over longer per-level lists?).
  Lesson: verify every change with BENCH_FRAMES=6000 interleaved A/B (racing = chunks[5:]).
- (4ee498) User reported desktop UI glitches. Kernel log: NVRM "GspRmAlloc failed ... NV_ERR_STATE_IN_USE"
  (GPU channel-group allocs refused) since 2026-11-14 (none 11-10..11-13), many clients, also when no emulator
  runs -> leaked driver/GSP state, most likely from SIGKILLed emulators. Harness now waits 45 s for a clean exit
  before SIGKILL (runs.py, nsr.py, launch.py watchdog, mkw.sh). Told user a reboot clears it (ends sessions; resume
  from this file). Also watch VRAM: races peak 6.4-7.9 GB of 8 GB with the desktop on the same card.
- (4ee498) Bind probes (EDEN_NX2_BIND_STATS=1 + OnDraw hooked in Draw, branch uscl-bindprobe): per draw ~4.9 us;
  small uniform binds 6.3/draw x 203 ns = 1.27 us (reuse hit 66%), VisitImageView 6.4 x 123 ns = 0.78 us (99.4%
  descriptor unchanged; cost = PrepareImage cache misses), FindBuffer ~0.27, locks ~0.33, samplers 0.14. No empty
  or render-enable-skipped draws (EDEN_NX2_DRAW_STATS: all ~100k draws/s real). REJECTED: in-place small uniform
  compare/copy (USCL_UBO_INPLACE): racing 39.9/36.8 vs old 39.6/38.9 (noise +-3 fps). Next: retrain PGO on uscl
  (patches 0001-0010) and A/B vs build-uscl.
- (4ee498) PGO retrain: instrumented worktree build-pgogen rebuilt at uscl 6ba5b5eeb; trained with one MKW race
  (BENCH_FRAMES=4000) -> /home/main/.claude-cli2/jobs/06fda08d/tmp/pgo2/eglantine.profdata (18 MB). Building
  worktree build-pgo with it (same flags as build-uscl). Then: A/B build-pgo vs build-uscl
  (RUN_BIN=..., BENCH_FRAMES=6000, 2 interleaved pairs). If better: copy profdata to
  ~/.local/share/uscl/eglantine-pgo/ (keep the old as .old) and rebuild build-uscl.
  Plan after: (a) BOLT (build llvm-bolt from LLVM 23.1.1 source, relink with -Wl,--emit-relocs, perf LBR
  profile -> perf2bolt -> llvm-bolt -reorder-blocks=ext-tsp -reorder-functions=cdsort -split-functions);
  (b) VRAM: test vram_usage_mode / budget so the desktop keeps headroom (glitches + stalls near 8 GB);
  (c) structural: back thread is ~91% busy; per-draw ~4.9 us is cache-miss bound across many caches.
- (4ee498) BOLT: LLVM 23.1.1 source downloaded + extracted (llvm, bolt, cmake, third-party) to
  /home/main/.claude-cli2/jobs/06fda08d/tmp/bolt-src, but configuring/building it was DENIED by the permission
  classifier. Do not work around it; it needs the user's OK. Skip BOLT unless the user allows it.
- (4ee498) ACCEPTED: PGO retrained on uscl 6ba5b5eeb (one MKW race, BENCH_FRAMES=4000): racing 49.6/44.2 vs
  43.8/42.7 for the old profile (2 interleaved long pairs, +8% avg). New profile installed at
  ~/.local/share/uscl/eglantine-pgo/eglantine.profdata (old kept as .old); rebuilding build-uscl with it
  (same path in its CMake flags, so a plain rebuild picks it up; profile file mtime triggers recompiles?
  -> forced full rebuild by touching CMakeCache/cleaning).
- (4ee498) Next experiment (BOLT-lite, no new tools): worktree build-pgo with the new profile PLUS
  -fsplit-machine-functions -mllvm -enable-ext-tsp-block-placement=1 (C/CXX flags), A/B vs rebuilt build-uscl
  with ./ab_bins.sh (job tmp) 2 pairs.
- (4ee498) build-uscl rebuilt clean with the new profile: racing 47.4/48.4 (A/B pgo1/pgo2). REJECTED:
  -fsplit-machine-functions + ext-tsp block placement (lay1/lay2 45.5/45.6). Worktree build-pgo has those flags
  now (reconfigure before reuse). Next: transparent huge pages (GLIBC_TUNABLES=glibc.malloc.hugetlb=1), NSR
  regression check on the new build-uscl.
- (2df788 TAKEOVER 23:23, handoff stale 65 min, nothing running) Continuing at "transparent huge pages".
  Found WIP e6931897d on branch uscl-hugepages (perf worktree): madvise(MADV_HUGEPAGE) on the guest memfd
  backing + fastmem views (shmem_enabled = advise, so memfd needs it); USCL_GUEST_HUGEPAGES=0 disables.
  Smoke run hpchk passed (54.3 fps short run). Running interleaved A/B hp vs nohp (same plain binary,
  BENCH_FRAMES=6000, 2 pairs) + smaps sampling -> job 2df7881f tmp hp_ab.txt / hp_smaps.txt.
- (2df788) REJECTED: guest huge pages (e6931897d, branch uscl-hugepages; NOT merged). They apply (ShmemPmdMapped
  ~6.7 GB vs 0) but racing hp 38.3/38.2 vs nohp 40.6/38.0 (plain build, 2 interleaved 6000-frame pairs), +0.6 GB
  RSS. dTLB is not the back-thread limiter. Next: NSR regression check on build-uscl (retrained PGO), then VRAM
  headroom (vram_usage_mode / budget).
- (2df788) NSR regression on rebuilt build-uscl (retrained PGO): PASS, 346 s, 59.8 fps steady (one 41.9 sample at
  the movie transition), reached a Tennis match, only the known unimplemented 201/103/903 asserts.
  VRAM headroom: SKIPPED. vulkan_device.cpp already uses VK_EXT_memory_budget (minus 1/8 reserve, Conservative cap
  6 GiB); lowering = old texture GC churn, raising = desktop glitches. No clean emulator-wide win.
  Next: fresh LBR profile of build-uscl racing (GPUBack + front) to pick the next target.
- (2df788) Fresh LBR profile of build-uscl racing (perf-prof_now.data): same flat back profile; NVIDIA driver =
  12.9% of GPUBack samples, of which vkAllocateMemory (VMA AllocateVulkanMemory) = 8.4% of GPUBack:
  staging buffers 4.7% (StagingBufferPool::CreateStagingBuffer), AccelerateImageUpload images 3.0%. The driver's
  clock_gettime (2.2% of back) is inside those allocations. Cause: ReleaseLevel frees ANY currently-free buffer
  (one size level per frame, 16 at a time), even ones reused every frame -> recreated. Upstream already has the
  fix on origin/fix/vk-staging-release f5e4384ef (idle 60 frames, all classes each frame, max 32/frame, quarter-step
  size classes; their MKW: 4510 vs 13168 buffers created). Cherry-picked cleanly onto uscl as branch
  uscl-staging-release 58089d11d (perf worktree). A/B running: build/bin/eglantine-stg vs eglantine-base (plain).
- (2df788) Staging A/B round 1 (plain, 6000 frames): stg 40.2/41.1 vs base 39.9/37.3 (+5%, inside noise). Round 2
  (base first) running. Harness note: bench.py finds the emulator by `pgrep -x eglantine`, so A/B binaries must be
  NAMED eglantine -> perf worktree ab-stg/eglantine, ab-base/eglantine (with nx2_switches.txt copied).
- (2df788) ACCEPTED (pending merge into uscl): uscl-staging-release 58089d11d. Round 2: base 40.5/38.0, stg
  39.5/39.3. All 4 pairs: stg 40.0 vs base 38.9 (+2.8%, noise-level), hitches same (frames>100 ms 30/23 vs 30/19,
  same ~1.7 s/1.2 s race-load spikes). Profile of stg: staging vkAllocateMemory 4.7% -> 0.0% of GPUBack, upstream
  code (eases future syncs), lower staging memory per upstream. Not merged into uscl yet: uscl is checked out in
  the main checkout ~/src/eglantine and this session's git is confined to the perf worktree; batch it with the
  next accepted change, then rebuild build-uscl (PGO) + MKW/NSR check.
- (2df788) Remaining driver allocations on GPUBack after the staging fix (perf-prof_stg.data): new texture images
  (Image::Image via JoinImages) 3.9%, ASTC->BCn decode scratch (TranscodeAstcToBcn) 2.4%. Scratch pool already
  keeps idle images 60 frames under a 256 MiB cap (misses = new shapes); new-texture memory is inherent and
  suballocation was rejected earlier. Allocation area exhausted: no further cheap win.
  STATUS for whoever resumes: uscl unchanged (6ba5b5eeb). Ready to ff into uscl: uscl-staging-release 58089d11d
  (do it in ~/src/eglantine: `git merge --ff-only uscl-staging-release`), then rebuild build-uscl, MKW + NSR check,
  export as USCL patch 0011 (export_patches.sh COMMITS += 58089d11d, verify_am.sh empty diff), commit locally.
  Rejected this round: guest huge pages (branch uscl-hugepages). Cheap per-draw/alloc/layout/THP options are
  spent; what is left is structural (back-thread pipelining inside channel 1, redundant-draw skipping, COW data
  snapshots) or BOLT (needs the user's OK to build llvm-bolt).
- (bdc6a4, resumed after the user's reboot 2026-11-16; driver NVRM errors gone after reboot) MERGED
  uscl-staging-release into uscl (ff, uscl = 58089d11d), rebuilt build-uscl: MKW 51.0 fps mean / min 44.4,
  0 garbage; NSR 318 s 59.8 fps. Exported USCL patch 0011, docs updated, USCL commit 787cf3f (local, not
  pushed). EDEN_NX2_GPU_PROFILE=1 race: front waits on the back 235 ms/s (list writers 274 ms/s ~5000/s =
  GPU-written command lists; acquire trigger 229 ms/s ~2500/s) -> back idles ~8%; back "method" zone ~19% under
  the profiler (perf: ~9-10%), already a lean fast path. Testing now: GPUBack pinned alone on physical core 5
  (cpu 5, sibling 11 idle), all other threads on 0-4,6-10 (pin.sh in job tmp) vs unpinned, 2 long pairs.
- (bdc6a4) ACCEPTED idea: GPUBack alone on a physical core. pin.sh (external taskset) A/B on build-uscl: pinned
  48.0/49.3 vs free 45.0/45.5 (+7.5%). Implemented in-emulator on branch uscl-backcore (perf worktree):
  common/thread.cpp StartLinuxBackCore() (Linux port of upstream's Windows-only EDEN_NX2_CPU_LAYOUT idea, whose
  shared MakeCpuThreadLayout needs 2 L3 domains): USCL_BACK_CORE=1 default (back on highest CPU, its core's
  siblings idle, others elsewhere; CPUPlacement thread re-applies every 1 s), =2 also front ("GPU") own core,
  =0 off. Verified masks via /proc (GPUBack 11, rest 0-4,6-10). A/B (plain build) bc/off/bf running -> ab-bc.txt.
- (bdc6a4) LOST ~6 h (04:05-09:55): the A/B's wait loop `while pgrep -f "runs.py bcchk"` matched its own shell
  command line and never ended. NEVER use pgrep -f with a pattern that appears in the same command; wait on a PID
  or just run sequentially. Restarted the bc/off/bf A/B at 09:56 -> ab-bc.txt.
- (bdc6a4) uscl-backcore A/B: back only = no gain (40.6/40.7 vs off 40.7/42.0); back+front (=2) 42.5/43.0, mins
  38.5/38.8 vs ~34 (+3%, noise). Not merged. Work continues in a NEW session from HANDOFF-gpu-rewrite.md
  (GPU back-thread rewrite). DONE
