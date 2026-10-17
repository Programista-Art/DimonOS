#!/usr/bin/env python3
"""Host-side FAT16 unit tests for dimon-mkiso / dimonfs behaviour."""
import struct, subprocess, sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
MKISO = ROOT / "dimon-mkiso"

def run(*args):
    return subprocess.run([str(MKISO), *args], capture_output=True, text=True, cwd=ROOT)

def read_ent(img, idx):
    off = 36 * 512 + idx * 32
    return img[off:off + 32]

def main():
    assert MKISO.exists(), "dimon-mkiso not built"
    img_path = ROOT / "build" / "fs_unit.img"
    img_path.parent.mkdir(exist_ok=True)
    if img_path.exists():
        img_path.unlink()

    r = run("-o", str(img_path), "--force")
    assert r.returncode == 0, r.stderr

    # BPB sanity
    b = img_path.read_bytes()
    assert b[510:512] == b"\x55\xAA", "boot signature"
    assert b[3:11] == b"DIMON64 ", "OEM"
    assert b[54:62] == b"FAT16   ", "FS type"
    print("PASS: BPB signature/OEM/FS type")

    # list
    r = run("--list", str(img_path))
    assert r.returncode == 0
    assert "NOTES.TXT" in r.stdout
    print("PASS: list shows sample files")

    # add a file
    payload = ROOT / "build" / "fs_unit_payload.txt"
    payload.write_text("hello dimonos native\n")
    r = run("--add", str(img_path), f"{payload}:HELLO.TXT", "--force")
    assert r.returncode == 0, r.stderr
    r = run("--list", str(img_path))
    assert "HELLO.TXT" in r.stdout
    print("PASS: add HELLO.TXT")

    # mkdir
    r = run("--mkdir", str(img_path), "/DOCS")
    assert r.returncode == 0, r.stderr
    print("PASS: mkdir /DOCS")

    # 8.3 validation rejects bad names
    bad = ROOT / "build" / "bad name.txt"
    bad.write_text("x")
    r = run("--add", str(img_path), f"{bad}:bad name.txt", "--force")
    assert r.returncode != 0
    print("PASS: invalid 8.3 name rejected")

    # clus < 2 must not underflow (empty dir entry)
    e = read_ent(b, 0)
    assert e[0] != 0
    print("PASS: root entries present")

    print("=== FS unit tests passed ===")
    return 0

if __name__ == "__main__":
    sys.exit(main())
