# Changelog - DimonOS-64

This document chronologically tracks all changes for DimonVirtualCPU-64 and DimonOS-64.

---

## [2026-09-17] - Fix Notepad Open and Save Workflow & FAT Shortname Validation

- **Root-Cause Filesystem Fix (`dimonfs.c`)**:
  - Fixed `split_parent()` root-path truncation bug where `slash == clean` caused `slash[1] = 0` to overwrite the first character of the filename (e.g. turning `"/NOTES.TXT"` into `"/\0OTES.TXT"`), causing root-directory file lookups to fail with `DFS_ERR_NOT_FOUND` or invalid name.
  - Added leading and trailing whitespace trimming in `name83()` and `entry_usable()` volume label filtering.
- **FAT Shortname Validation Routine (`os.asm`)**:
  - Implemented shared `fat_validate_shortname` enforcing canonical FAT 8.3 rules with truthful user-facing error strings:
    - Base name up to 8 characters (`Name exceeds 8 characters`).
    - Extension up to 3 characters (`Extension exceeds 3 characters`).
    - At most one dot (`Multiple dots not allowed`).
    - Non-empty base name (`Missing base name`).
    - Rejection of path separators in filename input (`No path separators in filename`).
    - Rejection of invalid characters (`Invalid character in filename`).
  - Whitespace-tolerant canonical 11-byte FAT shortname generation.
- **Notepad Open & Save Improvements**:
  - Default folder path initialized to `/` at root (never blank) and support for subdirectory navigation (e.g. `/DOCS/NOTES.TXT`).
  - Staging buffer (`note_staging_buf`) and UTF-8 pre-validation preventing editor buffer clobbering when loading corrupt or oversized (>4000 B) files.
  - Activation of Open dialog via Open button, Enter key, and list double-click.
  - Case-insensitive filename matching (e.g. typing lowercase `notes.txt`).
  - Mode 4 Overwrite confirmation modal dialog (`[Yes]` overwrites, `[No]` preserves Save As dialog & doc, `[Cancel]` dismisses).
  - Truthful status reporting on read-only media (`Save failed: read-only disk`, document dirty marker `*` preserved, never reports "Saved").
  - Fixed emulator read-only disk handling (`vm.c`, `vm_disk_attach`) ensuring `--disk-readonly` correctly sets `disk_writable = 0`.
- **Automated Regressions (`tests/desktop_regression.py`)**:
  - Added full automated suite for all 13 Notepad Open/Save test cases using real pointer and keyboard event paths with RAM and VRAM inspection.
  - 100% test pass rate across `make test`, `make regression-hosted`, and `make regression-qemu`.

---

## [2026-09-17] - Native File Dialogs, Notepad Workflow, Explorer Dispatcher, Compact Taskbar & Fullscreen Emulator

- **Native Modal Guest File Dialogs (`OpenDialog`, `SaveDialog`)**:
  - Implemented Delphi-style modal guest file dialogs for FAT16 browsing with editable folder/path and filename fields.
  - Keyboard & mouse list navigation, scrolling, single-click selection, double-click activation, text filter (*.TXT), and 8.3 name enforcement.
  - Complete modal isolation: clicks and key events outside the dialog bounds are swallowed without bleeding into underlying windows.
- **Coherent Notepad Document Workflow**:
  - Support for untitled documents, modified indicator `*` in title bar upon edit.
  - Save writes directly to the document path or triggers Save As when untitled.
  - Save As validates existing files and prompts for overwrite confirmation.
  - Save/Discard/Cancel dirty prompt on New, Open, Close, and OS Shutdown.
  - Buffer limit (<4000 B) and valid UTF-8 validation before replacing document content.
  - Full keyboard shortcuts: F2/F9/Ctrl+S (Save), F3 (Save As), F10/Ctrl+O (Open), Ctrl+N (New).
- **File Explorer Metadata & Type Dispatcher**:
  - Fixed initial 0 B file size bug using `SYS_FS_LIST`, formatting sizes as decimal B/KB/MB and directories as `<DIR>`.
  - Replaced unconditional "Open in Notepad" with unified type dispatcher:
    directories navigate into subfolders; `.TXT` files open in Notepad; `.APP` executables launch via `SYS_APP_EXEC` loader (e.g. `snake.app`); unsupported files report error status.
  - Per-type action button labels (`[Open]`, `[Run]`, `[Edit]`, `[Info]`).
- **Compact Taskbar**:
  - Eliminated the unused reserved gap immediately to the right of Start.
  - Open and minimized application buttons pack compactly starting at x=100 with stride 78.
- **Hosted Virtual CPU Emulator Fullscreen Mode**:
  - F11 fullscreen toggle and `--fullscreen`/`-f` CLI flags.
  - Centered 4:3 letterboxed/pillarboxed viewport maintaining crisp nearest-neighbor bitmap text and scaled hardware cursor.
  - Pointer coordinate mapping accounting for letterbox offsets, ignoring clicks in borders, and clearing capture on release and FocusOut.
  - Discoverable window title with `[F11: Fullscreen]` and `[F11: Windowed]` indicators.
- **Automated Verification**:
  - Extended `tests/desktop_regression.py` and `tests/x11_regression.py` with test coverage for file dialogs, dirty workflow, Explorer metadata, compact taskbar, and F11 fullscreen toggle.
  - Automated tests passing 100% across hosted headless, X11 with XTEST, and QEMU bare-metal.

---

## [2026-09-17] - Multi-window regression repair

- Fixed the minimize return-address loop that left the desktop unable to
  dispatch later input, and bounded/validated related focus and Z-order scans.
- Added ordered hosted input, real X11, and automated QEMU regression coverage
  for minimize/restore, Close, dragging/release, retained state and shutdown.
- Added an explicit shutdown frame and QEMU ACPI power-off with a defined halt
  fallback; isolated all test disk/ISO artifacts from `dimon.iso`.

---

## [2026-09-13] - DimonOS-64 Interactive Desktop GUI (64-bit RISC-V)

- `os.asm`: full interactive desktop (blue `░` background, top bar,
  7 program icons, green `[ START ]` taskbar with window title, hints
  and live worker-tick clock, Start menu with `X. Exit OS`), window
  manager with `[X]` close buttons, and 7 apps (Calculator with mouse
  keypad, Notepad, File Explorer, Paint canvas, System Info, playable
  Snake, Terminal CLI). Infinite event loop (mouse/key/timer), `F1`
  toggles the menu, `EBREAK` only on explicit exit. Zero console I/O
  after GUI init; GUI shell (Task 0) + background worker (Task 1).
- `emulator.c`: bare `tui`/`gui`/`headless` positionals, `--headless`
  memory-only mode, `--inject-keys`/`--inject-click` scripted input,
  `--dump-vram` VRAM snapshots for automated GUI tests.
- `examples/gui_test.asm`: ported to 64-bit (halting VRAM smoke test).
- `Makefile test`: non-hanging headless asserts (desktop, menu,
  all 7 apps, calculator math, multitasking counters).

## [2026-09-13] - DimonVirtualCPU-64 & DimonOS-64 (64-bit RISC + Multitasking)

### 1. Architecture Refactor (16-bit CISC -> 64-bit RISC)
- Replaced `dimon16.h` with `dimon64.h`: 32x64-bit GPRs (R0-R31 with ABI names),
  64-bit PC/FLAGS/TIMERTICKS, 64MB RAM via `malloc`, MMIO at high offsets.
- Fixed 32-bit RISC-V style formats: R/I/S/B/U/J with opcodes, funct3/funct7.
- Implemented full 64-bit arithmetic, shifts, branches, loads/stores with
  bounds validation, `R0 == 0` enforcement and 4-byte PC alignment.
- Syscall dispatcher via `a7` (ID) and `a0-a6` (args), return in `a0`.
- Hardware timer tick every N cycles, IVT vector, EPC/EFLAGS trap save,
  atomic `IRET` return, IE flag control.

### 2. Toolchain
- `assembler.c (dimon-as)`: 64-bit immediates, 32 registers + ABI names,
  fixed 4-byte LE words, `.quad/.dword/.word/.byte/.string/.space/.align`,
  pseudo-ops `LI/LA/MV/NOP/J/RET/CALL/HLT/INT`, PC-relative branch/jump
  validation, `.include` support.
- `vm.c`: rewritten decode loop, timer interrupt engine, native round-robin
  scheduler for 8 tasks x 64KB stacks, YIELD/SPAWN/EXIT/SLEEP/GETPID,
  MMIO serial/VRAM/timer, disk and GUI callbacks, 64-bit disassembler.
- `emulator.c (dimon-emu)`: 64-bit register dump (16 hex digits),
  32-bit disassembly, headless/TUI/X11 display modes.
- `Makefile`: clean 64-bit flags `-Wall -Wextra -O2 -std=c11`.

### 3. Operating System and Examples
- `os.asm (os.bin)`: English multitasking kernel, IVT + timer ISR,
  Process 0 shell plus counter/clock/monitor workers, distinct VRAM rows,
  cooperative SLEEP and preemptive timer switching, clean halts.
- `examples/`: ported `hello.asm`, `math.asm`, `fib.asm`, added
  `multitask.asm` (two timer-preempted tasks, distinct screen areas).
- `README.md` and `docs/`: ISA manual, ABI/syscall reference,
  multitasking guide and user manual, all in English.

---

## [2026-09-12] - DimonOS v2.1 (Legacy 16-bit, superseded)

Previous 16-bit CISC system with 64KB RAM and 8 registers.
Superseded by the 64-bit refactor above; legacy GUI/apps sources
remain on disk for reference but are no longer built.
