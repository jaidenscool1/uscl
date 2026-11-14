#!/usr/bin/env python3
"""Find where code from one build of a PS3 ELF sits in another (a game update moves every address).

    portaddr.py <old.elf> <new.elf> <vaddr> [more vaddrs...]

Takes the instructions around each address in the old ELF and looks for them in the new one. Branch
displacements and 16-bit immediates (TOC offsets, lis/addi halves) change between builds, so they are
compared by opcode only. Prints `old -> new` when exactly one place matches, growing the window until it does.
"""
import struct, sys


def segments(data):
    phoff, = struct.unpack_from('>Q', data, 0x20)
    phentsize, phnum = struct.unpack_from('>HH', data, 0x36)
    out = []
    for i in range(phnum):
        ptype, _, off, vaddr, _, filesz = struct.unpack_from('>IIQQQQ', data, phoff + i * phentsize)
        if ptype == 1 and filesz:
            out.append((vaddr, off, filesz))
    return out


def words(data, seg):
    vaddr, off, size = seg
    return vaddr, struct.unpack_from('>%dI' % (size // 4), data, off)


def key(w):
    op = w >> 26
    if op == 18:  # b / bl: target moves
        return w & 0xFC000003
    if op == 16:  # bc: displacement moves
        return w & 0xFFFF0003
    if op in (14, 15, 24, 25, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 50, 52, 54, 58, 62):
        return w & 0xFFFF0000  # immediate / displacement forms
    return w


def find(old, new, addr):
    oseg = next(s for s in segments(old) if s[0] <= addr < s[0] + s[2])
    obase, ow = words(old, oseg)
    i = (addr - obase) // 4
    hits = []
    for before, after in ((4, 8), (8, 16), (16, 32), (32, 64), (64, 128)):
        pat = [key(w) for w in ow[max(0, i - before):i + after]]
        lead = i - max(0, i - before)
        hits = []
        for seg in segments(new):
            nbase, nw = words(new, seg)
            nk = [key(w) for w in nw]
            first = pat[0]
            j = -1
            while True:
                try:
                    j = nk.index(first, j + 1)
                except ValueError:
                    break
                if nk[j:j + len(pat)] == pat:
                    hits.append(nbase + (j + lead) * 4)
        if len(hits) == 1:
            return hits
    return hits


if __name__ == '__main__':
    old, new = open(sys.argv[1], 'rb').read(), open(sys.argv[2], 'rb').read()
    # self-check: an address maps to itself within one build
    probe = segments(old)[0][0] + 0x1000
    assert find(old, old, probe) == [probe], 'self-check failed'
    for a in sys.argv[3:]:
        addr = int(a, 16)
        hits = find(old, new, addr)
        print('0x%08x ->' % addr, ' '.join('0x%08x' % h for h in hits) if hits else 'not found',
              '' if len(hits) == 1 else '(ambiguous)' if hits else '')
