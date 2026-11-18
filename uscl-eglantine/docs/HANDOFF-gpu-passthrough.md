# GPU passthrough backend: progress log

Goal and plan: `PROMPT-gpu-passthrough-continue.md`. Hand the game's own NVIDIA command streams and
shaders to the host RTX 4060 (AD107) through the RM ioctls, instead of translating every draw to Vulkan.
Host: driver 615.78.08 (open kernel module), Arch Linux.

## Status (2026-11-18, thirteenth pass)

| Gate / phase | Result |
|---|---|
| Phase 0 (standalone RM tool) | **GO**: own channel, semaphore release, compute launch, ADA_A clear all verified on hardware, 10/10 runs, no Xid, no VRAM leak |
| Gate 1 (driver access) | **Pass** on hardware |
| Gate 2 (method compatibility) | **Pass (real data)**: every 3D/compute/copy method in a captured MKW frame is a named Ampere method, plus three private 3D methods (0xF74/0xF7C/0xF80) that ADA_A accepts; only host-class changes needed (Tegra syncpoints 0x70/0x74 dropped) |
| Gate 3 (shader compatibility) | **Pass (decode level)**; guest compute SASS has started to **execute** on the RTX 4060 (Phase 1), no per-instruction failure seen yet |
| Phase 1 (capture + replay one frame) | **Road and seams localized, new capture** (eleventh pass): mkw19's near road / mid band / pylon / beam are MKW's virtual-texture pages the capture lacks (8192x8192x68 ASTC arrays: Eglantine keeps written layers only in host-only SparseLayers images; not a passthrough bug); the dashed seams come from `PT_HELPER_LOADS` zeroing per-instance UV loads in helper lanes (fix designed: quad-neighbour value); new capture `mkw21` (race-start countdown, pack in view) with per-pass HDR snapshots 300-7900: 19.9 dB, 0.93x, trees only in ours from entry 6784 (a macro-40 draw split across GPFIFO entries; which side is right is open). Before: **Shadow mask fixed, open course checked** (tenth pass): the race-start slabs were the cached Z16 atlas read in the depth GOB arrangement (guest depth PTE kinds; the capture holds Eglantine's colour-arrangement download): depth twins in `ptcap.py build` take `mkw19`'s shadow mask from 17.8 to **28.0 dB** vs Eglantine; `SET_SRGB_WRITE` forced to 0; the 3D macro trace is paired by call order (21 pixel programs the helper-lane patch had missed). `mkw19` 17.6 dB, 1.015x; open-course `mkw20` **22.9 dB world, 0.986x**, dirt 30.6 dB. Left, localized to entries: mkw19 near road / pylon / top beam / Mario's shadow; mkw20 post-chain wedges and seams (entry 2926). Before: **Exit check done, helper-lane fix** (ninth pass, 2026-11-17): the `PT_HELPER_LOADS` trampoline zeroed global loads in *covered* pixels (PIXLD.COVMASK's predicate is no helper test); with `SR_THREAD_KILL` the race-start frames replay lit: `mkw17` 9.6 -> **13.9 dB** (brightness 0.38 -> 0.89x), `mkw19` **17.6 dB** (1.03x), no Xid. Left: the shadow mask (cached shadow atlas tiles), gantry darker, foliage. Earlier in the pass: the replay matches Eglantine wherever the capture holds the frame's inputs (`mkw16`: 22.6 dB, HDR main view 1.00-1.05x of Eglantine's per-pass snapshots); the remaining mismatches are inputs Eglantine keeps only in host images it never writes back to guest memory (superseded: the zero virtual-texture samples were the trampoline bug, see "Ninth pass, continued"). Phase 2 plan written (see "Phase 2 plan"). Before: **Close to Eglantine** (eighth pass, 2026-11-17): capture `mkw16` replays to Eglantine's own render within **PSNR 22.6 dB, mean abs 9.1, brightness ratio 1.04**, HUD identical; left: Toad heads dark, terrain dirt paler, foliage differs (see "mkw16" below). Before: **Textured racing frame, exact reference**: capture `mkw15` replays with every sampled ASTC texture decoded (`PT_ASTC=decode`) to the right image: arch, signs, track, karts, HUD all match Eglantine's own render of the same frame (taken from the capture's end dump), aligned to the pixel; lit surfaces are 0.33-0.65x as bright as the reference (sky 1.00x): a missing light contribution, the next Phase 1 item. Before: **Lit racing frame** (seventh pass, 2026-11-17): capture `mkw14` (probe 0003 now flushes GPU-written images) replays to the lit scene; only ASTC (white stand-in) is left between it and the reference. Before: **Whole racing frame replays** (sixth pass, 2026-11-17): all 7757 entries of capture `mkw13` (racing, 3 channels) run on the RTX 4060 with no Xid, **~36 ms of GPU wait in total** (slowest entry 0.7 ms) with guest memory in VRAM. The fix was emulator-wide: Ada does not perform global loads in pixel-shader helper lanes, so `PT_HELPER_LOADS=1` routes every LDG of every pixel program through a trampoline that zeroes the destination in helper lanes. Output not yet right: the HUD layer matches the reference, the G-buffer shows the right scene (karts, track), but the lit scene composited under the HUD is black (next: find the lighting/composite passes). Earlier: the intro-flyover frame `mkw10` replays to a recognisable course image (third pass) |
| New: texture formats | **ASTC done for replay** (eighth pass; Phase 2: transcode on upload to BC7/BC3, see "Phase 2 plan"): the frame's 1455 sampled ASTC textures are decoded with Eglantine's own decoder to A8B8G8R8 in a host arena and their headers repointed. 28% of MKW's texture headers are ASTC (8x8, 6x6, 4x4, 5x5); the RTX 4060 has no ASTC sampling. Phase 2 needs the same on upload, and a smaller target (BC1/BC3) for VRAM |
| Phase 2 | **Sole executor works, async** (thirteenth pass, continued; textures in video memory and fresher copies: `patches/0024`-`0025`, race 28.6 fps with frames right): `EDEN_NX2_PASSTHROUGH_SOLE=1` (`patches/0021`-`0022`): Eglantine parses, the host GPU alone executes (draws, copies, queries, releases; syncpoints after the host's fences; acquires split the submission); MKW boots, menus and races in async mode with complete frames, no deadlock, no Xid. **Phase 3 started** (`patches/0023`): GPU-only regions in video memory: race **26.3-26.8 fps vs Vulkan 54.6-54.8** (interleaved pairs; 14.2 with everything in system memory). Next: textures in video memory (ASTC arena 4.3 GiB and VT copies 2.3 GiB are system memory the GPU samples over PCIe), helper-lane seams, one-time captures before their textures are transcoded. Before: **Virtual textures on the host GPU** (thirteenth pass): MKW's 8192^2 x 84 ASTC arrays (and its 8192^2 images) transcoded per 64x64-block cell into sparse BC3 copies (`patches/0020`); the race frame's track, canyon and buildings are textured (`bench/pt-live-vs-vulkan-race-vt.png`), menu vs Vulkan 26.1 dB, 0.998x; ~2.3 GiB of BC3 mapped in a race; no Xid in 8 runs. Open: dual-executor deadlocks on zeroed NVN semaphores (2 of 8 runs), SYNC racing drops to ~3 fps after ~45 s. Next: sole executor (plan below), then VRAM residency. Before: **The guest's own words run** (twelfth pass): `EDEN_NX2_PASSTHROUGH_WORDS=1` (fork e5091605d = `patches/0017`); MKW boots to the menu and race on the RTX 4060; menu vs Vulkan **20.7 dB, 1.01x** with sampled ASTC transcoded to BC1/BC3 (`patches/0018`); race frame matches except the virtual-texture track/buildings (black, `patches/0019`); `SYNC=1` correct (~7 fps), async stable for ~10M entries without Xid but races Eglantine's memory writes: next: virtual-texture (sparse ASTC) transcoding, host-as-sole-executor, VRAM residency. Before: **Steps 2b-3 done** (eleventh pass, continued): every guest channel on its own host channel with async submission (-1% racing fps next to Vulkan with doorbell batching), pixel programs patched for helper lanes on bind (C++ port byte-identical to ptcap on 210 programs; 237/237 patched programs read back right at their guest VAs). Fork 043f82389 = `patches/0016`. Next: the guest's own words (QMD dependence, inline-QMD macros, texture header pool, host-class rewrites). Before: **Step 2 done** (eleventh pass): guest VA space mirrored inside Eglantine (sparse RM range, guest memory as OS descriptors at the guest's VAs and kinds), checked live: 128556 entries read back at their guest VA by the host copy engine, 0 differ; hole punching off under passthrough (pinned pages went stale). Fork 65397093a = `patches/0015`. Next: submit the guest's own words (async), then steps 3-5. Before: **Step 1 done** (tenth pass): an RM channel follows guest channel 1 inside Eglantine (`EDEN_NX2_PASSTHROUGH=1`, fork `uscl-passthrough-ship` / `patches/0014`), 15.45M serialized submissions in an MKW race, no Xid; the per-entry wait costs 37-43 -> 13-21 fps, so later steps submit asynchronously. Next: step 2 (guest VA space) |
| Gate 4 (memory) | **Pass** (eleventh pass): sparse range + OS descriptors, no size limit up to 12 GiB, ~210 ms/GiB to pin, every MKW kind maps, live readback check 0 differences |
| Gates 5-6 | not started |

## Phase 0 tool: `uscl-eglantine/passthrough/nvrm_probe.cpp`

Build: `uscl-eglantine/passthrough/build.sh [out dir]` builds `nvrm_probe` and `pt_replay` (RM helpers shared in
`nvrm.h`; driver SDK headers from `/usr/src/nvidia-615.78.08/src`,
mesa class/QMD headers from `~/src/ext/mesa-nouveau/src/nouveau/headers/nvidia/classes`, both overridable
with `NV_SRC` / `MESA_CLASSES`).

Run: `nvrm_probe alloc|sem|compute <sass>|clear <sass>` (each step includes the ones before it); experiments:
`mthd <offset> <value>` (one ADA_A method), `barrier <sass> [prog]` (empty-grid QMD, release only),
`inline <sass> [vid|sys] [ab|b|bb|b1]` (the compute QMD as an inline QMD), `macro <sass> <macro file> <qmd file>`
(a guest compute macro on a bare channel). `<sass>` is the
raw `.text` of an sm_89 cubin of `extern "C" __global__ void k(unsigned* p) { p[threadIdx.x] = 0xA0000000u + threadIdx.x; }`
(`nvcc -arch=sm_89 -cubin`, then `dd` the `.text.k` section; param at `c[0][0x160]`, 8 registers).

Call sequence (all through `/dev/nvidiactl` + `/dev/nvidia0`, no UVM, no root):
root client → device (vaMode OPTIONAL_MULTIPLE_VASPACES) → subdevice → `FERMI_VASPACE_A` (RM-owned, flags 0)
→ `NV01_MEMORY_VIRTUAL` on it (limit 1 TiB: semaphore addresses are 40-bit) → 2 MiB contiguous vidmem
(CPU map write-combined over BAR1, GPU map with `NV_ESC_RM_MAP_MEMORY_DMA`) → 64 KiB sysmem error notifier
→ `AMPERE_USERMODE_A` (doorbell at +0x90) → TSG (`KEPLER_CHANNEL_GROUP_A`, engine GRAPHICS) →
`FERMI_CONTEXT_SHARE_A` **SUBCONTEXT_SYNC** (veid 0; async subcontexts cannot run graphics) →
`AMPERE_CHANNEL_GPFIFO_A` (ring @0, USERD @0x2000 of the same vidmem) → ADA_A, ADA_COMPUTE_A,
AMPERE_DMA_COPY_B on the channel → TSG schedule → work-submit token.
Cleanup: freeing the root client frees everything (atexit + SIGINT/TERM/HUP); closing the fd does the same on SIGKILL.

Measured:
- `sem`: host semaphore release (WFI) visible to the CPU 7-11 µs after the doorbell (300 µs on a first run).
- `compute`: one 32-thread QMD v3 launch, all 32 words right, the word after untouched, ~500 µs (includes first
  GR context load). QMD settings follow tinygrad's Ada path; cbuf0 driver params at 0x18..0x30
  (shared window, local window, 0xfffdc0 stack top), local memory sized from GR info.
- `clear`: ADA_A `CLEAR_SURFACE` on a 64x64 A8B8G8R8 pitch target, 4096/4096 pixels = 0xff407fff
  (1.0, 0.5, 0.25, 1.0; hardware rounds 0.5 to 0x7f), 64 guard words untouched, ~1.8 ms.
- GR info: 12 GPCs x 6 TPCs x 2 SMs (litter maxima), 48 warps/SM, sm_version 0x809.
- `journalctl -k`: no Xid at any point; `nvidia-smi` memory back to the 563 MiB baseline after every run.

## Gate 2: methods (static)

- mesa `clc797.h` (AMPERE_B, guest) vs `clc997.h` (ADA_A, host): with the class prefix normalised, the
  `#define` sets are identical (1300 method names, every field and enum). Same for `clc7c0.h` vs `clc9c0.h`
  (compute, 249). So per the public headers the only rewrite is SET_OBJECT 0xC797→0xC997, 0xC7C0→0xC9C0.
- QMD v3 (`clc7c0qmd.h` vs `clc9c0qmd.h`): same layout; Ada turns Ampere reserved bits 372..380 into
  `SM_DISABLE_MASK_EXT_*`. A guest QMD has them 0, which means no SMs disabled: harmless.
- Not covered by headers: methods the guest uses that the public headers don't name, and behaviour that
  depends on the T239's configuration (SM count / TPC masks, zcull and tile cache sizes, memory kinds).
  Phase 1 must diff the methods MKW actually sends.

## Gate 3: shaders

- Guest code is 128-bit Volta-family SASS (Eglantine `src/shader_recompiler/frontend/ampere/`, encoding
  reference Mesa NAK `sm70_encode.rs`); Eglantine lowers it to Maxwell 64-bit for its old frontend, so
  `vulkan.mythos.bin` holds the lowered copy, not the guest code.
- Raw guest programs: `EDEN_NX2_SHADER_DUMP=<dir>` (shader_environment.cpp) writes every program the cache
  meets for the first time; boot with an empty `XDG_CACHE_HOME` to catch all of them.
- Dump of MKW (2026-11-16): boot with an empty cache dir, ~150 s to the title screen, SIGTERM (clean exit
  in 4 s, no Xid): 1106 programs, ~980 graphics, ~100 compute, 13 MB. The dump holds the instructions
  only (the graphics SPH, `source_header_size`, is skipped).
- `nvdisasm -b SM86|SM87|SM89` on all of them: 0 errors, 0 unknown ops; SM86 and SM87 text identical,
  SM89 differs in 4 programs only by spelling `F2FP.PACK_AB` as `F2FP.F16.F32.PACK_AB` (same default
  conversion). So the host decodes every guest instruction the same way; matches CUDA's rule that sm_8x
  binaries run on later sm_8y parts.
- Guest conventions seen (example program): global loads through a uniform descriptor from `c[0x0][0x540]`,
  NVN constant banks `c[0x7]`, `c[0xb]`. Passthrough keeps the guest's own constant buffer bindings, so these
  need no translation; what remains is the guest's driver-reserved cbuf0 contents (local/shared windows,
  stack top) matching the host's windows, set by the guest's own SET_SHADER_*_WINDOW methods.
- Open: SPH (graphics program header) format is shared by Turing..Ada per mesa (`nak` writes one format
  for SM75+); confirm by replaying a guest draw (Phase 1). Shader-visible SM/warp IDs and SM counts differ
  (T239 vs AD107): only matters to programs that index by SM ID.

## Phase 1: capture and replay one MKW frame (2026-11-16, in progress)

### Capture: `probes/0003` (`EDEN_NX2_PT_CAPTURE=<dir>`, fork branch `uscl-ptcap`, commit 9bdc9afa5)
Applies with `git am` on 0001-0013 (verified: same tree as the branch). Touch `<dir>/capture.now` in a race;
recording starts at the next present and covers two presents. Writes (layouts in `nx2_pt_capture.cpp`):
`maps.txt` + `dev-*.bin` (GPU VA map and device memory at the start, after flushing every GPU-dirty mapping
back to guest memory: without the flush, data the GPU wrote in earlier frames is missing), the same as
`end-*` when recording stops, `state-<ch>.bin` per channel (3D registers, the compute engine's **native**
Ampere registers (`native_registers`, not the Kepler-layout `regs`), MME code + start addresses for 3D and
compute, MME data RAM, 3D cbuf bindings), `stream.bin` (every entry as executed, prefetched lists as address
0, each entry once even when resumed after an acquire), `qmds.bin` (inline QMDs Eglantine writes) and
`mme.txt` (compute macro trace: calls, register reads, emits) and `kinds.txt` (third pass: the PTE kinds the
guest asked for since boot, `gpu size kind fallback`, from nx2:gpu 212/216; 211 maps and 213 unmaps forget a
range). Run with `bench/runs.py` + a watcher that
touches the file 20 s into the race; `WATCH_STALL_S=300` (the dump stalls the GPU front ~8 s per dump).
A capture is ~48 GB (24 GB per dump; 3.9 GB of it nonzero). Current capture: `~/.local/share/uscl/ptcap/mkw13`
(fourth pass, a racing frame: touched 60 s after the menu screenshot, `BENCH_FRAMES=6000`; `ref-end.png` is the
debug-server screenshot taken when the end dump starts, i.e. the replayed frame). Its run used
`EDEN_NX2_BANK_REUPLOAD=3 EDEN_NX2_PERF_OFF=macro_batch,dma_forward EDEN_NX2_INDIRECT_RECORDS=0` so the 3D macro
trace and `deltas.bin` are complete. Older captures deleted (mkw8-mkw12). **Seventh pass: use `mkw14`** (same
settings, probe with the image flush; mkw13's dumps deleted for disk space, its `ref-end.png` and metadata kept).
Game path for `runs.py`: `USCL_GAME=".../GAME_DUMPS/Switch 2/Mario Kart World NS2/Mario Kart World/MKW [ROM]/base/exefs/main"`.

### Tools: `uscl-eglantine/passthrough/ptcap.py` (+ `pt_replay`)
- `ptcap.py stats <cap>`: methods per class with mesa names. `dump <cap> <entry> [n]`: decoded methods.
  `peek <cap> <va>`: snapshot words. `qmds <cap> [entry] [host readback]`: Eglantine's inline QMDs vs the host's.
  `image <file> <w> <h> <fmt> <mem> <png>`: de-tile a read-back color target (block linear GOBs).
- `ptcap.py build <cap>` writes `<cap>/replay/{plan.txt,push.bin,targets.txt}`. `PT_SPLIT=<entry>` cuts that
  entry into one fenced sub-entry per macro call / launch (numbered entry*1000+n) to bisect a failure;
  `PT_NO_END=1` ignores the end-of-frame dump; `PT_MME=trace`, `PT_NODEP=1`, `PT_ASTC=dummy`, `PT_TSC_NODC`,
  `PT_TIC_COPY`, `PT_TIC_SET=<idx>:<FIELD>=<val>` (one texture header field, names from clc797tex.h),
  `PT_KIND_RANGES=<va>:<size>:<kind>`, `PT_DRAM_CHECK` are replay patches/checks explained below; header and
  sampler pool patches now compose (one copy per pool; before, `PT_ASTC=dummy` silently undid `PT_TIC_COPY`).
  `pt_replay` takes `PT_KIND=<hex>` (all guest memory) and plan lines `mapk <va> <size> <block> <off> <kind>`.
  Fourth pass: `PT_SKIP=<entries>`, `PT_DROP=<regex of method names>`, `PT_PATCH=<va>:<hex bytes>` (e.g. `EXIT`
  `4d790000000000000000800300ea0f00` or `NOP` `18790000000000000000000000c00f00` over shader code: code starts at
  program + 0x80, the SPH), `PT_DELTAS=0`; `pt_replay` `PT_REPEAT=<n>` (whole plan n times, memory kept) and
  `PT_ZERO_RO=1` (the shared zero block read-only: stray writes fault with their address).
  Sixth pass: **`PT_HELPER_LOADS=1` is required for racing frames** (needs nvdisasm); `PT_STATS=<entries>` writes
  4 pipeline counters + timestamps per entry at the first MME data RAM image + 1 MiB (256 B each; read back e.g.
  `f004900000:<n*256>`). Full-frame build: `PT_NO_END=1 PT_MME=trace PT_NODEP=1 PT_ASTC=dummy PT_HELPER_LOADS=1`,
  replay `PT_VIDMEM=5500 pt_replay <cap>/replay`.
- `pt_replay <cap>/replay [entries] [va:size:file ...]`: maps the plan, runs entries one at a time, readbacks
  after the run or a timeout. Each failure so far killed only its own channel (Xid 11/13/31 on pt_replay's
  channel); the probe's `clear` test passes after every one and the desktop never noticed.

What the replay does (all in `ptcap.py build`, the C++ only executes the plan):
- **Memory**: pinned host memory (`NV01_MEMORY_SYSTEM_OS_DESCRIPTOR` over our own anonymous mmap; RM refuses
  CPU maps of its own sysmem here, status 0x1f) mapped at the guest's own VAs with fixed 4 KiB mappings (guest
  VAs 0x114b01b000..0x86a91c6000, all < 1 TiB). Real backing for 2 MiB device chunks with data (~2200, 4.4 GB)
  plus every chunk the frame renders, copies or uploads into; one 64 MiB zero block aliased under all other
  mapped pages so nothing can fault. Host-owned buffers at 0xF0_0000_0000+ (local memory) and 0xF8_0000_0000+
  (channels). PTE kinds from `kinds.txt` (the incompressible fallback for compressible kinds; Turing+ kind numbers
  are the same on T239 and Ada), pitch elsewhere. BAR1 is 256 MiB, hence host memory, not vidmem.
- **Channels**: one TSG + SUBCONTEXT_SYNC ctxshare + GPFIFO per guest channel (3: one with 3D/compute/I2M/2D/
  copy, two compute-only), all on one VA space; entries run in recorded order, each fenced and waited for.
- **State restore** per channel: SET_OBJECT with host classes, each class's MME program and start addresses,
  MME data RAM (3D only: `SET_MME_MEM_ADDRESS` + `MME_DMA_READ` from a host copy; without it the first
  entry trips Xid 13 "Const out of Bound", CCACHE LDCONST_OOB), every nonzero register whose name is plain
  state (`SET_*`, `OGL_SET_*`, `LOAD_CONSTANT_BUFFER_OFFSET`; `SET_BINDLESS_TEXTURE` even when 0), cbuf bindings, local memory moved to a host buffer
  sized for AD107's 72 TPC slots (the guest sized its own for T239; sizes kept: an earlier restore dropped them), zcull off, shadow RAM mode 1.
  Never sent: `SET_FALCON*`, `SET_RESERVED_SW_METHOD*`, `SET_INSTRUMENTATION_*`, `SET_SPA_VERSION` (host keeps 8.9).
  Array lengths are not in the mesa headers: an array ends at the next unrelated method (an unbounded guess
  once sent a non-method, Xid 11).
- **Stream rewrite**: semaphore acquires (`SEMAPHORED` ACQ*, report acquire/trap) dropped (entries are
  serialized), Tegra syncpoints dropped, local memory addresses rewritten, re-encoded.

### Where it stops (2026-11-16, second pass)
Build flags used for the furthest run: `PT_NO_END=1 PT_MME=trace PT_NODEP=1 PT_ASTC=dummy ptcap.py build <cap>`.

1. **Inline QMDs work on Ada exactly as Eglantine models them** (`nvrm_probe inline <sass> [vid|sys] [ab|b|bb|b1]`:
   64/64 words written to memory and the kernel launched, slot in vidmem or host memory, with any order of the
   address methods). The first pass's "Ada differs" came from a probe bug (the slot overlapped cbuf0).
   `LOAD_INLINE_QMD_DATA` is a **sequential 64-word buffer**, not 64 registers: the 64th word writes the QMD out
   and launches it. So the restore must never write it (the old restore's 30 nonzero words shifted every QMD by
   30 words: SKEDCHECK02/03/05/11/14/16 failures).
2. With that fixed, the host MME running NVN's compute macro 0x281 (`CALL_MME_MACRO(57)`) still writes its
   QMD one word late (word 0 = the `SET_INLINE_QMD_ADDRESS_B` value). Same macro code, parameters (53 words
   in the stream) and data RAM (round-tripped, `PT_DRAM_CHECK=1`); cause not found. `nvrm_probe macro` (the
   guest macro on a bare channel) launched but left its slot empty: inconclusive. **Workaround**
   `PT_MME=trace`: macro calls whose trace emits a whole inline QMD (64 `LOAD_INLINE_QMD_DATA` words) are
   replaced by the methods Eglantine's MME emitted (`mme.txt`), on every channel (third pass: chosen by
   content, not start address). Other macros keep the host MME: their traces can be incomplete (Eglantine's
   JIT passes part of macro 0x2ca's upload straight through without the emit hook, 3 of 18 words traced).
3. QMD dependencies: NVN counts finished compute work into a 16-slot ring of barrier QMDs (each QMD's
   `DEPENDENT_QMD0` decrements one, `SEND_SIGNALING_PCAS2_B` 0xC increments it). The ring's counters come from
   Eglantine's scheduler, not hardware state (SKEDCHECK36 underflow). `PT_NODEP=1` drops both (the entries run
   serialized; the barriers only release semaphores whose waits are dropped anyway). Third pass: also
   DECREMENT_DEPENDENCE (action 7), and every QMD launched from memory (SEND_PCAS_A + a schedule action: NVN's
   barrier QMDs, `DEPENDENCE_COUNTER` 2 in memory) gets `DEPENDENCE_COUNTER` and `DEPENDENT_QMD0/1_ENABLE`
   cleared in the replay's copy (otherwise it waits forever: a hang with no Xid).
4. **ASTC** (gate-level): `PT_ASTC=dummy` replaces each ASTC header with a copy of an A8B8G8R8 one (wrong image,
   valid memory). The real fix is transcoding (RGBA8 or BC7) into host memory and repointing the headers.
5. Second pass stop, **solved** in the third pass: TEX FORMAT on a Z16 texture header (below).

### Third pass (2026-11-16): the whole frame replays
Same build flags (`PT_NO_END=1 PT_MME=trace PT_NODEP=1 PT_ASTC=dummy`), capture `mkw10`:
`ran 1219 entries, ~205 ms GPU wait in total, slowest ~17 ms`, no Xid, `nvrm_probe clear` OK after every run.
Each fix is emulator-wide (derived from the guest's own requests or the method stream), none per game:
1. **TEX FORMAT = PTE kind.** Ada samples depth texture headers (Z16, Z24S8 family) only from pages mapped with
   the matching depth kind (Z16 = 1, S8Z24 = 3, ZF32_X24S8 = 4, Z24S8 = 5); ZF32 has no kind of its own and works
   on pitch memory; color headers work on depth-kind pages (an R16 header on the Z16 pages runs). Found with
   `PT_TIC_SET` (COMPONENTS Z16 -> R16 runs; RESERVED_ADDRESS, TILE_WIDTH_IN_GOBS, DEPTH_TEXTURE don't matter)
   and `PT_KIND_RANGES=<texture>:<size>:1` (runs). The second pass's "header ruled out" was wrong: `PT_ASTC=dummy`
   overwrote the `PT_TIC_COPY` pool. NVK encodes D16 the same way as the guest (TIC Z16, UNORM, R001), so the
   header itself is valid. Eglantine maps all NX2 memory as pitch (`nvhost_as_gpu::Nx2Map`), so the probe now
   records the guest's kinds (nx2:gpu 212 `NormalModifyMemoryKind4K`: wanted + fallback kind; 216 sparse maps):
   MKW asks for Z16 (compressible 0xb, fallback 1) on 516 ranges, S8Z24/ZF32_X24S8 on 3, generic on the rest.
   **For Phase 2: the passthrough backend must map guest memory with the guest's (fallback) kinds.**
2. **Per-channel compute MME image.** Eglantine keeps one process-wide `NativeMacroContext` with a 3D image and
   a compute image; a channel's compute macros run the compute image once that channel uploaded through the
   compute class (the two compute-only channels: macro 57 at 0x42), else the 3D image (channel 1: 0x281). The
   probe wrote the 3D image for every channel, so the host ran the wrong macros on channels 2/3 (a hang).
   Fixed in the probe (`KeplerCompute::PtNativeMacroCode/Positions` follow `FrontMacroEngine/Positions`).
3. **Trace substitution by content.** A traced macro call is replaced when its trace is complete: every
   inline-to-memory upload has all its data words and every inline QMD all 64 (300 calls in mkw10; macro 0x2ca's
   partial upload still runs on the host MME). Traced methods now go through the same rewrite as the stream
   (dependence filter, written-memory tracking). Calls pair up with `mme.txt` by count per entry, not start address.
4. **Report releases to unmapped VAs dropped.** NVN's compute macro path emits `SET_REPORT_SEMAPHORE` A/B = 0,
   D = release one word: GPU VA 0, which no guest mapping covers (harmless on the console); on the host it is
   Xid 31 (HUBCLIENT_FE fault @ 0x0) or a stalled channel.
5. **Macro calls cut by an entry boundary.** A method run can continue into the channel's next GPFIFO entry
   (MKW: `CALL_MME_MACRO(40)` ends one entry, its data starts the next); the replay's per-entry fence cut it:
   Xid 13 MISSING_MACRO_DATA. The trailing macro call is carried into the channel's next entry.

The image: `out_host` (target 0x210c51a000, 2560x1440 A8B8G8R8_SRGB, last drawn in entries 1146-1214; the same
picture is in 0x815f0d0000, A8B8G8R8) shows the course (road, arch bridge, crane, buildings) with a strong
white haze and no karts or HUD. The HDR target 0x811e4ff000 stays zero (bound but not drawn). Suspects, in
order: ASTC placeholders (`PT_ASTC=dummy` replaces 38k headers), history/exposure buffers from the start
snapshot, the HUD and karts drawn into a target not read back yet. Eglantine's end dump holds zeros for these
targets (GPU-only regions are not written back to guest memory), and `bench-<label>.png` is taken before the
race, so there is no reference for the captured frame yet.

Next:
- A reference image: let the capture watcher take a debug-server screenshot (`tools/nx2/debug_mcp.py`
  `screenshot`) right after touching `capture.now` (the dump stalls the game on the captured frame).
- Read back the 640x360 targets and the frame's last targets in drawing order (`targets.txt`) to find where
  karts/HUD go; then try `PT_ASTC` off on a few headers to see how much of the haze is ASTC.
- ASTC transcoding plan (RGBA8 or BC7 into host memory, headers repointed).

Also open:
- Memory timing: the end dump poisons rings reused within the frame, so `PT_NO_END=1`; per-entry deltas for
  data the CPU writes during the frame are not built.

### Fourth pass (2026-11-16): racing frames
- **The mkw10 frame was the pre-race course flyover** (captured 20 s after the menu screenshot): no karts or HUD
  is correct there. Haze not from history: `PT_REPEAT=10` gives a bit-identical image (the frame reads nothing
  from the previous one). Not resolved yet; ASTC placeholders remain the main suspect.
- New captures touch 60 s after the menu screenshot (racing) and grab `ref-start/ref-end.png` from the debug
  server when each dump starts (`tools/nx2/debug_mcp.py` `screenshot`).
- **Probe additions** (fork 9bdc9afa5 = `probes/0003`): 3D macro trace (`c`/`e` in `mme.txt`, from
  `Maxwell3D::FrontRunMacro`'s emit); per-entry guest data (`deltas.bin`: bank pages the back read whose bytes
  changed, hooked in `BufferCache` native uniform bank diff; the replay writes them into host memory before the
  entry); the split is drained at each recorded entry so back-side notes carry the right entry. Analysis helper
  idea (not committed): rebuild 3D state per draw from registers + stream + the 3D trace (pipeline program
  addresses, cbuf bindings via `BIND_GROUP_CONSTANT_BUFFER`).
- **Whole mappings backed**: a guest mapping with any data or write gets all its chunks (+1.3 GiB). Before, its
  other pages aliased the shared zero block, which reads back stray writes (a light-list tail read 0x7fff0000).
- **Current stop** (mkw13 entry 4981 of 7757; mkw12 3612): Xid 109 CTX SWITCH TIMEOUT in an indexed draw whose
  pixel shader (clustered lighting: tile index from interpolants and c[9] grid constants, per-tile record of
  0x88 bytes at `c[0][0x520]` = two u32 counts + 64 u16 light indices) runs its light loops far too long.
  Facts: `EXIT` at the shader start, NOPing its two loop back-branches, or forcing both counts to 0
  (`MOV R1/R0, RZ` over the two loads) each make the draw pass; textures (all 14 TEX NOPed), zero-block garbage
  (`PT_ZERO_RO=1`, no fault), uniform staleness (`deltas.bin` on/off, same stop) and VRS (dropping
  `SET_SHADING_RATE_INDEX_SURFACE_*`) are ruled out. The grid itself is sane after a clean run up to the entry
  (counts <= 5) and the base matches; yet forcing the tile offset to 0 (counts 0 there) still hangs. So the
  values the shader loads at draw time differ from what the CPU reads back: suspect stale L1 lines for
  `LDG.E.STRONG.SM` (the grid is rewritten by compute each frame; does the guest's cache invalidation reach the
  host's L1, e.g. private method 0xF74 / `INVALIDATE_SHADER_CACHES`?), or GPU writes not yet in host memory.
  Note: readbacks after a timeout can be stale (the dead channel's L2 is not written back): read GPU-written
  data after a clean run that stops before the failing entry (`pt_replay <dir> <count>`).
  Also ruled out: `PT_INVALIDATE=1` (`INVALIDATE_SHADER_CACHES` 0x1017, instruction/data/constant + flush, on
  every 3D/compute subchannel before each entry): same stop.
- Next: instrument the shader (an `STG` of the loaded count and offset into a debug buffer, encoded from an nvcc
  sm_86 kernel) to see what it actually reads; check whether the pixel stage's cbuf 0 at draw time is really
  0x210a499000 (my binding reconstruction) rather than a per-draw ring slot.

### Fifth pass (2026-11-16): the stop is a timeout on slow draws, VRAM residency built
- **Xid 109 = a draw longer than the context-switch window while the desktop needs the GPU.** Which entry dies
  varies by run (4966, 4981, 5026): entries 4923/4962/4966 take 118-140 ms each, 4981 longer. Not a wrong
  result as such: forcing the light counts to 0, or one loop pass, lets the draws through.
- **VRAM residency works** (`pt_replay PT_VIDMEM=<MiB>`, e.g. 5500): guest blocks get a video memory twin the
  guest mappings use, uploaded by the copy engine (`AMPERE_DMA_COPY_B` on a setup channel; 5.4 GiB in ~1 s);
  deltas and readbacks go through the same copies. GPU time to entry 4983: 582 ms (host memory) -> 274 ms.
  The 120-140 ms draws stay as slow, so memory placement is not their problem. Phase 3 needs this anyway.
- `PT_PREEMPT=1`: GFXP/CILP context buffers (sizes from `NV0080_CTRL_CMD_FIFO_GET_ENGINE_CONTEXT_PROPERTIES`
  0xb-0xf) bound with `NV2080_CTRL_CMD_GR_CTXSW_PREEMPTION_BIND`: RM accepts (status 0), Xid 109 unchanged.
  `PT_GPU_CACHE=1` (host memory GPU-cacheable + L2 invalidate/flush per entry): little change.
- Ruled out this pass: light grid contents (sane after a clean run, <= 5 lights per tile), stale CPU uniforms
  (deltas), the index buffer (static: identical start/end, max index 1328), generic PTE kinds (`PT_KINDS=depth`),
  shader cache staleness. The light grid mapping also holds other data the frame needs (zeroing or writing
  past the grid breaks entry 4966).
- **Measured with `PT_STATS=<entries>`** (3D report PS_INVOCATIONS, pipeline location PIXEL_SHADER 0xA:
  without the location Ada raises Xid 69): 4923 = 10.6 M pixel shader invocations (2.9x the screen) in
  139 ms; **4962 = 31 invocations in 118 ms, 4966 = 15 in 134 ms**. Instrumenting the 4962 and 4981 pixel shaders
  (`RED.MAX` of the loaded light counts and tile offsets into spare words, then `EXIT`) shows counts 0 and tile
  offsets in range: the light loops are not where the time goes. Caution: Xid 109 depends on whether the desktop
  wants the GPU during a long draw, so single "this patch makes it pass" results above are not conclusive.
  With the pixel shaders of 4962/4966 (also used by 4923) replaced by `EXIT`, the whole run to entry 4986 takes
  22 ms of GPU time instead of 273 ms.
- **Open, next**: where 4962/4966 spend 100+ ms with ~30 pixel shader invocations. Measure inside the entry with
  more reports (VS_INVOCATIONS, primitives generated, and the timestamps the four-word reports carry) to see
  whether vertex work (counts, instancing) or a stall dominates; then why these draws cost 100x more than in Eglantine. Leading idea: zcull is off in the replay
  (its on-chip state does not carry over), so a heavy forward-lighting pixel shader behind a depth prepass
  shades every hidden pixel; measure with a pipeline statistics report (pixel shader invocations) around the
  draw, then restore zcull properly (`SET_ZCULL*` state + a zcull region clear) if that is it.
- Shader patch encodings verified with nvdisasm (SM86), for PT_PATCH: `EXIT` 4d790000000000000000800300ea0f00,
  `NOP` 18790000000000000000000000c00f00; `MOV Rd, RZ` from an existing MOV with Rd at bits 16-23 and the replaced
  instruction's control bits (105-127); `STG.E [Ra.U32+UR4+imm24], Rb` = nvcc's `STG.E [R.64]` with bit 90
  cleared and bit 76 flipped; `RED.E.MAX.STRONG.GPU [Ra.U32+UR4+imm]` = nvcc's RED with bit 90 cleared and bit 71
  flipped; LDG `.STRONG.SM` -> `.CONSTANT` = flip bit 77. Control field: stall 105-108, yield 109, write barrier
  110-112, read barrier 113-115, wait mask 116-121.

### Sixth pass (2026-11-17): helper lanes, the whole racing frame replays
- **Root cause of the 100+ ms draws: helper-lane loads.** On Ada, a pixel shader's helper lanes (coverage 0, run
  only for derivatives) do not perform `LDG`: the destination register keeps its old value. MKW's light loops
  load a count with `LDG R5, [R5.U32+UR4]`, so in helper lanes the count *is* the address (~10^9 iterations).
  Proof on 4962's pixel shader (0x1150383300): `PIXLD.COVMASK RZ, P1` (P1 = lane covered) + `@!P1 BRA` past loop 2
  -> 0.87 ms; the same branch taken for covered lanes instead -> 117.75 ms. With the destination elsewhere, the
  helper lane keeps what an earlier warp left in the physical register, so the same draw hangs in some runs only
  (seen as entry 4966 passing at one entry count and timing out at another).
  Ruled out on the way: SPH flags (DOES_LOAD_OR_STORE, SASS_VERSION, bits 21/22), VRS, cache operations, memory
  placement, preemption, GPU caching, tile clamps, zero restore.
- **Fix (emulator-wide), `ptcap.py build` with `PT_HELPER_LOADS=1`:** finds every pixel program the frame
  draws with (3D state replayed with the 3D macro trace, `pixel_programs`), its end (`EXIT; BRA self`), its LDGs
  and an unused predicate (nvdisasm text, `NVDISASM` or PATH), and replaces each LDG with a `BRA` to a trampoline
  in a host arena block (`f004e00000`): the LDG (its control bits, operand reuse cleared), `PIXLD.COVMASK RZ, Pk`
  (on the LDG's scoreboard), `@!Pk MOV Rd(+i), RZ` waiting on it, `BRA` back. Programs the CPU rewrites mid-frame
  (deltas) get each version patched as an extra delta at that entry (28 in mkw13). mkw13: 2774 loads in 221
  programs; 2 programs nvdisasm rejects ("Illegal instruction") are left alone. Result: 4962 118 ms -> 1.0 ms,
  the whole frame (7760 replay entries) runs with no Xid in ~36 ms GPU wait (VRAM) / ~445 ms (host memory,
  4923 then 121 ms: 10.6 M pixel invocations from host memory). Shortcuts (in the code): helper lanes zero the
  destination even when the LDG's own guard was false; the trampoline waits for the load right away; `LD`/`LDS`/
  `ATOM` not covered. **Phase 2 needs this in the shader path** (a real SASS decoder instead of nvdisasm text).
- **Output check (not right yet).** Entry 7727 starts the next frame (clears 28 targets), so read targets with
  `pt_replay <dir> 7729 ...`. The frame's sRGB output `210c31a000` (2560x1440, d6) has the HUD ("05", "1/3") in the
  right places on black; `811844e000`/`811934e000` (d5, G-buffer style) show the race scene with the player's kart
  and the pack ahead, as in `ref-end.png`; the HDR target `8110901000` stays empty until entries 5500-7000 and then
  holds only far objects on the horizon (1.7% of pixels). So geometry renders; the lighting/composite of the opaque
  scene is missing. Note `targets.txt` first/last entries miss target switches done inside 3D macros (the build's
  shadow does not apply the 3D macro trace), so they are hints only.
- **Next:** find the passes between the G-buffer (entry 4923) and the composite (7646-7720), most likely compute
  (light/deferred shading, exposure, tonemap): list compute launches and their texture/image bindings in that
  range, read back their outputs, and compare with Eglantine (a capture-probe dump of the same targets would give
  pixel references). Then ASTC transcoding (`PT_ASTC=dummy` is still a stopgap).

### Seventh pass (2026-11-17): why the lit scene is black
The frame is not deferred. Structure of mkw13 (3D state rebuilt per draw with the 3D macro trace):
depth prepass 689-4863 into `812ee00000` (S8Z24, LESS, color writes off), screen-space passes in 4923 (half-res
RGBA16F `8116f40000`/`81176c1000`, an R32F 640x360 -> 80x45 chain, full-res RGBA8), light-grid compute on channel
2 (4924-4951), then a **forward** pass 4954-7646 into the HDR target `8110901000` (depth EQUAL, writes off), each
entry one small draw (1.1M indices in all). Entry 7540 clears the HDR target again (a second view), so read it
before that (e.g. after 6826).

Two independent causes, both found by measuring inside the pixel shader of entry 6826 (1.98M invocations):
1. **`PT_ASTC=dummy` used a black texture.** The pool's first A8B8G8R8 header (0x100) is NVN's 4x1 opaque black
   default, so every ASTC texture (albedo, the item box, the minimap, "24th") sampled black. `PT_ASTC=dummy:107`
   picks header 0x107 (4x4 white); with it the minimap comes back. Still a stopgap until ASTC transcoding.
2. **The capture misses every GPU-rendered image.** The forward shader's last factor is texture handle 0x16 =
   the half-res RGBA16F target `81176c1000` written in 4923; after 4923 it is 1.2% nonzero, and the signed-RGBA8
   clipmap the frame reads (`8171190000` 6144^2 ... `817ce90000` 768^2, format 0xD7) is exactly zero even after
   entry 4 shades 171k pixels into it. Cause in the probe: `DumpMemory` flushes what `IsMemoryDirty` reports, and
   `RasterizerVulkan::MustFlushRegion` only asks the texture cache when GPU accuracy is High, so the start dump
   flushed 59 MiB (buffers only) and dumped stale guest memory (zeros) for all images the GPU wrote in earlier
   frames. **Fix (probe 0003 update):** `Tegra::PtCapture::Dumping()` is true while a dump flushes, and
   `MustFlushRegion` then consults the texture cache at every accuracy level (`FlushRegion` itself is not gated;
   `DownloadMemory` downloads every downloadable image in the range, so the dirty check keeps it to GPU-written
   ones). **For Phase 2 this does not matter** (the real backend keeps GPU data in GPU memory); it only affects
   captures.

**Result:** capture `mkw14` (same procedure as mkw13, probe with the fix) flushed **951 MiB** at the start dump
(was 59), and its frame (7894 entries) replays with no Xid in ~34 ms GPU wait to a **lit racing scene**: karts,
the pack ahead, track, sky, lighting and shadows (`PT_ASTC=dummy:107 PT_NO_END=1 PT_MME=trace PT_NODEP=1
PT_HELPER_LOADS=1`, output `210a71a000` after entry 7854). Washed out to near-white because every ASTC texture is
the white stand-in. The debug-server screenshots of this run were not written (no `ref-*.png` in mkw14).

**Next:**
1. ASTC transcoding in `ptcap.py build` (the real fix for the washed-out image and the missing HUD art): decode each
   ASTC texture the frame samples (headers in the pool, block-linear layout, mips) to RGBA8 in a host arena and
   repoint its header; compare with Eglantine's own decode. A reference screenshot for mkw14-style frames (fix the
   watcher's `debug_mcp.py` call) makes the comparison exact.
2. Then the Phase 1 exit check: image diff against the reference, and the remaining stopgaps (`PT_NO_END`, deltas
   limited to uniform banks).

Ruled out on the way (each measured, not argued): depth test (EQUAL->LEQUAL and depth test off: same), zcull
(restore sets it off, nothing re-enables it), VRS (palette has no cull entries; image size 0: same), the
prepass depth (the 690 clear and the prepass both land), the host MME for 3D macros (`PT_MME3D=trace`: same
bindings), the helper-load trampolines (same with the site skipped), stale constant caches (`PT_INVALIDATE`), VRAM
vs host memory, previous-frame state (`PT_REPEAT=2`). Pitfall that cost time: a register that looks final at a
shader's output can be rewritten by a later multi-register TEX/LDC (`TEX ... R10` writes R10 and R11); check every
writer, including the second register of 64-bit and texture results, before trusting an instrumented output.

New replay knobs (`ptcap.py build`): `PT_SETV=<method>:<old>:<new>` (rewrite one method value in the stream),
`PT_ASTC=dummy:<hex header>`, `PT_HELPER_SKIP=<program>:<off>+<off>` (leave helper loads alone; the light-loop
loads still need the trampoline or Xid 109), `PT_MME3D=trace` (3D macro calls with a complete trace become
Eglantine's emitted methods; calls that emit `MME_DMA_READ_FIFOED` stay on the host MME, else Xid 13
DMA_READ_FIFOED_FROM_PB). `PT_STATS` now also records DA primitives, clipper primitives and zpass pixels.
Shader instrumentation encodings (verified with nvdisasm SM86): `MOV Rd, Rs` = 0x7202 | Rd<<16 | Rs<<32, 0xf at
byte 9; `I2FP.F32.U32 Rd, Rs` low 0x7245 | Rd<<16 | Rs<<32, high 0x201000 (+ control bits);
`I2FP.F32.U32 Rd, c[b][o]` = 0x7a45 | Rd<<16 | (o/4)<<40 | b<<54, same high word. Output the values through the
pixel shader's color registers into a target whose format can hold them (BF10GF11RF11 caps at 65024).

Facts learned on the way:
- MKW frame: ~1100 entries over 2 presents, ~100k words; 3D draws to 2560x1440 (HDR BF10GF11RF11 scene, sRGB
  RGBA8 output); 167 color targets.
- `SEND_SIGNALING_PCAS2_B` action 0xC (INCREMENT_DEPENDENCE) drives NVN's barrier-QMD ring; empty-grid QMDs
  with program 0 are fine on Ada (`nvrm_probe barrier`).
- The guest CPU writes ~17 MB of GPU-visible memory during a frame (265 64 KiB blocks differ start vs end).
- Eglantine on NX2 runs compute macros on the split's front (`FrontRunMacro`) against `fs.registers`.

### Eighth pass (2026-11-17): ASTC decoded, exact reference, GOB layout fixed
**Result:** capture `mkw15` (probe 0003 with `descriptors.txt`) replays with no Xid (~52 ms GPU wait, ASTC arena in host
memory) to the textured racing frame: the MARIOKART arch and "Mario Bros. Circuit" sign, ticketing building, track,
karts, minimap, portrait and lap box. Against Eglantine's own render of the same frame: aligned to the pixel (phase
correlation 0,0), mean abs error 122 -> 29.5, PSNR 5.6 -> 16.7 dB (before = `PT_ASTC=dummy:107`). Images in
`~/.local/share/uscl/ptcap/mkw15/`: `astc-before.png`, `astc-after.png`, `ref-eglantine.png`, `compare-astc.png`
(before / after / reference / |after - reference| x3).

Full-frame build and run for mkw15 (output `210bb1a000`, 2560x1440 d6 mem 40, last drawn at capture entry 7869 =
replay count 7873):
`PT_ASTC=decode PT_ASTC_LIB=<bin>/libptastc.so PT_NO_END=1 PT_MME=trace PT_NODEP=1 PT_HELPER_LOADS=1 python3 -I ptcap.py build <capture>`,
`PT_VIDMEM=6500 <bin>/pt_replay <capture>/replay 7873 210bb1a000:f00000:<file>` (6.3 GB guest memory in VRAM, the
10.5 GB ASTC arena in pinned host memory: 14.7 GB filled, 62 GB RAM machine). Build ~2.5 min.

What changed:
- **`PT_ASTC=decode` (`ptcap.py build`)**: each sampled block-linear ASTC texture (all mips and layers, 2D/array/
  cube/3D) is read from the snapshot, decoded with **Eglantine's own `video_core/textures/astc.cpp`** (built unchanged
  into `libptastc.so` by `build.sh`, with a stand-in `workers.h` in `astc_shim/`; boost headers from any checkout,
  `BOOST_INC`), written as A8B8G8R8 in the same GOB layout into a host arena above the guest's mappings (2 MiB chunks
  with data packed into blocks 2000+, chunks without data mapped to the zero block), and its header repointed
  (format, address; block height, mips, sRGB and swizzles kept). Layout: `bl_layout` (a port of Eglantine's
  `texture_cache/util.cpp` level sizes, layer stride, mip block shrink) and `bl_runs` (16-byte run offsets);
  `ptcap.py selftest` checks both. `PT_ASTC=decode:<hex header>` picks the fallback header, `PT_ASTC_PNG=<dir>`
  writes level 0 of each texture.
- **Only the textures the frame samples.** MKW's header pool holds 29k ASTC textures (104 GB as RGBA8: four
  8192^2 x 68 layer x 12 mip arrays alone are 23 GB each); decoding all of it wrote a 59 GB file (deleted). The
  probe now records every descriptor `DescriptorTable::Read` sees while recording (`descriptors.txt`); mkw15: 2082
  descriptors, **1455 ASTC textures**, the other 32360 ASTC headers become a copy of the pool's first A8B8G8R8 header.
  Checked: a white fallback (`decode:107`) changes 48 of 3.7M pixels, so the list is complete.
- **Sparse textures.** Two of the sampled 8192^2 x 68 arrays (`81a7a30000`, `836d530000`, virtual-texture style)
  are mapped piece by piece over many mappings with most tiles not resident. `guest_read` now spans mappings (sparse
  and unmapped read zero), all-zero ASTC blocks stay zero instead of decoding to the error colour, and only arena
  chunks with data get memory. Before this the environment (arch, buildings, road) rendered near black.
- **GOB layout: the RTX 4060 uses the guest's 'native' GOB** (Eglantine's NX2 tiling: the legacy yuzu GOB with
  offset bits 5 and 6 exchanged). Decoded ASTC sources are clean only in it, Eglantine's end-dump render decodes only
  in it, and the replay's own readback loses its comb fringes in it. `ptcap.py image` used the legacy layout, so
  **every earlier pass's PNG has a comb on edges** (4-pixel pieces of row pairs swapped; flat areas unaffected). Both
  now use `bl_runs` (native only).
- **Exact reference without a screenshot.** The probe flushes GPU-written images into the end dump, so Eglantine's
  own render of the replayed frame is in `end-dev-*.bin` at the output address (read it with the native GOB). This
  replaces the debug-server screenshot for the Phase 1 image diff.
- Probe 0003 (fork `uscl-ptcap` 07abad9af, `probes/0003` regenerated, `git am` onto its parent gives the same tree):
  `Tegra::PtCapture::NoteDescriptor` from `DescriptorTable::Read` while recording, `descriptors.txt` at stop.

Facts and open items:
- The cyan fill behind the HUD lap box, item box and portrait is in Eglantine's own render too: not a passthrough
  issue.
- **Remaining difference: missing light.** Linear ratio ours/reference: sky 1.00 (so exposure and tonemap agree),
  road 0.34-0.40, karts 0.33-0.47, dirt 0.65, arch 0.54-0.86. Some light contribution is missing or weaker on lit
  surfaces. Next: find it (candidates: GPU-written lighting data the replay starts from, e.g. probe/GI buffers or
  shadow terms; the light-grid compute 4924-4951 in mkw13 numbering; `PT_NO_END` data choices), measuring with
  `PT_STATS`/`PT_PATCH` as in the seventh pass, now with the reference image to diff per pass.
- The debug-server screenshot failure in mkw14 was its 5 s default timeout during the ~23 s dump stall (a timed-out
  request stays pending and the next fails with "already pending"). The mkw15 watcher (`capture15.sh` in the capture
  dir) asked once at the end dump with `timeout_s` 30, got "no answer within 60 s", and **the game never presented
  again after the end dump** (bench watcher: STUCK after 602 s, stopped). The capture itself completed (`done.txt`).
  Cause not found (no stacks): the screenshot request during the dump or the new descriptor hook. Next capture: no
  screenshot (the end dump is the reference); if it still hangs, it is the hook.
- Shortcuts in the code: ASTC is decoded from the frame's start data (uploads during the frame are not seen);
  RGBA8 costs 16x the ASTC 8x8 size (10.5 GB here, host memory; BC1/BC3 re-encode would fit VRAM); 2 textures
  crossing mappings were the sparse arrays above, now handled.
- Captures: `mkw15` is the current one (48 GB); mkw14's device dumps deleted (metadata kept).

### Eighth pass, continued (2026-11-17): hunting the missing light
Lit surfaces are a near-uniform **0.40x** (shadowed and mid-tones) to 0.58x (brightest) of Eglantine's linear
brightness; the untextured sky is 1.00x. Tools added (`ptcap.py`): `endread <capture> <va> <size> <out>` (Eglantine's
end-of-frame bytes at a VA, the reference for `image` and byte diffs; mkw15's final image:
`endread <cap> 210bb1a000 f00000 ref.bin`, `image ref.bin 2560 1440 d6 40 ref.png`), depth targets in `targets.txt`,
and bisecting knobs `PT_SETR=<3D method>:<hex>` (set a register after the restore), `PT_INJECT=<entry>:<va>:<size>`
(Eglantine's end bytes written right before an entry; verified to land), `PT_ASTC_SRGB=0` (decoded ASTC sampled linear).

Measured:
- **`SET_SRGB_WRITE` = 1 (restored from the guest) makes the RTX 4060 sRGB-encode clears into plain A8B8G8R8
  (0xD5) targets**: entry 5021 clears `8117e42000` to (1, 0.5, 0.5, 1) with macro 5 (`CLEAR_SURFACE 0x3c`); the
  replay stored G = 188 = sRGB(0.5), and with clear value 0.25 G = 137 = sRGB(0.25). `PT_SETR=SET_SRGB_WRITE:0` gives
  127 like Eglantine, and the downstream 5021 targets (`811934e000`, `8118203000`) then match Eglantine (means equal);
  the final sRGB output stays right (0xD6 encodes by format). It does **not** change the final brightness (16.4 vs
  16.7 dB). Which behaviour the guest GPU has is open (Eglantine is not hardware); not made a default.
- Not the cause (each measured on the final image): the bloom/downsample chain of entry 7747 (its seven outputs
  injected from Eglantine after 7747: final unchanged), the 128x128 HDR probes of entries 164-574 (within 5%), the
  candidate exposure target `8111802000` injected at frame start, missed ASTC textures (white fallback: 48 pixels),
  end-of-frame data instead of `PT_NO_END`. `PT_ASTC_SRGB=0` brightens to 18.0 dB but desaturates (the arch turns
  pink), so sRGB sampling is right and the gap is a light/exposure factor, not texture data.
- **The end dump is not a per-pass reference for transient memory.** The render-target heap is aliased six times
  (e.g. `2143484000` = `8103be0000` = ... = `8151de0000`: the third shadow cascade and the main depth buffer); depth
  images are not flushed faithfully (main depth reads 0 / 1.0). Only non-aliased outputs (the final image) compare
  reliably. Byte diffs of everything Eglantine changed in the frame (2137 64 KiB blocks, 142 ranges) are dominated
  by this heap.

Next: per-pass references from Eglantine. Probe idea: `EDEN_NX2_PT_CAPTURE_SNAP=<entry>:<va>:<size>[,...]` flushes
and writes those ranges right after that entry while recording (the dump flush path, `Dumping()` true), so the main
view's HDR target before entry 7540 (where it is cleared for a second view), the composite inputs and the light-grid
outputs can be compared pass by pass. Then bisect the 2.5x with `PT_INJECT` at the first pass that differs. The next
capture also tells whether the mkw15 hang was the screenshot (take none) or the descriptor hook.

### Eighth pass, continued (2026-11-17): per-pass references, mkw16 matches
- **Probe 0003: per-entry snapshots.** `EDEN_NX2_PT_CAPTURE_SNAP=<entry>[-<last>[/<step>]]:<gpu>:<size>[,...]` (hex
  gpu/size) flushes those ranges right after that entry ran (the dump flush path) and writes
  `snap-<entry>-<gpu>.bin`: Eglantine's own per-pass results, valid for aliased transient memory too. Fork
  `uscl-ptcap` 4b0dca457, `probes/0003` regenerated (`git am` onto its parent: same tree).
- **Capture `mkw16`** (`capture16.sh` in the capture dir: as mkw15, no screenshot, HDR target `8110901000`
  snapshotted every 50 entries 4900-7550): 7551 entries, 1495 sampled ASTC textures (9.3 GB RGBA8). **The game kept
  running after the dump**, so the mkw15 hang was the debug-server screenshot requested during the end dump (never
  request one while a dump runs; the end dump is the reference anyway). The descriptor hook is fine.
- **Result:** the replay (`PT_ASTC=decode PT_NO_END=1 PT_MME=trace PT_NODEP=1 PT_HELPER_LOADS=1`, `pt_replay <cap>/replay
  7495`, output `210a51a000`) matches Eglantine's render at **PSNR 22.6 dB, mean abs 9.1, median linear ratio 1.04**,
  HUD identical (`compare.png`, `compare-crops.png`, `replay-decode.png`, `ref-eglantine.png` in the capture dir). The
  HDR main view matches Eglantine's snapshots (0.67-0.88x while partly drawn, 1.00-1.05x when complete, 7400-7450).
- **mkw15's 0.4x darkness was specific to that frame** (race start under the gantry); its dumps were deleted for disk
  space before this was known, so its cause is open. Watch for it in future captures with the snapshot tooling.
- Left in mkw16 (localized): Toad spectators' heads render dark instead of lit, the KTV truck's yellow is weaker,
  the terrain dirt is pale sand instead of red-brown and lacks grass tufts, one bush appears only in ours.
  Ruled out: buffers the replay never writes (26 ranges injected from Eglantine at frame start or before the
  composite: no gain), end-of-frame data (19.3 dB, red artifacts on foliage: `PT_NO_END` stays). Next suspects: the
  sparse terrain virtual texture (tiles streamed during play; ASTC decoded from the frame's start data) and the
  Toads' materials; per-pass snapshots of their targets will localize them.
- Disk: `mkw16` is the one full capture (49 GB); mkw15's dumps deleted (metadata and images kept).

### Ninth pass (2026-11-17): what Eglantine read, per-draw localization, Phase 1 exit
**Result:** the remaining differences are localized to single draws and, for the big one, to the capture: in the
race-start frame `mkw17` the road's three virtual-texture fetches return **0 for every pixel** in the replay
(measured in the shader), because the data those textures sample lives in Eglantine's host images and never in
guest memory. Everything the GPU computes from inputs the capture does hold matches Eglantine.

New tools:
- **`pt_replay` readbacks at any count:** `va:size:file@n` reads back right after the n-th entry, so one run (13 s)
  gives every per-pass comparison point (e.g. the HDR target at all 51 snapshot entries). Count for capture entry K:
  the last push.bin record with entry <= K (K+4 in mkw16/mkw17).
- **Probe 0003, deltas from every read site** (fork `uscl-ptcap` d81773d89, `probes/0003` regenerated, `git am`
  onto its parent gives the same tree): besides the uniform bank pages (site 0), `deltas.bin` now holds the bytes
  Eglantine read at buffer uploads (site 1), every uniform bind not over GPU-modified memory (site 2) and texture
  uploads (site 3, GPU VA split over the VA map); site = the record's top 4 bits, a record equal to the previous one
  at its address is dropped. While a dump flushes, the probe logs (once per image) images the texture cache cannot
  download.
- **`ptcap.py build`:** each delta is cut to the bytes that differ from memory as replayed so far (start dump +
  earlier deltas; a restoring write is kept), `PT_DELTA_SITES=<digits>` keeps only those sites,
  `PT_FILL=<entry>:<va>:<size>:<byte>` fills guest memory before an entry. mkw16 rebuilt with it replays
  bit-identically (the old 26 MB of bank records were 57 KiB of real changes).
- `passthrough/tools/`: `drawstate.py <cap> <entry>` (3D state of an entry's last draw from the stream + 3D macro
  trace: pipeline programs, stage 3/4 constant buffers, pools; `DRAWS=1` lists draws whose methods are traced),
  `sass.py <cap> <program> [out]` (nvdisasm of a guest program from the start dump; `NVDISASM_FLAGS=-hex`),
  `startread.py <cap> <va> <size> <out>` (start-dump bytes at a VA across mappings). Ad-hoc scripts (region sweeps,
  float decodes) stay in `~/.local/share/uscl/ptcap/tools/` (`regions.py`, `hdrsweep.py` now takes paths).

Measured (mkw16, the good frame; HDR target `8110901000` vs Eglantine's snapshots, 50-entry windows):
- Racers ahead diverge from 4950-5200 (ours ~0.7x), both KTV trucks at 5700-5800 (0.5-0.65x), Toads at 5850-5900
  (~0.55x), the bush exists only in ours (5950-6000), and the dirt gets a draw in ours at 5700-5750 that Eglantine
  does not have, then is paler at 6450-6750. The same lit-object factor (~0.75x from the first draws) shows in mkw17.

Measured (capture `mkw17`: race start under the gantry, like mkw15; 7851 entries; output `2109d1a000`, replay count
7814; `compare.png`, `replay.png`, `ref-eglantine.png`, `shadowmask-4970.png`, `draw7373-ours-vs-egl.png` in the
capture dir):
- Final image **PSNR 9.6 dB, mean abs 70.1, brightness 0.38x** (Mario, karts and HUD right, world dark): mkw15's
  darkness is back, so it is frame-dependent, not a regression.
- **CPU writes during the frame are not the cause.** With every read site recorded, only **110 KiB** of the bytes
  Eglantine read differ from the start dump (bank 21, buffer uploads 15, uniform binds 73 KiB, texture uploads 0).
  The VA map is identical at start and end (no tile mapping changes). (mkw17 was only replayed with all sites on;
  an A/B with `PT_DELTA_SITES=0` was not run.)
- Per-entry readbacks: entry **7373** (the road, 48% of the screen, the slowest entry at 8.2 ms) writes its pixels
  at **0.018x** Eglantine's (median), 7204 (12% of the screen) likewise; earlier lit draws ~0.75x.
- Every input of draw 7373 was compared, ours before 7373 vs Eglantine: its 14 textures (handles from c2 in the
  pool `2109695000`), its storage buffers (`21f4ef4000`+0x990000: identical; the per-draw ring `2212f2b000`: static),
  its constant buffers (each byte Eglantine bound, from the site-2 deltas: identical; c14 is camera data the shader
  does not read). The only texture that differs, an 88x48x256 R8 volume (`8105c4a200`), changes nothing when
  injected. Ruled out by measurement too: `SET_SRGB_WRITE`=0 (9.56 dB), the 4970 screen-space outputs (the forward
  shader's half-res factor `81176c1000` matches).
- **The shadow mask** (layer 0 of the 2-layer RGBA8 array `811844e000`, written at 4970; G = lit) differs: ours has
  blocky shadow slabs on the near road (G 228 vs Eglantine 253). It comes from a 512x512x101 Z16 shadow atlas
  (`81605f0000`, sampled, not drawn this frame: cached from earlier frames; start = end dump). `PT_FILL` of the
  atlas with 0xff gives a fully lit mask (255) and with 0x00 a fully shadowed one, so the atlas controls the mask,
  but a fully lit mask leaves the final image at 0.39x: secondary.
- **The road's virtual textures read zero.** Patching draw 7373's pixel shader (`114cfad600`, at +0x17e0: `MOV`
  the samples to R0-R2, wait on all scoreboards, `EXIT`) shows the three 8192x8192x68 virtual-texture fetches
  (handle 0x8 and 0xc: ASTC sRGB, decoded into the arena; 0xa: **BC6H**, sampled straight from guest memory)
  return 0 in 100% of the road's pixels; a control texture (0xe) returns plausible colour at the same patch point.
  The BC6H array is fully mapped (5.9 GB of VA over 143 mappings) but only 224 MiB of it is nonzero in the dumps,
  and its device pages are shared with 82 other VAs (e.g. a 513 MB pool at `228e680000`): the game fills tiles of
  a physical page pool through other views and maps them into the virtual textures. Eglantine renders the road
  textured, so it has that data in host images; its texture cache never downloads `Unbacked` or `TrimmedLevels`
  images ("never upload/download"), and its log shows two such images as render targets (the 6144^2 and 3072^2
  signed-RGBA8 clipmaps `8171190000`/`817a190000`: "Render target in unbacked image", the clipmap the seventh pass
  found zero in the capture). Which host image holds the virtual-texture tiles is not confirmed: the mkw18 run's
  log was used up by one framebuffer image (now logged once per image).

**Superseded by "Ninth pass, continued" below: the zero samples came from the helper-load trampoline, not from
missing data.** Conclusion as first written: the replay reproduces Eglantine wherever the capture holds the frame's inputs; what is left
comes from data Eglantine keeps only on the host (unbacked/trimmed/sparse-tile images) and the shadow atlas content.
In Phase 2 none of this applies: the guest's own GPU writes go to real memory on the host GPU, so pools, virtual
textures, clipmaps and the shadow atlas hold what the game wrote. A Phase 1 capture that needs them would have to
read them back from Eglantine's host images during the dump (next item if Phase 1 work continues).

Next (if more Phase 1 fidelity is wanted): capture with the once-per-image log to name the images behind the
virtual-texture pool, the clipmaps and the shadow atlas; then a dump-only download path for them in the probe
(unbacked: per bound layer/tile; trimmed: the levels the host has); re-check mkw17 and mkw16's lit-object 0.75x.

Captures (as first written; see "continued" for the current state): `mkw17` was the one full capture; mkw16's dumps deleted (metadata, snapshots and
images kept); mkw18 (same procedure, logging probe) kept only for its run log (`bench/run-ptcap18.log`).

### Ninth pass, continued (2026-11-17): recapture with the image inventory, helper-lane fix
**Result:** the race-start darkness was a bug in the replay's helper-lane load trampoline, not missing data. Fixed
(emulator-wide, every pixel program): `mkw17` 9.6 -> 13.9 dB (brightness 0.38 -> 0.89x), new capture `mkw19` 17.6 dB
(1.03x), whole frames with no Xid (~55 ms GPU wait, slowest entry 9 ms). Images: `mkw17/compare-helperfix.png`,
`mkw19/compare.png`.

1. **Recapture `mkw19`** (probe logs every texture-cache image over dumped memory once, at each dump; fork
   `uscl-ptcap` eeffa63be, `probes/0003` regenerated, `git am` onto its parent gives the same tree). 3780 images:
   only **two are GPU-modified and not downloadable**, the sparse-tiled lighting clipmaps `8171190000` (6144^2) and
   `817a190000` (3072^2) (unbacked); their contents are missing from every capture. The road's virtual textures
   (`82027b0000` BC6H, `836d530000` ASTC: Sparse, Unbacked, not GPU-modified) hold no data hidden from the dump, and
   no other image covers their memory. The cached shadow atlas `81605f0000` is GPU-modified and downloadable: its
   dump is Eglantine's data, clean in the native GOB layout (`mkw17/shadow-atlas-layers.png`, top row native, bottom
   legacy). So the "data only in host images" reading of the zero samples was wrong (Eglantine's
   `nx2_gsv.h` `ApplyGpuOnlyRequest` re-points those 5.8 GB / 1.4 GB arrays through the memory manager, which the
   probe's VA map records).
2. **The fetch coordinates were zero.** Patching draw 7373's shader to output the BC6H fetch's operands (layer R16,
   x R17, y R12, per Eglantine's own operand decoding in `lower_to_maxwell.h`) gives 0 for 100% of road pixels,
   while evaluating the same address chain on the CPU from the replay's buffers gives in-bounds, nonzero constants.
   The operands come from the per-draw ring (`LDG [c0[0x540] + ...]`). Exempting those loads from the trampoline
   (`PT_HELPER_SKIP`) took the road draw from 0.018x to 0.856x of Eglantine.
3. **Cause:** the trampoline was `LDG; PIXLD.COVMASK RZ, P; @!P MOV Rd, RZ`. Measured in the road shader:
   `PIXLD.COVMASK` returns mask 1 for covered pixels but its **predicate output is 0**, so every covered lane's
   loaded value was zeroed (in all 209 patched pixel programs, since the sixth pass). That also hid the light loop's
   real behaviour: flipping to `@P`, or testing the mask register for 0, brought back the sixth pass's Xid 109
   (CTX SWITCH TIMEOUT at capture entry 5016, the light-grid loop, two times this pass, each recovered, health
   check OK). **Fix:** helper = `SR_THREAD_KILL` != 0 (yuzu's helper-invocation source), computed once per program
   in an entry trampoline (`S2R R0, SR_THREAD_KILL; ISETP.NE.U32 P<free>, R0, RZ`, then the program's first
   instruction; no GPR is live at a pixel program's start), and each LDG trampoline is `LDG; @P<free> MOV Rd, RZ`.
   Programs that start with a branch are left alone (none in MKW). Encodings checked with nvdisasm.
4. With the fix the mkw17 HDR main view tracks Eglantine's snapshots at 1.00-1.08x through entry 7100 (was
   0.4-0.8x); the rest of the gap is the shadow mask (near-road slab, `811844e000` layer 0 green 228 vs 253, from the
   atlas tiles the 4970 pass selects; the atlas data itself is right), the darker gantry, and foliage (a bush only in
   ours, missing grass tufts). Next: localize the mask's tile selection in the 4970 pass (it was not touched by this
   fix), then mkw16's lit-object factor on a new open-course capture.

Also learned: a pixel mask built as "changed by the draw" drops pixels an instrumented draw writes as exactly 0 (the
cleared target already holds 0); use the unpatched replay's mask. `PT_HELPER_SKIP=<program>:<off>+...` is the quick
test of whether a trampoline changes a draw.

Captures: **`mkw19`** is the one full capture (49 GB + 10 GB arena; output `210b01a000`, replay count for entry 7767);
mkw17's and mkw16's dumps deleted (metadata, snapshots and images kept); mkw18 kept only for its run log.

### Tenth pass (2026-11-17): the shadow mask, depth kinds, the 3D macro trace
**Result:** the race-start shadow slabs are fixed. The shadow mask's tile selection was right all along; the
cached Z16 shadow atlas was read in the wrong GOB arrangement. `mkw19` shadow mask (`811844e000` layer 0, entry
4910) **17.8 -> 28.0 dB vs Eglantine (mean abs 0.4)**; final image 17.56 -> **17.60 dB, brightness 1.032 -> 1.015x**;
no Xid in any of the ~15 runs this pass, health check OK after each. Code: USCL `bb2c73c`. Images (in
`~/.local/share/uscl/ptcap/mkw19`): `shadowmask-tenth.png` (G channel: before | after | Eglantine),
`compare-tenth.png` (before | after / Eglantine), `mask-ref-sample-diff.png`, `shadow-atlas-b0000.png`.

1. **The 3D macro trace was misnumbered, and the tools trusted the numbers.** Eglantine's front runs 3D macros out
   of step with the probe's entry counter: mkw19's mask entry 4910 (2292 macro calls) had its 2134 traced calls
   logged as entry 4936 (offset 26; offsets 0/26/31 across the frame). `drawstate.py` and `pixel_programs()`
   paired trace records by entry number, so the mask's draw (and its bindings) were invisible. New
   `ptcap.macro3d_calls()`: each stream call (every `CALL_MME_MACRO(n)` write starts one; NVN batches 1-parameter
   calls as non-incrementing runs, `CALL_MME_DATA(n)` adds parameters) pairs with the next trace record of the same
   start address and parameter count (55756 calls, 50972 paired, 4 s). `pixel_programs()` now finds **307 pixel
   programs (was 286)**: `PT_HELPER_LOADS` had missed 21, one of them with 12 LDGs (`114cc5f900`, 63 draws in
   4910). `drawstate.py` uses it and takes `PS=<program>` (state at the last draw with that pixel program).
   `PT_MME3D=trace` (bisecting only) still pairs by entry number.
2. **The mask shader** (`114caed000`, one draw in 4910, rt0 `811844e000`): R = cascaded shadow (handle 0xc,
   layer = cascade index from the depth splits `c3[0x220/0x224]`, 3x3 PCF for cascade 0); **G = a tiled shadow
   cache**: world XZ -> a 10x10 grid (`c5[0x640..0x66c]`), the cell's layer from a table at `c5[cell*4]` (a
   permutation of 0..100), `TEX.LZ ARRAY_2D` on handle 0xe = TIC `0x72b4` = the 512x512x101 Z16 atlas
   `81605f0000`, then an exponential (ESM) compare with per-cell planes (`c5[0x670+cell*16]`, scale
   `c5[0x1f70+cell*4]`). Measured in the shader: the GPU picks cell 55 / layer 58 for the near road, as the table
   says; the reference depth (from the scene depth buffer) is smooth; the atlas **sample** carried a small regular
   checker that ESM amplifies into the slabs (`mask-ref-sample-diff.png`: reference | sample | difference x 64).
3. **Cause: depth PTE kinds.** The guest maps the atlas with kind 0x0b (Z16, fallback 0x01); on Ada the texture
   unit uses the depth GOB arrangement (mesa nil's `FermiZS`, "grab-bag", undocumented) for depth-kind pages, while
   the capture's bytes are Eglantine's download in the colour arrangement (its texture cache writes every format
   that way). Measured: atlas on a colour kind (6) + header as R16 -> mask 28.0 dB. Ruled out on the way, each by a
   run: layer stride (the RTX samples layers 0xB0000 apart, as Eglantine lays them out; 0xAA000 gives 10.1 dB),
   the header format alone (R16 on the Z16 pages: identical result), the sampler's depth-compare bit (TSC `0x1c8`
   has it set and the fetch is non-`.DC`; clearing it changes nothing), re-uploading the atlas through the guest's
   Z16 mapping with the copy engine (a third arrangement, 16.0 dB; re-uploading *all* depth-kind mappings that way
   wrecks the frame, 6.3 dB: MKW's 250 MB kind-0x0b heaps hold colour data too).
   **Fix (ptcap build, emulator-wide, default on, `PT_DEPTH_TWIN=0` off):** every sampled depth header whose memory
   the frame never draws becomes its bit-identical colour twin (Z16 -> R16, ZF32 -> R32) on a colour kind (6). Other
   depth formats are counted and left alone (mkw19: 4 S8Z24 headers, all drawn in the frame). Kind overrides now
   cut guest kind ranges instead of dropping them (`override_ranges`, in `selftest`). **Phase 2 does not need it**:
   the guest's own depth writes land in the depth arrangement.
4. **`SET_SRGB_WRITE` is now forced to 0** (`PT_SRGB_WRITE=guest` keeps the guest's 1): instrumenting the mask
   shader showed the RTX sRGB-encodes the *shader outputs* into the UNORM mask (59/255 stored as 132), not just
   clears; sRGB-format targets encode by format either way (eighth pass). NVN sets 1 and keeps data in UNORM
   targets, so the guest GPU does not encode them; Eglantine's Vulkan backend and NVK ignore the method. Phase 2:
   the same host rewrite.

Per-pass HDR (`8110901000`) after the fix vs Eglantine's snapshots: median ratio 0.997-1.016 at every 50-entry
point, but a minority of pixels is brighter (mean +28% after 6600). Localized windows for the next per-draw pass:
near road darker in ours from 6200-6250 (0.180 vs 0.265), left gantry pylon 1.36x in 6350-6400 and 2x in
6500-6550, the top beam 1.18x in 6500-6550; Mario's kart shadow is right through 6600 and lost later.

Tools: `ptcap.macro3d_calls`, `drawstate.py PS=`, `PT_DEPTH_TWIN`, `PT_SRGB_WRITE`. Shader instrumentation
encodings added (SM86, checked with nvdisasm): `FFMA.FTZ Rd, Ra, imm, Rc` = `0x7823 | Rd<<16 | Ra<<24 | imm<<32`,
high `Rc | 0x10000`; `MOV Rd, imm` = `0x7802 | Rd<<16 | imm<<32`, high `0xF00`; `MOV Rd, c[b][o]` = `0x7a02 |
Rd<<16 | (o/4)<<40 | b<<54`, `0xF` at byte 9; `FRND.FLOOR Rd, Rb` = low `0x7307 | Rd<<16 | Rb<<32`, high `0x205000`.
The mask target writes R, G, B only (A is not written).

### Tenth pass, continued: open-course capture `mkw20`
**Result:** open-course racing replays at **22.9 dB in the world area (HUD excluded), brightness 0.986x**; the
banked dirt matches Eglantine (**30.6 dB, 0.999x**). mkw16's "pale dirt" does not reproduce. What is left is
local and starts in the post chain, not in lighting. Images in `~/.local/share/uscl/ptcap/mkw20`: `compare.png`
(ours / Eglantine), `post-wedge-2926.png` (the composite target after 2926, 2927, 2928 / Eglantine's final).

- Capture: `capture20.sh` (capture19.sh, touched 110 s after the menu screenshot, `BENCH_FRAMES=7200`, extra
  shadow-mask snapshots). The menu screenshot now lands on "Start Grand Prix?" and the race starts ~25 s later, so
  the frame is lap 1 on the banked dirt turn (Mario, mushrooms, a coin; the pack, KTV trucks and Toads are **not
  in view**, so those mkw16 items stay unchecked). It is a **3037-entry frame** (mkw19: 7854): the passes run
  ~3000 entries earlier (HDR main view `8110901000` at 359-2926, composite `811e4ff000` 2926-3001, output
  `210bb1a000` sRGB at 2933-3000), so the 4900+ snapshots never fired: **no per-pass reference**. Next capture:
  take snapshots relative to the frame (e.g. `0-8000/50`), or detect the HDR target's range first.
- Final vs Eglantine's end dump: 19.4 dB overall; the HUD differs because the end dump is taken after the next
  present (item box, place and lap text in ours, a later HUD state in Eglantine's). Not a replay difference.
- **Dark wedges** (shaped like the shadows of the chevron signs above the track; Eglantine has no such shadows)
  and **dashed seams** along triangle edges (top-left wall; the same seams show on mkw19's road). Localized with
  per-entry readbacks: the affected pixels are not dark in the HDR main view through 2900 (mean 0.91); they appear
  at **entry 2926**, the composite into `811e4ff000` (pixel programs `114ba6ad00`, `114ba6b900` = 648
  instructions, ~50 `TEX.LZ` taps: a wide post filter, and the composite `114ba6e600`); 2927 adds 4.6% of the
  screen. Next: name 2926's inputs (`drawstate.py . 2926`, then instrument `114ba6e600` as in the tenth pass).
- Disk: mkw20's dumps were **deleted after the analysis and mkw19 kept** (the rule says keep the newest, but mkw20
  has no per-pass snapshots, and mkw19's open windows need them). mkw20 keeps metadata, `replay/` (minus the
  arena) and images.

### Eleventh pass (2026-11-17): patch 0014 verified, the road's missing virtual-texture pages, seams
**1. Patch 0014 on its own: verified.** `uscl-passthrough-ship` (5e536b6a4) had never been compiled. Built in the
`ptcap` worktree's build dir (checkout of the ship branch, incremental ninja: 103 targets, 2 min, no errors, only
the usual `-Wno-error-all` warning), then the probe branch rebuilt the same way. MKW with `EDEN_NX2_PASSTHROUGH=1`
on that build (`runs.py pt2stop`, `BENCH_FRAMES=900`), SIGTERM: the live `eden_log.txt` read before the next run
shows `Nx2 passthrough: shutting down after 3118193 entries` then `RM client freed (status 0x0)`; no Xid, health
check OK, VRAM back to 752 MiB. (Racing 54.6-60 fps in that short run, against 13-21 in the step-1 measurement on the
probe build: not investigated; the step-1 number is the one to re-measure in step 2.) Note: `runs.py` needs
`USCL_GAME` in the environment; without it `launch.py` dies at once and the "live log" is the previous run's.
No backend change, so `patches/0014` is unchanged.

**2. mkw19 per-draw windows: near road, pylon, top beam.** Readbacks at every entry in each window
(`tools/winmeans.py`, regions picked from `tools/ratiomap.py`):

| Window | Entry | Draw (pixel program) | Ours / Eglantine after it |
|---|---|---|---|
| road (6200-6250) | **6249** (also the frame's slowest entry, 8.3 ms) | one draw, `114d3ad600` | near road **0.51x**, mid band 1.68x |
| pylon (6350-6400) | **6366** | one draw, `114d251900` | pylon 0.96 -> 1.33x |
| pylon + top beam (6500-6550) | **6502** | one draw, `114d2b4200` | pylon 1.66x, beam 0.94x |

All three are the same material family (~980 instructions; the same 20 fetches). Inputs checked against Eglantine's
end dump (race start: static camera, as in the tenth pass): shadow mask `811844e000` (0.8% bytes differ), half-res
RGBA16 `81176c1000` (float noise), G8R8 `81069fe400` (1.1%), froxel R8 volume `8105c4a200` (identical), specular
LUT `227fbd1000` and reflection cube array `227f6c4000` (identical), the light grid at `c[0][0x520]` (`21f52f4000`,
all zero in both: no local lights) and the per-instance records at `c[0][0x540]` (identical).
Instrumented (PT_PATCH, `passthrough/tools/movpatch.py` writes `MOV Rd, Rs` [+ `WAIT=1`: wait on all scoreboards], checked with
nvdisasm; `passthrough/tools/slice.py` = backward data slice of a register in nvdisasm output; `tools/exp.sh` = build + replay
to an entry + readback + Xid/health check; `tools/instr.py` = per-row means and a PNG). Results on the road:
- the final colour is `A + B`, A (emissive) ~0; the lighting sum (sun included) is a uniform ~1.41-1.44 over the
  road: **the light is right**. The difference is the material colour: raw albedo (`R20`, handle 0xe = ASTC 8x8
  `217c4b4800`, our decode checked mip by mip: consistent) is grey asphalt in ours; Eglantine's near road is brown.
- the material blends its layers with weights read from MKW's **virtual-texture page cache**: handles 0x8/0xa/0xc =
  8192x8192x68, 12-mip arrays (`81a7a30000` ASTC 8x8 sRGB, `82027b0000` BC6H, `836d530000` ASTC 8x8). Read right
  after the fetch (E8): **both ASTC arrays return exactly 0 over the whole road**, near and mid; only BC6H has data.
- **Cause: the capture lacks the VT pages.** In guest memory only layers 0-1 of the ASTC arrays hold data (start and
  end dumps identical; layers 2-67: 0-2% non-zero blocks), and the replay writes none during the frame. Eglantine's
  image for them (`run-ptcap19.log`, `PtCapture dump image`) has flags `0x4c12f3` = Converted | Sparse | Unbacked |
  TrimmedLevels | **SparseLayers**, `gpu_modified false downloadable false`: a layer gets host memory when something
  writes it and is never downloaded. So pages written into those layers (by the GPU, in earlier frames) live only in
  Eglantine's host image. Same class as the unbacked clipmaps (item 5), **not a passthrough bug**: in Phase 2 the
  guest's own writes land in guest memory. Pylon (6366) and beam (6502) bind the same three arrays (handles read
  back): consistent with the same cause, not instrumented per draw. Mario's shadow on the near road: our mask's R has
  it; the surface it multiplies is the one with zero VT terms (not instrumented separately). The bush: not looked at.
- To close it in Phase 1 the probe would need a dump-only readback of backed layers of SparseLayers/Unbacked
  Converted images (decoded RGBA8, as side files the ASTC arena could use directly) plus the trimmed-level base
  (Eglantine samples from `host_level_offset`). Not done: Phase 2 does not need it.

**Dashed seams (road, also mkw20's wall).** Isolated in the road shader's additive term (`R19`). Its slice holds the
per-instance UV transform loaded with LDG at the program start (`0x60`/`0x90`) feeding implicit-LOD fetches and
`TMML.LOD` (derivatives across the 2x2 quad). `PT_HELPER_LOADS` zeroes LDG results in helper lanes, so in quads on a
triangle edge the helper lanes' UVs are garbage and the derivatives (LOD) of the covered pixels are wrong: seams
along edges. The console's helper lanes load the real value. Fix (also for Phase 2 step 3): give a helper lane a
covered quad neighbour's loaded value (`SHFL` within the quad: exact for per-instance data) instead of 0; it needs
a scratch register (raise the stage's register count, `SET_PIPELINE_REGISTER_COUNT`). Hypothesis from the slice,
not yet tested (skipping the trampoline for this program is unsafe: its light loop is the Xid 109 case).

**3. Post-chain wedges (mkw20 entry 2926): not done this pass.** The seams above are the part of item 3 that was
localized. mkw21's composite snapshots (`811e4ff000`, 2900-3010 and 7680-7780 every 10) exist, but mkw21 is a long
frame and writes its output after 7780 (the swapchain window snapshots up to 7780 are identical in ours and
Eglantine's: previous frames), so the next look starts from mkw21's readbacks at 7750-7850.

**4. New capture `mkw21`** (`mkw21/capture21.sh`: capture20.sh touched at +50 s, snapshots spread over both frame
lengths). The race had **not started** at +50 s: it is the race-start countdown again (lights on the gantry), the
pack in view (racers, DK, Toads), KTV trucks not. Next time touch at ~+80-90 s for lap 1 with the pack ahead.
8439 entries, 3 channels; all 194 snapshots fired: **HDR `8110901000` every 50 from 300 to 7900** (the per-pass
reference the tenth pass lacked), composite and swapchain window. Replay: all 8442 entries, 78 ms GPU wait, no Xid.
- Final vs Eglantine's end dump (output is slot 1, `210b21a000`; the swapchain map moves per capture, here
  `210a31a000` + 3 x 0xF00000): **19.9 dB, mean abs 14.0, brightness 0.93x; world 18.7 dB** (`mkw21/compare.png`:
  ours over Eglantine). Per-pass HDR: median ratio 0.99-1.00 at every point.
- The big difference is **foliage only in ours** (the ninth pass's bush, here a row of trees at the left edge with
  their shadows on the road): ours covers +4.6% of the screen at **entry 6784** where Eglantine adds 0.2%. 6784 is
  the 5-word tail of a `CALL_MME_MACRO(40)` that entry 6783 started with one parameter (`0x4`): `(1089, 34, 0, 0,
  55)` = 1089 indices x 34 instances from base instance 55. The host MME collects the parameters across the GPFIFO
  entries and draws. Eglantine has dedicated handling for exactly these 5-word subroutine entries
  (`DmaPusher::Step`, `FrontTakeIndirectRecord`, "title 4 ends a main entry after a macro's first word"), and its
  3D macro trace logs the call with one parameter and no emitted methods (an HLE'd draw also logs none, so this is
  not proof). Before 6784 Eglantine covers ~0.6% more (6350-6750: the pipe railing, grass tufts). **Which side is
  right is open**: next, the draw's vertex shader instance data (base instance 55) in ours vs Eglantine's.
- Disk: **mkw19's dumps deleted** (it has no open per-draw question left that needs them: the windows are explained
  above); mkw19 keeps metadata, `replay/` (no arena: rebuild needs the dumps, so it is reference only now),
  snapshots and images. **mkw21 is the one full capture** (52 GB + 10 GB arena): it has per-pass snapshots.

**5. Clipmaps / VT pages: not done** (Phase 2 does not need it; the readback design is under item 2).

## Twelfth pass (2026-11-18): the guest's own words run on the host GPU
**Result:** with `EDEN_NX2_PASSTHROUGH_WORDS=1` the backend submits the guest's own methods (rewritten for the
host) on every guest channel; MKW boots to the main menu and into the race on the RTX 4060, and the main menu
read back from the game's output target in guest memory is the game's (`bench/pt-live-menu.png`; ASTC
surfaces black, see below). Fork `uscl-passthrough-ship` **e5091605d** = `patches/0017` (`git am` 0001-0017
onto 30181ba05: identical tree); probe branch `uscl-passthrough` **3b72596a8** (built; the ptcap worktree is
back on it).

Measured first (mkw21, offline):
- **QMD dependence:** no plain QMD is launched from memory. Every inline QMD (139) carries `DEPENDENT_QMD0` with
  action 4 (decrement); NVN's barrier QMDs are scheduled from memory (PCAS2 action 3, `DEPENDENCE_COUNTER` 2),
  raised by PCAS2 0xC, lowered by 7 and by the inline QMDs; a third release slot (word 52, `RELEASE2` in Ada's
  QMD V03 too) carries NVN's queue syncs. 19 report releases a frame go to VA 0. No macro emits SET_SRGB_WRITE.
- **Raw ASTC headers on Ada sample black, no fault**: mkw21 replay with all 37590 ASTC headers raw
  (`mkw21/replay-astcraw/`), 8442 entries, no Xid (`bench/pt-astc-raw-vs-decoded.png`). So the header pool is a
  quality item (transcode, plan step 6), not a safety gate. Z16 is covered live by the mirror's guest kinds.

The translator (`Words` in nx2_passthrough.cpp), each part found by a failing run:
- Decode + re-encode per entry (runs kept; encoder self-test at start); host-class rewrites: Ampere B -> Ada
  classes (+ I2M, 2D), syncpoints / firmware / SW / instrumentation / zcull / `SET_SPA_VERSION` / TLB mem ops
  dropped, `SET_SRGB_WRITE` 0, zcull off and `SET_BINDLESS_TEXTURE` 0 at bind (Phase 1 restore defaults), shader
  local memory moved to a per-channel 72 MiB host buffer (guest buffer sized for T239's TPCs), operations at
  unmapped VAs dropped.
- Compute macro calls -> the split front's emitted methods (hooks in `KeplerCompute::FrontRunMacro`); `OnStep`
  in `DmaPusher::Step` keeps the pairing for entries resumed after an acquire (30k unpaired calls -> 0).
- Inline QMDs go to host slots (Eglantine reads the guest's QMD ring back). QMD dependence is tracked in stream
  order with Eglantine's own rules (`native_qmd.h`, one table for every channel); a barrier reaching 0 is copied
  on the GPU timeline (copy engine + `SEM_EXECUTE` IAND reductions clear its dependence) and launched there.
  First version launched barriers at schedule time from a CPU copy: their release payloads were 0 (NVN patches
  them while the barrier waits; `_WATCH` showed `0x...37010 = 0 after entry`), Eglantine deadlocked.
- **While Eglantine still executes every entry, it alone writes and waits on the guest's semaphores**: two
  writers left a slot one behind (deadlock), and a host acquire behind Eglantine waited forever (channel 1000
  entries deep). `EDEN_NX2_PASSTHROUGH_RELEASES=1` turns host releases/acquires on (for the sole-executor mode).
- An entry that ends inside a method run is held and submitted with its continuation: MKW's macro-40 draws end
  a main entry after the first parameter; a host MME left mid-call while switched out raised **Xid 13
  MISSING_MACRO_DATA** (postmortem: GP 500 = entry 23607 ending in `CALL_MME_MACRO(40)`, 1 word).
- Program pages becoming or changing as shadow pages wait for the host channels first: the unmap-then-map
  window let the host fetch a zero program header (**Xid 13 Shader Program Header 18**). Guest unmaps and kind
  changes do not wait (that stalled loading for seconds; a data page reading 0 for a moment does not fault).
- `Ready()` honours `EDEN_NX2_PASSTHROUGH`: the nvhost hooks (kinds, unmaps) opened an RM client without it
  since 0015 (a bug for every SDK build).
- Bisecting tools: `_SYNC=1` (wait per entry), `_DROP=<mask>` (1 report sem, 2 host sem, 4 compute launches,
  8 I2M, 16 copy, 32 3D, 64 acquires), `_WATCH=<va or 20-bit suffix>`, `_REREAD=1`, `_SNAP=<s>:<va|0>:<size>:<file>`
  (guest memory as the host rendered it; 0 = the last 2560x1440 sRGB colour target), postmortem log (the
  submissions around GPGet) when a channel stops.

Runs (MKW, `runs.py`, Xid checked after each; `nvrm_probe clear` OK after each):

| Run | Result |
|---|---|
| markers / translate-only (`_SUBMIT=0`) | race 53.4 / 52.5 fps, as Vulkan alone (53.8) |
| async, first versions | stall at boot (host writes land after Eglantine's) |
| `SYNC=1`, scheduler + releases left to Eglantine | boots, menu, race: 4M entries, ~7 fps, no Xid |
| async, before the held-run fix | Xid 13 MISSING_MACRO_DATA (x2), Xid 32 (by-reference attempt, own bug) |
| async, before the program-page quiesce | Xid 13 Shader Program Header |
| async, final (3 runs, ~10M entries) | **no Xid**; ~27k entries/s: the host GPU, reading all guest memory as uncached sysmem next to Vulkan's work, is the bottleneck (ring full) |
| async, final, longer race | Eglantine deadlocked at 166 s: NVN's fence slots read 0 (the host's late memory writes reset them after Eglantine's releases) |

What this means: in this dual-execution mode every guest-memory write the host makes can land after Eglantine
moved on (async), so `SYNC=1` is the correct mode and it is slow. The way forward is the plan's step 5 for
real: **the host is the only executor** (Eglantine's back does not render; guest fences follow host semaphores,
`_RELEASES=1`), plus **VRAM residency** (Phase 3) for speed, and **ASTC transcoding** (step 6) for the black
surfaces. Shortcuts left in code: macro-parameter entries GPU-culled after Eglantine read them use Eglantine's
words (69 of 1617 differ; a GPU-timeline copy is the fix); QMD slot ring reused after 16384 launches; barrier
counters read on the CPU at schedule time; inline QMDs with their own dependence count launch at once.

### Twelfth pass, continued: live vs Vulkan, ASTC transcoded (plan step 6, first version)
Fork ship **2110a6d16** = `patches/0018` (series 0001-0018 verified: identical tree); probe **03355c153** (built).
- **Item 5, compared with Vulkan:** `EDEN_NX2_PASSTHROUGH_SNAP=-1:0:f00000:<file>` writes the game's output (the
  largest sRGB RGBA8 colour target, tracked from the 3D front because MKW sets targets from macros) when
  `<file>.now` appears; a driver (menu inputs, trigger, debug-server screenshot) takes both back to back.
  "Start Grand Prix?" screen (SYNC mode): layout, text, buttons, banners identical; ASTC content black:
  **15.9 dB, 0.65x brightness** (outside the OK button; `bench/pt-live-vs-vulkan-menu.png`).
- **ASTC on the host:** texture header pools become shadow pages (one unmap per run of pages; page by page made
  RM's page-table walk fail with NV_ERR_NO_MEMORY); an ASTC header points at a copy decoded on a worker thread
  with Eglantine's decoder, compressed to **BC1 (opaque) / BC3** with its BCn encoder, re-swizzled to the header's
  layout (`SwizzleImageToBuffer`, new in texture_cache/util), in a 2 GiB host arena. **Only headers Eglantine's
  texture cache reads are decoded** (`DescriptorTable` hook, new or changed descriptors): the whole pool filled
  2 GiB with 29k textures and still missed the thumbnails; sampled-only: 1845 textures, 1149 MiB, 0 failures.
  Result: **20.7 dB, 1.01x brightness**, thumbnails, labels and badge art present
  (`bench/pt-live-vs-vulkan-menu-astc.png`); what is left is animation timing between the two grabs, and
  Eglantine's own frame glitches on the OK button (2 of 3 grabs), ours does not.
- Shortcuts: a texture is decoded once (later uploads into it unseen); arena never freed; header slot rewrites
  do not wait for the GPU.

### Twelfth pass, continued: the race on the host GPU
Fork ship **3cde5150e** = `patches/0019` (series 0001-0019 verified); probe **c26bdcf63** (built, worktree on it).
- **Race-start frame, host GPU vs Eglantine** (`bench/pt-live-vs-vulkan-race.png`, SYNC mode): geometry, sky and
  clouds, lighting, Mario and the kart, HUD (coins, lap, 24th, minimap with racers), signs, banners, grass and
  Toads all as in Eglantine's frame. **Black: the track surface, arch and buildings** = MKW's 8192^2 x 68
  virtual-texture ASTC arrays (sparse, resident tiles only), left raw as too large. Next quality item:
  per-tile transcoding of sparse ASTC into a sparse output (tile map from nx2:gpu 216, BC1 2x the guest bytes).
- The ASTC arena grows in 256 MiB chunks to 8 GiB (boot + menus + race: ~11.5k sampled textures, 4.4 GiB); a
  2 GiB arena ran full mid-race; pinning the whole cap at start made shutdown take ~3 minutes.
- RM's page-table pool ran out of video memory mid-race next to Eglantine's Vulkan work (NV_ERR_NO_MEMORY in
  `virtmemReserveMempool`): device and VA space now allow page tables in system memory (`RETRY_PTE_ALLOC_IN_SYS`);
  one shader local memory buffer for all channels.
- **Xid 31 once** (texture unit, PDE fault at 0x1_00000000, recovered, health OK); MKW samples null texture
  headers at address 0 in its second pool (0x2109895000); a sparse range below the guest's is refused by RM
  (`NV_ERR_INSUFFICIENT_RESOURCES` at 0, 64 KiB and 4 GiB), so such reads stay faults. Not reproduced in the next
  three runs. Open.
- Snapshot address 0 is now the presented image (OnPresent; MKW draws 2560x1440 into it).
- **Async decode has a visible limit:** a texture sampled before its copy is ready reads black, and a game that
  renders a one-time capture then (MKW's blurred menu background) keeps it black (`menu8`); fix: decode on the
  entry thread for headers first seen there, or hold such entries until their copies exist.

## Thirteenth pass (2026-11-18): virtual textures on the host GPU
**Result:** every ASTC texture MKW samples now has a host-samplable copy, including the virtual-texture arrays
that were left raw (black track, arch, buildings). Fork probe `uscl-passthrough` **6a3bf2df4**, ship
`uscl-passthrough-ship` **8e58a33d8** = `patches/0020` (`git am` 0001-0020 onto 30181ba05: identical tree;
`nx2_passthrough.{h,cpp}` are byte-identical on both branches, the probe build compiles it).

Measured first:
- The arrays are **not sparse in guest memory**: mkw21's maps cover each one's whole range (137 / 35 / 143
  mappings, no gaps); per layer one 0x1500000 piece (levels 0-2) and one 0x60000 piece (the mip tail), layer
  stride 0x1560000. Live: **8192^2 x 84 layers, 12 levels, ASTC 8x8 (format 71), block height 3, tile width
  spacing 4** (that spacing is why the first version rejected them); ~68 layers mapped, the rest unmapped. Three more
  appear at race time: 8192x8168, 8188x5212 and 8008x8164 single-layer ASTC images (two of them mapped only after first sampled).
- **Who writes them:** no copy-engine launch into them in mkw21's frame nor live (copy hook, counted); a GOB scan
  (`_VTSCAN=1`, first 16 bytes of every 512-byte GOB) every 500k entries: **112 MiB and 217 MiB written, unchanged**
  from the menus on. **Corrected later this pass:** that scan only counted non-zero GOBs; the game does rewrite tiles in
  memory it keeps mapped (the background sweep of `patches/0025` re-transcodes ~11k changed cells in a race).

Design (`Host::VtStart`/`VtWorker`, nx2_passthrough.cpp):
- Such a header points at a **BC3** copy laid out as the header's own block-linear image (Eglantine's
  `FullUploadSwizzles` levels + `CalculateLayerStride`, per-level row stride aligned as `UnswizzleImage` does:
  `CalculateLevelStrideAlignment`) in an **RM sparse range at 2 TiB+** (fixed address above the guest's 1 TiB range,
  in Ada's 49-bit VA; unbacked pages read 0). A CPU reservation (MAP_NORESERVE) holds the same layout.
- Workers (`_VT_THREADS`, 2) go through cells (64x64 ASTC blocks of one level of one layer): `UnswizzleSubrect`
  from guest memory, CityHash, skip zero / unchanged, ASTC decode (Eglantine's), BC3 encode, `SwizzleSubrect` into
  the reservation, then import (OS descriptor) and map just the 64 KiB pages those blocks lie in (computed per
  row of GOB blocks, uncached in L2: cells are rewritten by the CPU).
- Cells are looked at when the texture is first sampled, when the guest maps memory over it (`Host::Map`), and after
  a copy-engine launch into it (after the entry ran, and again 300 ms later for async mode). Levels spread over
  several mappings are gathered page by page. A **layout self-test** (2048x1048 x 2 layers x 10 levels, the real
  texture's format and block sizes: cells unswizzled from the ASTC layout, BC3 cells swizzled one by one = the
  whole-level swizzle, inside the mapped pages) runs at the first such texture; it passed.
- Shortcut left: shader writes into such a texture are not seen (none measured).

Runs (MKW, `runs.py`, SYNC mode, Xid and `nvrm_probe clear` checked after each; none had an Xid):

| Run | Result |
|---|---|
| vtmeasure | boot deadlock at 27 s (see below) |
| vt1 (measuring) | race 22.4 fps; arrays rejected (tile width spacing), scans: 112 / 217 MiB written, static |
| vt2 | VT on: 6244 cells, 1314 MiB mapped, 34 s worker time; menu deadlock at 309 s (see below) |
| vt3 | menu vs Vulkan **26.1 dB, 0.998x** (`bench/pt-live-vs-vulkan-menu-vt.png`; twelfth pass 20.7 dB): blurred background present; 5 textures, 6501 cells, 1378 MiB; 0 ASTC headers left raw |
| vt4 | race frame on the host GPU **textured**: road with its markings, canyon rock, brick, buildings, rails (`bench/pt-live-vt-race-intro.png`, top); Vulkan's screenshot at that moment shows another intro shot, so no PSNR |
| vt5 | 1800 race frames: 10385 cells (3437 unchanged), 2239 MiB mapped, 78 s worker time; fps 20 -> 3 in the second half; snapshot missed (taken after SIGTERM) |
| vt6 | 900 race frames: the end screenshot falls on a black transition (both sides all 0) |
| vt7 | 1800 race frames, `BENCH_TRIGGER`: **racing frame textured on the host GPU** (`bench/pt-live-vs-vulkan-race-vt.png`, host on top): asphalt with cracks and markings, canyon walls, Hot Foot / Mario Motors signs, stands, HUD, karts; 13.7 dB, 0.93x vs Vulkan, mostly the ~1 s camera offset between the two grabs (the kart is on the dirt strip in Vulkan's, on asphalt in ours). Host only: a dark square under the kart (shadow blob? not chased). 6 textures, 10259 cells, 2299 MiB of BC3 mapped, 74 s of worker time; RM client freed, no Xid |

Racing in SYNC mode: 19-25 fps for the first ~45 s, then **~3 fps** (vt5, vt7 alike); not looked at (SYNC is the
bisecting mode; the slow part may be the VT workers' pages being uncached sysmem, or a heavier scene).

**Boot / menu deadlocks (2 of 8 runs):** Eglantine's puller waits on NVN semaphores that read 0 (vtmeasure: channel 2
`0x21f6497100` wants 5, channel 1 `...140` wants 1, from 27 s; vt2: `0x21f6097040` / `...100` at 309 s, words all 0).
The same signature (present fence stuck at 1226) is in an earlier session's `STUCK-1876056.txt`, so it predates this
pass. With `_RELEASES` off the host writes no semaphores (SEM_D, SEM_EXECUTE, REPORT_D and QMD RELEASE0/1/2
enables are all stripped), so the zeroing comes from another write landing after Eglantine's release: the
dual-executor race, more likely when the VT workers slow Eglantine (vt2: entries 5x slower for a minute). A
`_WATCH` run without a deadlock (vt3) only logs QMD release traffic. Not chased further: sole-executor mode
removes the second writer.

Tools: `bench/runs.py` now stops emulators with SIGTERM only (it sent SIGKILL after ~44 s); `bench.py`
`BENCH_TRIGGER=<file>` touches `<file>.now` after its end screenshot and waits for the backend's snapshot
(`EDEN_NX2_PASSTHROUGH_SNAP=-1:0:f00000:<file>`), so host and Vulkan frames come from the same moment. Note
`runs.py` splits variables at commas: only one SNAP / WATCH entry per run.

### Thirteenth pass, continued: the host GPU as the only executor (plan step 5)
Fork probe **58d62c5ee** / 1a9405f2b / 0397c5908, ship **a81442a16** / 8654e88dd / e651c0ac9 = `patches/0021`-`0023` (series 0001-0023 verified: identical tree). First: `patches/0021` (series 0001-0021 verified: identical tree; ship's
changed files syntax-checked from the ship tree, also the non-SDK stub path). `EDEN_NX2_PASSTHROUGH_SOLE=1` (with
`_WORDS=1`): Eglantine parses every entry (macros, QMD bookkeeping, program binds) and executes nothing.
- **Gates** (all `[[unlikely]]` on `Nx2Passthrough::Sole()`): `RasterizerVulkan::Draw/DrawIndirect/DrawTexture/
  Clear/DispatchCompute/Query`, `MaxwellDMA::Launch`, `Fermi2D::Blit`, compute release reductions
  (`KeplerCompute`, both paths), `Maxwell3D::StampQueryResult`. The host's releases are on (`HostReleases()`).
  Left alone: constant-buffer and inline uploads Eglantine writes at parse time (the host writes the same bytes
  again in stream order).
- **Syncpoints:** MKW's present fences are syncpoint 1. `Puller::ProcessFenceActionMethod` (Increment) calls
  `SoleSyncpoint`: guest value now, host value from a callback armed with the fence of the host submission that
  carries the entry; the 3D sync method (through `RasterizerVulkan::SignalSyncPoint`, possibly on the back's thread)
  waits for every channel's submitted work. A poller thread runs due callbacks (unlocked) and rings stale doorbells.
- **Acquires:** first run (`so1`) deadlocked at 7 s: Eglantine suspends an entry at an acquire, and the host only got
  entries once fully parsed, so the work before the acquire (with releases other channels waited for) never ran.
  Now an entry waiting at an acquire is submitted up to there (`OnEntrySuspended`, from `DmaPusher::Step`'s
  suspension points) and the rest after the resume; Ampere SEM_EXECUTE acquires suspend the channel like the others
  (they were checked once and passed); the host's copies of acquires are dropped (`so2`: a host channel blocked in
  hardware on one made the program-page drain fail and the backend turned itself off).
- **Present:** `AccelerateDisplay` waits for the host channels (at most 250 ms) and returns nothing, so the
  presenter reads the frame from guest memory (`Layer::UpdateRawImage`, CPU unswizzle): the debug-server screenshot
  is now the host GPU's image.
- **ASTC:** Eglantine's texture cache no longer reads descriptors (no draws), so every header of a pool is
  transcoded: MKW ~35k textures, 4.3 GiB of the 8 GiB arena, 0 failed (`so3` without this: thumbnails, HUD icons,
  most surfaces black).

| Run | Result |
|---|---|
| so1 | deadlock at 7 s (acquire before submit, above) |
| so2 | backend off at 25 s (host acquire blocked a drain) |
| so3 (SYNC) | **boot, menus, race on the host GPU alone**: 10M entries, 54k syncpoint callbacks, no fence timeout, no Xid, RM client freed; ASTC black (no descriptor reads) |
| so4 (SYNC) | **complete frames** (`bench/pt-sole-sync.png`, top: sole mode, bottom: dual mode's Vulkan output): menu with thumbnails and OK, race textured with the whole HUD (Eglantine's coin-counter glitch is not there); race 12.2 fps mean (19.7, 23.8, 12.4, then ~5.5), **VRAM 1.8 GiB** (Vulkan idle; 6.3 GiB in dual mode) |
| so5 (async) | Xid 13 Shader Program Header at 432 s: a newly bound pixel program read for patching before the host's copy that uploads it ran (sole mode: the host is the only uploader); the drain that followed failed, backend off. Async ran 13k-57k entries/s |
| so6 (async) | fixed (`patches/0022`: a program first seen or changed is read after a drain; `_REREAD` on by default in sole mode): **boot, menus, race, no Xid**; race 13.8 fps; mushrooms and place indicator black (ASTC backlog, one worker) |
| so7 (async) | ASTC worker pool (3): race frame complete; 14.2 fps (same as SYNC: the host GPU itself is the limit now); the menu's blurred background grey (the one-time-capture gap: captured before its textures were transcoded) |
| so8 (async, VRAM) | GPU-only regions in video memory (`patches/0023`, below): **24.9 fps**, but the race came out blocky (dark blocks on the track and walls) |
| so9 (async, VRAM) | fresh video memory zeroed with the copy engine first: frames right again (`bench/pt-sole-vram-race.png`, left; so8 right); **26.3 fps mean** (24.6, 36.3, 34.1, 30.1, 21.6, 11.2), 2.6 GiB in video memory, no Xid |
| vkA / ptA / vkB / ptB | **interleaved pairs, MKW race, 1800 frames:** Vulkan backend alone **54.6 / 54.8 fps**, passthrough (sole, async, VRAM) **26.8 / 26.3 fps** (chunks 23.9, 36.8, 35.1, 30.0, 23.1, 11.8 vs Vulkan 56.5, 55.6, 59.3, 60.4, 60.1, 35.8; both drop in the last chunk); passthrough GPU util 53-56%, VRAM 4.5 GiB; no Xid |

**Phase 3, first step: GPU-only regions in video memory** (`patches/0023`): nx2:gpu 214 regions without the sparse flag
are GPU-only (the guest CPU never maps them; MKW: 145 regions, 10 GiB, render targets among them). In sole mode nothing
but the host GPU touches them, so `Host::MapOff` maps them with video memory (allocated per mapping, refcounted
across partial unmaps and kind changes, freed with the last piece) up to `EDEN_NX2_PASSTHROUGH_VRAM_MB` (3072 by
default in sole mode), zeroed with the copy engine on a backend channel before use (those regions read zero until
written: without it so8's track was blocky), mapped cacheable in L2. `MirroredHost` returns null there (guest memory is
not what the GPU sees), the translator's semaphore "mapped" test uses `IsMapped`. Shortcut: first come, first served
under the budget (MKW: 2.6 GiB of the 10 GiB got video memory).

Seen in so4's race: **dashed dark lines along triangle edges** on the road (`bench/pt-sole-road-seams.png`): the
eleventh pass's helper-lane seams (`PT_HELPER_LOADS` zeroes per-instance UV loads in helper lanes; fix designed there:
a covered quad neighbour's value), not a sole-mode problem.
Shortcuts left: a pool is translated again whole after every finished decode (42M header rewrites in a race: CPU
cost); in async mode Eglantine may parse command words the host GPU writes (MKW's GPU culling) before the host
wrote them (SYNC mode is ordered).

## Phase 2, step 1: channels and submission (2026-11-17)
**Result:** the RM channel runs inside Eglantine. Fork branch `uscl-passthrough-ship` (`5e536b6a4`, on
`uscl-gpurw-ship`), USCL `patches/0014`. The same commit sits on top of the probe as `uscl-passthrough`
(`5436bc860`, built in the `ptcap` worktree: `-DEDEN_NX2_PASSTHROUGH_SDK=/usr/src/nvidia-615.78.08/src`).
`git am` of 0001-0014 onto the pinned base gives that branch's tree (0 lines of diff).

- `src/video_core/nx2_passthrough.{h,cpp}`: `EDEN_NX2_PASSTHROUGH=1` opens an RM client, an RM-owned VA space, a TSG
  and context share, and an `AMPERE_CHANNEL_GPFIFO_A` channel with `ADA_A`, `ADA_COMPUTE_A` and
  `AMPERE_DMA_COPY_B` (pt_replay's setup, errors as exceptions caught in one place: log, free, backend off; never
  `exit`). It follows the first guest channel that submits. Hook: `DmaPusher` (prefetched lists and
  `RecordParsedEntry`, once per entry: `command_resume_offset == 0`). Each entry becomes one serialized submission
  (a semaphore release with WFI, CPU waits, 2 s limit). The guest's words are counted, **not submitted** (they
  reference guest VAs: step 2). The RM client is freed in `GPU::NotifyShutdown` and at exit. Without the SDK path it
  compiles to a stub that warns once.
- MKW, same binary, `runs.py pt2on:EDEN_NX2_PASSTHROUGH=1 pt2off` (`BENCH_FRAMES=3000`): **15.45M entries (860M
  guest words) submitted, mean 3-8 us per submission, worst ~3 ms, no Xid**, health check OK, VRAM back to the
  desktop's 764 MiB after exit. **Racing fps 37-43 -> 13-21** with the per-entry wait (guest channel 1: ~125k
  entries/s). So no step may wait per entry: submit asynchronously and wait only where the guest waits (step 5's
  guest fences); the ~1-3 ms worst cases are our channel time-slicing against Eglantine's own Vulkan context.
- Not verified: the shutdown log line (runs.py copies the log before SIGTERM). Next, step 2: mirror the guest VA
  space (probe `NoteMap`/`NoteUnmap`/`NoteGuestKind` events, guest memory as an RM OS descriptor over Eglantine's
  host mapping), then submit the guest's own words with the host-class rewrites (syncpoints dropped,
  `SET_SRGB_WRITE` 0), asynchronously.

## Phase 2, step 2: the guest VA space (gate 4) (2026-11-17)
**Result: gate 4 passes on hardware.** The guest's GPU VA space is mirrored in the RM VA space inside Eglantine and
the host GPU reads the guest's own bytes at the guest's own addresses: **128556 pushbuffer entries read back by the
host copy engine at their guest VA, 0 differ from the guest memory** (6 differ from Eglantine's parse because the
guest rewrote them afterwards). Fork `uscl-passthrough-ship` **65397093a** = USCL `patches/0015`; `git am` of
0001-0015 onto 30181ba05 gives the branch's tree (0 lines of diff); also on the probe branch `uscl-passthrough`.

Open question answered first, with `nvrm_probe guestvm [GiB] [granule MiB]` (new step): a guest VA range as one RM
sparse virtual allocation (`NV50_MEMORY_VIRTUAL`, `VIRTUAL | SPARSE | FIXED_ADDRESS_ALLOCATE`, 4 KiB pages), a memfd
`MAP_SHARED` arena (as Eglantine's HostMemory) imported as `NV01_MEMORY_SYSTEM_OS_DESCRIPTOR`s, mapped inside the
range with `PAGE_KIND_OVERRIDE`:
- **No OS-descriptor limit found**: 4 GiB as one descriptor or as 64 x 64 MiB, 12 GiB as 192 x 64 MiB, all fine.
  Import = pinning (`pin_user_pages` `FOLL_LONGTERM` on the whole range): **~210 ms per GiB** (4 GiB 0.84 s, 12 GiB
  2.55 s), the pages count as RssShmem, VmPin stays 0. So: import 64 MiB granules lazily on first GPU map (MKW:
  7.4 GiB pinned over a session, 1.5 s in total), not the whole arena at boot.
- **Kinds**: every kind MKW uses maps on OS-descriptor memory (0x06, 0x08, 0x09, 0x0b, 0x01, 0x0c, 0x03, 0x0d, 0x04);
  0xfe (generic 16Bx2) is refused ("PTE kind override of 254 is not supported"; MKW does not use it).
- **Sparse works**: a copy from an unmapped address inside the range read 4096/4096 zero bytes, no fault.
- A GPU semaphore release through the imported mapping is seen by the CPU in the memfd.

The backend (`nx2_passthrough.cpp`; hooks in `MemoryManager::Map/MapSparse/Unmap` and nx2:gpu 211/212/213/216):
- Guest VA range **[1 << 36, 1 << 40)** = one sparse range, reserved before the channel's own memory is mapped (RM
  then places that below it). MKW's guest VAs: 0x114b41b000-0x86a91c6000, one memory manager (`mkw19/maps.txt`).
- `MemoryManager::Map` walks the device pages and hands runs contiguous in host memory to the backend; offsets are
  relative to HostMemory's backing (`BackingSize()` added). The backend keeps its RM mappings in an interval map
  (partial unmaps re-map the remainder), and the guest's kinds in another (compressible -> incompressible fallback;
  a kind change remaps what is mapped there). Default kind where the guest set none: pitch.
- Guest memory is mapped **GPU-uncached** (`NVOS46_FLAGS_GPU_CACHEABLE_NO`; it made no difference to the stale reads
  below, kept because nothing invalidates the L2 on guest CPU writes). A map that fails with `NV_ERR_NO_MEMORY`
  (once per session in RM's page-table walk) is retried once (then pitch).
- **Hole punching**: Eglantine's `HostMemory::ClearBackingRegion` punches holes in the memfd for cleared granules.
  A punched page stays pinned for the GPU and the file gets a new one, so the GPU read stale bytes (measured: **4.4%
  of checked entries**, all of them pitch-kind, CPU memory = parsed words; `EDEN_PUNCH_HOLE=0` -> 0). Fix: with
  `EDEN_NX2_PASSTHROUGH=1` HostMemory clears with memset (a punched pinned page returns no RAM anyway);
  `EDEN_PUNCH_HOLE` still overrides. Emulator-wide.
- `EDEN_NX2_PASSTHROUGH_CHECK=<n>`: every n-th entry the copy engine reads the entry's words at their guest VA into
  the channel buffer; compared with the parsed words and with the mirrored memory, logged every 50000 entries with
  the map counters (`LogStats`). `EDEN_NX2_PASSTHROUGH_KINDS=0` maps everything as pitch (bisecting).
- MKW race (`runs.py pt2fixed:EDEN_NX2_PASSTHROUGH=1,EDEN_NX2_PASSTHROUGH_CHECK=50`): 26898 maps, 3301 unmaps,
  16142 live RM mappings (32889 made, 16747 undone), 18422 kind changes, 7552 MiB pinned in 1.5 s; no Xid, RM client
  freed at shutdown, VRAM back to 751 MiB, health check OK. Racing 53-60 fps in these short runs (the step-1
  serialized marker submission is still there; 13-21 fps in step 1's measurement on the probe build did not
  reproduce on the ship build: 54-60 fps in `pt2stop`, not investigated).

Next (step 2b, then the plan's steps 3-5): submit the guest's own words. Copy each entry's words into the host
pushbuffer with the host-class rewrites (drop syncpoint methods 0x70/0x74; `SET_SRGB_WRITE` -> 0), submitted
asynchronously (a ring of pushbuffer space reclaimed by GPGet, no per-entry wait; wait only where the guest waits).
Before that is safe on this desktop GPU: pixel programs need the `PT_HELPER_LOADS` trampoline when uploaded (the
helper-lane light loop is the Xid 109 case), the other guest channels (2, 3) must be followed too or their semaphore
acquires stall channel 1, and fine-grained preemption (`bind_preemption` in nvrm.h) belongs in the channel setup.
ASTC sampling still needs step 6.

## Phase 2, steps 2b-3: every channel, async submission, pixel programs patched (2026-11-17)
**Result:** the backend follows every guest channel asynchronously and patches every bound pixel program for
helper-lane loads before the entry that binds it is submitted; the GPU reads the patched programs at their guest
VAs. Fork `uscl-passthrough-ship` **043f82389** = USCL `patches/0016` (`git am` 0001-0016 onto 30181ba05: 0 lines of
diff); also on the probe branch (`uscl-passthrough` 6cae14844). The guest's methods are still not submitted.

- **Channels**: one host channel (own TSG and graphics context) per guest channel, created on first use: MKW
  opens 5 (guest channels 1-5). GFXP/CILP preemption contexts are bound (`bind_preemption`;
  `EDEN_NX2_PASSTHROUGH_PREEMPT=0` off).
- **Async submission**: per-channel pushbuffer ring, reclaimed through a fence the channel releases without WFI
  after each submission (read over BAR only when the ring runs short); the CPU waits only for a full ring or a
  check. 0.1-0.8 us per entry. Entries are recorded by `OnEntry` and submitted by `OnEntryDone` (DmaPusher, once the
  entry's methods were processed), so a program bound in the entry is patched first.
- **fps next to Vulkan** (`runs.py` interleaved, MKW, racing chunks): doorbell per entry **-15%** (the GPU keeps
  switching to our channels), no submissions -8% (noisy), **doorbell every 64 entries -1%** (default,
  `EDEN_NX2_PASSTHROUGH_BATCH`). Once the guest's words run, a partial batch must be flushed where the guest waits.
- **Program binds** (`Maxwell3D::FrontWrite`, native `SET_PIPELINE_PROGRAM_ADDRESS_B` = word 6 of a pipeline slot):
  MKW binds ~2.5M programs per session, 57% from macros (the front runs them, `EDEN_NX2_SPLIT=2` default), **all on
  the entry thread** (0 elsewhere), ~670 distinct pixel programs.
- **Helper-lane patcher in C++** (`nx2_helper_loads.{h,cpp}`, a port of ptcap's `PT_HELPER_LOADS`): no
  disassembler; program end, LDG sites (opcode 0x981, Rd bits 16-23, width bits 73-75), first-instruction branch
  check, P2R/R2P, and a free predicate from per-opcode predicate fields (guard 12-14 plus 68/77/81/84/87 as each
  opcode uses them, measured with nvdisasm on mkw21's 320 pixel programs; carry-capable IADD3/LEA/IMAD count
  81/84/87; an opcode never seen counts every field). Validated offline against ptcap with nvdisasm
  (`passthrough/tools/helperloads/`): **210 of 210 programs with loads byte-identical, 2745 loads**,
  105 without loads unchanged, the same 5 programs without an end in 32 KiB left alone (their `EXIT; BRA self` is
  66 KiB-370 KiB out: probably not one program; left unpatched, as in every Phase 1 replay).
- **Shadow program pages**: a bound pixel program with loads is patched into shadow copies of the 4 KiB guest
  pages it lies in (video memory pool, 32 MiB), mapped by the mirror at the guest VAs instead of the guest pages
  (a guest remap of such a page drops the shadow; the next bind rebuilds it); trampolines in an 8 MiB arena.
  Other programs sharing a shadow page (any stage) are copied in when bound. A known program is compared with
  guest memory at most every 100 ms (shortcut: in-place code rewrites at the same address run unpatched for up to
  that long). MKW session: **411 pixel programs patched (4943 loads), 1793 shadow pages**; with
  `EDEN_NX2_PASSTHROUGH_CHECK` every newly patched program is read back at its guest VA by the copy engine:
  **237 of 237 identical**; entry check 0 of 8267 differ; no Xid in any run.
- Not covered yet: compute programs in a shadow page (not refreshed on dispatch), the GPU instruction cache after a
  shadow page changes (needs `INVALIDATE_SHADER_CACHES` in our stream once the guest's words run).

**Before the guest's own words can run** (each item is a Phase 1 replay stopgap that needs a live form):
1. Host-class rewrites while copying the words: drop syncpoint methods 0x70/0x74; `SET_SRGB_WRITE` -> 0 (also when a
   macro emits it: the host MME does, so rewrite the macro or set it after); flush the doorbell batch at guest waits.
2. **QMD dependence counters**: NVN's barrier QMDs in guest memory make the host wait forever (hang, no Xid,
   third pass). The replay cleared `DEPENDENCE_COUNTER` / `DEPENDENT_QMD*_ENABLE` in its copy; live needs shadow
   QMDs (as program pages) or the plan's hardware counters.
3. **Compute macros that emit an inline QMD**: the host MME writes the QMD one word late (`PT_MME=trace`). The
   front runs the same macros, so its emitted methods for those calls can replace the call words in our copy.
4. **Texture headers**: ASTC (28% of MKW's headers; never sampled raw on the RTX, untested) and the Z16 TEX FORMAT
   case (second pass) need a shadow header pool (pool pages remapped as program pages are), ASTC headers pointing
   at transcoded copies (plan step 6) or a stand-in first.
5. Present: copy the guest's output image for Vulkan (plan step 7).

## Phase 1 exit check (2026-11-17)
| Capture | Frame | PSNR vs Eglantine | Mean abs (0-255) | Brightness | Notes |
|---|---|---|---|---|---|
| `mkw16` | racing, open course | **22.6 dB** | 9.1 | 1.04x | HUD identical; HDR main view 1.00-1.05x of Eglantine's snapshots; Toad heads, KTV trucks and pack ~0.5-0.75x, dirt material and foliage differ |
| `mkw17` | race start under the gantry | 9.6 dB | 70.1 | 0.38x | before the helper-lane fix: covered pixels' global loads zeroed |
| `mkw17` + fix | same | **13.9 dB** | 33.9 | 0.89x | lit; HDR main view 1.00-1.08x of Eglantine's snapshots to entry 7100; shadow slab on the near road (atlas tiles), gantry darker |
| `mkw19` + fix | race start (same procedure, new probe) | **17.6 dB** | 21.9 | 1.03x | lit; gantry darker, Mario's shadow missing, one bush only in ours |
| `mkw19`, tenth pass | same | **17.6 dB** | 21.4 | 1.015x | shadow mask 28.0 dB vs Eglantine (depth twins, sRGB write off); near road, pylon, top beam and Mario's shadow localized to entry windows |
| `mkw20` | racing, open course (dirt turn) | 19.4 dB (**22.9 dB world, HUD excluded**) | 8.6 | 0.973x (world 0.986x) | dirt 30.6 dB; dark wedges and dashed seams from the post chain (entry 2926); HUD differs (end-dump timing) |

Runs: no Xid in any replay this pass, `nvrm_probe clear` health check OK after each; ~51 ms GPU wait per frame
(slowest entry 8.2 ms, the road), guest memory 6.2 GB in VRAM, ASTC arena in pinned host memory.

Stopgaps still in the Phase 1 replay (none is meant for Phase 2):
- `PT_NO_END=1`: start-of-frame data only (the end dump poisons rings reused within the frame).
- Mid-frame guest data comes from `deltas.bin`, now from every read site (bytes Eglantine read when it read them):
  a capture-only substitute for real memory.
- ASTC: decoded from the frame's start data with Eglantine's decoder into a **10.6 GB A8B8G8R8 arena in pinned host
  memory** (1495 sampled textures, 658 MiB of ASTC), headers repointed; unsampled ASTC headers point at a stand-in.
- `PT_MME=trace`: compute macros that emit an inline QMD are replaced by Eglantine's emitted methods (the host MME
  writes the QMD one word late; cause not found).
- `PT_NODEP=1`: QMD dependence counters (NVN's barrier ring) dropped; entries run serialized.
- `PT_HELPER_LOADS=1`: every pixel-shader LDG goes through a trampoline that zeroes it in helper lanes (Ada skips
  global loads in helper lanes; helper = `SR_THREAD_KILL` != 0, read once per program in an entry trampoline). Needed
  in Phase 2 too (patch programs when they are uploaded).
- `SET_SRGB_WRITE` forced to 0 (tenth pass; `PT_SRGB_WRITE=guest` keeps the guest's 1): with it set the 4060
  sRGB-encodes clears and shader outputs into plain RGBA8 targets. Phase 2 needs the same rewrite.
- Depth twins (tenth pass): sampled depth images the frame never draws are read as their colour twin on a colour
  kind (the capture holds Eglantine's colour-arrangement download). Phase 1 only.
- Data Eglantine never writes back is missing from the capture: measured (mkw19 inventory), only the two
  sparse-tiled lighting clipmaps `8171190000`/`817a190000` (GPU-modified, unbacked).

Verdict: **Phase 1 answered its question** (guest command streams and shaders run on the RTX 4060 and produce the
game's frame, 13.9-22.6 dB against Eglantine across three captures). Remaining differences are localized (shadow
atlas tile selection, clipmaps missing from the capture, foliage). Go to Phase 2.

## Phase 2 plan: passthrough as an optional Eglantine backend
Goal: `EDEN_NX2_PASSTHROUGH=1` runs the guest's channels on the host RTX through RM channels; Vulkan only presents.
Order (each step ends with MKW running, compared against the Vulkan backend with the debug-server screenshot):
1. **Channels and submission.** One RM channel per guest channel (ADA_A + ADA_COMPUTE_A + a copy engine), from
   `nvrm_probe`/`pt_replay`'s setup. Hook where Eglantine receives GPFIFO entries (`DmaPusher` before decode): submit
   the guest's pushbuffer ranges as host GPFIFO entries (guest pushbuffers mapped at their own VAs), with the host-class
   rewrites `ptcap.py build` does today: drop Tegra syncpoint methods (0x70/0x74), keep everything else. Start
   with one channel and serialized submission (a host semaphore after each entry, as `pt_replay`).
2. **Memory (gate 4).** Mirror the guest GPU VA space: the probe's `NoteMap`/`NoteUnmap`/`NoteGuestKind` hooks are
   exactly the events needed. Guest device memory imported into RM as system memory (OS descriptor over Eglantine's
   own host mapping, so CPU and GPU see the same bytes) and mapped at the guest's VAs with the guest's (fallback)
   PTE kinds; sparse regions as RM sparse VA with the guest's tile mappings. VRAM residency is Phase 3.
3. **Shader programs.** On writes to program memory (and at bind), patch helper-lane LDGs as `PT_HELPER_LOADS` does
   (entry trampoline: `S2R SR_THREAD_KILL` -> a free predicate; per LDG: `@P MOV Rd, RZ` after the load; trampoline
   arena per program region), cached per program hash. Never use PIXLD.COVMASK's predicate for this (measured wrong).
4. **Macros and QMDs.** Run 3D macros on the host MME; compute macros that emit inline QMDs through Eglantine's MME
   (as `PT_MME=trace`) until the host MME's one-word-late QMD is understood. QMD dependence: keep NVN's barrier
   ring but give it hardware counters (the replay dropped them because Eglantine's scheduler produced the values).
5. **Sync (gate 5).** Guest semaphores and syncpoint increments become host semaphore releases into guest memory;
   guest fence waits on the CPU poll that memory. The measured hazard (the guest CPU rewrites ~0.37% of uniform
   memory before the GPU finished reading it) disappears when the guest fence only signals after the host GPU passed
   it, which real semaphores do.
6. **ASTC (and other formats the RTX cannot sample) on upload.** The GPU reads texture headers straight from guest
   memory, so the backend keeps a shadow copy of each header pool (on `SET_TEX_HEADER_POOL`, and when the guest
   writes pool memory) with ASTC headers repointed to transcoded copies. Transcode on first use and when the
   guest CPU writes the texture's memory (Eglantine's memory tracker), per tile for sparse/virtual textures (on
   tile map). Target format: **BC7** (8 bpp, closest quality) or **BC3/BC1** (8/4 bpp, faster to encode) instead of
   RGBA8 (32 bpp): MKW's 1495 sampled ASTC textures are 10.6 GB as RGBA8, ~2.7 GB as BC7/BC3, ~1.3 GB as BC1, which
   fits the 8 GB card next to ~6 GB of guest memory only as BC1/BC3/BC7 with residency (Phase 3). First version: CPU
   decode (Eglantine's `astc.cpp`, as `libptastc.so`) + CPU BC encode, cached on disk by content hash (MKW's
   textures are static); GPU encode later if upload stalls show.
7. **Presentation (gate 6).** Copy the guest's output image (block-linear sRGB RGBA8) with the copy engine into a
   pitch buffer that Vulkan imports (RM export to an fd, else a staging copy), then present as now.
8. **Measure.** MKW racing fps vs the Vulkan backend (`bench/runs.py`, interleaved pairs), Switch Sports unchanged
   (`bench/nsr.py`). Phase 3 then moves hot memory to VRAM (the replay: 582 -> 274 ms to entry 4983 with VRAM).
Open questions to answer early: RM import of Eglantine's host memory (OS descriptor limits, page pinning of a
multi-GB guest arena), whether the 3D macro path on the host MME is complete for MKW (the `PT_MME=trace` QMD
case), and how Eglantine's channel scheduling maps onto concurrent host channels.

### Thirteenth pass, continued: textures in video memory, fresher texture copies
Fork probe 3c28257ca / 415bd987f, ship 256b9ba07 / adc4c7c96 = `patches/0024`-`0025` (series 0001-0025 verified).
- **`patches/0024`:** ASTC arena chunks get video memory twins (`EDEN_NX2_PASSTHROUGH_ASTC_VRAM_MB`, 2048 in sole mode;
  copy engine, one waited submission per pool refresh); in sole mode a texture's bytes are read only after the host
  passed the fences submitted when its header was first seen (racers and Lakitu came out as black silhouettes: an
  upload still in flight was decoded black for good); pools are re-translated only on pages whose headers waited
  for a copy or for their data (42M -> ~0.3M header rewrites a race).
- **`patches/0025`:** virtual textures follow rewritten tiles: idle VT workers sweep every level in turn (one per 5 ms),
  guest maps queue only the cells they cover. Cell-precise requeues alone left the track stale (dark, blocky): the
  earlier whole-level requeues on every map had been hiding the in-place rewrites.

| Run | Result |
|---|---|
| tv1 | ASTC twins: 28.6 fps (1989 MiB of textures in video memory) |
| tv2 | + targeted pool translation, faster doorbells: 29.0 fps; 135k header rewrites |
| hw1 | experiment: host executes SEM_EXECUTE acquires in hardware, Eglantine parses ahead: 31.7 fps **but wrong frames** (blocky track): Eglantine's front reads GPU-written data (traced compute macro output, inline QMDs, culled parameters) and must stay behind the host's releases. Removed |
| tv3 | 50 us acquire polling (was 1 ms) and ringing every channel at a suspension: 27.6 fps, no measurable gain; removed |
| ptC / ptD | the kept set (twins + pool): **27.5 / 27.8 fps** (26.8 / 26.3 before); ptC's ground black at one moment (stale VT, see 0025) |
| vtc1 | cell-precise VT requeues: 30.8 fps but the ground stale (dark, blocky) at the countdown; tv2 had it right |
| vtc2 | + background sweep and deferred texture reads: frames right (`bench/pt-sole-vtc2-race.png`), **28.6 fps**; VT workers 238 s busy in the run |

What bounds the frame rate now (not profiled: no perf on this machine): GPU utilisation 27-63%, the entry thread at
~75k entries/s with ~3 us of backend work per entry, every cross-channel dependency a round trip through Eglantine's
acquire wait (needed for correctness, hw1), guest data still uncached system memory. A host-side L2 for guest system
memory would need L2 flushes before every CPU read of GPU output (QMD releases included): not tried.

### Next (after the thirteenth pass)
1. **Done in this pass** (0024-0025): ASTC twins in video memory, targeted pool translation. Left: VT copies (2.9 GiB)
   stay host memory (no video memory left: 6.6 GiB used of 8); guest textures and vertex data the CPU never rewrites
   (needs a "CPU never wrote it since" test: the memory tracker). Profile first (install perf, or GPU timestamps).
2. Mario's shadow: a dark square under the kart on the host GPU (also in dual mode, vt7): not looked at.
3. **Seams:** helper-lane loads (eleventh pass design: a covered quad neighbour's value instead of 0).
4. **One-time captures** (menu blur) taken before their textures are transcoded: transcode on the entry thread for a
   header first seen there, or hold the entry until its copies exist.
5. Async GPU-written command words: `_REREAD` covers the host's words; Eglantine's own parse of such entries (its
   bookkeeping) still reads them early. No symptom seen in MKW yet.
6. The dual-executor deadlocks and the Xid 31 (null headers at 0) were not seen in sole mode (13 runs); the dual mode
   stays as it was.
