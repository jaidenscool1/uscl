#!/usr/bin/env python3
"""Virtual game pad over /dev/uinput, driven through a unix socket.
usage: vpad.py <socket> <x360|xone>
commands (one per connection): press <BTN> [ms] | dpad <UP|DOWN|LEFT|RIGHT> [ms] | stick <L|R> <x> <y> [ms]
BTN: A B X Y LB RB BACK START GUIDE LS RS LT RT"""
import fcntl, os, socket, struct, sys, time

UI_SET_EVBIT, UI_SET_KEYBIT, UI_SET_ABSBIT = 0x40045564, 0x40045565, 0x40045567
UI_DEV_SETUP, UI_ABS_SETUP, UI_DEV_CREATE = 0x405C5503, 0x401C5504, 0x5501
EV_SYN, EV_KEY, EV_ABS = 0, 1, 3
BTN = {'A': 0x130, 'B': 0x131, 'X': 0x133, 'Y': 0x134, 'LB': 0x136, 'RB': 0x137, 'BACK': 0x13A,
       'START': 0x13B, 'GUIDE': 0x13C, 'LS': 0x13D, 'RS': 0x13E}
ABS = {'X': 0, 'Y': 1, 'Z': 2, 'RX': 3, 'RY': 4, 'RZ': 5, 'HAT0X': 0x10, 'HAT0Y': 0x11}
MODELS = {'x360': (0x045E, 0x028E, b'Microsoft X-Box 360 pad'), 'xone': (0x045E, 0x02EA, b'Microsoft X-Box One S pad')}

sock_path, model = sys.argv[1], sys.argv[2]
vendor, product, name = MODELS[model]
fd = os.open('/dev/uinput', os.O_WRONLY | os.O_NONBLOCK)
fcntl.ioctl(fd, UI_SET_EVBIT, EV_KEY); fcntl.ioctl(fd, UI_SET_EVBIT, EV_ABS)
for code in BTN.values(): fcntl.ioctl(fd, UI_SET_KEYBIT, code)
for axis, code in ABS.items():
    fcntl.ioctl(fd, UI_SET_ABSBIT, code)
    lo, hi = (-1, 1) if axis.startswith('HAT') else (0, 255) if axis in ('Z', 'RZ') else (-32768, 32767)
    fcntl.ioctl(fd, UI_ABS_SETUP, struct.pack('<HH6i', code, 0, 0, lo, hi, 0, 0, 0))
fcntl.ioctl(fd, UI_DEV_SETUP, struct.pack('<4H80sI', 0x03, vendor, product, 0x0110, name, 0))
fcntl.ioctl(fd, UI_DEV_CREATE)

def emit(etype, code, value):
    os.write(fd, struct.pack('<qqHHi', 0, 0, etype, code, value))
    os.write(fd, struct.pack('<qqHHi', 0, 0, EV_SYN, 0, 0))

def hold(ms): time.sleep(int(ms) / 1000)

if os.path.exists(sock_path): os.unlink(sock_path)
srv = socket.socket(socket.AF_UNIX); srv.bind(sock_path); srv.listen(4)
while True:
    c, _ = srv.accept()
    w = c.recv(256).decode().split()
    try:
        if w[0] == 'press':
            if w[1] in ('LT', 'RT'):
                ax = ABS['Z' if w[1] == 'LT' else 'RZ']; emit(EV_ABS, ax, 255); hold(w[2] if len(w) > 2 else 150); emit(EV_ABS, ax, 0)
            else:
                emit(EV_KEY, BTN[w[1]], 1); hold(w[2] if len(w) > 2 else 150); emit(EV_KEY, BTN[w[1]], 0)
        elif w[0] == 'dpad':
            ax, v = {'UP': ('HAT0Y', -1), 'DOWN': ('HAT0Y', 1), 'LEFT': ('HAT0X', -1), 'RIGHT': ('HAT0X', 1)}[w[1]]
            emit(EV_ABS, ABS[ax], v); hold(w[2] if len(w) > 2 else 150); emit(EV_ABS, ABS[ax], 0)
        elif w[0] == 'stick':
            x, y = ('X', 'Y') if w[1] == 'L' else ('RX', 'RY')
            emit(EV_ABS, ABS[x], int(w[2])); emit(EV_ABS, ABS[y], int(w[3])); hold(w[4] if len(w) > 4 else 300)
            emit(EV_ABS, ABS[x], 0); emit(EV_ABS, ABS[y], 0)
        c.sendall(b'ok')
    except Exception as e:
        c.sendall(f'ERR {e!r}'.encode())
    c.close()
