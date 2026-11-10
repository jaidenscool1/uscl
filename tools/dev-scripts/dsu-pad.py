#!/usr/bin/env python3
"""Local cemuhook/DSU pad server for Eden's UDP controller injection.

Serves the DSU protocol on 127.0.0.1:26760 and exposes a Unix socket for
driving it. This is a development/diagnostic tool for the native MK8D Online
Play route; it supplies input only, never game state or credentials.

Control socket commands (one per connection):
  hold BTN [BTN ...] MS   press several buttons together for MS milliseconds
  tap BTN MS              press one button
  dpad DIR MS             press one dpad direction (UP/DOWN/LEFT/RIGHT)
  stick L|R x,y MS        tilt a stick (x,y are 0..255, 128 = center)
  clear                   release every button and recentre sticks

Buttons: SHARE L3 R3 OPTIONS UP RIGHT DOWN LEFT L2 R2 L1 R1
         TRIANGLE CIRCLE CROSS SQUARE

Run with Eden configured for the cemuhookudp engine (see the MK8 native
diagnostic notes) and USCL_UDP_POLL_MS set to a small value.
"""
import os
import socket
import struct
import sys
import threading
import time
import zlib

SERVER_MAGIC = 0x53555344
CLIENT_MAGIC = 0x43555344
PROTOCOL = 1001
TYPE_VERSION = 0x00100000
TYPE_PORTINFO = 0x00100001
TYPE_PADDATA = 0x00100002

BITS = {
    "SHARE": 0, "L3": 1, "R3": 2, "OPTIONS": 3, "UP": 4, "RIGHT": 5,
    "DOWN": 6, "LEFT": 7, "L2": 8, "R2": 9, "L1": 10, "R1": 11,
    "TRIANGLE": 12, "CIRCLE": 13, "CROSS": 14, "SQUARE": 15,
}

state = {"buttons": 0, "lx": 128, "ly": 128, "rx": 128, "ry": 128}
counter = 1
lock = threading.Lock()


def build_response(typ, data, req_id):
    header = struct.pack("<IHHII", SERVER_MAGIC, PROTOCOL, len(data) + 4, 0, req_id)
    header += struct.pack("<I", typ)
    msg = bytearray(header + data)
    msg[8:12] = b"\0\0\0\0"
    msg[8:12] = struct.pack("<I", zlib.crc32(bytes(msg)) & 0xFFFFFFFF)
    return bytes(msg)


def portinfo_data():
    return struct.pack("<BBBB6sBB", 0, 2, 3, 1, b"\x00\x00\x00\x00\x00\x01", 5, 1)


def paddata_data():
    global counter
    with lock:
        info = portinfo_data()
        c = counter
        counter = (counter + 1) & 0xFFFFFFFF
        btns = state["buttons"]
        sticks = struct.pack("<BBBB", state["lx"], state["ly"], state["rx"], state["ry"])
    return (info + struct.pack("<IHB", c, btns, 0) + b"\x00" + sticks +
            b"\x00" * 12 + b"\x00" * 12 + struct.pack("<Q", 0) + b"\x00" * 24)


def handle_request(data):
    if len(data) < 20:
        return None
    magic, _, _, _, req_id, typ = struct.unpack_from("<IHHIII", data, 0)
    if magic != CLIENT_MAGIC:
        return None
    if typ == TYPE_VERSION:
        return build_response(TYPE_VERSION, struct.pack("<H", PROTOCOL), req_id)
    if typ == TYPE_PORTINFO:
        return build_response(TYPE_PORTINFO, portinfo_data(), req_id)
    if typ == TYPE_PADDATA:
        return build_response(TYPE_PADDATA, paddata_data(), req_id)
    return None


def control_loop(sock_path):
    if os.path.exists(sock_path):
        os.unlink(sock_path)
    srv = socket.socket(socket.AF_UNIX)
    srv.bind(sock_path)
    srv.listen(4)
    while True:
        conn, _ = srv.accept()
        words = conn.recv(256).decode().split()
        try:
            if words[0] == "clear":
                with lock:
                    state["buttons"] = 0
                    state.update(lx=128, ly=128, rx=128, ry=128)
                conn.sendall(b"ok")
            else:
                ms = int(words[-1])
                if words[0] == "hold":
                    btns = [b.upper() for b in words[1:-1]]
                elif words[0] in ("tap", "dpad"):
                    btns = [words[1].upper()]
                elif words[0] == "stick":
                    which = words[1].upper()
                    x, y = (int(v) for v in words[2].split(","))
                    with lock:
                        if which == "L":
                            state["lx"], state["ly"] = x, y
                        else:
                            state["rx"], state["ry"] = x, y
                    time.sleep(ms / 1000)
                    with lock:
                        if which == "L":
                            state["lx"], state["ly"] = 128, 128
                        else:
                            state["rx"], state["ry"] = 128, 128
                    conn.sendall(b"ok")
                    conn.close()
                    continue
                else:
                    conn.sendall(b"ERR")
                    conn.close()
                    continue
                with lock:
                    for button in btns:
                        state["buttons"] |= 1 << BITS[button]
                time.sleep(ms / 1000)
                with lock:
                    for button in btns:
                        state["buttons"] &= ~(1 << BITS[button])
                conn.sendall(b"ok")
        except (KeyError, ValueError, IndexError) as error:
            conn.sendall(f"ERR {error!r}".encode())
        conn.close()


def main():
    sock_path = sys.argv[1]
    threading.Thread(target=control_loop, args=(sock_path,), daemon=True).start()
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.bind(("127.0.0.1", 26760))
    print("DSU pad server on 127.0.0.1:26760", flush=True)
    while True:
        data, addr = sock.recvfrom(100)
        response = handle_request(data)
        if response:
            sock.sendto(response, addr)


if __name__ == "__main__":
    main()
