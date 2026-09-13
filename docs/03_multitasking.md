# 03. Multitasking Architecture Guide

## Goals

Native hardware timer-driven multitasking with both preemptive
(timer interrupt) and cooperative (`YIELD`/`SLEEP`) switching,
8 concurrent tasks, no deadlocks or memory corruption.

## Hardware Timer

- `timer_period` cycles per tick (default 500, programmable via
  syscall 22 or MMIO `0x03FFF008`).
- Every tick: `TIMERTICKS++`, mirror to MMIO, wake `BLOCKED` tasks
  whose `sleep_until <= ticks`.
- If IE set and not already in ISR: round-robin preemption when
  more than one task is READY/RUNNING, then (if a vector is installed)
  trap to the ISR.

## Interrupt Vector Table (Virtual)

A single programmable vector (`timer_vector`, MMIO `0x03FFF010`,
syscall 21). `0` means no handler (pure native preemption).
On delivery:

```
EPC <- PC; EFLAGS <- FLAGS; IE <- 0; in_isr <- 1; PC <- vector
```

The current native PCB resume point is kept at `EPC` so later
switches preserve task entry points. `IRET` (`SYSTEM 0x102`)
restores `PC <- EPC`, `FLAGS <- EFLAGS`, clears `in_isr` and
syncs the current PCB.

ISR requirements (see `os.asm`):

```
timer_isr:
    ADDI sp, sp, -16
    SD t0, 0(sp)
    SD t1, 8(sp)
    ... minimal work, no YIELD/SLEEP inside ISR ...
    LD t1, 8(sp)
    LD t0, 0(sp)
    ADDI sp, sp, 16
    IRET
```

Do not call switching syscalls inside the ISR; preemption already
happened before entry. Preserve every register you clobber.
FLAGS clobber is safe (restored by IRET).

## Process Control Block

Host scheduler PCB (mirrored in OS comments as guest layout):

```
PID, STATE (FREE/READY/RUNNING/BLOCKED/TERMINATED),
SAVEDPC, SAVEDSP (= regs[sp]), SAVEDREGS[32] (256 bytes),
STACKBASE, STACKSIZE (64KB), NAME[16], sleep_until
```

Stacks live at `0x03E80000 + slot * 0x10000`, tops 16-byte aligned.
`R4/tp` holds the current PID after each switch.

## Scheduler

- `SPAWN`: find FREE slot, validate 4-aligned entry, set
  `pc=entry`, `sp=top`, `a0=arg`, `FLAGS=IE`, copy 16-byte name.
- `YIELD`: save current to PCB, pick next READY round-robin.
- `SLEEP n`: block until `ticks + n`; if no READY remains,
  fast-forward `ticks` to the next wakeup (idle, no busy wait).
- `EXIT`: free slot; if no READY but BLOCKED remain, fast-forward
  like SLEEP; halt only when no USED slots remain.
- Timer tick: wake sleepers, preempt round-robin, count `switches`.

This guarantees progress: overlapping sleeps never deadlock,
and the final EXIT halts cleanly.
