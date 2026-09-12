; ========================================================
; apps/fileman.asm — Application 3: File Manager / ISO Explorer
; ========================================================

fm_has_disk:
    DB 0
fm_file_count:
    DW 0
fm_selected_idx:
    DW 0
fm_preview_lba:
    DW 0
fm_cat_buf:
    DS 512              ; buffer for sector 1 (catalog)
fm_preview_buf:
    DS 512              ; buffer for previewed file

fileman_init:
    PUSH R0
    PUSH R1
    PUSH R2

    MOV [fm_file_count], 0
    MOV [fm_selected_idx], 0
    MOV [fm_preview_lba], 0

    ; 1. Check if disk is attached
    INT 7               ; SYS_DISK_INFO -> C=0 ok (R0=sectors lo), C=1 none
    JC fm_no_disk_found

    MOV [fm_has_disk], 1

    ; 2. Read catalog sector (LBA=1) to fm_cat_buf
    MOV R0, 1           ; LBA 1
    MOV R1, fm_cat_buf
    MOV R2, 1           ; 1 sector
    INT 6               ; SYS_DISK_READ
    JC fm_no_disk_found

    ; 3. Check catalog magic "MD" (0x444D)
    MOV R0, [fm_cat_buf]
    CMP R0, 0x444D
    JNZ fm_no_files

    ; 4. Count files in catalog (up to 10 on screen)
    ; Each entry has 24 bytes, starting at offset 8 in sector 1
    MOV R3, fm_cat_buf
    ADD R3, 8           ; R3 points to first entry
    MOV R4, 0           ; file counter

fm_count_loop:
    LDB R5, [R3]        ; first char of file name
    CMP R5, 0
    JZ fm_count_done

    INC R4
    ADD R3, 24          ; next catalog entry
    CMP R4, 10
    JC fm_count_loop

fm_count_done:
    MOV [fm_file_count], R4

    ; If files present, load preview of first file
    CMP R4, 0
    JZ fm_init_ret
    CALL fileman_load_preview
    JMP fm_init_ret

fm_no_disk_found:
    MOV [fm_has_disk], 0
    JMP fm_init_ret

fm_no_files:
    MOV [fm_file_count], 0

fm_init_ret:
    POP R2
    POP R1
    POP R0
    RET

; Load selected file content into preview buffer
fileman_load_preview:
    PUSH R0
    PUSH R1
    PUSH R2
    PUSH R3

    MOV R0, [fm_file_count]
    CMP R0, 0
    JZ fm_lp_done

    ; Find entry for fm_selected_idx
    MOV R1, [fm_selected_idx]
    MUL R1, 24          ; offset = idx * 24
    MOV R3, fm_cat_buf
    ADD R3, 8
    ADD R3, R1          ; R3 points to selected entry

    ; Read LBA (offset +12 in entry structure)
    ADD R3, 12
    MOV R0, [R3]        ; file LBA
    MOV [fm_preview_lba], R0

    ; Read 1 sector of file into fm_preview_buf
    MOV R1, fm_preview_buf
    MOV R2, 1
    INT 6

    ; Null-terminate preview buffer
    MOV R3, fm_preview_buf
    ADD R3, 510
    MOV R0, 0
    STB [R3], R0

fm_lp_done:
    POP R3
    POP R2
    POP R1
    POP R0
    RET

fileman_draw:
    PUSH R0
    PUSH R1
    PUSH R2
    PUSH R3

    ; Draw window: X=10, Y=3, W=60, H=18
    MOV R0, 0x030A
    MOV R1, 0x123C
    MOV R2, str_fm_title
    CALL draw_window_frame

    ; Status info bar (Row 5)
    MOV R0, 0x050C      ; X=12, Y=5
    LDB R1, [fm_has_disk]
    CMP R1, 1
    JZ fm_d_has_disk
    MOV R1, str_fm_nodisk
    MOV R2, 0x1C        ; red
    INT 15
    JMP fm_d_help

fm_d_has_disk:
    MOV R1, str_fm_disk_ok
    MOV R2, 0x1A        ; green
    INT 15

    ; Left panel header (X=12, Y=6)
    MOV R0, 0x060C
    MOV R1, str_fm_hdr_list
    MOV R2, 0x1E
    INT 15

    ; Right panel header (X=36, Y=6)
    MOV R0, 0x0624
    MOV R1, str_fm_hdr_prev
    MOV R2, 0x1E
    INT 15

    ; Draw file list in left panel (Rows 7..16)
    MOV R3, 0           ; i = 0
fm_d_file_loop:
    MOV R0, [fm_file_count]
    CMP R3, R0
    JNC fm_d_preview

    ; Calculate Y = 7 + i
    MOV R0, 7
    ADD R0, R3
    SHL R0, 8
    OR  R0, 12          ; X=12, Y=7+i

    ; Is this file selected?
    MOV R1, [fm_selected_idx]
    CMP R3, R1
    JZ fm_d_cur_sel

    ; Unselected: print spaces
    MOV R1, str_fm_space
    MOV R2, 0x1F
    INT 15
    JMP fm_d_print_name

fm_d_cur_sel:
    ; Selected: print cursor '>'
    MOV R1, str_fm_sel_arrow
    MOV R2, 0x1E        ; yellow
    INT 15

fm_d_print_name:
    ; Print filename from catalog
    MOV R1, R3
    MUL R1, 24
    MOV R4, fm_cat_buf
    ADD R4, 8
    ADD R4, R1          ; R4 points to name (12 bytes)

    ; Move X to 15
    MOV R0, 7
    ADD R0, R3
    SHL R0, 8
    OR  R0, 15

    ; Copy to temp buffer with null terminator
    MOV R5, 0
fm_cp_name:
    LDB R6, [R4]
    MOV R7, fm_temp_name
    ADD R7, R5
    STB [R7], R6
    INC R4
    INC R5
    CMP R5, 11
    JC fm_cp_name
    MOV R7, fm_temp_name
    ADD R7, 11
    MOV R6, 0
    STB [R7], R6

    MOV R1, fm_temp_name
    MOV R2, 0x1F
    INT 15

    INC R3
    CMP R3, 9
    JC fm_d_file_loop

fm_d_preview:
    ; Draw preview content in right panel (X=36..67, Y=7..16)
    MOV R0, 0x0724      ; X=36, Y=7
    MOV R1, 0x0A1F      ; W=31, H=10
    MOV R2, 0x0720      ; black background, gray text
    INT 14

    MOV R0, [fm_file_count]
    CMP R0, 0
    JZ fm_d_help

    ; Print text from fm_preview_buf in right panel
    MOV R3, fm_preview_buf
    MOV R4, 37          ; cur_x
    MOV R5, 8           ; cur_y
fm_prev_txt_loop:
    LDB R6, [R3]
    CMP R6, 0
    JZ fm_d_help
    CMP R6, 10          ; '\n'
    JZ fm_prev_nl

    ; Print single character
    MOV R0, R5
    SHL R0, 8
    OR  R0, R4
    MOV R1, 0x0101
    MOV R2, 0x0F00
    OR  R2, R6
    INT 14

    INC R4
    CMP R4, 66
    JC fm_prev_next

fm_prev_nl:
    MOV R4, 37
    INC R5
    CMP R5, 17
    JNC fm_d_help

fm_prev_next:
    INC R3
    JMP fm_prev_txt_loop

fm_d_help:
    ; Bottom hint bar (Row 19, X=12)
    MOV R0, 0x130C
    MOV R1, str_fm_bot_help
    MOV R2, 0x17
    INT 15

    POP R3
    POP R2
    POP R1
    POP R0
    RET

fileman_on_key:
    ; R1 = keycode
    CMP R1, 256         ; KEY_UP
    JZ fm_k_up
    CMP R1, 257         ; KEY_DOWN
    JZ fm_k_down
    CMP R1, 13          ; Enter
    JZ fm_k_enter
    CMP R1, 114         ; 'r' (refresh)
    JZ fm_k_refresh
    RET

fm_k_up:
    MOV R0, [fm_selected_idx]
    CMP R0, 0
    JZ fm_k_up_done
    DEC R0
    MOV [fm_selected_idx], R0
    CALL fileman_load_preview
    CALL os_repaint
fm_k_up_done:
    RET

fm_k_down:
    MOV R0, [fm_selected_idx]
    INC R0
    MOV R2, [fm_file_count]
    CMP R0, R2
    JNC fm_k_down_done
    MOV [fm_selected_idx], R0
    CALL fileman_load_preview
    CALL os_repaint
fm_k_down_done:
    RET

fm_k_enter:
    CALL fileman_load_preview
    CALL os_repaint
    RET

fm_k_refresh:
    CALL fileman_init
    CALL os_repaint
    RET

fileman_on_click:
    ; R1 = X, R2 = Y
    ; Check click in file list rows (Y=7..16, X=12..30)
    CMP R1, 12
    JC fm_clk_done
    CMP R1, 31
    JNC fm_clk_done

    CMP R2, 7
    JC fm_clk_done
    CMP R2, 17
    JNC fm_clk_done

    ; Calculate clicked index = Y - 7
    MOV R0, R2
    SUB R0, 7
    MOV R3, [fm_file_count]
    CMP R0, R3
    JNC fm_clk_done

    MOV [fm_selected_idx], R0
    CALL fileman_load_preview
    CALL os_repaint

fm_clk_done:
    RET

str_fm_title:
    DB " ISO File Explorer ", 0

str_fm_nodisk:
    DB "No ISO disk attached! Run with --iso file.iso", 0
str_fm_disk_ok:
    DB "ISO disk attached: DIMON-ISO file system active.", 0

str_fm_hdr_list:
    DB "FILES ON DISK:", 0
str_fm_hdr_prev:
    DB "FILE PREVIEW:", 0

str_fm_space:
    DB "  ", 0
str_fm_sel_arrow:
    DB "> ", 0

str_fm_bot_help:
    DB "Up/Down: Select file | Enter: Preview | R: Refresh", 0

fm_temp_name:
    DS 16
