#!/bin/sh
# ab_bins.sh <labelA> <binA> <labelB> <binB> [pairs]: interleaved long MKW races; prints racing mean (chunks[5:]).
T=$(dirname "$(readlink -f "$0")"); n=${5:-2}
for i in $(seq "$n"); do
  for side in A B; do
    if [ $side = A ]; then L=$1$i; B=$2; else L=$3$i; B=$4; fi
    RUN_BIN=$B BENCH_FRAMES=${BENCH_FRAMES:-6000} python3 -I "$T/runs.py" "$L" 2>&1 | grep -E "chunks|STUCK" | sed "s/^/$L /"
  done
done | python3 -c "
import sys,ast
for l in sys.stdin:
    if 'chunks:' in l:
        lab=l.split()[0]; c=ast.literal_eval(l.split('chunks:')[1].strip()); r=c[5:]
        print(lab, 'racing', round(sum(r)/len(r),1), 'min', min(r), 'n', len(r), flush=True)
    else: print(l.strip(), flush=True)"
