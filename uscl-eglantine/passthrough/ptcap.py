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

CHUNK = 1 << 21
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
    # PT_MME=trace: compute macro calls become the methods Eglantine's MME emitted for them (mme.txt), so
    # the host MME does not run compute macros (Eglantine's output is what the game ran).
    traced = collections.defaultdict(collections.deque)  # entry -> deque of (start, [(method, value)])
    if os.environ.get("PT_MME") == "trace":
        for line in open(os.path.join(d, "mme.txt")):
            k_, kind, a_, v_ = line.split()
            if kind == "C":
                traced[int(k_)].append((int(a_, 16), []))
            elif kind == "E":
                traced[int(k_)][-1][1].append((int(a_, 16), int(v_, 16)))
    # Only entries whose traced calls match the stream's (start addresses, in order): on the compute-only
    # channels the front's macro positions differ from the captured state, so those keep the host MME.
    # PT_NODEP=1: no QMD dependencies. NVN counts finished compute work into a ring of barrier QMDs (each
    # QMD decrements one, SEND_SIGNALING_PCAS2_B INCREMENT_DEPENDENCE raises it); the ring's counters come
    # from Eglantine's scheduler, not the hardware. Entries run serialized, so the barriers (releases whose
    # waits are dropped anyway) are left out: DEPENDENT_QMD0_ENABLE/ACTION (QMD word 16, bits 0-3) cleared
    # in inline QMDs, INCREMENT_DEPENDENCE launches dropped.
    nodep = bool(os.environ.get("PT_NODEP"))
    # Only macros whose trace is complete: Eglantine's JIT passes some parameters straight to the output
    # without the emit hook (macro 0x2ca's inline upload shows 3 of 18 words), so those run on the host MME.
    mme_starts = {int(x, 16) for x in os.environ.get("PT_MME_STARTS", "281").split(",") if x}
    substitute = False
    QMD16 = names[0xB1C0].off("LOAD_INLINE_QMD_DATA", 16) // 4

    def nodep_word(m, v):
        return v & ~0xF if nodep and m == QMD16 else v
    trace_ok = set()
    if traced:
        st0 = {ch: [0, 0, 0, 0] for ch in chans}
        for k, (ch, addr, *_rest, words) in enumerate(stream):
            starts = [states[ch]["posc"][int(nm[15:-1])] for sc, m, a, _ in decode(words, st0[ch])
                      if m >= 0x40 and states[ch]["engines"][sc] == 0xB1C0
                      and (nm := names[0xB1C0](m * 4) or "").startswith("CALL_MME_MACRO(")]
            if starts and starts == [c[0] for c in traced.get(k, ())]:
                trace_ok.add(k)
        print(f"PT_MME=trace: {len(trace_ok)} entries use Eglantine's compute macro output")
    names_of = {}
    for ch in chans:
        for sc in range(8):
            names_of[(ch, sc)] = (lambda e: (lambda m: names[e](m * 4) if m >= 0x40 else None) if e in names else (lambda m: None))(states[ch]["engines"][sc])
    st = {ch: [0, 0, 0, 0] for ch in chans}
    bound = {ch: list(states[ch]["engines"]) for ch in chans}
    triggers3d = {n3d.off("CLEAR_SURFACE") // 4} | {m // 4 for m in range(n3d.off("CALL_MME_MACRO", 0), n3d.off("CALL_MME_MACRO", 0) + 0x100, 8)}
    for k, (ch, addr, *_rest, words) in enumerate(stream):
        out = []
        for sc, m, a, _ in decode(words, st[ch]):
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
            if e == 0xB1C0 and k in trace_ok and n.startswith(("CALL_MME_MACRO(", "CALL_MME_DATA(")):
                if n.startswith("CALL_MME_MACRO("):
                    j = int(n[15:-1])
                    if not traced[k]:
                        raise ValueError(f"entry {k}: compute macro {j} not in mme.txt")
                    start, emits = traced[k].popleft()
                    if start != states[ch]["posc"][j]:
                        raise ValueError(f"entry {k}: macro {j} starts at {states[ch]['posc'][j]:#x}, mme.txt has {start:#x}")
                    substitute = start in mme_starts
                    if substitute:
                        out += [(sc, em, nodep_word(em, ev)) for em, ev in emits]
                        stats["compute macro calls replaced"] += 1
                if substitute:
                    continue
            if NEVER.fullmatch(n):
                stats[f"drop {n}"] += 1
                continue
            if n == "SET_REPORT_SEMAPHORE_D":
                if a & 3 in (1, 3):  # acquire, trap
                    stats["drop report acquire/trap"] += 1
                    continue
                written.add((a64(e, regs, "SET_REPORT_SEMAPHORE_A", "SET_REPORT_SEMAPHORE_B"), 32))
            if e == 0xB1C0 and nodep:
                if n == "SEND_SIGNALING_PCAS2_B" and a & 0xF == 0xC:
                    stats["drop INCREMENT_DEPENDENCE"] += 1
                    continue
                a = nodep_word(m, a)
            if e in lm_methods and m in lm_methods[e]:
                a = lm_va >> 32 if m == lm_methods[e][0] else lm_va & 0xFFFFFFFF
            if e == 0xB197:
                if m in triggers3d:
                    note_targets(regs, k)
                elif n.startswith("LOAD_CONSTANT_BUFFER("):
                    written.add((a64(e, regs, "SET_CONSTANT_BUFFER_SELECTOR_B", "SET_CONSTANT_BUFFER_SELECTOR_C"),
                                 rmw(e, regs, "SET_CONSTANT_BUFFER_SELECTOR_A")))
            if n == "LAUNCH_DMA" and e in (0xB197, 0xA140, 0xB0B5):
                dst = a64(e, regs, "OFFSET_OUT_UPPER", "OFFSET_OUT_LOWER" if e == 0xB0B5 else "OFFSET_OUT")
                size = rmw(e, regs, "LINE_LENGTH_IN") * max(1, rmw(e, regs, "LINE_COUNT"))
                if e == 0xB0B5:
                    size = max(size, rmw(e, regs, "PITCH_OUT") * max(1, rmw(e, regs, "LINE_COUNT")),
                               rmw(e, regs, "SET_DST_WIDTH") * rmw(e, regs, "SET_DST_HEIGHT") * max(1, rmw(e, regs, "SET_DST_DEPTH")))
                written.add((dst, size + CHUNK))
            out.append((sc, m, a))
        if k in trace_ok and traced[k]:
            raise ValueError(f"entry {k}: {len(traced[k])} traced compute macro calls left over")
        if k in split:  # bisecting: one fenced sub-entry per macro call / launch, numbered k * 1000 + n
            cuts = [i for i, (sc, m, _) in enumerate(out) if i and (names_of[(ch, sc)](m) or "").startswith(("CALL_MME_MACRO", "SEND_PCAS_A"))]
            for n, (i, j) in enumerate(zip([0] + cuts, cuts + [len(out)])):
                entries.append((chans.index(ch), k * 1000 + n, encode(out[i:j])))
                print(f"split {k}.{n}: {(names_of[(ch, out[i][0])](out[i][1]) if out[i][1] >= 0x40 else 'host')} ...")
            continue
        entries.append((chans.index(ch), k, encode(out)))

    # Memory plan: real backing for device chunks with data or written by the frame, one shared zero block
    # under every other mapped page (so no access faults; writes there alias, reads of untouched pages are 0).
    chunks = data_chunks(d, os.path.join(out_dir, "chunks.txt"))
    backed = set(chunks)
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
    slot = {c: i for i, c in enumerate(sorted(backed))}
    per_block = BLOCK_SIZE // CHUNK
    nblocks = -(-len(slot) // per_block)
    lines = []
    ZERO = nblocks
    LMB = nblocks + 1
    for b in range(nblocks):
        lines.append(f"block {b} {min(BLOCK_SIZE, (len(slot) - b * per_block) * CHUNK):x}")
    lines.append(f"block {ZERO} {ZERO_SIZE:x}")
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
    for va, size, b, o in pieces:
        lines.append(f"map {va:x} {size:x} {b} {o:x}")
    lines.append(f"map {lm_va:x} {lm_size:x} {LMB} 0")
    # PT_ASTC=dummy: the host samples no ASTC (RTX 40: textureCompressionASTC_LDR false; 28% of MKW's texture
    # headers): every ASTC header in the texture header pools becomes a copy of an A8B8G8R8 header of the same
    # pool (wrong image, valid memory). shortcut: transcode ASTC (to RGBA8/BC7) and repoint the headers instead.
    def patch_pool(pool, count, fix, tag):
        """Read a 32-byte-entry pool from the chunks' data, let fix(raw) edit it, add fill lines over it."""
        gpu, dev, msize, _, _ = next(m for m in maps if m[0] <= pool < m[0] + m[2])
        assert pool + count * 32 <= gpu + msize, "pool spans mappings"
        da = dev + pool - gpu
        raw = bytearray(count * 32)
        for c in range(da // CHUNK, (da + count * 32 - 1) // CHUNK + 1):
            for f, fo, co, n in chunks.get(c, ()):
                lo, hi = max(c * CHUNK + co, da), min(c * CHUNK + co + n, da + count * 32)
                if lo < hi:
                    with open(os.path.join(d, f), "rb") as fh:
                        fh.seek(fo + lo - (c * CHUNK + co))
                        raw[lo - da:hi - da] = fh.read(hi - lo)
        fix(raw)
        path = f"replay/{tag}-{pool:x}.bin"
        open(os.path.join(d, path), "wb").write(raw)
        for c in range(da // CHUNK, (da + count * 32 - 1) // CHUNK + 1):
            lo, hi = max(c * CHUNK, da), min((c + 1) * CHUNK, da + count * 32)
            b, o = divmod(slot[c], per_block)
            lines.append(f"fill {b} {o * CHUNK + lo % CHUNK:x} {path} {lo - da:x} {hi - lo:x}")

    def pools_of(kind):
        out = set()
        for s_ in states.values():
            for e in (0xB197, 0xB1C0):
                r = s_["regs"][e]
                out.add((((r[names[e].off(f"SET_TEX_{kind}_POOL_A") // 4] & 0xFF) << 32) | r[names[e].off(f"SET_TEX_{kind}_POOL_B") // 4],
                         r[names[e].off(f"SET_TEX_{kind}_POOL_C") // 4] + 1))
        return sorted(p for p in out if p[0])

    # PT_TSC_NODC=<hex indices|all>: clear DEPTH_COMPARE (TEXSAMP0 bit 9) in those samplers. NVN pairs
    # compare samplers with plain (non-.DC) TEX on depth textures; the T239 tolerates it, Ada raises
    # TEX FORMAT (MKW entry 58: Z16 2D_ARRAY with sampler 0x1c7).
    nodc = os.environ.get("PT_TSC_NODC")
    if nodc:
        for pool, count in pools_of("SAMPLER"):
            which = range(count) if nodc == "all" else [int(x, 16) for x in nodc.split(",")]

            def fix(raw, which=which):
                n = 0
                for i in which:
                    w0 = struct.unpack_from("<I", raw, i * 32)[0]
                    if w0 & 0x200:
                        struct.pack_into("<I", raw, i * 32, w0 & ~0x200)
                        n += 1
                print(f"PT_TSC_NODC: pool {pool:x}: depth compare cleared in {n} samplers")
            patch_pool(pool, count, fix, "tsc")
    # PT_TIC_COPY=<dst>:<src>[,...] (hex): texture header dst becomes a copy of src (bisecting header issues).
    tic_copy = [tuple(int(x, 16) for x in p.split(":")) for p in os.environ.get("PT_TIC_COPY", "").split(",") if p]
    if tic_copy:
        for pool, count in pools_of("HEADER"):
            def fix(raw):
                for dst, src in tic_copy:
                    raw[dst * 32:dst * 32 + 32] = raw[src * 32:src * 32 + 32]
                print(f"PT_TIC_COPY: pool {pool:x}: {len(tic_copy)} headers replaced")
            patch_pool(pool, count, fix, "ticcopy")
    if os.environ.get("PT_ASTC") == "dummy":
        pools = set()
        for s_ in states.values():
            for e in (0xB197, 0xB1C0):
                r = s_["regs"][e]
                pools.add((((r[names[e].off("SET_TEX_HEADER_POOL_A") // 4] & 0xFF) << 32) | r[names[e].off("SET_TEX_HEADER_POOL_B") // 4],
                           r[names[e].off("SET_TEX_HEADER_POOL_C") // 4] + 1))
        for pool, count in sorted(p for p in pools if p[0]):
            gpu, dev, msize, _, _ = next(m for m in maps if m[0] <= pool < m[0] + m[2])
            assert pool + count * 32 <= gpu + msize, "pool spans mappings"
            da = dev + pool - gpu
            raw = bytearray(count * 32)
            for c in range(da // CHUNK, (da + count * 32 - 1) // CHUNK + 1):  # from the chunks' own data
                for f, fo, co, n in chunks.get(c, ()):
                    lo, hi = max(c * CHUNK + co, da), min(c * CHUNK + co + n, da + count * 32)
                    if lo < hi:
                        with open(os.path.join(d, f), "rb") as fh:
                            fh.seek(fo + lo - (c * CHUNK + co))
                            raw[lo - da:hi - da] = fh.read(hi - lo)
            fmt = [raw[i * 32] & 0x7F for i in range(count)]
            rgba = next(i for i in range(count) if fmt[i] == TIC_FORMATS["A8B8G8R8"] and any(raw[i * 32:i * 32 + 32]))
            astc_codes = {v for k, v in TIC_FORMATS.items() if k.startswith("ASTC")}
            astc = [i for i in range(count) if fmt[i] in astc_codes and any(raw[i * 32:i * 32 + 32])]
            for i in astc:
                raw[i * 32:i * 32 + 32] = raw[rgba * 32:rgba * 32 + 32]
            path = f"replay/tic-{pool:x}.bin"
            open(os.path.join(d, path), "wb").write(raw)
            for c in range(da // CHUNK, (da + count * 32 - 1) // CHUNK + 1):
                lo, hi = max(c * CHUNK, da), min((c + 1) * CHUNK, da + count * 32)
                b, o = divmod(slot[c], per_block)
                lines.append(f"fill {b} {o * CHUNK + lo % CHUNK:x} {path} {lo - da:x} {hi - lo:x}")
            print(f"PT_ASTC=dummy: pool {pool:x}: {len(astc)} ASTC headers -> header {rgba:#x} (A8B8G8R8)")
    for idx, ch, va in drams:
        lines += [f"block {LMB + 1 + idx} {CHUNK:x}", f"fill {LMB + 1 + idx} 0 replay/dram-{ch}.bin 0 3000",
                  f"map {va:x} {CHUNK:x} {LMB + 1 + idx} 0"]
    for idx, ch in enumerate(chans):
        lines.append(f"channel {idx} " + " ".join(f"{ENGINES[e][1] if e in ENGINES else 0:x}" for e in states[ch]["engines"]))
    open(os.path.join(out_dir, "plan.txt"), "w").write("\n".join(lines) + "\n")
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


BPP = {0xD5: 4, 0xD6: 4, 0xCF: 4, 0xD0: 4, 0xD1: 4, 0xE0: 4, 0xCA: 8, 0xC6: 8}


def size_of(w, h, fmt, mem):
    bpp, bh = BPP[fmt], 1 << ((mem >> 4) & 0xF)
    if mem & 0x1000:  # pitch: width is the pitch in bytes
        return w * h
    return -(-w * bpp // 64) * -(-h // (8 * bh)) * 512 * bh


def image(path, w, h, fmt, mem, out):
    """Color target bytes (as read back) -> PNG. Block linear: 64 B x 8 row GOBs, block height from
    SET_COLOR_TARGET_MEMORY. Float formats are clamped to [0, 1]."""
    import numpy as np
    from PIL import Image
    w, h, fmt, mem = int(w), int(h), int(fmt, 16), int(mem, 16)
    bpp, bh = BPP[fmt], 1 << ((mem >> 4) & 0xF)
    raw = np.fromfile(path, dtype=np.uint8)
    if mem & 0x1000:
        px = raw[: w * h].reshape(h, w // bpp, bpp)
    else:
        y, xb = np.mgrid[0:h, 0:w * bpp]
        bpr = -(-w * bpp // 64)
        addr = (((y // (8 * bh)) * bpr + xb // 64) * 512 * bh + ((y % (8 * bh)) // 8) * 512 +
                ((xb % 64) // 32) * 256 + ((y % 8) // 2) * 64 + ((xb % 32) // 16) * 32 + (y % 2) * 16 + xb % 16)
        px = raw[addr].reshape(h, w, bpp)
    if fmt in (0xD5, 0xD6):
        rgb = px[..., :3]
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


if __name__ == "__main__":
    {"stats": stats, "build": build, "dump": dump, "image": image, "peek": peek, "qmds": qmds}[sys.argv[1]](*sys.argv[2:])
