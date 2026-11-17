# GPU passthrough backend: progress log

Goal and plan: `PROMPT-gpu-passthrough-continue.md`. Hand the game's own NVIDIA command streams and
shaders to the host RTX 4060 (AD107) through the RM ioctls, instead of translating every draw to Vulkan.
Host: driver 615.78.08 (open kernel module), Arch Linux.

## Status (2026-11-17, ninth pass)

| Gate / phase | Result |
|---|---|
| Phase 0 (standalone RM tool) | **GO**: own channel, semaphore release, compute launch, ADA_A clear all verified on hardware, 10/10 runs, no Xid, no VRAM leak |
| Gate 1 (driver access) | **Pass** on hardware |
| Gate 2 (method compatibility) | **Pass (real data)**: every 3D/compute/copy method in a captured MKW frame is a named Ampere method, plus three private 3D methods (0xF74/0xF7C/0xF80) that ADA_A accepts; only host-class changes needed (Tegra syncpoints 0x70/0x74 dropped) |
| Gate 3 (shader compatibility) | **Pass (decode level)**; guest compute SASS has started to **execute** on the RTX 4060 (Phase 1), no per-instruction failure seen yet |
| Phase 1 (capture + replay one frame) | **Exit check done** (ninth pass, 2026-11-17): the replay matches Eglantine wherever the capture holds the frame's inputs (`mkw16`: 22.6 dB, HDR main view 1.00-1.05x of Eglantine's per-pass snapshots); the remaining mismatches are inputs Eglantine keeps only in host images it never writes back to guest memory (the race-start frame `mkw17`: road virtual-texture samples read 0, 9.6 dB), a capture limit that Phase 2 does not have. Phase 2 plan written (see "Phase 2 plan"). Before: **Close to Eglantine** (eighth pass, 2026-11-17): capture `mkw16` replays to Eglantine's own render within **PSNR 22.6 dB, mean abs 9.1, brightness ratio 1.04**, HUD identical; left: Toad heads dark, terrain dirt paler, foliage differs (see "mkw16" below). Before: **Textured racing frame, exact reference**: capture `mkw15` replays with every sampled ASTC texture decoded (`PT_ASTC=decode`) to the right image: arch, signs, track, karts, HUD all match Eglantine's own render of the same frame (taken from the capture's end dump), aligned to the pixel; lit surfaces are 0.33-0.65x as bright as the reference (sky 1.00x): a missing light contribution, the next Phase 1 item. Before: **Lit racing frame** (seventh pass, 2026-11-17): capture `mkw14` (probe 0003 now flushes GPU-written images) replays to the lit scene; only ASTC (white stand-in) is left between it and the reference. Before: **Whole racing frame replays** (sixth pass, 2026-11-17): all 7757 entries of capture `mkw13` (racing, 3 channels) run on the RTX 4060 with no Xid, **~36 ms of GPU wait in total** (slowest entry 0.7 ms) with guest memory in VRAM. The fix was emulator-wide: Ada does not perform global loads in pixel-shader helper lanes, so `PT_HELPER_LOADS=1` routes every LDG of every pixel program through a trampoline that zeroes the destination in helper lanes. Output not yet right: the HUD layer matches the reference, the G-buffer shows the right scene (karts, track), but the lit scene composited under the HUD is black (next: find the lighting/composite passes). Earlier: the intro-flyover frame `mkw10` replays to a recognisable course image (third pass) |
| New: texture formats | **ASTC done for replay** (eighth pass; Phase 2: transcode on upload to BC7/BC3, see "Phase 2 plan"): the frame's 1455 sampled ASTC textures are decoded with Eglantine's own decoder to A8B8G8R8 in a host arena and their headers repointed. 28% of MKW's texture headers are ASTC (8x8, 6x6, 4x4, 5x5); the RTX 4060 has no ASTC sampling. Phase 2 needs the same on upload, and a smaller target (BC1/BC3) for VRAM |
| Gates 4-6 | not started (Phase 1 uses pinned host memory at the guest's own VAs, pitch kind only: first evidence for gate 4) |

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

Conclusion for Phase 1: the replay reproduces Eglantine wherever the capture holds the frame's inputs; what is left
comes from data Eglantine keeps only on the host (unbacked/trimmed/sparse-tile images) and the shadow atlas content.
In Phase 2 none of this applies: the guest's own GPU writes go to real memory on the host GPU, so pools, virtual
textures, clipmaps and the shadow atlas hold what the game wrote. A Phase 1 capture that needs them would have to
read them back from Eglantine's host images during the dump (next item if Phase 1 work continues).

Next (if more Phase 1 fidelity is wanted): capture with the once-per-image log to name the images behind the
virtual-texture pool, the clipmaps and the shadow atlas; then a dump-only download path for them in the probe
(unbacked: per bound layer/tile; trimmed: the levels the host has); re-check mkw17 and mkw16's lit-object 0.75x.

Captures: `mkw17` is the one full capture (49 GB + 10 GB arena); mkw16's dumps deleted (metadata, snapshots and
images kept); mkw18 (same procedure, logging probe) kept only for its run log (`bench/run-ptcap18.log`).

## Phase 1 exit check (2026-11-17)
| Capture | Frame | PSNR vs Eglantine | Mean abs (0-255) | Brightness | Notes |
|---|---|---|---|---|---|
| `mkw16` | racing, open course | **22.6 dB** | 9.1 | 1.04x | HUD identical; HDR main view 1.00-1.05x of Eglantine's snapshots; Toad heads, KTV trucks and pack ~0.5-0.75x, dirt material and foliage differ |
| `mkw17` | race start under the gantry | 9.6 dB | 70.1 | 0.38x | HUD, Mario, karts right; road virtual textures sample 0 (data only in Eglantine's host images), shadow atlas differs |

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
  global loads in helper lanes). This one is needed in Phase 2 too (patch programs when they are uploaded).
- `SET_SRGB_WRITE` kept at the guest value (the 4060 sRGB-encodes clears into plain RGBA8 targets with it set;
  console behaviour unknown).
- Data Eglantine never writes back (unbacked/trimmed images) is missing from the capture (above).

Verdict: **Phase 1 answered its question** (guest command streams and shaders run on the RTX 4060 and produce the
game's frame; differences trace to the capture, not the GPU). Go to Phase 2.

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
   (trampoline arena per program region), cached per program hash.
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
