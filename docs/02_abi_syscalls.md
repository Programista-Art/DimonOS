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
| 10 | GUI_INIT | — | a0=800,a1=600,a2=VRAM |
| 11 | GUI_POLL_EVENT | — | a0=type,a1=code/X,a2=data/Y,a3=button,a4=modifiers |
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
| 25 | GUI_DRAW_PIXEL | a0=x,a1=y,a2=color | — |
| 26 | GUI_DRAW_LINE | a0=x0,a1=y0,a2=x1,a3=y1,a4=color | — |
| 27 | GUI_BLIT | a0=x,a1=y,a2=w,a3=h,a4=source | — |
| 28 | MEMSET | a0=dst,a1=value32,a2=word count | — |
| 29 | RTC_GET | — | Unix UTC seconds; C if unavailable |
| 30 | PROC_INFO | a0=slot,a1=Dimon64ProcInfo* | status |
| 31 | PROC_KILL | a0=pid | status; PID 0 is protected |
| 32 | FS_STAT | a0=path,a1=Dimon64DirEnt* | status |
| 33 | FS_READ | a0=path,a1=offset,a2=buffer,a3=capacity | a0=bytes, a1=full size |
| 34 | FS_WRITE | a0=path,a1=buffer,a2=len,a3=flags | status |
| 35 | FS_LIST | a0=directory,a1=index,a2=Dimon64DirEnt* | status |
| 36 | FS_MKDIR | a0=path | status |
| 37 | FS_REMOVE | a0=path | status |
| 38 | FS_RENAME | a0=old,a1=new | status |
| 39 | FS_COPY | a0=source,a1=destination | status |
| 40 | APP_EXEC | a0=DEXE path,a1=argument or 0 | PID/status |
| 41 | GUI_SET_CONTEXT | a0=dx,a1=dy,a2=clip x,a3=clip y,a4=w,a5=h | status; zero w/h resets |
| 42 | GUI_TEXT_MEASURE | a0=UTF-8 string | rendered width in pixels |
| 43 | GUI_TEXT_FIT | a0=x,a1=y,a2=UTF-8 string,a3=fg,a4=bg,a5=max width | clipped text with glyph-safe ellipsis |

GUI event types: 0 NONE, 1 KEY, 2 CLICK, 3 MOVE, 4 TIMER, 5 MOUSE_RELEASE.
Keycodes: ASCII/UTF-8 bytes, 256-269 for arrows/F1-F10, Home=270,
End=271, Delete=272, PageUp=273, PageDown=274. Modifier bits are Shift=1,
Ctrl=2, Alt=4, Meta=8.

The drawing context translates guest coordinates and intersects all rectangle,
pixel, line, text and blit output with its screen clip. Context state is reset
with width/height zero. Text measurement counts decoded UTF-8 glyph advances
(currently 8 pixels each), not bytes; fitting truncates only at complete glyph
boundaries and adds `...` when space permits.

Filesystem/process calls return a negative error and set Carry on failure.
`FS_WRITE` flags are create=1 and truncate=2. FAT names are validated 8.3;
paths accept `/` or `\\`, `.`, and `..`. Directories must be empty before
removal. BPB geometry and cluster chains are bounds-checked, both FAT copies
are updated, and read-only/full/I/O failures propagate to the caller.

`Dimon64DirEnt` is 28 bytes: name `[13]`, attributes at 13, three reserved
bytes, size at 20 and first cluster at 24. `Dimon64ProcInfo` is 72 bytes: six
64-bit values (PID, state, memory base/size, executed instruction count and
context switches), name `[16]`, essential/isolated flags and reserved bytes.
