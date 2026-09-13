# 04. DimonOS-64 User Manual

## Prerequisites

```bash
sudo apt update
sudo apt install build-essential libx11-dev
```

## Build

```bash
make clean && make all && make test
```

Produces `dimon-as`, `dimon-emu`, `dimon-mkiso`, `os.bin` and
`examples/{hello,math,fib,multitask}.bin` with `-Wall -Wextra -O2 -std=c11`.

## Run

```bash
./dimon-emu os.bin                  # X11 desktop (or TUI if no DISPLAY)
./dimon-emu os.bin tui              # ANSI terminal desktop (also --tui)
./dimon-emu os.bin gui              # force X11 window (also --gui)
./dimon-emu examples/multitask.bin  # two timer-preempted tasks (halts)
./dimon-emu -d os.bin               # 64-bit debugger (s/c/r/m/u/b/q)
./dimon-emu -t examples/fib.bin     # trace each 32-bit instruction
./dimon-emu --headless -m 500000 os.bin -r   # smoke test, no display
./dimon-emu -s 0x0 -l 0x0 os.bin    # explicit start/load addresses
```

The OS runs an infinite event loop: use the Start menu (`X. Exit OS`),
`q` on the desktop, Esc, or the window close button to quit.

## Desktop Controls

- Mouse: click `[ START ]`, menu items, top-bar shortcuts, desktop
  icons, calculator keypad, paint canvas, window `[X]` button.
- Keyboard: `F1` Start menu, `F2`-`F7` / `1`-`7` open apps,
  `Esc` closes window/menu, `q` quits on the desktop.
- Apps: Calculator (mouse + keys), Notepad, File Explorer
  (Up/Down), Paint (arrows/Space, `1`-`6`, `R G B Y W`, `C`),
  System Info, Snake (arrows, `R`), Terminal (`help info clear exit`).

## Expected Output

- `hello`: `Hello, DimonVirtualCPU-64!`
- `math`: `35 42 42 42 48 255 85 32 8` (one per line)
- `fib`: `0 1 1 2 3 5 8 13 21 34`
- `multitask`: interleaved `[Main]`, `[Task1 upper]`, `[Task2 lower]`
  with VRAM rows 10/15, ending with both `done` lines.
- `os` (VRAM dump / screen): top bar `[ DimonOS-64 ] 1 Calc ...`,
  blue `░` desktop with 7 icons, bottom taskbar with green
  `[ START ]`, `[ Desktop ]`, hints and live `T+<ticks>` clock.
  `--inject-keys "1"` opens Calculator; `F1` opens the Start menu.

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
