#!/bin/bash
# bo2ctl.sh launch <a|b> | key <a|b> <KEY> [times] | shot <file> | win <a|b>
ISO="/home/main/Downloads/GAME_DUMPS/PS3_GAME/Call of Duty - Black Ops II (USA) (En,Fr)/Call of Duty - Black Ops II (USA) (En,Fr).dec.iso"
T=${USCL_TEST:-$HOME/.local/share/uscl/test}
win() { # address of the RPCS3 window whose process runs with config rpcs3-bo2$1
  hyprctl clients -j | python3 -I -c "
import json,sys
for c in json.load(sys.stdin):
    if c['class'] not in ('RPCS3','AppRun'): continue
    try: env=open(f'/proc/{c[\"pid\"]}/environ','rb').read()
    except OSError: continue
    if b'rpcs3-bo2$1/config' in env: print(c['address'])" | head -1
}
case $1 in
launch)
  systemd-run --user --unit=uscl-bo2$2-$(date +%s) --collect -E WAYLAND_DISPLAY=$WAYLAND_DISPLAY -E DISPLAY=$DISPLAY \
    -E XDG_RUNTIME_DIR=$XDG_RUNTIME_DIR -E XDG_CONFIG_HOME=$HOME/.local/share/uscl/rpcs3-bo2$2/config \
    -E XDG_CACHE_HOME=$T/cache-bo2$2 -p WorkingDirectory=$T \
    bash -c "exec ~/Applications/rpcs3.AppImage --no-gui \"$ISO\" > $T/rpcs3-bo2$2.out 2>&1" >/dev/null
  for i in $(seq 60); do a=$(win $2); [ -n "$a" ] && break; sleep 1; done
  hyprctl dispatch movetoworkspacesilent "name:uscl,address:$a" >/dev/null; echo "$a" ;;
key) a=$(win $2); for i in $(seq ${4:-1}); do hyprctl dispatch sendshortcut ", $3, address:$a" >/dev/null; sleep 0.6; done ;;
shot) grim -o JSXTV "$2" ;;
win) win $2 ;;
esac
