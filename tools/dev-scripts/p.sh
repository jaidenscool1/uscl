#!/bin/bash
# p.sh <a|b> <cmd...>: pad command, then a short pause for the game to react
USCL_TEST=$(dirname "$0")/ddl /home/main/src/ps3-steam-np/.claude/worktrees/dev/tools/autotest/pad.sh "$@" >/dev/null; timeout 1.2 tail -f /dev/null
