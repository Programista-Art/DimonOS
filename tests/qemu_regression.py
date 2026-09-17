#!/usr/bin/env python3
"""Automate focused bare-metal window-manager and shutdown regressions."""

import argparse
import hashlib
import json
import socket
import struct
import subprocess
import time
import zlib
from pathlib import Path

WIDTH = 800
TITLE = (0x25, 0x63, 0xEB)
BG = (0x1E, 0x24, 0x30)


def wait_for(predicate, timeout, description):
    end = time.monotonic() + timeout
    while time.monotonic() < end:
        if predicate():
            return
        time.sleep(0.05)
    raise AssertionError(f"timeout waiting for {description}")


def read_ppm(path):
    raw = path.read_bytes()
    header, pixels = raw.split(b"\n255\n", 1)
    assert header == b"P6\n800 600", header
    assert len(pixels) == WIDTH * 600 * 3
    return pixels


def pixel(data, x, y):
    pos = (y * WIDTH + x) * 3
    return tuple(data[pos:pos + 3])


def write_png(path, pixels):
    """Write dependency-free RGB evidence alongside QEMU's PPM dump."""
    scanlines = b"".join(b"\0" + pixels[y * WIDTH * 3:(y + 1) * WIDTH * 3]
                         for y in range(600))

    def chunk(kind, payload):
        return (struct.pack(">I", len(payload)) + kind + payload +
                struct.pack(">I", zlib.crc32(kind + payload) & 0xFFFFFFFF))

    png = (b"\x89PNG\r\n\x1a\n" +
           chunk(b"IHDR", struct.pack(">IIBBBBB", WIDTH, 600, 8, 2, 0, 0, 0)) +
           chunk(b"IDAT", zlib.compress(scanlines, 9)) + chunk(b"IEND", b""))
    path.with_suffix(".png").write_bytes(png)


class HMP:
    def __init__(self, path):
        self.sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        self.sock.settimeout(3)
        self.sock.connect(str(path))
        self._response()
        self.x = 400
        self.y = 300

    def _response(self):
        data = b""
        while b"(qemu)" not in data:
            chunk = self.sock.recv(4096)
            if not chunk:
                break
            data += chunk
        return data.decode(errors="replace")

    def command(self, command, settle=0.12):
        self.sock.sendall(command.encode() + b"\n")
        reply = self._response()
        assert "unknown command" not in reply.lower(), reply
        time.sleep(settle)
        return reply

    def key(self, key):
        self.command(f"sendkey {key}", 0.22)

    def move_to(self, x, y):
        self.command(f"mouse_move {x - self.x} {y - self.y}", 0.12)
        self.x, self.y = x, y

    def button(self, down):
        self.command(f"mouse_button {1 if down else 0}", 0.15)

    def click(self, x, y):
        self.move_to(x, y)
        self.button(True)
        self.button(False)

    def screenshot(self, path):
        self.command(f"screendump {path}", 0.15)
        wait_for(path.exists, 2, path.name)
        pixels = read_ppm(path)
        write_png(path, pixels)
        return pixels

    def close(self):
        self.sock.close()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--build-dir", default="build/regression")
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    build = (root / args.build_dir).resolve()
    iso = build / "dimon-regression.iso"
    assert iso.exists()
    out = build / "qemu-results"
    out.mkdir(parents=True, exist_ok=True)
    monitor = out / "monitor.sock"
    serial = out / "serial.log"
    qemu_log = out / "qemu.log"
    if monitor.exists():
        monitor.unlink()
    if serial.exists():
        serial.unlink()

    log = qemu_log.open("w")
    proc = subprocess.Popen([
        "qemu-system-i386", "-accel", "tcg", "-cdrom", str(iso),
        "-boot", "d", "-m", "256M", "-vga", "std", "-display", "none",
        "-serial", f"file:{serial}",
        "-monitor", f"unix:{monitor},server=on,wait=off", "-no-reboot",
    ], cwd=root, stdout=log, stderr=subprocess.STDOUT)
    hmp = None
    shutdown_succeeded = False
    try:
        wait_for(monitor.exists, 5, "QEMU monitor socket")
        wait_for(lambda: serial.exists() and
                 "Starting DimonOS-64" in serial.read_text(errors="replace"),
                 40, "fresh kernel boot marker")
        hmp = HMP(monitor)
        time.sleep(0.8)

        hmp.key("1")
        wait_for(lambda: pixel(hmp.screenshot(out / "01-calculator-open.ppm"), 245, 75) == TITLE, 4, "calculator open")
        opened = read_ppm(out / "01-calculator-open.ppm")
        assert pixel(opened, 245, 75) == TITLE

        # Original minimize hang: minimize only Calculator, then F1 must still
        # render and numeric selection must restore the retained window.
        hmp.click(520, 80)
        minimized = hmp.screenshot(out / "02-calculator-minimized.ppm")
        assert pixel(minimized, 300, 120) == BG
        hmp.key("f1")
        hmp.key("1")
        restored = hmp.screenshot(out / "03-calculator-restored.ppm")
        assert pixel(restored, 245, 75) == TITLE

        # Bare-metal PS/2 press/move/release. The release occurs outside the
        # original title bounds, then an idle move proves capture ended.
        hmp.move_to(350, 80)
        hmp.button(True)
        hmp.move_to(100, 40)
        hmp.button(False)
        hmp.move_to(700, 500)
        dragged = hmp.screenshot(out / "04-calculator-drag-release.ppm")
        assert pixel(dragged, 5, 35) == TITLE
        assert pixel(dragged, 485, 133) != TITLE

        # Buttons use their translated hit areas after movement: 1+2=3.
        for point in ((45, 290), (270, 350), (110, 290), (190, 350)):
            hmp.click(*point)
        calculated = hmp.screenshot(out / "05-calculator-result.ppm")
        cyan = (0x38, 0xBD, 0xF8)
        assert any(pixel(calculated, x, y) == cyan
                   for y in range(98, 114) for x in range(28, 40))

        # A title control press is not a drag. Restore retains the moved origin.
        hmp.move_to(270, 45)
        hmp.button(True)
        time.sleep(0.2)
        hmp.move_to(400, 200)
        hmp.button(False)
        hmp.click(110, 580)
        control = hmp.screenshot(out / "06-title-control-no-drag.ppm")
        assert pixel(control, 5, 35) == TITLE

        # Close foreground Notepad, then close the last open Calculator. A
        # desktop icon click must still reopen Calculator afterward.
        hmp.key("f2")
        hmp.click(700, 55)
        hmp.click(300, 45)
        closed = hmp.screenshot(out / "07-last-window-closed.ppm")
        assert pixel(closed, 300, 120) == BG
        hmp.click(50, 70)
        reopened = hmp.screenshot(out / "08-desktop-responsive-after-close.ppm")
        assert pixel(reopened, 5, 35) == TITLE

        # System Exit is a distinct operation. It must terminate QEMU through
        # ACPI; reaching a timeout and killing the process is a test failure.
        hmp.key("f1")
        hmp.key("x")
        hmp.close()
        hmp = None
        proc.wait(timeout=10)
        shutdown_succeeded = True
    finally:
        if hmp is not None:
            hmp.close()
        if proc.poll() is None:
            proc.terminate()
            proc.wait(timeout=3)
        log.close()

    assert shutdown_succeeded, "QEMU did not reach the tested shutdown terminal condition"
    assert proc.returncode == 0, f"QEMU ACPI exit status {proc.returncode}; see {qemu_log}"
    serial_text = serial.read_text(errors="replace")
    assert "Loaded embedded os.bin" in serial_text
    assert "Shutdown requested; final frame flushed." in serial_text
    assert "Requesting QEMU ACPI power-off." in serial_text
    assert "stopped unexpectedly" not in serial_text

    manifest = {
        "iso": str(iso),
        "iso_sha256": hashlib.sha256(iso.read_bytes()).hexdigest(),
        "kernel_sha256": hashlib.sha256((build / "dimon-kernel.elf").read_bytes()).hexdigest(),
        "qemu_exit": proc.returncode,
        "shutdown": "ACPI S5 process exit",
    }
    (out / "result.json").write_text(json.dumps(manifest, indent=2) + "\n")
    print(f"QEMU bare-metal regressions: PASS ({out})")


if __name__ == "__main__":
    main()
