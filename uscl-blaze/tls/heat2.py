#!/usr/bin/env python3
"""Decode a Blaze 3.x (heat2) TDF frame captured from Madden. Self-checks the wire format:
field = 3-byte tag + 1-byte type; string = varint len + bytes(incl NUL); int = varint.
usage: heat2.py <hexfile>  (file may contain '--- DIR N bytes ---' markers + hex)"""
import sys, re

TYPES = {0: 'int', 1: 'string', 2: 'blob', 3: 'struct', 4: 'list', 5: 'map',
         6: 'union', 7: 'variable', 8: 'objtype', 9: 'objid', 10: 'float', 0xd0: '??(2.x?)'}

def tag(b):  # 3 bytes -> 4-char Blaze tag
    v = (b[0] << 16) | (b[1] << 8) | b[2]
    out = ''
    for sh in (26, 20, 14, 8):  # 6 bits/char, offset by 0x20
        c = (v >> sh) & 0x3f
        out += chr(c + 0x20) if c else ''
    return out

def varint(d, i):
    # Blaze varint: first byte low 6 bits (bit6 = continue), then 7 bits/byte
    b = d[i]; val = b & 0x3f; i += 1
    if b & 0x40:
        sh = 6
        while True:
            b = d[i]; i += 1; val |= (b & 0x7f) << sh; sh += 7
            if not (b & 0x80): break
    return val, i

def decode(d, depth=0):
    i = 0; pad = '  ' * depth
    while i < len(d):
        if i + 4 > len(d): break
        tg = tag(d[i:i+3]); ty = d[i+3]; i += 4
        name = TYPES.get(ty, f'0x{ty:02x}')
        if ty == 1:  # string
            ln, i = varint(d, i); s = d[i:i+ln]; i += ln
            print(f'{pad}{tg} str = {s.rstrip(bytes(1)).decode("latin1")!r}')
        elif ty == 0:  # int
            v, i = varint(d, i); print(f'{pad}{tg} int = {v}')
        else:
            print(f'{pad}{tg} {name} (stop: type not decoded here) rest={d[i:i+24].hex()}')
            break

for blk in re.split(r'--- (\S+) \d+ bytes ---', open(sys.argv[1]).read()):
    blk = blk.strip()
    if not blk or ' ' in blk and not re.fullmatch(r'[0-9a-f]+', blk):
        if blk in ('C>S', 'S>C'): print(f'\n### {blk}'); continue
        continue
    if not re.fullmatch(r'[0-9a-f]+', blk): continue
    d = bytes.fromhex(blk)
    print(f'header: len={int.from_bytes(d[0:2],"big")} comp={int.from_bytes(d[2:4],"big")} '
          f'cmd={int.from_bytes(d[4:6],"big")} err={int.from_bytes(d[6:8],"big")} msgtype=0x{d[8]:02x}')
    decode(d[12:])
