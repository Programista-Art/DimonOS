; ========================================================
; apps/terminal.asm — Application 7: Built-in Terminal CLI
; ========================================================
; Terminal parameters: buffer 45 chars, max 8 history lines

term_input_len:
    DW 0
term_history_count:
    DW 0

term_input_buf:
    DS 64

; History buffer: 8 lines (each 50 characters)
term_lines:
    DS 400

terminal_init:
    PUSH R0
    PUSH R1
    MOV [term_input_len], 0
    MOV [term_history_count], 0
    MOV R0, term_input_buf
    MOV R1, 0
    STB [R0], R1

    ; Add welcome banner lines to terminal history
    MOV R0, str_term_banner1
    CALL term_add_line
    MOV R0, str_term_banner2
    CALL term_add_line

    POP R1
    POP R0
    RET

; Append string in R0 as new line to terminal history
term_add_line:
    PUSH R0
    PUSH R1
    PUSH R2
    PUSH R3

    MOV R1, [term_history_count]
    CMP R1, 8
    JC term_al_append

    ; Scroll lines upwards (shift lines 1..7 to 0..6)
    MOV R2, 0
term_scroll_loop:
    MOV R3, term_lines
    ADD R3, R2
    MOV R4, R3
    ADD R4, 50          ; next line

    PUSH R0
    PUSH R1
    MOV R0, R3
    MOV R1, R4
    CALL str_copy
    POP R1
    POP R0

    ADD R2, 50
    CMP R2, 350
    JC term_scroll_loop

    ; New line lands at position 7 (offset 350)
    MOV R1, 7
    JMP term_al_copy

term_al_append:
    MOV R2, [term_history_count]
    INC R2
    MOV [term_history_count], R2

term_al_copy:
    MUL R1, 50
    MOV R2, term_lines
    ADD R2, R1
    MOV R1, R0          ; src
    MOV R0, R2          ; dest
    CALL str_copy

    POP R3
    POP R2
    POP R1
    POP R0
    RET

terminal_draw:
    PUSH R0
    PUSH R1
    PUSH R2
    PUSH R3

    ; Draw window frame: X=10, Y=3, W=60, H=18
    MOV R0, 0x030A      ; X=10, Y=3
    MOV R1, 0x123C      ; W=60, H=18
    MOV R2, str_term_title
    CALL draw_window_frame

    ; Black console interior: X=12, Y=5, W=56, H=13
    MOV R0, 0x050C
    MOV R1, 0x0D38
    MOV R2, 0x0720      ; black background, gray text
    INT 14

    ; Draw history lines (Rows 6..14)
    MOV R3, 0           ; i = 0
term_d_hist_loop:
    MOV R0, [term_history_count]
    CMP R3, R0
    JNC term_d_prompt

    ; Calculate line pointer
    MOV R1, R3
    MUL R1, 50
    MOV R2, term_lines
    ADD R2, R1

    ; Print line on screen (X=13, Y=6+i)
    MOV R0, 6
    ADD R0, R3
    SHL R0, 8
    OR  R0, 13          ; X=13, Y=6+i
    MOV R1, R2
    MOV R2, 0x0A        ; bright green retro terminal
    INT 15

    INC R3
    CMP R3, 8
    JC term_d_hist_loop

term_d_prompt:
    ; Draw prompt: DimonOS> at row 16
    MOV R0, 0x100D      ; X=13, Y=16
    MOV R1, str_term_prompt
    MOV R2, 0x0E        ; yellow
    INT 15

    ; Draw typed input text at row 16
    MOV R0, 0x1016      ; X=22, Y=16
    MOV R1, term_input_buf
    MOV R2, 0x0F        ; white
    INT 15

    ; Draw cursor '_'
    MOV R0, [term_input_len]
    ADD R0, 22          ; X = 22 + len
    MOV R1, 16          ; Y = 16
    SHL R1, 8
    OR  R0, R1          ; X | (Y << 8)
    MOV R1, 0x0101
    MOV R2, 0x0E5F      ; '_' yellow
    INT 14

    ; Bottom help bar (Row 19, X=12)
    MOV R0, 0x130C
    MOV R1, str_term_help_bar
    MOV R2, 0x17
    INT 15

    POP R3
    POP R2
    POP R1
    POP R0
    RET

terminal_on_key:
    ; R1 = keycode
    CMP R1, 13          ; Enter
    JZ term_k_enter
    CMP R1, 8           ; Backspace
    JZ term_k_backspace

    ; Printable ASCII (32..126)
    CMP R1, 32
    JC term_k_ret
    CMP R1, 127
    JNC term_k_ret

    MOV R0, [term_input_len]
    CMP R0, 45
    JNC term_k_ret

    MOV R2, term_input_buf
    ADD R2, R0
    STB [R2], R1
    INC R0
    MOV [term_input_len], R0
    INC R2
    MOV R3, 0
    STB [R2], R3

    CALL os_repaint
    RET

term_k_backspace:
    MOV R0, [term_input_len]
    CMP R0, 0
    JZ term_k_ret
    DEC R0
    MOV [term_input_len], R0
    MOV R2, term_input_buf
    ADD R2, R0
    MOV R3, 0
    STB [R2], R3
    CALL os_repaint
    RET

term_k_enter:
    ; Execute command in buffer
    MOV R0, [term_input_len]
    CMP R0, 0
    JZ term_empty_enter

    ; Add entered command to history
    MOV R0, term_input_buf
    CALL term_add_line

    ; Check commands: HELP, INFO, CLEAR, FIB, ADD, EXIT
    MOV R0, term_input_buf
    MOV R1, str_cmd_help
    CALL str_cmp
    CMP R2, 0
    JZ term_exec_help

    MOV R0, term_input_buf
    MOV R1, str_cmd_info
    CALL str_cmp
    CMP R2, 0
    JZ term_exec_info

    MOV R0, term_input_buf
    MOV R1, str_cmd_clear
    CALL str_cmp
    CMP R2, 0
    JZ term_exec_clear

    MOV R0, term_input_buf
    MOV R1, str_cmd_fib
    CALL str_cmp
    CMP R2, 0
    JZ term_exec_fib

    MOV R0, term_input_buf
    MOV R1, str_cmd_add
    CALL str_cmp
    CMP R2, 0
    JZ term_exec_add

    MOV R0, term_input_buf
    MOV R1, str_cmd_exit
    CALL str_cmp
    CMP R2, 0
    JZ term_exec_exit

    ; Unknown command
    MOV R0, str_term_unknown
    CALL term_add_line
    JMP term_cmd_done

term_exec_help:
    MOV R0, str_term_h1
    CALL term_add_line
    MOV R0, str_term_h2
    CALL term_add_line
    JMP term_cmd_done

term_exec_info:
    MOV R0, str_term_inf1
    CALL term_add_line
    MOV R0, str_term_inf2
    CALL term_add_line
    JMP term_cmd_done

term_exec_clear:
    MOV [term_history_count], 0
    JMP term_cmd_done

term_exec_fib:
    MOV R0, str_term_fib_res
    CALL term_add_line
    JMP term_cmd_done

term_exec_add:
    MOV R0, str_term_add_res
    CALL term_add_line
    JMP term_cmd_done

term_exec_exit:
    CALL window_close
    RET

term_empty_enter:
term_cmd_done:
    ; Clear input buffer
    MOV [term_input_len], 0
    MOV R0, term_input_buf
    MOV R1, 0
    STB [R0], R1
    CALL os_repaint
term_k_ret:
    RET

terminal_on_click:
    RET

str_term_title:
    DB " Dimon-16 Terminal CLI ", 0

str_term_prompt:
    DB "DimonOS> ", 0

str_term_banner1:
    DB "DimonOS v2.1 CLI Terminal [Type HELP]", 0
str_term_banner2:
    DB "Compatible with original DimonOS v1.0 commands.", 0

str_term_help_bar:
    DB "Available commands: HELP, INFO, FIB, ADD, CLEAR, EXIT", 0

str_cmd_help:
    DB "HELP", 0
str_cmd_info:
    DB "INFO", 0
str_cmd_clear:
    DB "CLEAR", 0
str_cmd_fib:
    DB "FIB", 0
str_cmd_add:
    DB "ADD", 0
str_cmd_exit:
    DB "EXIT", 0

str_term_h1:
    DB "Commands: HELP, INFO, FIB, ADD, CLEAR, EXIT", 0
str_term_h2:
    DB "You can also launch apps from top bar or desktop.", 0

str_term_inf1:
    DB "Dimon-16: 16-bit RISC/CISC, 64KB RAM, VRAM @ 0xE000", 0
str_term_inf2:
    DB "Supports dual backend: Native VGA / X11 and ANSI TUI.", 0

str_term_fib_res:
    DB "Fibonacci sequence: 0, 1, 1, 2, 3, 5, 8, 13, 21, 34", 0

str_term_add_res:
    DB "Addition test: 128 + 256 = 384 (flags Z=0, C=0)", 0

str_term_unknown:
    DB "Unknown command. Type HELP for assistance.", 0
