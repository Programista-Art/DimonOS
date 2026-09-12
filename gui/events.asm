; ========================================================
; gui/events.asm — Event Loop & Dispatcher (Keyboard / Mouse / Timer)
; ========================================================

events_poll:
    ; Poll event from VM queue
    INT 11              ; SYS_GUI_POLL_EVENT -> R0=type, R1=code/X, R2=data/Y
    CMP R0, 0
    JZ evt_none

    CMP R0, 1
    JZ evt_handle_key
    CMP R0, 2
    JZ evt_handle_click
    CMP R0, 4
    JZ evt_handle_timer
    RET

evt_none:
    RET

; --- Keyboard Event Handler ---
evt_handle_key:
    ; R1 = keycode
    ; Check ESC (code 27)
    CMP R1, 27
    JZ evt_key_esc

    ; If Start Menu is open, keys 1-7 select an item
    LDB R0, [start_menu_open]
    CMP R0, 1
    JZ evt_sm_key

    ; Function key shortcuts F1..F7
    CMP R1, 260         ; KEY_F1
    JZ evt_open_1
    CMP R1, 261         ; KEY_F2
    JZ evt_open_2
    CMP R1, 262         ; KEY_F3
    JZ evt_open_3
    CMP R1, 263         ; KEY_F4
    JZ evt_open_4
    CMP R1, 264         ; KEY_F5
    JZ evt_open_5
    CMP R1, 265         ; KEY_F6
    JZ evt_open_6
    CMP R1, 266         ; KEY_F7
    JZ evt_open_7

    ; If on clean desktop (active_window == 0)
    LDB R0, [active_window]
    CMP R0, 0
    JNZ evt_send_to_app

    ; Numeric keys 1..7 on desktop open applications
    CMP R1, 49          ; '1'
    JZ evt_open_1
    CMP R1, 50          ; '2'
    JZ evt_open_2
    CMP R1, 51          ; '3'
    JZ evt_open_3
    CMP R1, 52          ; '4'
    JZ evt_open_4
    CMP R1, 53          ; '5'
    JZ evt_open_5
    CMP R1, 54          ; '6'
    JZ evt_open_6
    CMP R1, 55          ; '7'
    JZ evt_open_7
    CMP R1, 113         ; 'q'
    JZ evt_quit_os
    CMP R1, 81          ; 'Q'
    JZ evt_quit_os
    RET

evt_sm_key:
    CMP R1, 49
    JZ evt_open_1
    CMP R1, 50
    JZ evt_open_2
    CMP R1, 51
    JZ evt_open_3
    CMP R1, 52
    JZ evt_open_4
    CMP R1, 53
    JZ evt_open_5
    CMP R1, 54
    JZ evt_open_6
    CMP R1, 55
    JZ evt_open_7
    CMP R1, 120         ; 'x'
    JZ evt_quit_os
    CMP R1, 88          ; 'X'
    JZ evt_quit_os
    MOV [start_menu_open], 0
    CALL os_repaint
    RET

evt_key_esc:
    LDB R0, [start_menu_open]
    CMP R0, 1
    JZ evt_close_sm
    LDB R0, [active_window]
    CMP R0, 0
    JZ evt_done_k
    CALL window_close
    RET
evt_close_sm:
    MOV [start_menu_open], 0
    CALL os_repaint
    RET

evt_open_1:
    MOV R0, 1
    CALL window_open
    RET
evt_open_2:
    MOV R0, 2
    CALL window_open
    RET
evt_open_3:
    MOV R0, 3
    CALL window_open
    RET
evt_open_4:
    MOV R0, 4
    CALL window_open
    RET
evt_open_5:
    MOV R0, 5
    CALL window_open
    RET
evt_open_6:
    MOV R0, 6
    CALL window_open
    RET
evt_open_7:
    MOV R0, 7
    CALL window_open
    RET

evt_quit_os:
    HLT

evt_send_to_app:
    ; Forward key code to active application
    LDB R0, [active_window]
    CMP R0, 1
    JZ evt_k_calc
    CMP R0, 2
    JZ evt_k_notepad
    CMP R0, 3
    JZ evt_k_fileman
    CMP R0, 4
    JZ evt_k_paint
    CMP R0, 5
    JZ evt_k_sysinfo
    CMP R0, 6
    JZ evt_k_snake
    CMP R0, 7
    JZ evt_k_terminal
evt_done_k:
    RET

evt_k_calc:
    CALL calc_on_key
    RET
evt_k_notepad:
    CALL notepad_on_key
    RET
evt_k_fileman:
    CALL fileman_on_key
    RET
evt_k_paint:
    CALL paint_on_key
    RET
evt_k_sysinfo:
    CALL sysinfo_on_key
    RET
evt_k_snake:
    CALL snake_on_key
    RET
evt_k_terminal:
    CALL terminal_on_key
    RET

; --- Mouse Click Event Handler ---
evt_handle_click:
    ; R1 = X (0..79), R2 = Y (0..24)
    ; 1. Bottom taskbar click (Y == 24)
    CMP R2, 24
    JZ evt_clk_taskbar

    ; 2. Start Menu open check
    LDB R0, [start_menu_open]
    CMP R0, 1
    JZ evt_clk_start_menu

    ; 3. Top bar click (Y == 0)
    CMP R2, 0
    JZ evt_clk_topbar

    ; 4. Active window check: close button [X] or inside window
    LDB R0, [active_window]
    CMP R0, 0
    JNZ evt_clk_window

    ; 5. Desktop icon clicks
    JMP evt_clk_desktop_icons

evt_clk_taskbar:
    ; START button (X=1..11)
    CMP R1, 1
    JC evt_clk_tb_other
    CMP R1, 12
    JNC evt_clk_tb_other
    ; Toggle Start Menu
    LDB R0, [start_menu_open]
    XOR R0, 1
    STB [start_menu_open], R0
    CALL os_repaint
    RET

evt_clk_tb_other:
    RET

evt_clk_start_menu:
    ; Menu bounds: X=1..21, Y=13..23
    CMP R1, 1
    JC evt_sm_close
    CMP R1, 22
    JNC evt_sm_close

    CMP R2, 14
    JZ evt_open_1
    CMP R2, 15
    JZ evt_open_2
    CMP R2, 16
    JZ evt_open_3
    CMP R2, 17
    JZ evt_open_4
    CMP R2, 18
    JZ evt_open_5
    CMP R2, 19
    JZ evt_open_6
    CMP R2, 20
    JZ evt_open_7
    CMP R2, 22
    JZ evt_quit_os

evt_sm_close:
    MOV [start_menu_open], 0
    CALL os_repaint
    RET

evt_clk_topbar:
    ; Shortcuts on top bar: 1 Calc, 2 Notes, 3 Files, 4 Paint, 5 Info, 6 Snake, 7 Term
    CMP R1, 18
    JC evt_tb_done
    CMP R1, 26
    JC evt_open_1
    CMP R1, 35
    JC evt_open_2
    CMP R1, 44
    JC evt_open_3
    CMP R1, 53
    JC evt_open_4
    CMP R1, 61
    JC evt_open_5
    CMP R1, 70
    JC evt_open_6
    CMP R1, 78
    JC evt_open_7
evt_tb_done:
    RET

evt_clk_window:
    ; Check if [X] close button was clicked
    MOV R0, [cur_win_pos]
    MOV R3, R0
    AND R3, 0x00FF      ; win_x
    MOV R4, R0
    SHR R4, 8           ; win_y

    ; Click on title row? (Y == win_y)
    CMP R2, R4
    JNZ evt_clk_in_app

    ; Click on [X] button? (win_x .. win_x + 3)
    CMP R1, R3
    JC evt_clk_in_app
    MOV R5, R3
    ADD R5, 3
    CMP R1, R5
    JNC evt_clk_in_app

    ; Close window
    CALL window_close
    RET

evt_clk_in_app:
    ; Pass click coordinates (R1=X, R2=Y) to active application
    LDB R0, [active_window]
    CMP R0, 1
    JZ evt_c_calc
    CMP R0, 2
    JZ evt_c_notepad
    CMP R0, 3
    JZ evt_c_fileman
    CMP R0, 4
    JZ evt_c_paint
    CMP R0, 5
    JZ evt_c_sysinfo
    CMP R0, 6
    JZ evt_c_snake
    CMP R0, 7
    JZ evt_c_terminal
    RET

evt_c_calc:
    CALL calc_on_click
    RET
evt_c_notepad:
    CALL notepad_on_click
    RET
evt_c_fileman:
    CALL fileman_on_click
    RET
evt_c_paint:
    CALL paint_on_click
    RET
evt_c_sysinfo:
    CALL sysinfo_on_click
    RET
evt_c_snake:
    CALL snake_on_click
    RET
evt_c_terminal:
    CALL terminal_on_click
    RET

evt_clk_desktop_icons:
    ; Column 1: X=4..21
    CMP R1, 4
    JC evt_clk_col2
    CMP R1, 22
    JNC evt_clk_col2

    CMP R2, 3
    JZ evt_open_1
    CMP R2, 6
    JZ evt_open_2
    CMP R2, 9
    JZ evt_open_3
    CMP R2, 12
    JZ evt_open_4
    CMP R2, 15
    JZ evt_open_5
    RET

evt_clk_col2:
    ; Column 2: X=24..41
    CMP R1, 24
    JC evt_clk_end
    CMP R1, 42
    JNC evt_clk_end

    CMP R2, 3
    JZ evt_open_6
    CMP R2, 6
    JZ evt_open_7
evt_clk_end:
    RET

; --- Timer Event Handler ---
evt_handle_timer:
    LDB R0, [active_window]
    CMP R0, 5
    JZ evt_t_sysinfo
    CMP R0, 6
    JZ evt_t_snake
    RET

evt_t_sysinfo:
    CALL sysinfo_on_timer
    RET
evt_t_snake:
    CALL snake_on_timer
    RET
