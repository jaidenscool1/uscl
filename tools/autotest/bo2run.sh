#!/bin/bash
# bo2run.sh <a|b> <npid>: (re)start one test copy and drive it to Multiplayer -> Play Online
T=${USCL_TEST:-$HOME/.local/share/uscl/test}; x=$1; npid=$2
[ -z "$NOLAUNCH" ] && w=$($(dirname "$0")/bo2ctl.sh win $x) && [ -n "$w" ] && kill -KILL $(hyprctl clients -j | python3 -I -c "import json,sys;print([c['pid'] for c in json.load(sys.stdin) if c['address']=='$w'][0])") && sleep 2
n=$(grep -c "online_id=$npid\$" $T/dw.out)
[ -n "$NOLAUNCH" ] && n=${NOLAUNCH_N:-$n}
[ -z "$NOLAUNCH" ] && $(dirname "$0")/bo2ctl.sh launch $x >/dev/null
w=$($(dirname "$0")/bo2ctl.sh win $x); hyprctl --batch "dispatch focuswindow address:$w; dispatch fullscreen 0" >/dev/null
timeout 600 bash -c "until [ \$(grep -c 'online_id=$npid\$' $T/dw.out) -gt $n ]; do sleep 3; done" || { echo "$x: no launcher login"; exit 1; }
sleep 25; $(dirname "$0")/pad.sh $x press START 200 >/dev/null; sleep 5
# select MULTIPLAYER by looking at the screen (menu input is ignored for a variable time)
$(dirname "$0")/place.sh; geo="$([ $x = a ] && echo 13 || echo 967) 940"
for i in $(seq 40); do
  grim -o JSXTV $T/sel-$x.png; wsel=$(python3 $(dirname "$0")/menusel.py $T/sel-$x.png $geo)
  if [ "$wsel" -ge 122 ]; then $(dirname "$0")/pad.sh $x press A >/dev/null; break
  elif [ "$wsel" -gt 0 ]; then $(dirname "$0")/pad.sh $x dpad UP 200 >/dev/null
  else $(dirname "$0")/pad.sh $x press START 200 >/dev/null; fi
  sleep 3
done
m=$(grep -c "online_id=$npid\$" $T/dw.out)
timeout 600 bash -c "until [ \$(grep -c 'online_id=$npid\$' $T/dw.out) -gt $m ]; do sleep 3; done" || { echo "$x: multiplayer never logged in"; exit 1; }
sleep 20; $(dirname "$0")/pad.sh $x press A >/dev/null; echo "$x: Play Online pressed"
sleep 30; for k in 4 4 3 3; do $(dirname "$0")/pad.sh $x press A >/dev/null; sleep $k; done   # Public Match, Find Match, Core, Team Deathmatch
echo "$x: searching Core / Team Deathmatch"
