#!/usr/bin/env python3
"""Exec Eglantine on MKW with USCL's profile, the debug server, and ptrace allowed (for eu-stack).
usage: launch.py [eglantine binary]"""
import ctypes, os, sys

# Bench profile: a copy of the user's config, keys and saves, so runs never touch the real setup (AGENTS.md).
P = os.environ.get("USCL_SWITCH_PROFILE", "/home/main/.local/share/uscl/eglantine-test")
GAME = os.environ["USCL_GAME"]  # the game executable (exefs/main) to boot
binary = sys.argv[1] if len(sys.argv) > 1 else "/home/main/src/eglantine/build-uscl/bin/eglantine"
env = dict(os.environ, XDG_CONFIG_HOME=P + "/config", XDG_DATA_HOME=P + "/data", XDG_CACHE_HOME=P + "/cache",
           QT_QPA_PLATFORM="xcb", EDEN_NX2_DEBUG_SERVER="127.0.0.1:6545")
# PR_SET_PTRACER_ANY survives exec: lets a sibling eu-stack attach despite yama ptrace_scope=1
ctypes.CDLL(None, use_errno=True).prctl(0x59616D61, ctypes.c_ulong(-1).value, 0, 0, 0)
os.chdir(P)
# Watchdog: a forked child polls the debug server. No new present for WATCH_STALL_S (10) seconds once the game
# has drawn, or for WATCH_BOOT_S (90) before its first frame, means stuck: write STUCK-<pid>.txt (+ .png) next
# to this script and kill the emulator, so runners stop at once instead of waiting out their timers.
emu = os.getpid()  # exec keeps the pid
if os.fork() == 0:
    import json, signal, subprocess, time
    T = os.path.dirname(os.path.abspath(__file__))
    DBG = "/home/main/src/eglantine/tools/nx2/debug_mcp.py"
    stall, boot = float(os.environ.get("WATCH_STALL_S", "10")), float(os.environ.get("WATCH_BOOT_S", "90"))
    start = changed = answered = time.time(); last = 0
    silent = float(os.environ.get("WATCH_SILENT_S", "240"))  # silent: the server serves one client, and bench input sequences hold it ~60 s
    def alive():
        try:
            return open(f"/proc/{emu}/stat").read().rsplit(")", 1)[1].split()[0] != "Z"
        except OSError:
            return False
    while alive():
        time.sleep(2)
        # Only an answer counts: under heavy load (boot) the debug server can take >10 s to reply, which
        # says nothing about frames. A stall is an answered, unchanged count; silence counts after `silent` s.
        try:
            out = subprocess.run([sys.executable, "-I", DBG, "--call", '{"method":"status","params":{}}'],
                                 capture_output=True, text=True, timeout=10).stdout
            reply = json.loads(out)
            presents = int(reply["presents"])  # an error reply has none: not an answer
            answered = time.time()
            if reply.get("switch2") is False:
                presents = None  # Switch 1 games present through nvnflinger: the counter stays 0
        except Exception:
            presents = None
        now = time.time()
        if presents is not None and presents != last:
            last, changed = presents, now
        stalled = presents is not None and now - changed > (stall if last > 0 else boot)
        if (stalled or now - answered > silent) and alive():
            log = open(P + "/data/eglantine/log/eden_log.txt", errors="replace").read().splitlines()[-40:]
            with open(f"{T}/STUCK-{emu}.txt", "w") as f:
                f.write(f"STUCK: no new present for {now - changed:.0f} s at presents={last} (debug server answered {now - answered:.0f} s ago), {now - start:.0f} s after launch\n")
                f.write("\n".join(log) + "\n")
            try:  # a hung emulator's debug server may not answer: the kill below must still happen
                subprocess.run([sys.executable, "-I", DBG, "--call", '{"method":"screenshot","params":{}}',
                                "--out", f"{T}/STUCK-{emu}.png"], capture_output=True, timeout=15)
            except Exception:
                pass
            os.kill(emu, signal.SIGTERM)
            for _ in range(45):
                time.sleep(1)
                if not alive():
                    break
            else:
                os.kill(emu, signal.SIGKILL)
            break
    os._exit(0)
os.execve(binary, [binary, "-f", "-g", GAME], env)
