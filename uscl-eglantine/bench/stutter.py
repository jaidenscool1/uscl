#!/usr/bin/env python3
"""Stutter summary from Eglantine's own 'last 600 presents' lines (no spike recorder).
usage: stutter.py label [label ...]  (reads run-<label>.log)"""
import os, re, sys

T = os.path.dirname(os.path.abspath(__file__))
pat = re.compile(r"last 600 presents: frame (\d+) us \(max (\d+), (\d+) over 100 ms\)")
for label in sys.argv[1:]:
    rows = [tuple(map(int, m.groups())) for m in pat.finditer(open(os.path.join(T, f"run-{label}.log"), errors="replace").read())]
    over = sum(r[2] for r in rows)
    worst = sorted((r[1] // 1000 for r in rows), reverse=True)[:5]
    print(f"{label:12} windows={len(rows):2d} frames>100ms={over:3d}  worst ms={worst}  avg frame ms={[r[0] // 1000 for r in rows]}")
