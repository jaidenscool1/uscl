#!/bin/bash
# shotb.sh name [a|b]: screenshot one half of the USCLTEST output
grim -o USCLTEST /tmp/.$$.png && python3 -c "
from PIL import Image; im=Image.open('/tmp/.$$.png'); x=0 if '${2:-b}'=='a' else 960; im=im.crop((x,180,x+960,720)); im.save('$1')"; rm -f /tmp/.$$.png
