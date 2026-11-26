#!/usr/bin/env python3
"""Render the XMB category icons for each console USCL supports (PS3, PS4, Wii, GameCube, Wii U, 3DS, DS, Switch, Switch 2, Xbox 360), in the same style as the
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

# --- Xbox 360: the Xbox nexus -- a ring with a stylized X inside ---
def x360(d, S):
    t = int(S * 0.06)
    cx, cy, r = int(S*0.5), int(S*0.5), int(S*0.34)
    d.ellipse([cx-r, cy-r, cx+r, cy+r], outline=255, width=t)   # ring
    a = int(r*0.5)
    d.line([cx-a, cy-a, cx+a, cy+a], fill=255, width=t)         # X
    d.line([cx-a, cy+a, cx+a, cy-a], fill=255, width=t)

# --- Wii U: the GamePad tablet -- a wide rounded slab with a central screen and two circle-pad dots ---
def wiiu(d, S):
    t = int(S * 0.05)
    x0, x1 = int(S*0.08), int(S*0.92)
    y0, y1 = int(S*0.30), int(S*0.70)
    rrect(d, [x0, y0, x1, y1], int(S*0.10), outline_w=t)          # tablet body
    rrect(d, [int(S*0.30), int(S*0.37), int(S*0.70), int(S*0.63)], int(S*0.03), outline_w=int(t*0.7))  # center screen
    for cx in (int(S*0.20), int(S*0.80)):                          # two circle pads
        r = int(S*0.035)
        d.ellipse([cx-r, int(S*0.50)-r, cx+r, int(S*0.50)+r], outline=255, width=int(t*0.7))

# --- 3DS: open clamshell, two hinged rounded screens (bottom wider) ---
def nds3(d, S):
    t = int(S * 0.05)
    # top screen (slightly back/up)
    rrect(d, [int(S*0.22), int(S*0.12), int(S*0.78), int(S*0.46)], int(S*0.05), outline_w=t)
    # bottom screen (wider, front)
    rrect(d, [int(S*0.14), int(S*0.52), int(S*0.86), int(S*0.88)], int(S*0.05), outline_w=t)
    # hinge line between them
    d.line([int(S*0.20), int(S*0.49), int(S*0.80), int(S*0.49)], fill=255, width=int(t*0.8))

# --- Switch: the tablet with a controller on each side (stick dot left, button dot right) ---
def switch(d, S):
    t = int(S * 0.05)
    y0, y1 = int(S*0.28), int(S*0.72)
    rrect(d, [int(S*0.26), y0, int(S*0.74), y1], int(S*0.03), outline_w=t)          # tablet
    rrect(d, [int(S*0.06), y0, int(S*0.22), y1], int(S*0.08), outline_w=t)          # left controller
    rrect(d, [int(S*0.78), y0, int(S*0.94), y1], int(S*0.08), outline_w=t)          # right controller
    r = int(S*0.03)
    d.ellipse([int(S*0.14)-r, int(S*0.42)-r, int(S*0.14)+r, int(S*0.42)+r], fill=255)  # stick
    d.ellipse([int(S*0.86)-r, int(S*0.56)-r, int(S*0.86)+r, int(S*0.56)+r], fill=255)  # buttons

# --- GameCube: the cube with its round disc lid and carry handle ---
def gamecube(d, S):
    t = int(S * 0.05)
    rrect(d, [int(S*0.16), int(S*0.22), int(S*0.84), int(S*0.86)], int(S*0.08), outline_w=t)   # body
    r = int(S*0.17)
    cx, cy = int(S*0.50), int(S*0.50)
    d.ellipse([cx-r, cy-r, cx+r, cy+r], outline=255, width=t)                                   # disc lid
    d.line([int(S*0.26), int(S*0.14), int(S*0.74), int(S*0.14)], fill=255, width=t)              # handle
    d.line([int(S*0.26), int(S*0.12), int(S*0.26), int(S*0.24)], fill=255, width=t)
    d.line([int(S*0.74), int(S*0.12), int(S*0.74), int(S*0.24)], fill=255, width=t)

# --- DS: the clamshell open, equal screens, d-pad left and buttons right of the lower one ---
def nds(d, S):
    t = int(S * 0.045)
    rrect(d, [int(S*0.10), int(S*0.08), int(S*0.90), int(S*0.47)], int(S*0.05), outline_w=t)     # top half
    rrect(d, [int(S*0.32), int(S*0.15), int(S*0.68), int(S*0.40)], int(S*0.02), fill=255)        # top screen
    rrect(d, [int(S*0.10), int(S*0.53), int(S*0.90), int(S*0.92)], int(S*0.05), outline_w=t)     # bottom half
    rrect(d, [int(S*0.32), int(S*0.60), int(S*0.68), int(S*0.85)], int(S*0.02), outline_w=t)     # touch screen
    c, a = int(S*0.21), int(S*0.045)
    d.rectangle([c-a, int(S*0.725)-a//3, c+a, int(S*0.725)+a//3], fill=255)                       # d-pad
    d.rectangle([c-a//3, int(S*0.725)-a, c+a//3, int(S*0.725)+a], fill=255)
    r = int(S*0.022)
    for x, y in [(0.79, 0.69), (0.79, 0.76), (0.75, 0.725), (0.83, 0.725)]:
        d.ellipse([int(S*x)-r, int(S*y)-r, int(S*x)+r, int(S*y)+r], fill=255)                    # buttons

# --- Switch 2: the Switch silhouette, larger screen, a "2" on it ---
def switch2(d, S):
    from PIL import ImageFont
    t = int(S * 0.05)
    y0, y1 = int(S*0.26), int(S*0.74)
    rrect(d, [int(S*0.24), y0, int(S*0.76), y1], int(S*0.03), outline_w=t)
    rrect(d, [int(S*0.05), y0, int(S*0.20), y1], int(S*0.07), outline_w=t)
    rrect(d, [int(S*0.80), y0, int(S*0.95), y1], int(S*0.07), outline_w=t)
    font = ImageFont.truetype(os.path.join(os.path.dirname(OUT), "..", "fonts", "Exo2-Variable.ttf"), int(S*0.34))
    font.set_variation_by_axes([900])
    d.text((int(S*0.50), int(S*0.50)), "2", font=font, fill=255, anchor="mm")

# --- PS5: standing upright, the dark core between two tall curved white side panels ---
def ps5(d, S):
    t = int(S * 0.05)
    # core body
    rrect(d, [int(S*0.42), int(S*0.12), int(S*0.58), int(S*0.88)], int(S*0.04), outline_w=t)
    # side panels: wider at top and bottom, pinched toward the core at the waist
    for side in (-1, 1):
        cx = S * 0.5
        inner, outer, waist = S * 0.10, S * 0.36, S * 0.22
        pts = [(cx + side*inner, S*0.06), (cx + side*outer, S*0.10), (cx + side*waist, S*0.50),
               (cx + side*outer, S*0.90), (cx + side*inner, S*0.94)]
        d.line([(int(x), int(y)) for x, y in pts], fill=255, width=t, joint="curve")

def main():
    os.makedirs(OUT, exist_ok=True)
    for name, fn in [("cat_ps3", ps3), ("cat_ps4", ps4), ("cat_ps5", ps5),("cat_wii", wii), ("cat_wiiu", wiiu),
                     ("cat_3ds", nds3), ("cat_x360", x360), ("cat_switch", switch),
                     ("cat_gamecube", gamecube), ("cat_ds", nds), ("cat_switch2", switch2)]:
        render(fn).save(os.path.join(OUT, name + ".png"))
        print("wrote", name + ".png")

if __name__ == "__main__":
    main()
