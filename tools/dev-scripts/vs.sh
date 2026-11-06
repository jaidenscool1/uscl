#!/bin/bash
# vs.sh name: screenshot USCLTEST as a 1280x720 png
grim -o USCLTEST /tmp/.vs.$$.png && python3 -c "
from PIL import Image; im=Image.open('/tmp/.vs.$$.png'); im.thumbnail((1280,720)); im.save('$1')"; rm -f /tmp/.vs.$$.png
