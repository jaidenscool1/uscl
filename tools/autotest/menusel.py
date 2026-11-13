#!/usr/bin/env python3
"""menusel.py <screenshot> <window x> <window width>: width in px of the orange selected main-menu entry
(BO2 launcher main menu; ~150 = MULTIPLAYER, ~105 = CAMPAIGN, 0 = not on that menu)."""
import sys
from PIL import Image
img = Image.open(sys.argv[1]).convert('RGB')
x0, w = int(sys.argv[2]), int(sys.argv[3])
cols = set()
for y in range(540, 575):
    for x in range(x0, x0 + w):
        r, g, b = img.getpixel((x, y))
        if r > 200 and 90 < g < 170 and b < 80:  # BO2 orange
            cols.add(x)
print(max(cols) - min(cols) if cols else 0)
