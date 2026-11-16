#!/usr/bin/env python3
"""stackcut.py perf.data comm needle [depth]: of samples on thread `comm` whose stack has a frame
matching `needle`, count the frame `depth` levels below the match (default: the leaf)."""
import collections, re, subprocess, sys
data, comm, needle = sys.argv[1:4]
out = subprocess.run(["./perf", "script", "-i", data, "--comm", comm, "-F", "ip,sym"], capture_output=True, text=True).stdout
leaf, total, hit = collections.Counter(), 0, 0
def short(s):
    s = re.sub(r"\(.*", "", s)          # drop args
    return re.sub(r"<[^<>]*>", "", re.sub(r"<[^<>]*>", "", s))[-90:]
for block in out.split("\n\n"):
    frames = [l.strip().split(None, 1)[1] if len(l.strip().split(None, 1)) > 1 else "?" for l in block.strip().splitlines()]
    if not frames: continue
    total += 1
    idx = next((i for i, f in enumerate(frames) if needle in f), None)
    if idx is None: continue
    hit += 1
    leaf[short(frames[0])] += 1
print(f"{hit}/{total} samples ({100*hit/max(total,1):.1f}%) pass through {needle}")
for s, n in leaf.most_common(25):
    print(f"{100*n/max(hit,1):5.1f}%  {s}")
