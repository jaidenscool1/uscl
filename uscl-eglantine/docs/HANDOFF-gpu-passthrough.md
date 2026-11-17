# GPU passthrough backend: progress log

Goal and plan: `PROMPT-gpu-passthrough-continue.md`. Hand the game's own NVIDIA command streams and
shaders to the host RTX 4060 (AD107) through the RM ioctls, instead of translating every draw to Vulkan.
Host: driver 615.78.08 (open kernel module), Arch Linux.

## Status (2026-11-17)

| Gate / phase | Result |
|---|---|
| Phase 0 (standalone RM tool) | **GO**: own channel, semaphore release, compute launch, ADA_A clear all verified on hardware, 10/10 runs, no Xid, no VRAM leak |
| Gate 1 (driver access) | **Pass** on hardware |
| Gate 2 (method compatibility) | **Pass (real data)**: every 3D/compute/copy method in a captured MKW frame is a named Ampere method, plus three private 3D methods (0xF74/0xF7C/0xF80) that ADA_A accepts; only host-class changes needed (Tegra syncpoints 0x70/0x74 dropped) |
| Gate 3 (shader compatibility) | **Pass (decode level)**; guest compute SASS has started to **execute** on the RTX 4060 (Phase 1), no per-instruction failure seen yet |
| Phase 1 (capture + replay one frame) | **Whole racing frame replays** (sixth pass, 2026-11-17): all 7757 entries of capture `mkw13` (racing, 3 channels) run on the RTX 4060 with no Xid, **~36 ms of GPU wait in total** (slowest entry 0.7 ms) with guest memory in VRAM. The fix was emulator-wide: Ada does not perform global loads in pixel-shader helper lanes, so `PT_HELPER_LOADS=1` routes every LDG of every pixel program through a trampoline that zeroes the destination in helper lanes. Output not yet right: the HUD layer matches the reference, the G-buffer shows the right scene (karts, track), but the lit scene composited under the HUD is black (next: find the lighting/composite passes). Earlier: the intro-flyover frame `mkw10` replays to a recognisable course image (third pass) |
| New: texture formats | **Open, needs work**: 28% of MKW's texture headers are ASTC (8x8, 6x6, 4x4, 5x5); the RTX 4060 has no ASTC sampling (`textureCompressionASTC_LDR` false), so passthrough needs ASTC transcoding + header repointing |
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
trace and `deltas.bin` are complete. Older captures deleted (mkw8-mkw12).
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

Facts learned on the way:
- MKW frame: ~1100 entries over 2 presents, ~100k words; 3D draws to 2560x1440 (HDR BF10GF11RF11 scene, sRGB
  RGBA8 output); 167 color targets.
- `SEND_SIGNALING_PCAS2_B` action 0xC (INCREMENT_DEPENDENCE) drives NVN's barrier-QMD ring; empty-grid QMDs
  with program 0 are fine on Ada (`nvrm_probe barrier`).
- The guest CPU writes ~17 MB of GPU-visible memory during a frame (265 64 KiB blocks differ start vs end).
- Eglantine on NX2 runs compute macros on the split's front (`FrontRunMacro`) against `fs.registers`.

