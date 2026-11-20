# drawstate.py <capture> <entry>: the 3D state of the last draw in a capture entry (3D macro trace replayed in
# order): pixel program address, fragment constant buffers (stage 4), texture header / sampler pools.
# DRAWS=1: also one line per draw in the entry (color target 0, pixel program). PS=<program>: the last draw
# with that pixel program instead. Macro calls pair with the trace through ptcap.macro3d_calls.
import collections, os, sys
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
import ptcap as P
d, K = sys.argv[1], int(sys.argv[2])
states, stream = P.read_states(d), P.read_stream(d)
n3d = P.Names(P.ENGINES[0xB197][0])
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
PS = int(os.environ["PS"], 16) if os.environ.get("PS") else None
prog = lambda r: r.get(off("SET_PIPELINE_PROGRAM_ADDRESS_A", 5), 0) << 32 | r.get(off("SET_PIPELINE_PROGRAM_ADDRESS_B", 5), 0)
def report(ch, name):
    global snap
    r = regs[ch]
    if os.environ.get("DRAWS"):
        rt = r.get(off("SET_COLOR_TARGET_A"), 0) << 32 | r.get(off("SET_COLOR_TARGET_B"), 0)
        print(f"  draw {name}: rt0 {rt:x} fmt {r.get(off('SET_COLOR_TARGET_FORMAT'), 0):#x} ps {prog(r):x}")
    if PS is None or prog(r) == PS:
        snap = (ch, dict(r), [list(x) for x in cb[ch]])
call = {}
for k, ch, sc, m, a, emits in P.macro3d_calls(d, states, stream, n3d):
    if k > K:
        break
    if m < 0x40 or states[ch]["engines"][sc] != 0xB197:
        continue
    nm = n3d(m * 4) or ""
    if nm.startswith("CALL_MME_DATA("):
        continue
    for mm, vv in call.pop(ch, []):  # the previous call is complete once a non-parameter method follows
        if mm < 0xE00:
            put(ch, mm, vv)
            if k == K and (n3d(mm * 4) or "").startswith(("DRAW_INDEX", "DRAW_VERTEX")):
                report(ch, n3d(mm * 4))
    if emits is not None:
        call[ch] = emits
        continue
    put(ch, m, a)
    if k == K and nm.startswith(("DRAW_INDEX", "DRAW_VERTEX")):
        report(ch, nm)
for ch, emits in call.items():  # a call that ends the entry
    for mm, vv in emits:
        if mm < 0xE00:
            put(ch, mm, vv)
            if (n3d(mm * 4) or "").startswith(("DRAW_INDEX", "DRAW_VERTEX")):
                report(ch, n3d(mm * 4))
if snap is None:
    sys.exit(f"entry {K}: no draw" + (f" with program {PS:x}" if PS else ""))
ch, r, cbs = snap
cb[ch] = cbs
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
