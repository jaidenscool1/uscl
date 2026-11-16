#!/bin/sh
# pin.sh <back cpus> <other cpus> <seconds>: pin the running eglantine's GPUBack thread to <back cpus> and
# every other thread to <other cpus>, re-applied every 5 s (threads appear later), for <seconds>.
end=$(( $(date +%s) + $3 ))
while [ "$(date +%s)" -lt "$end" ]; do
  p=$(pgrep -x eglantine | head -1)
  if [ -n "$p" ]; then
    for t in /proc/$p/task/*; do
      tid=${t##*/}; name=$(cat "$t/comm" 2>/dev/null)
      if [ "$name" = GPUBack ]; then taskset -pc "$1" "$tid" >/dev/null 2>&1; else taskset -pc "$2" "$tid" >/dev/null 2>&1; fi
    done
  fi
  sleep 5
done
