#!/bin/bash
# online.sh <a|b>: from the multiplayer title menu: Play Online -> Public Match -> Find Match -> Core -> TDM
T=${USCL_TEST:-$HOME/.local/share/uscl/test}; x=$1
$(dirname "$0")/pad.sh $x press A >/dev/null; sleep 30
for k in 4 4 3 3; do $(dirname "$0")/pad.sh $x press A >/dev/null; sleep $k; done
