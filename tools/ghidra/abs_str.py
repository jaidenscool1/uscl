# abs_str.py ELF "string"...: code addresses that build the string's address with lis/addis + addi/ori (any reg pair)
import sys, struct
d = open(sys.argv[1], "rb").read(); code = d[:0x6da960]
for s in sys.argv[2:]:
    off = d.find(s.encode()); va = off + 0x10000 if off < 0x6e0000 else off - 0x6e0000 + 0x6f0000
    hi = (va + 0x8000) >> 16; lo = va & 0xffff
    for i in range(0, len(code) - 4, 4):
        ins = struct.unpack(">I", code[i:i + 4])[0]
        if ins >> 26 == 15 and (ins & 0xffff) in (hi, va >> 16):
            rt = (ins >> 21) & 31
            for j in range(1, 40):
                n = struct.unpack(">I", code[i + 4 * j:i + 4 * j + 4])[0]
                if n >> 26 in (14, 24) and (n >> 16) & 31 == rt and (n & 0xffff) == lo:
                    print(f"{s[:40]!r} va={va:x} at {0x10000 + i + 4 * j:x}"); break
