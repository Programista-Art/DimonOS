# DimonVirtualCPU-64 & DimonOS-64 — 64-bit RISC System & Multitasking OS

<!-- ![Dimon](img/2.png) -->
<p align="center">
  <img src="img/1.png" width="48%" />
  <img src="img/2.png" width="48%" />
</p>

An educational 64-bit RISC computer system and preemptive multitasking OS:

- **DimonVirtualCPU-64**: 64-bit RISC-V style CPU, fixed 32-bit instructions,
  32x64-bit registers, 64 MB RAM, hardware timer interrupts.
- **Assembler (`dimon-as`)**: Two-pass assembler for R/I/S/B/U/J formats,
  64-bit immediates, ABI register names, rich directives.
- **Emulator (`dimon-emu`)**: Headless, ANSI TUI and X11 backends with
  64-bit debugger and disassembler.
- **DimonOS-64 (`os.bin`)**: Multitasking kernel with timer ISR, round-robin
  scheduler, and a retained seven-app desktop with overlapping movable windows,
  focus/Z-order, minimize/restore, taskbar switching and clipped composition.
- **Examples**: `hello`, `math`, `fib`, `multitask` (timer-preempted tasks
  printing to distinct screen areas).

---

## Architecture Overview

| Component | Specification |
|---|---|
| Word Size | 64-bit registers, 32-bit fixed-length instructions (4-byte aligned) |
| RAM | 64 MB (`0x00000000`–`0x03FFFFFF`), dynamically allocated via `malloc` in `vm.c` |
| VRAM | 1,920,000 bytes at `0x02000000` (800x600 TrueColor @ 32bpp ARGB8888) |
| Stacks | 8 x 64 KB at `0x03E80000`–`0x03EFFFFF` (one per process) |
| MMIO | Timer ticks `0x03FFF000`, period `0x03FFF008`, vector `0x03FFF010`, serial `0x03FFFF00` |
| Registers | `R0`–`R31` / `x0`–`x31` with ABI names (`zero, ra, sp, gp, tp, t0-t2, s0-s1, a0-a7, s2-s11, t3-t6`) |
| Special | `R0/zero` hardwired to 0, `R1/ra` return address, `R2/sp` stack pointer, `R4/tp` current PID |
| Control | `PC` (64-bit, 4-byte aligned), `FLAGS` (Z/N/C/O/IE), `TIMERTICKS` monotonic, `EPC/EFLAGS` trap save |

### Register ABI Convention

| Register | ABI | Role |
|---|---|---|
| R0 | zero | Constant 0 (writes discarded) |
| R1 | ra | Return address |
| R2 | sp | Stack pointer (16-byte aligned) |
| R3 | gp | Global pointer |
| R4 | tp | Thread pointer / current PID |
| R5-R7 | t0-t2 | Temporaries |
| R8-R9 | s0-s1/fp | Saved / frame pointer |
| R10-R17 | a0-a7 | Args, syscall params (a7 = syscall ID, return in a0) |
| R18-R27 | s2-s11 | Saved registers |
| R28-R31 | t3-t6 | Temporaries |

---

## ISA Manual (RISC-V Style)

### R-Type (register-register): `opcode(7) rd(5) funct3(3) rs1(5) rs2(5) funct7(7)`

`ADD, SUB, AND, OR, XOR, SLL, SRL, SRA, SLT, SLTU, MUL, DIV, DIVU, REM`

Example: `ADD a0, a1, a2`, `SUB t0, t1, t2`, `MUL a0, a0, a1`

### I-Type (immediate/loads/syscall)

`ADDI, ANDI, ORI, XORI, SLTI, SLTIU, SLLI, SRLI, SRAI`
`LB, LH, LW, LD, LBU, LHU, LWU` with `LD rd, offset(rs1)`
`JALR rd, offset(rs1)`, `ECALL`, `EBREAK`, `IRET`, `INT n`

Syscall convention: `a7` = ID, `a0-a6` = args, return in `a0`.

### S-Type (stores): `SD rs2, offset(rs1)` etc.

`SB (8-bit), SH (16-bit), SW (32-bit), SD (64-bit)`

### B-Type (branches): `BEQ rs1, rs2, label` etc.

`BEQ, BNE, BLT, BGE, BLTU, BGEU` (PC-relative +/-4KB)

### U-Type: `LUI rd, imm20`, `AUIPC rd, imm20`

### J-Type: `JAL rd, label` (PC-relative +/-1MB)

### Pseudo-instructions

`LI rd, imm64`, `LA rd, label`, `MV rd, rs`, `NOP`, `NOT rd, rs`,
`NEG rd, rs`, `J label`, `JR rs`, `RET`, `CALL label`, `HLT/HALT`

### Directives

`.org`, `.align`, `.quad/.dword` (64-bit), `.word` (32-bit),
`.half` (16-bit), `.byte` (8-bit), `.string/.asciz`, `.space`,
legacy `DB/DW/DS` aliases, `.include`, `.equ`

---

## Syscall Table (`a7` / `INT n`)

| ID | Name | Args / Return |
|---|---|---|
| 0 | PUTCHAR | `a0` = char |
| 1 | GETCHAR | returns `a0` = char (0 on EOF) |
| 2 | PRINTSTR | `a0` = address of NUL-terminated string |
| 3 | PRINTNUM | `a0` = signed 64-bit value |
| 4 | READNUM | returns `a0` = value, FLAGS C on error |
| 5 | NEWLINE | prints `\n` |
| 6 | DISK_READ | `a0`=LBA, `a1`=RAM, `a2`=count |
| 7 | DISK_INFO | returns disk geometry |
| 8 | DISK_WRITE | same as READ |
| 10 | GUI_INIT | returns `a0`=800, `a1`=600, `a2`=0x02000000 (LFB base) |
| 11 | GUI_POLL_EVENT | returns `a0`=type, `a1`=X/key, `a2`=Y/data, `a3`=button (1=L, 2=R) |
| 12 | GUI_FLUSH | flush VRAM buffer to display |
| 13 | GET_TICKS | returns wall-clock ms in `a0` |
| 14 | GUI_DRAW_RECT | TrueColor `(x, y, w, h, col32)` |
| 15 | GUI_DRAW_TEXT | TrueColor `(x, y, str, fg_col32, bg_col32)` with alpha |
| 16 | YIELD | voluntary task switch (cooperative) |
| 17 | SPAWN | `a0`=entry, `a1`=arg, `a2`=name -> `a0`=pid |
| 18 | EXIT | `a0`=code, terminate task |
| 19 | SLEEP | `a0`=ticks to sleep |
| 20 | GETPID | returns active PID |
| 21 | SET_TIMER_HANDLER | `a0`=ISR address (0 disables) |
| 22 | SET_TIMER_PERIOD | `a0`=cycles per tick |
| 23 | ENABLE_INTERRUPTS | `a0`=0/1 |
| 24 | GET_TIMER_TICKS | returns `TIMERTICKS` |
| 25 | GUI_DRAW_PIXEL | `(x, y, col32)` fast clipped pixel write |
| 26 | GUI_DRAW_LINE | `(x0, y0, x1, y1, col32)` Bresenham line rasterizer |
| 41 | GUI_SET_CONTEXT | translated/clipped drawing context; zero size resets |
| 42 | GUI_TEXT_MEASURE | decoded UTF-8 glyph width in framebuffer pixels |
| 43 | GUI_TEXT_FIT | padded-region text clipping and glyph-safe ellipsis |

---

## Multitasking Architecture

- **Hardware timer** fires every `timer_period` cycles (default 500).
  `TIMERTICKS` increments monotonically; sleeping tasks with
  `sleep_until <= ticks` become READY.
- **Interrupt delivery**: when IE is set and a vector is installed,
  the VM first performs native round-robin preemption (if >1 task),
  then saves `PC/FLAGS` to `EPC/EFLAGS`, clears IE and jumps to the ISR.
  The ISR must preserve registers (stack save) and return with `IRET`.
- **Native scheduler**: 8 PCBs with 64 KB stacks, states
  FREE/READY/RUNNING/BLOCKED/TERMINATED, round-robin selection,
  `R4/tp` points to current PID. `YIELD` cooperatively switches,
  `SLEEP` blocks until a future tick, `SPAWN` creates READY tasks,
  `EXIT` frees the slot (idles to next wakeup if only BLOCKED remain).
- **No deadlocks**: `SLEEP`/`EXIT` fast-forward ticks when all tasks
  are BLOCKED; the last `EXIT` halts the VM.

---

## Build & Run

```bash
make all os.bin apps            # compile/assemble; does not run the VM
make disk-new                   # explicit new FAT16 image (refuses overwrite)
make regression                 # hosted/headless, X11, isolated QEMU checks
./dimon-emu --disk dimon.iso --disk-writable os.bin
                                # X11 desktop (or TUI without DISPLAY)
./dimon-emu os.bin tui          # ANSI terminal desktop
./dimon-emu examples/multitask.bin
./dimon-emu -d os.bin           # interactive 64-bit debugger
./dimon-emu -t examples/fib.bin # trace mode
```

Build flags: `-Wall -Wextra -O2 -std=c11`, warning-free.

`make disk-new` packages `apps/snake.app` as `SNAKE.APP`. Existing disk images
are never replaced implicitly. To intentionally recreate one, remove/move it
yourself or call `./dimon-mkiso --force -o dimon.iso ...`. In the desktop
Terminal, `run SNAKE.APP` loads it through the DEXE64 process loader.
Install into an existing image without reformatting it with
`make disk-install-apps`; an existing same-name app is preserved. To replace
that one entry explicitly, use
`./dimon-mkiso --add dimon.iso apps/snake.app:SNAKE.APP --force`.

The historical `make test` target performs broad headless checks. Its FAT16
fixture now lives at `build/regression/legacy-test-disk.img`; it never formats
or writes `dimon.iso`. Focused multi-window results, diagnostics and QEMU PPM
screenshots are written below `build/regression/`.

---

## Documentation (`docs/`)

- `docs/01_isa_manual.md` — full ISA encoding, formats, pseudos.
- `docs/02_abi_syscalls.md` — register ABI and syscall reference.
- `docs/03_multitasking.md` — timer, IVT, PCB, scheduler guide.
- `docs/04_user_manual.md` — build, emulator flags, debugging.
- `docs/05_executable_format.md` — DEXE64 loader, relocation and isolation ABI.
- `docs/06_roadmap_status.md` — exact integrated/remaining roadmap scope.

---

## Verification

`make test` builds without warnings and runs `hello`, `math`, `fib`,
`multitask` plus headless GUI checks: desktop composition, Start menu,
all 7 apps, calculator math, notepad/terminal input and multitasking
(`ticks`/`switches` counters, clean halts where applicable).

`make regression-hosted`, `make regression-x11`, and `make regression-qemu`
exercise ordered press/move/release input, retained multi-window state,
minimize/restore, app Close and system shutdown. The QEMU target launches the
fresh `build/regression/dimon-regression.iso`, not an older top-level image.
