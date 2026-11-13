#!/bin/bash
# mw2nav.sh <a|b> leave|tdm: leave the lobby (to the online menu) / Find Game -> Team Deathmatch
D=$(cd "$(dirname "$0")" && pwd); p() { $D/p.sh "$@"; }; w() { timeout $1 tail -f /dev/null; }
case $2 in
leave) p $1 press B 300; w 3; p $1 dpad UP 200; p $1 press A 300; w 6 ;;
tdm) for step in 1 2; do p $1 dpad DOWN 200; p $1 dpad UP 200; p $1 press A 300; w 6; done ;;
esac
