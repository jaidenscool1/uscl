#!/usr/bin/env python3
"""Idle test: boot (launch.py) with extra env, no input, poll presents every 5 s for SECS seconds.
Reports whether presents still advance at the end (the attract-movie-end freeze).
usage: idle.py label [VAR=val ...]   env: RUN_BIN, IDLE_SECS (default 200), USCL_GAME"""
import json, os, signal, subprocess, sys, time
T = os.path.dirname(os.path.abspath(__file__))
DBG = "/home/main/src/eglantine/tools/nx2/debug_mcp.py"
BIN = os.environ.get("RUN_BIN", "/home/main/src/eglantine/.claude/worktrees/perf/build-lto/bin/eglantine")
LOG = "/home/main/.config/uscl/uscl/switch/data/eglantine/log/eden_log.txt"
def stuck_note(pid):
    """The watchdog's verdict for this emulator (launch.py), or ''."""
    path = os.path.join(T, f"STUCK-{pid}.txt")
    return open(path).readline().strip() if os.path.exists(path) else ""


label, extra = sys.argv[1], sys.argv[2:]
env = dict(os.environ, **dict(a.split("=", 1) for a in extra))
game = subprocess.Popen([sys.executable, "-I", f"{T}/launch.py", BIN], env=env,
                        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
start, rows = time.time(), []
if os.environ.get("IDLE_MENU"):
    # Into the Grand Prix menu (bench.py's sequence without the last confirm), then idle there:
    # MKW plays its attract movie after ~100 s in a menu.
    while time.time() - start < 120:
        time.sleep(3)
        try:
            out = subprocess.run([sys.executable, "-I", DBG, "--call", '{"method":"status","params":{}}'],
                                 capture_output=True, text=True, timeout=10).stdout
            if json.loads(out).get("presents", 0) >= 300:
                break
        except Exception:
            pass
    steps = [(["L", "R"], 10), (None, 400), (["L", "R"], 10), (None, 420)] + \
            [s for _ in range(int(os.environ.get("IDLE_A", "5"))) for s in ((["A"], 8), (None, 200))]
    subprocess.run([sys.executable, "-I", DBG, "--call", json.dumps({"method": "input_sequence", "params": {
        "steps": [dict(frames=f, **({"buttons": b} if b else {})) for b, f in steps], "timeout_s": 300}})],
        capture_output=True, text=True, timeout=400)
    start = time.time()  # idle time counts from the menu
while time.time() - start < float(os.environ.get("IDLE_SECS", "200")):
    time.sleep(5)
    try:
        out = subprocess.run([sys.executable, "-I", DBG, "--call", '{"method":"status","params":{}}'],
                             capture_output=True, text=True, timeout=10).stdout
        rows.append((round(time.time() - start), json.loads(out).get("presents")))
    except Exception:
        rows.append((round(time.time() - start), None))
    if game.poll() is not None:
        break
subprocess.run([sys.executable, "-I", DBG, "--call", '{"method":"screenshot","params":{}}', "--out",
                f"{T}/idle-{label}.png"], capture_output=True, timeout=30)
last = [p for _, p in rows[-4:]]
alive = len(set(last)) > 1 and None not in last
print(label, stuck_note(game.pid) or ("ADVANCING" if alive else "STALLED"), rows[-8:], flush=True)
import shutil; shutil.copy(LOG, f"{T}/idle-{label}.log")
game.send_signal(signal.SIGTERM)
try:
    game.wait(15)
except subprocess.TimeoutExpired:
    game.kill()
for _ in range(10):
    pids = subprocess.run(["pgrep", "-f", "-x", f"{BIN} .*"], capture_output=True, text=True).stdout.split()
    if not pids:
        break
    for p in pids:
        os.kill(int(p), signal.SIGKILL if _ > 4 else signal.SIGTERM)
    time.sleep(2)
