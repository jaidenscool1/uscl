#!/usr/bin/env python3
"""Render the four trophy grade cups (bronze, silver, gold, platinum) into uscl-shell/xmb/trophies/.

Original artwork drawn here (no console assets): a cup with handles on a stepped base, filled with a
metallic vertical gradient and a rim highlight. Rendered at 4x and scaled down for smooth edges.

usage: python3 -I uscl-shell/tools/trophy_icons.py
"""
import os
from PIL import Image, ImageChops, ImageDraw, ImageFilter

OUT = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "xmb", "trophies")
SIZE, SS = 128, 4
# light, dark, rim highlight
GRADES = {
    "bronze": ("#f0b483", "#7a421c", "#ffd9b8"),
    "silver": ("#f7f9fb", "#7f8a94", "#ffffff"),
    "gold": ("#fff0a0", "#b07a08", "#fffbe0"),
    "platinum": ("#eef6ff", "#4a6896", "#ffffff"),
}


def hex_rgb(h):
    return tuple(int(h[i:i + 2], 16) for i in (1, 3, 5))


def cup_mask(n):
    s = n / 512
    m = Image.new("L", (n, n), 0)
    d = ImageDraw.Draw(m)
    box = lambda *v: [round(x * s) for x in v]
    # bowl: the lower half of an ellipse below the rim
    bowl = Image.new("L", (n, n), 0)
    ImageDraw.Draw(bowl).ellipse(box(128, -130, 384, 300), fill=255)
    ImageDraw.Draw(bowl).rectangle(box(0, 0, 512, 64), fill=0)
    m = ImageChops.lighter(m, bowl)
    d = ImageDraw.Draw(m)
    for x0 in (62, 322):  # handles
        d.ellipse(box(x0, 96, x0 + 128, 232), outline=255, width=round(24 * s))
    d.rectangle(box(232, 290, 280, 372), fill=255)                    # stem
    d.rounded_rectangle(box(176, 362, 336, 404), radius=round(10 * s), fill=255)
    d.rounded_rectangle(box(146, 404, 366, 452), radius=round(12 * s), fill=255)
    return m


def render(light, dark, rim):
    n = SIZE * SS
    mask = cup_mask(n)
    grad = Image.new("RGB", (n, n))
    a, b = hex_rgb(light), hex_rgb(dark)
    gd = ImageDraw.Draw(grad)
    for y in range(n):
        t = min(1.0, max(0.0, (y / n - 0.1) / 0.85))
        gd.line([(0, y), (n, y)], fill=tuple(round(a[i] + (b[i] - a[i]) * t) for i in range(3)))
    # a vertical sheen on the left of the bowl
    sheen = Image.new("L", (n, n), 0)
    ImageDraw.Draw(sheen).ellipse([round(v * n / 512) for v in (176, 84, 226, 250)], fill=110)
    grad = Image.composite(Image.new("RGB", (n, n), "white"), grad, sheen)
    # a thin dark edge, so pale cups stay visible on light themes
    edge = mask.filter(ImageFilter.MaxFilter(2 * round(3 * n / 512) + 1))
    img = Image.new("RGBA", (n, n), (0, 0, 0, 0))
    img.paste(Image.new("RGB", (n, n), tuple(c // 2 for c in b)), (0, 0), edge)
    img.paste(grad, (0, 0), mask)
    # rim: a lighter ellipse across the top of the bowl
    rim_layer = Image.new("RGBA", (n, n), (0, 0, 0, 0))
    ImageDraw.Draw(rim_layer).ellipse([round(v * n / 512) for v in (128, 48, 384, 84)], fill=hex_rgb(rim) + (255,),
                                      outline=hex_rgb(dark) + (255,), width=round(5 * n / 512))
    img = Image.alpha_composite(img, rim_layer)
    return img.resize((SIZE, SIZE), Image.LANCZOS)


def main():
    os.makedirs(OUT, exist_ok=True)
    for name, colors in GRADES.items():
        render(*colors).save(os.path.join(OUT, name + ".png"))
        print("wrote", os.path.join(OUT, name + ".png"))


if __name__ == "__main__":
    main()
