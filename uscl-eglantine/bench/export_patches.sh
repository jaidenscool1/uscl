#!/usr/bin/env bash
# Regenerate USCL's uscl-eglantine/patches: cherry-pick our commits onto origin/nx2-experimental in a
# scratch worktree, check the result equals branch uscl, format-patch into OUT.
# A conflicted file is taken from uscl (where the merge with upstream resolved it) when no later
# commit of ours touches it; otherwise the script stops.
set -euo pipefail
REPO=/home/main/src/eglantine
SCRATCH=/home/main/.claude-cli2/jobs/06fda08d/tmp/egl-export
OUT=/home/main/.claude-cli2/jobs/06fda08d/tmp/egl-patches-new
COMMITS=(9c4892b8a 4f9320815 437a33bdc 88ecb9c6d fc973acd1 d60211910 e0be717d7 c79d9f120 3820e2075 6ba5b5eeb 58089d11d)
cd "$REPO"
git worktree remove --force "$SCRATCH" 2>/dev/null || true
git worktree add --detach "$SCRATCH" origin/nx2-experimental >/dev/null
cd "$SCRATCH"
for i in "${!COMMITS[@]}"; do
    c=${COMMITS[$i]}
    if git cherry-pick "$c" >/dev/null 2>&1; then
        continue
    fi
    later=("${COMMITS[@]:$((i + 1))}")
    for f in $(git diff --name-only --diff-filter=U); do
        for l in "${later[@]}"; do
            if git show --name-only --format= "$l" | grep -qx "$f"; then
                echo "conflict in $f ($c) and later commit $l touches it"; exit 1
            fi
        done
        echo "  $c: $f taken from uscl"
        git checkout uscl -- "$f"
        git add "$f"
    done
    GIT_EDITOR=true git cherry-pick --continue >/dev/null
done
echo "diff vs uscl (must be empty):"
git diff --stat uscl HEAD
rm -rf "$OUT" && mkdir -p "$OUT"
git format-patch -q -o "$OUT" origin/nx2-experimental..HEAD
ls "$OUT"
echo "base: $(git rev-parse --short origin/nx2-experimental) $(git log -1 --format=%cs origin/nx2-experimental)"
