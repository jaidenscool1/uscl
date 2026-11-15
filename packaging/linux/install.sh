#!/bin/bash
# Install USCL for this user: the AppImage goes to ~/Applications and USCL appears in the app menu.
# usage: ./install.sh [USCL-x86_64.AppImage]     (uninstall: ./install.sh --remove)
set -euo pipefail
DEST=$HOME/Applications/USCL.AppImage
DESKTOP=$HOME/.local/share/applications/uscl.desktop
ICON=$HOME/.local/share/icons/hicolor/512x512/apps/uscl.png
if [ "${1:-}" = "--remove" ]; then
  rm -f "$DEST" "$DESKTOP" "$ICON" "$HOME/.local/share/icons/hicolor/scalable/apps/uscl.svg"; echo "USCL removed (your settings and saves in ~/.config/uscl are kept)."; exit 0
fi
SRC=${1:-$(ls "$(dirname "$0")"/USCL*.AppImage 2>/dev/null | head -1)}
[ -f "$SRC" ] || { echo "Put this script next to USCL-x86_64.AppImage, or pass its path."; exit 1; }
mkdir -p "$(dirname "$DEST")" "$(dirname "$DESKTOP")" "$(dirname "$ICON")"
install -m755 "$SRC" "$DEST"
rm -f "$HOME/.local/share/icons/hicolor/scalable/apps/uscl.svg"   # older installs put RPCS3's icon here; it would win over the PNG
# the icon from inside the AppImage (the USCL orb)
TMP=$(mktemp -d); (cd "$TMP" && "$DEST" --appimage-extract 'usr/share/icons/hicolor/512x512/apps/uscl.png' >/dev/null 2>&1 || true)
cp "$TMP/squashfs-root/usr/share/icons/hicolor/512x512/apps/uscl.png" "$ICON" 2>/dev/null || true; rm -rf "$TMP"
cat > "$DESKTOP" <<DESK
[Desktop Entry]
Type=Application
Name=USCL
GenericName=Play your console games online with Steam friends
Comment=PS3, PS4, Wii, 3DS, Switch and DS games online over Steam
Exec="$DEST" %f
Icon=uscl
Terminal=false
Categories=Game;Emulator;
DESK
update-desktop-database "$(dirname "$DESKTOP")" 2>/dev/null || true
echo "USCL installed: open it from your app menu, or run $DEST"
