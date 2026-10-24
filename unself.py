#!/usr/bin/env python3
"""Decrypt a PS3 SELF (EBOOT.BIN, *.self, *.sprx) to an ELF, headless. Same steps as RPCS3's Crypto/unself.cpp.

usage: unself.py <in.self> <out.elf> [--klic <32 hex>] [--rpcs3 <rpcs3 source dir>]   (default ~/src/rpcs3-uscl)

Keys are read from that RPCS3 checkout's rpcs3/Crypto/key_vault.{cpp,h} at run time, so none are kept in this repo.
Handles APP and NPDRM-free-license SELFs (disc games and their updates). A game's extra .self files often need
the klicensee the game passes to sceNpDrmIsAvailable (RPCS3 logs its address; read 16 bytes there from the
decrypted EBOOT). Paid NPDRM needs a RAP: not supported.
"""
import re
import struct
import sys
import zlib
from pathlib import Path

from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes

KEY_TYPES = {4: "APP", 8: "NPDRM"}  # program_type -> key table (key_vault.h order: LV0=1 ... NPDRM=8)


def aes(key, mode, data, decrypt):
    c = Cipher(algorithms.AES(key), mode)
    op = c.decryptor() if decrypt else c.encryptor()
    return op.update(data) + op.finalize()


def load_keys(src: Path):
    cpp = (src / "rpcs3/Crypto/key_vault.cpp").read_text()
    h = (src / "rpcs3/Crypto/key_vault.h").read_text()
    tables = {}
    for name in KEY_TYPES.values():
        tables[name] = {
            int(rev, 16): (bytes.fromhex(erk), bytes.fromhex(riv))
            for rev, erk, riv in re.findall(
                rf'sk_{name}_arr\.emplace_back\(\s*0x[0-9A-Fa-f]+,\s*0x[0-9A-Fa-f]+,\s*0x([0-9A-Fa-f]+),\s*KEY_{name},\s*"([0-9A-F]+)",\s*"([0-9A-F]+)"', cpp)
        }

    def const(name):
        m = re.search(rf"{name}\[0x10\]\s*=\s*\{{([^}}]*)\}}", h)
        return bytes(int(b, 16) for b in re.findall(r"0x([0-9A-Fa-f]{2})", m.group(1)))

    return tables, const("NP_KLIC_FREE"), const("NP_KLIC_KEY")


def unself(data: bytes, keys, klic: bytes | None = None) -> bytes:
    tables, klic_free, klic_key = keys
    magic, _, se_flags, _, se_meta, se_hsize, _ = struct.unpack_from(">IIHHIQQ", data, 0)
    if magic != 0x53434500:
        raise ValueError("not a SELF (no SCE magic)")
    (_, prog_off, ehdr_off, phdr_off, shdr_off, _, _, supp_off, supp_size, _) = struct.unpack_from(">10Q", data, 0x20)
    _, _, prog_type, _ = struct.unpack_from(">QIIQ", data, prog_off)
    if se_flags & 0x8000:
        raise ValueError("debug SELF: not supported (data is plain, use it as is)")
    table = KEY_TYPES.get(prog_type)
    if not table or se_flags not in tables[table]:
        raise ValueError(f"no key for program_type={prog_type} revision={se_flags:#x}")
    erk, riv = tables[table][se_flags]

    ehdr = data[ehdr_off:ehdr_off + 0x40]
    if ehdr[4] != 2:
        raise ValueError("ELF32 SELF: not supported")
    e_phoff, e_shoff = struct.unpack_from(">QQ", ehdr, 0x20)
    e_phentsize, e_phnum, e_shentsize, e_shnum = struct.unpack_from(">HHHH", ehdr, 0x36)
    phdrs = [struct.unpack_from(">IIQQQQQQ", data, phdr_off + i * 0x38) for i in range(e_phnum)]

    # Metadata info: optional NPDRM layer (free license only), then AES-256-CBC with the key table's erk/riv.
    meta = data[0x20 + se_meta:0x20 + se_meta + 0x40]
    pos = supp_off
    while pos < supp_off + supp_size:
        stype, ssize = struct.unpack_from(">IIQ", data, pos)[:2]
        if stype == 3:  # NPDRM header
            license_type = struct.unpack_from(">i", data, pos + 0x10 + 8)[0]
            if license_type != 3:
                raise ValueError("paid NPDRM SELF: needs a RAP, not supported")
            k = aes(klic_key, modes.ECB(), klic or klic_free, True)
            meta = aes(k, modes.CBC(bytes(16)), meta, True)
        pos += ssize
    meta = aes(erk, modes.CBC(riv), meta, True)
    if meta[0x10] or meta[0x30]:
        raise ValueError("metadata didn't decrypt (wrong key?)")

    hdrs = data[0x20 + se_meta + 0x40:se_hsize]
    hdrs = aes(meta[0:0x10], modes.CTR(meta[0x20:0x30]), hdrs, True)
    _, _, section_count, key_count = struct.unpack_from(">QIII", hdrs, 0)
    sections = [struct.unpack_from(">QQ8I", hdrs, 0x20 + i * 0x30) for i in range(section_count)]
    dk = hdrs[0x20 + section_count * 0x30:][:key_count * 0x10]

    end = max([e_shoff + e_shnum * e_shentsize] + [p[2] + p[5] for p in phdrs])
    out = bytearray(end)
    out[0:0x40] = ehdr
    out[e_phoff:e_phoff + e_phnum * 0x38] = data[phdr_off:phdr_off + e_phnum * 0x38]
    if shdr_off:
        out[e_shoff:e_shoff + e_shnum * e_shentsize] = data[shdr_off:shdr_off + e_shnum * e_shentsize]
    for off, size, stype, pidx, _, _, enc, kidx, ividx, comp in sections:
        if stype != 2:  # only PHDR data goes into the ELF
            continue
        blob = data[off:off + size]
        if enc == 3:
            blob = aes(dk[kidx * 16:kidx * 16 + 16], modes.CTR(dk[ividx * 16:ividx * 16 + 16]), blob, True)
        if comp == 2:
            blob = zlib.decompress(blob)
        p_offset = phdrs[pidx][2]
        out[p_offset:p_offset + len(blob)] = blob
    return bytes(out)


def main():
    args = sys.argv[1:]
    src = Path.home() / "src/rpcs3-uscl"
    klic = None
    if "--klic" in args:
        i = args.index("--klic")
        klic = bytes.fromhex(args[i + 1])
        del args[i:i + 2]
    if "--rpcs3" in args:
        i = args.index("--rpcs3")
        src = Path(args[i + 1])
        del args[i:i + 2]
    if len(args) != 2:
        sys.exit(__doc__)
    elf = unself(Path(args[0]).read_bytes(), load_keys(src), klic)
    Path(args[1]).write_bytes(elf)
    print(f"{args[1]}: {len(elf)} bytes")


if __name__ == "__main__":
    main()
