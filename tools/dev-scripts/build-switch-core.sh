#!/usr/bin/env bash
# Build the pinned, patched Eden frontends for the isolated Linux preview.
set -euo pipefail
repo=$(cd "$(dirname "$0")/../.." && pwd)
source_dir="${USCL_EDEN_SOURCE:-$repo/build/eden-source}"
prefix="$repo/build/switch-core"
base=58c1e20ee58efa3900ba616207d460886214480b
mkdir -p "$(dirname "$source_dir")" "$prefix/bin" "$prefix/lib"
if [[ ! -d "$source_dir/.git" ]]; then
  git clone --depth 1 --branch v0.2.1 https://git.eden-emu.dev/eden-emu/eden.git "$source_dir"
fi
git -C "$source_dir" merge-base --is-ancestor "$base" HEAD
if [[ -n "$(git -C "$source_dir" status --porcelain --untracked-files=no)" ]]; then
  echo "Eden checkout has uncommitted changes; commit or use a separate USCL_EDEN_SOURCE." >&2
  exit 1
fi
for patch in "$repo"/uscl-eden/patches/*.patch; do
  if ! git -C "$source_dir" apply --reverse --check "$patch" 2>/dev/null; then
    git -C "$source_dir" am "$patch"
  fi
done
extra=()
if [[ -n "${USCL_EDEN_QT_PREFIX:-}" ]]; then
  extra+=("-DCMAKE_PREFIX_PATH=$USCL_EDEN_QT_PREFIX")
fi
cmake -S "$source_dir" -B "$source_dir/build-uscl" -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ \
  -DENABLE_QT=ON -DYUZU_TESTS=OFF -DBUILD_TESTING=OFF -DENABLE_CUBEB=OFF \
  -DENABLE_WEB_SERVICE=OFF -DYUZU_USE_BUNDLED_FFMPEG=OFF \
  -DYUZU_ROOM_STANDALONE=OFF -DYUZU_DISABLE_LLVM=ON \
  -DVulkanHeaders_FORCE_BUNDLED=ON "${extra[@]}"
cmake --build "$source_dir/build-uscl" --target eden eden-cli -j "${USCL_BUILD_JOBS:-8}"
install -m755 "$source_dir/build-uscl/bin/eden" "$prefix/bin/eden-native"
install -m755 "$source_dir/build-uscl/bin/eden-cli" "$prefix/bin/eden-cli"
if [[ -n "${USCL_EDEN_QT_PREFIX:-}" ]]; then
  # Support development dependencies extracted without installing system packages.
  for library in "$USCL_EDEN_QT_PREFIX"/lib/libQt6Charts.so*; do
    [[ ! -f "$library" ]] || cp -L "$library" "$prefix/lib/$(basename "$library")"
  done
fi
cat > "$prefix/bin/eden" <<'CORE'
#!/bin/sh
prefix=$(cd "$(dirname "$0")/.." && pwd)
export LD_LIBRARY_PATH="$prefix/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
exec "$prefix/bin/eden-native" "$@"
CORE
chmod +x "$prefix/bin/eden"
echo "Built USCL Eden at $prefix/bin/eden; switch-preview.sh will select it."
