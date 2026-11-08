#!/usr/bin/env python3
"""Render the XMB category icons for each console USCL supports (PS3, PS4, Wii, 3DS), in the same style as the
JStationX Material-Icon glyphs in xmb_assets.py: a white silhouette with the XMB soft glow, 256x256 RGBA, on
transparent. Standalone (PIL only, no kodi-xmb) so it can regenerate just these; the shapes are simple, recognizable
console silhouettes drawn geometrically (same approach as uscl_logo). Output: uscl-shell/xmb/icons/cat_<console>.png

usage: python3 -I uscl-shell/tools/console_icons.py
"""
import os
from PIL import Image, ImageDraw, ImageFilter

OUT = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "xmb", "icons")
SIZE = 256
PAD = 26
K = 4  # supersample

def rgba(L):
    w = Image.new("L", L.size, 255)
    return Image.merge("RGBA", (w, w, w, L))

def render(draw_fn):
    S = (SIZE - 2 * PAD) * K
    img = Image.new("L", (S, S), 0)
    d = ImageDraw.Draw(img)
    draw_fn(d, S)
    small = img.resize((SIZE - 2 * PAD, SIZE - 2 * PAD), Image.LANCZOS)
    logo = rgba(small)
    glow = rgba(small.filter(ImageFilter.GaussianBlur(6)).point(lambda v: int(v * 0.45)))
    canvas = Image.new("RGBA", (SIZE, SIZE), (0, 0, 0, 0))
    canvas.alpha_composite(glow, (PAD, PAD))
    canvas.alpha_composite(logo, (PAD, PAD))
    return canvas

def rrect(d, box, r, outline_w=0, fill=255):
    if outline_w: d.rounded_rectangle(box, radius=r, outline=fill, width=outline_w)
    else: d.rounded_rectangle(box, radius=r, fill=fill)

# --- PS3: slim horizontal console slab with the fat-PS3 curved top edge + disc slot ---
def ps3(d, S):
    t = int(S * 0.055)
    x0, x1 = int(S*0.06), int(S*0.94)
    y0, y1 = int(S*0.30), int(S*0.70)
    # body outline (rounded slab)
    rrect(d, [x0, y0, x1, y1], int(S*0.07), outline_w=t)
    # the signature curved top (a shallow arc bulging up on the right 60%)
    ax0 = int(S*0.30)
    d.arc([ax0, int(S*0.17), x1+int(S*0.12), y0+int(S*0.26)], start=200, end=340, fill=255, width=t)
    # disc slot line
    d.line([int(S*0.14), int(S*0.50), int(S*0.60), int(S*0.50)], fill=255, width=int(t*0.7))
    # power/eject dots
    d.ellipse([int(S*0.80)-int(S*0.02), int(S*0.47), int(S*0.80)+int(S*0.02), int(S*0.51)], fill=255)

# --- PS4: the angular split slab (parallelogram), PS4's slanted two-tone wedge ---
def ps4(d, S):
    t = int(S * 0.055)
    # a slanted slab: parallelogram outline
    sl = int(S*0.14)
    pts = [(int(S*0.12)+sl, int(S*0.30)), (int(S*0.90), int(S*0.30)),
           (int(S*0.90)-sl, int(S*0.70)), (int(S*0.12), int(S*0.70))]
    d.line(pts + [pts[0]], fill=255, width=t, joint="curve")
    # the diagonal split line down the middle (PS4's two-tone seam)
    d.line([(int(S*0.51)+int(sl*0.5), int(S*0.30)), (int(S*0.51)-int(sl*0.5), int(S*0.70))], fill=255, width=int(t*0.8))
    # power light line on the seam
    d.line([int(S*0.20), int(S*0.50), int(S*0.44), int(S*0.50)], fill=255, width=int(t*0.6))

# --- Wii: the console standing vertically on its stand, disc slot + slot LED ---
def wii(d, S):
    t = int(S * 0.055)
    x0, x1 = int(S*0.36), int(S*0.64)
    y0, y1 = int(S*0.10), int(S*0.80)
    rrect(d, [x0, y0, x1, y1], int(S*0.05), outline_w=t)   # tall body
    d.line([x0+int(S*0.04), int(S*0.24), x1-int(S*0.04), int(S*0.24)], fill=255, width=int(t*0.7))  # disc slot
    d.ellipse([int(S*0.50)-int(S*0.015), int(S*0.30), int(S*0.50)+int(S*0.015), int(S*0.33)], fill=255)  # slot LED
    # stand base
    d.line([int(S*0.28), int(S*0.86), int(S*0.72), int(S*0.86)], fill=255, width=t)
    d.line([int(S*0.50), y1, int(S*0.50), int(S*0.86)], fill=255, width=int(t*0.8))

# --- 3DS: open clamshell, two hinged rounded screens (bottom wider) ---
def nds3(d, S):
    t = int(S * 0.05)
    # top screen (slightly back/up)
    rrect(d, [int(S*0.22), int(S*0.12), int(S*0.78), int(S*0.46)], int(S*0.05), outline_w=t)
    # bottom screen (wider, front)
    rrect(d, [int(S*0.14), int(S*0.52), int(S*0.86), int(S*0.88)], int(S*0.05), outline_w=t)
    # hinge line between them
    d.line([int(S*0.20), int(S*0.49), int(S*0.80), int(S*0.49)], fill=255, width=int(t*0.8))

def main():
    os.makedirs(OUT, exist_ok=True)
    for name, fn in [("cat_ps3", ps3), ("cat_ps4", ps4), ("cat_wii", wii), ("cat_3ds", nds3)]:
        render(fn).save(os.path.join(OUT, name + ".png"))
        print("wrote", name + ".png")

if __name__ == "__main__":
    main()
