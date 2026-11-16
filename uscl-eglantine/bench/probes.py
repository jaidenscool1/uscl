"""Average EDEN_NX2_BIND_STATS reports: probes.py LOG [first_frac] [last_frac]"""
import re, sys, collections
lines = open(sys.argv[1], errors="replace").read().splitlines()
lo, hi = float(sys.argv[2]) if len(sys.argv) > 2 else 0.5, float(sys.argv[3]) if len(sys.argv) > 3 else 1.0
reps, cur = [], None
pat = re.compile(r"^\s+(.+?)\s+([\d.]+)/draw\s+hit\s+([\d.]+)%\s+(\d+) ns/call\s+([\d.]+) us/draw")
for l in lines:
    if "nx2 draw probes" in l:
        cur = {"t": float(l[1:l.index("]")])}; reps.append(cur); continue
    m = pat.match(l) if cur is not None else None
    if m: cur[m.group(1)] = tuple(map(float, m.group(2, 3, 4, 5)))
    elif cur is not None and l and not l.startswith(" "): cur = None
sel = reps[int(len(reps)*lo):int(len(reps)*hi)]
print(f"{len(sel)} of {len(reps)} reports, t={sel[0]['t']:.0f}-{sel[-1]['t']:.0f}s, draws/s={20000*len(sel)/(sel[-1]['t']-sel[0]['t']):.0f}")
names = collections.OrderedDict()
for r in sel:
    for k in r:
        if k != "t": names[k] = 1
for k in names:
    v = [r[k] for r in sel if k in r]
    n = len(sel)
    calls = sum(x[0] for x in v)/n; hit = sum(x[0]*x[1] for x in v)/max(1e-9, sum(x[0] for x in v))
    ns = sum(x[0]*x[2] for x in v)/max(1e-9, sum(x[0] for x in v)); us = sum(x[3] for x in v)/n
    print(f"  {k:<26} {calls:6.2f}/draw  hit {hit:5.1f}%  {ns:6.0f} ns/call  {us:5.2f} us/draw")
