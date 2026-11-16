#!/usr/bin/env python3
import argparse
import hashlib
import json
import os
import shutil
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]
CONTENT_ID = "IV0000-USCL00001_00-USCLNATIVEPS4001"

def main():
    p = argparse.ArgumentParser()
    p.add_argument("--sdk", type=Path, required=True)
    p.add_argument("--pkgtool", type=Path, required=True)
    args = p.parse_args()
    stage = ROOT / "build/stage"
    tool = args.pkgtool.resolve()
    env = dict(os.environ, DOTNET_SYSTEM_GLOBALIZATION_INVARIANT="1")
    # SDK-supplied empty modules, not console-extracted firmware files.
    sample = args.sdk.resolve() / "samples/hello_world"
    for relative in ("sce_module/libc.prx", "sce_module/libSceFios2.prx", "sce_sys/about/right.sprx"):
        output = stage / relative
        output.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(sample / relative, output)
    def run(*command, cwd=stage):
        subprocess.run(list(map(str, command)), cwd=cwd, env=env, check=True)
    sfo = stage / "sce_sys/param.sfo"
    sfo.unlink(missing_ok=True)
    run(tool, "sfo_new", sfo)
    for key, kind, capacity, value in (
        ("APP_TYPE", "Integer", 4, "1"), ("APP_VER", "Utf8", 8, "00.01"),
        ("ATTRIBUTE", "Integer", 4, "2"), ("CATEGORY", "Utf8", 4, "gd"),
        ("CONTENT_ID", "Utf8", 48, CONTENT_ID), ("DOWNLOAD_DATA_SIZE", "Integer", 4, "0"),
        ("SYSTEM_VER", "Integer", 4, "0"), ("TITLE", "Utf8", 128, "USCL Native Preview"),
        ("TITLE_ID", "Utf8", 12, "USCL00001"), ("VERSION", "Utf8", 8, "00.01"),
    ):
        run(tool, "sfo_setentry", sfo, key, "--type", kind, "--maxsize", capacity, "--value", value)
    files = sorted(str(f.relative_to(stage)) for f in stage.rglob("*") if f.is_file() and f.suffix != ".gp4")
    run(args.sdk.resolve() / "bin/linux/create-gp4", "-out", "project.gp4", "--content-id=" + CONTENT_ID, "--files", " ".join(files))
    run(tool, "pkg_build", "project.gp4", ROOT / "build")
    pkg = ROOT / "build" / (CONTENT_ID + ".pkg")
    digest = hashlib.sha256(pkg.read_bytes()).hexdigest()
    (ROOT / "build/SHA256SUMS").write_text(digest + "  " + pkg.name + "\n")
    (ROOT / "build/build-manifest.json").write_text(json.dumps({
        "source_commit": subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip(),
        "working_tree_modified": bool(subprocess.check_output(["git", "status", "--porcelain"], cwd=ROOT, text=True).strip()),
        "title_id": "USCL00001", "content_id": CONTENT_ID, "sha256": digest,
        "console_tested": False, "pc_feature_parity": False,
        "scope": "Native application and local PS3/Switch game launcher using separate native emulator applications",
    }, indent=2) + "\n")
    print(pkg)

if __name__ == "__main__":
    main()
