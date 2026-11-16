# Prompt: continue the NVIDIA GPU passthrough research for Eglantine (Switch 2)

Paste everything below the line into a new Claude Code session. It is self-contained.

---

You are continuing a research-and-build project for USCL (github.com/jaidenscool1/uscl, public repo): an optional
GPU passthrough backend for Eglantine, the Switch 2 emulator core USCL ships. Instead of translating every guest
draw into Vulkan, the backend hands the game's own NVIDIA GPU command streams and shaders to the host NVIDIA GPU,
with only light patching ("pushbuffer passthrough", by analogy with NCE on the CPU side; close to mediated GPU
passthrough in virtualization). Nobody is known to have shipped this for a console emulator, so treat it as
research: prove feasibility in small steps, and stop early with a clear write-up if a gate fails.

## Why (measured 2026-11-16)
- Switch 2 GPU = NVIDIA Ampere (T239). Host = RTX 4060 8 GB (AD107, Ada), i5-10600K 6C/12T, Arch Linux, NVIDIA
  open kernel module 615.78.08 (source installed at `/usr/src/nvidia-615.78.08/src`, incl. the RM source and SDK
  headers under `src/common/sdk/nvidia/inc`).
- Mario Kart World (MKW) racing runs ~48-50 fps; the goal is > 60. The limit is Eglantine's GPU back thread (~87%
  busy): ~100k draws/s, ~4.5-5 us each, re-resolving bindings and translating state to Vulkan per draw. The cost is
  flat (no function above ~12%), and bindings change on almost every draw, so caching only trims it. 60 fps needs
  ~25-30% less back-thread time. Details: `uscl-eglantine/docs/HANDOFF-gpu-rewrite.md` (read it first) and
  `HANDOFF-perf.md`.

## Progress so far (previous session, 2026-11-16, read-only research, nothing submitted to the GPU)
- Gate 1 (driver access), static evidence only: in `src/nvidia/src/kernel/rmapi/resource_list.h`, ADA_A (0xC997)
  and ADA_COMPUTE_A (0xC9C0) are `RS_FLAGS_ALLOC_NON_PRIVILEGED` with parent `KernelChannel`, so an ordinary
  client should be able to allocate the 3D class on its own channel. Not yet tested on hardware.
- AD107 class list (`src/nvidia/generated/g_gpu_class_list.c`): ADA_A, ADA_COMPUTE_A, AMPERE_DMA_COPY_B (0xC7B5),
  KEPLER_INLINE_TO_MEMORY_B, FERMI_TWOD_A, AMPERE_CHANNEL_GPFIFO_A (0xC56F), TURING/AMPERE_USERMODE_A,
  KEPLER_CHANNEL_GROUP_A, FERMI_CONTEXT_SHARE_A. No AMPERE_B.
- Guest classes (Eglantine `src/video_core/engines/puller.cpp` ProcessBindMethod): AMPERE_B 0xC797,
  AMPERE_COMPUTE_B 0xC7C0, AMPERE_DMA_COPY_B 0xC7B5. Copy class matches the host exactly; 3D and compute need their
  SET_OBJECT class rewritten (0xC797 -> 0xC997, 0xC7C0 -> 0xC9C0) plus a method diff.
- Pushbuffer (GP_ENTRY1_GET_HI) and semaphore (SEM_ADDR_HI) addresses are 40-bit (`clc56f.h`): a 1:1 guest-VA
  mapping only works if guest GPU VAs stay below 1 TiB, and RM's own internal VA allocations (context buffers)
  must be kept out of the guest window (e.g. reserve it first with NV50_MEMORY_VIRTUAL).
- Kernel log baseline at 12:57: no Xid since boot.
- Reference code (read-only, do not execute): `~/src/ext/tinygrad-ref/tinygrad` (runtime/ops_nv.py: the RM call
  sequence root client -> device -> subdevice -> usermode doorbell -> VA space -> TSG + ctxshare -> GPFIFO ->
  engine objects -> work-submit token; tinygrad uses a UVM-managed VA space), `~/src/ext/open-gpu-doc` and
  `~/src/ext/mesa-nouveau/src/nouveau/headers/nvidia/classes` (method headers incl. clc797.h, clc997.h,
  clc7c0/clc9c0 + QMD headers, clc56f.h).
- The SDK headers compile in C++ with `-I src/common/sdk/nvidia/inc -I src/nvidia/arch/nvalloc/unix/include`
  (ioctl wrappers for NV_ESC_RM_ALLOC_MEMORY / MAP_MEMORY are in `nv-unix-nvos-params-wrappers.h`).
- Tools: ptxas, nvcc, nvdisasm (CUDA 13.5, `--binary SM86/SM89` for raw SASS) in
  `~/.local/share/uscl/cuda-tools/lib/python3.14/site-packages/nvidia/cu13/bin`.
- Worktree for this work: `~/src/ps3-steam-np/.claude/worktrees/gpu-passthrough`, branch
  `agent/jaiden-pc/gpu-passthrough` (off `agent/jaiden-pc/switch2-grace`, PR #18). No commits yet. The progress
  log `uscl-eglantine/docs/HANDOFF-gpu-passthrough.md` doesn't exist yet: create it.

## Where things are
- USCL repo: `uscl-eglantine/` = how USCL builds Eglantine. `patches/` 0001-0013 apply with `git am` on upstream
  Eglantine nx2-experimental 30181ba05 (github.com/Eglantine-Community/Eglantine; we do NOT contribute upstream).
  `probes/` = optional measurement patches; `bench/` = benchmark harness; `docs/` = handoffs.
- Fork `~/src/eglantine` (branch `uscl` = patches 0001-0011; branch `uscl-gpurw-ship` = 0001-0013 in worktree
  `~/src/eglantine/.claude/worktrees/backcore`, PGO build `build-pgo`). Harness `~/.local/share/uscl/eglantine-bench`
  (same scripts as bench/, plus perf binaries in p/; game paths via USCL_GAME / NSR_GAME).
- Relevant Eglantine code: `src/video_core/gpu_split.{h,cpp}` (front/back GPU threads, ring), `dma_pusher.*`
  (GPFIFO / command lists), `engines/maxwell_3d.cpp`, `engines/kepler_compute.cpp`, `engines/puller.cpp`,
  `memory_manager.*` (guest GPU VA -> device address), `renderer_vulkan/*`, NX2 services `core/hle/service/nx2_*`.

## User rules
- Emulator-wide work only, no per-game hacks. The backend is optional (env switch, e.g. EDEN_NX2_PASSTHROUGH=1),
  Linux + NVIDIA RTX 30/40 only, falling back to the Vulkan path everywhere else.
- Commit locally on the branch above; push to GitHub only when the user asks (CI budget; ask before tagging).
  Fork changes reach GitHub as patch files in `uscl-eglantine/patches` (verify with `git am` that the folder
  reproduces the branch).
- The user is often away: don't stop to ask; make reasonable calls, write them down, and log progress in
  `uscl-eglantine/docs/HANDOFF-gpu-passthrough.md`.
- Keep USCL's online/launcher layers emulator-agnostic.
- Emulator hygiene: stop emulators with SIGTERM and wait 45 s (SIGKILL leaked GPU channels:
  NV_ERR_STATE_IN_USE, desktop glitches until reboot). Never wait with `pgrep -f "<pattern>"` when the pattern is
  in your own command line. Never compile while a benchmark runs. bench.py finds the emulator with
  `pgrep -x eglantine`, so binaries must be named eglantine. Never use EDEN_NX2_SPIKES.

## GPU safety (this GPU also drives the user's desktop)
A bad pushbuffer causes Xid errors and channel resets. Free channels and allocations on every exit path (including
signals), start with the tiniest possible submission, check `journalctl -k` for Xid after each experiment, and stop
experimenting if the driver reports a stuck state. Translating and patching guest command streams and shaders is
the project and has no limits. The limit is only on how the tool reaches the host GPU: through the driver's normal
interface (the RM ioctls the userspace driver itself uses), no direct register access, no kernel module or firmware
changes. If RM refuses something, write that up as a gate result rather than working around it. Fallback (nouveau kernel + NVK-style submission) means
replacing the user's driver: last resort, ask first.

## Feasibility gates (answer each with evidence before building on it)
1. Driver access: own channel, VA space and mappings on the RTX 4060 through the RM API while the desktop keeps
   running; allocate ADA_A + ADA_COMPUTE_A on it (static evidence says yes; confirm on hardware).
2. Method compatibility: diff the 3D/compute methods MKW actually uses against the host class headers; list what
   needs patching or emulating.
3. Shader compatibility: confirm the Switch 2 GPU's compute capability; disassemble real guest shader binaries
   (Eglantine has them) with nvdisasm for SM86/SM89; account for program headers (SPH) and constant-bank
   conventions.
4. Memory: guest GPU VAs -> host GPU VAs; first pass guest RAM as pinned system memory at the same VAs; uncompressed
   PTE kinds first.
5. Sync: guest semaphores/syncpoints as host semaphore releases into guest memory, exact. Measured: the guest CPU
   rewrites ~0.37% of uniform memory before the host GPU has finished reading it (guest fences run ahead of host
   completion); copies hide that today, passthrough would not.
6. Presentation: get the guest framebuffer on screen (Vulkan external memory import, or a staging copy).

## Plan (stop at a failed gate and write up why)
- Phase 0: standalone C++ tool in the worktree (outside Eglantine): open the RM interface, create a channel, one
  semaphore release, then a compute QMD that writes a buffer, then an ADA_A clear of a small surface; read both
  back. Go / no-go report in the handoff.
- Phase 1: capture one real MKW frame from Eglantine (command lists + referenced memory) and replay it in the tool;
  compare against Eglantine's Vulkan output.
- Phase 2: integrate as the optional backend, reusing Eglantine's GPFIFO / channel plumbing.
- Phase 3: performance (VRAM residency, coherency, fewer sync points). Success: MKW racing > 60 fps, Switch Sports
  Resort unchanged.

## How to measure
`bench/runs.py` (one MKW Grand Prix race per label; racing fps = mean of chunks[5:]; BENCH_FRAMES=6000),
`bench/ab_bins.sh` (interleaved binaries), `bench/nsr.py label 300 25 --press-after 40` (Switch Sports regression
check: ~59.8 fps, ends on the Tennis "press the button" screen). Noise is +-3 fps per run: accept a change only if
it wins at least 2 interleaved long-race pairs. For 1-2% effects use the in-race A/B probe (EDEN_NX2_AB=,
probes/0002).
