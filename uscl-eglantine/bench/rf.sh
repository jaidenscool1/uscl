#!/bin/sh
# rf.sh off|global: MKW per-game reactive flushing off (upstream preset) or back to the global setting
C="${USCL_SWITCH_PROFILE:-/home/main/.local/share/uscl/eglantine-test}/config/eglantine/custom/0400C3F00006E000.ini"
cp "/home/main/.claude-cli2/jobs/06fda08d/tmp/mkw-custom.ini.orig" "$C"
if [ "$1" = off ]; then
  sed -i 's/^use_reactive_flushing\\use_global=true$/use_reactive_flushing\\use_global=false\nuse_reactive_flushing\\default=false\nuse_reactive_flushing=false/' "$C"
fi
grep -n "use_reactive_flushing" "$C"
