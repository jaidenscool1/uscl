#!/bin/sh
# Samples the emulator's thread stacks during the race: waits for the bench's race-start screenshot.
B=$(dirname "$0")/../../bench
until [ -f $B/bench-$1.png ]; do sleep 1; done
sleep 20
P=$(pgrep -x eglantine)
i=0
while [ $i -lt 160 ]; do
  eu-stack -p $P -n 24 > $2/s$i.txt 2>/dev/null
  i=$((i+1)); sleep 0.25
done
echo sampled $i
