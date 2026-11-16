# emu-sync — keep our emulator forks current without clobbering our tweaks

The default configuration checks separate development clones for RPCS3 (`master`), shadPS4 (`main`),
shadNet (`main`), RPCN (`master`), open-bitdemon (`main`) and Eglantine (`nx2-experimental`). Each clone's
`origin` is upstream; our work is on `uscl` (`master` for RPCN) and can include uncommitted edits.
Paths are machine-specific; override them in the config below before using another checkout.

Eden, Dolphin, Azahar, melonDS and Skate3BlazeServer are not in the default checker configuration. The emulator
patch sets and pinned build recipes are listed in the [root README](../../README.md#fork-maintenance).
Cemu and Xenia have no USCL patch set here. This tool checks and optionally merges development clones;
it does not update repository patch files or installer pins, publish releases, or install player updates.

## Usage

```
python3 tools/emu-sync/emu-sync.py check            # fetch upstream, report per fork (default)
python3 tools/emu-sync/emu-sync.py apply            # pull the CLEAN forks; skip + list the rest
python3 tools/emu-sync/emu-sync.py apply --build    # ...and run each applied fork's build command
          [--no-fetch] [--json] [name|all]
```

## What each state means

- **✓ up-to-date** — upstream has nothing new.
- **↓ clean** — upstream has new commits, and *none of them touch a file we changed*. Eligible for `apply`; still requires build and behavior verification.
- **⚠ needs-review** — upstream changed a file we also altered; the fork is left untouched and the overlapping
  files (and the upstream commits that touched them) are listed, so a human merges those by hand.
- **missing / error** — clone unavailable or a Git command failed; inspect the report.
- **no-base** — histories have no common ancestor; reconcile them manually.
- **⚑ unshallow-needed** — the fork is a shallow clone, so there's no merge-base to reason from. Run the printed
  `git fetch --unshallow` once, then sync works normally.

"Our lines" = files that differ between our branch and upstream's merge-base **plus** any uncommitted edits in the
working tree (the forks often keep tweaks uncommitted). Detection never modifies a working tree — it uses
`git diff` and history queries (plus fetch unless `--no-fetch`). `apply` records a backup ref (`refs/emu-sync/backup/<ts>`) before merging, and
git itself refuses if a pull would overwrite an uncommitted edit, so overlapping uncommitted edits block the merge. A clean classification is not proof of runtime compatibility.

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
ExecStart=/usr/bin/python3 %h/src/uscl-ps4/tools/emu-sync/emu-sync.py check
                                                 Persistent=true
                                                 [Install]
                                                 WantedBy=timers.target
```

`systemctl --user enable --now emu-sync.timer`. Review the reported states: the current script does not return
a non-zero exit for every failure (for example, `missing` and `error`). JSON output also exits zero.
