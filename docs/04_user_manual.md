# 04. DimonOS-64 User Manual

## Prerequisites

```bash
sudo apt update
sudo apt install build-essential libx11-dev
```

## Build

```bash
make all os.bin apps
make disk-new       # only when creating a new disk image
```

Produces `dimon-as`, `dimon-emu`, `dimon-mkiso`, `os.bin` and
`apps/snake.app` with `-Wall -Wextra -O2 -std=c11`. `disk-new` refuses
to overwrite `dimon.iso`; existing user files are therefore preserved.
Use `make disk-install-apps` to add Snake to an existing volume without
reformatting it. It refuses a same-name entry unless the explicit
`dimon-mkiso --add ... --force` form is used.

## Run

```bash
./dimon-emu --disk dimon.iso --disk-writable os.bin
                                      # X11 desktop with persistent FAT16
./dimon-emu os.bin tui              # ANSI terminal desktop (also --tui)
./dimon-emu os.bin gui              # force X11 window (also --gui)
./dimon-emu examples/multitask.bin  # two timer-preempted tasks (halts)
./dimon-emu -d os.bin               # 64-bit debugger (s/c/r/m/u/b/q)
./dimon-emu -t examples/fib.bin     # trace each 32-bit instruction
./dimon-emu -s 0x0 -l 0x0 os.bin    # explicit start/load addresses
```

The desktop runs an event loop until the Start-menu `X. Exit OS` command is
selected. `Esc` and a window's red `X` close only the focused application; they
do not shut down the system.

On the hosted emulator, `X. Exit OS` draws the final shutdown frame and exits
the emulator process cleanly. On bare metal, the kernel flushes that frame and
requests QEMU ACPI S5 power-off. If the platform does not implement that
mechanism, the final screen says `System halted. You may close QEMU.` and the
kernel enters its defined interrupt-disabled halt loop. Disk writes in the
hosted backend are synchronous (`fflush`/`fclose` per write); the embedded
bare-metal FAT16 test disk is read-only, so no pending write is abandoned.

## Desktop Controls

- Mouse: click `[ START ]`, menu items, top-bar shortcuts, desktop
  icons, calculator keypad and paint canvas. Drag a title bar to move its
  window; `-` minimizes and `X` closes it. Dragging remains captured until the
  mouse button is released.
- Keyboard: `F1` Start menu, `F2`-`F7` / `1`-`7` open apps,
  `Alt+Tab` cycles and restores windows, `Esc` closes the focused window/menu.
- Each open app has a fixed taskbar slot. Clicking an inactive/minimized slot
  focuses or restores it; clicking the active slot minimizes it. All seven
  slots fit between Start and the clock without overlap.
- Apps: Calculator (mouse + keys), Notepad, File Explorer
  (Up/Down), Paint (arrows/Space, `1`-`6`, `R G B Y W`, `C`),
  System Info, Snake (arrows, `R`), Terminal
  (`help info pwd ls run PATH clear exit`).
- Notepad: arrows, Home/End, Backspace/Delete, F9 or Ctrl+S to save,
  F10 to open. The current editor limit is 4000 bytes; larger files are
  rejected without truncation.
- `run SNAKE.APP` starts the packaged isolated app. Esc exits Snake.

## Expected Output

- `hello`: `Hello, DimonVirtualCPU-64!`
- `math`: `35 42 42 42 48 255 85 32 8` (one per line)
- `fib`: `0 1 1 2 3 5 8 13 21 34`
- `multitask`: interleaved `[Main]`, `[Task1 upper]`, `[Task2 lower]`
  with VRAM rows 10/15, ending with both `done` lines.
- `os` (screen): top bar `[ DimonOS-64 ]` with bounded shortcut/status regions,
  blue `░` desktop with 7 icons, bottom taskbar with green
  `[ START ]`, open-app buttons and a real `UTC <Unix-seconds>` clock.
  Bare-metal builds without an RTC display `RTC N/A`.
  `--inject-keys "1"` opens Calculator; `F1` opens the Start menu.

The seven desktop apps are retained embedded modules (one instance per app).
Standalone DEXE64 programs launched with `run` still use their existing
exclusive display and are not yet hosted inside managed desktop windows.

## Focused desktop regression tests

```bash
make regression-hosted   # ordered normal input path, state and rendering
make regression-x11      # real X11 key/button/motion/release via XTEST
make regression-qemu     # fresh isolated FAT16/kernel/ISO and PS/2 input
make regression          # all three
```

The X11 target requires an accessible `DISPLAY` and `libXtst`. The QEMU target
requires `qemu-system-i386` and `grub-mkrescue`. Results and screenshots are
written below `build/regression/`; the repository's `dimon.iso` is never used
as a test fixture.

Context switches are visible as interleaving; `-r` shows
`ticks` and `switches` counters.

## Debugger

```
s [N]      step N (default 1)
c          continue to HLT/error/breakpoint
r          64-bit registers (16 hex digits)
m ADDR [N] hexdump N bytes at 64-bit ADDR
u [A] [N]  disassemble N 32-bit words
b ADDR     breakpoint, `b c` clears
q          quit
```

## Troubleshooting

- `Execution error: -1` — illegal opcode (usually jumped to data;
  check JAL/Branch ranges +/-1MB and +/-4KB).
- `branch target out of range` — split far branches via `JAL` or
  invert condition + `J`.
- `jump target out of range` — keep programs under 1MB or use
  `LA + JALR` for far calls.
- Hangs in `os`/`multitask` — ensure every task ends with `SYSEXIT`
  and sleeps use `SYSSLEEP`, not busy loops without `YIELD`.
