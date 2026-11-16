# GPU passthrough backend: progress log

Goal and plan: `PROMPT-gpu-passthrough-continue.md`. Hand the game's own NVIDIA command streams and
shaders to the host RTX 4060 (AD107) through the RM ioctls, instead of translating every draw to Vulkan.
Host: driver 615.78.08 (open kernel module), Arch Linux.

## Status (2026-11-16)

| Gate / phase | Result |
|---|---|
| Phase 0 (standalone RM tool) | **GO**: own channel, semaphore release, compute launch, ADA_A clear all verified on hardware, 10/10 runs, no Xid, no VRAM leak |
| Gate 1 (driver access) | **Pass** on hardware |
| Gate 2 (method compatibility) | **Pass (real data)**: every 3D/compute/copy method in a captured MKW frame is a named Ampere method, plus three private 3D methods (0xF74/0xF7C/0xF80) that ADA_A accepts; only host-class changes needed (Tegra syncpoints 0x70/0x74 dropped) |
| Gate 3 (shader compatibility) | **Pass (decode level)**; guest compute SASS has started to **execute** on the RTX 4060 (Phase 1), no per-instruction failure seen yet |
| Phase 1 (capture + replay one frame) | **In progress**: capture probe done (`probes/0003`); replay runs the state restore and the first 58 frame entries (guest MME macros, draws and compute) on the host, then stops at an inline-QMD launch (see Phase 1 below) |
| Gates 4-6 | not started (Phase 1 uses pinned host memory at the guest's own VAs, pitch kind only: first evidence for gate 4) |

## Phase 0 tool: `uscl-eglantine/passthrough/nvrm_probe.cpp`

Build: `uscl-eglantine/passthrough/build.sh [out dir]` builds `nvrm_probe` and `pt_replay` (RM helpers shared in
`nvrm.h`; driver SDK headers from `/usr/src/nvidia-615.78.08/src`,
mesa class/QMD headers from `~/src/ext/mesa-nouveau/src/nouveau/headers/nvidia/classes`, both overridable
with `NV_SRC` / `MESA_CLASSES`).

Run: `nvrm_probe alloc|sem|compute <sass>|clear <sass>` (each step includes the ones before it); experiments:
`mthd <offset> <value>` (one ADA_A method), `barrier <sass> [prog]` (empty-grid QMD, release only), `inline <sass>`
(the compute QMD as an inline QMD; currently faults, see Phase 1). `<sass>` is the
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

### Capture: `probes/0003` (`EDEN_NX2_PT_CAPTURE=<dir>`, fork branch `uscl-ptcap`, commit 467ad5b6c)
Applies with `git am` on 0001-0013 (verified: same tree as the branch). Touch `<dir>/capture.now` in a race;
recording starts at the next present and covers two presents. Writes (layouts in `nx2_pt_capture.cpp`):
`maps.txt` + `dev-*.bin` (GPU VA map and device memory at the start, after flushing every GPU-dirty mapping
back to guest memory: without the flush, data the GPU wrote in earlier frames is missing), the same as
`end-*` when recording stops, `state-<ch>.bin` per channel (3D registers, the compute engine's **native**
Ampere registers (`native_registers`, not the Kepler-layout `regs`), MME code + start addresses for 3D and
compute, MME data RAM, 3D cbuf bindings), `stream.bin` (every entry as executed, prefetched lists as address
0, each entry once even when resumed after an acquire), `qmds.bin` (inline QMDs Eglantine writes) and
`mme.txt` (compute macro trace: calls, register reads, emits). Run with `bench/runs.py` + a watcher that
touches the file 20 s into the race; `WATCH_STALL_S=300` (the dump stalls the GPU front ~8 s per dump).
A capture is ~48 GB (24 GB per dump; 3.9 GB of it nonzero). Captures: `~/.local/share/uscl/ptcap/mkw8`.

### Tools: `uscl-eglantine/passthrough/ptcap.py` (+ `pt_replay`)
- `ptcap.py stats <cap>`: methods per class with mesa names. `dump <cap> <entry> [n]`: decoded methods.
  `peek <cap> <va>`: snapshot words. `qmds <cap> [entry] [host readback]`: Eglantine's inline QMDs vs the host's.
  `image <file> <w> <h> <fmt> <mem> <png>`: de-tile a read-back color target (block linear GOBs).
- `ptcap.py build <cap>` writes `<cap>/replay/{plan.txt,push.bin,targets.txt}`. `PT_SPLIT=<entry>` cuts that
  entry into one fenced sub-entry per macro call / launch (numbered entry*1000+n) to bisect a failure;
  `PT_NO_END=1` ignores the end-of-frame dump.
- `pt_replay <cap>/replay [entries] [va:size:file ...]`: maps the plan, runs entries one at a time, readbacks
  after the run or a timeout. Each failure so far killed only its own channel (Xid 11/13/31 on pt_replay's
  channel); the probe's `clear` test passes after every one and the desktop never noticed.

What the replay does (all in `ptcap.py build`, the C++ only executes the plan):
- **Memory**: pinned host memory (`NV01_MEMORY_SYSTEM_OS_DESCRIPTOR` over our own anonymous mmap; RM refuses
  CPU maps of its own sysmem here, status 0x1f) mapped at the guest's own VAs with fixed 4 KiB mappings (guest
  VAs 0x114b01b000..0x86a91c6000, all < 1 TiB). Real backing for 2 MiB device chunks with data (~2200, 4.4 GB)
  plus every chunk the frame renders, copies or uploads into; one 64 MiB zero block aliased under all other
  mapped pages so nothing can fault. Host-owned buffers at 0xF0_0000_0000+ (local memory) and 0xF8_0000_0000+
  (channels). All guest kinds are 0 (pitch, uncompressed). BAR1 is 256 MiB, hence host memory, not vidmem.
- **Channels**: one TSG + SUBCONTEXT_SYNC ctxshare + GPFIFO per guest channel (3: one with 3D/compute/I2M/2D/
  copy, two compute-only), all on one VA space; entries run in recorded order, each fenced and waited for.
- **State restore** per channel: SET_OBJECT with host classes, each class's MME program and start addresses,
  MME data RAM (3D only: `SET_MME_MEM_ADDRESS` + `MME_DMA_READ` from a host copy; without it the first
  entry trips Xid 13 "Const out of Bound", CCACHE LDCONST_OOB), every nonzero register whose name is plain
  state (`SET_*`, `OGL_SET_*`, `LOAD_CONSTANT_BUFFER_OFFSET`, `LOAD_INLINE_QMD_DATA(0..62)`: macros rewrite part
  of the inline QMD and the rest is the guest's template), cbuf bindings, local memory moved to a host buffer
  sized for AD107's 72 TPC slots (the guest sized its own for T239; sizes kept), zcull off, shadow RAM mode 1.
  Never sent: `SET_FALCON*`, `SET_RESERVED_SW_METHOD*`, `SET_INSTRUMENTATION_*`, `SET_SPA_VERSION` (host keeps 8.9).
  Array lengths are not in the mesa headers: an array ends at the next unrelated method (an unbounded guess
  once sent a non-method, Xid 11).
- **Stream rewrite**: semaphore acquires (`SEMAPHORED` ACQ*, report acquire/trap) dropped (entries are
  serialized), Tegra syncpoints dropped, local memory addresses rewritten, re-encoded.

### Where it stops: inline QMDs (next step)
NVN builds compute work with macro 0x281 (`CALL_MME_MACRO(57)`): `SET_INLINE_QMD_ADDRESS_B` then 64
`LOAD_INLINE_QMD_DATA` writes (Eglantine's trace, `mme.txt`; the macro reads no registers). Eglantine writes
those 256 bytes to memory and launches; the QMD it records is a valid QMD v3 (1x1x1 x 32 threads, program
0x114c475d00) whose dependent pointer chains into a 16-slot ring of barrier QMDs. On the host the same entry
fails with SKEDCHECK16 CTA_THREAD_DIMENSION_ZERO for that QMD address (ESR 0x40702c): SKED reads the slot's
**old** memory, not the inline data. `nvrm_probe inline` (Phase 0's working compute QMD sent as an inline QMD)
also launches but the kernel writes to VA 0 (Xid 31, write): so Ada's inline-QMD path is not the plain "write to
memory, then launch" Eglantine models. Next:
1. Pin down inline-QMD semantics on Ada with `nvrm_probe inline`: read the slot back before and after; try
   `SEND_PCAS_A` on the slot after the inline writes; check the field Eglantine tests for launching
   (`regs[0xa7]` bit 24, QMD word 4 bit 12). If Ada differs, the replay rewrites inline QMDs into plain memory
   QMDs: emulate the 64 inline words in the pushbuffer as an I2M (`LAUNCH_DMA`) to the slot followed by
   `SEND_PCAS_A` (macros emit them, so the cheapest form is patching macro 0x281's last emits or running it
   in a host MME with `LOAD_INLINE_QMD_DATA` remapped).
2. Memory timing: the end-of-frame dump poisons rings reused within the frame (QMD/parameter rings); use the
   start dump (`PT_NO_END=1`) and, where the CPU writes during the frame, per-entry deltas (not built).
3. Then run to the end, read back the last color target in `targets.txt` (2560x1440 A8B8G8R8_SRGB, last
   drawn at the final entries) with `pt_replay ... va:size:file` (size: `ptcap.size_of`), `ptcap.py image`, and
   compare with Eglantine's output (bench screenshot of the same moment, or `EDEN_NX2_FRAME_DUMP`).

Facts learned on the way:
- MKW frame: ~1100 entries over 2 presents, ~100k words; 3D draws to 2560x1440 (HDR BF10GF11RF11 scene, sRGB
  RGBA8 output); 167 color targets.
- `SEND_SIGNALING_PCAS2_B` action 0xC (INCREMENT_DEPENDENCE) drives NVN's barrier-QMD ring; empty-grid QMDs
  with program 0 are fine on Ada (`nvrm_probe barrier`).
- The guest CPU writes ~17 MB of GPU-visible memory during a frame (265 64 KiB blocks differ start vs end).
- Eglantine on NX2 runs compute macros on the split's front (`FrontRunMacro`) against `fs.registers`.

