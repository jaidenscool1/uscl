#!/usr/bin/env python3
"""Read an RPCS3.log and report how a PS3 game does online, and how much a Steam backend could cover.

usage: np-profile.py [RPCS3.log]      (default: ~/.cache/rpcs3/RPCS3.log; boot the game to its menu first)

At boot RPCS3 logs every PSN function the game imports ("sceNp import: [name]"). Which libraries show up
tells us the game's online design:
  sceNpMatching2 (+ its Signaling calls)  rooms + P2P through Sony: a Steam backend can cover all of it
  sceNpBasic / sceNpManager only          PSN for login, invites, presence; matchmaking on the publisher's
                                          own servers: Steam covers friends/invites, the rest needs a
                                          community server for that publisher
"""
import os
import re
import sys

# PSN library -> what Steam could stand in for it ("" = nothing useful)
STEAM_EQUIVALENT = {
    "sceNpMatching2": "lobbies + P2P relay",
    "sceNpMatching": "lobbies (old matching API)",
    "sceNpSignaling": "P2P relay",
    "sceNpBasic": "friends, presence, invites",
    "sceNpScore": "leaderboards",
    "sceNpTus": "",  # per-user cloud data: needs a small server
    "sceNpManager": "sign-in (Steam account)",
    "sceNpLookup": "",
    "sceNpCommerce2": "",
    "sceNpTrophy": "",
    "sceNpUtil": "",
}
LIBS = sorted(STEAM_EQUIVALENT, key=len, reverse=True)  # longest prefix first (sceNpMatching2 before sceNpMatching)


def library(func):
    return next((lib for lib in LIBS if func.startswith(lib)), "sceNp")


def profile(log_text):
    title = re.search(r"SYS: Title: (.+)", log_text)
    serial = re.search(r"SYS: Serial: (\S+)", log_text)
    # e.g. "ppu_loader: **** sceNp2 import: [sceNpMatching2CreateJoinRoom]": the module name varies
    # (sceNp, sceNp2, sceNpTrophy, ...), so match on the function name.
    funcs = sorted(set(re.findall(r"\*\*\*\* \w+ import: \[(sceNp\w+)\]", log_text)))
    libs = {}
    for f in funcs:
        libs.setdefault(library(f), []).append(f)
    if "sceNpMatching2" in libs or "sceNpMatching" in libs:
        verdict = "FULL: matchmaking runs on Sony's services, a Steam backend can cover it"
    elif "sceNpBasic" in libs:
        verdict = "PARTIAL: PSN for login/invites only; matchmaking is on the publisher's own servers"
    elif funcs:
        verdict = "NONE: no PSN multiplayer (store/trophies only)"
    else:
        verdict = "UNKNOWN: no sceNp imports in this log (did the game boot?)"
    return (title.group(1).strip() if title else "?", serial.group(1) if serial else "?", libs, verdict)


def main():
    path = sys.argv[1] if len(sys.argv) > 1 else os.path.expanduser("~/.cache/rpcs3/RPCS3.log")
    with open(path, encoding="utf-8", errors="replace") as f:
        title, serial, libs, verdict = profile(f.read())
    print(f"{title} [{serial}]")
    for lib, fs in sorted(libs.items(), key=lambda kv: -len(kv[1])):
        steam = STEAM_EQUIVALENT.get(lib, "")
        print(f"  {lib:16} {len(fs):3} functions  -> {steam or '(no Steam equivalent)'}")
    print(verdict)


def _selftest():
    imp = lambda mod, fn: f"·! 0:00:00.1 ppu_loader: **** {mod} import: [{fn}]\n"  # real log line format
    mc = ("·! 0:00:00.1 SYS: Title: Minecraft\n·! 0:00:00.1 SYS: Serial: BLUS31426\n"
          + imp("sceNp2", "sceNpMatching2CreateJoinRoom") + imp("sceNp2", "sceNpMatching2SignalingGetConnectionStatus")
          + imp("sceNp", "sceNpBasicSendMessageGui") + imp("sceNp", "sceNpScoreInit"))
    t, s, libs, v = profile(mc)
    assert (t, s) == ("Minecraft", "BLUS31426") and v.startswith("FULL") and len(libs["sceNpMatching2"]) == 2
    skate = "SYS: Title: Skate 3\n" + imp("sceNp", "sceNpBasicRecvMessageCustom") + imp("sceNp", "sceNpManagerGetTicket")
    assert profile(skate)[3].startswith("PARTIAL")
    assert profile("SYS: Title: X\n" + imp("sceNpTrophy", "sceNpTrophyInit"))[3].startswith("NONE")
    assert profile("")[3].startswith("UNKNOWN")
    print("selftest ok")


if __name__ == "__main__":
    _selftest() if sys.argv[1:] == ["--selftest"] else main()
