You're continuing the NVIDIA GPU passthrough work for Eglantine (the Switch 2 emulator core in USCL,
github.com/jaidenscool1/uscl): an optional backend that runs the game's own NVIDIA command streams and shaders on
the host RTX 4060 through the NVIDIA driver's RM ioctls, instead of translating every draw to Vulkan.

Work until done. Done means all of these, measured as below:
1. Mario Kart World (MKW) **racing** (bench chunks 4-11, not the intro) at 60 fps or more in sole mode, and faster
   than the Vulkan backend on the same bench (interleaved pairs, at least two of each).
2. Racing frames as right as Vulkan's: **0 washed-out frames in 5 runs x 84 shots** (`BENCH_SHOTS=6 FRAMES=3600`,
   washed = mean luminance > 225 in the crop (300,150)-(980,560); Vulkan's brightest is 199), no black or stale
   textures at the countdown, no dashed seams, no dark square under the kart, menu blur right.
3. Stable: 5 consecutive full runs (boot, menus, race, SIGTERM) with no deadlock, no crash, no Xid, `nvrm_probe clear`
   OK, no video-memory leak across runs.
Don't stop at a milestone or to ask: after each step, log it in the handoff, commit, and take the next item. Stop
early only if the driver reports a stuck state, or a step needs something only the user can do (say exactly what).

Read first: ~/src/ps3-steam-np/.claude/worktrees/gpu-passthrough/uscl-eglantine/docs/HANDOFF-gpu-passthrough.md:
the Status table, every "Fifteenth pass" section (especially "washed-out race frames" and its **Incident**
paragraph) and "Next (after the fifteenth pass)". The handoff wins over this prompt where they differ.

**Before any emulator run (the fifteenth pass damaged the user's setup):** `bench/runs.py` runs Eglantine with the
user's REAL config (`~/.config/uscl/uscl/switch/config/eglantine/qt-config.ini`). An exit crash truncated it once
(defaults regenerated: `gpu_accuracy` High keeps the GPU thread split off and sole mode then hangs at boot; the user's
SDL controller mappings were lost), and crash core dumps (~15 GB each) filled the disk. So:
- Ask the user once whether to move the benches to a separate test config/data dir (a copy of the config, keys and
  the MKW save; AGENTS.md asks the same for RPCS3 testing). If yes, do that first; if no, keep the guards below.
- Every run: `ulimit -c 0`, at least 20 GB free, and afterwards check the config still has `gpu_accuracy=0` and
  `engine:sdl` mappings (restore from `qt-config.ini.good-2026-11-19.bak` next to it if not). The fifteenth pass's
  wrapper did this: recreate it (`run.sh` in your job tmp: label:VARS, `@PT` = the sole-mode switches, Xid count before
  and after, `nvrm_probe clear`, VRAM, config check).
- If boots change suddenly, first diff the `LogSettings: M-` lines against a good run log (e.g. `bench/run-df7.log`).

Where things are:
- USCL worktree ~/src/ps3-steam-np/.claude/worktrees/gpu-passthrough, branch agent/jaiden-pc/gpu-passthrough, local
  commits up to 8799f72 plus this prompt (not pushed). Check git log for anything newer.
- Eglantine fork ~/src/eglantine:
  - branch uscl-passthrough-ship 677119a92 = USCL patches/0001-0035; `git am` of the series onto the pinned base
    30181ba05 reproduces its tree exactly (check that after every new patch; the next patch is 0036).
  - worktree .claude/worktrees/ptcap, branch uscl-passthrough 67efee9bd = capture probe + the same backend + measuring
    switches; build/bin/eglantine there is built from it (ninja in build/). Develop there, commit, cherry-pick to the
    ship branch in a temporary worktree, syntax-check the ship tree's changed files with the probe build's
    compile_commands.json (also with -DNX2_PASSTHROUGH removed: the stub path), export the patch.
- Env: EDEN_NX2_PASSTHROUGH=1, _WORDS=1, _SOLE=1 (+ _TIMES=1 for wait counters). Switches from the fifteenth pass:
  `_BACK_UPLOADS=0` (upload store, experimental), `_STAMPS=1`, `_CATCHUP_ALL=1`, `_QMD_LOG=1`, `_SYNC=1`, `_HWACQ=0`;
  the rest in the handoff.
- Runs: bench/runs.py label:VARS with RUN_BIN=<probe binary>, USCL_GAME="/home/main/Downloads/GAME_DUMPS/Switch 2/Mario
  Kart World NS2/Mario Kart World/MKW [ROM]/base/exefs/main", WATCH_STALL_S=600 WATCH_SILENT_S=900,
  BENCH_FRAMES=3600, BENCH_CHUNK_SHOTS=1, BENCH_SHOTS=6 (84 screenshots a run). runs.py splits variables at commas.
  Read bench/run-<label>.log after a run. Delete the multi-shot PNGs you no longer need (they add up to GBs).

State (end of the fifteenth pass):
- Sole mode with hardware acquires: racing ~21-24 fps (df7) vs Vulkan ~40-45 in the same bench; no Xid with defaults.
- **Washed-out race frames in 3-10% of shots in every sole configuration** (fourteenth-pass equivalent too; `SYNC=1`
  lowers it to 1/168; Vulkan 0/102). Ruled out: guest sysmem caching, ring size, front reads of GPU-written data
  (`CATCHUP_ALL`), report timestamps (`STAMPS`), the 3D and Kepler-memory engines' uploads.
- Leading cause: Eglantine's back thread (GPUBack) still writes the compute class's inline uploads (NVN QMDs and
  their patches) and inline QMDs into guest memory, off the host GPU's timeline. `BACK_UPLOADS=0` keeps them in a store
  that Eglantine's own readers patch in (compute QMD reads, front and macro-interpreter MME DMA reads, translator
  `s.i2m` up to 4 KiB): 0 washed in 168 shots (ow1, uw1), but uw2 hit Xid 13 "Shader Program Header" and the backend
  turned off: some Eglantine reader still expects those words in guest memory.

Plan, in order:
1. **Make the upload store safe.** List every read Eglantine makes of guest memory in sole mode (program binds and
   the helper-lane patcher's code reads are the first suspects for the SPH Xid; texture/sampler header reads, the
   translator's mirror reads, `GpuCopyQmd` sources), find which ones read words the back used to upload, route them
   through `SolePatch` or the host timeline. Inline QMDs at their guest address are written only by the back
   (`kepler_compute.cpp`, inline QMD path): decide who writes them on the host timeline. Then 5 x 84 shots with
   0 washed and no Xid before the store goes on by default (patch 0036).
2. Front CPU (~35 ms a frame; 60 fps needs <16): profile with perf (user space works) and cut what sole mode does not
   need (3D front macros' bookkeeping, compute front ring sends, per-bind work, the translator's per-word decode).
3. NVN fence hand-offs (~200 ms/s of front waits), host GPU per pass (zcull off, compression fallbacks, one TSG per
   channel), the present on the host copy engine: measure each before changing (`_TIMES`, `GPUTIME`).
4. Correctness: kart shadow square, helper-lane seams, one-time captures before their textures exist (menu blur),
   then the stability pass and the Switch Sports check (bench/nsr.py).

Facts that cost time to learn (more in the handoff):
- Judge washed frames with 84+ shots per run: at a 5% rate, 12-24 shots prove nothing.
- In sole mode Eglantine must not read GPU-written data before the host wrote it, and must not write guest memory
  the host also writes (its back runs ahead of or behind the host).
- Never write the guest's QMD memory from the host with fixups; never leave the host MME mid-call across submissions
  (Xid 13 MISSING_MACRO_DATA); never unmap-then-map a live program page (Xid 13 SPH); fresh RM vidmem is not zero.
- An emulator exit segfault is common; with core dumps on it costs ~15 GB of disk each time.

Rules (from the user):
- Emulator-wide fixes only, no per-game hacks.
- Commit locally on the branches; don't push unless asked. One numbered patch per change in uscl-eglantine/patches
  (git format-patch --start-number); verify git am of the whole series onto 30181ba05 reproduces the ship tree. Before
  fork commits run `python3 -I tools/nx2/catalog.py status` (AGENTS.md). Keep the probe branch in step. Log progress in
  the handoff (a "Sixteenth pass" section, Status table updated) as you go. Never claim frames are right from a few
  shots.
- GPU safety (this GPU also drives the desktop): RM ioctls only, no register pokes, no kernel or firmware changes;
  free everything on every exit; after each experiment check `journalctl -k | grep -i xid` and run `nvrm_probe clear`
  (~/.local/share/uscl/ptcap/bin); stop if the driver reports a stuck state; keep the mirror complete; watch video
  memory (8 GiB card, also the desktop's).
- Emulator hygiene: SIGTERM and wait (shutdown can take a minute), never SIGKILL; pgrep -x, never pgrep -f a pattern
  matching your own command line; one emulator at a time; no builds or experiments while a benchmark runs; never use
  EDEN_NX2_SPIKES. If a run wedges, stop it with SIGTERM. The user may close a stuck emulator window themselves.
- Disk: keep >20 GB free; delete perf data, screenshots and core dumps you made when done; keep the capture mkw21.

When done (or stopped), end with a short report: what changed (commits, patches), before/after evidence (racing fps
pairs, washed counts per 84 shots, frames at the same moment), what's left, and an estimate of overall project
completion in percent with a one-line reason per part.
