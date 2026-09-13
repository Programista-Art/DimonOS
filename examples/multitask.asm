; multitask.asm - two tasks alternating via timer preemption
; Demonstrates preemptive and cooperative multitasking on DimonVirtualCPU-64.
; Task 1 prints to the upper screen area (VRAM row 10) and console with
; prefix [Task1], Task 2 prints to the lower area (VRAM row 15) with
; prefix [Task2]. Execution interleaves via timer ticks and SYSSLEEP.
.org 0x0

main:
    ; Use a fast timer period so preemption is visible
    LI a0, 300
    LI a7, 22
    ECALL

    ; Install timer ISR (preserves context, counts ticks)
    LA a0, timer_isr
    LI a7, 21
    ECALL
    LI a0, 1
    LI a7, 23
    ECALL

    ; Spawn Task 1
    LA a0, task1
    LI a1, 0
    LA a2, name1
    LI a7, 17
    ECALL

    ; Spawn Task 2
    LA a0, task2
    LI a1, 0
    LA a2, name2
    LI a7, 17
    ECALL

    ; Main task prints 4 iterations then exits
    LI s0, 0
main_loop:
    LI t0, 4
    BGE s0, t0, main_done
    LA a0, msg_main
    LI a7, 2
    ECALL
    MV a0, s0
    LI a7, 3
    ECALL
    LI a7, 5
    ECALL
    LI a0, 35
    LI a7, 19
    ECALL
    ADDI s0, s0, 1
    J main_loop
main_done:
    LA a0, msg_main_done
    LI a7, 2
    ECALL
    LI a0, 0
    LI a7, 18
    ECALL
    EBREAK

; Timer ISR: save t0/t1 on stack, bump counter, restore, IRET.
timer_isr:
    ADDI sp, sp, -16
    SD t0, 0(sp)
    SD t1, 8(sp)
    LA t0, isr_ticks
    LD t1, 0(t0)
    ADDI t1, t1, 1
    SD t1, 0(t0)
    LD t1, 8(sp)
    LD t0, 0(sp)
    ADDI sp, sp, 16
    IRET

; Task 1: upper screen area (row 10)
task1:
    LI s0, 0
t1_loop:
    LI t0, 6
    BGE s0, t0, t1_done
    LA a0, msg_t1
    LI a7, 2
    ECALL
    MV a0, s0
    LI a7, 3
    ECALL
    LI a7, 5
    ECALL
    ; Distinct screen area: VRAM row 10, green text
    LI a0, 2560
    LA a1, vram_t1
    LI a2, 0x0A
    LI a7, 15
    ECALL
    LI a0, 25
    LI a7, 19
    ECALL
    ADDI s0, s0, 1
    J t1_loop
t1_done:
    LA a0, msg_t1_done
    LI a7, 2
    ECALL
    LI a0, 0
    LI a7, 18
    ECALL

; Task 2: lower screen area (row 15)
task2:
    LI s0, 0
t2_loop:
    LI t0, 6
    BGE s0, t0, t2_done
    LA a0, msg_t2
    LI a7, 2
    ECALL
    MV a0, s0
    LI a7, 3
    ECALL
    LI a7, 5
    ECALL
    ; Distinct screen area: VRAM row 15, yellow text
    LI a0, 3840
    LA a1, vram_t2
    LI a2, 0x0E
    LI a7, 15
    ECALL
    LI a0, 30
    LI a7, 19
    ECALL
    ADDI s0, s0, 1
    J t2_loop
t2_done:
    LA a0, msg_t2_done
    LI a7, 2
    ECALL
    LI a0, 0
    LI a7, 18
    ECALL

msg_main:
    .string "[Main] iter "
msg_main_done:
    .string "[Main] done, workers continue.\n"
msg_t1:
    .string "[Task1 upper] iter "
msg_t1_done:
    .string "[Task1] done.\n"
msg_t2:
    .string "[Task2 lower] iter "
msg_t2_done:
    .string "[Task2] done.\n"
vram_t1:
    .string "Task1 running (upper area row 10)"
vram_t2:
    .string "Task2 running (lower area row 15)"
name1:
    .string "task1"
name2:
    .string "task2"
.align 8
isr_ticks:
    .quad 0
