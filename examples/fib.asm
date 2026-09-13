; fib.asm - first 10 Fibonacci numbers on DimonVirtualCPU-64
.org 0x0
    LI s0, 0             ; a = 0
    LI s1, 1             ; b = 1
    LI s2, 0             ; i = 0
    LI s3, 10            ; n = 10
loop:
    BGE s2, s3, done
    MV a0, s0
    LI a7, 3
    ECALL
    LI a0, 32            ; space
    LI a7, 0
    ECALL
    ADD t0, s0, s1
    MV s0, s1
    MV s1, t0
    ADDI s2, s2, 1
    J loop
done:
    LI a7, 5
    ECALL
    EBREAK
