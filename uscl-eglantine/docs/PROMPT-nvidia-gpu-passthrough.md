# Prompt: NVIDIA GPU passthrough backend for Eglantine (Switch 2)

Paste everything below the line into a new Claude Code session. It is self-contained: the new session
may run on another account without access to earlier conversations or the local memory files.

---

You are starting a research-and-build project for **USCL** (github.com/jaidenscool1/uscl, public repo):
an optional **GPU passthrough backend** for **Eglantine**, the Switch 2 emulator core USCL ships. Instead of
translating every guest draw into Vulkan, the backend should hand the game's own NVIDIA GPU command streams
and shaders to the host NVIDIA GPU, with only light patching. Other names for the technique: "pushbuffer
passthrough", "native GPU execution" (by analogy with NCE on the CPU side), close to "mediated GPU passthrough"
in virtualization. As far as we know nobody has shipped this for a console emulator, so treat it as research:
prove feasibility in small steps and stop early with a clear write-up if a gate fails.

## Why (measured, 2026-11-16)
- Switch 2 GPU = NVIDIA Ampere (T239). Host here = RTX 4060 8 GB (Ada, successor of Ampere), i5-10600K
  6C/12T, Arch Linux, **NVIDIA open kernel module 615.78.08** (`/proc/driver/nvidia/version` says
  "Open Kernel Module": its ioctl interface is public source, github.com/NVIDIA/open-gpu-kernel-modules).
- Mario Kart World (MKW) racing runs ~48-50 fps; the goal is > 60. The limit is Eglantine's GPU **back
  thread** (~87% busy): ~100k draws/s, ~4.5-5 us each, re-resolving bindings and translating state to
  Vulkan per draw. Draw preparation is 57% of that thread, command-stream processing ~20%. The cost is flat
  (no function above ~12%) and bindings change on almost every draw (pipeline same as the previous draw
  30%, nothing changed 1.9%), so caching only trims it: 60 fps needs ~25-30% less back-thread time.
- Passthrough removes the per-draw translation itself. Expected next limits: guest CPU JIT (ARM on x86,
  guest cores ~47% busy now), CPU/GPU memory coherency, exact sync.

## Where things are
- USCL repo: `uscl-eglantine/` = how USCL builds Eglantine. `patches/` 0001-0013 apply with `git am` on
  upstream Eglantine `nx2-experimental` 30181ba05 (github.com/Eglantine-Community/Eglantine; we do NOT
  contribute upstream). `probes/` = optional measurement patches (EDEN_NX2_BIND_STATS stage probes,
  EDEN_NX2_AB in-race switch A/B, EDEN_NX2_UBO_LIVE_PROBE, EDEN_NX2_PROFILE_FAST). `bench/` = benchmark
  harness scripts. `docs/HANDOFF-gpu-rewrite.md` (latest measurements, what was tried and rejected) and
  `docs/HANDOFF-perf.md` (older perf history). Read both handoffs first.
- On the user's machine (if you are on it): fork `~/src/eglantine` (branch `uscl` = patches 0001-0011;
  branch `uscl-gpurw-ship` = 0001-0013 in worktree `~/src/eglantine/.claude/worktrees/backcore`, PGO build
  `build-pgo`); USCL worktree `~/src/ps3-steam-np/.claude/worktrees/switch2-eglantine`, branch
  `agent/jaiden-pc/switch2-grace` (PR #18); harness `~/.local/share/uscl/eglantine-bench` (same scripts
  as `bench/`, plus perf binaries in `p/`; game paths via USCL_GAME / NSR_GAME).
- Relevant Eglantine code: `src/video_core/gpu_split.{h,cpp}` (front/back GPU threads, ring),
  `dma_pusher.*` (GPFIFO / command list reading and snapshots), `engines/maxwell_3d.cpp` (3D class
  methods), `engines/kepler_compute.cpp` (compute/QMD), `memory_manager.*` (guest GPU VA -> device
  address), `renderer_vulkan/*` (the translation you would bypass), NX2 services `core/hle/service/nx2_*`.

## User rules
- Emulator-wide work only: no per-game hacks. The backend must be optional (env switch, e.g.
  EDEN_NX2_PASSTHROUGH=1), Linux + NVIDIA RTX 30/40 only, falling back to the Vulkan path everywhere else.
- Commit locally on a branch in a worktree; push to GitHub only when the user asks (CI budget: batch
  commits, push rarely, ask before tagging releases). Fork changes reach GitHub as patch files in
  `uscl-eglantine/patches` (verify with `git am` that the patch folder reproduces the branch).
- The user is often away: do not stop to ask; make reasonable calls, write them down, and log progress as
  you go in `uscl-eglantine/docs/HANDOFF-gpu-passthrough.md`.
- Keep USCL's online/launcher layers emulator-agnostic (Xbox 360 and PS4/PS5 cores are planned).
- Emulator hygiene: stop emulators with SIGTERM and wait 45 s (SIGKILL leaked GPU channels:
  NV_ERR_STATE_IN_USE, desktop glitches until reboot). Never wait with `pgrep -f "<pattern>"` when the pattern
  is in your own command line. Never compile while a benchmark runs (the emulator's debug server starves and
  the watchdog kills the run). bench.py finds the emulator with `pgrep -x eglantine`: binaries must be named
  `eglantine`. Never use EDEN_NX2_SPIKES (it corrupted rendering).
- GPU safety: this GPU also drives the user's desktop. A bad pushbuffer causes Xid errors and channel resets.
  Always free channels/allocations on every exit path, start with tiny submissions, check `dmesg` for Xid
  after each experiment, and stop experimenting if the driver reports a stuck state.

## Feasibility gates (answer each with evidence before building on it)
1. **Driver access.** Can a userspace process create its own GPU channel (GPFIFO), VA space and memory
   mappings on the RTX 4060 and submit raw pushbuffers, through the open kernel module's RM API
   (`/dev/nvidiactl`, `/dev/nvidia0`, `/dev/nvidia-uvm`; NV_ESC_RM_ALLOC / CONTROL / MAP_MEMORY ...), while
   the proprietary userspace driver keeps running the desktop? Lead to verify: tinygrad's NV backend talks to
   the NVIDIA kernel driver directly this way for compute (QMDs) on Ampere/Ada without CUDA; read it. Then the
   key question: can a non-driver client allocate the **3D class** (Ada: ADA_A 0xC997; check which class the
   Switch 2 guest uses, likely AMPERE_B 0xC797) on its channel? Fallback if RM blocks it: nouveau kernel +
   NVK-style submission (DRM_NOUVEAU_EXEC), which means replacing the user's driver: last resort, ask first.
2. **Method compatibility.** Diff the 3D/compute class methods MKW actually uses against the host class
   (class headers in NVIDIA open-gpu-doc and Mesa's nvk). List what needs patching or emulating.
3. **Shader compatibility.** Find the Switch 2 GPU's compute capability. CUDA's rule is that SASS runs on
   the same major version at an equal or higher minor (RTX 30 = 8.6, RTX 40 = 8.9), so guest shaders may run
   unmodified on Ada: verify with real guest shader binaries (Eglantine has them; disassemble with nvdisasm
   for sm_86 / sm_89). Graphics shaders also carry program headers (SPH) and constant bank conventions.
4. **Memory.** Map guest GPU VAs to host GPU VAs. First pass: the guest RAM backing as pinned system
   memory at the same VAs (slow over PCIe, but correct); later VRAM residency for hot resources with
   coherency tracking. Block-linear tiling is the same family; start with uncompressed PTE kinds.
5. **Sync.** Guest semaphores/syncpoints as host semaphore releases written straight into guest memory.
   They must be exact: measured today, the guest CPU rewrites ~0.37% of uniform memory before the host GPU
   has finished reading it (guest fences currently run ahead of host completion), which copies hide but
   passthrough would not.
6. **Presentation.** Get the guest framebuffer to the screen: import the memory into Vulkan
   (external memory) or copy through staging.

## Plan (stop at a failed gate and write up why)
- Phase 0: standalone C++ tool (outside Eglantine): open the RM interface, create a channel, run a compute
  QMD that writes a buffer, then use the 3D class to clear a surface, read both back. Go / no-go report.
- Phase 1: capture one real MKW frame from Eglantine (its command lists plus the memory they reference) and
  replay it on the host GPU in the standalone tool; compare against Eglantine's Vulkan output.
- Phase 2: integrate as the optional backend, reusing Eglantine's GPFIFO / channel plumbing.
- Phase 3: performance (VRAM residency, coherency, fewer sync points). Success: MKW racing > 60 fps,
  Switch Sports Resort unchanged.

## How to measure
`bench/runs.py` (one MKW Grand Prix race per label; racing fps = mean of chunks[5:]; BENCH_FRAMES=6000),
`bench/ab_bins.sh` (interleaved binaries), `bench/nsr.py label 300 25 --press-after 40` (Switch Sports
regression check: ~59.8 fps, ends on the Tennis "press the button" screen; Tennis needs a motion swing).
Noise is +-3 fps per run: accept a change only if it wins at least 2 interleaved long-race pairs. For 1-2%
effects use the in-race A/B probe (EDEN_NX2_AB=<Nx2Perf switch>, probes/0002).
