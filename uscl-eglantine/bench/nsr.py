#!/usr/bin/env python3
"""Boot Switch Sports Resort, take a screenshot every STEP seconds (pressing A after each from
--press-after on), report presents/fps, then stop. usage: nsr.py label TOTAL STEP [--press-after N]"""
import json, os, signal, subprocess, sys, time

T = os.path.dirname(os.path.abspath(__file__))
BIN = os.environ.get("RUN_BIN", "/home/main/src/eglantine/build-uscl/bin/eglantine")
DBG = "/home/main/src/eglantine/tools/nx2/debug_mcp.py"
GAME = os.environ["NSR_GAME"]  # Switch Sports Resort exefs/main
LOG = "/home/main/.config/uscl/uscl/switch/data/eglantine/log/eden_log.txt"
def stuck_note(pid):
    """The watchdog's verdict for this emulator (launch.py), or ''."""
    path = os.path.join(T, f"STUCK-{pid}.txt")
    return open(path).readline().strip() if os.path.exists(path) else ""


label, total, step = sys.argv[1], float(sys.argv[2]), float(sys.argv[3])
press_after = float(sys.argv[sys.argv.index("--press-after") + 1]) if "--press-after" in sys.argv else 1e9


def call(method, out=None, **params):
    cmd = [sys.executable, "-I", DBG, "--call", json.dumps({"method": method, "params": params})]
    if out:
        cmd += ["--out", out]
    try:
        r = subprocess.run(cmd, capture_output=True, text=True, timeout=120).stdout
        return json.loads(r) if r.strip().startswith("{") else r
    except Exception as e:
        return {"error": str(e)[:100]}


proc = subprocess.Popen([sys.executable, "-I", f"{T}/launch.py", BIN], env=dict(os.environ, USCL_GAME=GAME),
                        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
start = time.time()
n = 0
while time.time() - start < total and proc.poll() is None:
    tick = time.time()
    while time.time() - tick < step and proc.poll() is None:
        time.sleep(1)
    if proc.poll() is not None:
        break
    n += 1
    st = call("status")
    st = st.get("result", st) if isinstance(st, dict) else {}
    shot = f"{T}/{label}-{n:02d}.png"
    call("screenshot", out=shot)
    print(f"t={time.time() - start:5.0f}s presents={st.get('presents')} fps={round(st.get('fps', 0) or 0, 1)} -> {os.path.basename(shot)}", flush=True)
    if time.time() - start >= press_after:
        call("input_sequence", steps=[{"frames": 8, "buttons": ["A"]}, {"frames": 30}])
ended = proc.poll()  # launch.py execs the emulator: this is its exit status
print(stuck_note(proc.pid) or (f"CRASHED: emulator exited with {ended}" if ended is not None else "finished (not stuck)"),
      f"[{time.time() - start:.0f} s]", flush=True)
proc.send_signal(signal.SIGTERM)
try:
    proc.wait(45)
except subprocess.TimeoutExpired:
    proc.kill()
for i in range(25):
    pids = subprocess.run(["pgrep", "-f", "-x", f"{BIN} .*"], capture_output=True, text=True).stdout.split()
    if not pids:
        break
    for pid in pids:
        os.kill(int(pid), signal.SIGTERM if i < 22 else signal.SIGKILL)
    time.sleep(2)
import shutil
shutil.copy(LOG, f"{T}/run-{label}.log")  # after exit: the log is buffered while it runs
