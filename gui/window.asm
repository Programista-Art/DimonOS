; ========================================================
; gui/window.asm — Window Manager
; ========================================================

active_window:
    DW 0                ; 0=Desktop, 1=Calc, 2=Notepad, 3=FileMan, 4=Paint, 5=SysInfo, 6=Snake, 7=Terminal

window_open:
    ; R0 = application ID (1..7)
    PUSH R0
    PUSH R1
    MOV [active_window], R0
    MOV [start_menu_open], 0

    ; Initialize selected application
    CMP R0, 1
    JZ wo_init_calc
    CMP R0, 2
    JZ wo_init_notepad
    CMP R0, 3
    JZ wo_init_fileman
    CMP R0, 4
    JZ wo_init_paint
    CMP R0, 5
    JZ wo_init_sysinfo
    CMP R0, 6
    JZ wo_init_snake
    CMP R0, 7
    JZ wo_init_terminal
    JMP wo_redraw

wo_init_calc:
    CALL calc_init
    JMP wo_redraw
wo_init_notepad:
    CALL notepad_init
    JMP wo_redraw
wo_init_fileman:
    CALL fileman_init
    JMP wo_redraw
wo_init_paint:
    CALL paint_init
    JMP wo_redraw
wo_init_sysinfo:
    CALL sysinfo_init
    JMP wo_redraw
wo_init_snake:
    CALL snake_init
    JMP wo_redraw
wo_init_terminal:
    CALL terminal_init

wo_redraw:
    CALL os_repaint
    POP R1
    POP R0
    RET

window_close:
    MOV [active_window], 0
    CALL os_repaint
    RET

; Helper to draw standard window frame:
; Input:
;   R0 = X | (Y << 8)
;   R1 = W | (H << 8)
;   R2 = pointer to null-terminated title string
draw_window_frame:
    PUSH R0
    PUSH R1
    PUSH R2
    PUSH R3
    PUSH R4

    MOV [cur_win_pos], R0
    MOV [cur_win_dim], R1
    MOV [cur_win_title], R2

    ; Unpack X, Y, W, H
    MOV R3, R0
    AND R3, 0x00FF      ; R3 = X
    MOV R4, R0
    SHR R4, 8           ; R4 = Y

    ; 1. Fill window interior (blue background 0x1F, space)
    MOV R2, 0x1F20      ; ' ', 0x1F (white on blue)
    INT 14

    ; 2. Title bar (row Y, width W, color 0x3F)
    MOV R1, [cur_win_dim]
    AND R1, 0x00FF      ; R1 = W
    OR  R1, 0x0100      ; H = 1 -> R1 = W | (1 << 8)
    MOV R2, 0x3F20      ; ' ', 0x3F (white on cyan)
    INT 14

    ; 3. Close button [X] on left side of title bar
    MOV R0, [cur_win_pos] ; X, Y
    MOV R1, str_win_close_btn
    MOV R2, 0x4F        ; white on red!
    INT 15

    ; 4. Window title
    MOV R0, [cur_win_pos]
    ADD R0, 5           ; X + 5
    MOV R1, [cur_win_title]
    MOV R2, 0x3F        ; white on cyan
    INT 15

    ; 5. Draw border around window
    MOV R0, [cur_win_pos]
    MOV R3, R0
    AND R3, 0x00FF      ; X
    MOV R4, R0
    SHR R4, 8           ; Y

    MOV R1, [cur_win_dim]
    MOV R5, R1
    AND R5, 0x00FF      ; W
    MOV R6, R1
    SHR R6, 8           ; H

    ; Bottom line of frame (Y_bot = Y + H - 1)
    MOV R0, R4
    ADD R0, R6
    DEC R0              ; Y_bot
    SHL R0, 8
    OR  R0, R3          ; X | (Y_bot << 8)
    MOV R1, R5
    OR  R1, 0x0100      ; W | (1 << 8)
    MOV R2, 0x1FC4      ; char 0xC4 (─), color 0x1F
    INT 14

    ; Left edge
    MOV R0, [cur_win_pos]
    MOV R1, 0x0100      ; W=1
    OR  R1, R6          ; H
    SHL R1, 8
    OR  R1, 1           ; W=1 | (H << 8)
    MOV R2, 0x1FB3      ; char 0xB3 (│), color 0x1F
    INT 14

    ; Right edge
    MOV R0, R3
    ADD R0, R5
    DEC R0              ; X_right = X + W - 1
    MOV R7, R4
    SHL R7, 8
    OR  R0, R7          ; X_right | (Y << 8)
    MOV R2, 0x1FB3      ; char 0xB3 (│), color 0x1F
    INT 14

    ; Frame corners
    ; Top-left corner
    MOV R0, [cur_win_pos]
    MOV R1, 0x0101
    MOV R2, 0x3FDA      ; 0xDA (┌)
    INT 14

    ; Top-right corner
    MOV R0, R3
    ADD R0, R5
    DEC R0
    MOV R7, R4
    SHL R7, 8
    OR  R0, R7
    MOV R1, 0x0101
    MOV R2, 0x3FBF      ; 0xBF (┐)
    INT 14

    ; Bottom-left corner
    MOV R0, R4
    ADD R0, R6
    DEC R0
    SHL R0, 8
    OR  R0, R3
    MOV R1, 0x0101
    MOV R2, 0x1FC0      ; 0xC0 (└)
    INT 14

    ; Bottom-right corner
    MOV R0, R3
    ADD R0, R5
    DEC R0
    MOV R7, R4
    ADD R7, R6
    DEC R7
    SHL R7, 8
    OR  R0, R7
    MOV R1, 0x0101
    MOV R2, 0x1FD9      ; 0xD9 (┘)
    INT 14

    POP R4
    POP R3
    POP R2
    POP R1
    POP R0
    RET

str_win_close_btn:
    DB "[X]", 0

cur_win_pos:
    DW 0
cur_win_dim:
    DW 0
cur_win_title:
    DW 0
