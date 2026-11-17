#!/usr/bin/env python3
"""Read a capture made with Eglantine's EDEN_NX2_PT_CAPTURE probe (layout: nx2_pt_capture.cpp).
usage: ptcap.py stats <capture dir>     methods the frame uses, per class, with mesa names
       ptcap.py build <capture dir>     write <dir>/replay/{plan.txt,push.bin} for pt_replay
       ptcap.py dump <capture dir> <first entry> [count]   decoded methods of captured entries
       ptcap.py peek <capture dir> <gpu va hex> [bytes]   snapshot memory as words
       ptcap.py qmds <capture dir> [entry] [host readback]   Eglantine's inline QMDs (vs. the host's)
       ptcap.py image <file> <width> <height> <format hex> <memory hex> <out.png>   a read-back color target
"""
import collections, os, re, struct, sys

MESA = os.environ.get("MESA_CLASSES", os.path.expanduser("~/src/ext/mesa-nouveau/src/nouveau/headers/nvidia/classes"))
# Eglantine's engine ids (puller.h) -> guest class header, host class.
ENGINES = {0xB197: ("clc797", 0xC997), 0xB1C0: ("clc7c0", 0xC9C0), 0xA140: ("cla140", 0xA140),
           0x902D: ("cl902d", 0x902D), 0xB0B5: ("clc7b5", 0xC7B5)}
HOST = "clc56f"
# Methods the guest uses that no public header names (Eglantine's maxwell_3d.h names the first two);
# ADA_A accepts them with the frame's arguments (nvrm_probe mthd, 2026-11-16).
PRIVATE = {"clc797": {0xF74: "PRIVATE_INVALIDATE_TEXTURE_DATA_CACHE", 0xF7C: "PRIVATE_TILED_CACHE_BARRIER",
                      0xF80: "PRIVATE_F80"}}


def methods(header):
    """{byte offset: name} for every method in a mesa class header. A method define is the one directly
    followed by its field defines (NAME_FIELD hi:lo); enum values look the same as offsets otherwise."""
    lines = [l.split() for l in open(f"{MESA}/{header}.h") if l.startswith("#define ")]
    out = {}
    arrays = []
    for i, l in enumerate(lines[:-1]):
        name, val = l[1], " ".join(l[2:])
        base = re.sub(r"\(\w\)$", "", name)
        nxt = lines[i + 1]
        if not (nxt[1].startswith(base + "_") and re.fullmatch(r"\d+:\d+", nxt[-1])):
            continue
        short = base.split("_", 1)[1]
        m = re.fullmatch(r"\(?(0x[0-9a-fA-F]+)\+\(\w\)\*(\d+)\)?", val.replace(" ", ""))
        if m:
            arrays.append((int(m[1], 16), int(m[2]), short))
        elif re.fullmatch(r"\(?0x[0-9a-fA-F]+\)?", val):
            out[int(val.strip("()"), 16)] = short
    return out, sorted(arrays)


class Names:
    def __init__(self, header):
        self.plain, arrays = methods(header)
        self.plain.update(PRIVATE.get(header, {}))
        # Array lengths are not in the headers: an array ends at the next method that is not one of the
        # members interleaved with it (bases within one stride).
        bases = sorted(set(self.plain) | {a[0] for a in arrays})
        self.arrays = []
        for base, stride, name in arrays:
            end = next((b for b in bases if b >= base + stride), 0x4000)  # the last ones run to the end
            self.arrays.append((base, stride, name, end))

    def off(self, name, i=0):
        for o, n in self.plain.items():
            if n == name:
                return o
        for base, stride, n, end in self.arrays:
            if n == name:
                assert base + i * stride < end, (name, i)
                return base + i * stride
        raise KeyError(name)

    def __call__(self, off):
        """Name of the method at byte offset off: a plain method, else the array with the highest base
        below it whose stride fits (array lengths are not in the headers)."""
        if off in self.plain:
            return self.plain[off]
        best = None
        for base, stride, name, end in self.arrays:
            if base <= off < end and (off - base) % stride == 0 and (best is None or base > best[0]):
                best = (base, stride, name)
        return f"{best[2]}({(off - best[0]) // best[1]})" if best else None


def read_states(d):
    states = {}
    for f in os.listdir(d):
        if f.startswith("state-"):
            b = open(os.path.join(d, f), "rb").read()
            magic, ch, mm = struct.unpack_from("<IiQ", b)
            assert magic == 0x31535450
            engines = struct.unpack_from("<8I", b, 16)
            off = 16 + 32 + 16 + 4
            regs3d = struct.unpack_from("<3584I", b, off); off += 0xE00 * 4
            regsc = struct.unpack_from("<4096I", b, off); off += 0x1000 * 4  # compute: native (Ampere) layout
            mme = struct.unpack_from("<32768I", b, off); off += 0x8000 * 4
            pos3d = struct.unpack_from("<128I", b, off); off += 0x200
            posc = struct.unpack_from("<128I", b, off); off += 0x200
            dram = struct.unpack_from("<3072I", b, off); off += 0xC00 * 4
            cbufs = [[struct.unpack_from("<IIQ", b, off + (s * 18 + i) * 16) for i in range(18)] for s in range(5)]
            off += 5 * 18 * 16
            mme_c = struct.unpack_from("<32768I", b, off)  # the compute engine's own MME code
            states[ch] = dict(engines=engines, regs={0xB197: regs3d, 0xB1C0: regsc}, mme=mme, mme_c=mme_c, pos3d=pos3d,
                              posc=posc, dram=dram, cbufs=cbufs,
                              native_turing=struct.unpack_from("<I", b, 64)[0])
    return states


def read_stream(d):
    """[(channel, gpu address, dma method, dma subchannel, dma count, words)] in execution order."""
    b = open(os.path.join(d, "stream.bin"), "rb").read()
    out, o = [], 0
    while o < len(b):
        magic, ch, addr, n, dma, count = struct.unpack_from("<IiQIII", b, o)
        assert magic == 0x31455450, hex(o)
        o += 28
        out.append((ch, addr, dma & 0xFFFF, dma >> 16, count, struct.unpack_from(f"<{n}I", b, o)))
        o += 4 * n
    return out


def decode(words, st):
    """Yield (subchannel, method word index, argument, header index) for an entry; st carries the
    method in flight across entries: [method, subchannel, count, mode]."""
    i = 0
    while i < len(words):
        w = words[i]
        if st[2]:
            yield st[1], st[0], w, i
            st[2] -= 1
            if st[3] == 1:
                st[0] += 1
            elif st[3] == 5:  # IncreaseOnce: first argument at method, the rest at method + 1
                st[0] += 1
                st[3] = 3
            i += 1
            continue
        mode, count, subc, mthd = w >> 29, (w >> 16) & 0x1FFF, (w >> 13) & 7, w & 0x1FFF
        if mode == 4:
            yield subc, mthd, count, i
        elif mode in (1, 3, 5):
            st[:] = [mthd, subc, count, mode]
        elif w != 0:
            raise ValueError(f"unsupported header {w:#010x}")
        i += 1


def stats(d):
    states, stream = read_states(d), read_stream(d)
    names = {e: Names(h) for e, (h, _) in ENGINES.items()}
    host = Names(HOST)
    counts = collections.Counter()
    unknown = collections.Counter()
    st = {ch: [0, 0, 0, 0] for ch in states}
    for ch, addr, *_rest, words in stream:
        engines = list(states[ch]["engines"])
        for subc, mthd, arg, _ in decode(words, st[ch]):
            if mthd < 0x40:
                cls, n = "host", host(mthd * 4)
                if mthd == 0:
                    engines[subc] = arg & 0xFFFF
            else:
                e = engines[subc]
                cls, n = (f"{e:#x}", names[e](mthd * 4)) if e in names else (f"{e:#x}", None)
            if n is None:
                unknown[(cls, mthd * 4)] += 1
            else:
                counts[(cls, re.sub(r"\(\d+\)", "(i)", n))] += 1
    print(f"{len(stream)} entries, {sum(len(e[-1]) for e in stream)} words, channels {sorted(states)}")
    for cls in sorted({c for c, _ in counts}):
        items = sorted(((n, v) for (c, n), v in counts.items() if c == cls), key=lambda x: -x[1])
        print(f"== {cls}: {len(items)} distinct methods, {sum(v for _, v in items)} calls")
        print("   " + ", ".join(f"{n} {v}" for n, v in items))
    if unknown:
        print("== not in the mesa headers:", ", ".join(f"{c} {m:#x} x{v}" for (c, m), v in unknown.most_common()))


# Texture header (TIC) component formats, from NVIDIA's open-gpu-doc (mesa has no clc797tex.h).
TIC_FORMATS = {m[1]: int(m[2], 16) for l in open(os.environ.get("TEX_HEADER", os.path.expanduser(
    "~/src/ext/open-gpu-doc/classes/3d/clc797tex.h")))
    if (m := re.match(r"#define NVC797_TEXHEAD_BL_COMPONENTS_SIZES_(\w+)\s+0x([0-9a-fA-F]+)", l))}

# Texture header fields: name -> (high bit, low bit).
TIC_FIELDS = {m[1]: (int(m[2]), int(m[3])) for l in open(os.environ.get("TEX_HEADER", os.path.expanduser(
    "~/src/ext/open-gpu-doc/classes/3d/clc797tex.h")))
    if (m := re.match(r"#define NVC797_TEXHEAD_BL_(\w+)\s+MW\((\d+):(\d+)\)", l))}

CHUNK = 1 << 21
# PT_STATS counters: (report, pipeline location): DA vertices and primitives, VS invocations, clipper invocations
# and primitives, zpass pixels (64-bit field: the count is in the upper word), PS invocations.
STATS = [(0x1, 0x1), (0x3, 0x1), (0x5, 0x2), (0xF, 0x4), (0x11, 0x4), (0x2, 0xF), (0x13, 0xA)]  # locations as NVK uses them (VTG_PRIMITIVES_OUT at VPC: Xid 69)
STATS_NAMES = ["vertices", "DA prims", "VS", "clipper", "clipper prims", "Z pass", "PS"]
HOST_BASE = 0xF0_0000_0000  # host-owned buffers: above every guest mapping, inside the 1 TiB window
ZERO_SIZE = BLOCK_SIZE = 64 << 20
TPC_SLOTS = 72  # GR litter maxima on AD107, 12 GPCs x 6 TPCs (nvrm_probe gr info)
# Never sent to the host: firmware/software methods (they trap into RM), instrumentation, and zcull, whose
# on-chip state does not carry over to a fresh host context (zcull is only a culling optimisation: off).
NEVER = re.compile(r"SET_FALCON\d+|SET_RESERVED_SW_METHOD\d+|SET_INSTRUMENTATION_METHOD_\w+|SET_ZCULL|CLEAR_ZCULL_REGION")
# Restored from the captured registers: plain state. Not: triggers named SET_*, local memory (rewritten),
# the shader ISA version (the host keeps its own; guest SASS decodes as SM89, gate 3).
NO_RESTORE = re.compile(r"SET_REPORT_SEMAPHORE_D|SET_MME_SHADOW_RAM_CONTROL|SET_SPA_VERSION|"
                        r"SET_SHADER_LOCAL_MEMORY_[AB]|" + NEVER.pattern)  # local memory: address rewritten, sizes kept


# Restored even when 0: the host context's default differs from the guest's (bound texture handles are
# read from cbuf slot SET_BINDLESS_TEXTURE; the guest leaves it 0).
ALWAYS = {"SET_BINDLESS_TEXTURE"}


def restorable(name):
    if name is None:
        return False
    base = re.sub(r"\(\d+\)$", "", name)
    # LOAD_INLINE_QMD_DATA is a sequential buffer on the hardware (the 64th word writes the QMD out and
    # launches it), not 64 registers: never restored (macro 0x281 writes all 64 words each time).
    if base == "LOAD_INLINE_QMD_DATA":
        return False
    return (base.startswith(("SET_", "OGL_SET_")) or base == "LOAD_CONSTANT_BUFFER_OFFSET") and not NO_RESTORE.fullmatch(base)


def read_maps(d):
    out = []
    for line in open(os.path.join(d, "maps.txt")):
        mm, gpu, dev, size, kind, sparse = line.split()
        out.append((int(gpu, 16), int(dev, 16), int(size, 16), int(kind, 16), sparse == "1"))
    return sorted(out)


def data_chunks(d, cache):
    """{device chunk: [(file, file offset, chunk offset, length)]} for every 2 MiB device chunk with data.
    Per 64 KiB block the end-of-frame dump (end-dev-*) wins where it differs from the start: the guest CPU
    fills QMD and uniform rings during the frame, and their slots are not reused before the ring wraps.
    shortcut: GPU data read before it is rewritten (history buffers) then starts as this frame's output;
    upgrade to per-entry memory deltas if that shows."""
    if os.path.exists(cache):
        out = {}
        for line in open(cache):
            c, f, fo, co, n = line.split()
            out.setdefault(int(c, 16), []).append((f, int(fo, 16), int(co, 16), int(n, 16)))
        return out
    import numpy as np
    B = 1 << 16
    out, changed = {}, 0
    for f in sorted(x for x in os.listdir(d) if x.startswith("dev-")):
        base, size = int(f[4:-4], 16), os.path.getsize(os.path.join(d, f))
        start = np.memmap(os.path.join(d, f), dtype=np.uint8, mode="r")
        endf = "end-" + f
        end = None
        if not os.environ.get("PT_NO_END") and os.path.exists(os.path.join(d, endf)) and os.path.getsize(os.path.join(d, endf)) == size:
            end = np.memmap(os.path.join(d, endf), dtype=np.uint8, mode="r")
        a = base
        while a < base + size:
            e = min((a // B + 1) * B, base + size)
            lo, hi = a - base, e - base
            src, mm = f, start
            if end is not None and not np.array_equal(start[lo:hi], end[lo:hi]):
                src, mm = endf, end
                changed += 1
            if mm[lo:hi].any():
                parts = out.setdefault(a // CHUNK, [])
                p = parts[-1] if parts else None
                if p and p[0] == src and p[1] + p[3] == lo and p[2] + p[3] == a % CHUNK:
                    parts[-1] = (src, p[1], p[2], p[3] + (e - a))
                else:
                    parts.append((src, lo, a % CHUNK, e - a))
            a = e
    print(f"data: {changed} 64 KiB blocks taken from the end-of-frame dump")
    with open(cache, "w") as fh:
        for c, parts in sorted(out.items()):
            for f, fo, co, n in parts:
                fh.write(f"{c:x} {f} {fo:x} {co:x} {n:x}\n")
    return out


def encode(methods):
    """Pushbuffer words for [(subchannel, method word index, argument)]: incrementing runs, else
    non-incrementing runs of one method."""
    words, i = [], 0
    while i < len(methods):
        sc, m, _ = methods[i]
        j = i + 1
        inc = j < len(methods) and methods[j][0] == sc and methods[j][1] == m + 1
        while j < len(methods) and j - i < 0x1FFF and methods[j][0] == sc and methods[j][1] == (m + (j - i) if inc else m):
            j += 1
        words.append(((1 if inc else 3) << 29) | ((j - i) << 16) | (sc << 13) | m)
        words.extend(a for _, _, a in methods[i:j])
        i = j
    return words


def pixel_programs(d, states, stream, n3d):
    """Pixel shader program addresses the frame draws with: the 3D state, replayed with the 3D macro trace
    ('c'/'e' lines in mme.txt: the program addresses are set inside macros), sampled after each macro call
    and draw method (A and B are written as a pair by then)."""
    tr = collections.defaultdict(collections.deque)
    for line in open(os.path.join(d, "mme.txt")):
        k, kind, a, v = line.split()
        if kind == "c":
            tr[int(k)].append([])
        elif kind == "e":
            tr[int(k)][-1].append((int(a, 16), int(v, 16)))
    pa, pb, ps = (n3d.off(x, 5) // 4 for x in ("SET_PIPELINE_PROGRAM_ADDRESS_A", "SET_PIPELINE_PROGRAM_ADDRESS_B", "SET_PIPELINE_SHADER"))
    regs = {ch: dict(enumerate(s["regs"][0xB197])) for ch, s in states.items()}
    st, out = {ch: [0, 0, 0, 0] for ch in states}, set()
    for k, (ch, _addr, *_r, words) in enumerate(stream):
        r, calls = regs[ch], tr.get(k, collections.deque())
        for sc, m, a, _ in decode(words, st[ch]):
            if m < 0x40 or states[ch]["engines"][sc] != 0xB197:
                continue
            r[m] = a
            nm = n3d(m * 4) or ""
            if nm.startswith("CALL_MME_MACRO(") and calls:
                r.update((mm, vv) for mm, vv in calls.popleft() if mm < 0xE00)
            elif not nm.startswith(("DRAW", "BEGIN")):
                continue
            if r.get(ps, 0) & 1 and r.get(ps, 0) >> 4 & 0xF == 5:
                out.add(r.get(pa, 0) << 32 | r.get(pb, 0))
    return out


# SM86 SASS words for the helper-lane load trampolines (PT_HELPER_LOADS); checked with nvdisasm.
SASS_CTL = ((1 << 128) - 1) ^ ((1 << 105) - 1)


def sass_ctl(x, stall=2, yld=0, wr=7, rd=7, wait=0):
    return (x & ~SASS_CTL) | ((stall | yld << 4 | wr << 5 | rd << 8 | wait << 11) << 105)


def sass_bra(off):  # BRA to (this instruction + 16 + off)
    return sass_ctl(0x7947 | 7 << 12 | (off & ((1 << 50) - 1)) << 32 | 0x03800000 << 64, stall=5, yld=1)


def sass_pixld_covmask(p, wr):  # PIXLD.COVMASK RZ, P<p>: P<p> = this lane is covered (not a helper)
    return sass_ctl(0x925 | 7 << 12 | 0xFF << 16 | 1 << 78 | p << 81, stall=1, wr=wr)


def sass_mov_rz_unless(p, r, wait):  # @!P<p> MOV R<r>, RZ
    return sass_ctl(0x0202 | (8 | p) << 12 | r << 16 | 0xF << 72 | 0xFF << 32, wait=wait)


def bl_layout(w, h, d, levels, tile, bpb_log2, block, tws):
    """One layer of a block-linear image: [(byte offset, block height, block depth, GOBs in x, size)] per mip level,
    and the layer stride. w/h/d in texels, tile = the format's block size in texels (ASTC), block = log2 GOBs
    per block (w, h, d), tws = TILE_WIDTH_IN_GOBS. Port of Eglantine's texture_cache/util.cpp
    (CalculateLevelSizes, AlignLayerSize, AdjustMipBlockSize), which matches the guest's own layout."""
    def adj_tile(shift, unit, dim):  # AdjustTileSize
        if shift and unit << (shift - 1) >= dim:
            x, shift = unit << (shift - 1), shift - 1
            while shift:
                x >>= 1
                if x < dim:
                    break
                shift -= 1
        return shift

    out, off = [], 0
    for lv in range(levels):
        tx, ty, dz = -(-max(w >> lv, 1) // tile[0]), -(-max(h >> lv, 1) // tile[1]), max(d >> lv, 1)
        bx = tx << bpb_log2  # NumLevelBlocks: width in bytes
        ts = block if lv == 0 and levels == 1 else (adj_tile(block[0], 64, bx), adj_tile(block[1], 8, ty), adj_tile(block[2], 1, dz))
        small = bx <= 1 << (6 - bpb_log2 + tws) or ty <= 1 << (3 + block[1]) or dz < 1 << block[2]  # NumGobs
        al = 0 if small else tws
        gx, gy = -(-(-(-bx >> 6)) >> al) << al, -(-ty >> 3)
        size = (-(-gx >> ts[0])) * (-(-gy >> ts[1])) * (-(-dz >> ts[2])) << (9 + sum(ts))
        mb = []  # AdjustMipBlockSize: the block shrinks while the level fits in half of it
        for b_, n, unit in ((block[1], ty, 8), (block[2], dz, 1)):
            while b_ > 0 and n <= (1 << (b_ - 1)) * unit and not (unit == 1 and lv == 0 and levels == 1):
                b_ -= 1
            mb.append(b_)
        out.append((off, mb[0], mb[1], gx, size))
        off += size
    if tws:  # AlignLayerSize
        al = 9 + tws + block[1] + block[2]
    else:
        bh, bd, ah = block[1], block[2], -(-h // tile[1]) * tile[1]
        while bh and ah <= (1 << (bh - 1)) * 8:
            bh -= 1
        while bd and d <= 1 << (bd - 1):
            bd -= 1
        al = 9 + bh + bd
    return out, -(-off >> al) << al


def bl_runs(row_bytes, rows, depth, bh, bd, gx):
    """Byte offset of every 16-byte run of a block-linear level, in linear order (depth, rows, row_bytes / 16).
    The GOB is the 'native' one (Eglantine's NX2 tiling: the legacy yuzu GOB with offset bits 5 and 6
    exchanged), which is the layout of both the guest's memory and the RTX 4060's."""
    import numpy as np
    z, y, x = (a.astype(np.int64) for a in np.ogrid[0:depth, 0:rows, 0:row_bytes:16])
    blk = gx << (9 + bh + bd)
    off = ((z >> bd) * (-(-rows >> (bh + 3)) * blk) + ((z & ((1 << bd) - 1)) << (9 + bh)) +
           ((y >> 3) >> bh) * blk + (((y >> 3) & ((1 << bh) - 1)) << 9) + ((x >> 6) << (9 + bh + bd)) +
           ((x & 32) << 3) + ((y & 6) << 5) + ((x & 16) << 1) + ((y & 1) << 4))  # SWIZZLE_X/Y_BITS 0x12F/0xD0
    t = ((off >> 5) ^ (off >> 6)) & 1
    return (off ^ (t << 5) ^ (t << 6)).reshape(-1)


def build(d):
    out_dir = os.path.join(d, "replay")
    os.makedirs(out_dir, exist_ok=True)
    states, stream, maps = read_states(d), read_stream(d), read_maps(d)
    names = {e: Names(h) for e, (h, _) in ENGINES.items()}
    n3d, nc = names[0xB197], names[0xB1C0]
    chans = sorted(states)

    # Host local memory: the guest sized its own for T239's TPC count.
    per_tpc = 0
    for s in states.values():
        r3, rc = s["regs"][0xB197], s["regs"][0xB1C0]
        per_tpc = max(per_tpc, (r3[n3d.off("SET_SHADER_LOCAL_MEMORY_C") // 4] << 32) | r3[n3d.off("SET_SHADER_LOCAL_MEMORY_D") // 4],
                      (rc[nc.off("SET_SHADER_LOCAL_MEMORY_NON_THROTTLED_A") // 4] << 32) | rc[nc.off("SET_SHADER_LOCAL_MEMORY_NON_THROTTLED_B") // 4])
    lm_size = -(-max(per_tpc, 1 << 20) * TPC_SLOTS // CHUNK) * CHUNK
    lm_va = HOST_BASE
    LM = {0xB197: ("SET_SHADER_LOCAL_MEMORY_A", "SET_SHADER_LOCAL_MEMORY_B"),
          0xB1C0: ("SET_SHADER_LOCAL_MEMORY_A", "SET_SHADER_LOCAL_MEMORY_B")}
    lm_methods = {e: (names[e].off(a) // 4, names[e].off(b) // 4) for e, (a, b) in LM.items()}

    written = set()  # (gpu address, size) the frame may write: gets real backing

    def rmw(e, regs, name, i=0):
        return regs.get(names[e].off(name, i) // 4, 0)

    def a64(e, regs, hi, lo, i=0):
        return (rmw(e, regs, hi, i) << 32) | rmw(e, regs, lo, i)

    targets = {}  # color target address -> [width, height, format, memory, first entry, last entry]

    def note_targets(regs, k=-1):
        ct_select = rmw(0xB197, regs, "SET_CT_SELECT")
        for i in range(8):
            addr = a64(0xB197, regs, "SET_COLOR_TARGET_A", "SET_COLOR_TARGET_B", i)
            if addr:
                w, h = rmw(0xB197, regs, "SET_COLOR_TARGET_WIDTH", i), rmw(0xB197, regs, "SET_COLOR_TARGET_HEIGHT", i)
                fmt = rmw(0xB197, regs, "SET_COLOR_TARGET_FORMAT", i)
                if fmt and i < (ct_select & 0xF) and k >= 0:
                    t = targets.setdefault(addr, [w, h, fmt, rmw(0xB197, regs, "SET_COLOR_TARGET_MEMORY", i), k, k])
                    t[5] = k
                layers = max(1, rmw(0xB197, regs, "SET_COLOR_TARGET_THIRD_DIMENSION", i) & 0xFFFF)
                pitch = rmw(0xB197, regs, "SET_COLOR_TARGET_ARRAY_PITCH", i) * 4
                written.add((addr, max(pitch * layers, w * h * 16) + CHUNK))
        addr = a64(0xB197, regs, "SET_ZT_A", "SET_ZT_B")
        if addr:
            w, h = rmw(0xB197, regs, "SET_ZT_SIZE_A"), rmw(0xB197, regs, "SET_ZT_SIZE_B")
            layers = max(1, rmw(0xB197, regs, "SET_ZT_SIZE_C") & 0xFFFF)
            pitch = rmw(0xB197, regs, "SET_ZT_ARRAY_PITCH") * 4
            written.add((addr, max(pitch * layers, w * h * 8) + CHUNK))

    shadow = {}  # (channel, engine) -> {method: value}
    for ch, s in states.items():
        for e in (0xB197, 0xB1C0):
            shadow[(ch, e)] = dict(enumerate(s["regs"][e]))
        for e in (0xA140, 0x902D, 0xB0B5):
            shadow[(ch, e)] = {}
    host_shadow = {ch: {} for ch in chans}

    # Per channel restore entry.
    entries = []
    drams = []  # (channel index, channel, va of its MME data RAM image)
    stats = collections.Counter()
    for idx, ch in enumerate(chans):
        s = states[ch]
        engines = s["engines"]
        out = []
        for sc, e in enumerate(engines):
            if e in ENGINES:
                out.append((sc, 0, ENGINES[e][1]))
        # Each class has its own MME program and start addresses (Eglantine keeps the compute ones apart
        # once the guest uploads through the compute class): load each through its own subchannel.
        for mme_sc, e in enumerate(engines):
            if e not in (0xB197, 0xB1C0):
                continue
            code, pos = (s["mme"], s["pos3d"]) if e == 0xB197 else (s["mme_c"], s["posc"])
            if not any(code):
                continue
            mme_n = names[e]
            last = -(-(max(i for i, v in enumerate(code) if v) + 1) // 3) * 3  # whole 96-bit instructions
            out.append((mme_sc, mme_n.off("LOAD_MME_INSTRUCTION_RAM_POINTER") // 4, 0))
            out += [(mme_sc, mme_n.off("LOAD_MME_INSTRUCTION_RAM") // 4, v) for v in code[:last]]
            out.append((mme_sc, mme_n.off("LOAD_MME_START_ADDRESS_RAM_POINTER") // 4, 0))
            out += [(mme_sc, mme_n.off("LOAD_MME_START_ADDRESS_RAM") // 4, v) for v in pos]
        mme_sc = next(sc for sc, e in enumerate(engines) if e in (0xB197, 0xB1C0))
        if engines[mme_sc] == 0xB197:  # MME data RAM: DMA-read from a host copy (compute has no such method)
            dram_va = lm_va + lm_size + idx * CHUNK
            open(os.path.join(out_dir, f"dram-{ch}.bin"), "wb").write(struct.pack("<3072I", *s["dram"]))
            drams.append((idx, ch, dram_va))
            out += [(mme_sc, n3d.off("SET_MME_MEM_ADDRESS_A") // 4, dram_va >> 32),
                    (mme_sc, n3d.off("SET_MME_MEM_ADDRESS_B") // 4, dram_va & 0xFFFFFFFF),
                    (mme_sc, n3d.off("SET_MME_DATA_RAM_ADDRESS") // 4, 0),
                    (mme_sc, n3d.off("MME_DMA_READ") // 4, len(s["dram"])),
                    (0, 0x78 // 4, 0)]  # host WFI: the read is done before the registers below
            if os.environ.get("PT_DRAM_CHECK"):  # copy the data RAM back out to dram_va + 1 MiB
                out += [(mme_sc, n3d.off("SET_MME_MEM_ADDRESS_A") // 4, (dram_va + (1 << 20)) >> 32),
                        (mme_sc, n3d.off("SET_MME_MEM_ADDRESS_B") // 4, (dram_va + (1 << 20)) & 0xFFFFFFFF),
                        (mme_sc, n3d.off("SET_MME_DATA_RAM_ADDRESS") // 4, 0),
                        (mme_sc, n3d.off("MME_DMA_WRITE") // 4, len(s["dram"])),
                        (0, 0x78 // 4, 0)]
                print(f"PT_DRAM_CHECK: channel {ch} data RAM copy at {dram_va + (1 << 20):x}")
        for sc, e in enumerate(engines):
            if e not in (0xB197, 0xB1C0):
                continue
            n, regs = names[e], s["regs"][e]
            out.append((sc, n.off("SET_MME_SHADOW_RAM_CONTROL") // 4, 0))  # track while restoring
            for m, v in enumerate(regs):
                if m >= 0x40 and (v or n(m * 4) in ALWAYS) and restorable(n(m * 4)):
                    out.append((sc, m, v))
                    stats[e] += 1
            if e == 0xB197:
                out.append((sc, n.off("SET_ZCULL") // 4, 0))
            out.append((sc, lm_methods[e][0], lm_va >> 32))
            out.append((sc, lm_methods[e][1], lm_va & 0xFFFFFFFF))
            if e == 0xB197:
                for stage, slots in enumerate(s["cbufs"]):
                    for slot, (enabled, size, addr) in enumerate(slots):
                        if enabled:
                            out += [(sc, n.off("SET_CONSTANT_BUFFER_SELECTOR_A") // 4, size),
                                    (sc, n.off("SET_CONSTANT_BUFFER_SELECTOR_B") // 4, addr >> 32),
                                    (sc, n.off("SET_CONSTANT_BUFFER_SELECTOR_C") // 4, addr & 0xFFFFFFFF),
                                    (sc, n.off("BIND_GROUP_CONSTANT_BUFFER", stage) // 4, (slot << 4) | 1)]
                for name in ("SET_CONSTANT_BUFFER_SELECTOR_A", "SET_CONSTANT_BUFFER_SELECTOR_B",
                             "SET_CONSTANT_BUFFER_SELECTOR_C", "LOAD_CONSTANT_BUFFER_OFFSET"):
                    out.append((sc, n.off(name) // 4, regs[n.off(name) // 4]))
                note_targets(shadow[(ch, e)])
            out.append((sc, n.off("SET_MME_SHADOW_RAM_CONTROL") // 4, 1))  # the guest's steady state
        out.append((0, 0x78 // 4, 0))  # host WFI
        entries.append((idx, -1, encode(out)))

    # The frame, rewritten for the host.
    split = {int(x) for x in os.environ.get("PT_SPLIT", "").split(",") if x}
    skip = {int(x) for x in os.environ.get("PT_SKIP", "").split(",") if x}
    invalidate = bool(os.environ.get("PT_INVALIDATE"))
    stats_at = [int(x) for x in os.environ.get("PT_STATS", "").split(",") if x]
    drop_re = re.compile(os.environ["PT_DROP"]) if os.environ.get("PT_DROP") else None
    # bisecting: PT_SETV=<method name>:<old hex>:<new hex>[,...] rewrites one method's value in the stream
    setv = {(nm, int(o, 16)): int(v, 16) for nm, o, v in (x.split(":") for x in os.environ.get("PT_SETV", "").split(",") if x)}
    # PT_MME=trace: compute macro calls become the methods Eglantine's MME emitted for them (mme.txt), so
    # the host MME does not run compute macros (Eglantine's output is what the game ran).
    traced = collections.defaultdict(collections.deque)  # entry -> deque of (start, [(method, value)])
    # PT_MME3D=trace: the same for 3D macros (lowercase c/e lines, Maxwell3D::FrontRunMacro's emits), to tell a
    # host MME difference from a replay bug
    traced3d = collections.defaultdict(collections.deque)
    if os.environ.get("PT_MME") == "trace" or os.environ.get("PT_MME3D") == "trace":
        for line in open(os.path.join(d, "mme.txt")):
            k_, kind, a_, v_ = line.split()
            if kind == "C" and os.environ.get("PT_MME") == "trace":
                traced[int(k_)].append((int(a_, 16), []))
            elif kind == "E" and os.environ.get("PT_MME") == "trace":
                traced[int(k_)][-1][1].append((int(a_, 16), int(v_, 16)))
            elif kind == "c" and os.environ.get("PT_MME3D") == "trace":
                traced3d[int(k_)].append((int(a_, 16), []))
            elif kind == "e" and os.environ.get("PT_MME3D") == "trace":
                traced3d[int(k_)][-1][1].append((int(a_, 16), int(v_, 16)))
    # Only entries whose traced calls match the stream's in number (calls pair up in order). Start addresses
    # are not compared: each channel's front uploads the same macros at its own positions (macro 57 is 0x281
    # on the 3D channel, 0x42 on a compute-only one), which the captured engine state does not show.
    # PT_NODEP=1: no QMD dependencies. NVN counts finished compute work into a ring of barrier QMDs (each
    # QMD decrements one, SEND_SIGNALING_PCAS2_B INCREMENT_DEPENDENCE raises it); the ring's counters come
    # from Eglantine's scheduler, not the hardware. Entries run serialized, so the barriers (releases whose
    # waits are dropped anyway) are left out: DEPENDENT_QMD0_ENABLE/ACTION (QMD word 16, bits 0-3) cleared
    # in inline QMDs, INCREMENT_DEPENDENCE launches dropped.
    nodep = bool(os.environ.get("PT_NODEP"))
    # Only calls whose trace emits a whole inline QMD (64 LOAD_INLINE_QMD_DATA words: the output the host
    # MME gets wrong, one word late). Other traces can be incomplete (Eglantine's JIT passes some parameters
    # straight to the output without the emit hook: macro 0x2ca's inline upload shows 3 of 18 words), so
    # those run on the host MME.
    # Complete: every inline-to-memory upload has all its data words, every inline QMD all 64 (the JIT's
    # pass-through shows as missing words); then any macro is replaced, not only the QMD ones.
    substitute = False
    QMD0, QMD16 = (names[0xB1C0].off("LOAD_INLINE_QMD_DATA", i) // 4 for i in (0, 16))
    I2M = {x: nc.off(x) // 4 for x in ("LINE_LENGTH_IN", "LINE_COUNT", "LAUNCH_DMA", "LOAD_INLINE_DATA")}
    launched_qmds = set()  # PT_NODEP: QMDs launched from memory (their dependence fields are cleared)

    def trace_complete(emits, regs):
        if sum(QMD0 <= em < QMD0 + 64 for em, _ in emits) % 64 or any(em >= 0xE00 for em, _ in emits):
            return False
        r, need = dict(regs), 0
        for em, ev in emits:
            if (em == I2M["LOAD_INLINE_DATA"]) != (need > 0):
                return False
            if need:
                need -= 1
                continue
            r[em] = ev
            if em == I2M["LAUNCH_DMA"]:
                need = -(-r.get(I2M["LINE_LENGTH_IN"], 0) * max(1, r.get(I2M["LINE_COUNT"], 1)) // 4)
        return need == 0

    def nodep_word(m, v):
        return v & ~0xF if nodep and m == QMD16 else v
    trace_ok = set()
    if traced:
        st0 = {ch: [0, 0, 0, 0] for ch in chans}
        for k, (ch, addr, *_rest, words) in enumerate(stream):
            starts = [states[ch]["posc"][int(nm[15:-1])] for sc, m, a, _ in decode(words, st0[ch])
                      if m >= 0x40 and states[ch]["engines"][sc] == 0xB1C0
                      and (nm := names[0xB1C0](m * 4) or "").startswith("CALL_MME_MACRO(")]
            if starts and len(starts) == len(traced.get(k, ())):
                trace_ok.add(k)
        print(f"PT_MME=trace: {len(trace_ok)} entries use Eglantine's compute macro output")
    trace3d_ok = set()
    if traced3d:
        st0 = {ch: [0, 0, 0, 0] for ch in chans}
        for k, (ch, addr, *_rest, words) in enumerate(stream):
            n_ = sum(1 for sc, m, a, _ in decode(words, st0[ch]) if m >= 0x40 and states[ch]["engines"][sc] == 0xB197
                     and (names[0xB197](m * 4) or "").startswith("CALL_MME_MACRO("))
            if n_ and n_ == len(traced3d.get(k, ())):
                trace3d_ok.add(k)
        print(f"PT_MME3D=trace: {len(trace3d_ok)} entries can use Eglantine's 3D macro output")
    names_of = {}
    for ch in chans:
        for sc in range(8):
            names_of[(ch, sc)] = (lambda e: (lambda m: names[e](m * 4) if m >= 0x40 else None) if e in names else (lambda m: None))(states[ch]["engines"][sc])
    st = {ch: [0, 0, 0, 0] for ch in chans}
    bound = {ch: list(states[ch]["engines"]) for ch in chans}
    import bisect
    map_starts = [m[0] for m in maps]

    def mapped(va):
        i = bisect.bisect_right(map_starts, va) - 1
        return i >= 0 and va < maps[i][0] + maps[i][2] and not maps[i][4]
    triggers3d = {n3d.off("CLEAR_SURFACE") // 4} | {m // 4 for m in range(n3d.off("CALL_MME_MACRO", 0), n3d.off("CALL_MME_MACRO", 0) + 0x100, 8)}
    carry = {ch: [] for ch in chans}  # a macro call whose parameters continue in the channel's next entry
    for k, (ch, addr, *_rest, words) in enumerate(stream):
        out, carry[ch] = carry[ch], []
        src, spliced = decode(words, st[ch]), collections.deque()  # spliced: a replaced macro's traced methods
        while True:
            if spliced:
                sc, m, a = spliced.popleft()
                from_trace = True
            elif (nxt := next(src, None)) is not None:
                sc, m, a, _ = nxt
                from_trace = False
            else:
                break
            if m < 0x40:
                hs = host_shadow[ch]
                hs[m] = a
                if m == 0:
                    raise ValueError(f"entry {k}: SET_OBJECT in the frame")
                if m in (0x70 // 4, 0x74 // 4):  # Tegra syncpoints: no such methods on a dGPU host
                    stats["drop syncpoint"] += 1
                    continue
                if m == 0x1C // 4:
                    if a & 0x1F in (1, 4, 8):  # acquire: the replay runs entries in their recorded order
                        stats["drop acquire"] += 1
                        continue
                    written.add(((hs.get(4, 0) & 0xFF) << 32 | hs.get(5, 0), 16))
                if m not in (0x08 // 4, 0x10 // 4, 0x14 // 4, 0x18 // 4, 0x1C // 4, 0x78 // 4) and not 0x28 // 4 <= m <= 0x34 // 4:
                    stats[f"drop host {m * 4:#x}"] += 1
                    continue
                out.append((sc, m, a))
                continue
            e = bound[ch][sc]
            if e not in ENGINES:
                raise ValueError(f"entry {k}: method {m * 4:#x} on unbound subchannel {sc}")
            n = names[e](m * 4)
            if n is None:
                raise ValueError(f"entry {k}: {e:#x} method {m * 4:#x} not in the class header")
            regs = shadow[(ch, e)]
            regs[m] = a
            if e == 0xB1C0 and k in trace_ok and not from_trace and n.startswith(("CALL_MME_MACRO(", "CALL_MME_DATA(")):
                if n.startswith("CALL_MME_MACRO("):
                    j = int(n[15:-1])
                    if not traced[k]:
                        raise ValueError(f"entry {k}: compute macro {j} not in mme.txt")
                    start, emits = traced[k].popleft()
                    substitute = trace_complete(emits, regs)
                    if substitute:
                        spliced.extend((sc, em, ev) for em, ev in emits)
                        stats["compute macro calls replaced"] += 1
                if substitute:
                    continue
            if e == 0xB197 and k in trace3d_ok and not from_trace and n.startswith(("CALL_MME_MACRO(", "CALL_MME_DATA(")):
                if n.startswith("CALL_MME_MACRO("):
                    start, emits = traced3d[k].popleft()
                    # MME_DMA_READ_FIFOED pulls its data from the pushbuffer: only the MME itself can issue it
                    substitute = trace_complete(emits, regs) and n3d.off("MME_DMA_READ_FIFOED") // 4 not in (em for em, _ in emits)
                    if substitute:
                        spliced.extend((sc, em, ev) for em, ev in emits)
                        stats["3D macro calls replaced"] += 1
                if substitute:
                    continue
            if drop_re and drop_re.fullmatch(n):  # bisecting: PT_DROP=<regex of method names>
                stats[f"PT_DROP {n}"] += 1
                continue
            if NEVER.fullmatch(n):
                stats[f"drop {n}"] += 1
                continue
            if (n, a) in setv:
                stats[f"PT_SETV {n}"] += 1
                a = setv[(n, a)]
            if n == "SET_REPORT_SEMAPHORE_D":
                if a & 3 in (1, 3):  # acquire, trap
                    stats["drop report acquire/trap"] += 1
                    continue
                sem = a64(e, regs, "SET_REPORT_SEMAPHORE_A", "SET_REPORT_SEMAPHORE_B")
                if not mapped(sem):  # NVN releases to GPU VA 0 (unmapped: nothing on the console; the host stalls)
                    stats["drop report release to unmapped VA"] += 1
                    continue
                written.add((sem, 32))
            if e == 0xB1C0 and nodep:
                if n == "SEND_SIGNALING_PCAS2_B":
                    if a & 0xF in (0x7, 0xC):
                        stats["drop DECREMENT/INCREMENT_DEPENDENCE"] += 1
                        continue
                    if a & 0xF:  # invalidate / schedule / prefetch a QMD in memory: patched below
                        launched_qmds.add(rmw(e, regs, "SEND_PCAS_A") << 8)
                a = nodep_word(m, a)
            if e in lm_methods and m in lm_methods[e]:
                a = lm_va >> 32 if m == lm_methods[e][0] else lm_va & 0xFFFFFFFF
            if e == 0xB197:
                if m in triggers3d:
                    note_targets(regs, k)
                elif n.startswith("LOAD_CONSTANT_BUFFER("):
                    written.add((a64(e, regs, "SET_CONSTANT_BUFFER_SELECTOR_B", "SET_CONSTANT_BUFFER_SELECTOR_C"),
                                 rmw(e, regs, "SET_CONSTANT_BUFFER_SELECTOR_A")))
            if n == "LAUNCH_DMA" and e in (0xB197, 0xB1C0, 0xA140, 0xB0B5):
                dst = a64(e, regs, "OFFSET_OUT_UPPER", "OFFSET_OUT_LOWER" if e == 0xB0B5 else "OFFSET_OUT")
                size = rmw(e, regs, "LINE_LENGTH_IN") * max(1, rmw(e, regs, "LINE_COUNT"))
                if e == 0xB0B5:
                    size = max(size, rmw(e, regs, "PITCH_OUT") * max(1, rmw(e, regs, "LINE_COUNT")),
                               rmw(e, regs, "SET_DST_WIDTH") * rmw(e, regs, "SET_DST_HEIGHT") * max(1, rmw(e, regs, "SET_DST_DEPTH")))
                written.add((dst, size + CHUNK))
            out.append((sc, m, a))
        if st[ch][2]:  # a method run continues into the next entry: keep the macro call with its data (the
            # fence between entries would cut it: MISSING_MACRO_DATA)
            nm = names_of
            i = next((i for i in range(len(out) - 1, -1, -1) if (nm[(ch, out[i][0])](out[i][1]) or "").startswith("CALL_MME_MACRO(")), None)
            if i is not None and all((nm[(ch, sc_)](m_) or "").startswith("CALL_MME_DATA(") for sc_, m_, _ in out[i + 1:]):
                out, carry[ch] = out[:i], out[i:]
                stats["macro calls carried into the next entry"] += 1
        if k in trace_ok and traced[k]:
            raise ValueError(f"entry {k}: {len(traced[k])} traced compute macro calls left over")
        if k in split:  # bisecting: one fenced sub-entry per macro call / launch, numbered k * 1000 + n
            cuts = [i for i, (sc, m, _) in enumerate(out) if i and (names_of[(ch, sc)](m) or "").startswith(("CALL_MME_MACRO", "SEND_PCAS_A"))]
            for n, (i, j) in enumerate(zip([0] + cuts, cuts + [len(out)])):
                entries.append((chans.index(ch), k * 1000 + n, encode(out[i:j])))
                print(f"split {k}.{n}: {(names_of[(ch, out[i][0])](out[i][1]) if out[i][1] >= 0x40 else 'host')} ...")
            continue
        if k in skip:  # bisecting: leave the entry out (its state changes too)
            continue
        if k in stats_at and ch == chans[0]:  # PT_STATS=<entries>: pipeline counters (+ timestamps) before/after
            sc3 = bound[ch].index(0xB197)
            at = drams[0][2] + (1 << 20) + 256 * stats_at.index(k)

            def rep(a, report, loc):
                return [(sc3, n3d.off("SET_REPORT_SEMAPHORE_A") // 4, a >> 32),
                        (sc3, n3d.off("SET_REPORT_SEMAPHORE_B") // 4, a & 0xFFFFFFFF),
                        (sc3, n3d.off("SET_REPORT_SEMAPHORE_C") // 4, 0),
                        (sc3, n3d.off("SET_REPORT_SEMAPHORE_D") // 4, 2 | loc << 12 | report << 23)]
            before, after = [], []
            for i, (report, loc) in enumerate(STATS):  # 16-byte records: counter, timestamp
                before += rep(at + 16 * i, report, loc)
                after += rep(at + 128 + 16 * i, report, loc)
            out = before + out + after
            print(f"PT_STATS: entry {k}: counters at {at:x} (before) and {at + 128:x} (after)")
        if invalidate:  # PT_INVALIDATE=1: shader instruction/data/constant caches flushed + invalidated first
            out = [(sc_, 0x21C // 4, 0x1017) for sc_, e_ in enumerate(bound[ch]) if e_ in (0xB197, 0xB1C0)] + out
        entries.append((chans.index(ch), k, encode(out)))

    for ch in chans:
        if carry[ch]:
            entries.append((chans.index(ch), len(stream), encode(carry[ch])))
    # Memory plan: real backing for device chunks with data or written by the frame, one shared zero block
    # under every other mapped page (so no access faults; writes there alias, reads of untouched pages are 0).
    chunks = data_chunks(d, os.path.join(out_dir, "chunks.txt"))
    backed = set(chunks)
    # deltas.bin (capture probe NoteCpuData): guest bytes the back read during entry k (uniform bank pages
    # the guest CPU rewrote mid-frame), applied by pt_replay right before that entry. PT_DELTAS=0: off.
    deltas = []
    if os.path.exists(os.path.join(d, "deltas.bin")) and os.environ.get("PT_DELTAS") != "0":
        raw_d = open(os.path.join(d, "deltas.bin"), "rb").read()
        o = 0
        while o + 16 <= len(raw_d):
            dk, da, dn = struct.unpack_from("<IQI", raw_d, o)
            deltas.append((dk, da, raw_d[o + 16:o + 16 + dn]))
            o += 16 + dn
        for _, da, data in deltas:
            backed.update(range(da // CHUNK, (da + len(data) - 1) // CHUNK + 1))
    import bisect
    starts = [m[0] for m in maps]
    for addr, size in written:
        i = max(0, bisect.bisect_right(starts, addr) - 1)
        while i < len(maps) and maps[i][0] < addr + size:
            gpu, dev, msize, _, sparse = maps[i]
            lo, hi = max(addr, gpu), min(addr + size, gpu + msize)
            if lo < hi and not sparse:
                backed.update(range((dev + lo - gpu) // CHUNK, (dev + hi - gpu - 1) // CHUNK + 1))
            i += 1
    # Whole mappings: a buffer with any data or write gets all of its chunks. Its other pages would alias the
    # shared zero block, which reads back whatever stray writes landed there (MKW: a light-list tail read
    # 0x7fff0000 as a light count instead of 0, and the pixel shader looped for seconds: CTX SWITCH TIMEOUT).
    whole = set()
    for gpu, dev, msize, _, sparse in maps:
        cs = range(dev // CHUNK, (dev + msize - 1) // CHUNK + 1)
        if not sparse and any(c in backed for c in cs):
            whole.update(cs)
    print(f"memory: {len(whole - backed)} more chunks to back whole mappings")
    backed |= whole
    slot = {c: i for i, c in enumerate(sorted(backed))}
    per_block = BLOCK_SIZE // CHUNK
    nblocks = -(-len(slot) // per_block)
    lines = []
    ZERO = nblocks
    LMB = nblocks + 1
    for b in range(nblocks):
        lines.append(f"block {b} {min(BLOCK_SIZE, (len(slot) - b * per_block) * CHUNK):x}")
    lines.append(f"block {ZERO} {ZERO_SIZE:x}")
    lines.append(f"zero {ZERO}")  # pt_replay PT_ZERO_RO=1 maps it read-only (stray writes fault, reads stay 0)
    lines.append(f"block {LMB} {lm_size:x}")
    for c, parts in sorted(chunks.items()):
        b, o = divmod(slot[c], per_block)
        for f, fo, co, n in parts:
            lines.append(f"fill {b} {o * CHUNK + co:x} {f} {fo:x} {n:x}")
    pieces = []
    for gpu, dev, size, kind, sparse in maps:
        if sparse:
            continue
        d0, end = dev, dev + size
        while d0 < end:
            c = d0 // CHUNK
            e = min((c + 1) * CHUNK, end)
            va = gpu + (d0 - dev)
            if c in slot:
                b, o = divmod(slot[c], per_block)
                piece = [va, e - d0, b, o * CHUNK + d0 % CHUNK]
            else:
                piece = [va, e - d0, ZERO, 0]
            p = pieces[-1] if pieces else None
            if p and p[0] + p[1] == va and p[2] == piece[2] and (
                    (piece[2] == ZERO and p[1] + piece[1] <= ZERO_SIZE) or (piece[2] != ZERO and p[3] + p[1] == piece[3])):
                p[1] += piece[1]
            else:
                pieces.append(piece)
            d0 = e
    # PTE kinds: Ada samples depth texture headers (Z16, Z24S8, ...) only from pages of the matching depth
    # kind (TEX FORMAT otherwise; color views of those pages are fine). Kinds come from the guest's own
    # requests (kinds.txt, capture probe NoteGuestKind; Turing+ numbering, the same on T239 and Ada), the
    # incompressible fallback for compressible kinds (no compression tags here). PT_KIND_RANGES=<va>:<size>:<kind>[,...]
    # (hex) overrides.
    kranges = []
    if os.path.exists(os.path.join(d, "kinds.txt")):
        for line in open(os.path.join(d, "kinds.txt")):
            ka, ks, kw, kf = (int(x, 16) for x in line.split())
            k_ = kf if 0x8 <= kw <= 0xE else kw
            if k_ not in (0, 7) and (os.environ.get("PT_KINDS") != "depth" or 1 <= k_ <= 5):  # pitch is the default; 7 = invalid; PT_KINDS=depth: depth kinds only
                kranges.append((ka, ks, k_))
    env_k = [tuple(int(x, 16) for x in r.split(":")) for r in os.environ.get("PT_KIND_RANGES", "").split(",") if r]
    kranges = sorted([r for r in kranges if not any(r[0] < ea + es and ea < r[0] + r[1] for ea, es, _ in env_k)] + env_k)
    kstarts = [r[0] for r in kranges]
    nkind = collections.Counter()
    # PT_VIDMEM_RANGES=<va>:<size>[,...] (hex): those guest pages in video memory (no CPU map, start as zero;
    # only for data the frame itself produces), to test what host-memory access costs a draw.
    vranges = [tuple(int(x, 16) for x in r.split(":")) for r in os.environ.get("PT_VIDMEM_RANGES", "").split(",") if r]
    for vi, (vva, vsz) in enumerate(vranges):
        lines.append(f"vblock {1000 + vi} {vsz:x}")
    for va, size, b, o in pieces:
        hit = next(((vi, vva) for vi, (vva, vsz) in enumerate(vranges) if vva <= va and va + size <= vva + vsz), None)
        if hit:
            lines.append(f"map {va:x} {size:x} {1000 + hit[0]} {va - hit[1]:x}")
            continue
        near = kranges[max(0, bisect.bisect_right(kstarts, va) - 1):bisect.bisect_left(kstarts, va + size)]
        near = [r for r in near if r[0] + r[1] > va]
        cuts = sorted({va, va + size} | {x for ka, ks, _ in near for x in (ka, ka + ks) if va < x < va + size})
        for lo, hi in zip(cuts, cuts[1:]):
            kind = next((k_ for ka, ks, k_ in near if ka <= lo < ka + ks), None)
            if kind is not None:
                nkind[kind] += hi - lo
            lines.append(f"map {lo:x} {hi - lo:x} {b} {o + lo - va:x}" if kind is None else
                         f"mapk {lo:x} {hi - lo:x} {b} {o + lo - va:x} {kind:x}")
    lines.append(f"map {lm_va:x} {lm_size:x} {LMB} 0")
    if nkind:
        print("PTE kinds: " + ", ".join(f"{k:#x} {v >> 20} MiB" for k, v in sorted(nkind.items())))
    # Texture header / sampler pool patches (bisecting, ASTC stopgap). Every patch edits one copy of each pool
    # (read once from the chunks' data); the copies become fill lines at the end, so patches compose.
    pool_copies = {}  # pool va -> (count, bytearray)


    def guest_read(va, size):  # guest memory at the frame's start (as replayed); unmapped, sparse, unbacked: zero
        raw = bytearray(size)
        for gpu, dev, msize, _, sparse in maps[max(0, bisect.bisect_right(map_starts, va) - 1):bisect.bisect_left(map_starts, va + size)]:
            v0, v1 = max(va, gpu), min(va + size, gpu + msize)
            if v0 >= v1 or sparse:
                continue
            da, n_ = dev + v0 - gpu, v1 - v0
            for c in range(da // CHUNK, (da + n_ - 1) // CHUNK + 1):
                for f, fo, co, n in chunks.get(c, ()):
                    lo, hi = max(c * CHUNK + co, da), min(c * CHUNK + co + n, da + n_)
                    if lo < hi:
                        with open(os.path.join(d, f), "rb") as fh:
                            fh.seek(fo + lo - (c * CHUNK + co))
                            raw[v0 - va + lo - da:v0 - va + hi - da] = fh.read(hi - lo)
        return raw

    def pool_raw(pool, count):
        if pool not in pool_copies:
            pool_copies[pool] = (count, guest_read(pool, count * 32))
        return pool_copies[pool][1]

    def pools_of(kind):
        out = set()
        for s_ in states.values():
            for e in (0xB197, 0xB1C0):
                r = s_["regs"][e]
                out.add((((r[names[e].off(f"SET_TEX_{kind}_POOL_A") // 4] & 0xFF) << 32) | r[names[e].off(f"SET_TEX_{kind}_POOL_B") // 4],
                         r[names[e].off(f"SET_TEX_{kind}_POOL_C") // 4] + 1))
        return sorted(p for p in out if p[0])

    # PT_ASTC=dummy: the host samples no ASTC (RTX 40: textureCompressionASTC_LDR false; 28% of MKW's texture
    # headers): every ASTC header in the texture header pools becomes a copy of an A8B8G8R8 header of the same
    # pool (wrong image, valid memory). shortcut: transcode ASTC (to RGBA8/BC7) and repoint the headers instead.
    # PT_ASTC=dummy[:<hex header index>]: every ASTC header becomes a copy of that A8B8G8R8 header (default: the
    # pool's first one; in MKW that is a black 4x1 texture, so ASTC-textured surfaces render black: 107 is white)
    if os.environ.get("PT_ASTC", "").startswith("dummy"):
        astc_codes = {v for k, v in TIC_FORMATS.items() if k.startswith("ASTC")}
        pick = os.environ["PT_ASTC"].partition(":")[2]
        for pool, count in pools_of("HEADER"):
            raw = pool_raw(pool, count)
            fmt = [raw[i * 32] & 0x7F for i in range(count)]
            rgba = int(pick, 16) if pick else \
                next(i for i in range(count) if fmt[i] == TIC_FORMATS["A8B8G8R8"] and any(raw[i * 32:i * 32 + 32]))
            astc = [i for i in range(count) if fmt[i] in astc_codes and any(raw[i * 32:i * 32 + 32])]
            for i in astc:
                raw[i * 32:i * 32 + 32] = raw[rgba * 32:rgba * 32 + 32]
            print(f"PT_ASTC=dummy: pool {pool:x}: {len(astc)} ASTC headers -> header {rgba:#x} (A8B8G8R8)")
    # PT_ASTC=decode: the block-linear ASTC textures the frame samples (descriptors.txt from the capture probe;
    # without it every ASTC header of the pools) are decoded with Eglantine's own decoder (libptastc.so from
    # build.sh; PT_ASTC_LIB overrides) to A8B8G8R8, all mips and layers, the sRGB bit kept, into a host arena
    # above the guest's mappings; their headers point there. Other ASTC headers become a copy of the pool's first
    # A8B8G8R8 header. Levels with no data in the capture are not decoded: 2 MiB arena chunks without data map
    # to the zero block (MKW pools hold 8192^2 x 68 layer textures, 23 GB each as RGBA8, nearly unbacked).
    # PT_ASTC=decode:<hex header>: that A8B8G8R8 header is the fallback. PT_ASTC_PNG=<dir>: level 0 of each decoded texture as PNG.
    # shortcut: decoded from the frame's start data; ASTC data the frame itself writes (copy engine uploads) is
    # not seen. Upgrade with a decode per delta, or in Phase 2 on upload.
    astc_lines = []
    if os.environ.get("PT_ASTC", "").startswith("decode"):
        import ctypes
        import numpy as np
        lib = ctypes.CDLL(os.environ.get("PT_ASTC_LIB") or os.path.join(os.path.dirname(os.path.abspath(__file__)), "libptastc.so"))
        lib.pt_astc_decode.argtypes = [ctypes.c_void_p, ctypes.c_size_t] + [ctypes.c_uint32] * 5 + [ctypes.c_void_p]
        tiles = {v: tuple(int(x) for x in k[8:].split("X")) for k, v in TIC_FORMATS.items() if k.startswith("ASTC_2D_")}
        png_dir = os.environ.get("PT_ASTC_PNG")
        if png_dir:
            from PIL import Image
            os.makedirs(png_dir, exist_ok=True)
        desc = os.path.join(d, "descriptors.txt")
        used = {int(x, 16) for x in open(desc)} if os.path.exists(desc) else None
        astc_va = -(-max(m[0] + m[2] for m in maps) >> 36) << 36
        a_size, a_in, filled = 0, 0, set()
        done, nhdr, nfall, skipped = {}, 0, 0, collections.Counter()
        arena_f = open(os.path.join(out_dir, "astc-arena.bin"), "wb")

        def tic(h, f, v=None):  # read (v None) or set one header field
            hi, lo = TIC_FIELDS[f]
            m = (1 << (hi - lo + 1)) - 1
            return h >> lo & m if v is None else h & ~(m << lo) | (v & m) << lo

        def transcode(idx, addr, tile, w, h, depth, layers, levels, block, tws):
            nonlocal a_size, a_in
            src_lv, src_stride = bl_layout(w, h, depth, levels, tile, 4, block, tws)
            dst_lv, dst_stride = bl_layout(w, h, depth, levels, (1, 1), 2, block, tws)
            if not any(m[0] <= addr < m[0] + m[2] for m in maps):
                return None
            al = 9 + block[1] + block[2]
            base = a_size = -(-a_size >> al) << al
            a_size += dst_stride * layers
            for layer in range(layers):
                for lv in range(levels):
                    lw, lh, ld = max(w >> lv, 1), max(h >> lv, 1), max(depth >> lv, 1)
                    so, bh, bd, gx, ssz = src_lv[lv]
                    sva = addr + layer * src_stride + so
                    src = np.frombuffer(guest_read(sva, ssz), np.uint8)
                    if not src.any():  # no data in the capture: stays a hole (zero)
                        continue
                    nbx, nby = -(-lw // tile[0]), -(-lh // tile[1])
                    blocks = np.ascontiguousarray(src.view(np.uint64).reshape(-1, 2)[bl_runs(nbx * 16, nby, ld, bh, bd, gx) // 16])
                    zero = ~blocks.any(1)  # sparse textures: tiles not resident read zero, kept zero (not decoded)
                    a_in += int(blocks.size * 8 - zero.sum() * 16)
                    pix = np.empty(lw * lh * ld * 4, np.uint8)
                    lib.pt_astc_decode(blocks.ctypes.data, blocks.nbytes, lw, lh, ld, *tile, pix.ctypes.data)
                    if zero.any():
                        zm = np.repeat(np.repeat(zero.reshape(ld, nby, nbx), tile[1], 1), tile[0], 2)[:, :lh, :lw]
                        pix.reshape(ld, lh, lw, 4)[zm] = 0
                    if png_dir and layer == 0 and lv == 0:
                        Image.fromarray(pix.reshape(ld * lh, lw, 4)[:lh]).save(os.path.join(png_dir, f"{idx:x}-{addr:x}.png"))
                    rb = -(-lw * 4 // 16) * 16  # whole 16-byte runs: pad each row
                    pix = np.pad(pix.reshape(ld * lh, lw * 4), ((0, 0), (0, rb - lw * 4)))
                    do, bh, bd, gx, dsz = dst_lv[lv]
                    out = np.zeros((dsz // 16, 2), np.uint64)
                    out[bl_runs(rb, lh, ld, bh, bd, gx) // 16] = pix.reshape(-1, 16).view(np.uint64)
                    o, buf = base + layer * dst_stride + do, out.view(np.uint8).reshape(-1)
                    for c in range(o // CHUNK, (o + dsz - 1) // CHUNK + 1):  # only chunks with data
                        lo, hi = max(c * CHUNK, o), min((c + 1) * CHUNK, o + dsz)
                        if buf[lo - o:hi - o].any():
                            arena_f.seek(lo)
                            arena_f.write(buf[lo - o:hi - o].tobytes())
                            filled.add(c)
            return astc_va + base

        for pool, count in pools_of("HEADER"):
            raw = pool_raw(pool, count)
            fmt = [raw[i * 32] & 0x7F for i in range(count)]
            fallback = int(os.environ["PT_ASTC"][7:], 16) if os.environ["PT_ASTC"][7:] else \
                next(i for i in range(count) if fmt[i] == TIC_FORMATS["A8B8G8R8"] and any(raw[i * 32:i * 32 + 32]))
            for i in range(count):
                tile = tiles.get(fmt[i])
                if not tile:
                    continue
                h = int.from_bytes(raw[i * 32:i * 32 + 32], "little")
                tt = tic(h, "TEXTURE_TYPE")
                if used is not None and pool + i * 32 not in used or tic(h, "HEADER_VERSION") != 3 or tt > 8:
                    if used is None or pool + i * 32 in used:  # pitch or 1D buffer ASTC, HTEX
                        skipped[f"header version {tic(h, 'HEADER_VERSION')} type {tt}"] += 1
                    raw[i * 32:i * 32 + 32] = raw[fallback * 32:fallback * 32 + 32]
                    nfall += 1
                    continue
                dd = (tic(h, "DEPTH_MINUS_ONE") | tic(h, "DEPTH_MINUS_ONE_BIT14") << 14) + 1
                depth, layers = (dd, 1) if tt == 2 else (1, {3: 6, 8: 6 * dd}.get(tt, dd))
                block = tuple(tic(h, f"GOBS_PER_BLOCK_{a}") for a in ("WIDTH", "HEIGHT", "DEPTH"))
                addr = tic(h, "ADDRESS_BITS31TO9") << 9 | tic(h, "ADDRESS_BITS48TO32") << 32
                key = (addr, fmt[i], tic(h, "WIDTH_MINUS_ONE") + 1,
                       (tic(h, "HEIGHT_MINUS_ONE") | tic(h, "HEIGHT_MINUS_ONE_BIT16") << 16) + 1, depth, layers,
                       tic(h, "MAX_MIP_LEVEL") + 1, block, tic(h, "TILE_WIDTH_IN_GOBS"))
                if key not in done:
                    done[key] = transcode(i, addr, tile, *key[2:])
                if done[key] is None:
                    skipped["unmapped"] += 1
                    raw[i * 32:i * 32 + 32] = raw[fallback * 32:fallback * 32 + 32]
                    continue
                h = tic(h, "COMPONENTS", TIC_FORMATS["A8B8G8R8"])
                h = tic(tic(h, "ADDRESS_BITS31TO9", done[key] >> 9), "ADDRESS_BITS48TO32", done[key] >> 32)
                raw[i * 32:i * 32 + 32] = h.to_bytes(32, "little")
                nhdr += 1
        arena_f.truncate(-(-a_size // CHUNK) * CHUNK)
        arena_f.close()
        assert astc_va + a_size <= HOST_BASE, "ASTC arena runs into the host buffers"
        # chunks with data packed into blocks (ids 2000+), the rest of the arena maps to the zero block
        per = BLOCK_SIZE // CHUNK
        fl = sorted(filled)
        for bi in range(0, len(fl), per):
            astc_lines.append(f"block {2000 + bi // per} {min(per, len(fl) - bi) * CHUNK:x}")
        aslot = {c: k for k, c in enumerate(fl)}
        run = None  # [va, size, block, offset]
        for c in range(-(-a_size // CHUNK)):
            if c in aslot:
                b_, o_ = 2000 + aslot[c] // per, aslot[c] % per * CHUNK
                astc_lines.append(f"fill {b_} {o_:x} replay/astc-arena.bin {c * CHUNK:x} {CHUNK:x}")
            else:
                b_, o_ = ZERO, 0
            if run and run[2] == b_ and (run[1] + CHUNK <= ZERO_SIZE if b_ == ZERO else run[3] + run[1] == o_):
                run[1] += CHUNK
            else:
                if run:
                    astc_lines.append("map {:x} {:x} {} {:x}".format(*run))
                run = [astc_va + c * CHUNK, CHUNK, b_, o_]
        if run:
            astc_lines.append("map {:x} {:x} {} {:x}".format(*run))
        print(f"PT_ASTC=decode: {nhdr} headers ({'all' if used is None else 'sampled'}), {len(done)} textures: {a_in >> 20} MiB "
              f"ASTC -> {len(fl) * 2} MiB A8B8G8R8 at {astc_va:x} ({a_size >> 20} MiB of VA); {nfall} headers -> "
              f"fallback A8B8G8R8 header; left alone: {dict(skipped)}")
    # PT_TSC_NODC=<hex indices|all>: clear DEPTH_COMPARE (TEXSAMP0 bit 9) in those samplers.
    nodc = os.environ.get("PT_TSC_NODC")
    if nodc:
        for pool, count in pools_of("SAMPLER"):
            raw = pool_raw(pool, count)
            n = 0
            for i in (range(count) if nodc == "all" else [int(x, 16) for x in nodc.split(",")]):
                w0 = struct.unpack_from("<I", raw, i * 32)[0]
                if w0 & 0x200:
                    struct.pack_into("<I", raw, i * 32, w0 & ~0x200)
                    n += 1
            print(f"PT_TSC_NODC: pool {pool:x}: depth compare cleared in {n} samplers")
    # PT_TIC_COPY=<dst>:<src>[,...] (hex): texture header dst becomes a copy of src (bisecting header issues).
    tic_copy = [tuple(int(x, 16) for x in p.split(":")) for p in os.environ.get("PT_TIC_COPY", "").split(",") if p]
    # PT_TIC_SET=<idx>:<FIELD>=<value>[,...] (hex idx/value; FIELD as in clc797tex.h NVC797_TEXHEAD_BL_<FIELD>):
    # set one field of a texture header, e.g. PT_TIC_SET=72b4:RESERVED_ADDRESS=0.
    tic_set = [(int(i, 16), f, int(v, 16)) for i, f, v in
               (re.fullmatch(r"(\w+):(\w+)=(\w+)", p).groups() for p in os.environ.get("PT_TIC_SET", "").split(",") if p)]
    if tic_copy or tic_set:
        for pool, count in pools_of("HEADER"):
            raw = pool_raw(pool, count)
            for dst, src in tic_copy:
                raw[dst * 32:dst * 32 + 32] = raw[src * 32:src * 32 + 32]
            for i, f, v in tic_set:
                hi, lo = TIC_FIELDS[f]
                h = int.from_bytes(raw[i * 32:i * 32 + 32], "little")
                h = h & ~(((1 << (hi - lo + 1)) - 1) << lo) | (v << lo)
                raw[i * 32:i * 32 + 32] = h.to_bytes(32, "little")
            print(f"PT_TIC_COPY/SET: pool {pool:x}: {len(tic_copy)} copies, {len(tic_set)} fields")
    # PT_PATCH=<va>:<hex bytes>[,...]: overwrite guest memory in the replay (bisecting, e.g. EXIT at a shader's start).
    for p_ in os.environ.get("PT_PATCH", "").split(","):
        if p_:
            pva, hexb = p_.split(":")
            pva, data = int(pva, 16), bytes.fromhex(hexb)
            base = pva & ~31
            raw = pool_raw(base, -(-(pva + len(data) - base) // 32))
            raw[pva - base:pva - base + len(data)] = data
            print(f"PT_PATCH: {len(data)} bytes at {pva:x}")
    # PT_HELPER_LOADS=1: on Ada, a pixel shader's helper lanes (coverage 0, run for derivatives) do not perform
    # global loads: LDG leaves the destination unchanged. Guest code like `LDG R5, [R5.U32+UR4]` then keeps the
    # address as the loaded value (MKW racing: a light count read this way became the address, a loop of
    # ~10^9 iterations per helper lane, 120 ms draws, Xid 109); other destinations keep whatever an earlier warp
    # left in the register (the same draw then hangs in some runs only). Each LDG jumps
    # to a trampoline: the LDG, PIXLD.COVMASK into a predicate the program does not use, helper lanes zero the
    # destination, BRA back. shortcut: helper lanes zero the destination even when the LDG's own guard was false,
    # and wait for the load right away (no latency hiding); LDG only (no LD/LDS/ATOM). Upgrade when a game needs it.
    arena = bytearray()
    if os.environ.get("PT_HELPER_LOADS"):
        import shutil, subprocess, tempfile
        arena_va = lm_va + lm_size + len(chans) * CHUNK
        nvdisasm = os.environ.get("NVDISASM") or shutil.which("nvdisasm")
        assert nvdisasm, "PT_HELPER_LOADS needs nvdisasm (PATH or NVDISASM)"
        # bisecting: PT_HELPER_SKIP=<program>:<code offset>+<code offset>[,...] leaves those loads alone
        helper_skip = {(int(pr, 16), int(o, 16)) for pr, _, offs in (x.partition(":") for x in
                       os.environ.get("PT_HELPER_SKIP", "").split(",") if x) for o in offs.split("+")}
        ldg_re = re.compile(r"/\*([0-9a-f]+)\*/\s+(?:@!?P[0-6T]\s+)?LDG(\S*)\s+R(\d+),")
        nprog = nsite = nver = 0

        def patch(prog, img):  # img: unpatched memory from prog on -> the patched program (to its end), or None
            nonlocal arena, nprog, nsite
            end = next((o for o in range(0x80, len(img) - 16, 16)
                        if img[o] == 0x4D and img[o + 1] & 0xF == 9 and int.from_bytes(img[o + 16:o + 32], "little") & ~SASS_CTL
                        == sass_bra(-0x10) & ~SASS_CTL), None)  # EXIT; BRA to itself
            if end is None:
                print(f"PT_HELPER_LOADS: {prog:x}: no program end found, left alone")
                return None
            raw = bytearray(img[:-(-(end + 0x20) // 32) * 32])
            with tempfile.NamedTemporaryFile(suffix=".bin") as f:
                f.write(raw[0x80:end + 0x20])
                f.flush()
                res = subprocess.run([nvdisasm, "-b", "SM86", f.name], capture_output=True, text=True)
            if res.returncode:
                print(f"PT_HELPER_LOADS: {prog:x}: nvdisasm: {res.stderr.strip().splitlines()[:1]}, left alone")
                return None
            text = res.stdout
            free = [] if re.search(r"\bPR\b", text) else [p for p in range(6, -1, -1) if not re.search(rf"\bP{p}\b", text)]
            sites = [(int(mo[1], 16) + 0x80, int(mo[3]), 4 if ".128" in mo[2] else 2 if ".64" in mo[2] else 1)
                     for mo in ldg_re.finditer(text) if (prog, int(mo[1], 16)) not in helper_skip]
            if sites and not free:
                print(f"PT_HELPER_LOADS: {prog:x}: all predicates used, {len(sites)} loads left alone")
                return None
            nprog += bool(sites)
            for off, rd, n in sites:
                x = int.from_bytes(raw[off:off + 16], "little")
                wr = x >> 110 & 7
                x &= ~(0xF << 122)  # no operand reuse across the jump
                t_va, site_va = arena_va + len(arena), prog + off
                tramp = [x, sass_pixld_covmask(free[0], wr)] + [sass_mov_rz_unless(free[0], rd + i, 1 << wr) for i in range(n)]
                tramp.append(sass_bra(site_va + 16 - (t_va + 16 * len(tramp) + 16)))
                arena += b"".join(w.to_bytes(16, "little") for w in tramp)
                raw[off:off + 16] = sass_bra(t_va - (site_va + 16)).to_bytes(16, "little")
                nsite += 1
            return raw
        for prog in sorted(pixel_programs(d, states, stream, n3d)):
            m_ = next((m for m in maps if m[0] <= prog < m[0] + m[2]), None)
            if not prog or m_ is None or prog in pool_copies:
                continue
            img = bytearray(pool_raw(prog, min(1 << 15, (m_[0] + m_[2] - prog) // 32)))
            del pool_copies[prog]
            raw = patch(prog, img)
            if raw is not None:
                pool_copies[prog] = (len(raw) // 32, raw)
            # Programs the CPU (re)writes during the frame: each version is patched at the entry that writes it
            # (an extra delta after that entry's own).
            dev = m_[1] + prog - m_[0]
            by_entry = collections.defaultdict(list)
            for dk, da, data in deltas:
                if da < dev + len(img) and dev < da + len(data):
                    by_entry[dk].append((da, data))
            for dk in sorted(by_entry):
                for da, data in by_entry[dk]:
                    lo, hi = max(da, dev), min(da + len(data), dev + len(img))
                    img[lo - dev:hi - dev] = data[lo - da:hi - da]
                raw = patch(prog, img)
                if raw is not None:
                    deltas.append((dk, dev, bytes(raw)))
                    nver += 1
        assert len(arena) + 0x100 <= CHUNK, "trampoline arena overflow"
        print(f"PT_HELPER_LOADS: {nsite} loads in {nprog} pixel programs ({nver} rewritten mid-frame) go through trampolines at {arena_va:x}")
        if arena:
            arena += sass_bra(-0x10).to_bytes(16, "little") * 16  # instruction prefetch past the last trampoline
            open(os.path.join(out_dir, "helper-arena.bin"), "wb").write(arena)
            ab = LMB + 1 + len(chans)
            lines += [f"block {ab} {CHUNK:x}", f"fill {ab} 0 replay/helper-arena.bin 0 {len(arena):x}", f"map {arena_va:x} {CHUNK:x} {ab} 0"]
    lines += astc_lines  # last: guest blocks and the helper arena get video memory first
    # PT_NODEP: QMDs launched from memory (NVN's barrier ring) wait for no dependence and signal none.
    patched = 0
    for qva in sorted(launched_qmds):
        m_ = next((m for m in maps if m[0] <= qva < m[0] + m[2] and not m[4]), None)
        if m_ is None or any(c not in slot for c in range((m_[1] + qva - m_[0]) // CHUNK, (m_[1] + qva - m_[0] + 255) // CHUNK + 1)):
            continue  # unbacked: reads zero, nothing to clear
        raw = pool_raw(qva, 8)
        w4, w16 = struct.unpack_from("<I", raw, 16)[0], struct.unpack_from("<I", raw, 64)[0]
        struct.pack_into("<I", raw, 16, w4 & ~(0xFFFF << 14))  # DEPENDENCE_COUNTER (157:142)
        struct.pack_into("<I", raw, 64, w16 & ~((1 << 0) | (1 << 5)))  # DEPENDENT_QMD0/1_ENABLE (512, 517)
        patched += 1
    if launched_qmds:
        print(f"PT_NODEP: dependence cleared in {patched} of {len(launched_qmds)} QMDs launched from memory")
    for pool, (count, raw) in sorted(pool_copies.items()):
        gpu, dev, _, _, _ = next(m for m in maps if m[0] <= pool < m[0] + m[2])
        da = dev + pool - gpu
        path = f"replay/pool-{pool:x}.bin"
        open(os.path.join(d, path), "wb").write(raw)
        for c in range(da // CHUNK, (da + count * 32 - 1) // CHUNK + 1):
            lo, hi = max(c * CHUNK, da), min((c + 1) * CHUNK, da + count * 32)
            b, o = divmod(slot[c], per_block)
            lines.append(f"fill {b} {o * CHUNK + lo % CHUNK:x} {path} {lo - da:x} {hi - lo:x}")
    for idx, ch, va in drams:
        lines += [f"block {LMB + 1 + idx} {CHUNK:x}", f"fill {LMB + 1 + idx} 0 replay/dram-{ch}.bin 0 3000",
                  f"map {va:x} {CHUNK:x} {LMB + 1 + idx} 0"]
    for idx, ch in enumerate(chans):
        lines.append(f"channel {idx} " + " ".join(f"{ENGINES[e][1] if e in ENGINES else 0:x}" for e in states[ch]["engines"]))
    open(os.path.join(out_dir, "plan.txt"), "w").write("\n".join(lines) + "\n")
    with open(os.path.join(out_dir, "deltas.bin"), "wb") as f:  # u32 push entry key, u32 block, u64 offset, u32 size, bytes
        n_d = 0
        for dk, da, data in deltas:
            key = dk * 1000 if dk in split else dk
            for c in range(da // CHUNK, (da + len(data) - 1) // CHUNK + 1):  # split at chunk edges
                lo, hi = max(da, c * CHUNK), min(da + len(data), (c + 1) * CHUNK)
                b, so = divmod(slot[c], per_block)
                f.write(struct.pack("<IIQI", key & 0xFFFFFFFF, b, so * CHUNK + lo % CHUNK, hi - lo) + data[lo - da:hi - da])
                n_d += hi - lo
    if deltas:
        print(f"deltas: {len(deltas)} records, {n_d >> 10} KiB of mid-frame guest data")
    with open(os.path.join(out_dir, "targets.txt"), "w") as f:
        f.write("# address width height format memory first_entry last_entry (color targets drawn to)\n")
        for addr, (w, h, fmt, mem, k0, k1) in sorted(targets.items(), key=lambda t: t[1][5]):
            f.write(f"{addr:x} {w} {h} {fmt:x} {mem:x} {k0} {k1}\n")
    with open(os.path.join(out_dir, "push.bin"), "wb") as f:
        for idx, k, words in entries:
            f.write(struct.pack(f"<IiI{len(words)}I", idx, k, len(words), *words))
    print(f"{len(entries)} entries ({len(chans)} restore), {sum(len(w) for *_, w in entries)} words; "
          f"restored regs {dict((hex(k), v) for k, v in stats.items() if isinstance(k, int))}")
    print("rewrites:", {k: v for k, v in stats.items() if isinstance(k, str)})
    print(f"memory: {len(chunks)} data chunks, {len(backed) - len(set(chunks) & backed)} more written by the frame, "
          f"{len(slot) * 2} MiB backed in {nblocks} blocks, {len(pieces)} mappings; local memory {lm_size >> 20} MiB "
          f"({per_tpc:#x} per TPC)")


def dump(d, first, count="1"):
    states, stream = read_states(d), read_stream(d)
    names = {e: Names(h) for e, (h, _) in ENGINES.items()}
    host = Names(HOST)
    st = {ch: [0, 0, 0, 0] for ch in states}
    bound = {ch: list(states[ch]["engines"]) for ch in states}
    first, count = int(first), int(count)
    for k, (ch, addr, *_rest, words) in enumerate(stream[:first + count]):
        for sc, m, a, _ in decode(words, st[ch]):
            if k >= first:
                e = bound[ch][sc] if m >= 0x40 else None
                n = host(m * 4) if e is None else names[e](m * 4) if e in names else None
                print(f"{k} ch{ch} sc{sc} {e or 0:#06x} {m * 4:#06x} {n} = {a:#x}")


def peek(d, va, size="0x100"):
    """Snapshot bytes at a guest GPU VA, as u32 words."""
    import bisect
    va, size = int(va, 16), int(size, 0)
    m = next(x for x in read_maps(d) if x[0] <= va < x[0] + x[2])
    da = m[1] + va - m[0]
    files = sorted((int(f[4:-4], 16), f) for f in os.listdir(d) if f.startswith("dev-"))
    base, f = files[bisect.bisect_right([b for b, _ in files], da) - 1]
    with open(os.path.join(d, f), "rb") as fh:
        fh.seek(da - base)
        words = struct.unpack(f"<{size // 4}I", fh.read(size))
    for i in range(0, len(words), 8):
        print(f"{va + i * 4:#x}: " + " ".join(f"{w:08x}" for w in words[i:i + 8]))


def qmds(d, entry=None, host=None):
    """Inline QMDs Eglantine wrote (qmds.bin); with an entry and a host readback of one QMD, the words
    that differ from the last QMD Eglantine wrote at that address in that entry."""
    b = open(os.path.join(d, "qmds.bin"), "rb").read()
    rec = [(struct.unpack_from("<IQ", b, o), struct.unpack_from("<64I", b, o + 12)) for o in range(0, len(b), 268)]
    if host is None:
        for (k, addr), _ in rec:
            if entry is None or k == int(entry):
                print(k, hex(addr))
        return
    want = open(host, "rb").read(256)
    hw = struct.unpack("<64I", want)
    hits = [(addr, w) for (k, addr), w in rec if k == int(entry)]
    for addr, w in hits:
        diff = [i for i in range(64) if w[i] != hw[i]]
        print(f"{addr:#x}: {len(diff)} words differ from the host's: {diff}")
    if hits:
        addr, w = hits[-1]
        for i in range(0, 64, 8):
            print(f"  eglantine w{i:2}: " + " ".join(f"{x:08x}" for x in w[i:i + 8]))
            print(f"  host      w{i:2}: " + " ".join(f"{x:08x}" for x in hw[i:i + 8]))


BPP = {0xD5: 4, 0xD6: 4, 0xD7: 4, 0xCF: 4, 0xD0: 4, 0xD1: 4, 0xE0: 4, 0xCA: 8, 0xC6: 8, 0xF2: 2}


def size_of(w, h, fmt, mem):
    bpp, bh = BPP[fmt], 1 << ((mem >> 4) & 0xF)
    if mem & 0x1000:  # pitch: width is the pitch in bytes
        return w * h
    return -(-w * bpp // 64) * -(-h // (8 * bh)) * 512 * bh


def image(path, w, h, fmt, mem, out):
    """Color target bytes (as read back) -> PNG. Block linear (bl_runs), block height from
    SET_COLOR_TARGET_MEMORY. Float formats are clamped to [0, 1]."""
    import numpy as np
    from PIL import Image
    w, h, fmt, mem = int(w), int(h), int(fmt, 16), int(mem, 16)
    bpp, bh = BPP[fmt], 1 << ((mem >> 4) & 0xF)
    raw = np.fromfile(path, dtype=np.uint8)
    if mem & 0x1000:
        px = raw[: w * h].reshape(h, w // bpp, bpp)
    else:
        assert w * bpp % 16 == 0, "row not a whole number of 16-byte runs"
        runs = bl_runs(w * bpp, h, 1, (mem >> 4) & 0xF, 0, -(-w * bpp // 64)) // 16
        px = raw[:runs.max() * 16 + 16].view(np.uint64).reshape(-1, 2)[runs].view(np.uint8).reshape(h, w, bpp)
    if fmt in (0xD5, 0xD6):
        rgb = px[..., :3]
    elif fmt == 0xD7:  # signed normalized: 0 shows as mid grey
        rgb = (px[..., :3].view(np.int8).astype(np.int16) + 128).astype(np.uint8)
    elif fmt == 0xF2:  # one float16 channel, as grey
        v = np.clip(px.copy().view(np.float16)[..., 0].astype(np.float32), 0, 1)
        rgb = np.repeat((v ** (1 / 2.2) * 255).astype(np.uint8)[..., None], 3, -1)
    elif fmt in (0xCF, 0xD0):
        rgb = px[..., 2::-1]
    else:
        v = px.copy().view(np.uint32 if bpp == 4 else np.uint64)[..., 0]
        if fmt == 0xD1:
            rgb = np.stack([(v >> s) & 0x3FF for s in (0, 10, 20)], -1) / 1023.0
        elif fmt == 0xE0:
            def f11(bits, mbits):  # unsigned small float, 5-bit exponent
                e, m = (bits >> mbits) & 0x1F, bits & ((1 << mbits) - 1)
                return np.where(e == 0, m / (1 << mbits) * 2.0 ** -14, (1 + m / (1 << mbits)) * 2.0 ** (e.astype(np.int32) - 15))
            rgb = np.stack([f11(v & 0x7FF, 6), f11((v >> 11) & 0x7FF, 6), f11((v >> 22) & 0x3FF, 5)], -1)
        else:  # 16-bit channels
            ch = px.copy().view(np.float16 if fmt == 0xCA else np.uint16).astype(np.float32)[..., :3]
            rgb = ch if fmt == 0xCA else ch / 65535.0
        rgb = (np.clip(rgb, 0, 1) ** (1 / 2.2) * 255).astype(np.uint8)
    Image.fromarray(np.ascontiguousarray(rgb)).save(out)
    print(f"{out}: {w}x{h}")


def selftest():
    """Block-linear layout helpers (PT_ASTC=decode) against hand-checked sizes and the readback layout of image()."""
    import numpy as np
    assert bl_layout(256, 256, 1, 1, (1, 1), 2, (0, 4, 0), 0) == ([(0, 4, 0, 16, 256 * 256 * 4)], 256 * 256 * 4)
    assert bl_layout(64, 64, 1, 1, (8, 8), 4, (0, 0, 0), 0) == ([(0, 0, 0, 2, 1024)], 1024)  # ASTC 8x8: 8x8 blocks of 16 B
    # one GOB, runs in (row, 16-byte column) order: native = legacy with offset bits 5 and 6 exchanged
    assert list(bl_runs(64, 4, 1, 0, 0, 1)) == [0, 64, 256, 320, 16, 80, 272, 336, 32, 96, 288, 352, 48, 112, 304, 368]
    assert sorted(bl_runs(256, 64, 1, 2, 0, 4)) == list(range(0, 256 * 64, 16))  # a bijection onto the level
    print("selftest: ok")


if __name__ == "__main__":
    {"stats": stats, "build": build, "selftest": selftest, "dump": dump, "image": image, "peek": peek, "qmds": qmds}[sys.argv[1]](*sys.argv[2:])
