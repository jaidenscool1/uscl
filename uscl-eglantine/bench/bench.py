#!/usr/bin/env python3
"""MKW race benchmark through Eglantine's debug server.
usage: bench.py <label> [--skip-menus]
Boots must already be running with EDEN_NX2_DEBUG_SERVER=127.0.0.1:6545. Drives title -> Grand Prix
with frame-counted input, holds accelerate for RACE_FRAMES, samples fps/CPU/GPU every 5 s."""
import json, os, statistics, subprocess, sys, threading, time

DBG = "/home/main/src/eglantine/tools/nx2/debug_mcp.py"
RACE_FRAMES = int(os.environ.get("BENCH_FRAMES", "2400"))


def call(method, **params):
    out = subprocess.run([sys.executable, "-I", DBG, "--call", json.dumps({"method": method, "params": params})],
                         capture_output=True, text=True, timeout=900)
    try:
        return json.loads(out.stdout)
    except ValueError:
        return {"error": (out.stdout + out.stderr).strip()[:300]}


def status(window=0):
    r = call("status", fps_window_ms=window) if window else call("status")
    return r.get("result", r) if isinstance(r, dict) else r


def proc_cpu_seconds(pid):
    with open(f"/proc/{pid}/stat") as f:
        fields = f.read().rsplit(")", 1)[1].split()
    return (int(fields[11]) + int(fields[12])) / os.sysconf("SC_CLK_TCK")


def gpu():
    out = subprocess.run(["nvidia-smi", "--query-gpu=utilization.gpu,memory.used", "--format=csv,noheader,nounits"],
                         capture_output=True, text=True).stdout.split(",")
    return int(out[0]), int(out[1])


def wait_frames(n, timeout=600):
    end = time.time() + timeout
    while time.time() < end:
        alive = subprocess.run(["pgrep", "-x", "eglantine"], capture_output=True, text=True).stdout.split()
        if not alive or all(open(f"/proc/{p}/stat").read().split(") ")[1][0] == "Z" for p in alive if os.path.exists(f"/proc/{p}/stat")):
            raise SystemExit("emulator exited")
        s = status()
        frames = s.get("presents", 0) if isinstance(s, dict) else 0
        if isinstance(frames, int) and frames >= n:
            return s
        time.sleep(2)
    raise SystemExit(f"no {n} frames: {s}")


def seq(*steps):
    r = call("input_sequence", steps=[dict(frames=f, **({"buttons": b} if b else {})) for b, f in steps], timeout_s=900)
    if "error" in r:
        print("input error:", r, file=sys.stderr)


def main():
    label = sys.argv[1]
    pid = int(subprocess.run(["pgrep", "-x", "eglantine"], capture_output=True, text=True).stdout)
    first = wait_frames(300)
    print("status:", json.dumps(first)[:400], file=sys.stderr)
    if "--skip-menus" not in sys.argv:
        seq((["L", "R"], 10), (None, 400), (["L", "R"], 10), (None, 420),
            *[step for _ in range(int(os.environ.get("BENCH_A", "6"))) for step in ((["A"], 8), (None, 200))], (None, 1500))
    shot = os.path.join(os.path.dirname(os.path.abspath(__file__)), f"bench-{label}.png")
    subprocess.run([sys.executable, "-I", DBG, "--call", json.dumps({"method": "screenshot", "params": {}}), "--out", shot],
                   capture_output=True, timeout=120)
    samples = []
    start = time.time()
    for _ in range(RACE_FRAMES // 300):
        s0, c0 = status(), proc_cpu_seconds(pid)
        seq((["A"], 300))
        s1, c1 = status(), proc_cpu_seconds(pid)
        if os.environ.get("BENCH_CHUNK_SHOTS"):  # a frame per chunk: the countdown is in one of the first
            subprocess.run([sys.executable, "-I", DBG, "--call", json.dumps({"method": "screenshot", "params": {}}),
                            "--out", shot.replace(".png", f"-c{len(samples)}.png")], capture_output=True, timeout=120)
        dt = s1["uptime_s"] - s0["uptime_s"]
        util, vram = gpu()
        samples.append({"fps": round((s1["presents"] - s0["presents"]) / dt, 1), "cpu_cores": round((c1 - c0) / dt, 2),
                        "gpu": util, "vram": vram})
    subprocess.run([sys.executable, "-I", DBG, "--call", json.dumps({"method": "screenshot", "params": {}}),
                    "--out", shot.replace(".png", "-end.png")], capture_output=True, timeout=120)
    # BENCH_TRIGGER=<file>: the passthrough backend's snapshot (EDEN_NX2_PASSTHROUGH_SNAP=-1:...:<file>) of the
    # frame next to the end screenshot; waits until the backend took it (it removes <file>.now).
    if trigger := os.environ.get("BENCH_TRIGGER"):
        open(trigger + ".now", "w").close()
        for _ in range(60):
            if not os.path.exists(trigger + ".now"):
                break
            time.sleep(1)
    fps = [x["fps"] for x in samples]
    summary = {"label": label, "race_s": round(time.time() - start, 1), "samples": len(samples),
               "fps_mean": round(statistics.mean(fps), 1), "fps_min": min(fps),
               "cpu_cores_mean": round(statistics.mean(x["cpu_cores"] for x in samples), 2),
               "gpu_util_last": samples[-1]["gpu"], "vram_max": max(x["vram"] for x in samples)}
    print(json.dumps(summary))
    print(json.dumps(samples), file=sys.stderr)


main()
