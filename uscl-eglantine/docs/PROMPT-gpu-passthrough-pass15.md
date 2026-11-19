You're continuing the NVIDIA GPU passthrough work for Eglantine (the Switch 2 emulator core in USCL,
github.com/jaidenscool1/uscl): an optional backend that runs the game's own NVIDIA command streams and shaders on
the host RTX 4060 through the NVIDIA driver's RM ioctls, instead of translating every draw to Vulkan.

Work until done. Done means all of these, measured the same way as the fourteenth pass:
1. Mario Kart World (MKW) **racing** (bench chunks 4-5, not the intro) at 60 fps or more in sole mode, and faster than
   the Vulkan backend on the same bench (interleaved pairs, at least two of each).
2. Racing frames as right as Vulkan's: compared at the same moment (snapshot + BENCH_TRIGGER, chunk shots), no black
   or stale textures at the countdown, no dashed seams, no dark square under the kart, menu blur right.
3. Stable: 5 consecutive full runs (boot, menus, 1800 race frames, SIGTERM) with no deadlock, no crash, no Xid,
   `nvrm_probe clear` OK, no video-memory leak across runs.
Don't stop at a milestone or to ask: after each step, log it in the handoff, commit, and take the next item. Stop
early only if the driver reports a stuck state, or a step needs something only the user can do (say exactly what).

Read first: ~/src/ps3-steam-np/.claude/worktrees/gpu-passthrough/uscl-eglantine/docs/HANDOFF-gpu-passthrough.md:
the Status table, both "Fourteenth pass" sections and "Next (after the fourteenth pass, continued)". The handoff
wins over this prompt where they differ.

Where things are:
- USCL worktree ~/src/ps3-steam-np/.claude/worktrees/gpu-passthrough, branch agent/jaiden-pc/gpu-passthrough, local
  commits up to a61f837 plus this prompt (not pushed). Check git log for anything newer.
- Eglantine fork ~/src/eglantine:
  - branch uscl-passthrough-ship b38bc8c7d = USCL patches/0001-0030; `git am` of the series onto the pinned base
    30181ba05 reproduces its tree exactly (check that after every new patch).
  - worktree .claude/worktrees/ptcap, branch uscl-passthrough 508eb69b3 = capture probe (probes/0003) + the same
    backend; build/bin/eglantine there is built from it (ninja in build/). Develop there, commit, cherry-pick to the
    ship branch in a temporary worktree, syntax-check the ship tree's changed files with the probe build's
    compile_commands.json (also with -DNX2_PASSTHROUGH removed: the stub path), export the patch.
  - Backend: src/video_core/nx2_passthrough.{h,cpp}; sole-mode gates in vk_rasterizer.cpp, puller.cpp,
    maxwell_dma.cpp, fermi_2d.cpp, kepler_compute.cpp, maxwell_3d.cpp, dma_pusher.cpp, gpu.cpp (present to the back),
    renderer_vulkan.cpp (VulkanMutex); GPU ASTC in vk_texture_cache.cpp (TranscodeAstcForPassthrough).
- Env: EDEN_NX2_PASSTHROUGH=1, _WORDS=1, _SOLE=1. Measuring: _TIMES=1 (front idle / acquire waits / presents every
  5 s, and acquire waits per channel and semaphore), _GPU_ASTC_CHECK=<n>. Other knobs in the handoff.
- Runs: bench/runs.py label:VARS with RUN_BIN=<probe binary>, USCL_GAME="/home/main/Downloads/GAME_DUMPS/Switch 2/Mario
  Kart World NS2/Mario Kart World/MKW [ROM]/base/exefs/main", WATCH_STALL_S=600 WATCH_SILENT_S=900 BENCH_FRAMES=1800,
  BENCH_CHUNK_SHOTS=1 (a screenshot per 300-frame chunk; chunk 4 is the race start), BENCH_TRIGGER for snapshots.
  runs.py splits variables at commas. Read bench/run-<label>.log after a run. Vulkan baseline: racing chunk ~36 fps
  (vkA/vkB), re-measure it with chunk shots at the start of the session.
- Profiling: perf works for user space only (perf_event_paranoid 2): no off-CPU or context-switch samples. Use the
  _TIMES counters for waits. EDEN_NX2_GPU_PROFILE wedges sole mode at ~55 s: don't use it until that is fixed.

State (end of the fourteenth pass):
- Race start fully textured (GPU ASTC on the idle Vulkan device, VT sweep by chunk hashes). Racing ~12-14 fps.
- The race is bounded by the GPU front: per second ~380 ms in acquire waits, the rest work, no idle. Channels 1 and 2
  hand off ~40 times a frame through semaphores 0x2105849000/010 and 0x2105837000/010 (~1 ms per hop: a host-GPU
  round trip through Eglantine, because sole mode drops the host's acquires), plus four per-frame NVN fences
  (0x21f6a97000-140, 5-9 ms waits). Ruled out: texture placement, poll/doorbell latency, drains, the present.

Plan, in order (measure before each change, and decide from the numbers):
1. **Hardware acquires.** Let the host channels execute the guest's acquires (SEM_EXECUTE and SEMAPHORE_D) and let
   Eglantine's front parse past them, keeping it behind the host only where it reads GPU-written data. First find
   exactly which front reads follow the hot acquires (instrument: log the methods/macros channel 2 runs after
   0x2105849010 resumes, and every front read of guest memory that the host GPU wrote since: MME DMA reads, inline
   QMDs, QMD barrier payloads, `_REREAD` macro parameters). Then pick: (a) the host MME runs those macros itself
   (understand the "inline QMD one word late" behaviour first, eleventh/twelfth passes), or (b) targeted host waits
   only at those reads. Redesign `Host::Drain` / `_REREAD` so they never wait on a channel blocked in hardware behind
   a release that isn't submitted yet (so2's failure). Gate it behind an env switch until frames match Vulkan; hw1
   (thirteenth pass) is the cautionary result.
2. Front CPU: a sole-mode filter for what the split ring ships to the back (most of it is gated off there); keep what
   the back still needs (3D sync method, inline/constant uploads). Then the backend's own per-entry cost.
3. Remaining frame-rate limits after 1-2, re-profiled (front, back, game threads, host GPU utilisation, sysmem
   traffic): guest data in uncached system memory, VT copies uncached, whatever the numbers show next.
4. Correctness: helper-lane seams (eleventh pass design: a covered quad neighbour's value), the kart shadow square,
   one-time captures before their textures exist (menu blur), the flying black-and-white sprite.
5. Video memory: drop or shrink the ASTC twins (no racing gain measured), keep the card under ~7 GiB in a race.
6. Stability pass (the "done" criteria above), then a Switch Sports check (bench/nsr.py) so nothing else regressed.

Facts that cost time to learn (more in the handoff):
- In sole mode Eglantine must not read GPU-written data before the host wrote it; hardware acquires gave wrong
  frames when it did (hw1).
- A guest syncpoint action can run on the GPU front in sole mode (SoleSyncpoint -> Composite); anything that records
  Vulkan work outside the back needs Nx2Passthrough::VulkanMutex(); never Defer to the back while holding a lock the
  back takes (the syncpoint manager's, the backend's state mutex).
- ImageInfo built from a TIC needs native_tiling = MemoryManager::UsesNativeTiling() for the GPU decoders.
- Never write the guest's QMD memory from the host; never leave the host MME mid-call across submissions (Xid 13
  MISSING_MACRO_DATA); never unmap-then-map a live program page without waiting for the channels (Xid 13 SPH); fresh
  RM video memory is not zero.
- Judge speed by racing chunks and interleaved pairs (single runs vary +-3 fps); compare frames at the same moment.

Rules (from the user):
- Emulator-wide fixes only, no per-game hacks.
- Commit locally on the branches; don't push unless asked. One numbered patch per change in uscl-eglantine/patches
  (git format-patch --start-number, next is 0031); verify git am of the whole series onto 30181ba05 reproduces the
  ship tree. Before fork commits run `python3 -I tools/nx2/catalog.py status` (AGENTS.md). Keep the probe branch in
  step. Log progress in the handoff (a "Fifteenth pass" section, Status table updated) as you go.
- GPU safety (this GPU also drives the desktop): RM ioctls only, no register pokes, no kernel or firmware changes;
  free everything on every exit; after each experiment check `journalctl -k | grep -i xid` and run `nvrm_probe clear`
  (~/.local/share/uscl/ptcap/bin); stop if the driver reports a stuck state; keep the mirror complete; watch video
  memory (8 GiB card, also the desktop's).
- Emulator hygiene: SIGTERM and wait (shutdown can take a minute), never SIGKILL; pgrep -x, never pgrep -f a pattern
  matching your own command line; one emulator at a time; no builds or experiments while a benchmark runs; never use
  EDEN_NX2_SPIKES. If a run wedges (log stops, "Special puller engine method" floods), stop it with SIGTERM.
- Disk: ~45 GB free; delete perf data and core dumps you made when done with them; keep the capture mkw21.

When done (or stopped), end with a short report: what changed (commits, patches), before/after evidence (racing fps
pairs, frames at the same moment), what's left, and an estimate of overall project completion in percent with a
one-line reason per part.
