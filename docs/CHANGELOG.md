# Changelog - DimonOS-64

This document chronologically tracks all changes for DimonVirtualCPU-64 and DimonOS-64.

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
