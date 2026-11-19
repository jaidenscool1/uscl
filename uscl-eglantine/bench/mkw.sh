#!/bin/sh
# mkw.sh <label> <eglantine binary>: boot MKW (launch.py, watchdog on), run the race bench, stop it,
# keep the log as run-<label>.log and count garbage-command errors in it.
T=$(dirname "$(readlink -f "$0")")
L=${USCL_SWITCH_PROFILE:-/home/main/.local/share/uscl/eglantine-test}/data/eglantine/log/eden_log.txt
python3 -I "$T/launch.py" "$2" >/dev/null 2>&1 &
pid=$!
sleep 20
python3 -I "$T/bench.py" "$1"
python3 -I /home/main/src/eglantine/tools/nx2/debug_mcp.py --call '{"method":"screenshot","params":{}}' --out "$T/$1-end.png" >/dev/null 2>&1
kill "$pid" 2>/dev/null
for i in $(seq 45); do kill -0 "$pid" 2>/dev/null || break; sleep 1; done
kill -9 "$pid" 2>/dev/null
for f in "$T"/STUCK-"$pid".txt; do [ -f "$f" ] && head -1 "$f"; done
cp "$L" "$T/run-$1.log"
printf '%s garbage lines: ' "$1"
grep -cE "out of range|Invalid KeplerMemory|Unimplemented engine|nbound subchannel|Invalid MSAA|Unimplemented format" "$T/run-$1.log"
