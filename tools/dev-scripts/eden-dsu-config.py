#!/usr/bin/env python3
"""Point an Eden profile's player 1 buttons at a local DSU (cemuhook) pad, for scripted tests.

eden-dsu-config.py <qt-config.ini> <dsu port>
Only touches player_0 button bindings and the UDP server list; use isolated test profiles.
"""
import re
import sys

GUID = "0000000000000000000000007f000001"  # Eden derives it from 127.0.0.1
# Switch button -> DSU button bit, as in Eden's UDPClient::GetButtonMappingForDevice.
BUTTONS = {
    "a": 0x2000, "b": 0x4000, "x": 0x1000, "y": 0x8000, "plus": 0x0008, "minus": 0x0001,
    "dleft": 0x0080, "dup": 0x0010, "dright": 0x0020, "ddown": 0x0040,
    "l": 0x0400, "r": 0x0800, "zl": 0x0100, "zr": 0x0200, "lstick": 0x0002, "rstick": 0x0004,
}

path, port = sys.argv[1], int(sys.argv[2])
text = open(path, encoding="utf-8").read()
for name, bit in BUTTONS.items():
    value = f'"engine:cemuhookudp,guid:{GUID},port:{port},pad:0,button:{bit}"'
    text, n = re.subn(rf"^player_0_button_{name}=.*$", f"player_0_button_{name}={value}", text, flags=re.M)
    if n != 1:
        sys.exit(f"player_0_button_{name} not found in {path}")
    text = re.sub(rf"^player_0_button_{name}\\default=.*$", f"player_0_button_{name}\\\\default=false", text, flags=re.M)
text = re.sub(r"^udp_input_servers=.*$", f"udp_input_servers=127.0.0.1:{port}", text, flags=re.M)
text = re.sub(r"^udp_input_servers\\default=.*$", r"udp_input_servers\\default=false", text, flags=re.M)
open(path, "w", encoding="utf-8").write(text)
print(f"{path}: player 1 on DSU 127.0.0.1:{port}")
