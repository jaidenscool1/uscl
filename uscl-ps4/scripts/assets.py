#!/usr/bin/env python3
"""Stage USCL's existing artwork, not generated approximations."""
from pathlib import Path
import shutil
import struct
import zlib

ROOT = Path(__file__).resolve().parents[1]
SHELL = ROOT.parent / "uscl-shell"

def png(path, size=512):
    # Deterministic package icon; the UI uses the established USCL artwork.
    def chunk(kind, data):
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data))
    # A simple crossbar glyph built in code, no image generation service needed.
    rows = bytearray()
    for y in range(size):
        rows.append(0)
        for x in range(size):
            cross = (100 <= x < 412 and 225 <= y < 277) or (230 <= x < 282 and 110 <= y < 402)
            rows.extend((240, 234, 255, 255) if cross else (57 + y // 12, 34 + x // 24, 91 + y // 10, 255))
    path.write_bytes(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", size, size, 8, 6, 0, 0, 0)) + chunk(b"IDAT", zlib.compress(rows, 9)) + chunk(b"IEND", b""))

def main():
    assets = ROOT / "build/stage/assets"
    assets.mkdir(parents=True, exist_ok=True)
    for name in ("cat_game", "cat_ps4", "cat_ps3", "cat_switch", "cat_settings"):
        shutil.copyfile(SHELL / "xmb/icons" / (name + ".png"), assets / (name + ".png"))
    shutil.copyfile(SHELL / "fonts/NotoSans-Regular.ttf", assets / "font.ttf")
    shutil.copyfile(SHELL / "fonts/noto_license.txt", assets / "FONT-LICENSE.txt")
    shutil.copyfile(SHELL / "xmb/MaterialIcons-LICENSE.txt", assets / "ICONS-LICENSE.txt")
    shutil.copyfile(ROOT / "THIRD_PARTY.md", assets / "THIRD_PARTY.txt")
    system = ROOT / "build/stage/sce_sys"
    system.mkdir(parents=True, exist_ok=True)
    png(system / "icon0.png")

if __name__ == "__main__":
    main()
