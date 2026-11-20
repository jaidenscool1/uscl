#!/usr/bin/env python3
"""Download separate upstream emulator test packages with published checksums.

This does not install anything on a console or assert game/firmware compatibility.
"""
import argparse
import hashlib
import json
from pathlib import Path
import urllib.request

PACKAGES = (
    ("RPCS3.PORT.FOR.PS4.v0.40.by.Roberix.Studio.pkg",
     "https://github.com/RoberixStudios/rpcs3-port-for-ps4/releases/download/v0.40/RPCS3.PORT.FOR.PS4.v0.40.by.Roberix.Studio.pkg",
     "a49bcca52a584bcbac29d6abb5a875e3d371f9a790b81929bb8ba8ec33cd6670",
     "https://github.com/RoberixStudios/rpcs3-port-for-ps4/tree/v0.40"),
    ("nx-on-orbis-v0.1.0.pkg",
     "https://github.com/alechurri/nx-on-orbis/releases/download/v0.1.0/nx-on-orbis-v0.1.0.pkg",
     "ee16b7cbbbde918486e3d807778e092e9b3a70a1405249fabf4a9c671d2899c9",
     "https://github.com/alechurri/nx-on-orbis/tree/v0.1.0"),
)

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    records = []
    for name, url, expected, source in PACKAGES:
        target = args.output / name
        if not target.exists():
            partial = target.with_suffix(".pkg.partial")
            urllib.request.urlretrieve(url, partial)
            with partial.open("rb") as stream:
                digest = hashlib.file_digest(stream, "sha256").hexdigest()
            if digest != expected:
                partial.unlink()
                raise RuntimeError("Upstream checksum mismatch: " + name)
            partial.rename(target)
        with target.open("rb") as stream:
            digest = hashlib.file_digest(stream, "sha256").hexdigest()
        if digest != expected:
            raise RuntimeError("Existing file checksum mismatch: " + name)
        print("Verified", name, digest)
        records.append(dict(file=name, sha256=digest, url=url, source=source,
                            console_tested_by_uscl=False, firmware_900_verified_by_uscl=False))
    (args.output / "UPSTREAM-EMULATORS.json").write_text(json.dumps(records, indent=2) + "\n")

if __name__ == "__main__":
    main()
