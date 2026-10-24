#!/bin/bash
# Build the USCL AppImage: our RPCS3 build (uscl-rpcs3 patches) + uscl-rpcn + our Demonware + uscl-bridge.
# usage: packaging/build-linux.sh
#   USCL_RPCS3_BUILD  RPCS3 build dir (cmake -G Ninja, Release)   default ~/src/rpcs3-uscl/build-release
#   USCL_RPCN_SRC     uscl-rpcn checkout                           default ~/src/rpcn-uscl
#   USCL_DW_SRC       uscl-dw checkout                             default ~/src/obde-uscl
# Output: build/USCL-x86_64.AppImage. Tools (linuxdeploy) are cached in ~/.cache/uscl-build.
set -euo pipefail
REPO=$(cd "$(dirname "$0")/.." && pwd)
RPCS3_BUILD=${USCL_RPCS3_BUILD:-$HOME/src/rpcs3-uscl/build-release}
RPCN_SRC=${USCL_RPCN_SRC:-$HOME/src/rpcn-uscl}
DW_SRC=${USCL_DW_SRC:-$HOME/src/obde-uscl}
OUT=$REPO/build; APPDIR=$OUT/AppDir; TOOLS=$HOME/.cache/uscl-build
mkdir -p "$OUT" "$TOOLS"; rm -rf "$APPDIR"

echo "== servers"
(cd "$REPO/uscl-bridge" && cargo build --release -q)
(cd "$RPCN_SRC" && cargo build --release -q)
(cd "$DW_SRC" && cargo build --release -q -p dw-server)

echo "== home screen (uscl-shell)"
cmake -S "$REPO/uscl-shell" -B "$REPO/uscl-shell/build-release" -G Ninja -DCMAKE_BUILD_TYPE=Release >/dev/null
ninja -C "$REPO/uscl-shell/build-release"
"$REPO/uscl-shell/build-release/uscl" --self-test

echo "== USCL (RPCS3)"
ninja -C "$RPCS3_BUILD"
DESTDIR="$APPDIR" ninja -C "$RPCS3_BUILD" install >/dev/null

BIN=$APPDIR/usr/bin
install -m755 "$REPO/uscl-bridge/target/release/uscl-bridge" "$RPCN_SRC/target/release/rpcn" "$DW_SRC/target/release/dw-server" "$REPO/uscl-shell/build-release/uscl" "$BIN/"
mv "$BIN/rpcn" "$BIN/uscl-rpcn"
cp "$(find "$REPO/uscl-bridge/target/release/build" -name libsteam_api.so | head -1)" "$BIN/"   # the bridge loads it from its own folder

echo "== data templates (copied to the user's USCL folder when they first host)"
DATA=$APPDIR/usr/share/uscl
mkdir -p "$DATA/rpcn" "$DATA/dw/storage/publisher/18396"
cp "$RPCN_SRC"/{servers,server_redirs,scoreboards}.cfg "$DATA/rpcn/"
# the session tunnel's port; loopback only, players reach it through uscl-bridge
sed -e 's/^Port=.*/Port=31413/' -e 's/^Host=.*/Host=127.0.0.1/' -e 's/^HostIPv6=.*/HostIPv6=::1/' -e 's/^StatServerHost=.*/StatServerHost=127.0.0.1/' \
  "$RPCN_SRC/rpcn.cfg" > "$DATA/rpcn/rpcn.cfg"
python3 "$REPO/uscl-dw/bo2-wad/mkwad.py" "$DATA/dw/storage/publisher/18396/online_tu19_mp_english.wad" \
  "$REPO/uscl-dw/bo2-wad/leagues.info" "$REPO/uscl-dw/bo2-wad/playlists_mp.info"

echo "== desktop entry"
DESKTOP=$(ls "$APPDIR"/usr/share/applications/*.desktop | head -1)
sed -i -e 's/^Name=.*/Name=USCL/' -e 's/^GenericName=.*/GenericName=PS3 emulator with online play over Steam/' \
  -e 's/^Comment=.*/Comment=Play PS3 games online with your Steam friends/' "$DESKTOP"

echo "== AppImage"
for t in linuxdeploy linuxdeploy-plugin-qt; do
  [ -x "$TOOLS/$t" ] || { curl -fsSLo "$TOOLS/$t" "https://github.com/linuxdeploy/$t/releases/download/continuous/$t-x86_64.AppImage"; chmod +x "$TOOLS/$t"; }
done
[ -x "$TOOLS/appimagetool" ] || { curl -fsSLo "$TOOLS/appimagetool" https://github.com/AppImage/appimagetool/releases/download/continuous/appimagetool-x86_64.AppImage; chmod +x "$TOOLS/appimagetool"; }
# linuxdeploy's bundled strip is older than the distro's libraries
export NO_STRIP=1 PATH=$TOOLS:$PATH APPIMAGE_EXTRACT_AND_RUN=1 OUTPUT=$OUT/USCL-x86_64.AppImage
# Qt 6 (distros may also ship Qt 5's qmake), seen through a plugin folder without KDE's extra image formats,
# which pull in libraries the AppImage doesn't need (kimg_jxr needs libjpegxr, often not installed)
REAL_QMAKE=${QMAKE:-$(command -v qmake6 || command -v qmake)}
QT_PLUGINS=$("$REAL_QMAKE" -query QT_INSTALL_PLUGINS)
rm -rf "$OUT/qt-plugins"; cp -rs "$QT_PLUGINS" "$OUT/qt-plugins"; rm -f "$OUT"/qt-plugins/imageformats/kimg_*
printf '#!/bin/sh\n"%s" "$@" | sed "s|^QT_INSTALL_PLUGINS:.*|QT_INSTALL_PLUGINS:%s|"\n' "$REAL_QMAKE" "$OUT/qt-plugins" > "$OUT/qmake"
chmod +x "$OUT/qmake"; export QMAKE=$OUT/qmake
export EXTRA_PLATFORM_PLUGINS="libqwayland.so"
export EXTRA_QT_PLUGINS="svg;wayland-decoration-client;wayland-graphics-integration-client;wayland-shell-integration"
cd "$OUT"
# Qt Quick draws with OpenGL on Wayland through this plugin, which the Qt plugin for linuxdeploy leaves out
mkdir -p "$APPDIR/usr/plugins"
cp -r "$QT_PLUGINS/wayland-graphics-integration-client" "$APPDIR/usr/plugins/"
export QML_SOURCES_PATHS="$REPO/uscl-shell/qml"   # so the Qt plugin bundles the QML modules the home screen imports
linuxdeploy --appdir "$APPDIR" --plugin qt --executable "$BIN/uscl" --executable "$BIN/uscl-rpcn" --executable "$BIN/dw-server" \
  --deploy-deps-only "$APPDIR/usr/plugins/wayland-graphics-integration-client"
# same cleanup as RPCS3's .ci/deploy-linux.sh: these break other distros / gamescope
rm -f "$APPDIR"/usr/lib/libwayland-client.so* "$APPDIR"/usr/lib/libvulkan.so* \
  "$APPDIR"/usr/lib/libQt6VirtualKeyboard.so* "$APPDIR"/usr/plugins/platforminputcontexts/libqtvirtualkeyboardplugin.so*
# AppRun starts programs by their real path: through a symlink Qt looks for qt.conf/plugins in the AppImage root
# The home screen by default; --classic or a game file go straight to RPCS3
rm -f "$APPDIR/AppRun"; cat > "$APPDIR/AppRun" <<'RUN'
#!/bin/sh
bin="$(dirname "$(readlink -f "$0")")/usr/bin"
case "$1" in
  --classic) shift; exec "$bin/rpcs3" "$@" ;;
  ?*) [ -e "$1" ] && exec "$bin/rpcs3" "$@" ;;
esac
exec "$bin/uscl" "$@"
RUN
chmod +x "$APPDIR/AppRun"
# not linuxdeploy again: that would replace the AppRun launcher the Qt plugin made
appimagetool "$APPDIR" "$OUTPUT.new" && mv -f "$OUTPUT.new" "$OUTPUT"   # mv replaces it even while it is running
ls -la "$OUTPUT"
