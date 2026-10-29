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

# Plain peer data (bdDTLSData, crossplay with PS3): the Steam tunnel encrypts between PCs, so DATA packets carry the
# payload unencrypted and the bridge can translate it. Handshake and the sent MAC stay; the MAC is no longer checked.
SEND_ENC = 0x8A62DF  # call [rax+0x10] (in-place encrypt) -> mov al, 1
RECV_MAC_JNZ = 0x8A67EE  # jnz after memcmp of the MAC
RECV_DEC = 0x8A683F  # 26 bytes up to the call [rax+0x18] (decrypt src r14 -> dst [rbp-0x3b8], length r15d)
dec = (b"\x48\x8b\xbd\x48\xfc\xff\xff"  # mov rdi, [rbp-0x3b8]
       b"\x4c\x89\xf6"  # mov rsi, r14
       b"\x44\x89\xf9"  # mov ecx, r15d
       b"\xf3\xa4"  # rep movsb
       b"\xb0\x01")  # mov al, 1
dec += b"\x90" * (26 - len(dec))
plain = "\n            ".join(f'<Line Type="bytes" Address="{a + 0x400000:#x}" Value="{v}"/>' for a, v in (
    (SEND_ENC, "B00190"), (RECV_MAC_JNZ, "90" * 6), (RECV_DEC, dec.hex().upper())))
plain_meta = meta.replace("USCL: host when no public match is found", "USCL: plain peer data").replace(
    "SetupMatchmakingStage never ends the search; give up after {stages} stages and host",
    "No 3DES on game-to-game data (the Steam tunnel encrypts); needed for PS3 crossplay")

# PS3 party layout (crossplay): the PS4 port writes one more availableMapPackFlags bit (12, PS3 11) in each party
# member (descriptor table 0xe07fe0, entry 21) and an extra string (+0xbe14) in the party state (pseg); make both
# like PS3's, on both sides of a PS4 game, so PS3 and PS4 read each other's party state.
MAP_PACK_BITS = 0xE081D8  # int32 12 -> 11
PSEG_WRITE_STR = 0x14EC84  # call MSG_WriteString (+0xbe14) -> nop
PSEG_READ_STR = 0x144DE8  # call MSG_ReadString (+0xbe14, 0x40) -> nop
party = "\n            ".join(f'<Line Type="bytes" Address="{a + 0x400000:#x}" Value="{v}"/>' for a, v in (
    (MAP_PACK_BITS, "0B000000"), (PSEG_WRITE_STR, "0F1F440000"), (PSEG_READ_STR, "0F1F440000")))
party_meta = meta.replace("USCL: host when no public match is found", "USCL: PS3 party layout").replace(
    "SetupMatchmakingStage never ends the search; give up after {stages} stages and host",
    "Party member map pack flags in 11 bits and no extra party string, as on PS3 (crossplay)")
xml = '<?xml version="1.0" encoding="utf-8"?>\n<Patch>\n' + "\n".join(
    m.format(stages=STAGES, ver=v, lines=l) for m, l in ((meta, lines), (plain_meta, plain), (party_meta, party))
    for v in ("01.11", "01.00")) + "\n</Patch>\n"
out = sys.argv[1]
os.makedirs(out, exist_ok=True)
open(os.path.join(out, "CUSA57548.xml"), "w").write(xml)
json.dump({"CUSA57548.xml": ["CUSA57548"]}, open(os.path.join(out, "files.json"), "w"))
print(code.hex())
