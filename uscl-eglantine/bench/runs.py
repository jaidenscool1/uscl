#!/usr/bin/env python3
"""Run labelled configurations one after another: each boots the perf build with extra env vars,
drives into a race (bench.py), saves the log, stops the emulator.
usage: runs.py label:VAR=1,VAR2=2 [label2:...]   (label alone = no extra env)"""
import os, shutil, signal, subprocess, sys, time

T = os.path.dirname(os.path.abspath(__file__))
BIN = os.environ.get("RUN_BIN", "/home/main/src/eglantine/.claude/worktrees/perf/build/bin/eglantine")
LOG = "/home/main/.config/uscl/uscl/switch/data/eglantine/log/eden_log.txt"

def stuck_note(pid):
    """The watchdog's verdict for this emulator (launch.py), or ''."""
    path = os.path.join(T, f"STUCK-{pid}.txt")
    return open(path).readline().strip() if os.path.exists(path) else ""

for spec in sys.argv[1:]:
    label, _, assigns = spec.partition(":")
    env = dict(os.environ)
    for pair in filter(None, assigns.split(",")):
        name, _, value = pair.partition("=")
        env[name] = value
    game = subprocess.Popen([sys.executable, "-I", f"{T}/launch.py", BIN], env=env,
                            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    time.sleep(5)
    out = subprocess.run([sys.executable, "-I", f"{T}/bench.py", label], capture_output=True, text=True)
    print(label, out.stdout.strip().splitlines()[-1] if out.stdout.strip() else out.stderr[-300:], flush=True)
    if out.stderr.strip():
        print("  chunks:", [s["fps"] for s in __import__("json").loads(out.stderr.strip().splitlines()[-1])]
              if out.stderr.strip().splitlines()[-1].startswith("[") else out.stderr[-200:], flush=True)
    if stuck_note(game.pid):
        print(label, stuck_note(game.pid), f"(STUCK-{game.pid}.txt/.png)", flush=True)
    shutil.copy(LOG, f"{T}/run-{label}.log")
    game.send_signal(signal.SIGTERM)
    try:
        game.wait(45)
    except subprocess.TimeoutExpired:
        game.kill()
        game.wait()
    # The emulator can outlive launch.py: stop any left (only ones this job launched use this binary).
    for _ in range(25):
        pids = subprocess.run(["pgrep", "-f", "-x", f"{BIN} .*"], capture_output=True, text=True).stdout.split()
        if not pids:
            break
        for pid in pids:
            os.kill(int(pid), signal.SIGTERM if _ < 22 else signal.SIGKILL)
        time.sleep(2)
    time.sleep(3)
