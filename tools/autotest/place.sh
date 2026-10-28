#!/bin/bash
# place.sh: pin both test windows as floating 940x1024, a left / b right, on output JSXTV (x 10000)
T=${USCL_TEST:-$HOME/.local/share/uscl/test}
for x in a b; do
  w=$($(dirname "$0")/bo2ctl.sh win $x); [ -z "$w" ] && continue
  px=$([ $x = a ] && echo 10013 || echo 10967)
  f=$(hyprctl clients -j | python3 -I -c "import json,sys;print([c['floating'] for c in json.load(sys.stdin) if c['address']=='$w'][0])")
  [ "$f" = "False" ] && hyprctl dispatch setfloating "address:$w" >/dev/null
  hyprctl --batch "dispatch resizewindowpixel exact 940 1024,address:$w; dispatch movewindowpixel exact $px 43,address:$w" >/dev/null
done
