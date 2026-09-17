#!/usr/bin/env python3
"""Drive Calculator through the hosted X11 backend with XTEST events."""

import argparse
import ctypes
import json
import os
import re
import subprocess
import time
from ctypes import c_bool, c_char_p, c_int, c_long, c_uint, c_ulong, c_void_p
from pathlib import Path

WIDTH = 800
TITLE_ARGB_LE = bytes((0xEB, 0x63, 0x25, 0xFF))


class ClientData(ctypes.Union):
    _fields_ = [("l", c_long * 5)]


class ClientMessage(ctypes.Structure):
    _fields_ = [
        ("type", c_int), ("serial", c_ulong), ("send_event", c_int),
        ("display", c_void_p), ("window", c_ulong),
        ("message_type", c_ulong), ("format", c_int), ("data", ClientData),
    ]


class XEvent(ctypes.Union):
    _fields_ = [("xclient", ClientMessage), ("pad", c_long * 24)]


def wait_window(timeout=8):
    end = time.monotonic() + timeout
    pattern = re.compile(r"^\s*(0x[0-9a-f]+).*DimonOS-64 Modern TrueColor GUI", re.I)
    while time.monotonic() < end:
        out = subprocess.run(["xwininfo", "-root", "-tree"], capture_output=True,
                             text=True, check=False).stdout
        for line in out.splitlines():
            found = pattern.search(line)
            if found:
                return int(found.group(1), 16)
        time.sleep(0.1)
    raise AssertionError("X11 emulator window did not appear")


def pixel(data, x, y):
    start = (y * WIDTH + x) * 4
    return data[start:start + 4]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--build-dir", default="build/regression")
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    build = (root / args.build_dir).resolve()
    out = build / "x11-results"
    out.mkdir(parents=True, exist_ok=True)
    vram = out / "calculator-drag.argb"
    state = out / "calculator-drag.json"
    log_path = out / "emulator.log"
    input_log = out / "input.log"

    assert os.environ.get("DISPLAY"), "DISPLAY is not set; X11 regression cannot run"
    log = log_path.open("w")
    proc = subprocess.Popen([
        str(root / "dimon-emu"), "--gui", "--scale", "1",
        "-m", "100000000", str(build / "os.bin"),
        "--dump-vram", str(vram), "--dump-state", str(state),
        "--input-log", str(input_log),
    ], cwd=root, stdout=log, stderr=subprocess.STDOUT)

    x11 = ctypes.CDLL("libX11.so.6")
    xtst = ctypes.CDLL("libXtst.so.6")
    x11.XOpenDisplay.argtypes = [c_char_p]
    x11.XOpenDisplay.restype = c_void_p
    x11.XDefaultRootWindow.argtypes = [c_void_p]
    x11.XDefaultRootWindow.restype = c_ulong
    x11.XTranslateCoordinates.argtypes = [c_void_p, c_ulong, c_ulong, c_int, c_int,
                                           ctypes.POINTER(c_int), ctypes.POINTER(c_int),
                                           ctypes.POINTER(c_ulong)]
    x11.XKeysymToKeycode.argtypes = [c_void_p, c_ulong]
    x11.XKeysymToKeycode.restype = c_uint
    x11.XStringToKeysym.argtypes = [c_char_p]
    x11.XStringToKeysym.restype = c_ulong
    x11.XInternAtom.argtypes = [c_void_p, c_char_p, c_bool]
    x11.XInternAtom.restype = c_ulong
    x11.XSendEvent.argtypes = [c_void_p, c_ulong, c_bool, c_long, ctypes.POINTER(XEvent)]
    xtst.XTestFakeKeyEvent.argtypes = [c_void_p, c_uint, c_bool, c_ulong]
    xtst.XTestFakeButtonEvent.argtypes = [c_void_p, c_uint, c_bool, c_ulong]
    xtst.XTestFakeMotionEvent.argtypes = [c_void_p, c_int, c_int, c_int, c_ulong]

    display = None
    try:
        window = wait_window()
        display = x11.XOpenDisplay(None)
        assert display
        root_window = x11.XDefaultRootWindow(display)
        origin_x, origin_y, child = c_int(), c_int(), c_ulong()
        ok = x11.XTranslateCoordinates(display, window, root_window, 0, 0,
                                       ctypes.byref(origin_x), ctypes.byref(origin_y),
                                       ctypes.byref(child))
        assert ok

        def flush_wait(delay=0.18):
            x11.XFlush(display)
            time.sleep(delay)

        def key(name):
            sym = x11.XStringToKeysym(name.encode())
            code = x11.XKeysymToKeycode(display, sym)
            assert code
            xtst.XTestFakeKeyEvent(display, code, True, 0)
            xtst.XTestFakeKeyEvent(display, code, False, 0)
            flush_wait()

        def move(x, y):
            xtst.XTestFakeMotionEvent(display, -1, origin_x.value + x,
                                      origin_y.value + y, 0)
            flush_wait(0.08)

        def button(down):
            xtst.XTestFakeButtonEvent(display, 1, down, 0)
            flush_wait(0.08)

        def click(x, y):
            move(x, y)
            button(True)
            button(False)

        # A real click lets the window manager establish focus before XTEST
        # keyboard input; the unused top-bar edge has no guest-side action.
        click(790, 10)
        key("1")
        move(350, 80)
        button(True)
        move(100, 40)       # pointer capture outside original title bounds
        button(False)
        move(700, 500)      # release must stop dragging

        # Use Calculator controls at their translated screen positions: 1+2=3.
        for point in ((45, 290), (270, 350), (110, 290), (190, 350)):
            click(*point)

        # Normal X11 WM_DELETE path stops the hosted emulator after preserving
        # the last guest frame for assertions.
        event = XEvent()
        event.xclient.type = 33  # ClientMessage
        event.xclient.display = display
        event.xclient.window = window
        event.xclient.message_type = x11.XInternAtom(display, b"WM_PROTOCOLS", False)
        event.xclient.format = 32
        event.xclient.data.l[0] = x11.XInternAtom(display, b"WM_DELETE_WINDOW", False)
        event.xclient.data.l[1] = 0
        assert x11.XSendEvent(display, window, False, 0, ctypes.byref(event))
        flush_wait(0.05)
        proc.wait(timeout=8)
    finally:
        if display:
            x11.XCloseDisplay(display)
        if proc.poll() is None:
            proc.terminate()
            proc.wait(timeout=3)
        log.close()

    assert proc.returncode == 0, f"X11 emulator failed; see {log_path}"
    status = json.loads(state.read_text())
    assert status["halted"] == 1 and status["run_result"] == 1, status
    data = vram.read_bytes()
    assert pixel(data, 5, 35) == TITLE_ARGB_LE
    assert pixel(data, 485, 133) != TITLE_ARGB_LE, "X11 button release did not end capture"

    # Compare the rendered result with the headless known-result oracle.
    reference = (build / "hosted-results" / "calc_reference_three.argb").read_bytes()
    moved_glyph = b"".join(data[((98 + y) * WIDTH + 28) * 4:
                                  ((98 + y) * WIDTH + 40) * 4] for y in range(16))
    ref_glyph = b"".join(reference[((138 + y) * WIDTH + 268) * 4:
                                     ((138 + y) * WIDTH + 280) * 4] for y in range(16))
    assert moved_glyph == ref_glyph, "X11 moved Calculator buttons did not yield 3"
    print(f"hosted X11 drag regression: PASS ({out})")


if __name__ == "__main__":
    main()
