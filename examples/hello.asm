; hello.asm - first program on DimonVirtualCPU-64
; Prints a greeting using 64-bit syscalls.
.org 0x0
    LA a0, msg
    LI a7, 2
    ECALL
    EBREAK
msg:
    .string "Hello, DimonVirtualCPU-64!\n"
