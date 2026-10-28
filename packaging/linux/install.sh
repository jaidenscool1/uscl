#!/bin/bash
# Install USCL for this user: the AppImage goes to ~/Applications and USCL appears in the app menu.
# usage: ./install.sh [USCL-x86_64.AppImage]     (uninstall: ./install.sh --remove)
set -euo pipefail
DEST=$HOME/Applications/USCL.AppImage
DESKTOP=$HOME/.local/share/applications/uscl.desktop
ICON=$HOME/.local/share/icons/hicolor/scalable/apps/uscl.svg
if [ "${1:-}" = "--remove" ]; then
  rm -f "$DEST" "$DESKTOP" "$ICON"; echo "USCL removed (your settings and saves in ~/.config/uscl are kept)."; exit 0
fi
SRC=${1:-$(ls "$(dirname "$0")"/USCL*.AppImage 2>/dev/null | head -1)}
[ -f "$SRC" ] || { echo "Put this script next to USCL-x86_64.AppImage, or pass its path."; exit 1; }
mkdir -p "$(dirname "$DEST")" "$(dirname "$DESKTOP")" "$(dirname "$ICON")"
install -m755 "$SRC" "$DEST"
# the icon from inside the AppImage
TMP=$(mktemp -d); (cd "$TMP" && "$DEST" --appimage-extract 'usr/share/icons/hicolor/scalable/apps/*.svg' >/dev/null 2>&1 || true)
cp "$(find "$TMP/squashfs-root" -name '*.svg' | head -1)" "$ICON" 2>/dev/null || true; rm -rf "$TMP"
cat > "$DESKTOP" <<DESK
[Desktop Entry]
Type=Application
Name=USCL
GenericName=PS3 emulator with online play over Steam
Comment=Play PS3 games online with your Steam friends
Exec="$DEST" %f
Icon=uscl
Terminal=false
Categories=Game;Emulator;
DESK
update-desktop-database "$(dirname "$DESKTOP")" 2>/dev/null || true
echo "USCL installed: open it from your app menu, or run $DEST"
