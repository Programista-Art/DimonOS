; ========================================================
; apps/notepad.asm — Application 2: Notepad with ISO Disk Support
; ========================================================
; Notepad limits: max 500 chars, disk sector 10

note_len:
    DW 0
note_cursor:
    DW 0
note_status_msg:
    DW 0
note_buf:
    DS 512

notepad_init:
    PUSH R0
    PUSH R1
    ; If buffer is empty, insert default welcome text
    MOV R0, [note_len]
    CMP R0, 0
    JNZ np_init_done

    MOV R0, note_buf
    MOV R1, str_np_default
    CALL str_copy

    MOV R0, note_buf
    CALL str_len
    MOV [note_len], R1
    MOV [note_cursor], R1

    MOV [note_status_msg], str_np_st_ready

np_init_done:
    POP R1
    POP R0
    RET

notepad_draw:
    PUSH R0
    PUSH R1
    PUSH R2
    PUSH R3

    ; Draw window frame: X=10, Y=3, W=60, H=18
    MOV R0, 0x030A      ; X=10, Y=3
    MOV R1, 0x123C      ; W=60 (0x3C), H=18 (0x12)
    MOV R2, str_np_title
    CALL draw_window_frame

    ; Toolbar (Row 5): [F8:New]   [F9:Save ISO]   [F10:Load ISO]
    MOV R0, 0x050C      ; X=12, Y=5
    MOV R1, str_np_toolbar
    MOV R2, 0x1E        ; yellow on blue
    INT 15

    ; Text editing area: X=12, Y=7, W=56, H=11 (background 0x17)
    MOV R0, 0x070C
    MOV R1, 0x0B38      ; W=56, H=11
    MOV R2, 0x1720
    INT 14

    ; Draw text from buffer line by line
    MOV R3, note_buf    ; buffer pointer
    MOV R4, 12          ; cur_x = 12
    MOV R5, 7           ; cur_y = 7
    MOV R6, 0           ; char index

np_draw_loop:
    LDB R7, [R3]
    CMP R7, 0
    JZ np_draw_cursor

    CMP R7, 10          ; '\n'
    JZ np_draw_nl

    ; Print single character
    MOV R0, R5
    SHL R0, 8
    OR  R0, R4          ; X | (Y << 8)
    MOV R1, 0x0101      ; W=1, H=1
    MOV R2, 0x1700
    OR  R2, R7          ; char | 0x1700
    INT 14

    INC R4
    ; Line wrap (max X=66)
    CMP R4, 67
    JC np_next_char

np_draw_nl:
    MOV R4, 12          ; return to line start
    INC R5              ; next line
    CMP R5, 18          ; check if past bottom edge
    JNC np_draw_cursor

np_next_char:
    INC R3
    INC R6
    JMP np_draw_loop

np_draw_cursor:
    ; Draw visible cursor '_' at current position (R4, R5)
    CMP R5, 18
    JNC np_draw_statusbar
    MOV R0, R5
    SHL R0, 8
    OR  R0, R4
    MOV R1, 0x0101
    MOV R2, 0x705F      ; '_' (0x5F) on gray background 0x70
    INT 14

np_draw_statusbar:
    ; Status bar at window bottom (Row 19, X=12)
    MOV R0, 0x130C      ; X=12, Y=19
    MOV R1, [note_status_msg]
    MOV R2, 0x1A        ; light green
    INT 15

    POP R3
    POP R2
    POP R1
    POP R0
    RET

; Notepad keyboard handler
notepad_on_key:
    ; R1 = keycode
    CMP R1, 267         ; KEY_F8: New document
    JZ np_k_new
    CMP R1, 268         ; KEY_F9: Save to ISO disk
    JZ np_k_save
    CMP R1, 269         ; KEY_F10: Load from ISO disk
    JZ np_k_load

    ; Backspace (8 or 127)
    CMP R1, 8
    JZ np_k_backspace

    ; Enter (13)
    CMP R1, 13
    JZ np_k_enter

    ; Printable ASCII characters (32..126)
    CMP R1, 32
    JC np_k_ignore
    CMP R1, 127
    JNC np_k_ignore

    ; Check buffer limit
    MOV R0, [note_len]
    CMP R0, 500
    JNC np_k_ignore

    ; Append character to buffer
    MOV R2, note_buf
    ADD R2, R0          ; note_buf + note_len
    STB [R2], R1
    INC R0
    MOV [note_len], R0
    ; Add null terminator
    INC R2
    MOV R3, 0
    STB [R2], R3

    MOV [note_status_msg], str_np_st_editing
    CALL os_repaint
    RET

np_k_enter:
    MOV R0, [note_len]
    CMP R0, 500
    JNC np_k_ignore

    MOV R2, note_buf
    ADD R2, R0
    MOV R3, 10          ; '\n'
    STB [R2], R3
    INC R0
    MOV [note_len], R0
    INC R2
    MOV R3, 0
    STB [R2], R3

    CALL os_repaint
    RET

np_k_backspace:
    MOV R0, [note_len]
    CMP R0, 0
    JZ np_k_ignore
    DEC R0
    MOV [note_len], R0
    MOV R2, note_buf
    ADD R2, R0
    MOV R3, 0
    STB [R2], R3
    CALL os_repaint
    RET

np_k_new:
    MOV R0, 0
    MOV [note_len], R0
    MOV [note_cursor], R0
    STB [note_buf], R0
    MOV [note_status_msg], str_np_st_new
    CALL os_repaint
    RET

np_k_save:
    ; Save 512 bytes of notepad buffer to ISO disk at sector 10
    MOV R0, 10          ; LBA = 10
    MOV R1, note_buf    ; RAM address
    MOV R2, 1           ; 1 sector (512 bytes)
    INT 8               ; SYS_DISK_WRITE -> C=0 ok, C=1 error
    JC np_save_err
    MOV [note_status_msg], str_np_st_saved
    CALL os_repaint
    RET

np_save_err:
    MOV [note_status_msg], str_np_st_err_ro
    CALL os_repaint
    RET

np_k_load:
    ; Read 512 bytes from ISO disk at sector 10
    MOV R0, 10
    MOV R1, note_buf
    MOV R2, 1
    INT 6               ; SYS_DISK_READ
    JC np_load_err

    ; Measure length of loaded text
    MOV R0, note_buf
    CALL str_len
    MOV [note_len], R1
    MOV [note_cursor], R1
    MOV [note_status_msg], str_np_st_loaded
    CALL os_repaint
    RET

np_load_err:
    MOV [note_status_msg], str_np_st_err_ld
    CALL os_repaint
    RET

np_k_ignore:
    RET

; Notepad mouse click handler
notepad_on_click:
    ; R1 = X, R2 = Y
    CMP R2, 5
    JNZ np_clk_none

    ; [F8:New] X=12..21
    CMP R1, 12
    JC np_clk_none
    CMP R1, 22
    JC np_k_new

    ; [F9:Save ISO] X=23..37
    CMP R1, 23
    JC np_clk_none
    CMP R1, 38
    JC np_k_save

    ; [F10:Load ISO] X=39..55
    CMP R1, 39
    JC np_clk_none
    CMP R1, 56
    JC np_k_load

np_clk_none:
    RET

str_np_title:
    DB " DimonOS Notepad ", 0

str_np_toolbar:
    DB "[F8:New]   [F9:Save ISO]   [F10:Load ISO]", 0

str_np_default:
    DB "Welcome to DimonOS Notepad!", 10, "You can write notes here and save them", 10, "directly to the virtual ISO disk.", 0

str_np_st_ready:
    DB "Status: Ready | Type text from keyboard", 0
str_np_st_editing:
    DB "Status: Editing... | F9=Save, F10=Load", 0
str_np_st_new:
    DB "Status: Notepad buffer cleared.", 0
str_np_st_saved:
    DB "Status: Success! Note saved to ISO sector 10.", 0
str_np_st_loaded:
    DB "Status: Success! Note loaded from ISO sector 10.", 0
str_np_st_err_ro:
    DB "Write error: Disk is read-only (--disk-writable).", 0
str_np_st_err_ld:
    DB "Read error from ISO disk!", 0
