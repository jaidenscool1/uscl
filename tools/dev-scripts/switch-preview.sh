#!/usr/bin/env bash
# Build and run a separate USCL profile; leave the installed AppImage and player profiles alone.
set -euo pipefail
repo=$(cd "$(dirname "$0")/../.." && pwd)
preview="$repo/build/switch-preview"
mkdir -p "$preview/bin" "$preview/profile/config/uscl"
cmake -S "$repo/uscl-shell" -B "$repo/uscl-shell/build-switch" -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_RUNTIME_OUTPUT_DIRECTORY="$preview/bin"
cmake --build "$repo/uscl-shell/build-switch" -j "${USCL_BUILD_JOBS:-4}"
cargo build --locked --release --manifest-path "$repo/uscl-bridge/Cargo.toml" --target-dir "$preview/bridge"
install -m755 "$preview/bridge/release/uscl-bridge" "$preview/bin/uscl-bridge"
steam_library=$(find "$preview/bridge/release/build" -name libsteam_api.so -print -quit)
install -m755 "$steam_library" "$preview/bin/libsteam_api.so"

export XDG_CONFIG_HOME="$preview/profile/config"
export XDG_DATA_HOME="$preview/profile/data"
export XDG_CACHE_HOME="$preview/profile/cache"
export USCL_SWITCH_PROFILE="$preview/profile/switch"
export USCL_TOOLS="$preview/bin"
export APPIMAGE_EXTRACT_AND_RUN=1
# The unbundled bridge needs the Steam runtime library copied beside it above.
export LD_LIBRARY_PATH="$preview/bin${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
if [[ -z "${USCL_SWITCH_CORE:-}" && -x "$repo/build/switch-core/bin/eden" ]]; then
  export USCL_SWITCH_CORE="$repo/build/switch-core/bin/eden"
  export USCL_SWITCH_FRONTEND=qt
  export USCL_SWITCH_GUI="${USCL_SWITCH_GUI:-$repo/build/switch-core/bin/eden}"
fi
if [[ -x "$HOME/Applications/Eden-v0.2.1.AppImage" ]]; then
  cat > "$preview/bin/eden-cli" <<'CORE'
#!/bin/sh
exec "$HOME/Applications/Eden-v0.2.1.AppImage" eden-cli "$@"
CORE
  cat > "$preview/bin/eden" <<'GUI'
#!/bin/sh
exec "$HOME/Applications/Eden-v0.2.1.AppImage" "$@"
GUI
  chmod +x "$preview/bin/eden-cli" "$preview/bin/eden"
fi
if [[ -x "$HOME/Applications/eden-room" ]]; then
  ln -sfn "$HOME/Applications/eden-room" "$preview/bin/eden-room"
fi
if [[ ! -f "$XDG_CONFIG_HOME/uscl/shell.conf" ]]; then
  cat > "$XDG_CONFIG_HOME/uscl/shell.conf" <<'SETTINGS'
[General]
setupDone=true
bootScreen=false
autoUpdate=false
SETTINGS
fi
if [[ -d "${USCL_SWITCH_GAMES:-$HOME/Downloads/GAME_DUMPS/Switch/extracted}" ]]; then
  QT_QPA_PLATFORM=offscreen "$preview/bin/uscl" --scan-games \
    "${USCL_SWITCH_GAMES:-$HOME/Downloads/GAME_DUMPS/Switch/extracted}"
fi
if [[ "${1:-}" == --build-only ]]; then
  QT_QPA_PLATFORM=offscreen "$preview/bin/uscl" --self-test
  QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software "$preview/bin/uscl" --qml-smoke-test --windowed
  exit
fi
exec "$preview/bin/uscl" "$@"
