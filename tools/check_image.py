"""Check the application binary's size and initial vectors."""

import argparse
import hashlib
import json
import struct
from pathlib import Path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("image", type=Path)
    parser.add_argument("--elf", type=Path, required=True)
    args = parser.parse_args()
    data = args.image.read_bytes()
    if len(data) < 8 or len(data) > 0x18000:
        raise SystemExit("application must fit in 96 KiB and contain vectors")
    stack, reset = struct.unpack_from("<II", data)
    if not 0x20000000 < stack <= 0x20005000 or stack % 8:
        raise SystemExit("invalid initial stack")
    if not reset & 1 or not 0x08008000 <= (reset & ~1) < 0x08008000 + len(data):
        raise SystemExit("reset vector outside application")
    elf = args.elf.read_bytes()
    if elf[:7] != b"\x7fELF\x01\x01\x01":
        raise SystemExit("expected little-endian ELF32")
    header = struct.unpack_from("<16sHHIIIIIHHHHHH", elf)
    loads = []
    for index in range(header[10]):
        kind, _, _, physical, size, _, _, _ = struct.unpack_from(
            "<8I", elf, header[5] + index * header[9]
        )
        if kind == 1 and size:
            if not 0x08008000 <= physical < physical + size <= 0x08020000:
                raise SystemExit("file-backed ELF segment outside application flash")
            loads.append({"address": hex(physical), "bytes": size})
    if not loads:
        raise SystemExit("no file-backed load segments")
    print(json.dumps({"bytes": len(data), "stack": hex(stack), "reset": hex(reset),
                      "sha256": hashlib.sha256(data).hexdigest(),
                      "load_segments": loads}, indent=2))


if __name__ == "__main__":
    main()
