#!/bin/bash
# pad.sh <a|b> <command...>: send one command to that copy's virtual pad
python3 -I -c "
import socket,sys
s=socket.socket(socket.AF_UNIX); s.connect('${USCL_TEST:-$HOME/.local/share/uscl/test}/$1.sock'); s.sendall(' '.join(sys.argv[1:]).encode()); print(s.recv(100).decode())" "${@:2}"
