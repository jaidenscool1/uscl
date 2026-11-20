# startread.py <capture> <va> <size> <out>: the start dump's bytes at a GPU VA (spans mappings; sparse/unmapped = 0).
import bisect, os, sys, numpy as np
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
import ptcap as P
d, va, size, out = sys.argv[1], int(sys.argv[2], 16), int(sys.argv[3], 16), sys.argv[4]
files = sorted((int(f[4:-4], 16), f) for f in os.listdir(d) if f.startswith("dev-"))
mms = {f: np.memmap(os.path.join(d, f), np.uint8, mode="r") for _, f in files}
buf = np.zeros(size, np.uint8)
for gpu, dev, msize, _, sparse in P.read_maps(d):
    lo, hi = max(va, gpu), min(va + size, gpu + msize)
    if lo >= hi or sparse:
        continue
    for base, f in files:
        a, b = max(dev + lo - gpu, base), min(dev + hi - gpu, base + len(mms[f]))
        if a < b:
            buf[a - (dev - gpu) - va:b - (dev - gpu) - va] = mms[f][a - base:b - base]
buf.tofile(out)
