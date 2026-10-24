#!/usr/bin/env python3
"""Left-click at a screen position (Hyprland): moves the cursor with hyprctl, clicks with a uinput mouse.
usage: click.py <x> <y> [<x> <y> | type:<text> ...]   (global layout coordinates, like hyprctl clients' "at")
type:<text> types lower-case letters, digits, - and _ into the focused widget"""
import fcntl, os, struct, subprocess, sys, time

UI_SET_EVBIT, UI_SET_KEYBIT, UI_SET_RELBIT = 0x40045564, 0x40045565, 0x40045566
UI_DEV_SETUP, UI_DEV_CREATE, UI_DEV_DESTROY = 0x405C5503, 0x5501, 0x5502
EV_SYN, EV_KEY, EV_REL, BTN_LEFT, REL_X, REL_Y = 0, 1, 2, 0x110, 0, 1

# evdev key codes: rows of a US keyboard
KEYS = {c: base + i for row, base in (('1234567890-', 2), ('qwertyuiop', 16), ('asdfghjkl', 30), ('zxcvbnm', 44))
        for i, c in enumerate(row)}
KEY_LEFTSHIFT = 42
steps, args = [], sys.argv[1:]
while args:
    if args[0].startswith('type:'):
        steps.append(args.pop(0)[5:])
    else:
        steps.append((int(args.pop(0)), int(args.pop(0))))
assert steps, __doc__
fd = os.open('/dev/uinput', os.O_WRONLY | os.O_NONBLOCK)
fcntl.ioctl(fd, UI_SET_EVBIT, EV_KEY)
for code in [BTN_LEFT, KEY_LEFTSHIFT, *KEYS.values()]: fcntl.ioctl(fd, UI_SET_KEYBIT, code)
fcntl.ioctl(fd, UI_SET_EVBIT, EV_REL); fcntl.ioctl(fd, UI_SET_RELBIT, REL_X); fcntl.ioctl(fd, UI_SET_RELBIT, REL_Y)
fcntl.ioctl(fd, UI_DEV_SETUP, struct.pack('HHHH80sI', 3, 0x1234, 0x5678, 1, b'uscl-click', 0))
fcntl.ioctl(fd, UI_DEV_CREATE)
time.sleep(1.5)  # let the compositor pick up the new device

def ev(t, c, v): os.write(fd, struct.pack('llHHi', 0, 0, t, c, v))

def tap(code, shift=False):
    if shift: ev(EV_KEY, KEY_LEFTSHIFT, 1)
    for v in (1, 0):
        ev(EV_KEY, code, v); ev(EV_SYN, 0, 0); time.sleep(0.03)
    if shift: ev(EV_KEY, KEY_LEFTSHIFT, 0); ev(EV_SYN, 0, 0)

for step in steps:
    if isinstance(step, str):
        for c in step:
            tap(KEYS['-'], True) if c == '_' else tap(KEYS[c])
        continue
    subprocess.run(['hyprctl', 'dispatch', 'movecursor', *map(str, step)], stdout=subprocess.DEVNULL, check=True)
    time.sleep(0.15)
    tap(BTN_LEFT)
    time.sleep(0.3)
fcntl.ioctl(fd, UI_DEV_DESTROY)
