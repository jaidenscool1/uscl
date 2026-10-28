#!/usr/bin/env python3
"""Build Black Ops II's online data file (online_tu19_mp_english.wad) from files in a folder.
Layout (t6mp 1.19, read in 0055cc24): header u32 BE magic 0x543377ab, timestamp, entry count, version;
then 0x2c-byte entries name[32], compressed size, size, offset from file start; zlib data.
The game inflates each entry into a zeroed 256 KB buffer, so every file must stay under that.
usage: mkwad.py <out.wad> <file>..."""
import os, struct, sys, zlib

MAGIC, ENTRY, MAX = 0x543377AB, 0x2C, 0x40000

def build(paths):
    entries, blobs = [], []
    off = 16 + ENTRY * len(paths)
    for p in paths:
        raw = open(p, 'rb').read()
        name = os.path.basename(p).encode()
        assert len(raw) < MAX and len(name) < 32, p
        z = zlib.compress(raw, 9)
        entries.append(struct.pack('>32sIII', name, len(z), len(raw), off))
        blobs.append(z)
        off += len(z)
    return struct.pack('>IIIi', MAGIC, 0, len(paths), -1) + b''.join(entries) + b''.join(blobs)

if __name__ == '__main__':
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    w = build(sys.argv[2:])
    # self-check: entries round-trip
    for i in range(struct.unpack_from('>I', w, 8)[0]):
        n, c, s, o = struct.unpack_from('>32sIII', w, 16 + i * ENTRY)
        assert len(zlib.decompress(w[o:o + c])) == s
    open(sys.argv[1], 'wb').write(w)
