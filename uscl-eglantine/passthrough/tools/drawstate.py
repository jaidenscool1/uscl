# drawstate.py <capture> <entry>: the 3D state of the last draw in a capture entry (3D macro trace replayed in
# order): pixel program address, fragment constant buffers (stage 4), texture header / sampler pools.
# DRAWS=1: also one line per draw in the entry (color target 0, pixel program).
import collections, os, sys
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
import ptcap as P
d, K = sys.argv[1], int(sys.argv[2])
states, stream = P.read_states(d), P.read_stream(d)
n3d = P.Names(P.ENGINES[0xB197][0])
tr = collections.defaultdict(collections.deque)
for line in open(os.path.join(d, "mme.txt")):
    k, kind, a, v = line.split()
    if kind == "c":
        tr[int(k)].append([])
    elif kind == "e":
        tr[int(k)][-1].append((int(a, 16), int(v, 16)))
off = lambda name, i=0: n3d.off(name, i) // 4
SA, SB, SC = off("SET_CONSTANT_BUFFER_SELECTOR_A"), off("SET_CONSTANT_BUFFER_SELECTOR_B"), off("SET_CONSTANT_BUFFER_SELECTOR_C")
BIND = {off("BIND_GROUP_CONSTANT_BUFFER", g): g for g in range(5)}
regs = {ch: dict(enumerate(s["regs"][0xB197])) for ch, s in states.items()}
cb = {ch: [[(e, sz, a) for e, sz, a in slots] for slots in s["cbufs"]] for ch, s in states.items()}
st = {ch: [0, 0, 0, 0] for ch in states}
def put(ch, m, v):
    r = regs[ch]; r[m] = v
    if m in BIND:
        slot = (v >> 4) & 0x1F
        cb[ch][BIND[m]][slot] = (v & 1, r.get(SA, 0), r.get(SB, 0) << 32 | r.get(SC, 0))
snap = None
def report(ch, name):
    if os.environ.get("DRAWS") and name.startswith(("DRAW_INDEX", "DRAW_VERTEX", "BEGIN")):
        r = regs[ch]
        rt = r.get(off("SET_COLOR_TARGET_A"), 0) << 32 | r.get(off("SET_COLOR_TARGET_B"), 0)
        print(f"  draw {name}: rt0 {rt:x} fmt {r.get(off('SET_COLOR_TARGET_FORMAT'), 0):#x} "
              f"ps {r.get(off('SET_PIPELINE_PROGRAM_ADDRESS_A', 5), 0) << 32 | r.get(off('SET_PIPELINE_PROGRAM_ADDRESS_B', 5), 0):x}")
for k, (ch, _a, *_r, words) in enumerate(stream[:K + 1]):
    calls = tr.get(k, collections.deque())
    for sc, m, a, _ in P.decode(words, st[ch]):
        if m < 0x40 or states[ch]["engines"][sc] != 0xB197:
            continue
        put(ch, m, a)
        nm = n3d(m * 4) or ""
        if k == K:
            report(ch, nm)
        if nm.startswith("CALL_MME_MACRO(") and calls:
            for mm, vv in calls.popleft():
                if mm < 0xE00:
                    put(ch, mm, vv)
                if k == K and (n3d(mm * 4) or "").startswith(("DRAW", "BEGIN")):
                    snap = ch
                    report(ch, n3d(mm * 4) or "")
    if k == K and snap is None:
        snap = ch
r, ch = regs[snap], snap
g = lambda n, i=0: r.get(off(n, i), 0)
print(f"entry {K} channel {ch}")
for i in range(6):
    if g("SET_PIPELINE_SHADER", i) & 1:
        print(f"  pipeline {i}: shader {g('SET_PIPELINE_SHADER', i):#x} program {g('SET_PIPELINE_PROGRAM_ADDRESS_A', i) << 32 | g('SET_PIPELINE_PROGRAM_ADDRESS_B', i):x}")
print(f"  tex header pool {g('SET_TEX_HEADER_POOL_A') << 32 | g('SET_TEX_HEADER_POOL_B'):x} max {g('SET_TEX_HEADER_POOL_C'):#x}; "
      f"sampler pool {g('SET_TEX_SAMPLER_POOL_A') << 32 | g('SET_TEX_SAMPLER_POOL_B'):x}")
for stage in (3, 4):
    for slot, (e, sz, a) in enumerate(cb[ch][stage]):
        if e:
            print(f"  stage {stage} c{slot}: {a:x} +{sz:#x}")
