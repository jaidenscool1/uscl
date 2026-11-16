#!/bin/sh
# prof_race.sh label [VAR=val,...]: bench race on RUN_BIN with dynarmic's perf map; perf-samples 20 s of racing.
T=$(dirname "$(readlink -f "$0")"); L=$1; shift
PERF="env LD_LIBRARY_PATH=$T/p/usr/lib $T/p/usr/bin/perf"
rm -f "$T/bench-$L.png"
PERF_BUILDID_DIR=/tmp python3 -I "$T/runs.py" "$L:$1" > "$T/prof-$L.runs" 2>&1 &
until [ -f "$T/bench-$L.png" ]; do sleep 2; done
sleep 32
$PERF record -q -F 1999 ${PERF_EXTRA} -p "$(pgrep -x eglantine)" -o "$T/perf-$L.data" -- sleep 20 2>/dev/null
wait
cat "$T/prof-$L.runs"
