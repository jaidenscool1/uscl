You're continuing the NVIDIA GPU passthrough work for Eglantine (the Switch 2 emulator core in USCL,
github.com/jaidenscool1/uscl): an optional backend that runs the game's own NVIDIA command streams and shaders
on the host RTX 4060 through the NVIDIA driver's RM ioctls, instead of translating every draw to Vulkan.
The target: Mario Kart World (MKW) racing past 60 fps with frames as right as the Vulkan backend's.

Read first: ~/src/ps3-steam-np/.claude/worktrees/gpu-passthrough/uscl-eglantine/docs/HANDOFF-gpu-passthrough.md:
the Status table, every "Thirteenth pass" section, and "Next (after the thirteenth pass)". The handoff wins
over this prompt where they differ.

Where things are:
- USCL worktree ~/src/ps3-steam-np/.claude/worktrees/gpu-passthrough, branch agent/jaiden-pc/gpu-passthrough,
  local commits up to 39cbd02 (not pushed). Check git log for anything newer.
- Eglantine fork ~/src/eglantine:
  - branch uscl-passthrough-ship d6a73e86a = USCL patches/0001-0026; `git am` of the series onto the pinned
    base 30181ba05 reproduces its tree exactly (check that after every new patch).
  - worktree .claude/worktrees/ptcap, branch uscl-passthrough ee3310b2c = capture probe (probes/0003) + the same
    backend; build/bin/eglantine there is built from it (configured with
    -DEDEN_NX2_PASSTHROUGH_SDK=/usr/src/nvidia-615.78.08/src). Develop there, commit, cherry-pick to the ship
    branch in a temporary worktree, syntax-check the ship tree's changed files with the probe build's
    compile_commands.json (also with -DNX2_PASSTHROUGH removed: the stub path), export the patch.
  - Backend: src/video_core/nx2_passthrough.{h,cpp} (Host: RM objects, mirror, shadow pages, ASTC arena and
    workers, virtual textures, video memory; Words: the translator; Xlate: per-channel decode state; sole mode:
    Poll, AfterEntry, SoleSyncpoint, WaitHost, SubmitEntry/OnEntrySuspended), nx2_helper_loads.{h,cpp}.
    Sole-mode gates in vk_rasterizer.cpp, puller.cpp, maxwell_dma.cpp, fermi_2d.cpp, kepler_compute.cpp,
    maxwell_3d.cpp, dma_pusher.cpp; nx2_gsv.h calls OnGpuOnlyRegion and OnPresent.
- Env: EDEN_NX2_PASSTHROUGH=1, _WORDS=1, _SOLE=1 (the host GPU is the only executor; async; the mode to work in).
  Knobs: _VRAM_MB (GPU-only regions, 3072), _ASTC_VRAM_MB (2048), _ASTC_THREADS (6 in sole mode),
  _VT_THREADS (2), _ASTC_MB (8192), _SYNC=1 (wait per entry, bisecting), _SNAP=-1:0:f00000:<file> (snapshot of
  the presented image when <file>.now appears), _VTSCAN=1, _WATCH, _DROP, _REREAD (on in sole mode).
- Runs: bench/runs.py label:VARS [label2:VARS ...] with RUN_BIN=<binary> and
  USCL_GAME="/home/main/Downloads/GAME_DUMPS/Switch 2/Mario Kart World NS2/Mario Kart World/MKW [ROM]/base/exefs/main",
  WATCH_STALL_S=600 WATCH_SILENT_S=900 BENCH_FRAMES=1800 (lands in racing). runs.py splits variables at commas
  (one SNAP/WATCH per run). BENCH_TRIGGER=<file> makes bench.py touch <file>.now after its end screenshot and
  wait for the backend's snapshot. In sole mode the debug-server screenshots (bench-<label>.png,
  bench-<label>-end.png) are the host GPU's own frames. Read bench/run-<label>.log after a run.
  Baseline: the Vulkan backend alone races at 54.6-54.8 fps on this bench (interleaved pairs, runs vkA/vkB).
- Profiling: perf is now installed (perf_event_paranoid 2: your own processes, user space). Example:
  `perf record -g --call-graph dwarf -p $(pgrep -x eglantine) -- sleep 20` started ~20 s after
  bench-<label>.png appears, then `perf report --no-children --sort comm,sym`. The no-root fallback is in
  uscl-eglantine/passthrough/tools (ptracer.so preload + sample.sh + agg.py, eu-stack).

State (end of the thirteenth pass):
- Sole mode works in async mode: MKW boots, menus and races on the host GPU alone, no deadlock, no Xid in the
  last ~30 runs. Race 29-34 fps (q2 34.1, st2 31.9). VRAM ~6.6 GiB of 8 in use (GPU-only regions 2.6 GiB, ASTC
  twins 2 GiB).
- Correctness gaps, in order of what a player sees:
  1. Textures at a course's start arrive late: ASTC is decoded on the CPU (Eglantine's software decoder + stb BC
     encode, ~20-50 large textures/s in a race; sole mode decodes every pool header, ~35k), so racers, Lakitu,
     HUD pieces stay black for seconds to minutes; virtual-texture tiles the game rewrites in place wait for the
     background sweep (~10 s a pass), so the ground can be dark and blocky at the countdown.
  2. Dashed seams along triangle edges (helper-lane loads zeroed; the eleventh pass designed the fix: a covered
     quad neighbour's value).
  3. A dark square under the player's kart (also in dual mode; not looked at).
  4. The menu's blurred background can be grey (captured before its textures were transcoded).
- Speed: the GPU thread is the place to look (busy ~40-55%, it waits for the host's releases at every
  cross-channel acquire: Eglantine must keep waiting there, measured: hardware acquires gave wrong frames);
  the last ~30 s of the race run 13-19 fps in every passthrough run (Vulkan ~35 there); guest data is still
  uncached system memory.

Goal of this session, in order:
1. Texture readiness: decode ASTC on the GPU. Options to weigh first (measure, then pick): run Eglantine's ASTC
   decoder (video_core/host_shaders/astc_decoder.comp, used by its Vulkan texture cache) on the Vulkan device,
   which sole mode leaves idle, into memory the backend can map (Vulkan external memory -> RM import, or a
   staging copy); or transcode ASTC to BC on the host GPU another way. Also a faster change signal for
   virtual-texture tiles than the sweep. Goal: no black racers or ground at the countdown in q-style runs.
2. Profile with perf in a race (GPU thread, the game's threads, the workers) and fix what limits the frame rate;
   look at the slow last part of the race in particular. Goal: past Vulkan's 54.7, then 60.
3. The seams (helper-lane fix) and the kart shadow square, if time remains.

Facts that cost time to learn (more in the handoff):
- Sole mode: Eglantine must suspend at acquires (its front reads GPU-written data: traced compute macro output,
  inline QMDs, culled parameters); the host's copies of acquires are dropped; an entry waiting at an acquire is
  submitted up to there first. Host channels never wait for Eglantine, so drains are safe in sole mode.
- Fresh RM video memory is not zero: clear it (the copy engine on the backend's channel -1).
- Virtual-texture contents change in place; a count of non-zero GOBs does not show that.
- A texture's guest bytes must be read after the host's uploads (fences recorded when its header was seen).
- Never write the guest's QMD memory from the host; never leave the host MME mid-call across submissions
  (Xid 13 MISSING_MACRO_DATA); never unmap-then-map a live program page without waiting for the channels
  (Xid 13 Shader Program Header); a newly bound program is read after a drain in sole mode.
- Compare frames at the same moment (snapshot + BENCH_TRIGGER), and judge speed by interleaved pairs or at
  least two runs: single runs vary by +-3 fps.

Rules (from the user):
- Emulator-wide fixes only, no per-game hacks.
- Commit locally on the branches; don't push unless asked. Fork changes reach USCL as patch files
  (uscl-eglantine/patches): one numbered patch per change (git format-patch --start-number, next is 0027) and
  verify git am of the whole series onto 30181ba05 reproduces the ship branch's tree. Before fork commits run
  `python3 -I tools/nx2/catalog.py status` (see AGENTS.md). Keep the probe branch in step (cherry-pick).
- The user is often away. Don't stop to ask: make reasonable calls, write them down, log progress in the
  handoff (add a "Fourteenth pass" section, update the Status table).
- GPU safety (this GPU also drives the desktop): RM ioctls only, no register pokes, no kernel or firmware changes;
  free everything on every exit; after each experiment check `journalctl -k | grep -i xid` and run
  `nvrm_probe clear` (~/.local/share/uscl/ptcap/bin); stop if the driver reports a stuck state; keep the mirror
  complete (the backend turns itself off rather than run with a hole). Watch video memory: the card has 8 GiB and
  also drives the desktop.
- Emulator hygiene: stop emulators with SIGTERM and wait (shutdown can take a minute), never SIGKILL; never
  pgrep -f a pattern that matches your own command line (use pgrep -x); one emulator at a time; no experiments
  while a benchmark or capture runs; never use EDEN_NX2_SPIKES.
- Disk: ~50 GB free; keep the one full capture (mkw21).

End with a short report: what changed (commits, patches), before/after evidence (fps pairs, frames at the same
moment), what's next, and an estimate of overall project completion in percent with a one-line reason per part.
