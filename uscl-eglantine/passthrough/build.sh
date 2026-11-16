#!/bin/sh
# Build the RM probe against the installed driver's SDK headers plus mesa's class/QMD headers.
S=${NV_SRC:-/usr/src/nvidia-615.78.08/src}
M=${MESA_CLASSES:-$HOME/src/ext/mesa-nouveau/src/nouveau/headers/nvidia/classes}
exec g++ -std=c++17 -O1 -Wall -Wno-missing-field-initializers -I "$S/common/sdk/nvidia/inc" \
  -I "$S/nvidia/arch/nvalloc/unix/include" -I "$S/common/inc" -I "$M" -o "${1:-nvrm_probe}" "$(dirname "$0")/nvrm_probe.cpp"
