#!/usr/bin/env python3
"""USCL emulator upstream-sync: detect when a forked emulator's upstream (its origin's main branch) has new
commits, and bring them in WITHOUT disturbing our own changes -- auto-merging when upstream didn't touch any
line we altered, and flagging the rest for manual review.

Each fork's `origin` IS the upstream project (e.g. rpcs3-uscl -> RPCS3/rpcs3.git); our work lives on a branch
(`uscl`, or `master` for rpcn). "Our lines" = whatever differs between our branch and the upstream branch.

Detection never touches a working tree: it uses `git merge-tree --write-tree` to merge in memory and report
conflicts. A clean trial-merge means upstream changed nothing we changed -> safe to auto-apply. A conflict means
upstream edited a file region we also edited -> manual review, and we leave the fork untouched.

usage:
  emu-sync.py check [name|all]          # fetch upstream, report new commits + clean/needs-review (default)
  emu-sync.py apply [name|all]          # merge upstream into our branch for the CLEAN forks; skip + report conflicts
  emu-sync.py apply --build [name|all]  # ...and run each applied fork's build command
Options: --no-fetch (skip network), --json (machine-readable report to stdout).

Config: the FORKS list below, overridden by ~/.config/uscl/emu-sync.json if present (same shape).
"""
import json, os, subprocess, sys, time

FORKS = [
    {"name": "rpcs3",   "path": "/home/main/src/rpcs3-uscl",   "ours": "uscl",   "upstream": "master", "build": None},
    {"name": "shadps4", "path": "/home/main/src/shadps4-uscl", "ours": "uscl",   "upstream": "main",   "build": None},
    {"name": "shadnet", "path": "/home/main/src/shadnet-uscl", "ours": "uscl",   "upstream": "main",   "build": None},
    {"name": "rpcn",    "path": "/home/main/src/rpcn-uscl",    "ours": "master", "upstream": "master", "build": None},
    {"name": "obde",    "path": "/home/main/src/obde-uscl",    "ours": "uscl",   "upstream": "main",   "build": None},
]
CONFIG = os.path.expanduser("~/.config/uscl/emu-sync.json")


def git(path, *args, check=True):
    r = subprocess.run(["git", "-C", path, *args], capture_output=True, text=True)
    if check and r.returncode != 0:
        raise RuntimeError(f"git {' '.join(args)} failed in {path}: {r.stderr.strip()}")
    return r


def load_forks():
    if os.path.exists(CONFIG):
        try:
            return json.load(open(CONFIG))
        except Exception as e:
            print(f"warning: ignoring bad {CONFIG}: {e}", file=sys.stderr)
    return FORKS


def _files(out):
    return {l for l in out.splitlines() if l.strip()}


def assess(fork, fetch=True):
    """Status for one fork, without touching its working tree or branches.

    "Our lines" = files we changed, whether committed on our branch OR still uncommitted in the working tree
    (the forks keep tweaks as working-tree edits). We flag a fork for review when upstream's NEW commits (since
    our merge-base) touch any of those files; otherwise the update is clean. Shallow clones have no merge-base,
    so we can't isolate "new" upstream changes -- we say so and ask for an unshallow rather than guess."""
    p, ours = fork["path"], fork["ours"]
    up = f"origin/{fork['upstream']}"
    st = {"name": fork["name"], "path": p, "ours": ours, "upstream": up}
    if not os.path.isdir(os.path.join(p, ".git")):
        st["state"] = "missing"
        return st
    try:
        if fetch:
            git(p, "fetch", "--quiet", "origin", fork["upstream"])
        st["shallow"] = git(p, "rev-parse", "--is-shallow-repository").stdout.strip() == "true"
        behind = int(git(p, "rev-list", "--count", f"{ours}..{up}").stdout.strip())
        ahead = int(git(p, "rev-list", "--count", f"{up}..{ours}").stdout.strip())
        # our uncommitted edits count as "our lines" too
        wt = {l[3:] for l in git(p, "status", "--porcelain").stdout.splitlines() if l[:2].strip() and l[3:]}
        st.update(new_upstream=behind, ours_ahead=ahead, wt_changes=len(wt),
                  upstream_head=git(p, "log", "-1", "--format=%h %s", up).stdout.strip())

        base = git(p, "merge-base", ours, up, check=False).stdout.strip()
        if not base:
            # no common ancestor: shallow truncation (or unrelated). Can't isolate new upstream work.
            st["state"] = "unshallow-needed" if st["shallow"] else "no-base"
            return st
        if behind == 0:
            st["state"] = "up-to-date"
            return st

        upstream_files = _files(git(p, "diff", "--name-only", f"{base}..{up}").stdout)   # what upstream added
        our_files = wt | _files(git(p, "diff", "--name-only", f"{base}..{ours}").stdout)  # what we changed
        hits = sorted(upstream_files & our_files)
        if not hits:
            st["state"] = "clean"             # upstream's new commits touch nothing we changed
        else:
            st["state"] = "needs-review"
            st["conflicts"] = hits
            # for each shared file: which upstream commits changed it (what a human must reconcile against)
            st["upstream_commits"] = {
                f: git(p, "log", "--oneline", "-3", f"{base}..{up}", "--", f).stdout.splitlines()
                for f in hits}
    except Exception as e:
        st["state"] = "error"
        st["error"] = str(e)
    return st


def apply_clean(fork, build=False):
    """Pull upstream into our branch for a fork whose update is CLEAN (upstream touched none of our files).
    Records a backup ref first (reversible via refs/emu-sync/backup/*). Git itself refuses if the merge would
    overwrite an uncommitted edit, so a dirty tree is safe as long as it stays disjoint from upstream's changes."""
    p = fork["path"]
    up = f"origin/{fork['upstream']}"
    cur = git(p, "rev-parse", "--abbrev-ref", "HEAD").stdout.strip()
    if cur != fork["ours"]:
        return f"skipped (checked out {cur}, not {fork['ours']})"
    git(p, "update-ref", f"refs/emu-sync/backup/{int(time.time())}", fork["ours"])  # reversible
    m = git(p, "merge", "--no-edit", up, check=False)
    if m.returncode != 0:
        git(p, "merge", "--abort", check=False)
        return f"left untouched — git refused (likely an uncommitted edit overlaps): {m.stderr.strip()[:140]}"
    msg = f"merged {up}"
    if build and fork.get("build"):
        b = subprocess.run(fork["build"], shell=True, cwd=p)
        msg += f"; build {'ok' if b.returncode == 0 else 'FAILED'}"
    return msg


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("-")]
    opts = {a for a in sys.argv[1:] if a.startswith("-")}
    cmd = args[0] if args else "check"
    target = args[1] if len(args) > 1 else "all"
    fetch = "--no-fetch" not in opts
    forks = [f for f in load_forks() if target in ("all", f["name"])]
    if not forks:
        print(f"no fork named {target!r}"); return 2

    reports = [assess(f, fetch=fetch) for f in forks]

    if "--json" in opts:
        print(json.dumps(reports, indent=2)); return 0

    icon = {"up-to-date": "✓", "clean": "↓", "needs-review": "⚠", "unshallow-needed": "⚑",
            "no-base": "⚑", "missing": "?", "error": "✗"}
    print(f"USCL emulator upstream sync — {time.strftime('%Y-%m-%d %H:%M')}\n")
    rc = 0
    for st in reports:
        i = icon.get(st["state"], "·")
        tag = f" +{st['wt_changes']} uncommitted" if st.get("wt_changes") else ""
        if st["state"] == "up-to-date":
            print(f"  {i} {st['name']:<9} up to date ({st['ours_ahead']} local commits{tag})")
        elif st["state"] == "clean":
            print(f"  {i} {st['name']:<9} {st['new_upstream']} new upstream commit(s), CLEAN — none touch our "
                  f"files{tag}, auto-appliable (→ {st['upstream_head']})")
        elif st["state"] == "needs-review":
            rc = 1
            print(f"  {i} {st['name']:<9} {st['new_upstream']} new upstream commit(s), NEEDS REVIEW — upstream "
                  f"changed files we altered{tag}:")
            for f in st["conflicts"]:
                ucs = "; ".join(st["upstream_commits"].get(f, [])[:2]) or "(upstream change)"
                print(f"        {f}  ←  {ucs}")
        elif st["state"] in ("unshallow-needed", "no-base"):
            rc = 1
            why = ("shallow clone — run `git -C %s fetch --unshallow` so our merge-base is visible" % st["path"]
                   if st["state"] == "unshallow-needed" else "no common ancestor with upstream")
            print(f"  {i} {st['name']:<9} {st['new_upstream']} behind but can't isolate changes: {why}")
        else:
            print(f"  {i} {st['name']:<9} {st['state']}: {st.get('error','')}")

    if cmd == "apply":
        print("\napplying clean updates:")
        for f, st in zip(forks, reports):
            if st["state"] == "clean":
                print(f"  {f['name']}: {apply_clean(f, build='--build' in opts)}")
            elif st["state"] == "needs-review":
                print(f"  {f['name']}: left for manual review ({len(st['conflicts'])} conflicting file(s))")
    else:
        pending = [s["name"] for s in reports if s["state"] == "clean"]
        if pending:
            print(f"\n{len(pending)} fork(s) can auto-update: run `emu-sync.py apply` "
                  f"(add --build to rebuild).")
    return rc


if __name__ == "__main__":
    sys.exit(main())
