#!/usr/bin/env python3
"""Focused hosted regression coverage for retained desktop windows."""

import argparse
import json
import shutil
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
        self.symbols = {}
        self.load_symbols()

    def load_symbols(self):
        map_file = self.build / "os.map"
        if not map_file.exists():
            res = subprocess.run([str(self.root / "dimon-as"), "os.asm", "-o", str(self.build / "os.bin")],
                                 cwd=self.root, capture_output=True, text=True)
            map_file.write_text(res.stdout)
        for line in map_file.read_text().splitlines():
            if "=" in line:
                p = line.split("=")
                self.symbols[p[0].strip()] = int(p[1].strip(), 16)

    def run(self, name, events, steps=700_000, halt=False, disk=None, disk_writable=False):
        vram = self.out / f"{name}.argb"
        ram = self.out / f"{name}.ram"
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
        ]
        if disk:
            cmd.extend(["--disk", str(disk)])
            if disk_writable:
                cmd.append("--disk-writable")
            else:
                cmd.append("--disk-readonly")
        cmd.extend([
            str(self.build / "os.bin"),
            "--dump-vram", str(vram),
            "--dump-ram", str(ram),
            "--dump-state", str(state),
        ])
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

    def read_ram(self, name):
        return (self.out / f"{name}.ram").read_bytes()

    def get_str(self, ram, sym, max_len=64):
        addr = self.symbols[sym]
        s = bytearray()
        for i in range(max_len):
            if addr + i >= len(ram) or ram[addr + i] == 0:
                break
            s.append(ram[addr + i])
        return s.decode("latin1")

    def get_u32(self, ram, sym):
        addr = self.symbols[sym]
        return struct.unpack_from("<I", ram, addr)[0]


def ensure_fixtures(root, build):
    fixtures = build / "fixtures"
    fixtures.mkdir(parents=True, exist_ok=True)
    large = fixtures / "LARGE.TXT"
    if not large.exists() or len(large.read_bytes()) != 4500:
        large.write_bytes(b"A" * 4500)
    badenc = fixtures / "BADENC.BIN"
    if not badenc.exists():
        badenc.write_bytes(bytes([0xFF, 0xFE, 0xFD, 0xFC, 0x80, 0x81, 0xC0, 0xAF, 0xE0, 0x80]))
    sub_notes = fixtures / "sub_notes.txt"
    if not sub_notes.exists():
        sub_notes.write_text("Subdirectory notes content\n")
    return fixtures


def make_test_disk(root, build, dst_path):
    fixtures = ensure_fixtures(root, build)
    cmd = [
        str(root / "dimon-mkiso"), "-o", str(dst_path),
        str(root / "apps/snake.app") + ":SNAKE.APP",
        str(fixtures / "LARGE.TXT") + ":LARGE.TXT",
        str(fixtures / "BADENC.BIN") + ":BADENC.BIN",
        "--force"
    ]
    subprocess.run(cmd, cwd=root, check=True, capture_output=True)
    subprocess.run([str(root / "dimon-mkiso"), "--mkdir", str(dst_path), "/DOCS"],
                   cwd=root, check=True, capture_output=True)
    subprocess.run([str(root / "dimon-mkiso"), "--add", str(dst_path),
                    str(fixtures / "sub_notes.txt") + ":/DOCS/NOTES.TXT"],
                   cwd=root, check=True, capture_output=True)


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
    # Taskbar buttons are compacted from x=100 in window-id order, so with
    # Notepad(id2)+Paint(id4) open the buttons sit at 100 and 178.
    retained = (
        "k:50;k:90;p:300,55,1;m:330,75,1;r:700,400,1;"
        "k:263;p:200,200,1;p:300,45,1;m:350,75,1;r:750,400,1;"
        "p:740,75,1;p:215,580,1"
    )
    paint, _ = run.run("retained_paint_after_move_min_restore", retained, 1_100_000)
    # Paint's 520px frame clamps to Y=48 (568px work-area bottom), so the
    # retained canvas point moves from (200,200) to (250,213).
    assert pixel(paint, 250, 213) == RED, "Paint canvas did not follow/retain content"

    note, _ = run.run("retained_notepad_after_focus_move_min_restore",
                      retained + ";p:130,580,1", 1_200_000)
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

    # Compact taskbar layout: open Calculator (app 1) and Notepad (app 2).
    # Buttons start at x=100 with stride 78. Padding after Start (x=8..92) is 8px.
    tb_data, _ = run.run("compact_taskbar_gapless", "k:49;k:261")
    assert pixel(tb_data, 96, 580) == 0xFF0F172A, "taskbar padding between Start and apps should be dark slate"
    assert pixel(tb_data, 110, 580) in (0xFF334155, 0xFF2563EB), "first app button should start at x=100"
    assert pixel(tb_data, 180, 580) in (0xFF334155, 0xFF2563EB), "second app button should start at x=178"

    # OpenDialog modal isolation and cancel:
    # F10 opens OpenDialog. List selection highlight sits at (140, 160).
    # Click on Start (50, 580) must be swallowed by modal isolation.
    # Esc (k:27) dismisses dialog, restoring Notepad focus.
    dlg_open, _ = run.run("file_dialog_open_cancel",
                          "k:50;k:269;p:50,580,1;k:27",
                          disk=build / "test-disk.img")
    assert pixel(dlg_open, 200, 200) == 0xFF0F172A, "Notepad editor not restored after dialog dismiss"

    # Save As dialog via toolbar button:
    # Notepad button [Save As] sits at X: 375..470, Y: 82..110.
    dlg_saveas, _ = run.run("file_dialog_saveas_btn",
                            "k:50;p:420,95,1",
                            disk=build / "test-disk.img")
    assert pixel(dlg_saveas, 125, 95) == TITLE, "Save As dialog frame title missing"
    assert pixel(dlg_saveas, 560, 375) == 0xFF059669, "Save As primary button missing"

    # Notepad dirty document workflow:
    # Typing 'X' marks document dirty. Close [X] shows Save/Discard/Cancel dialog.
    # Cancel (470, 330) keeps Notepad open. Close + Discard (350, 330) closes it.
    dirty_cancel, _ = run.run("notepad_dirty_cancel",
                              "k:50;k:88;p:700,55,1;p:470,330,1")
    assert pixel(dirty_cancel, 200, 200) == 0xFF0F172A, "Cancel did not keep dirty Notepad open"

    dirty_discard, _ = run.run("notepad_dirty_discard",
                               "k:50;k:88;p:700,55,1;p:350,330,1")
    assert pixel(dirty_discard, 200, 200) == BG, "Discard did not close Notepad window"

    # File Explorer metadata and sizes:
    # Explorer listing with test-disk.img shows decimal sizes in green and [Run] for snake.app
    fm_data, _ = run.run("file_explorer_metadata_sizes",
                         "k:51",
                         disk=build / "test-disk.img")
    assert pixel(fm_data, 105, 75) == TITLE, "File Explorer title missing"
    has_row0_size = any(pixel(fm_data, x, y) == 0xFF34C759
                        for y in range(146, 162) for x in range(410, 480))
    assert has_row0_size, "File Explorer row 0 size text missing"
    has_row3_size = any(pixel(fm_data, x, y) == 0xFF34C759
                        for y in range(236, 252) for x in range(410, 480))
    assert has_row3_size, "File Explorer row 3 size text missing"
    has_row3_run = any(pixel(fm_data, x, y) == 0xFF2563EB
                       for y in range(234, 256) for x in range(545, 680))
    assert has_row3_run, "File Explorer row 3 [Run] button missing"

    # -------------------------------------------------------------------------
    # Comprehensive Notepad Open/Save & File Dialog Regressions (Cases 1 - 13)
    # -------------------------------------------------------------------------
    base_disk = build / "test-disk.img"
    make_test_disk(root, build, base_disk)

    # Case 1: Open valid file via Open button
    # k:50 opens Notepad; p:200,95,1 clicks [Open]; p:200,155,1 selects NOTES.TXT (row 0); p:560,375,1 clicks [Open] dialog button
    run.run("test_case1_open_btn", "k:50;p:200,95,1;p:200,155,1;p:560,375,1", disk=base_disk)
    ram = run.read_ram("test_case1_open_btn")
    assert run.get_u32(ram, "dlg_active") == 0, "Dialog should be dismissed"
    assert run.get_str(ram, "note_path") == "/NOTES.TXT", f"Unexpected path: {run.get_str(ram, 'note_path')}"
    assert run.get_u32(ram, "note_len") == 127, f"Length mismatch: {run.get_u32(ram, 'note_len')}"
    assert run.get_u32(ram, "note_dirty") == 0, "Document should be clean"
    assert "Welcome to DimonOS-64 Notepad" in run.get_str(ram, "note_buf", 60)

    # Case 2: Open valid file via Enter in filename field
    # k:50 opens Notepad; p:200,95,1 clicks [Open]; p:200,184,1 selects README.TXT (row 1); k:13 activates Enter
    run.run("test_case2_open_enter", "k:50;p:200,95,1;p:200,184,1;k:13", disk=base_disk)
    ram = run.read_ram("test_case2_open_enter")
    assert run.get_u32(ram, "dlg_active") == 0
    assert run.get_str(ram, "note_path") == "/README.TXT"
    assert run.get_u32(ram, "note_len") == 150
    assert run.get_u32(ram, "note_dirty") == 0
    assert "Modern TrueColor Linear Framebuffer" in run.get_str(ram, "note_buf", 60)

    # Case 3: Open valid file via double-click in file list
    # k:50 opens Notepad; p:200,95,1 clicks [Open]; row 2 (TODO.TXT) is at Y=204; double click at 200, 204
    run.run("test_case3_open_dblclick", "k:50;p:200,95,1;p:200,204,1;p:200,204,1", disk=base_disk)
    ram = run.read_ram("test_case3_open_dblclick")
    assert run.get_u32(ram, "dlg_active") == 0
    assert run.get_str(ram, "note_path") == "/TODO.TXT"
    assert run.get_u32(ram, "note_len") == 108
    assert run.get_u32(ram, "note_dirty") == 0
    assert "Paint TrueColor artwork" in run.get_str(ram, "note_buf", 60)

    # Case 4: Open valid file typed in lowercase (notes.txt)
    # k:50 opens Notepad; p:200,95,1 clicks [Open]; type "notes.txt" then Enter (13)
    type_notes = "k:50;p:200,95,1;k:110;k:111;k:116;k:101;k:115;k:46;k:116;k:120;k:116;k:13"
    run.run("test_case4_typed_lowercase", type_notes, disk=base_disk)
    ram = run.read_ram("test_case4_typed_lowercase")
    assert run.get_u32(ram, "dlg_active") == 0
    assert run.get_u32(ram, "note_len") == 127
    assert run.get_u32(ram, "note_dirty") == 0
    assert "Welcome to DimonOS-64 Notepad" in run.get_str(ram, "note_buf", 60)

    # Case 5: Open non-existent file -> truthful error, document unchanged
    type_missing = "k:50;p:200,95,1;k:77;k:73;k:83;k:83;k:73;k:78;k:71;k:46;k:84;k:88;k:84;k:13"
    run.run("test_case5_missing_file", type_missing, disk=base_disk)
    ram = run.read_ram("test_case5_missing_file")
    assert run.get_u32(ram, "dlg_active") == 1, "Dialog must remain active on error"
    assert run.get_str(ram, "dlg_err") == "File not found", f"Unexpected error: {run.get_str(ram, 'dlg_err')}"
    assert run.get_str(ram, "dlg_name") == "MISSING.TXT"
    assert "Welcome to DimonOS-64 Notepad" in run.get_str(ram, "note_buf", 60), "Buffer must remain unchanged"
    assert run.get_u32(ram, "note_len") == 127

    # Case 6: Open file exceeding buffer size -> rejected, document unchanged
    type_large = "k:50;p:200,95,1;k:76;k:65;k:82;k:71;k:69;k:46;k:84;k:88;k:84;k:13"
    run.run("test_case6_large_file", type_large, disk=base_disk)
    ram = run.read_ram("test_case6_large_file")
    assert run.get_u32(ram, "dlg_active") == 1
    assert run.get_str(ram, "dlg_err") == "File exceeds 4000 bytes", f"Unexpected error: {run.get_str(ram, 'dlg_err')}"
    assert run.get_str(ram, "dlg_name") == "LARGE.TXT"
    assert "Welcome to DimonOS-64 Notepad" in run.get_str(ram, "note_buf", 60)
    assert run.get_u32(ram, "note_len") == 127

    # Case 7: Open non-UTF-8 binary file -> rejected, document unchanged
    type_badenc = "k:50;p:200,95,1;k:66;k:65;k:68;k:69;k:78;k:67;k:46;k:66;k:73;k:78;k:13"
    run.run("test_case7_bad_encoding", type_badenc, disk=base_disk)
    ram = run.read_ram("test_case7_bad_encoding")
    assert run.get_u32(ram, "dlg_active") == 1
    assert run.get_str(ram, "dlg_err") == "Not valid UTF-8 text", f"Unexpected error: {run.get_str(ram, 'dlg_err')}"
    assert run.get_str(ram, "dlg_name") == "BADENC.BIN"
    assert "Welcome to DimonOS-64 Notepad" in run.get_str(ram, "note_buf", 60)
    assert run.get_u32(ram, "note_len") == 127

    # Case 8: Save untitled document -> Save As dialog opens -> type name -> saves successfully
    disk8 = build / "test-case8.img"
    shutil.copyfile(base_disk, disk8)
    save_untitled = "k:50;p:120,95,1;k:78;k:69;k:87;k:32;k:68;k:79;k:67;k:268;k:84;k:69;k:83;k:84;k:46;k:84;k:88;k:84;k:13"
    run.run("test_case8_save_untitled", save_untitled, disk=disk8, disk_writable=True)
    ram = run.read_ram("test_case8_save_untitled")
    assert run.get_u32(ram, "dlg_active") == 0
    assert run.get_str(ram, "note_path") == "/TEST.TXT"
    assert run.get_u32(ram, "note_dirty") == 0
    assert run.get_str(ram, "note_status_str") == "FAT16: Saved to Disk!"
    assert b"NEW DOC" in disk8.read_bytes(), "Saved text not found on disk"

    # Case 9: Save existing file -> updates in place, marks document clean
    disk9 = build / "test-case9.img"
    shutil.copyfile(base_disk, disk9)
    save_existing = "k:50;k:33;k:268"
    run.run("test_case9_save_existing", save_existing, disk=disk9, disk_writable=True)
    ram = run.read_ram("test_case9_save_existing")
    assert run.get_u32(ram, "dlg_active") == 0
    assert run.get_str(ram, "note_path") == "/NOTES.TXT"
    assert run.get_u32(ram, "note_dirty") == 0
    assert run.get_str(ram, "note_status_str") == "FAT16: Saved to Disk!"
    assert run.get_u32(ram, "note_len") == 128
    assert b"!Welcome to DimonOS-64 Notepad" in disk9.read_bytes(), "Saved text not on disk"

    # Case 10: Save As with existing name -> overwrite prompt -> No preserves dialog and doc -> Yes overwrites
    disk10 = build / "test-case10.img"
    shutil.copyfile(base_disk, disk10)
    save_overwrite = (
        "k:50;p:120,95,1;k:79;k:86;k:69;k:82;k:87;k:82;k:73;k:84;k:69;p:420,95,1;"
        "k:82;k:69;k:65;k:68;k:77;k:69;k:46;k:84;k:88;k:84;k:13;"
        "p:389,334,1;k:13;p:267,334,1"
    )
    run.run("test_case10_overwrite", save_overwrite, disk=disk10, disk_writable=True)
    ram = run.read_ram("test_case10_overwrite")
    assert run.get_u32(ram, "dlg_active") == 0
    assert run.get_str(ram, "note_path") == "/README.TXT"
    assert run.get_u32(ram, "note_dirty") == 0
    assert run.get_u32(ram, "note_len") == 9
    assert b"OVERWRITE" in disk10.read_bytes()

    # Case 11: Invalid 8.3 filename rejected with truthful error strings
    invalid_cases = [
        ("TOOLONGNAME.TXT", "k:84;k:79;k:79;k:76;k:79;k:78;k:71;k:78;k:65;k:77;k:69;k:46;k:84;k:88;k:84;k:13", "Name exceeds 8 characters"),
        ("BAD*CHAR.TXT", "k:66;k:65;k:68;k:42;k:67;k:72;k:65;k:82;k:46;k:84;k:88;k:84;k:13", "Invalid character in filename"),
        (".NOBASE", "k:46;k:78;k:79;k:66;k:65;k:83;k:69;k:13", "Missing base name"),
        ("TWO..DOTS", "k:84;k:87;k:79;k:46;k:46;k:68;k:79;k:84;k:83;k:13", "Multiple dots not allowed"),
        ("DIR/FILE.TXT", "k:68;k:73;k:82;k:47;k:70;k:73;k:76;k:69;k:46;k:84;k:88;k:84;k:13", "No path separators in filename"),
        ("TEST.EXTN", "k:84;k:69;k:83;k:84;k:46;k:69;k:88;k:84;k:78;k:13", "Extension exceeds 3 characters"),
    ]
    for label, keys, expected_err in invalid_cases:
        seq = f"k:50;p:120,95,1;p:420,95,1;{keys}"
        safe_name = f"test_case11_{label.replace('/', '_')}"
        run.run(safe_name, seq, disk=base_disk)
        ram = run.read_ram(safe_name)
        assert run.get_u32(ram, "dlg_active") == 2, f"Dialog should stay open for {label}"
        err = run.get_str(ram, "dlg_err")
        assert err == expected_err, f"For {label}: expected {expected_err!r}, got {err!r}"

    # Case 12: Path handling: root displays /; opening from subdirectory works
    run.run("test_case12_root_slash", "k:50;p:200,95,1", disk=base_disk)
    ram = run.read_ram("test_case12_root_slash")
    assert run.get_str(ram, "dlg_dir") == "/", f"Expected '/', got {run.get_str(ram, 'dlg_dir')!r}"
    assert run.get_str(ram, "dlg_path") == "/", f"Expected '/', got {run.get_str(ram, 'dlg_path')!r}"

    seq_sub = "k:50;p:200,95,1;p:200,244,1;p:200,244,1;p:200,184,1;p:200,184,1"
    run.run("test_case12_subdir_open", seq_sub, disk=base_disk)
    ram = run.read_ram("test_case12_subdir_open")
    assert run.get_u32(ram, "dlg_active") == 0
    assert run.get_str(ram, "note_path") == "/DOCS/NOTES.TXT", f"Unexpected path: {run.get_str(ram, 'note_path')}"
    assert run.get_u32(ram, "note_len") == 27, f"Length mismatch: {run.get_u32(ram, 'note_len')}"
    assert "Subdirectory notes content" in run.get_str(ram, "note_buf", 32)

    # Case 13: Read-only media failure handling: error reported, dirty state preserved
    seq_ro = "k:50;k:65;k:268"
    run.run("test_case13_readonly", seq_ro, disk=base_disk, disk_writable=False)
    ram = run.read_ram("test_case13_readonly")
    assert run.get_u32(ram, "note_dirty") == 1, "Document dirty state must be preserved on read-only write failure"
    status_msg = run.get_str(ram, "note_status_str")
    assert status_msg == "Save failed: read-only disk", f"Expected read-only failure status, got: {status_msg!r}"
    assert "Saved" not in status_msg, "Must not report saved on read-only failure"

    _, shutdown = run.run("hosted_system_shutdown", "k:260;k:120", halt=True)
    assert shutdown["steps"] < 100_000, shutdown

    print(f"hosted desktop regressions: PASS ({run.out})")


if __name__ == "__main__":
    main()
