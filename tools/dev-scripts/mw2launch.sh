#!/bin/bash
# mw2launch.sh <a|b>: start MW2 in RPCS3 profile rpcs3-bo2<x>, place the window on USCLTEST (a left, b right)
ISO="/home/main/Downloads/GAME_DUMPS/PS3_GAME/Call of Duty - Modern Warfare 2 (USA, Korea)/Call of Duty - Modern Warfare 2 (USA, Korea).dec.iso"
T=~/.local/share/uscl/test; D=$(cd "$(dirname "$0")" && pwd)
systemd-run --user --unit=uscl-mw2$1-$(date +%s) --collect -E WAYLAND_DISPLAY=$WAYLAND_DISPLAY -E DISPLAY=$DISPLAY -E XDG_RUNTIME_DIR=$XDG_RUNTIME_DIR \
  -E XDG_CONFIG_HOME=$HOME/.local/share/uscl/rpcs3-bo2$1/config -E XDG_CACHE_HOME=$T/cache-bo2$1 -p WorkingDirectory=$T \
  bash -c "exec ~/Applications/rpcs3.AppImage --no-gui \"$ISO\" > $D/svc-mw2/rpcs3-$1.out 2>&1" >/dev/null 2>&1
for i in $(seq 60); do w=$($D/bo2ctl.sh win $1); [ -n "$w" ] && break; sleep 1; done
sleep 3; px=$([ $1 = a ] && echo 11920 || echo 12880)
hyprctl dispatch movetoworkspacesilent "3,address:$w" >/dev/null
f=$(hyprctl clients -j | python3 -I -c "import json,sys;print([c['fullscreen'] for c in json.load(sys.stdin) if c['address']=='$w'][0])")
[ "$f" != "0" ] && hyprctl --batch "dispatch focuswindow address:$w; dispatch fullscreen 0" >/dev/null
hyprctl --batch "dispatch setfloating address:$w; dispatch resizewindowpixel exact 960 1080,address:$w; dispatch movewindowpixel exact $px 0,address:$w" >/dev/null
echo $w
