# 02. Register ABI and Syscall Reference

## Register ABI

| Reg | ABI | Preserved | Role |
|---|---|---|---|
| R0/x0 | zero | — | Constant 0 |
| R1 | ra | caller | Return address (`JAL ra`, `RET`) |
| R2 | sp | callee | Stack pointer, 16-byte aligned, grows down |
| R3 | gp | — | Global pointer (unused by default) |
| R4 | tp | — | Current PID (set by scheduler) |
| R5-R7 | t0-t2 | caller | Temporaries |
| R8 | s0/fp | callee | Saved / frame pointer |
| R9 | s1 | callee | Saved |
| R10-R17 | a0-a7 | caller | Function args; syscalls: `a7`=ID, `a0-a6`=args, `a0`=return |
| R18-R27 | s2-s11 | callee | Saved |
| R28-R31 | t3-t6 | caller | Temporaries |

Caller-saved registers must be preserved by the ISR if clobbered
(stack save/restore pattern used in `os.asm` timer handler).

## Calling Convention

- Arguments in `a0-a5`, extra on stack at `0(sp)` etc.
- Return value in `a0` (and `a1` for 128-bit, unused here).
- `sp` 16-byte aligned at calls; `ra` holds return PC+4.
- Leaf functions may use `t*` without saving; non-leaf save `ra/s*`.

## Syscalls

All syscalls use `ECALL` with `a7` = ID (or `INT n` shorthand).
Return value in `a0`. FLAGS C indicates disk/read errors.

| ID | Name | In | Out |
|---|---|---|---|
| 0 | PUTCHAR | a0=char | — |
| 1 | GETCHAR | — | a0=char |
| 2 | PRINTSTR | a0=str addr | — |
| 3 | PRINTNUM | a0=signed 64 | — |
| 4 | READNUM | — | a0=value, C on error |
| 5 | NEWLINE | — | — |
| 6 | DISK_READ | a0=LBA,a1=RAM,a2=count | a0=code |
| 7 | DISK_INFO | — | geometry |
| 8 | DISK_WRITE | same as 6 | a0=code |
| 10 | GUI_INIT | — | a0=80,a1=25,a2=VRAM |
| 11 | GUI_POLL_EVENT | — | a0=type,a1=code,a2=data |
| 12 | GUI_FLUSH | — | — |
| 13 | GET_TICKS | — | a0=wall ms |
| 14 | GUI_DRAW_RECT | packed | — |
| 15 | GUI_DRAW_TEXT | a0=X\|Y<<8,a1=str,a2=attr | — |
| 16 | YIELD | — | switch |
| 17 | SPAWN | a0=entry,a1=arg,a2=name | a0=pid |
| 18 | EXIT | a0=code | — |
| 19 | SLEEP | a0=ticks | block |
| 20 | GETPID | — | a0=pid |
| 21 | SET_TIMER_HANDLER | a0=vector | a0=status |
| 22 | SET_TIMER_PERIOD | a0=cycles | — |
| 23 | ENABLE_INTERRUPTS | a0=0/1 | — |
| 24 | GET_TIMER_TICKS | — | a0=ticks |

GUI event types: 0 NONE, 1 KEY, 2 CLICK, 3 MOVE, 4 TIMER.
Keycodes: ASCII plus 256-269 for arrows and F1-F10.
