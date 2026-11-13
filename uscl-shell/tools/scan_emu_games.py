#!/usr/bin/env python3
"""Scan folders for Wii (.wbfs/.rvz/.iso) and 3DS (.cci/.3ds) games and write the USCL emulator-games manifest the
home screen reads (GameModel::loadEmuGames): $XDG_CONFIG_HOME/uscl/emu_games.json (default ~/.config/uscl/).
These games run in Dolphin / Azahar, not RPCS3, and appear in their own console columns on the home screen.

usage: python3 -I uscl-shell/tools/scan_emu_games.py <folder> [<folder> ...]
       (no folders -> the default Wii + 3DS download locations)
"""
import os, sys, json, glob, re

DEFAULTS = ["/home/main/Downloads/GAME_DUMPS/WII", "/home/main/Downloads/organized game dumps/Wii",
            "/home/main/Downloads/organized game dumps/3DS", "/home/main/Downloads/organized game dumps/WiiU",
            "/home/main/Downloads/GAME_DUMPS/XBOX360_GAMES", "/mnt/games/GAME_DUMPS/XBOX360_GAMES"]
WII_EXT = (".wbfs", ".rvz")
TDS_EXT = (".cci", ".3ds", ".cxi")
WIIU_EXT = (".wua", ".rpx", ".wud", ".wux")
EMU = {"Wii": "dolphin", "3DS": "azahar", "WiiU": "cemu", "Xbox360": "xenia"}

def clean_title(name):
    t = re.sub(r"\s*\((USA|USA, [^)]*|En|Fr|Es|De|It|Ja|Ko|Zh|Nl|Pt|Sv|No|Da|Fi|Pl|Ru|Tr|Rev \d|Asia)[^)]*\)", "", name)
    t = t.replace("_", ":").replace(", The", "").replace("  ", " ")
    return re.sub(r"\s+-\s+", ": ", t).strip(" :-")

def main():
    folders = sys.argv[1:] or DEFAULTS
    entries, seen = [], set()
    for folder in folders:
        for p in sorted(glob.glob(os.path.join(folder, "**", "*"), recursive=True)):
            ext = os.path.splitext(p)[1].lower()
            console = ("Wii" if ext in WII_EXT else "3DS" if ext in TDS_EXT
                       else "WiiU" if ext in WIIU_EXT
                       else "Xbox360" if ext == ".iso" and "xbox" in p.lower() else None)
            if not console or p in seen:
                continue
            seen.add(p)
            name = os.path.splitext(os.path.basename(p))[0]
            entries.append({"title": clean_title(name), "console": console, "path": p,
                            "serial": f"{console}-" + re.sub(r"\W", "", name)[:16],
                            "emu": EMU[console]})
    base = os.environ.get("XDG_CONFIG_HOME", os.path.expanduser("~/.config"))
    out = os.path.join(base, "uscl", "emu_games.json")
    os.makedirs(os.path.dirname(out), exist_ok=True)
    json.dump(entries, open(out, "w"), indent=1)
    print(f"wrote {len(entries)} games -> {out}")
    for e in entries:
        print(" ", e["console"], "-", e["title"])

if __name__ == "__main__":
    main()
