#!/usr/bin/env python3
"""Independent structural checks. These do not assert PS4 runtime compatibility."""
from pathlib import Path
import struct
import subprocess
import sys

def sfo_entries(data):
    if len(data) < 20 or data[:4] != b"\0PSF":
        raise ValueError("Invalid SFO header")
    version, keys, values, count = struct.unpack_from("<4I", data, 4)
    if version != 0x101 or count > (len(data) - 20) // 16 or not 20 + count * 16 <= keys <= values <= len(data):
        raise ValueError("Invalid SFO layout")
    entries = {}
    for i in range(count):
        key, fmt, length, capacity, offset = struct.unpack_from("<HHIII", data, 20 + i * 16)
        if key >= values - keys or length > capacity or offset > len(data) - values or capacity > len(data) - values - offset:
            raise ValueError("Invalid SFO entry bounds")
        end = data.find(b"\0", keys + key, values)
        if end < 0:
            raise ValueError("Unterminated SFO key")
        name = data[keys + key:end].decode("utf-8")
        if name in entries:
            raise ValueError("Duplicate SFO key")
        raw = data[values + offset:values + offset + length]
        if fmt == 0x404 and length == 4:
            entries[name] = struct.unpack("<I", raw)[0]
        elif fmt == 0x204 and raw.endswith(b"\0"):
            entries[name] = raw[:-1].decode("utf-8")
        else:
            raise ValueError("Invalid SFO value")
    return entries

def validate(pkg, sfo, elf):
    data = pkg.read_bytes()
    if len(data) < 0x200 or data[:4] != b"\x7fCNT":
        raise ValueError("Invalid PS4 PKG magic or size")
    if b"IV0000-USCL00001_00-USCLNATIVEPS4001" not in data[:0x100]:
        raise ValueError("PKG content ID mismatch")
    info = sfo_entries(sfo.read_bytes())
    assert info["TITLE_ID"] == "USCL00001" and info["CATEGORY"] == "gd"
    assert info["CONTENT_ID"] == "IV0000-USCL00001_00-USCLNATIVEPS4001"
    raw = elf.read_bytes()
    assert raw[:6] == b"\x7fELF\x02\x01" and struct.unpack_from("<H", raw, 18)[0] == 62
    symbols = subprocess.check_output(["readelf", "-Ws", elf], text=True)
    assert "sceLncUtilLaunchApp" in symbols and "sceVideoOutOpen" in symbols
    assert "SDL_" not in symbols and "Qt_" not in symbols
    oelf = elf.with_suffix(".oelf").read_bytes()
    phoff = struct.unpack_from("<Q", oelf, 32)[0]
    entsize, count = struct.unpack_from("<HH", oelf, 54)
    segments = [struct.unpack_from("<IIQQQQQQ", oelf, phoff + i * entsize) for i in range(count)]
    dyn = next(p for p in segments if p[0] == 2)
    dynlib = next(p for p in segments if p[0] == 0x61000000)
    tags = [struct.unpack_from("<QQ", oelf, at) for at in range(dyn[2], dyn[2] + dyn[5], 16)]
    table = dynlib[2] + next(value for tag, value in tags if tag == 0x61000035)
    def name(value):
        offset = table + (value & 0xffffffff)
        return oelf[offset:oelf.index(b"\0", offset)].decode()
    modules = [name(value) for tag, value in tags if tag == 0x6100000f]
    libraries = [name(value) for tag, value in tags if tag == 0x61000015]
    assert "libSceSystemService" in modules and "libSceLncUtil" not in modules, modules
    assert "libSceLncUtil" in libraries, libraries
    print("PASS: PKG identity, SFO bounds, x86-64 ELF and native launch/display imports")

if __name__ == "__main__":
    validate(*map(Path, sys.argv[1:]))
