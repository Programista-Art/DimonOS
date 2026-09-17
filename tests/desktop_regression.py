#!/usr/bin/env python3
"""Focused hosted regression coverage for retained desktop windows."""

import argparse
import json
import struct
import subprocess
from pathlib import Path

WIDTH = 800
HEIGHT = 600
BG = 0xFF1E2430
TITLE = 0xFF2563EB
MENU = 0xFF1E222D
RED = 0xFFFF3B30


def pixel(data, x, y):
    return struct.unpack_from("<I", data, (y * WIDTH + x) * 4)[0]


def crop(data, x, y, w, h):
    return b"".join(
        data[((y + row) * WIDTH + x) * 4:((y + row) * WIDTH + x + w) * 4]
        for row in range(h)
    )


class Runner:
    def __init__(self, root, build):
        self.root = root
        self.build = build
        self.out = build / "hosted-results"
        self.out.mkdir(parents=True, exist_ok=True)

    def run(self, name, events, steps=700_000, halt=False):
        vram = self.out / f"{name}.argb"
        state = self.out / f"{name}.json"
        log = self.out / f"{name}.log"
        if not halt:
            # Two F1 toggles leave the UI unchanged while guaranteeing that the
            # final queued action requests and completes a full repaint.
            events += ";k:260;k:260"
        cmd = [
            str(self.root / "dimon-emu"), "--headless",
            "--inject-events", events,
            "-m", str(steps),
            str(self.build / "os.bin"),
            "--dump-vram", str(vram),
            "--dump-state", str(state),
        ]
        if not halt:
            cmd.append("--stop-when-idle")
        result = subprocess.run(cmd, cwd=self.root, capture_output=True, text=True,
                                timeout=20, check=False)
        log.write_text(result.stdout + result.stderr)
        assert result.returncode == 0, f"{name}: emulator exit {result.returncode}; see {log}"
        status = json.loads(state.read_text())
        if halt:
            assert status["halted"] == 1 and status["run_result"] == 1, status
        else:
            assert status["halted"] == 0 and status["run_result"] == -100, status
            assert status["pending_events"] == 0, f"{name}: input dispatch stalled: {status}"
            assert status["idle_completed"] == 1, f"{name}: final frame did not complete: {status}"
        data = vram.read_bytes()
        assert len(data) == WIDTH * HEIGHT * 4
        return data, status


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--build-dir", default="build/regression")
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    build = (root / args.build_dir).resolve()
    run = Runner(root, build)

    # Original failure: after minimizing the only window, a subsequent Start
    # click must be consumed and painted. Calculator is hidden but retained.
    data, _ = run.run("single_minimized_interactive",
                      "k:49;p:520,80,1;p:50,580,1")
    assert pixel(data, 300, 120) == BG
    assert pixel(data, 20, 540) == MENU

    data, _ = run.run("single_minimize_restore",
                      "k:49;p:520,80,1;p:50,580,1;p:110,580,1")
    assert pixel(data, 245, 75) == TITLE
    assert pixel(data, 110, 580) == TITLE

    # Foreground Notepad minimizes to Calculator; Calculator accepts a click;
    # Notepad then restores from its stable taskbar slot.
    data, _ = run.run("multi_minimize_restore",
                      "k:49;k:261;p:675,55,1;p:290,330,1;p:190,580,1")
    assert pixel(data, 85, 50) == TITLE
    assert pixel(data, 245, 580) == TITLE

    data, _ = run.run("all_minimized_restore",
                      "k:49;k:261;p:675,55,1;p:520,80,1;p:50,580,1;p:110,580,1")
    assert pixel(data, 245, 75) == TITLE

    cycle = "k:49;" + ";".join(["p:520,80,1;p:110,580,1"] * 4)
    cycle += ";p:540,80,1;p:50,70,1"
    data, _ = run.run("repeat_min_restore_close_reopen", cycle, 1_000_000)
    assert pixel(data, 245, 75) == TITLE

    # App Close is deliberately separate from system Exit. Close foreground,
    # close the last window, then consume a desktop icon click.
    data, _ = run.run("app_close_foreground_and_last",
                      "k:49;k:261;p:700,55,1;p:540,80,1;p:50,70,1")
    assert pixel(data, 245, 75) == TITLE

    # A known value produced from keyboard is the rendering oracle for the same
    # value produced by real pointer button handlers after moving Calculator.
    ref, _ = run.run("calc_reference_three", "k:49;k:51")
    moved, _ = run.run(
        "calc_drag_release_and_buttons",
        "k:49;p:350,80,1;m:100,40,1;r:50,500,1;m:700,500,0;"
        "p:45,290,1;p:270,350,1;p:110,290,1;p:190,350,1",
        1_000_000,
    )
    assert pixel(moved, 5, 35) == TITLE, "Calculator did not move to clamped work area"
    assert pixel(moved, 485, 133) != TITLE, "release failed: idle motion kept dragging"
    assert crop(moved, 28, 98, 12, 16) == crop(ref, 268, 138, 12, 16), \
        "moved Calculator buttons did not produce 1+2=3"

    # Minimize is acted on at press and must never become a drag capture.
    data, _ = run.run("title_control_not_drag",
                      "k:49;p:520,80,1;m:400,200,1;r:400,200,1;p:110,580,1")
    assert pixel(data, 245, 75) == TITLE

    # Keyboard focus/Alt+Tab shares the same validated focus path.
    data, _ = run.run("alt_tab_focus", "k:49;k:261;k:9,4")
    assert pixel(data, 245, 75) == TITLE
    assert pixel(data, 110, 580) == TITLE

    # Retained Notepad and Paint state, including shared title dragging.
    retained = (
        "k:50;k:90;p:300,55,1;m:330,75,1;r:700,400,1;"
        "k:263;p:200,200,1;p:300,45,1;m:350,75,1;r:750,400,1;"
        "p:740,75,1;p:350,580,1"
    )
    paint, _ = run.run("retained_paint_after_move_min_restore", retained, 1_100_000)
    # Paint's 520px frame clamps to Y=48 (568px work-area bottom), so the
    # retained canvas point moves from (200,200) to (250,213).
    assert pixel(paint, 250, 213) == RED, "Paint canvas did not follow/retain content"

    note, _ = run.run("retained_notepad_after_focus_move_min_restore",
                      retained + ";p:200,580,1", 1_200_000)
    reference_note, _ = run.run("notepad_moved_reference",
                                "k:50;k:90;p:300,55,1;m:330,75,1;r:700,400,1")
    assert crop(note, 135, 155, 10, 16) == crop(reference_note, 135, 155, 10, 16)

    # Existing unsaved-document behavior retains initialized editor state when
    # its window entry is closed and reopened.
    reopened_note, _ = run.run("notepad_unsaved_close_reopen",
                               "k:50;k:90;p:700,55,1;p:50,140,1")
    plain_note, _ = run.run("notepad_plain_reference", "k:50;k:90")
    assert crop(reopened_note, 105, 135, 10, 16) == crop(plain_note, 105, 135, 10, 16)

    # Controls remain bounded after movement; taskbar status region is intact.
    assert pixel(moved, 269, 37) == 0xFF475569
    assert pixel(moved, 293, 37) == 0xFFEF4444
    assert pixel(moved, 650, 580) == 0xFF0F172A

    _, shutdown = run.run("hosted_system_shutdown", "k:260;k:120", halt=True)
    assert shutdown["steps"] < 100_000, shutdown

    print(f"hosted desktop regressions: PASS ({run.out})")


if __name__ == "__main__":
    main()
