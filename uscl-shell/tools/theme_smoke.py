#!/usr/bin/env python3
"""Load the packaged QML/resources for every theme using isolated preferences."""
import os
from pathlib import Path
import subprocess
import sys

THEMES = ("ps3", "ps4", "ps5", "x360classic", "x360", "xboxone", "switch", "switch2")
ERRORS = ("ReferenceError", "TypeError", "Unable to assign", "Binding loop", "failed to load component", "Cannot override", "Cannot open:", "Cannot specify", "keyboard flow failed")


def main():
    if len(sys.argv) != 2:
        raise SystemExit("usage: python theme_smoke.py /path/to/uscl")
    binary = str(Path(sys.argv[1]).resolve())
    env = dict(os.environ, QT_QPA_PLATFORM="offscreen", QT_QUICK_BACKEND="software", QT_FORCE_STDERR_LOGGING="1", QT_LOGGING_TO_CONSOLE="1")
    failed = False
    for theme in THEMES:
        run = subprocess.run([binary, "--windowed", "--qml-smoke-test", "--theme", theme], env=env, capture_output=True, text=True, timeout=30)
        log = run.stdout + run.stderr
        errors = [line for line in log.splitlines() if any(marker in line for marker in ERRORS)]
        ok = run.returncode == 0 and not errors and "qml-smoke-test: loaded " + theme in log and "keyboard flow passed" in log and "offline changelog passed" in log
        print(f"{theme}: {'PASS' if ok else 'FAIL'}")
        if not ok:
            print(f"exit code: {run.returncode}")
            print(log)
            failed = True
    return int(failed)


if __name__ == "__main__":
    sys.exit(main())
