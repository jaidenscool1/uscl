"""children.py perf.data needle: GPUBack samples under `needle`, by the frame directly below it."""
import collections, re, subprocess, sys
out = subprocess.run(["./perf", "script", "-i", sys.argv[1], "--comm", "GPUBack", "-F", "ip,sym"],
                     capture_output=True, text=True).stdout
c, total, hit = collections.Counter(), 0, 0
def short(s): return re.sub(r"<[^<>]*>", "", re.sub(r"<[^<>]*>", "", re.sub(r"\(.*", "", s)))[-70:]
for block in out.split("\n\n"):
    frames = [l.strip().split(None, 1)[-1] for l in block.strip().splitlines()] if block.strip() else []
    if not frames: continue
    total += 1
    idx = next((i for i, f in enumerate(frames) if sys.argv[2] in f), None)
    if idx is None: continue
    hit += 1
    c["(self)" if idx == 0 else short(frames[idx - 1])] += 1
print(f"{hit} samples under {sys.argv[2]} = {100*hit/total:.1f}% of GPUBack")
for k, n in c.most_common(22): print(f"  {100*n/total:5.1f}%  {k}")
