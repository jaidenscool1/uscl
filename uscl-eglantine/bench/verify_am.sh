#!/usr/bin/env bash
# git am the USCL patch folder onto a clean origin/nx2-experimental; the tree must equal uscl.
set -euo pipefail
SCRATCH=/home/main/.claude-cli2/jobs/06fda08d/tmp/egl-export
PATCHES=/home/main/src/ps3-steam-np/.claude/worktrees/switch2-eglantine/uscl-eglantine/patches
cd "$SCRATCH"
git checkout -q --detach origin/nx2-experimental
git am -q "$PATCHES"/*.patch
echo "applied $(git rev-list --count origin/nx2-experimental..HEAD) patches; diff vs uscl:"
git diff --stat uscl-opt HEAD
echo "(end of diff)"
