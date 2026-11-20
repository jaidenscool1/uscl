# slice.py <program.sass> <reg> [addr]: backward data slice of register <reg> as read at [addr] (default: EXIT) in
# nvdisasm output, straight-line approximation (branches ignored, last writer above wins). Prints the slice's
# instructions and its leaves (texture fetches by handle, global loads, constants, attributes).
# shortcut: ignores predication and control flow; enough to see which inputs feed an output, not exact semantics.
import re, sys
lines = [(int(m[1], 16), m[2].strip()) for l in open(sys.argv[1]) if (m := re.match(r"\s*/\*([0-9a-f]{4})\*/\s+(.*?);", l))]
def regs(s): return re.findall(r"\bR(\d+)\b", s)
def parse(ins):
    ins = re.sub(r"^@!?P\w+\s+", "", ins)
    op, _, rest = ins.partition(" ")
    ops = [o.strip() for o in rest.split(",")]
    if not ops or not ops[0] or op.split(".")[0] in ("EXIT", "BRA", "BSSY", "BSYNC", "DEPBAR", "NOP", "ISETP", "FSETP", "PLOP3"):
        return op, [], regs(rest)
    base = op.split(".")[0]
    wide = 2 if ".64" in op else 1
    if base in ("TEX", "TLD", "TLD4", "TMML", "TXQ"):  # TEX[.SCR] Rd2, Rd, Ra, Rb, ...: up to 4 results
        d = [r for o in ops[:2] for r in regs(o)]
        dst = [str(int(r) + k) for r in d for k in range(2)]
        return op, dst, [r for o in ops[2:4] for r in regs(o)]
    d = regs(ops[0])
    dst = [str(int(d[0]) + k) for k in range(wide)] if d else []
    return op, dst, [r for o in ops[1:] for r in regs(o)]
at = int(sys.argv[3], 16) if len(sys.argv) > 3 else next(a for a, i in lines if i.startswith("EXIT"))
want, seen, leaves, out = {sys.argv[2].lstrip("R")}, set(), set(), []
for a, ins in reversed([x for x in lines if x[0] < at]):
    op, dst, src = parse(ins)
    hit = want & set(dst)
    if not hit:
        continue
    want -= hit
    want |= set(src)
    out.append((a, ins))
    base = op.split(".")[0]
    if base in ("TEX", "TLD", "TLD4", "TMML"):
        leaves.add("tex handle " + ins.split(",")[-2 if ins.count(",") >= 5 else -1].strip() if False else "tex " + ",".join(ins.split(",")[4:6]).strip())
    if base == "LDG": leaves.add("LDG " + ins)
    if base == "IPA": leaves.add("attr " + re.search(r"a\[[^\]]+\]", ins)[0])
    for c in re.findall(r"c\[0x[0-9a-f]+\]\[[^\]]+\]", ins): leaves.add(c)
for a, ins in reversed(out): print(f"{a:04x}  {ins}")
print("leaves:", *sorted(leaves), sep="\n  ")
print("unresolved regs:", sorted(want, key=int))
