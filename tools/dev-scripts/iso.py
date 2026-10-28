# minimal ISO9660 lister/extractor: iso.py ISO [extract-name-substring outdir]
import sys, struct, os
f = open(sys.argv[1], "rb")
def sec(n, c=1): f.seek(n * 2048); return f.read(2048 * c)
pvd = sec(16); root = pvd[156:190]
def walk(rec, path):
    lba, size = struct.unpack("<I", rec[2:6])[0], struct.unpack("<I", rec[10:14])[0]
    data = sec(lba, (size + 2047) // 2048); i = 0
    while i < size:
        n = data[i]
        if n == 0: i = (i // 2048 + 1) * 2048; continue
        r = data[i:i + n]; name = r[33:33 + r[32]]; i += n
        if name in (b"\0", b"\1"): continue
        nm = name.decode().split(";")[0]
        if r[25] & 2: yield from walk(r, path + nm + "/")
        else: yield path + nm, struct.unpack("<I", r[2:6])[0], struct.unpack("<I", r[10:14])[0]
for p, lba, size in walk(root, "/"):
    if len(sys.argv) > 2:
        if sys.argv[2] in p:
            f.seek(lba * 2048); open(os.path.join(sys.argv[3], os.path.basename(p)), "wb").write(f.read(size)); print("extracted", p, size)
    else: print(p, size)
