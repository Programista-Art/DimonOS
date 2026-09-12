; fib.asm — 10 terms of Fibonacci sequence
    MOV R3, 0       ; a
    MOV R4, 1       ; b
    MOV R5, 0       ; i
    MOV R6, 10      ; n
loop:
    CMP R5, R6
    JNC done
    MOV R0, R3
    INT 3
    MOV R0, 32      ; space
    INT 0
    MOV R2, R3
    ADD R2, R4
    MOV R3, R4
    MOV R4, R2
    INC R5
    JMP loop
done:
    INT 5
    HLT
