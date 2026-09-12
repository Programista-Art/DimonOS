; ========================================================
; apps/calc.asm — Application 1: Calculator
; ========================================================

calc_val1:
    DW 0
calc_val2:
    DW 0
calc_op:
    DB 0                ; 0=none, 1='+', 2='-', 3='*', 4='/'
calc_str_buf:
    DS 16

calc_init:
    MOV [calc_val1], 0
    MOV [calc_val2], 0
    MOV [calc_op], 0
    RET

calc_draw:
    PUSH R0
    PUSH R1
    PUSH R2

    ; Draw window frame: X=22, Y=4, W=36, H=16
    MOV R0, 0x0416      ; X=22, Y=4
    MOV R1, 0x1024      ; W=36, H=16
    MOV R2, str_calc_title
    CALL draw_window_frame

    ; Display panel frame: X=25, Y=6, W=30, H=3
    MOV R0, 0x0619
    MOV R1, 0x031E
    MOV R2, 0x0720      ; black background, gray text
    INT 14

    ; Format calc_val2 into string for display
    MOV R0, [calc_val2]
    MOV R1, calc_str_buf
    CALL num_to_str

    ; Print number on display (X=27, Y=7)
    MOV R0, 0x071B
    MOV R1, calc_str_buf
    MOV R2, 0x0F        ; white text on black display
    INT 15

    ; Display current operator on right side of display
    MOV R0, 0x0733      ; X=51, Y=7
    LDB R1, [calc_op]
    CMP R1, 1
    JZ c_draw_op_plus
    CMP R1, 2
    JZ c_draw_op_minus
    CMP R1, 3
    JZ c_draw_op_mul
    CMP R1, 4
    JZ c_draw_op_div
    MOV R1, str_op_none
    JMP c_draw_op_print
c_draw_op_plus:
    MOV R1, str_op_plus
    JMP c_draw_op_print
c_draw_op_minus:
    MOV R1, str_op_minus
    JMP c_draw_op_print
c_draw_op_mul:
    MOV R1, str_op_mul
    JMP c_draw_op_print
c_draw_op_div:
    MOV R1, str_op_div
c_draw_op_print:
    MOV R2, 0x0E        ; yellow on black
    INT 15

    ; Draw calculator buttons
    ; Row 10: [ 7 ]  [ 8 ]  [ 9 ]  [ + ]
    MOV R0, 0x0A1A      ; X=26, Y=10
    MOV R1, str_calc_row1
    MOV R2, 0x1F
    INT 15

    ; Row 11: [ 4 ]  [ 5 ]  [ 6 ]  [ - ]
    MOV R0, 0x0B1A      ; X=26, Y=11
    MOV R1, str_calc_row2
    MOV R2, 0x1F
    INT 15

    ; Row 12: [ 1 ]  [ 2 ]  [ 3 ]  [ * ]
    MOV R0, 0x0C1A      ; X=26, Y=12
    MOV R1, str_calc_row3
    MOV R2, 0x1F
    INT 15

    ; Row 13: [ C ]  [ 0 ]  [ = ]  [ / ]
    MOV R0, 0x0D1A      ; X=26, Y=13
    MOV R1, str_calc_row4
    MOV R2, 0x1F
    INT 15

    ; Shortcut hint at bottom of window (Row 15)
    MOV R0, 0x0F19      ; X=25, Y=15
    MOV R1, str_calc_hint
    MOV R2, 0x17        ; gray on blue
    INT 15

    POP R2
    POP R1
    POP R0
    RET

; Calculator keyboard handler
calc_on_key:
    ; R1 = keycode
    CMP R1, 48          ; '0'
    JC c_k_check_op
    CMP R1, 58          ; '9' + 1
    JNC c_k_check_op

    ; Digit 0..9
    SUB R1, 48          ; R1 = digit
    MOV R0, [calc_val2]
    MUL R0, 10
    ADD R0, R1
    MOV [calc_val2], R0
    CALL os_repaint
    RET

c_k_check_op:
    CMP R1, 43          ; '+'
    JZ c_op_add
    CMP R1, 45          ; '-'
    JZ c_op_sub
    CMP R1, 42          ; '*'
    JZ c_op_mul
    CMP R1, 47          ; '/'
    JZ c_op_div
    CMP R1, 61          ; '='
    JZ c_op_eq
    CMP R1, 13          ; Enter
    JZ c_op_eq
    CMP R1, 67          ; 'C'
    JZ c_op_clear
    CMP R1, 99          ; 'c'
    JZ c_op_clear
    CMP R1, 8           ; Backspace
    JZ c_op_bs
    RET

c_op_add:
    MOV R0, [calc_val2]
    MOV [calc_val1], R0
    MOV [calc_val2], 0
    MOV [calc_op], 1
    CALL os_repaint
    RET

c_op_sub:
    MOV R0, [calc_val2]
    MOV [calc_val1], R0
    MOV [calc_val2], 0
    MOV [calc_op], 2
    CALL os_repaint
    RET

c_op_mul:
    MOV R0, [calc_val2]
    MOV [calc_val1], R0
    MOV [calc_val2], 0
    MOV [calc_op], 3
    CALL os_repaint
    RET

c_op_div:
    MOV R0, [calc_val2]
    MOV [calc_val1], R0
    MOV [calc_val2], 0
    MOV [calc_op], 4
    CALL os_repaint
    RET

c_op_clear:
    MOV [calc_val1], 0
    MOV [calc_val2], 0
    MOV [calc_op], 0
    CALL os_repaint
    RET

c_op_bs:
    MOV R0, [calc_val2]
    DIV R0, 10
    MOV [calc_val2], R0
    CALL os_repaint
    RET

c_op_eq:
    LDB R2, [calc_op]
    CMP R2, 1
    JZ c_exec_add
    CMP R2, 2
    JZ c_exec_sub
    CMP R2, 3
    JZ c_exec_mul
    CMP R2, 4
    JZ c_exec_div
    RET

c_exec_add:
    MOV R0, [calc_val1]
    ADD R0, [calc_val2]
    MOV [calc_val2], R0
    MOV [calc_op], 0
    CALL os_repaint
    RET

c_exec_sub:
    MOV R0, [calc_val1]
    SUB R0, [calc_val2]
    MOV [calc_val2], R0
    MOV [calc_op], 0
    CALL os_repaint
    RET

c_exec_mul:
    MOV R0, [calc_val1]
    MUL R0, [calc_val2]
    MOV [calc_val2], R0
    MOV [calc_op], 0
    CALL os_repaint
    RET

c_exec_div:
    MOV R0, [calc_val2]
    CMP R0, 0
    JZ c_div_zero
    MOV R1, [calc_val1]
    DIV R1, R0
    MOV [calc_val2], R1
    MOV [calc_op], 0
    CALL os_repaint
    RET
c_div_zero:
    MOV [calc_val2], 0
    MOV [calc_op], 0
    CALL os_repaint
    RET

; Calculator mouse click handler
calc_on_click:
    ; R1 = X, R2 = Y
    CMP R2, 10
    JZ c_clk_r1
    CMP R2, 11
    JZ c_clk_r2
    CMP R2, 12
    JZ c_clk_r3
    CMP R2, 13
    JZ c_clk_r4
    RET

c_clk_r1:
    ; X: [ 7 ]=26..29, [ 8 ]=33..36, [ 9 ]=40..43, [ + ]=47..50
    CMP R1, 26
    JC c_clk_r1_end
    CMP R1, 30
    JC c_clk_7
    CMP R1, 33
    JC c_clk_r1_end
    CMP R1, 37
    JC c_clk_8
    CMP R1, 40
    JC c_clk_r1_end
    CMP R1, 44
    JC c_clk_9
    CMP R1, 47
    JC c_clk_r1_end
    CMP R1, 51
    JC c_clk_plus
c_clk_r1_end:
    RET

c_clk_7: MOV R1, 55 ; '7'
    JMP calc_on_key
c_clk_8: MOV R1, 56 ; '8'
    JMP calc_on_key
c_clk_9: MOV R1, 57 ; '9'
    JMP calc_on_key
c_clk_plus: MOV R1, 43 ; '+'
    JMP calc_on_key

c_clk_r2:
    CMP R1, 26
    JC c_clk_r2_end
    CMP R1, 30
    JC c_clk_4
    CMP R1, 33
    JC c_clk_r2_end
    CMP R1, 37
    JC c_clk_5
    CMP R1, 40
    JC c_clk_r2_end
    CMP R1, 44
    JC c_clk_6
    CMP R1, 47
    JC c_clk_r2_end
    CMP R1, 51
    JC c_clk_minus
c_clk_r2_end:
    RET

c_clk_4: MOV R1, 52 ; '4'
    JMP calc_on_key
c_clk_5: MOV R1, 53 ; '5'
    JMP calc_on_key
c_clk_6: MOV R1, 54 ; '6'
    JMP calc_on_key
c_clk_minus: MOV R1, 45 ; '-'
    JMP calc_on_key

c_clk_r3:
    CMP R1, 26
    JC c_clk_r3_end
    CMP R1, 30
    JC c_clk_1
    CMP R1, 33
    JC c_clk_r3_end
    CMP R1, 37
    JC c_clk_2
    CMP R1, 40
    JC c_clk_r3_end
    CMP R1, 44
    JC c_clk_3
    CMP R1, 47
    JC c_clk_r3_end
    CMP R1, 51
    JC c_clk_mul
c_clk_r3_end:
    RET

c_clk_1: MOV R1, 49 ; '1'
    JMP calc_on_key
c_clk_2: MOV R1, 50 ; '2'
    JMP calc_on_key
c_clk_3: MOV R1, 51 ; '3'
    JMP calc_on_key
c_clk_mul: MOV R1, 42 ; '*'
    JMP calc_on_key

c_clk_r4:
    CMP R1, 26
    JC c_clk_r4_end
    CMP R1, 30
    JC c_clk_C
    CMP R1, 33
    JC c_clk_r4_end
    CMP R1, 37
    JC c_clk_0
    CMP R1, 40
    JC c_clk_r4_end
    CMP R1, 44
    JC c_clk_eq
    CMP R1, 47
    JC c_clk_r4_end
    CMP R1, 51
    JC c_clk_div
c_clk_r4_end:
    RET

c_clk_C: MOV R1, 67 ; 'C'
    JMP calc_on_key
c_clk_0: MOV R1, 48 ; '0'
    JMP calc_on_key
c_clk_eq: MOV R1, 61 ; '='
    JMP calc_on_key
c_clk_div: MOV R1, 47 ; '/'
    JMP calc_on_key

str_calc_title:
    DB " Dimon-16 Calculator ", 0

str_calc_row1:
    DB "[ 7 ]   [ 8 ]   [ 9 ]   [ + ]", 0
str_calc_row2:
    DB "[ 4 ]   [ 5 ]   [ 6 ]   [ - ]", 0
str_calc_row3:
    DB "[ 1 ]   [ 2 ]   [ 3 ]   [ * ]", 0
str_calc_row4:
    DB "[ C ]   [ 0 ]   [ = ]   [ / ]", 0

str_calc_hint:
    DB "Keys: 0-9, + - * /, C, Enter", 0

str_op_none:
    DB " ", 0
str_op_plus:
    DB "+", 0
str_op_minus:
    DB "-", 0
str_op_mul:
    DB "*", 0
str_op_div:
    DB "/", 0
