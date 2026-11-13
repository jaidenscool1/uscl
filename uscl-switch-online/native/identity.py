"""Private local profile credentials; no Nintendo or Steam identity claims."""
import argparse
import os
from pathlib import Path
import secrets


def load_identity(path):
    with open(path, "rb") as stream:
        data = stream.read(129)
    lines = data.split(b"\n")
    if len(data) > 128 or len(lines) != 4 or lines[0] != b"USCL_NATIVE_ID_V1" or lines[3]:
        raise ValueError("invalid private identity")
    if not lines[1].isdigit() or not 0 < int(lines[1]) < 2 ** 64:
        raise ValueError("invalid private identity")
    token = lines[2]
    if len(token) != 64 or any(value not in b"0123456789abcdef" for value in token):
        raise ValueError("invalid private identity")
    return int(lines[1]), token.decode("ascii")


def create_identity(path):
    # O_EXCL prevents accidentally replacing another profile's identity.
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True, mode=0o700)
    identity = secrets.randbits(63) or 1
    token = secrets.token_hex(32)
    with os.fdopen(os.open(path, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600), "wb") as stream:
        stream.write(f"USCL_NATIVE_ID_V1\n{identity}\n{token}\n".encode("ascii"))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("path")
    args = parser.parse_args()
    create_identity(args.path)
    print("Created private local identity; contents are not logged.")
