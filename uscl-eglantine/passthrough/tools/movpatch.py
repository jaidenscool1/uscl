# movpatch.py <program va> <sass offset> Rd=Rs [...]: PT_PATCH spec writing `MOV Rd, Rs` at consecutive instruction
# slots from <sass offset> (offset as nvdisasm prints it; the 0x80 SPH is added). Checks each with nvdisasm.
# WAIT=1: the first MOV waits on all six scoreboards (sources from in-flight TEX/LDG).
import subprocess, sys, tempfile
base, off = int(sys.argv[1], 16) + 0x80, int(sys.argv[2], 16)
specs = []
for i, a in enumerate(sys.argv[3:]):
    d, s = (int(x.strip().lstrip("R")) for x in a.split("="))
    lo = 0x7202 | d << 16 | s << 32
    hi = 0x0F00 | 0x000FE2 << 40  # mask 0xf; control: stall 1, no barriers (the source is long ready at program end)
    if i == 0 and __import__("os").environ.get("WAIT") == "1":
        hi |= 0x3F << 52  # wait mask (bits 116-121): all scoreboards
    blob = (lo | hi << 64).to_bytes(16, "little")
    with tempfile.NamedTemporaryFile(suffix=".bin") as f:
        f.write(blob); f.flush()
        txt = subprocess.run(["nvdisasm", "-b", "SM86", f.name], capture_output=True, text=True, check=True).stdout
    assert f"MOV R{d}, R{s} " in txt, txt
    specs.append(f"{base + off + 16 * i:x}:{blob.hex()}")
print(",".join(specs))
