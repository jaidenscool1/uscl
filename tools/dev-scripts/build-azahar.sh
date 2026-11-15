#!/usr/bin/env bash
# Build USCL's Azahar (3DS): pinned 2126.2 + uscl-3ds/patches, frontend `azahar` and room server `azahar-room`.
# The home screen uses them for 3DS rooms over Steam: put both next to `uscl`, or set USCL_AZAHAR / USCL_AZAHAR_ROOM.
set -euo pipefail
repo=$(cd "$(dirname "$0")/../.." && pwd)
source_dir="${USCL_AZAHAR_SOURCE:-$repo/build/azahar-source}"
prefix="$repo/build/azahar"
if [[ ! -d "$source_dir/.git" ]]; then
  git clone --depth 1 --branch 2126.2 --recurse-submodules --shallow-submodules \
    https://github.com/azahar-emu/azahar.git "$source_dir"
fi
# git apply, not am: the tree stays on the release commit (same version string for every player)
for patch in "$repo"/uscl-3ds/patches/*.patch; do
  if ! git -C "$source_dir" apply --reverse --check "$patch" 2>/dev/null; then
    git -C "$source_dir" apply "$patch"
  fi
done
cmake -S "$source_dir" -B "$source_dir/build-uscl" -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DENABLE_TESTS=OFF -DENABLE_QT_UPDATE_CHECKER=OFF
cmake --build "$source_dir/build-uscl" --target azahar azahar-room -j "${USCL_BUILD_JOBS:-8}"
install -Dm755 "$source_dir/build-uscl/bin/Release/azahar" "$prefix/azahar"
install -Dm755 "$source_dir/build-uscl/bin/Release/azahar-room" "$prefix/azahar-room"
echo "Built USCL Azahar in $prefix: USCL_AZAHAR=$prefix/azahar USCL_AZAHAR_ROOM=$prefix/azahar-room"
