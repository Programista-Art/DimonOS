#!/usr/bin/env python3
"""Wrap a flat i386 binary with a NAPP64 header using nm symbol offsets."""
import struct, subprocess, sys
from pathlib import Path

def main():
    if len(sys.argv) != 5:
        print("usage: mknapp.py obj bin out name", file=sys.stderr)
        return 1
    obj, binary, out, name = sys.argv[1:5]
    nm = subprocess.check_output(["nm", obj], text=True)
    syms = {}
    for line in nm.splitlines():
        p = line.split()
        if len(p) >= 3:
            syms[p[2]] = int(p[0], 16)
    data = Path(binary).read_bytes()
    hdr = bytearray(48)
    hdr[0:7] = b"NAPP64\0"
    struct.pack_into("<I", hdr, 8, 1)
    struct.pack_into("<I", hdr, 12, syms.get("app_main", 0))
    struct.pack_into("<I", hdr, 16, syms.get("snake_key", 0))
    struct.pack_into("<I", hdr, 20, syms.get("snake_timer", 0))
    struct.pack_into("<I", hdr, 24, syms.get("snake_draw", 0))
    struct.pack_into("<I", hdr, 28, len(data))
    nb = name.encode()[:15]
    hdr[32:32 + len(nb)] = nb
    Path(out).write_bytes(bytes(hdr) + data)
    print(f"{out}: NAPP64 {name} image={len(data)}B")
    return 0

if __name__ == "__main__":
    sys.exit(main())
