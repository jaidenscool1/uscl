#!/bin/sh
# Build the passthrough tools (nvrm_probe, pt_replay) into <out dir> (default: here) against the installed
# driver's SDK headers plus mesa's class/QMD headers.
S=${NV_SRC:-/usr/src/nvidia-615.78.08/src}
M=${MESA_CLASSES:-$HOME/src/ext/mesa-nouveau/src/nouveau/headers/nvidia/classes}
for t in nvrm_probe pt_replay; do
  g++ -std=c++17 -O1 -Wall -Wno-missing-field-initializers -I "$S/common/sdk/nvidia/inc" \
    -I "$S/nvidia/arch/nvalloc/unix/include" -I "$S/common/inc" -I "$M" -o "${1:-.}/$t" "$(dirname "$0")/$t.cpp" || exit 1
done
