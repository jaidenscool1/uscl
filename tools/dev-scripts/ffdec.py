# Inflate an IW4 PS3 fastfile: find deflate streams after the header and concatenate their output.
import sys, zlib
d = open(sys.argv[1], "rb").read(); out = bytearray(); i = 0x10
while i < len(d) - 2:
    for wbits in (15, -15):
        try:
            o = zlib.decompressobj(wbits); r = o.decompress(d[i:i + (1 << 22)])
            if len(r) > 64 and (o.eof or len(r) > 4096):
                out += r; i += len(d[i:i + (1 << 22)]) - len(o.unused_data) if o.eof else 1; break
        except zlib.error: pass
    else: i += 1; continue
    if not o.eof: break
open(sys.argv[2], "wb").write(out); print(sys.argv[1], len(out))
