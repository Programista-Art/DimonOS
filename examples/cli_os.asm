; =====================================================
; DimonOS v1.0 — simple 16-bit text OS for Dimon-16
; Commands: HELP, INFO, ADD, FIB, ECHO <text>, CLEAR, HALT
; =====================================================

    MOV R0, banner
    INT 2

main_loop:
    MOV R0, prompt
    INT 2

    ; --- read line into buffer (max 63 chars) ---
    MOV R1, buffer
    MOV R2, 0
read_loop:
    INT 1
    CMP R0, 10
    JZ eol
    CMP R0, 13
    JZ eol
    CMP R2, 63
    JZ read_loop
    STB [R1], R0
    INC R1
    INC R2
    JMP read_loop
eol:
    MOV R0, 0
    STB [R1], R0

    ; --- empty line? ---
    LDB R0, [buffer]
    CMP R0, 0
    JZ main_loop

    ; --- ECHO <text>? (check first, preserve casing) ---
    MOV R1, buffer
    LDB R0, [R1]
    CMP R0, 97
    JC echo_c0
    CMP R0, 123
    JNC echo_c0
    SUB R0, 32
echo_c0:
    CMP R0, 69        ; 'E'
    JNZ not_echo
    INC R1
    LDB R0, [R1]
    CMP R0, 97
    JC echo_c1
    CMP R0, 123
    JNC echo_c1
    SUB R0, 32
echo_c1:
    CMP R0, 67        ; 'C'
    JNZ not_echo
    INC R1
    LDB R0, [R1]
    CMP R0, 97
    JC echo_c2
    CMP R0, 123
    JNC echo_c2
    SUB R0, 32
echo_c2:
    CMP R0, 72        ; 'H'
    JNZ not_echo
    INC R1
    LDB R0, [R1]
    CMP R0, 97
    JC echo_c3
    CMP R0, 123
    JNC echo_c3
    SUB R0, 32
echo_c3:
    CMP R0, 79        ; 'O'
    JNZ not_echo
    INC R1
    LDB R0, [R1]
    CMP R0, 0
    JZ echo_nl
    CMP R0, 32
    JNZ not_echo
    INC R1
    MOV R0, R1
    INT 2
    INT 5
    JMP main_loop
echo_nl:
    INT 5
    JMP main_loop
not_echo:

    ; --- convert to UPPERCASE (case-insensitive) ---
    MOV R1, buffer
upper_loop:
    LDB R0, [R1]
    CMP R0, 0
    JZ upper_done
    CMP R0, 97        ; 'a'
    JC upper_next
    CMP R0, 123       ; 'z'+1
    JNC upper_next
    SUB R0, 32
    STB [R1], R0
upper_next:
    INC R1
    JMP upper_loop
upper_done:

    ; --- compare commands (CALL strcmp) ---
    MOV R0, buffer
    MOV R1, cmd_help
    CALL strcmp
    CMP R2, 0
    JZ do_help

    MOV R0, buffer
    MOV R1, cmd_info
    CALL strcmp
    CMP R2, 0
    JZ do_info

    MOV R0, buffer
    MOV R1, cmd_add
    CALL strcmp
    CMP R2, 0
    JZ do_add

    MOV R0, buffer
    MOV R1, cmd_fib
    CALL strcmp
    CMP R2, 0
    JZ do_fib

    MOV R0, buffer
    MOV R1, cmd_clear
    CALL strcmp
    CMP R2, 0
    JZ do_clear

    MOV R0, buffer
    MOV R1, cmd_halt
    CALL strcmp
    CMP R2, 0
    JZ do_halt

    MOV R0, msg_unknown
    INT 2
    JMP main_loop

; ---------------- command handlers ----------------
do_help:
    MOV R0, msg_help
    INT 2
    JMP main_loop

do_info:
    MOV R0, msg_info
    INT 2
    JMP main_loop

do_add:
    MOV R0, msg_a
    INT 2
    INT 4
    JC add_err
    MOV [var_a], R0
    MOV R0, msg_b
    INT 2
    INT 4
    JC add_err
    MOV [var_b], R0
    MOV R0, [var_a]
    ADD R0, [var_b]
    MOV [var_c], R0
    MOV R0, msg_sum
    INT 2
    MOV R0, [var_c]
    INT 3
    INT 5
    JMP main_loop
add_err:
    MOV R0, msg_numerr
    INT 2
    JMP main_loop

do_fib:
    MOV R0, msg_n
    INT 2
    INT 4
    JC fib_err
    CMP R0, 24
    JNC fib_err
    MOV R3, R0
    MOV R1, 0
    MOV R2, 1
fib_check:
    CMP R3, 0
    JZ fib_done
fib_loop:
    MOV R0, R1
    INT 3
    MOV R0, 32
    INT 0
    MOV R0, R1
    ADD R0, R2
    MOV R1, R2
    MOV R2, R0
    DEC R3
    JNZ fib_loop
fib_done:
    INT 5
    JMP main_loop
fib_err:
    MOV R0, msg_fiberr
    INT 2
    JMP main_loop

do_clear:
    MOV R1, 0
clear_loop:
    INT 5
    INC R1
    CMP R1, 40
    JNZ clear_loop
    JMP main_loop

do_halt:
    MOV R0, msg_bye
    INT 2
    HLT

; --- strcmp: R0 = s1, R1 = s2, returns R2 = 0 if equal ---
strcmp:
    PUSH R0
    PUSH R1
    PUSH R3
    PUSH R4
sc_loop:
    LDB R3, [R0]
    LDB R4, [R1]
    CMP R3, R4
    JNZ sc_neq
    CMP R3, 0
    JZ sc_eq
    INC R0
    INC R1
    JMP sc_loop
sc_eq:
    MOV R2, 0
    POP R4
    POP R3
    POP R1
    POP R0
    RET
sc_neq:
    MOV R2, 1
    POP R4
    POP R3
    POP R1
    POP R0
    RET

; ---------------- data ----------------
banner:
    DB "DimonOS v1.0 (16-bit)", 10
    DB "Type HELP for list of commands.", 10, 0
prompt:
    DB "DimonOS> ", 0
msg_help:
    DB "Commands:", 10
    DB "  HELP        - this help", 10
    DB "  INFO        - system info", 10
    DB "  ADD         - add two numbers", 10
    DB "  FIB         - Fibonacci sequence", 10
    DB "  ECHO <txt>  - print text", 10
    DB "  CLEAR       - clear screen", 10
    DB "  HALT        - stop system", 10, 0
msg_info:
    DB "DimonOS v1.0 on Dimon-16 VM", 10
    DB "Architecture: 16-bit, 64KB RAM, 8 registers R0-R7", 10
    DB "Author: Dimon", 10, 0
msg_unknown:
    DB "Unknown command. Type HELP.", 10, 0
msg_a:
    DB "Number A: ", 0
msg_b:
    DB "Number B: ", 0
msg_sum:
    DB "Sum = ", 0
msg_numerr:
    DB "Error: invalid number!", 10, 0
msg_n:
    DB "Count of terms (1..24): ", 0
msg_fiberr:
    DB "Error: enter a number between 1 and 23!", 10, 0
msg_bye:
    DB "Shutting down DimonOS. Goodbye!", 10, 0

cmd_help:  DB "HELP", 0
cmd_info:  DB "INFO", 0
cmd_add:   DB "ADD", 0
cmd_fib:   DB "FIB", 0
cmd_clear: DB "CLEAR", 0
cmd_halt:  DB "HALT", 0

var_a:  DW 0
var_b:  DW 0
var_c:  DW 0

buffer: DS 64
