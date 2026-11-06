# toc_str.py ELF TOC "string"...: code addresses that build a pointer to the string as TOC slot + addi offset
import sys, struct
d = open(sys.argv[1], "rb").read(); toc = int(sys.argv[2], 16)
segs = [(0x10000, 0, 0x6da960), (0x6f0000, 0x6e0000, 0x1bbb1c)]
def rd(va, n):
    for v, o, s in segs:
        if v <= va < v + s: return d[o + va - v:o + va - v + n]
code = d[0:0x6da960]
for s in sys.argv[3:]:
    off = d.find(s.encode()); va = off + 0x10000 if off < 0x6e0000 else off - 0x6e0000 + 0x6f0000
    slots = {}
    for a in range(toc - 0x8000, toc + 0x8000, 4):
        w = rd(a, 4)
        if w and len(w) == 4:
            v = struct.unpack(">I", w)[0]
            if 0 <= va - v < 0x8000: slots[a - toc] = va - v
    for i in range(0, len(code) - 4, 4):
        ins = struct.unpack(">I", code[i:i + 4])[0]
        if ins >> 26 == 32 and (ins >> 16) & 31 == 2:
            dd = ins & 0xffff; dd = dd - 0x10000 if dd & 0x8000 else dd
            if dd in slots:
                rt = (ins >> 21) & 31
                if slots[dd] == 0: print(f"{s[:40]!r} va={va:x} slot used at {0x10000 + i:x}"); continue
                for j in range(1, 30):
                    n = struct.unpack(">I", code[i + 4 * j:i + 4 * j + 4])[0]
                    if n >> 26 == 14 and (n >> 16) & 31 == rt and (n & 0xffff) == slots[dd]:
                        print(f"{s[:40]!r} va={va:x} used at {0x10000 + i + 4 * j:x}"); break
