You're continuing the NVIDIA GPU passthrough work for Eglantine (the Switch 2 emulator core in USCL,
github.com/jaidenscool1/uscl): an optional backend that runs the game's own NVIDIA command streams and shaders on
the host RTX 4060 through the NVIDIA driver's RM ioctls, instead of translating every draw to Vulkan.

Work until done. Done means all of these, measured as below:
1. Mario Kart World (MKW) **racing** (bench chunks 4-11, not the intro) at 60 fps or more in sole mode, and faster
   than the Vulkan backend on the same bench (interleaved pairs, at least two of each).
2. Frames as right as Vulkan's: keep **0 washed-out shots in 5 runs x 84** (`run.sh` counts them), and fix what is
   left: the dark square under the kart, the dashed helper-lane seams, the menu blur captured before its textures
   exist, Switch Sports' grey background / black portrait.
3. Stable: 5 consecutive full MKW runs (boot, menus, race, SIGTERM) and 5 Switch Sports runs (`bench/nsr.py`) with no
   deadlock, no stall, no crash, no Xid, `nvrm_probe clear` OK, no video-memory leak across runs.
Don't stop at a milestone or to ask: after each step, log it in the handoff, commit, and take the next item. Stop
early only if the driver reports a stuck state, or a step needs something only the user can do (say exactly what).

Read first: ~/src/ps3-steam-np/.claude/worktrees/gpu-passthrough/uscl-eglantine/docs/HANDOFF-gpu-passthrough.md:
the Status table, every "Sixteenth pass" section and "Next (after the sixteenth pass)". The handoff wins over this
prompt where they differ.

**Benches run on a test profile, never the user's real config** (the user chose this): `~/.local/share/uscl/eglantine-test`
(a copy of config, keys, NAND with the MKW save, shader cache). `bench/launch.py`, `runs.py`, `nsr.py`, `idle.py`,
`mkw.sh`, `rf.sh` use it by default (`USCL_SWITCH_PROFILE` overrides); its log is
`<profile>/data/eglantine/log/eden_log.txt`; a good copy of its config is `<profile>/qt-config.ini.good`. Never point a
run at `~/.config/uscl/uscl/switch`. Every run: `ulimit -c 0` (an exit crash's core dump is ~15 GB), at least 20 GB
free (the disk is at ~24 GB free; other jobs also use it), afterwards the test config still has `gpu_accuracy=0`
and `engine:sdl` mappings (restore from `qt-config.ini.good` if not), and the real config's checksum is unchanged.
Recreate the run wrapper from the sixteenth pass (`run.sh` in your job tmp): label:VARS, `@PT` = the sole switches
(`EDEN_NX2_PASSTHROUGH=1,_WORDS=1,_SOLE=1,_TIMES=1`), Xid count before and after, `nvrm_probe clear`, VRAM, the
washed count per label (crop (300,150)-(980,560) of each 1280x720 shot, mean luminance > 225), test config check,
real config checksum. Abort if an emulator is already running or free disk < 20 GB.

Where things are:
- USCL worktree ~/src/ps3-steam-np/.claude/worktrees/gpu-passthrough, branch agent/jaiden-pc/gpu-passthrough, local
  commits up to 451c45c plus this prompt (not pushed). Check git log for anything newer.
- Eglantine fork ~/src/eglantine:
  - branch uscl-passthrough-ship 0ffc5efdd = USCL patches/0001-0039; `git am` of the series onto the pinned base
    30181ba05 reproduces its tree exactly (453a5f74; check that after every new patch; the next patch is 0040).
  - worktree .claude/worktrees/ptcap, branch uscl-passthrough 8fc889401 = capture probe + the same backend +
    measuring switches + a PROBE commit (backtrace of the first unmapped GPU read, not shipped); build/bin/eglantine
    there is built from it (ninja in build/). Develop there, commit, cherry-pick to the ship branch in a temporary
    worktree (probe-only capture hooks can conflict: resolve to the ship form), syntax-check the ship tree's changed
    files with the probe build's compile_commands.json with and without -DNX2_PASSTHROUGH, export the patch.
- Runs: bench/runs.py label:VARS with RUN_BIN=<probe binary>, USCL_GAME="/home/main/Downloads/GAME_DUMPS/Switch 2/Mario
  Kart World NS2/Mario Kart World/MKW [ROM]/base/exefs/main", WATCH_STALL_S=600 WATCH_SILENT_S=900, BENCH_FRAMES=3600,
  BENCH_CHUNK_SHOTS=1, BENCH_SHOTS=6 (84 shots a run; runs.py splits variables at commas). Switch Sports:
  `nsr.py <label> 300 25 --press-after 40` with NSR_GAME=".../NSR ROM/NintendoSwitchSportsResort_v1.1.0/updates/65536/exefs/main"
  and the sole switches in the environment (nsr.py does not use runs.py; copy the log after each run). bench.py finds
  the emulator by process name: a copied binary must still be named `eglantine`. Delete screenshots and perf data you
  no longer need.

State (end of the sixteenth pass):
- Washed-out race frames fixed: they were stale texture headers (MKW rewrites 80-200 a frame through its 4 MiB pool;
  shadow pools were refreshed every 100 ms). A pool scanner thread now finds changed pages every 2 ms. 0 washed in
  924 shots; 5 of 5 full MKW runs clean.
- Defaults in sole mode now: upload store on (`BACK_UPLOADS=1` turns it off; it closes boot hangs where a barrier QMD
  Eglantine's back wrote late was copied empty by the host), video memory keyed by guest memory, pool scanner,
  no Vulkan pipeline builds, no writer waits.
- Racing ~20 fps (ws2: chunks 4-11 18.3-21.3), Vulkan ~33 at the same chunks.
- **Switch Sports stall, open:** 3 of 15 sole runs stopped presenting at ~800 presents (~30-40 s): host channels idle,
  no stuck acquire, backend on, no Xid; each with a burst of "Read32 of unmapped GPU address 0x2004c00400000021"
  just before (an address read out of data that was not there yet). 8 clean runs since. Vulkan is fine (59.8 fps).

Plan, in order:
1. **Switch Sports stall.** Run nsr.py with the probe build until one stalls (check the first ~800 presents); read the
   backtrace logged after the first "Read32 of unmapped" line, find which Eglantine reader computed that address
   and why its data was missing (upload store, GPU-only video memory, the header scanner are the suspects), fix it
   emulator-wide, then 5 clean Switch Sports runs.
2. **Speed.** At ~20 fps the guest main thread, the GPU front and the host GPU are each ~50% busy and do not overlap.
   Measure before changing (flat `perf` per thread, `/proc` per-thread CPU, `eu-stack` sampling for off-CPU waits;
   DWARF call graphs lost most of the front's samples). Candidates from the handoff: the ~14k front acquire waits a
   second (NVN fences between channels), the host GPU per pass (`GPUTIME`: zcull off, compression fallbacks,
   helper-lane trampolines, textures in system memory), ASTC transcode threads (3 x ~40% of a core while racing),
   the present's CPU de-tile (~10 ms a frame). Ruled out this pass: TSG switches on kept acquires (kn1), the front's
   writer waits (wf1/wf2).
3. Correctness: kart shadow square, helper-lane seams, menu blur, Switch Sports background/portrait.
4. Stability pass: 5 MKW + 5 Switch Sports full runs on the final build.

Facts that cost time to learn (more in the handoff):
- One 84-shot run's washed count is noisy (0-15% at the same settings): never conclude from one run. Observe
  instead: `EDEN_NX2_PASSTHROUGH_WASHED=1` logs each frame's crop luminance and the 32 frames before a washed one.
- Intermittent faults need several runs per arm before a bisect means anything (the Switch Sports stall fooled a
  one-run bisect).
- In sole mode Eglantine must not read GPU-written data before the host wrote it, and must not write guest memory
  the host also writes; GPU-only regions live in video memory Eglantine cannot read.
- Never write the guest's QMD memory from the host with fixups (the host writes inline QMDs at their guest address
  with the guest's own words); never leave the host MME mid-call across submissions (Xid 13 MISSING_MACRO_DATA);
  never unmap-then-map a live program page (Xid 13 SPH); fresh RM vidmem is not zero; a plain fence passes before
  the work's writes land (use WFI fences), and cached guest memory needs an L2 flush before the CPU reads it.

Rules (from the user):
- Emulator-wide fixes only, no per-game hacks.
- Commit locally on the branches; don't push unless asked. One numbered patch per change in uscl-eglantine/patches
  (git format-patch --start-number); verify git am of the whole series onto 30181ba05 reproduces the ship tree. Before
  fork commits run `python3 -I tools/nx2/catalog.py status` (AGENTS.md). Keep the probe branch in step. Log progress in
  the handoff (a "Seventeenth pass" section, Status table updated) as you go. Never claim frames are right from a few
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
pairs, washed counts per 84 shots, Switch Sports runs), what's left, and an estimate of overall project completion
in percent with a one-line reason per part.
