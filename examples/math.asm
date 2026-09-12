; math.asm — 16-bit arithmetic and logic
    MOV R0, 10
    MOV R1, 25
    ADD R0, R1      ; 35
    INT 3
    INT 5

    MOV R0, 100
    SUB R0, 58      ; 42
    INT 3
    INT 5

    MOV R0, 6
    MUL R0, 7       ; 42
    INT 3
    INT 5

    MOV R0, 84
    MOV R1, 2
    DIV R0, R1      ; 42
    INT 3
    INT 5

    MOV R0, 0xF0
    AND R0, 0x3C    ; 0x30 = 48
    INT 3
    INT 5

    MOV R0, 0xF0
    OR R0, 0x0F     ; 0xFF = 255
    INT 3
    INT 5

    MOV R0, 0xFF
    XOR R0, 0xAA    ; 0x55 = 85
    INT 3
    INT 5

    MOV R0, 1
    SHL R0, 5       ; 32
    INT 3
    INT 5

    MOV R0, 64
    SHR R0, 3       ; 8
    INT 3
    INT 5

    HLT
