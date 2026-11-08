# emu-sync — keep our emulator forks current without clobbering our tweaks

We fork several emulators (RPCS3, shadPS4, shadNet, rpcn, open-bitdemon). Each fork's `origin` **is** the
upstream project; our changes live on a work branch (`uscl`, or `master` for rpcn) and/or as uncommitted
working-tree edits. `emu-sync.py` detects when upstream's main branch moves and tells us whether the new commits
are safe to pull or touch something we changed and need a human.

## Usage

```
python3 tools/emu-sync/emu-sync.py check            # fetch upstream, report per fork (default)
python3 tools/emu-sync/emu-sync.py apply            # pull the CLEAN forks; skip + list the rest
python3 tools/emu-sync/emu-sync.py apply --build    # ...and run each applied fork's build command
          [--no-fetch] [--json] [name|all]
```

## What each state means

- **✓ up-to-date** — upstream has nothing new.
- **↓ clean** — upstream has new commits, and *none of them touch a file we changed*. Safe to auto-apply.
- **⚠ needs-review** — upstream changed a file we also altered; the fork is left untouched and the overlapping
  files (and the upstream commits that touched them) are listed, so a human merges those by hand.
- **⚑ unshallow-needed** — the fork is a shallow clone, so there's no merge-base to reason from. Run the printed
  `git fetch --unshallow` once, then sync works normally.

"Our lines" = files that differ between our branch and upstream's merge-base **plus** any uncommitted edits in the
working tree (the forks often keep tweaks uncommitted). Detection never modifies a working tree — it uses
`git merge-tree` / `git diff` only. `apply` records a backup ref (`refs/emu-sync/backup/<ts>`) before merging, and
git itself refuses if a pull would overwrite an uncommitted edit, so a clean pull can never silently eat our work.

## Config

The `FORKS` list in `emu-sync.py`, overridable by `~/.config/uscl/emu-sync.json` (same shape): each entry is
`{name, path, ours, upstream, build}` where `upstream` is the origin branch to track and `build` is an optional
shell command to rebuild the fork after a clean pull.

## Scheduling (auto-detect)

Run `check` on a timer and surface anything that isn't ✓. Example user systemd timer:

```ini
# ~/.config/systemd/user/emu-sync.service       # ~/.config/systemd/user/emu-sync.timer
[Unit]                                           [Unit]
Description=USCL emulator upstream check         Description=daily USCL emulator upstream check
[Service]                                        [Timer]
Type=oneshot                                     OnCalendar=daily
ExecStart=/usr/bin/python3 %h/src/ps3-steam-np/tools/emu-sync/emu-sync.py check
                                                 Persistent=true
                                                 [Install]
                                                 WantedBy=timers.target
```

`systemctl --user enable --now emu-sync.timer`. A non-zero exit means at least one fork needs attention.
