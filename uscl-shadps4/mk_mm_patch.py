"""Build the USCL shadPS4 patch for BO2 PS4 (CUSA57548 codmp.elf): public-match search gives up after a few stages so
the party hosts its own lobby (status 4 -> 0013f1c0 -> createSession), also when online. The game's matchmaking.lua
(SetupMatchmakingStage) never returns false here, so a lone searcher never hosts.

Replaces FUN_0056d050 (next search stage, 48 bytes) with:
    stage++ (DAT_0344fcd0); if stage >= STAGES return 0; FUN_00571230(&DAT_0344a5c0, controller); return 1
usage: mk_mm_patch.py <out dir>"""
import json
import os
import struct
import sys

STAGES = 4
G = 0x56D050  # Ghidra address; the game runs at +0x400000
STAGE, SESSION, UPDATE = 0x344FCD0, 0x344A5C0, 0x571230


def rel(target, next_ip):
    return struct.pack("<i", target - next_ip)


code = b"\x53"  # push rbx
code += b"\x89\xfb"  # mov ebx, edi
code += b"\xff\x05" + rel(STAGE, G + len(code) + 6)  # inc dword [stage]
code += b"\x83\x3d" + rel(STAGE, G + len(code) + 7) + bytes([STAGES])  # cmp dword [stage], STAGES
code += b"\x7d\x15"  # jge .no
code += b"\x48\x8d\x3d" + rel(SESSION, G + len(code) + 7)  # lea rdi, [session]
code += b"\x89\xde"  # mov esi, ebx
code += b"\xe8" + rel(UPDATE, G + len(code) + 5)  # call FUN_00571230
code += b"\xb8\x01\x00\x00\x00\x5b\xc3"  # mov eax, 1; pop rbx; ret
assert len(code) == 39, len(code)  # .no is the jge target
code += b"\x31\xc0\x5b\xc3"  # .no: xor eax, eax; pop rbx; ret
assert len(code) <= 48

# FUN_00310b90 (may this player host after a failed search): online it always says no (JNZ after FUN_0036ee30, the
# PS4 port expects dedicated servers for public matches). NOP it so the NAT/bandwidth checks decide, as on PS3.
ONLINE_JNZ = 0x310BE1  # 75 5B
lines = (f'<Line Type="bytes" Address="{G + 0x400000:#x}" Value="{code.hex().upper()}"/>\n            '
         f'<Line Type="bytes" Address="{ONLINE_JNZ + 0x400000:#x}" Value="9090"/>')
meta = '''    <Metadata Title="Call of Duty: Black Ops II"
              Name="USCL: host when no public match is found"
              Note="SetupMatchmakingStage never ends the search; give up after {stages} stages and host"
              Author="USCL" PatchVer="1.0" AppVer="{ver}" AppElf="codmp.elf" isEnabled="true">
        <PatchList>
            {lines}
        </PatchList>
    </Metadata>'''
xml = '<?xml version="1.0" encoding="utf-8"?>\n<Patch>\n' + "\n".join(
    meta.format(stages=STAGES, ver=v, lines=lines) for v in ("01.11", "01.00")) + "\n</Patch>\n"
out = sys.argv[1]
os.makedirs(out, exist_ok=True)
open(os.path.join(out, "CUSA57548.xml"), "w").write(xml)
json.dump({"CUSA57548.xml": ["CUSA57548"]}, open(os.path.join(out, "files.json"), "w"))
print(code.hex())
