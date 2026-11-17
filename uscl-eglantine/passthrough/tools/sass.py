# sass.py <capture> <program va> [out.sass]: disassemble a guest shader program (SPH skipped) from the start dump.
import bisect, os, subprocess, sys, tempfile
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
import ptcap as P
d, va = sys.argv[1], int(sys.argv[2], 16)
def read(va, size):
    m = next(x for x in P.read_maps(d) if x[0] <= va < x[0] + x[2])
    da = m[1] + va - m[0]
    files = sorted((int(f[4:-4], 16), f) for f in os.listdir(d) if f.startswith("dev-"))
    base, f = files[bisect.bisect_right([b for b, _ in files], da) - 1]
    with open(os.path.join(d, f), "rb") as fh:
        fh.seek(da - base); return fh.read(size)
img = read(va, 0x10000)
end = next(o for o in range(0x80, len(img) - 16, 16) if img[o] == 0x4D and img[o + 1] & 0xF == 9 and
           int.from_bytes(img[o + 16:o + 32], "little") & ~P.SASS_CTL == P.sass_bra(-0x10) & ~P.SASS_CTL)
with tempfile.NamedTemporaryFile(suffix=".bin") as f:
    f.write(img[0x80:end + 0x20]); f.flush()
    out = subprocess.run([os.environ.get("NVDISASM", "nvdisasm"), "-b", "SM86", *os.environ.get("NVDISASM_FLAGS", "").split(), f.name], capture_output=True, text=True, check=True).stdout
open(sys.argv[3], "w").write(out) if len(sys.argv) > 3 else print(out)
