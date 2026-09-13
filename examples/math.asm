; math.asm - 64-bit arithmetic and logic on DimonVirtualCPU-64
; Expected output: 35 42 42 42 48 255 85 32 8 (each on its own line)
.org 0x0
    LI t0, 10
    LI t1, 25
    ADD t2, t0, t1       ; 35
    MV a0, t2
    LI a7, 3
    ECALL
    LI a7, 5
    ECALL

    LI t0, 100
    LI t1, 58
    SUB t2, t0, t1       ; 42
    MV a0, t2
    LI a7, 3
    ECALL
    LI a7, 5
    ECALL

    LI t0, 6
    LI t1, 7
    MUL t2, t0, t1       ; 42
    MV a0, t2
    LI a7, 3
    ECALL
    LI a7, 5
    ECALL

    LI t0, 84
    LI t1, 2
    DIV t2, t0, t1       ; 42
    MV a0, t2
    LI a7, 3
    ECALL
    LI a7, 5
    ECALL

    LI t0, 0xF0
    LI t1, 0x3C
    AND t2, t0, t1       ; 0x30 = 48
    MV a0, t2
    LI a7, 3
    ECALL
    LI a7, 5
    ECALL

    LI t0, 0xF0
    LI t1, 0x0F
    OR t2, t0, t1        ; 0xFF = 255
    MV a0, t2
    LI a7, 3
    ECALL
    LI a7, 5
    ECALL

    LI t0, 0xFF
    LI t1, 0xAA
    XOR t2, t0, t1       ; 0x55 = 85
    MV a0, t2
    LI a7, 3
    ECALL
    LI a7, 5
    ECALL

    LI t0, 1
    LI t1, 5
    SLL t2, t0, t1       ; 32
    MV a0, t2
    LI a7, 3
    ECALL
    LI a7, 5
    ECALL

    LI t0, 64
    LI t1, 3
    SRL t2, t0, t1       ; 8
    MV a0, t2
    LI a7, 3
    ECALL
    LI a7, 5
    ECALL

    EBREAK
