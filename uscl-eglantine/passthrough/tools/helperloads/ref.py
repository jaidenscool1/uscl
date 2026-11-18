# ref.py <capture> <outdir>: writes each pixel program's image (<va>.img) and ptcap's patch of it (<va>.py.out,
# same layout as harness.cpp) for trampolines at 0x9000000000, plus <va>.free (predicates nvdisasm never shows).
import os, re, sys, struct, subprocess, tempfile, bisect
sys.path.insert(0, os.path.expanduser("~/src/ps3-steam-np/.claude/worktrees/gpu-passthrough/uscl-eglantine/passthrough"))
import ptcap as P
d, out = sys.argv[1:3]; os.makedirs(out, exist_ok=True)
states, stream = P.read_states(d), P.read_stream(d); n3d = P.Names(P.ENGINES[0xB197][0])
maps = P.read_maps(d)
files = sorted((int(f[4:-4], 16), f) for f in os.listdir(d) if f.startswith("dev-"))
def read(va, n):
    m = next(x for x in maps if x[0] <= va < x[0] + x[2]); da = m[1] + va - m[0]; n = min(n, m[0] + m[2] - va)
    base, f = files[bisect.bisect_right([b for b, _ in files], da) - 1]
    fh = open(os.path.join(d, f), "rb"); fh.seek(da - base); return fh.read(n)
ldg_re = re.compile(r"/\*([0-9a-f]+)\*/\s+(?:@!?P[0-6T]\s+)?LDG(\S*)\s+R(\d+),")
for prog in sorted(P.pixel_programs(d, states, stream, n3d)):
    if not prog or not any(m[0] <= prog < m[0] + m[2] for m in maps): continue
    img = bytearray(read(prog, 1 << 15)); open(f"{out}/{prog:x}.img", "wb").write(img)
    end = next((o for o in range(0x80, len(img) - 16, 16) if img[o] == 0x4D and img[o + 1] & 0xF == 9 and
                int.from_bytes(img[o + 16:o + 32], "little") & ~P.SASS_CTL == P.sass_bra(-0x10) & ~P.SASS_CTL), None)
    res = dict(loads=0, skipped="", program=b"", tramp=b"")
    if end is None:
        res["skipped"] = "no program end found"
    else:
        raw = bytearray(img[:-(-(end + 0x20) // 32) * 32])
        with tempfile.NamedTemporaryFile(suffix=".bin") as f:
            f.write(raw[0x80:end + 0x20]); f.flush()
            text = subprocess.run(["nvdisasm", "-b", "SM86", f.name], capture_output=True, text=True, check=True).stdout
        free = [] if re.search(r"\bPR\b", text) else [p for p in range(6, -1, -1) if not re.search(rf"\bP{p}\b", text)]
        open(f"{out}/{prog:x}.free", "w").write(" ".join(map(str, free)))
        open(f"{out}/{prog:x}.sass", "w").write(text)
        sites = [(int(mo[1], 16) + 0x80, int(mo[3]), 4 if ".128" in mo[2] else 2 if ".64" in mo[2] else 1) for mo in ldg_re.finditer(text)]
        open(f"{out}/{prog:x}.sites", "w").write(" ".join(f"{o:x}:{r}:{n}" for o, r, n in sites))
        res["program"] = bytes(raw)
        first = int.from_bytes(raw[0x80:0x90], "little")
        if sites and not free: res["skipped"] = "no free predicate"
        elif sites and first & 0xFFF in (0x943, 0x945, 0x947, 0x949, 0x94A, 0x950, 0x94D): res["skipped"] = "starts with a branch"
        elif sites:
            arena = bytearray(); arena_va = 0x9000000000; p = free[0]
            moved = [w for w in sites if w[0] == 0x80]; t_va = arena_va
            tramp = [P.sass_s2r_thread_kill(0, 0), P.sass_isetp_ne_zero(p, 0, 1), first & ~(0xF << 122)]
            tramp += [P.sass_mov_rz_if(p, rd + i, 1 << (first >> 110 & 7)) for _, rd, n in moved for i in range(n)]
            tramp.append(P.sass_bra(prog + 0x90 - (t_va + 16 * len(tramp) + 16)))
            arena += b"".join(w.to_bytes(16, "little") for w in tramp)
            raw[0x80:0x90] = P.sass_bra(t_va - (prog + 0x80 + 16)).to_bytes(16, "little")
            for off, rd, n in sites:
                if off == 0x80: continue
                x = int.from_bytes(raw[off:off + 16], "little"); wr = x >> 110 & 7; x &= ~(0xF << 122)
                t_va, site_va = arena_va + len(arena), prog + off
                tramp = [x] + [P.sass_mov_rz_if(p, rd + i, 1 << wr) for i in range(n)]
                tramp.append(P.sass_bra(site_va + 16 - (t_va + 16 * len(tramp) + 16)))
                arena += b"".join(w.to_bytes(16, "little") for w in tramp)
                raw[off:off + 16] = P.sass_bra(t_va - (site_va + 16)).to_bytes(16, "little")
            res.update(loads=len(sites), program=bytes(raw), tramp=bytes(arena))
    with open(f"{out}/{prog:x}.py.out", "wb") as o:
        o.write(struct.pack("<II", res["loads"], len(res["skipped"])) + res["skipped"].encode())
        o.write(struct.pack("<I", len(res["program"])) + res["program"] + struct.pack("<I", len(res["tramp"])) + res["tramp"])
print("programs:", len([f for f in os.listdir(out) if f.endswith(".img")]))
