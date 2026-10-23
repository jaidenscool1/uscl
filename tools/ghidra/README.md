# Ghidra helpers for PS3 executables

Headless scripts used on Black Ops II (see `games/BLUS31011.md`). Ghidra 12 (`pacman -S ghidra`).

```
./unself.py <EBOOT.BIN or .self> out.elf [--klic <hex>]        # decrypt first (never commit the output)
python3 - out.elf <<'PY'                                          # prints the TOC base for SetToc
import struct,sys;d=open(sys.argv[1],'rb').read();e,=struct.unpack_from('>Q',d,0x18);p,=struct.unpack_from('>Q',d,0x20)
for i in range(struct.unpack_from('>H',d,0x38)[0]):
    t,f,o,v,_,s,_,_=struct.unpack_from('>IIQQQQQQ',d,p+i*0x38)
    if t==1 and v<=e<v+s: print(hex(struct.unpack_from('>I',d,o+e-v+4)[0]))
PY
G=/opt/ghidra/support/analyzeHeadless
MAXMEM=24G $G <project dir> <name> -import out.elf -processor "PowerPC:BE:64:A2ALT-32addr" \
  -scriptPath tools/ghidra -preScript SetToc.java 0x<toc> -max-cpu 10      # ~7 min for a 14 MB executable
$G <project dir> <name> -process out.elf -noanalysis -readOnly -scriptPath tools/ghidra \
  -postScript StringUsers.java <out dir> "some string" ...
```
- **TOC (r2):** the ELF entry point is a function descriptor `[u32 code][u32 toc]`; read the second word at
  `e_entry`. Without it Ghidra can't link code to strings and globals. BO2 1.19 multiplayer: `0xd67e98`.
- `StringUsers`: functions that use a string (directly or through a TOC slot), decompiled.
- `Around`: a function with its callers/callees to N levels. `RefsTo`: functions referencing an address.
- `Range`: decompile every function in an address range. `FuncList`: function list for uscl-rpcs3 logpoints.
- `Disasm`: raw instructions, e.g. jump-table targets.
- Virtual methods have no direct callers: find the descriptor (`.opd`, 8 bytes `[code][toc]`) holding the code
  address, then the table holding the descriptor.
