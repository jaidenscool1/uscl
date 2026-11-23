#!/bin/bash
# The emulators the Windows installer ships besides RPCS3: where each comes from and how our patches go on.
# usage: emulators.sh list                 names
#        emulators.sh key <name>           ci-cache asset name (without .zip): changes when the base or patches change
#        emulators.sh fetch <name> <dir>   clone the pinned base into <dir> and apply our patches
# Used by .github/workflows/windows-emulators.yml (builds) and windows-installer.yml (unpacks).
set -euo pipefail
REPO=$(cd "$(dirname "$0")/../.." && pwd)
# name | upstream | pinned base (tag or full commit) | patch folder | git am or git apply (apply keeps the release
# commit: Dolphin NetPlay and Azahar rooms only let in the same build) | submodules
TABLE='
shadps4   https://github.com/shadps4-emu/shadPS4.git        3eecea94ddaca360b19f4b800523b609a8b2ffcc uscl-shadps4   apply yes
shadnet   https://github.com/shadps4-emu/shadNet.git        6f534a127f362f5f6f5465cecaa18b62e0466b17 uscl-shadnet   apply yes
dolphin   https://github.com/dolphin-emu/dolphin.git        2609a                                    uscl-dolphin   apply yes
azahar    https://github.com/azahar-emu/azahar.git          2126.2                                   uscl-3ds       apply yes
eden      https://git.eden-emu.dev/eden-emu/eden.git        v0.2.1                                   uscl-eden      am    no
eglantine https://github.com/Eglantine-Community/Eglantine.git 30181ba05b484a4beefa838457b02d6ab5aa7baa uscl-eglantine am no
melonds   https://github.com/melonDS-emu/melonDS.git        497aa6cdd26f45f8064b78916b00a773eab4ded7 uscl-melonds   am    no
'
row() { awk -v n="$1" '$1 == n' <<< "$TABLE"; }
case "${1:-}" in
list) awk 'NF {print $1}' <<< "$TABLE" ;;
key)
  read -r name url base dir mode subs < <(row "$2"); [ -n "${name:-}" ] || { echo "unknown emulator $2" >&2; exit 1; }
  # bump the recipe number when a build recipe in windows-emulators.yml changes what comes out (eden w2: + eden-room, w3: VS 2022)
  rev=w1; [ "$name" = eden ] && rev=w3
  echo "emu-$name-$rev-${base:0:9}-$(cat "$REPO/$dir"/patches/*.patch | tr -d '\r' | sha256sum | cut -c1-16)" ;;   # same on CRLF checkouts
fetch)
  read -r name url base dir mode subs < <(row "$2"); [ -n "${name:-}" ] || { echo "unknown emulator $2" >&2; exit 1; }
  src=$3; git init -q "$src"; g() { git -C "$src" -c core.autocrlf=false "$@"; }   # LF patches apply byte for byte
  g config core.autocrlf false; g remote add origin "$url"
  if [ ${#base} = 40 ]; then g fetch -q --depth 1 origin "$base"; else g fetch -q --depth 1 origin "+refs/tags/$base:refs/tags/$base"; fi
  g checkout -q FETCH_HEAD
  [ "$subs" = yes ] && { g submodule update -q --init --recursive --depth 1 || g submodule update -q --init --recursive; }
  if [ "$mode" = am ]; then g -c user.name=uscl -c user.email=uscl@localhost am -q "$REPO/$dir"/patches/*.patch
  else g apply "$REPO/$dir"/patches/*.patch; fi
  echo "$name: $base + $(ls "$REPO/$dir"/patches/*.patch | wc -l) patches ($mode) in $src" ;;
*) sed -n '2,6p' "$0"; exit 1 ;;
esac
