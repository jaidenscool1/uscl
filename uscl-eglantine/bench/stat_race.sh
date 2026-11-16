#!/bin/sh
# stat_race.sh label [VAR=val,...]: bench race on RUN_BIN; per-thread CPU utilization over 10 s of racing.
T=$(dirname "$(readlink -f "$0")"); L=$1; shift
PERF="env LD_LIBRARY_PATH=$T/llvmpkg/p/usr/lib $T/llvmpkg/p/usr/bin/perf"
rm -f "$T/bench-$L.png"
python3 -I "$T/runs.py" "$L:$1" > "$T/stat-$L.runs" 2>&1 &
until [ -f "$T/bench-$L.png" ]; do sleep 2; done
sleep 34
$PERF stat --per-thread -e task-clock -p "$(pgrep -x eglantine)" -- sleep 10 2>&1 | awk '$2 ~ /^[0-9.,]+$/ {gsub(",","",$2); if ($2 > 300) printf "%-22s %5.0f%%\n", $1, $2/100}' | sort -k2 -nr | head -16
wait
grep chunks "$T/stat-$L.runs"
