; ========================================================
; gui/utils.asm — Common Helper Functions (text, numbers, strings)
; ========================================================

; Convert 16-bit number (R0) to decimal string at [R1]
num_to_str:
    PUSH R0
    PUSH R1
    PUSH R2
    PUSH R3
    PUSH R4
    PUSH R5

    MOV R2, R1          ; R2 = start of buffer
    MOV R3, 0           ; digit count on stack

    ; Special case: 0
    CMP R0, 0
    JNZ nts_loop
    MOV R4, 48          ; '0'
    STB [R2], R4
    INC R2
    MOV R4, 0
    STB [R2], R4
    JMP nts_done

nts_loop:
    CMP R0, 0
    JZ nts_pop_loop
    MOV R4, R0
    DIV R4, 10          ; R4 = R0 / 10
    MOV R5, R4
    MUL R5, 10          ; R5 = (R0 / 10) * 10
    SUB R0, R5          ; R0 = remainder (R0 % 10)
    ADD R0, 48          ; ASCII conversion '0'..'9'
    PUSH R0
    INC R3
    MOV R0, R4
    JMP nts_loop

nts_pop_loop:
    CMP R3, 0
    JZ nts_end_str
    POP R0
    STB [R2], R0
    INC R2
    DEC R3
    JMP nts_pop_loop

nts_end_str:
    MOV R0, 0
    STB [R2], R0

nts_done:
    POP R5
    POP R4
    POP R3
    POP R2
    POP R1
    POP R0
    RET

; Calculate string length (R0 -> R1)
str_len:
    PUSH R0
    PUSH R2
    MOV R1, 0
sl_loop:
    LDB R2, [R0]
    CMP R2, 0
    JZ sl_done
    INC R0
    INC R1
    JMP sl_loop
sl_done:
    POP R2
    POP R0
    RET

; String copy: R0 = dest, R1 = src
str_copy:
    PUSH R0
    PUSH R1
    PUSH R2
sc_loop:
    LDB R2, [R1]
    STB [R0], R2
    CMP R2, 0
    JZ sc_done
    INC R0
    INC R1
    JMP sc_loop
sc_done:
    POP R2
    POP R1
    POP R0
    RET

; String compare: R0 = str1, R1 = str2 -> R2 = 0 if equal
str_cmp:
    PUSH R0
    PUSH R1
    PUSH R3
    PUSH R4
scmp_loop:
    LDB R3, [R0]
    LDB R4, [R1]
    CMP R3, R4
    JNZ scmp_neq
    CMP R3, 0
    JZ scmp_eq
    INC R0
    INC R1
    JMP scmp_loop
scmp_eq:
    MOV R2, 0
    JMP scmp_end
scmp_neq:
    MOV R2, 1
scmp_end:
    POP R4
    POP R3
    POP R1
    POP R0
    RET
