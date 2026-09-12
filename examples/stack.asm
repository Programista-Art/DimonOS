; stack.asm — stack, CALL/RET, PUSH/POP
    PUSH 10
    PUSH 20
    PUSH 30
    POP R0          ; 30
    INT 3
    INT 5
    POP R0          ; 20
    INT 3
    INT 5
    POP R0          ; 10
    INT 3
    INT 5

    CALL square     ; R0=7 -> 49
    INT 3
    INT 5
    HLT

square:
    MOV R0, 7
    MUL R0, R0
    RET
