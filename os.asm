; ==============================================================================
; DimonOS-64 - Modern TrueColor Desktop Operating System
; 64-bit RISC-V architecture for DimonVirtualCPU-64
; Display: 800x600 @ 32bpp TrueColor Linear Framebuffer (LFB) at 0x02000000
; Multitasking: Kernel Shell + Background Worker Task + Hardware Timer
; ==============================================================================

    .org 0x0

boot:
    LI a7, 10
    ECALL                    ; SYS_GUI_INIT (returns 800x600 LFB at 0x02000000)

    ; Setup hardware timer multitasking
    LI a0, 500
    LI a7, 22
    ECALL                    ; timer period = 500 cycles per tick
    LA a0, timer_isr
    LI a7, 21
    ECALL                    ; install timer ISR vector
    LI a0, 1
    LI a7, 23
    ECALL                    ; enable interrupts

    ; Spawn Task 1 (worker counter)
    LA a0, worker_task
    LI a1, 0
    LA a2, worker_name
    LI a7, 17
    ECALL                    ; SYS_SPAWN

    ; Initialize FAT16 on virtual disk if available
    CALL fat16_init

    ; Initialize fm_cwd to "/"
    LA t0, fm_cwd
    LI t1, 47
    SB t1, 0(t0)
    SB zero, 1(t0)

    ; Apps initialize lazily when their window is first opened.
    CALL wm_init

    ; First repaint
    LA t0, need_redraw
    LI t1, 1
    SW t1, 0(t0)
    CALL os_repaint

main_loop:
    CALL events_poll         ; non-blocking event dispatch

    LA t0, need_redraw
    LW t1, 0(t0)
    BEQ t1, zero, main_no_paint
    CALL os_repaint
    LA t0, need_redraw
    SW zero, 0(t0)

main_no_paint:
    LA t0, exit_requested
    LW t1, 0(t0)
    BNE t1, zero, do_exit

    LI a0, 2
    LI a7, 19
    ECALL                    ; SYS_SLEEP 2 ms (yields CPU to background worker)
    J main_loop

do_exit:
    ; Leave an explicit terminal screen before halting. The hosted backend
    ; exits on EBREAK; the bare-metal wrapper flushes this frame, requests
    ; QEMU power-off, and otherwise remains in a documented halted state.
    CALL wm_reset_context
    LI a0, 0
    LI a1, 0
    LI a2, 800
    LI a3, 600
    LI a4, 0xFF0F172A
    LI a7, 14
    ECALL
    LI a0, 248
    LI a1, 276
    LA a2, str_system_halted
    LI a3, 0xFFFFFFFF
    LI a4, 0
    LI a7, 15
    ECALL
    LI a7, 12
    ECALL
    EBREAK                   ; Defined terminal condition

; ------------------------------------------------------------------------------
; Timer ISR: bump monotonic interrupt counter
; ------------------------------------------------------------------------------
timer_isr:
    ADDI sp, sp, -16
    SD t0, 0(sp)
    SD t1, 8(sp)
    LA t0, isr_ticks
    LD t1, 0(t0)
    ADDI t1, t1, 1
    SD t1, 0(t0)
    LD t1, 8(sp)
    LD t0, 0(sp)
    ADDI sp, sp, 16
    IRET

; ------------------------------------------------------------------------------
; Background Worker Task: increments worker_ticks for live OS clock
; ------------------------------------------------------------------------------
worker_task:
    LI a0, 50
    LI a7, 19
    ECALL                    ; SYS_SLEEP 50 ms
    LA t0, worker_ticks
    LD t1, 0(t0)
    ADDI t1, t1, 1
    SD t1, 0(t0)
    J worker_task

; ------------------------------------------------------------------------------
; Repaint Dispatcher: full back-to-front composition. Full repainting is
; intentional: moving/closing/minimizing a window cannot leave stale pixels.
; ------------------------------------------------------------------------------
os_repaint:
    ADDI sp, sp, -32
    SD ra, 24(sp)
    SD s0, 16(sp)
    SD s1, 8(sp)

    CALL desktop_draw

    LI s0, 0
    LA t0, win_z_count
    LW s1, 0(t0)
rep_window_loop:
    BGE s0, s1, rep_windows_done
    LA t0, win_z_order
    ADD t0, t0, s0
    LBU a0, 0(t0)
    CALL wm_draw_window
    ADDI s0, s0, 1
    J rep_window_loop

rep_windows_done:
    CALL wm_reset_context
    CALL desktop_draw_taskbar

check_start_menu_draw:
    LA t0, start_menu_open
    LW t1, 0(t0)
    BEQ t1, zero, check_dlg_draw
    CALL desktop_draw_start_menu

check_dlg_draw:
    LA t0, dlg_active
    LW t1, 0(t0)
    BEQ t1, zero, rep_flush
    CALL dlg_draw

rep_flush:
    LI a7, 12
    ECALL                    ; SYS_GUI_FLUSH
    LD s1, 8(sp)
    LD s0, 16(sp)
    LD ra, 24(sp)
    ADDI sp, sp, 32
    RET

; Draw one non-minimized app through its translated/clipped context.
wm_draw_window:
    ADDI sp, sp, -16
    SD ra, 8(sp)
    SD a0, 0(sp)
    LA t0, drawing_window
    SW a0, 0(t0)
    ADDI t0, a0, -1
    SLLI t0, t0, 2
    LA t1, win_flags
    ADD t1, t1, t0
    LW t2, 0(t1)
    ANDI t3, t2, 1
    BEQ t3, zero, wdw_done
    ANDI t3, t2, 2
    BNE t3, zero, wdw_done
    LD a0, 0(sp)
    CALL wm_set_context
    LD t0, 0(sp)
    LI t1, 1
    BEQ t0, t1, wdw_calc
    LI t1, 2
    BEQ t0, t1, wdw_note
    LI t1, 3
    BEQ t0, t1, wdw_files
    LI t1, 4
    BEQ t0, t1, wdw_paint
    LI t1, 5
    BEQ t0, t1, wdw_info
    LI t1, 6
    BEQ t0, t1, wdw_snake
    CALL terminal_draw
    J wdw_done
wdw_calc:
    CALL calc_draw
    J wdw_done
wdw_note:
    CALL notepad_draw
    J wdw_done
wdw_files:
    CALL fileman_draw
    J wdw_done
wdw_paint:
    CALL paint_draw
    J wdw_done
wdw_info:
    CALL sysinfo_draw
    J wdw_done
wdw_snake:
    CALL snake_draw
wdw_done:
    CALL wm_reset_context
    LD ra, 8(sp)
    ADDI sp, sp, 16
    RET

; a0=window id. Translate the app's historical fixed coordinates to the
; retained window position and clip every accelerated drawing path to it.
wm_set_context:
    ADDI t0, a0, -1
    SLLI t0, t0, 2
    LA t1, win_x
    ADD t1, t1, t0
    LW a2, 0(t1)            ; screen clip X
    LA t1, win_default_x
    ADD t1, t1, t0
    LW t2, 0(t1)
    SUB a0, a2, t2          ; translation X
    LA t1, win_y
    ADD t1, t1, t0
    LW a3, 0(t1)            ; screen clip Y
    LA t1, win_default_y
    ADD t1, t1, t0
    LW t2, 0(t1)
    SUB a1, a3, t2          ; translation Y
    LA t1, win_w
    ADD t1, t1, t0
    LW a4, 0(t1)
    LA t1, win_h
    ADD t1, t1, t0
    LW a5, 0(t1)
    LI a7, 41
    ECALL
    RET

wm_reset_context:
    LI a0, 0
    LI a1, 0
    LI a2, 0
    LI a3, 0
    LI a4, 0
    LI a5, 0
    LI a7, 41
    ECALL
    RET

; After a frame is drawn, narrow the same translation to its client area.
; This prevents app content from painting over title controls or other windows.
wm_set_client_context:
    LA t0, drawing_window
    LW t0, 0(t0)
    ADDI t0, t0, -1
    SLLI t0, t0, 2
    LA t1, win_x
    ADD t1, t1, t0
    LW a2, 0(t1)
    LA t1, win_default_x
    ADD t1, t1, t0
    LW t2, 0(t1)
    SUB a0, a2, t2
    LA t1, win_y
    ADD t1, t1, t0
    LW a3, 0(t1)
    LA t1, win_default_y
    ADD t1, t1, t0
    LW t2, 0(t1)
    SUB a1, a3, t2
    ADDI a3, a3, 32
    LA t1, win_w
    ADD t1, t1, t0
    LW a4, 0(t1)
    LA t1, win_h
    ADD t1, t1, t0
    LW a5, 0(t1)
    ADDI a5, a5, -32
    LI a7, 41
    ECALL
    RET

; ------------------------------------------------------------------------------
; Desktop Composition (800x600 TrueColor)
; ------------------------------------------------------------------------------
desktop_draw:
    ADDI sp, sp, -16
    SD ra, 8(sp)

    ; 1. Desktop background: modern smooth dark slate (0xFF1E2430)
    LI a0, 0
    LI a1, 0
    LI a2, 800
    LI a3, 600
    LI a4, 0xFF1E2430
    LI a7, 14
    ECALL                    ; fillrect desktop

    ; 2. Top Bar (Y=0..28, color 0xFF111827)
    LI a0, 0
    LI a1, 0
    LI a2, 800
    LI a3, 28
    LI a4, 0xFF111827
    LI a7, 14
    ECALL

    ; Top Bar title
    LI a0, 16
    LI a1, 6
    LA a2, str_top_title
    LI a3, 0xFFFFFFFF        ; White
    LI a4, 0x00000000        ; Transparent
    LI a7, 15
    ECALL

    ; Top Bar shortcuts hint
    LI a0, 200
    LI a1, 6
    LA a2, str_top_apps
    LI a3, 0xFF94A3B8        ; Slate gray
    LI a4, 0x00000000
    LI a5, 444                ; stop before status region at X=656
    LI a6, 16
    LI a7, 43
    ECALL

    ; Top Bar uptime
    CALL format_clock
    LI a0, 680
    LI a1, 6
    LA a2, clock_buf
    LI a3, 0xFF38BDF8        ; Cyan
    LI a4, 0x00000000
    LI a7, 15
    ECALL

    ; 3. Desktop Icons (Column 1 at X=40, Column 2 at X=170)
    ; Icon 1: [1] Calc
    LI a0, 40
    LI a1, 50
    LI a2, 110
    LI a3, 54
    LI a4, 0xFF2563EB        ; Royal blue
    LI a7, 14
    ECALL
    LI a0, 52
    LI a1, 68
    LA a2, str_ico_calc
    LI a3, 0xFFFFFFFF
    LI a4, 0x00000000
    LI a7, 15
    ECALL

    ; Icon 2: [2] Notes
    LI a0, 40
    LI a1, 120
    LI a2, 110
    LI a3, 54
    LI a4, 0xFF059669        ; Emerald green
    LI a7, 14
    ECALL
    LI a0, 50
    LI a1, 138
    LA a2, str_ico_note
    LI a3, 0xFFFFFFFF
    LI a4, 0x00000000
    LI a7, 15
    ECALL

    ; Icon 3: [3] Files
    LI a0, 40
    LI a1, 190
    LI a2, 110
    LI a3, 54
    LI a4, 0xFFD97706        ; Amber
    LI a7, 14
    ECALL
    LI a0, 50
    LI a1, 208
    LA a2, str_ico_disk
    LI a3, 0xFFFFFFFF
    LI a4, 0x00000000
    LI a7, 15
    ECALL

    ; Icon 4: [4] Paint
    LI a0, 40
    LI a1, 260
    LI a2, 110
    LI a3, 54
    LI a4, 0xFFDB2777        ; Pink
    LI a7, 14
    ECALL
    LI a0, 52
    LI a1, 278
    LA a2, str_ico_paint
    LI a3, 0xFFFFFFFF
    LI a4, 0x00000000
    LI a7, 15
    ECALL

    ; Icon 5: [5] Info
    LI a0, 40
    LI a1, 330
    LI a2, 110
    LI a3, 54
    LI a4, 0xFF7C3AED        ; Purple
    LI a7, 14
    ECALL
    LI a0, 52
    LI a1, 348
    LA a2, str_ico_info
    LI a3, 0xFFFFFFFF
    LI a4, 0x00000000
    LI a7, 15
    ECALL

    ; Icon 6: [6] Snake
    LI a0, 170
    LI a1, 50
    LI a2, 110
    LI a3, 54
    LI a4, 0xFF0D9488        ; Teal
    LI a7, 14
    ECALL
    LI a0, 180
    LI a1, 68
    LA a2, str_ico_snake
    LI a3, 0xFFFFFFFF
    LI a4, 0x00000000
    LI a7, 15
    ECALL

    ; Icon 7: [7] Term
    LI a0, 170
    LI a1, 120
    LI a2, 110
    LI a3, 54
    LI a4, 0xFF4F46E5        ; Indigo
    LI a7, 14
    ECALL
    LI a0, 180
    LI a1, 138
    LA a2, str_ico_term
    LI a3, 0xFFFFFFFF
    LI a4, 0x00000000
    LI a7, 15
    ECALL

    LD ra, 8(sp)
    ADDI sp, sp, 16
    RET

; Taskbar is composed last, so no window can cover it. Seven fixed 76-pixel
; slots fit between Start and the 136-pixel status area without overlap.
desktop_draw_taskbar:
    ADDI sp, sp, -56
    SD ra, 48(sp)
    SD s0, 40(sp)
    SD s1, 32(sp)
    SD s2, 24(sp)
    SD s3, 16(sp)
    LI a0, 0
    LI a1, 568
    LI a2, 800
    LI a3, 32
    LI a4, 0xFF0F172A
    LI a7, 14
    ECALL
    LI a0, 8
    LI a1, 572
    LI a2, 84
    LI a3, 24
    LI a4, 0xFF10B981
    LI a7, 14
    ECALL
    LI a0, 20
    LI a1, 576
    LA a2, str_start_btn
    LI a3, 0xFFFFFFFF
    LI a4, 0
    LI a7, 15
    ECALL
    ; Compact taskbar: only open apps occupy buttons, packed from x=100.
    ; taskbar_map[slot] -> window id; taskbar_count -> visible buttons.
    LI s0, 1
    LI s1, 100
    LI s3, 0
dtb_loop:
    LI t0, 8
    BGE s0, t0, dtb_map_done
    ADDI t0, s0, -1
    SLLI t0, t0, 2
    LA t1, win_flags
    ADD t1, t1, t0
    LW t2, 0(t1)
    ANDI t2, t2, 1
    BEQ t2, zero, dtb_next
    LA t0, taskbar_map
    ADD t0, t0, s3
    SB s0, 0(t0)
    ADDI s3, s3, 1
    LA t0, active_window
    LW t1, 0(t0)
    LI s2, 0xFF334155
    BNE t1, s0, dtb_color
    LI s2, 0xFF2563EB
dtb_color:
    MV a0, s1
    LI a1, 572
    LI a2, 74
    LI a3, 24
    MV a4, s2
    LI a7, 14
    ECALL
    ADDI a0, s1, 6
    LI a1, 576
    MV a2, s0
    CALL get_win_short_name
    MV a2, a0
    ADDI a0, s1, 6
    LI a1, 576
    LI a3, 0xFFFFFFFF
    LI a4, 0
    LI a5, 62
    LI a6, 16
    LI a7, 43
    ECALL
    ADDI s1, s1, 78
dtb_next:
    ADDI s0, s0, 1
    J dtb_loop
dtb_map_done:
    LA t0, taskbar_count
    SW s3, 0(t0)
dtb_clock:
    CALL format_clock
    LI a0, 660
    LI a1, 576
    LA a2, clock_buf
    LI a3, 0xFF38BDF8
    LI a4, 0
    LI a5, 132
    LI a6, 16
    LI a7, 43
    ECALL
    LD s3, 16(sp)
    LD s2, 24(sp)
    LD s1, 32(sp)
    LD s0, 40(sp)
    LD ra, 48(sp)
    ADDI sp, sp, 56
    RET

; ------------------------------------------------------------------------------
; Start Menu Popup (X=8, Y=250, W=220, H=314)
; ------------------------------------------------------------------------------
desktop_draw_start_menu:
    ADDI sp, sp, -16
    SD ra, 8(sp)

    ; Shadow
    LI a0, 14
    LI a1, 256
    LI a2, 220
    LI a3, 312
    LI a4, 0xFF0A0E17
    LI a7, 14
    ECALL

    ; Menu Body
    LI a0, 8
    LI a1, 250
    LI a2, 220
    LI a3, 314
    LI a4, 0xFF1E222D
    LI a7, 14
    ECALL

    ; Header
    LI a0, 8
    LI a1, 250
    LI a2, 220
    LI a3, 28
    LI a4, 0xFF2563EB
    LI a7, 14
    ECALL

    LI a0, 16
    LI a1, 256
    LA a2, str_sm_title
    LI a3, 0xFFFFFFFF
    LI a4, 0x00000000
    LI a7, 15
    ECALL

    ; Menu Items
    LI a0, 18
    LI a1, 286
    LA a2, str_sm_i1
    LI a3, 0xFFF1F5F9
    LI a4, 0x00000000
    LI a7, 15
    ECALL

    LI a0, 18
    LI a1, 314
    LA a2, str_sm_i2
    LI a7, 15
    ECALL

    LI a0, 18
    LI a1, 342
    LA a2, str_sm_i3
    LI a7, 15
    ECALL

    LI a0, 18
    LI a1, 370
    LA a2, str_sm_i4
    LI a7, 15
    ECALL

    LI a0, 18
    LI a1, 398
    LA a2, str_sm_i5
    LI a7, 15
    ECALL

    LI a0, 18
    LI a1, 426
    LA a2, str_sm_i6
    LI a7, 15
    ECALL

    LI a0, 18
    LI a1, 454
    LA a2, str_sm_i7
    LI a7, 15
    ECALL

    ; Separator
    LI a0, 16
    LI a1, 486
    LI a2, 204
    LI a3, 1
    LI a4, 0xFF334155
    LI a7, 14
    ECALL

    ; Exit OS
    LI a0, 18
    LI a1, 510
    LA a2, str_sm_exit
    LI a3, 0xFFFF3B30        ; Red
    LI a4, 0x00000000
    LI a7, 15
    ECALL

    LD ra, 8(sp)
    ADDI sp, sp, 16
    RET

; ------------------------------------------------------------------------------
; Window Frame Helper: draw shadow, title bar, [X] button, outer border
; a0=X, a1=Y, a2=W, a3=H, a4=title_str
; ------------------------------------------------------------------------------
draw_window_frame:
    ADDI sp, sp, -48
    SD ra, 40(sp)
    SD s0, 32(sp)
    SD s1, 24(sp)
    SD s2, 16(sp)
    SD s3, 8(sp)
    SD s4, 0(sp)

    MV s0, a0                ; X
    MV s1, a1                ; Y
    MV s2, a2                ; W
    MV s3, a3                ; H
    MV s4, a4                ; title

    ; Store current window dimensions for hit testing
    LA t0, cur_win_x
    SW s0, 0(t0)
    LA t0, cur_win_y
    SW s1, 0(t0)
    LA t0, cur_win_w
    SW s2, 0(t0)
    LA t0, cur_win_h
    SW s3, 0(t0)

    ; 1. Window Drop Shadow (+6, +6)
    ADDI a0, s0, 6
    ADDI a1, s1, 6
    MV a2, s2
    MV a3, s3
    LI a4, 0xFF0A0E17
    LI a7, 14
    ECALL

    ; 2. Window Body background (0xFF1E222D)
    MV a0, s0
    MV a1, s1
    MV a2, s2
    MV a3, s3
    LI a4, 0xFF1E222D
    LI a7, 14
    ECALL

    ; 3. Outer border (1px 0xFF334155)
    MV a0, s0
    MV a1, s1
    MV a2, s2
    LI a3, 1
    LI a4, 0xFF334155
    LI a7, 14
    ECALL

    ; 4. Title Bar (H=32, 0xFF2563EB)
    MV a0, s0
    MV a1, s1
    MV a2, s2
    LI a3, 32
    LI a4, 0xFF2563EB
    LI a7, 14
    ECALL

    ; Title text: reserve 64 pixels for minimize/close controls and fit UTF-8
    ADDI a0, s0, 12
    ADDI a1, s1, 8
    MV a2, s4
    LI a3, 0xFFFFFFFF
    LI a4, 0x00000000
    ADDI a5, s2, -88
    LI a6, 16
    LI a7, 43
    ECALL

    ; Minimize button
    ADD a0, s0, s2
    ADDI a0, a0, -52
    ADDI a1, s1, 6
    LI a2, 20
    LI a3, 20
    LI a4, 0xFF475569
    LI a7, 14
    ECALL
    ADD a0, s0, s2
    ADDI a0, a0, -46
    ADDI a1, s1, 8
    LA a2, str_minimize
    LI a3, 0xFFFFFFFF
    LI a4, 0
    LI a7, 15
    ECALL

    ; Close [X] button (X = s0 + s2 - 28, Y = s1 + 6, W=20, H=20)
    ADD a0, s0, s2
    ADDI a0, a0, -28
    ADDI a1, s1, 6
    LI a2, 20
    LI a3, 20
    LI a4, 0xFFEF4444        ; Red
    LI a7, 14
    ECALL

    ; Letter 'X'
    ADD a0, s0, s2
    ADDI a0, a0, -22
    ADDI a1, s1, 8
    LA a2, str_close_x
    LI a3, 0xFFFFFFFF
    LI a4, 0x00000000
    LI a7, 15
    ECALL

    CALL wm_set_client_context

    LD s4, 0(sp)
    LD s3, 8(sp)
    LD s2, 16(sp)
    LD s1, 24(sp)
    LD s0, 32(sp)
    LD ra, 40(sp)
    ADDI sp, sp, 48
    RET

; ------------------------------------------------------------------------------
; format_clock: host wall-clock source as Unix UTC seconds. Bare-metal platforms
; without an RTC return zero and explicitly display "RTC N/A".
; ------------------------------------------------------------------------------
format_clock:
    ADDI sp, sp, -16
    SD ra, 8(sp)
    LI a7, 29                ; SYS_RTC_GET
    ECALL
    BEQ a0, zero, fc_no_rtc
    LA a1, num_tmp
    CALL num_to_dec
    LA t0, clock_buf
    LI t1, 85                ; U
    SB t1, 0(t0)
    LI t1, 84                ; T
    SB t1, 1(t0)
    LI t1, 67                ; C
    SB t1, 2(t0)
    LI t1, 32
    SB t1, 3(t0)
    ADDI a0, t0, 4
    LA a1, num_tmp
    CALL str_copy
    J fc_done
fc_no_rtc:
    LA a0, clock_buf
    LA a1, str_rtc_unavailable
    CALL str_copy
fc_done:
    LD ra, 8(sp)
    ADDI sp, sp, 16
    RET

get_active_win_name:
    LA t0, active_window
    LW t1, 0(t0)
    LI t2, 1
    BEQ t1, t2, gwn_1
    LI t2, 2
    BEQ t1, t2, gwn_2
    LI t2, 3
    BEQ t1, t2, gwn_3
    LI t2, 4
    BEQ t1, t2, gwn_4
    LI t2, 5
    BEQ t1, t2, gwn_5
    LI t2, 6
    BEQ t1, t2, gwn_6
    LI t2, 7
    BEQ t1, t2, gwn_7
    LA a0, str_wname_desk
    RET
gwn_1:
    LA a0, str_wname_calc
    RET
gwn_2:
    LA a0, str_wname_note
    RET
gwn_3:
    LA a0, str_wname_file
    RET
gwn_4:
    LA a0, str_wname_paint
    RET
gwn_5:
    LA a0, str_wname_info
    RET
gwn_6:
    LA a0, str_wname_snake
    RET
gwn_7:
    LA a0, str_wname_term
    RET

get_win_short_name:
    LI t0, 1
    BEQ a2, t0, gws_1
    LI t0, 2
    BEQ a2, t0, gws_2
    LI t0, 3
    BEQ a2, t0, gws_3
    LI t0, 4
    BEQ a2, t0, gws_4
    LI t0, 5
    BEQ a2, t0, gws_5
    LI t0, 6
    BEQ a2, t0, gws_6
    LA a0, str_short_term
    RET
gws_1:
    LA a0, str_short_calc
    RET
gws_2:
    LA a0, str_short_note
    RET
gws_3:
    LA a0, str_short_files
    RET
gws_4:
    LA a0, str_short_paint
    RET
gws_5:
    LA a0, str_short_info
    RET
gws_6:
    LA a0, str_short_snake
    RET

str_copy:
sc_loop:
    LBU t0, 0(a1)
    SB t0, 0(a0)
    BEQ t0, zero, sc_done
    ADDI a0, a0, 1
    ADDI a1, a1, 1
    J sc_loop
sc_done:
    RET

; ------------------------------------------------------------------------------
; Event Polling & Dispatching
; ------------------------------------------------------------------------------
events_poll:
    ADDI sp, sp, -48
    SD ra, 40(sp)
    SD s0, 32(sp)
    SD s1, 24(sp)
    SD s2, 16(sp)
    SD s3, 8(sp)

    LI s3, 16                ; Max 16 events per poll cycle to prevent starvation
ev_poll_loop:
    BEQ s3, zero, ev_done
    ADDI s3, s3, -1

    LI a7, 11
    ECALL                    ; SYS_GUI_POLL_EVENT
    ; a0 = type, a1 = X/code, a2 = Y/data, a3 = button
    BEQ a0, zero, ev_done

    LI t0, 1
    BEQ a0, t0, ev_key
    LI t0, 2
    BEQ a0, t0, ev_click
    LI t0, 3
    BEQ a0, t0, ev_move
    LI t0, 4
    BEQ a0, t0, ev_timer
    LI t0, 5
    BEQ a0, t0, ev_release
    J ev_next

ev_key:
    ; a1 = keycode
    LA t0, dlg_active
    LW t0, 0(t0)
    BEQ t0, zero, ev_key_not_dlg
    CALL dlg_on_key
    J ev_next
ev_key_not_dlg:
    ; Alt+Tab is global and is consumed before any application sees Tab.
    LI t0, 9
    BNE a1, t0, ev_key_not_alt_tab
    ANDI t0, a4, 4
    BEQ t0, zero, ev_key_not_alt_tab
    CALL wm_alt_tab
    J ev_next
ev_key_not_alt_tab:
    LI t0, 260               ; KEY_F1 (toggle start menu)
    BEQ a1, t0, toggle_menu
    LI t0, 261
    BEQ a1, t0, sm_open_2
    LI t0, 262
    BEQ a1, t0, sm_open_3
    LI t0, 263
    BEQ a1, t0, sm_open_4
    LI t0, 264
    BEQ a1, t0, sm_open_5
    LI t0, 265
    BEQ a1, t0, sm_open_6
    LI t0, 266
    BEQ a1, t0, sm_open_7
    LI t0, 1                 ; ASCII 1 / Ctrl-A / injected '\1'
    BEQ a1, t0, toggle_menu
    LI t0, 27                ; KEY_ESC
    BEQ a1, t0, on_esc

    ; If Start Menu is open:
    LA t0, start_menu_open
    LW t2, 0(t0)
    BEQ t2, zero, ev_key_app_or_desk

    LI t0, 49                ; '1'
    BEQ a1, t0, sm_open_1
    LI t0, 50                ; '2'
    BEQ a1, t0, sm_open_2
    LI t0, 51                ; '3'
    BEQ a1, t0, sm_open_3
    LI t0, 52                ; '4'
    BEQ a1, t0, sm_open_4
    LI t0, 53                ; '5'
    BEQ a1, t0, sm_open_5
    LI t0, 54                ; '6'
    BEQ a1, t0, sm_open_6
    LI t0, 55                ; '7'
    BEQ a1, t0, sm_open_7
    LI t0, 120               ; 'x'
    BEQ a1, t0, do_quit_os
    LI t0, 88                ; 'X'
    BEQ a1, t0, do_quit_os
    ; Any other key closes start menu
    LA t0, start_menu_open
    SW zero, 0(t0)
    CALL flag_redraw
    J ev_next

ev_key_app_or_desk:
    LA t0, active_window
    LW t2, 0(t0)
    BNE t2, zero, route_key_to_app

    ; Desktop shortcut numbers 1..7
    LI t0, 49
    BEQ a1, t0, sm_open_1
    LI t0, 50
    BEQ a1, t0, sm_open_2
    LI t0, 51
    BEQ a1, t0, sm_open_3
    LI t0, 52
    BEQ a1, t0, sm_open_4
    LI t0, 53
    BEQ a1, t0, sm_open_5
    LI t0, 54
    BEQ a1, t0, sm_open_6
    LI t0, 55
    BEQ a1, t0, sm_open_7
    J ev_next

route_key_to_app:
    LI t3, 1
    BEQ t2, t3, k_calc
    LI t3, 2
    BEQ t2, t3, k_notes
    LI t3, 3
    BEQ t2, t3, k_files
    LI t3, 4
    BEQ t2, t3, k_paint
    LI t3, 5
    BEQ t2, t3, k_info
    LI t3, 6
    BEQ t2, t3, k_snake
    LI t3, 7
    BEQ t2, t3, k_term
    J ev_next

k_calc:
    CALL calc_on_key
    J ev_next
k_notes:
    CALL notepad_on_key
    J ev_next
k_files:
    CALL fileman_on_key
    J ev_next
k_paint:
    CALL paint_on_key
    J ev_next
k_info:
    CALL sysinfo_on_key
    J ev_next
k_snake:
    CALL snake_on_key
    J ev_next
k_term:
    CALL terminal_on_key
    J ev_next

toggle_menu:
    LA t0, start_menu_open
    LW t1, 0(t0)
    XORI t1, t1, 1
    SW t1, 0(t0)
    CALL flag_redraw
    J ev_next

on_esc:
    LA t0, start_menu_open
    LW t1, 0(t0)
    BEQ t1, zero, esc_win
    SW zero, 0(t0)
    CALL flag_redraw
    J ev_next
esc_win:
    LA t0, active_window
    LW t1, 0(t0)
    BEQ t1, zero, ev_next
    LI t2, 2
    BNE t1, t2, esc_win_close
    CALL notepad_request_close
    BNE a0, zero, ev_next
esc_win_close:
    CALL window_close
    J ev_next

sm_open_1:
    LI a0, 1
    CALL window_open
    J ev_next
sm_open_2:
    LI a0, 2
    CALL window_open
    J ev_next
sm_open_3:
    LI a0, 3
    CALL window_open
    J ev_next
sm_open_4:
    LI a0, 4
    CALL window_open
    J ev_next
sm_open_5:
    LI a0, 5
    CALL window_open
    J ev_next
sm_open_6:
    LI a0, 6
    CALL window_open
    J ev_next
sm_open_7:
    LI a0, 7
    CALL window_open
    J ev_next

do_quit_os:
    LA t0, start_menu_open
    SW zero, 0(t0)
    CALL notepad_request_shutdown
    J ev_done

ev_click:
    ; a1 = X, a2 = Y, a3 = button
    MV s0, a1                ; X
    MV s1, a2                ; Y
    MV s2, a3                ; button

    ; UI click tone (freq=800, dur=20, wave=0, vol=160)
    LI a0, 800
    LI a1, 20
    LI a2, 0
    LI a3, 160
    CALL sound_play_tone

    LA t0, dlg_active
    LW t0, 0(t0)
    BEQ t0, zero, ev_click_not_dlg
    MV a0, s0
    MV a1, s1
    MV a2, s2
    CALL dlg_on_click
    J ev_next
ev_click_not_dlg:

    ; 1. Check Taskbar click (Y >= 568)
    LI t0, 568
    BLT s1, t0, chk_sm_click
    ; Start button: X in 8..92
    LI t0, 8
    BLT s0, t0, ev_next
    LI t0, 92
    BGT s0, t0, chk_taskbar_tab
    J toggle_menu

chk_taskbar_tab:
    ; Compacted buttons: walk taskbar_map slots from x=100, stride 78.
    ; Clicks in the 4px inter-button gap or past the last button do nothing.
    LI t0, 100
    BLT s0, t0, ev_next
    LA t0, taskbar_count
    LW t1, 0(t0)
    LI t2, 0
    LI t3, 100
ctt_loop:
    BGE t2, t1, ev_next
    BLT s0, t3, ev_next
    ADDI t4, t3, 74
    BLT s0, t4, ctt_hit
    ADDI t2, t2, 1
    ADDI t3, t3, 78
    J ctt_loop
ctt_hit:
    LA t4, taskbar_map
    ADD t4, t4, t2
    LBU t0, 0(t4)
    LI t1, 1
    BLT t0, t1, ev_next
    LI t1, 7
    BGT t0, t1, ev_next
    ADDI t1, t0, -1
    SLLI t1, t1, 2
    LA t2, win_flags
    ADD t2, t2, t1
    LW t3, 0(t2)
    ANDI t3, t3, 1
    BEQ t3, zero, ev_next
    LA t1, active_window
    LW t2, 0(t1)
    BNE t2, t0, tb_focus
    CALL window_minimize
    J ev_next
tb_focus:
    MV a0, t0
    CALL window_open
    J ev_next

chk_sm_click:
    ; 2. If start menu open, check click inside start menu (X: 8..228, Y: 250..564)
    LA t0, start_menu_open
    LW t1, 0(t0)
    BEQ t1, zero, chk_win_click
    LI t0, 8
    BLT s0, t0, close_sm_anyway
    LI t0, 228
    BGT s0, t0, close_sm_anyway
    LI t0, 250
    BLT s1, t0, close_sm_anyway
    LI t0, 564
    BGT s1, t0, close_sm_anyway

    ; Inside start menu:
    LI t0, 280
    BLT s1, t0, ev_next
    LI t0, 310
    BLT s1, t0, sm_open_1
    LI t0, 340
    BLT s1, t0, sm_open_2
    LI t0, 370
    BLT s1, t0, sm_open_3
    LI t0, 400
    BLT s1, t0, sm_open_4
    LI t0, 430
    BLT s1, t0, sm_open_5
    LI t0, 460
    BLT s1, t0, sm_open_6
    LI t0, 490
    BLT s1, t0, sm_open_7
    LI t0, 500
    BLT s1, t0, ev_next
    J do_quit_os

close_sm_anyway:
    LA t0, start_menu_open
    SW zero, 0(t0)
    CALL flag_redraw

chk_win_click:
    ; Hit-test the visible stack from front to back. Once a window is hit,
    ; desktop icons and covered windows never receive the same click.
    LA t0, win_z_count
    LW t1, 0(t0)
    LI t2, 7
    BLE t1, t2, chw_scan
    MV t1, t2
chw_scan:
    ADDI t1, t1, -1
    BLT t1, zero, chk_desktop_icons
    LA t2, win_z_order
    ADD t2, t2, t1
    LBU t6, 0(t2)
    LI t2, 1
    BLT t6, t2, chw_scan
    LI t2, 7
    BGT t6, t2, chw_scan
    ADDI t2, t6, -1
    SLLI t2, t2, 2
    LA t3, win_flags
    ADD t3, t3, t2
    LW t4, 0(t3)
    ANDI t4, t4, 3
    LI t5, 1
    BNE t4, t5, chw_scan
    LA t3, win_x
    ADD t3, t3, t2
    LW t4, 0(t3)
    BLT s0, t4, chw_scan
    LA t3, win_w
    ADD t3, t3, t2
    LW t5, 0(t3)
    ADD t5, t5, t4
    BGE s0, t5, chw_scan
    LA t3, win_y
    ADD t3, t3, t2
    LW a5, 0(t3)
    BLT s1, a5, chw_scan
    LA t3, win_h
    ADD t3, t3, t2
    LW a6, 0(t3)
    ADD a6, a6, a5
    BGE s1, a6, chw_scan
    LA t0, hit_window
    SW t6, 0(t0)
    MV a0, t6
    CALL wm_raise
    LA t0, hit_window
    LW t1, 0(t0)
    ADDI t2, t1, -1
    SLLI t2, t2, 2
    LA t3, win_x
    ADD t3, t3, t2
    LW t4, 0(t3)
    LA t3, win_y
    ADD t3, t3, t2
    LW t5, 0(t3)
    LA t3, win_w
    ADD t3, t3, t2
    LW t6, 0(t3)
    ADD a6, t4, t6
    ; Title controls and drag capture use screen coordinates.
    ADDI a5, t5, 32
    BGE s1, a5, chw_client
    ADDI a5, a6, -28
    BGE s0, a5, chw_close
    ADDI a5, a6, -52
    BGE s0, a5, chw_minimize
    LA t0, drag_active
    LI a0, 1
    SW a0, 0(t0)
    LA t0, drag_window
    SW t1, 0(t0)
    SUB a0, s0, t4
    LA t0, drag_off_x
    SW a0, 0(t0)
    SUB a0, s1, t5
    LA t0, drag_off_y
    SW a0, 0(t0)
    CALL flag_redraw
    J ev_next
chw_close:
    CALL notepad_request_close
    BNE a0, zero, ev_next
    CALL window_close
    J ev_next
chw_minimize:
    CALL window_minimize
    J ev_next
chw_client:
    ; Convert screen coordinates back into each app's historical coordinate
    ; space. Rendering uses the exact inverse translation.
    LA t3, win_default_x
    ADD t3, t3, t2
    LW a0, 0(t3)
    SUB a3, s0, t4
    ADD a0, a0, a3
    LA t3, win_default_y
    ADD t3, t3, t2
    LW a1, 0(t3)
    SUB a3, s1, t5
    ADD a1, a1, a3
    MV a2, s2
    LI t2, 1
    BEQ t1, t2, c_calc
    LI t2, 2
    BEQ t1, t2, c_notes
    LI t2, 3
    BEQ t1, t2, c_files
    LI t2, 4
    BEQ t1, t2, c_paint
    LI t2, 5
    BEQ t1, t2, c_info
    LI t2, 6
    BEQ t1, t2, c_snake
    LI t2, 7
    BEQ t1, t2, c_term
    J ev_next

c_calc:
    CALL calc_on_click
    J ev_next
c_notes:
    CALL notepad_on_click
    J ev_next
c_files:
    CALL fileman_on_click
    J ev_next
c_paint:
    CALL paint_on_click
    J ev_next
c_info:
    CALL sysinfo_on_click
    J ev_next
c_snake:
    CALL snake_on_click
    J ev_next
c_term:
    CALL terminal_on_click
    J ev_next

chk_desktop_icons:
    ; Check Column 1 (X in 40..150)
    LI t0, 40
    BLT s0, t0, chk_col2
    LI t0, 150
    BGT s0, t0, chk_col2

    LI t0, 50
    BLT s1, t0, ev_next
    LI t0, 104
    BLE s1, t0, sm_open_1
    LI t0, 120
    BLT s1, t0, ev_next
    LI t0, 174
    BLE s1, t0, sm_open_2
    LI t0, 190
    BLT s1, t0, ev_next
    LI t0, 244
    BLE s1, t0, sm_open_3
    LI t0, 260
    BLT s1, t0, ev_next
    LI t0, 314
    BLE s1, t0, sm_open_4
    LI t0, 330
    BLT s1, t0, ev_next
    LI t0, 384
    BLE s1, t0, sm_open_5
    J ev_next

chk_col2:
    ; Check Column 2 (X in 170..280)
    LI t0, 170
    BLT s0, t0, ev_next
    LI t0, 280
    BGT s0, t0, ev_next

    LI t0, 50
    BLT s1, t0, ev_next
    LI t0, 104
    BLE s1, t0, sm_open_6
    LI t0, 120
    BLT s1, t0, ev_next
    LI t0, 174
    BLE s1, t0, sm_open_7
    J ev_next

ev_move:
    LA t0, dlg_active
    LW t0, 0(t0)
    BNE t0, zero, ev_next
    ; Pointer capture keeps dragging active until an explicit release event.
    LA t0, drag_active
    LW t1, 0(t0)
    BEQ t1, zero, ev_move_paint
    LA t0, drag_window
    LW t1, 0(t0)
    LI t2, 1
    BLT t1, t2, ev_cancel_drag
    LI t2, 7
    BGT t1, t2, ev_cancel_drag
    ADDI t2, t1, -1
    SLLI t2, t2, 2
    LA t0, win_flags
    ADD t0, t0, t2
    LW t3, 0(t0)
    ANDI t3, t3, 3
    LI t4, 1
    BNE t3, t4, ev_cancel_drag
    LA t0, drag_off_x
    LW t3, 0(t0)
    SUB t3, a1, t3
    BGE t3, zero, drag_x_nonneg
    LI t3, 0
drag_x_nonneg:
    LA t0, win_w
    ADD t0, t0, t2
    LW t4, 0(t0)
    LI t5, 800
    SUB t5, t5, t4
    BLE t3, t5, drag_x_ok
    MV t3, t5
drag_x_ok:
    LA t0, win_x
    ADD t0, t0, t2
    SW t3, 0(t0)
    LA t0, drag_off_y
    LW t3, 0(t0)
    SUB t3, a2, t3
    LI t4, 28
    BGE t3, t4, drag_y_top_ok
    MV t3, t4
drag_y_top_ok:
    LA t0, win_h
    ADD t0, t0, t2
    LW t4, 0(t0)
    LI t5, 568
    SUB t5, t5, t4
    BLE t3, t5, drag_y_ok
    MV t3, t5
drag_y_ok:
    LA t0, win_y
    ADD t0, t0, t2
    SW t3, 0(t0)
    CALL flag_redraw
    J ev_next
ev_cancel_drag:
    LA t0, drag_active
    SW zero, 0(t0)
    J ev_next
ev_move_paint:
    ; Paint receives drag only inside its own moved client/canvas mapping.
    LA t0, active_window
    LW t1, 0(t0)
    LI t2, 4
    BNE t1, t2, ev_next
    LI t2, 1
    BNE a3, t2, ev_next
    LI t2, 3
    SLLI t2, t2, 2
    LA t0, win_x
    ADD t0, t0, t2
    LW t3, 0(t0)
    LA t0, win_y
    ADD t0, t0, t2
    LW t4, 0(t0)
    LA t0, win_w
    ADD t0, t0, t2
    LW t5, 0(t0)
    BLT a1, t3, ev_next
    ADD t5, t5, t3
    BGE a1, t5, ev_next
    LA t0, win_h
    ADD t0, t0, t2
    LW t5, 0(t0)
    BLT a2, t4, ev_next
    ADD t5, t5, t4
    BGE a2, t5, ev_next
    SUB a0, a1, t3
    LI t5, 60
    ADD a0, a0, t5
    SUB a1, a2, t4
    LI t5, 35
    ADD a1, a1, t5
    CALL paint_on_drag
    J ev_next

ev_release:
    LA t0, drag_active
    SW zero, 0(t0)
    J ev_next

ev_timer:
    ; Host timer (50 ms): advance clock and snake
    CALL flag_redraw
    LA t0, win_flags
    LW t1, 20(t0)
    ANDI t1, t1, 1
    BEQ t1, zero, ev_next
t_snake:
    CALL snake_on_timer
    J ev_next

ev_next:
    J ev_done

ev_done:
    LD s3, 8(sp)
    LD s2, 16(sp)
    LD s1, 24(sp)
    LD s0, 32(sp)
    LD ra, 40(sp)
    ADDI sp, sp, 48
    RET

wm_init:
    LA t0, win_z_count
    SW zero, 0(t0)
    LA t0, active_window
    SW zero, 0(t0)
    LA t0, drag_active
    SW zero, 0(t0)
    RET

; Move a0 to the top, adding it when first opened.
wm_raise:
    ADDI sp, sp, -32
    SD s0, 24(sp)
    SD s1, 16(sp)
    MV s0, a0
    LI t0, 1
    BLT s0, t0, wmr_invalid
    LI t0, 7
    BGT s0, t0, wmr_invalid
    LA t0, win_z_count
    LW t1, 0(t0)
    LI t2, 7
    BLE t1, t2, wmr_count_ok
    MV t1, t2
wmr_count_ok:
    LI t2, 0
    LI t3, 0
    LA t4, win_z_order
wmr_scan:
    BGE t2, t1, wmr_append
    ADD t5, t4, t2
    LBU t6, 0(t5)
    BEQ t6, s0, wmr_found
    ADD t5, t4, t3
    SB t6, 0(t5)
    ADDI t3, t3, 1
    J wmr_step
wmr_found:
    LI s1, 1
wmr_step:
    ADDI t2, t2, 1
    J wmr_scan
wmr_append:
    ADD t5, t4, t3
    SB s0, 0(t5)
    ADDI t3, t3, 1
    SW t3, 0(t0)
    LA t0, active_window
    SW s0, 0(t0)
wmr_invalid:
    LD s1, 16(sp)
    LD s0, 24(sp)
    ADDI sp, sp, 32
    RET

window_open:
    ADDI sp, sp, -32
    SD ra, 24(sp)
    SD s0, 16(sp)
    MV s0, a0
    LI t0, 1
    BLT s0, t0, wo_done
    LI t0, 7
    BGT s0, t0, wo_done
    ADDI t0, s0, -1
    SLLI t0, t0, 2
    LA t1, win_flags
    ADD t1, t1, t0
    LW t2, 0(t1)
    ANDI t3, t2, 4
    BNE t3, zero, wo_initialized
    ORI t2, t2, 4
    SW t2, 0(t1)
    LI t3, 1
    BEQ s0, t3, wo_init_calc
    LI t3, 2
    BEQ s0, t3, wo_init_note
    LI t3, 4
    BEQ s0, t3, wo_init_paint
    LI t3, 6
    BEQ s0, t3, wo_init_snake
    LI t3, 7
    BEQ s0, t3, wo_init_term
    J wo_initialized
wo_init_calc:
    CALL calc_init
    J wo_initialized
wo_init_note:
    CALL notepad_init
    J wo_initialized
wo_init_paint:
    CALL paint_init
    J wo_initialized
wo_init_snake:
    CALL snake_init
    J wo_initialized
wo_init_term:
    CALL terminal_init
wo_initialized:
    ADDI t0, s0, -1
    SLLI t0, t0, 2
    LA t1, win_flags
    ADD t1, t1, t0
    LW t2, 0(t1)
    ORI t2, t2, 1
    ANDI t2, t2, -3
    SW t2, 0(t1)
    MV a0, s0
    CALL wm_raise
    LA t0, start_menu_open
    SW zero, 0(t0)
    CALL flag_redraw
    LI a0, 1000
    LI a1, 30
    LI a2, 0
    LI a3, 140
    CALL sound_play_tone
wo_done:
    LD s0, 16(sp)
    LD ra, 24(sp)
    ADDI sp, sp, 32
    RET

; Choose the highest non-minimized open window.
wm_activate_top:
    LA t0, win_z_count
    LW t1, 0(t0)
    LI t2, 7
    BLE t1, t2, wmat_loop
    MV t1, t2
wmat_loop:
    ADDI t1, t1, -1
    BLT t1, zero, wmat_none
    LA t2, win_z_order
    ADD t2, t2, t1
    LBU t3, 0(t2)
    LI t4, 1
    BLT t3, t4, wmat_loop
    LI t4, 7
    BGT t3, t4, wmat_loop
    ADDI t4, t3, -1
    SLLI t4, t4, 2
    LA t5, win_flags
    ADD t5, t5, t4
    LW t6, 0(t5)
    ANDI t4, t6, 3
    LI t5, 1
    BNE t4, t5, wmat_loop
    LA t0, active_window
    SW t3, 0(t0)
    RET

; Select the next window in reverse stacking order, including minimized ones.
wm_alt_tab:
    ADDI sp, sp, -32
    SD ra, 24(sp)
    SD s0, 16(sp)
    SD s1, 8(sp)
    LA t0, win_z_count
    LW t1, 0(t0)
    BEQ t1, zero, walt_done
    LA t2, active_window
    LW s0, 0(t2)
    LI s1, 7                 ; stale lists/IDs can never create an endless scan
    LI t3, 1
    BLT s0, t3, walt_wrap
    LI t3, 7
    BGT s0, t3, walt_wrap
    BEQ s0, zero, walt_wrap
walt_scan:
    ADDI s1, s1, -1
    BLT s1, zero, walt_none
    ADDI s0, s0, -1
    BNE s0, zero, walt_check
walt_wrap:
    LI s0, 7
walt_check:
    ADDI t4, s0, -1
    SLLI t4, t4, 2
    LA t5, win_flags
    ADD t5, t5, t4
    LW t6, 0(t5)
    ANDI t3, t6, 1
    BEQ t3, zero, walt_scan
    ANDI t6, t6, -3
    SW t6, 0(t5)
    MV a0, s0
    CALL wm_raise
    CALL flag_redraw
    J walt_done
walt_none:
    LA t0, active_window
    SW zero, 0(t0)
    CALL flag_redraw
walt_done:
    LD s1, 8(sp)
    LD s0, 16(sp)
    LD ra, 24(sp)
    ADDI sp, sp, 32
    RET
wmat_none:
    LA t0, active_window
    SW zero, 0(t0)
    RET

window_minimize:
    ADDI sp, sp, -16
    SD ra, 8(sp)
    LA t0, active_window
    LW t1, 0(t0)
    BEQ t1, zero, wmin_done
    LI t0, 1
    BLT t1, t0, wmin_clear_active
    LI t0, 7
    BGT t1, t0, wmin_clear_active
    ADDI t2, t1, -1
    SLLI t2, t2, 2
    LA t3, win_flags
    ADD t3, t3, t2
    LW t4, 0(t3)
    ORI t4, t4, 2
    SW t4, 0(t3)
    LA t0, drag_active
    SW zero, 0(t0)
    CALL wm_activate_top
    CALL flag_redraw
    J wmin_done
wmin_clear_active:
    LA t0, active_window
    SW zero, 0(t0)
    CALL flag_redraw
wmin_done:
    LD ra, 8(sp)
    ADDI sp, sp, 16
    RET

window_close:
    ADDI sp, sp, -16
    SD ra, 8(sp)
    LA t0, active_window
    LW t6, 0(t0)
    BEQ t6, zero, wc_done
    LI t0, 1
    BLT t6, t0, wc_clear_active
    LI t0, 7
    BGT t6, t0, wc_clear_active
    ADDI t1, t6, -1
    SLLI t1, t1, 2
    LA t2, win_flags
    ADD t2, t2, t1
    LW t3, 0(t2)
    ANDI t3, t3, -4
    SW t3, 0(t2)
    LA t0, win_z_count
    LW t1, 0(t0)
    LI t2, 0
    LI t3, 0
    LA t4, win_z_order
wc_scan:
    BGE t2, t1, wc_compact_done
    ADD t5, t4, t2
    LBU a0, 0(t5)
    BEQ a0, t6, wc_skip
    ADD t5, t4, t3
    SB a0, 0(t5)
    ADDI t3, t3, 1
wc_skip:
    ADDI t2, t2, 1
    J wc_scan
wc_compact_done:
    SW t3, 0(t0)
    LA t0, drag_active
    SW zero, 0(t0)
    CALL wm_activate_top
    CALL flag_redraw
    J wc_done
wc_clear_active:
    LA t0, active_window
    SW zero, 0(t0)
    LA t0, drag_active
    SW zero, 0(t0)
    CALL flag_redraw
wc_done:
    LD ra, 8(sp)
    ADDI sp, sp, 16
    RET

flag_redraw:
    LA t0, need_redraw
    LI t1, 1
    SW t1, 0(t0)
    RET

; ==============================================================================
; Application 1: Calculator (64-bit TrueColor)
; ==============================================================================
calc_init:
    LA t0, calc_acc
    SD zero, 0(t0)
    LA t0, calc_cur
    SD zero, 0(t0)
    LA t0, calc_op
    SW zero, 0(t0)
    LA t0, calc_buf
    LI t1, 48                ; '0'
    SB t1, 0(t0)
    SB zero, 1(t0)
    RET

calc_draw:
    ADDI sp, sp, -16
    SD ra, 8(sp)

    ; Draw window frame (X=240, Y=70, W=320, H=440)
    LI a0, 240
    LI a1, 70
    LI a2, 320
    LI a3, 440
    LA a4, str_calc_title
    CALL draw_window_frame

    ; Display screen (X=256, Y=116, W=288, H=64)
    LI a0, 256
    LI a1, 116
    LI a2, 288
    LI a3, 64
    LI a4, 0xFF0B0F19
    LI a7, 14
    ECALL

    ; Border around display screen
    LI a0, 256
    LI a1, 116
    LI a2, 288
    LI a3, 1
    LI a4, 0xFF334155
    LI a7, 14
    ECALL

    ; Format current value or buffer
    LI a0, 268
    LI a1, 138
    LA a2, calc_buf
    LI a3, 0xFF38BDF8        ; Cyan
    LI a4, 0x00000000
    LI a7, 15
    ECALL

    ; 16 Buttons (4 rows x 4 cols, X=256, Y=196)
    ; Row 0: 7, 8, 9, /
    CALL draw_calc_btn_row0
    ; Row 1: 4, 5, 6, *
    CALL draw_calc_btn_row1
    ; Row 2: 1, 2, 3, -
    CALL draw_calc_btn_row2
    ; Row 3: C, 0, =, +
    CALL draw_calc_btn_row3

    LD ra, 8(sp)
    ADDI sp, sp, 16
    RET

draw_calc_btn_row0:
    ADDI sp, sp, -16
    SD ra, 8(sp)
    LI a0, 256
    LI a1, 196
    LA a2, str_btn_7
    LI a3, 0xFF334155
    CALL draw_cbtn
    LI a0, 330
    LI a1, 196
    LA a2, str_btn_8
    LI a3, 0xFF334155
    CALL draw_cbtn
    LI a0, 404
    LI a1, 196
    LA a2, str_btn_9
    LI a3, 0xFF334155
    CALL draw_cbtn
    LI a0, 478
    LI a1, 196
    LA a2, str_btn_div
    LI a3, 0xFF2563EB
    CALL draw_cbtn
    LD ra, 8(sp)
    ADDI sp, sp, 16
    RET

draw_calc_btn_row1:
    ADDI sp, sp, -16
    SD ra, 8(sp)
    LI a0, 256
    LI a1, 254
    LA a2, str_btn_4
    LI a3, 0xFF334155
    CALL draw_cbtn
    LI a0, 330
    LI a1, 254
    LA a2, str_btn_5
    LI a3, 0xFF334155
    CALL draw_cbtn
    LI a0, 404
    LI a1, 254
    LA a2, str_btn_6
    LI a3, 0xFF334155
    CALL draw_cbtn
    LI a0, 478
    LI a1, 254
    LA a2, str_btn_mul
    LI a3, 0xFF2563EB
    CALL draw_cbtn
    LD ra, 8(sp)
    ADDI sp, sp, 16
    RET

draw_calc_btn_row2:
    ADDI sp, sp, -16
    SD ra, 8(sp)
    LI a0, 256
    LI a1, 312
    LA a2, str_btn_1
    LI a3, 0xFF334155
    CALL draw_cbtn
    LI a0, 330
    LI a1, 312
    LA a2, str_btn_2
    LI a3, 0xFF334155
    CALL draw_cbtn
    LI a0, 404
    LI a1, 312
    LA a2, str_btn_3
    LI a3, 0xFF334155
    CALL draw_cbtn
    LI a0, 478
    LI a1, 312
    LA a2, str_btn_sub
    LI a3, 0xFF2563EB
    CALL draw_cbtn
    LD ra, 8(sp)
    ADDI sp, sp, 16
    RET

draw_calc_btn_row3:
    ADDI sp, sp, -16
    SD ra, 8(sp)
    LI a0, 256
    LI a1, 370
    LA a2, str_btn_c
    LI a3, 0xFFDC2626
    CALL draw_cbtn
    LI a0, 330
    LI a1, 370
    LA a2, str_btn_0
    LI a3, 0xFF334155
    CALL draw_cbtn
    LI a0, 404
    LI a1, 370
    LA a2, str_btn_eq
    LI a3, 0xFF10B981
    CALL draw_cbtn
    LI a0, 478
    LI a1, 370
    LA a2, str_btn_add
    LI a3, 0xFF2563EB
    CALL draw_cbtn
    LD ra, 8(sp)
    ADDI sp, sp, 16
    RET

; Helper: draw calculator button at a0=X, a1=Y, a2=text, a3=color
draw_cbtn:
    ADDI sp, sp, -32
    SD ra, 24(sp)
    SD s0, 16(sp)
    SD s1, 8(sp)
    SD s2, 0(sp)
    MV s0, a0
    MV s1, a1
    MV s2, a2

    ; Button body: W=66, H=48
    MV a4, a3
    LI a2, 66
    LI a3, 48
    LI a7, 14
    ECALL

    ; Button text at center (+28, +16)
    ADDI a0, s0, 28
    ADDI a1, s1, 16
    MV a2, s2
    LI a3, 0xFFFFFFFF
    LI a4, 0x00000000
    LI a7, 15
    ECALL

    LD s2, 0(sp)
    LD s1, 8(sp)
    LD s0, 16(sp)
    LD ra, 24(sp)
    ADDI sp, sp, 32
    RET

calc_on_key:
    ADDI sp, sp, -16
    SD ra, 8(sp)

    ; a1 = key
    LI t0, 48                ; '0'
    BLT a1, t0, calc_chk_op
    LI t0, 57                ; '9'
    BGT a1, t0, calc_chk_op
    ; Digit 0..9
    ADDI a0, a1, -48         ; a0 = digit (0..9)
    J calc_input_digit

calc_chk_op:
    LI t0, 43                ; '+'
    BEQ a1, t0, cop_add
    LI t0, 45                ; '-'
    BEQ a1, t0, cop_sub
    LI t0, 42                ; '*'
    BEQ a1, t0, cop_mul
    LI t0, 47                ; '/'
    BEQ a1, t0, cop_div
    LI t0, 61                ; '='
    BEQ a1, t0, cop_eq
    LI t0, 13                ; Enter
    BEQ a1, t0, cop_eq
    LI t0, 99                ; 'c'
    BEQ a1, t0, cop_c
    LI t0, 67                ; 'C'
    BEQ a1, t0, cop_c
    J calc_ret

calc_input_digit:
    LA t0, calc_cur
    LD t1, 0(t0)
    LI t2, 1000000000000000
    BGE t1, t2, calc_ret
    LI t2, 10
    MUL t1, t1, t2
    ADD t1, t1, a0
    SD t1, 0(t0)
    MV a0, t1
    LA a1, calc_buf
    CALL num_to_dec
    CALL flag_redraw
    J calc_ret

cop_add:
    LI a0, 1
    J set_op
cop_sub:
    LI a0, 2
    J set_op
cop_mul:
    LI a0, 3
    J set_op
cop_div:
    LI a0, 4
    J set_op

set_op:
    LA t0, calc_acc
    LA t1, calc_cur
    LD t2, 0(t1)
    SD t2, 0(t0)             ; acc = cur
    SD zero, 0(t1)           ; cur = 0
    LA t0, calc_op
    SW a0, 0(t0)
    CALL flag_redraw
    J calc_ret

cop_eq:
    LA t0, calc_op
    LW t1, 0(t0)
    BEQ t1, zero, calc_ret
    LA t2, calc_acc
    LD t3, 0(t2)             ; acc
    LA t4, calc_cur
    LD t5, 0(t4)             ; cur

    LI t6, 1
    BEQ t1, t6, do_add
    LI t6, 2
    BEQ t1, t6, do_sub
    LI t6, 3
    BEQ t1, t6, do_mul
    LI t6, 4
    BEQ t1, t6, do_div
    J calc_done_eval
do_add:
    ADD t3, t3, t5
    J calc_done_eval
do_sub:
    SUB t3, t3, t5
    J calc_done_eval
do_mul:
    MUL t3, t3, t5
    J calc_done_eval
do_div:
    BEQ t5, zero, calc_done_eval
    DIV t3, t3, t5
calc_done_eval:
    SD t3, 0(t4)             ; cur = result
    SW zero, 0(t0)           ; op = 0
    MV a0, t3
    LA a1, calc_buf
    CALL num_to_dec
    CALL flag_redraw
    J calc_ret

cop_c:
    CALL calc_init
    CALL flag_redraw
    J calc_ret

calc_ret:
    LD ra, 8(sp)
    ADDI sp, sp, 16
    RET

calc_on_click:
    ADDI sp, sp, -16
    SD ra, 8(sp)

    ; a0=X, a1=Y
    ; Determine which button row & col was clicked
    LI t0, 196
    BLT a1, t0, calc_click_ret
    LI t0, 420
    BGT a1, t0, calc_click_ret
    LI t0, 256
    BLT a0, t0, calc_click_ret
    LI t0, 546
    BGT a0, t0, calc_click_ret

    ; Which column?
    LI t0, 324
    BLT a0, t0, col_0
    LI t0, 398
    BLT a0, t0, col_1
    LI t0, 472
    BLT a0, t0, col_2
    J col_3

col_0:
    LI t0, 246
    BLT a1, t0, b_7
    LI t0, 304
    BLT a1, t0, b_4
    LI t0, 362
    BLT a1, t0, b_1
    J b_c
col_1:
    LI t0, 246
    BLT a1, t0, b_8
    LI t0, 304
    BLT a1, t0, b_5
    LI t0, 362
    BLT a1, t0, b_2
    J b_0
col_2:
    LI t0, 246
    BLT a1, t0, b_9
    LI t0, 304
    BLT a1, t0, b_6
    LI t0, 362
    BLT a1, t0, b_3
    J b_eq
col_3:
    LI t0, 246
    BLT a1, t0, b_div
    LI t0, 304
    BLT a1, t0, b_mul
    LI t0, 362
    BLT a1, t0, b_sub
    J b_add

b_0:
    LI a0, 0
    J calc_input_digit
b_1:
    LI a0, 1
    J calc_input_digit
b_2:
    LI a0, 2
    J calc_input_digit
b_3:
    LI a0, 3
    J calc_input_digit
b_4:
    LI a0, 4
    J calc_input_digit
b_5:
    LI a0, 5
    J calc_input_digit
b_6:
    LI a0, 6
    J calc_input_digit
b_7:
    LI a0, 7
    J calc_input_digit
b_8:
    LI a0, 8
    J calc_input_digit
b_9:
    LI a0, 9
    J calc_input_digit
b_add:
    J cop_add
b_sub:
    J cop_sub
b_mul:
    J cop_mul
b_div:
    J cop_div
b_eq:
    J cop_eq
b_c:
    J cop_c

calc_click_ret:
    LD ra, 8(sp)
    ADDI sp, sp, 16
    RET

; ==============================================================================
; FAT16 Filesystem Subsystem & Applications (Notepad & File Explorer)
; ==============================================================================

; --- Helper: String Case-Insensitive Compare ---
; a0 = str1, a1 = str2 -> returns a0 = 0 if equal, 1 if not
str_case_cmp:
scc_loop:
    LBU t0, 0(a0)
    LBU t1, 0(a1)
    LI t2, 65
    BLT t0, t2, scc_t0_ok
    LI t2, 90
    BGT t0, t2, scc_t0_ok
    ADDI t0, t0, 32
scc_t0_ok:
    LI t2, 65
    BLT t1, t2, scc_t1_ok
    LI t2, 90
    BGT t1, t2, scc_t1_ok
    ADDI t1, t1, 32
scc_t1_ok:
    BNE t0, t1, scc_diff
    BEQ t0, zero, scc_eq
    ADDI a0, a0, 1
    ADDI a1, a1, 1
    J scc_loop
scc_diff:
    LI a0, 1
    RET
scc_eq:
    LI a0, 0
    RET

; --- Helper: Copy a2 bytes from a0 to a1 ---
memcpy_bytes:
mcb_loop:
    BEQ a2, zero, mcb_done
    LBU a3, 0(a0)
    SB a3, 0(a1)
    ADDI a0, a0, 1
    ADDI a1, a1, 1
    ADDI a2, a2, -1
    J mcb_loop
mcb_done:
    RET

; --- Helper: Format 8.3 Directory Entry Name ---
; a0 = entry ptr (32 bytes), a1 = dst buffer (at least 16 bytes)
fat16_format_name:
    MV t0, a0
    MV t1, a1

    LI t2, 8
fn_len_l:
    BEQ t2, zero, fn_copy_n
    ADDI t3, t2, -1
    ADD t3, t0, t3
    LBU t3, 0(t3)
    LI t4, 32
    BNE t3, t4, fn_copy_n
    ADDI t2, t2, -1
    J fn_len_l

fn_copy_n:
    LI t3, 0
fn_cn_l:
    BGE t3, t2, fn_chk_e
    ADD t4, t0, t3
    LBU t4, 0(t4)
    LI t5, 65
    BLT t4, t5, fn_cn_p
    LI t5, 90
    BGT t4, t5, fn_cn_p
    ADDI t4, t4, 32
fn_cn_p:
    SB t4, 0(t1)
    ADDI t1, t1, 1
    ADDI t3, t3, 1
    J fn_cn_l

fn_chk_e:
    LBU t3, 8(t0)
    LI t4, 32
    BEQ t3, t4, fn_fin

    LI t4, 46
    SB t4, 0(t1)
    ADDI t1, t1, 1

    LI t3, 8
fn_ext_l:
    LI t4, 11
    BGE t3, t4, fn_fin
    ADD t4, t0, t3
    LBU t4, 0(t4)
    LI t5, 32
    BEQ t4, t5, fn_fin
    LI t5, 65
    BLT t4, t5, fn_ext_p
    LI t5, 90
    BGT t4, t5, fn_ext_p
    ADDI t4, t4, 32
fn_ext_p:
    SB t4, 0(t1)
    ADDI t1, t1, 1
    ADDI t3, t3, 1
    J fn_ext_l

fn_fin:
    SB zero, 0(t1)
    RET

; ==============================================================================
; fat16_init: Mount FAT16 volume by reading BPB and calculating offsets
; ==============================================================================
fat16_init:
    ADDI sp, sp, -16
    SD ra, 8(sp)

    ; Read Sector 0 (BPB)
    LI a0, 0
    LA a1, fat16_sec_buf
    LI a2, 1
    LI a7, 6
    ECALL                    ; SYS_DISK_READ
    BNE a0, zero, f16_init_err

    ; Check boot signature (0x55, 0xAA)
    LA t0, fat16_sec_buf
    LBU t1, 510(t0)
    LI t2, 0x55
    BNE t1, t2, f16_init_err
    LBU t1, 511(t0)
    LI t2, 0xAA
    BNE t1, t2, f16_init_err

    ; Bytes per sector == 512
    LHU t1, 11(t0)
    LI t2, 512
    BNE t1, t2, f16_init_err

    ; Sectors per cluster
    LBU t1, 13(t0)
    BEQ t1, zero, f16_init_err
    LA t2, fat16_sec_per_clus
    SW t1, 0(t2)

    ; Reserved sectors
    LHU t3, 14(t0)
    LA t2, fat16_fat_start
    SW t3, 0(t2)

    ; Number of FATs
    LBU t4, 16(t0)

    ; Root entries
    LHU t5, 17(t0)

    ; Sectors per FAT
    LHU t6, 22(t0)
    LA t2, fat16_sec_per_fat
    SW t6, 0(t2)

    ; Root Directory Start = reserved_sectors + (num_fats * sec_per_fat)
    MUL t1, t4, t6
    ADD t1, t3, t1
    LA t2, fat16_root_start
    SW t1, 0(t2)

    ; Root Directory Sectors = (root_entries * 32 + 511) / 512
    SLLI t2, t5, 5
    ADDI t2, t2, 511
    SRLI t2, t2, 9
    LA a3, fat16_root_secs
    SW t2, 0(a3)

    ; Data Start = root_start + root_secs
    ADD t2, t1, t2
    LA a3, fat16_data_start
    SW t2, 0(a3)

    ; Mounted flag = 1
    LA t0, fat16_mounted
    LI t1, 1
    SW t1, 0(t0)

    ; Read initial root directory
    CALL fat16_read_dir

    LI a0, 0
    J f16_init_done

f16_init_err:
    LA t0, fat16_mounted
    SW zero, 0(t0)
    LI a0, -1

f16_init_done:
    LD ra, 8(sp)
    ADDI sp, sp, 16
    RET

; ==============================================================================
; fat16_read_dir: Read root directory sectors & count valid files
; ==============================================================================
fat16_read_dir:
    ADDI sp, sp, -16
    SD ra, 8(sp)

    LA t0, fat16_mounted
    LW t0, 0(t0)
    BEQ t0, zero, f16_rd_err

    ; Read first 2 sectors of Root Directory into fat16_dir_buf
    LA t0, fat16_root_start
    LW a0, 0(t0)
    LA a1, fat16_dir_buf
    LI a2, 2
    LI a7, 6
    ECALL
    BNE a0, zero, f16_rd_err

    ; Scan entries
    LI t0, 0
    LI t1, 0
    LA t2, fat16_dir_buf

f16_cnt_loop:
    LI t3, 32
    BGE t0, t3, f16_cnt_done
    SLLI t3, t0, 5
    ADD t3, t2, t3
    LBU t4, 0(t3)
    BEQ t4, zero, f16_cnt_done
    LI t5, 0xE5
    BEQ t4, t5, f16_cnt_next
    LBU t4, 11(t3)
    LI t5, 0x0F
    BEQ t4, t5, f16_cnt_next
    ANDI t5, t4, 0x08
    BNE t5, zero, f16_cnt_next

    ADDI t1, t1, 1

f16_cnt_next:
    ADDI t0, t0, 1
    J f16_cnt_loop

f16_cnt_done:
    LA t0, fat16_file_count
    SW t1, 0(t0)
    MV a0, t1
    J f16_rd_done

f16_rd_err:
    LI a0, -1

f16_rd_done:
    LD ra, 8(sp)
    ADDI sp, sp, 16
    RET

; ==============================================================================
; fat16_find_file: Find directory entry by filename
; a0 = pointer to filename string -> returns a0 = pointer to entry or 0
; ==============================================================================
fat16_find_file:
    ADDI sp, sp, -32
    SD ra, 24(sp)
    SD s0, 16(sp)
    SD s1, 8(sp)

    MV s0, a0
    LI s1, 0

f16_ff_loop:
    LI t0, 32
    BGE s1, t0, f16_ff_fail

    SLLI t0, s1, 5
    LA t1, fat16_dir_buf
    ADD t1, t1, t0

    LBU t2, 0(t1)
    BEQ t2, zero, f16_ff_fail
    LI t3, 0xE5
    BEQ t2, t3, f16_ff_nxt
    LBU t2, 11(t1)
    LI t3, 0x0F
    BEQ t2, t3, f16_ff_nxt
    ANDI t3, t2, 0x08
    BNE t3, zero, f16_ff_nxt

    MV a0, t1
    LA a1, fm_name_buf
    CALL fat16_format_name

    LA a0, fm_name_buf
    MV a1, s0
    CALL str_case_cmp
    BEQ a0, zero, f16_ff_match

f16_ff_nxt:
    ADDI s1, s1, 1
    J f16_ff_loop

f16_ff_match:
    SLLI t0, s1, 5
    LA t1, fat16_dir_buf
    ADD a0, t1, t0
    J f16_ff_ret

f16_ff_fail:
    LI a0, 0

f16_ff_ret:
    LD s1, 8(sp)
    LD s0, 16(sp)
    LD ra, 24(sp)
    ADDI sp, sp, 32
    RET

; ==============================================================================
; fat16_read_file: Traverse FAT cluster chain to load file contents into RAM
; a0 = start cluster, a1 = RAM dst, a2 = max bytes, a3 = file size
; returns a0 = total bytes read
; ==============================================================================
fat16_read_file:
    ADDI sp, sp, -48
    SD ra, 40(sp)
    SD s0, 32(sp)
    SD s1, 24(sp)
    SD s2, 16(sp)
    SD s3, 8(sp)
    SD s4, 0(sp)

    MV s0, a0
    MV s1, a1
    MV s2, a2
    MV s4, a3
    LI s3, 0

    LI t0, 2
    BLT s0, t0, f16_rf_done
    LI t0, 0xFFF8
    BGE s0, t0, f16_rf_done

f16_rf_loop:
    BGE s3, s4, f16_rf_done
    BGE s3, s2, f16_rf_done

    ; LBA = fat16_data_start + (cur_cluster - 2) * fat16_sec_per_clus
    LA t0, fat16_data_start
    LW t1, 0(t0)
    ADDI t2, s0, -2
    LA t0, fat16_sec_per_clus
    LW t3, 0(t0)
    MUL t2, t2, t3
    ADD a0, t1, t2

    SUB t0, s4, s3
    SUB t1, s2, s3
    BLE t0, t1, f16_rf_lim
    MV t0, t1
f16_rf_lim:
    LI t1, 512
    BLT t0, t1, f16_rf_part

    ADD a1, s1, s3
    LI a2, 1
    LI a7, 6
    ECALL
    BNE a0, zero, f16_rf_done
    ADDI s3, s3, 512
    J f16_rf_next_c

f16_rf_part:
    BEQ t0, zero, f16_rf_done
    LA a1, fat16_sec_buf
    LI a2, 1
    LI a7, 6
    ECALL
    BNE a0, zero, f16_rf_done

    LA a0, fat16_sec_buf
    ADD a1, s1, s3
    ADD s3, s3, t0
    MV a2, t0
    CALL memcpy_bytes
    J f16_rf_done

f16_rf_next_c:
    SLLI t0, s0, 1
    SRLI t1, t0, 9
    ANDI t2, t0, 511

    LA t3, fat16_fat_start
    LW a0, 0(t3)
    ADD a0, a0, t1
    LA a1, fat16_fat_buf
    LI a2, 1
    LI a7, 6
    ECALL
    BNE a0, zero, f16_rf_done

    LA t0, fat16_fat_buf
    ADD t0, t0, t2
    LHU s0, 0(t0)

    LI t0, 2
    BLT s0, t0, f16_rf_done
    LI t0, 0xFFF8
    BGE s0, t0, f16_rf_done

    J f16_rf_loop

f16_rf_done:
    MV a0, s3
    LD s4, 0(sp)
    LD s3, 8(sp)
    LD s2, 16(sp)
    LD s1, 24(sp)
    LD s0, 32(sp)
    LD ra, 40(sp)
    ADDI sp, sp, 48
    RET

; ==============================================================================
; fat16_write_file: Write buffer to file clusters & update root dir entry
; a0 = entry ptr, a1 = src buffer, a2 = byte length
; returns a0 = 0 on success, -1 on failure
; ==============================================================================
fat16_write_file:
    ADDI sp, sp, -64
    SD ra, 56(sp)
    SD s0, 48(sp)
    SD s1, 40(sp)
    SD s2, 32(sp)
    SD s3, 24(sp)
    SD s4, 16(sp)
    SD s5, 8(sp)

    MV s0, a0
    MV s1, a1
    MV s2, a2
    LI s4, 0

    LA t0, fat16_mounted
    LW t0, 0(t0)
    BEQ t0, zero, f16_wf_fail

    ; Read FAT Sector 0 into fat16_fat_buf
    LA t0, fat16_fat_start
    LW a0, 0(t0)
    LA a1, fat16_fat_buf
    LI a2, 1
    LI a7, 6
    ECALL
    BNE a0, zero, f16_wf_fail

    ; Get cluster from entry
    LHU s3, 26(s0)
    LI t0, 2
    BGE s3, t0, f16_wf_alloc_ok

    ; Allocate free cluster in fat16_fat_buf
    LI t0, 2
f16_wf_find_free:
    LI t1, 256
    BGE t0, t1, f16_wf_fail
    SLLI t1, t0, 1
    LA t2, fat16_fat_buf
    ADD t2, t2, t1
    LHU t3, 0(t2)
    BEQ t3, zero, f16_wf_got_free
    ADDI t0, t0, 1
    J f16_wf_find_free

f16_wf_got_free:
    MV s3, t0
    SH s3, 26(s0)

f16_wf_alloc_ok:
f16_wf_write_loop:
    LA t0, fat16_data_start
    LW t1, 0(t0)
    ADDI t2, s3, -2
    LA t0, fat16_sec_per_clus
    LW t3, 0(t0)
    MUL t2, t2, t3
    ADD s5, t1, t2

    SUB t0, s2, s4
    LI t1, 512
    BLT t0, t1, f16_wf_part_sec

    MV a0, s5
    ADD a1, s1, s4
    LI a2, 1
    LI a7, 8
    ECALL
    ADDI s4, s4, 512
    J f16_wf_check_done

f16_wf_part_sec:
    LA t1, fat16_sec_buf
    LI t2, 512
f16_wf_clr_l:
    BEQ t2, zero, f16_wf_clr_d
    SB zero, 0(t1)
    ADDI t1, t1, 1
    ADDI t2, t2, -1
    J f16_wf_clr_l
f16_wf_clr_d:
    ADD a0, s1, s4
    LA a1, fat16_sec_buf
    ADD s4, s4, t0
    MV a2, t0
    CALL memcpy_bytes

    MV a0, s5
    LA a1, fat16_sec_buf
    LI a2, 1
    LI a7, 8
    ECALL

f16_wf_check_done:
    BGE s4, s2, f16_wf_end_chain

    SLLI t0, s3, 1
    LA t1, fat16_fat_buf
    ADD t1, t1, t0
    LHU t2, 0(t1)
    LI t3, 2
    BLT t2, t3, f16_wf_alloc_next
    LI t3, 0xFFF8
    BLT t2, t3, f16_wf_reuse_next

f16_wf_alloc_next:
    LI t0, 2
f16_wf_alloc_l:
    LI t1, 256
    BGE t0, t1, f16_wf_fail
    SLLI t1, t0, 1
    LA t2, fat16_fat_buf
    ADD t2, t2, t1
    LHU t3, 0(t2)
    BEQ t3, zero, f16_wf_alloc_found
    ADDI t0, t0, 1
    J f16_wf_alloc_l

f16_wf_alloc_found:
    SLLI t1, s3, 1
    LA t2, fat16_fat_buf
    ADD t2, t2, t1
    SH t0, 0(t2)
    MV s3, t0
    J f16_wf_write_loop

f16_wf_reuse_next:
    MV s3, t2
    J f16_wf_write_loop

f16_wf_end_chain:
    SLLI t0, s3, 1
    LA t1, fat16_fat_buf
    ADD t1, t1, t0
    LI t2, 0xFFFF
    SH t2, 0(t1)

    LA t0, fat16_fat_start
    LW a0, 0(t0)
    LA a1, fat16_fat_buf
    LI a2, 1
    LI a7, 8
    ECALL

    LA t0, fat16_fat_start
    LW a0, 0(t0)
    LA t1, fat16_sec_per_fat
    LW t1, 0(t1)
    ADD a0, a0, t1
    LA a1, fat16_fat_buf
    LI a2, 1
    LI a7, 8
    ECALL

    SW s2, 28(s0)

    LA t0, fat16_root_start
    LW a0, 0(t0)
    LA a1, fat16_dir_buf
    LI a2, 1
    LI a7, 8
    ECALL

    LI a0, 0
    J f16_wf_ret

f16_wf_fail:
    LI a0, -1

f16_wf_ret:
    LD s5, 8(sp)
    LD s4, 16(sp)
    LD s3, 24(sp)
    LD s2, 32(sp)
    LD s1, 40(sp)
    LD s0, 48(sp)
    LD ra, 56(sp)
    ADDI sp, sp, 64
    RET

; ==============================================================================
; Application 2: Notepad (FAT16 Text Editor)
; ==============================================================================
notepad_init:
    ADDI sp, sp, -16
    SD ra, 8(sp)
    LA t0, note_len
    SW zero, 0(t0)
    LA t0, note_cursor
    SW zero, 0(t0)
    LA t0, note_dirty
    SW zero, 0(t0)
    LA t0, note_file_idx
    SW zero, 0(t0)
    ; Boot document: existing /NOTES.TXT destination (Save writes it back).
    LA a0, note_path
    LA a1, str_boot_note
    CALL str_copy
    CALL notepad_open_file
    LD ra, 8(sp)
    ADDI sp, sp, 16
    RET

notepad_draw:
    ADDI sp, sp, -32
    SD ra, 24(sp)
    SD s0, 16(sp)
    SD s1, 8(sp)

    ; Window title carries path + modified mark (fitted by the frame).
    LA a0, note_title_buf
    LA a1, str_note_title_base
    CALL str_copy
    LA t0, note_path
    LBU t1, 0(t0)
    BEQ t1, zero, np_title_untitled
    LA a0, note_title_buf
    LA a1, note_path
    CALL str_append
    J np_title_dirty
np_title_untitled:
    LA a0, note_title_buf
    LA a1, str_note_untitled
    CALL str_append
np_title_dirty:
    LA t0, note_dirty
    LW t1, 0(t0)
    BEQ t1, zero, np_title_draw
    LA a0, note_title_buf
    LA a1, str_note_dirty_mark
    CALL str_append
np_title_draw:
    ; Window frame (X=80, Y=45, W=640, H=500)
    LI a0, 80
    LI a1, 45
    LI a2, 640
    LI a3, 500
    LA a4, note_title_buf
    CALL draw_window_frame

    ; Action Bar (X=80, Y=77, W=640, H=38, 0xFF242C3D)
    LI a0, 80
    LI a1, 77
    LI a2, 640
    LI a3, 38
    LI a4, 0xFF242C3D
    LI a7, 14
    ECALL

    ; Button [ New ] (X=95, Y=82, W=60, H=28, 0xFF334155)
    LI a0, 95
    LI a1, 82
    LI a2, 60
    LI a3, 28
    LI a4, 0xFF334155
    LI a7, 14
    ECALL
    LI a0, 108
    LI a1, 88
    LA a2, str_btn_new
    LI a3, 0xFFFFFFFF
    LI a4, 0x00000000
    LI a7, 15
    ECALL

    ; Button [ Open (F10) ] (X=165, Y=82, W=95, H=28, 0xFF2563EB)
    LI a0, 165
    LI a1, 82
    LI a2, 95
    LI a3, 28
    LI a4, 0xFF2563EB
    LI a7, 14
    ECALL
    LI a0, 172
    LI a1, 88
    LA a2, str_btn_open
    LI a3, 0xFFFFFFFF
    LI a4, 0x00000000
    LI a7, 15
    ECALL

    ; Button [ Save (F9) ] (X=270, Y=82, W=95, H=28, 0xFF059669)
    LI a0, 270
    LI a1, 82
    LI a2, 95
    LI a3, 28
    LI a4, 0xFF059669
    LI a7, 14
    ECALL
    LI a0, 278
    LI a1, 88
    LA a2, str_btn_save
    LI a3, 0xFFFFFFFF
    LI a4, 0x00000000
    LI a7, 15
    ECALL

    ; Button [ Save As ] (X=375, Y=82, W=95, H=28, 0xFF7C3AED)
    LI a0, 375
    LI a1, 82
    LI a2, 95
    LI a3, 28
    LI a4, 0xFF7C3AED
    LI a7, 14
    ECALL
    LI a0, 383
    LI a1, 88
    LA a2, str_btn_saveas
    LI a3, 0xFFFFFFFF
    LI a4, 0x00000000
    LI a7, 15
    ECALL

    ; Status text (fitted; the path now lives in the window title).
    LI a0, 480
    LI a1, 88
    LA a2, note_status_str
    LI a3, 0xFF94A3B8
    LI a4, 0x00000000
    LI a5, 225
    LI a6, 16
    LI a7, 43
    ECALL

    ; Text Editor Area: X=95, Y=125, W=610, H=400, 0xFF0F172A
    LI a0, 95
    LI a1, 125
    LI a2, 610
    LI a3, 400
    LI a4, 0xFF0F172A
    LI a7, 14
    ECALL

    ; Render Text Buffer
    LA s0, note_buf
    LA t0, note_len
    LW s1, 0(t0)
    LI t0, 105
    LI t1, 135
    LA t2, note_cur_col
    SW t0, 0(t2)
    LA t2, note_cur_row
    SW t1, 0(t2)

    LI t2, 0
np_txt_loop:
    LA t3, note_cursor
    LW t3, 0(t3)
    BNE t2, t3, np_not_cursor_pos
    LA t3, note_cur_col
    SW t0, 0(t3)
    LA t3, note_cur_row
    SW t1, 0(t3)
np_not_cursor_pos:
    BGE t2, s1, np_draw_cursor
    ADD t3, s0, t2
    LBU t3, 0(t3)

    LI t4, 10
    BEQ t3, t4, np_newline

    LA t4, one_char_buf
    SB t3, 0(t4)
    SB zero, 1(t4)
    LI t5, 0xC0
    BLT t3, t5, np_char_ready
    ADDI t6, t2, 1
    BGE t6, s1, np_char_ready
    ADD t6, s0, t6
    LBU t6, 0(t6)
    SB t6, 1(t4)
    SB zero, 2(t4)
    ADDI t2, t2, 1
np_char_ready:
    MV a0, t0
    MV a1, t1
    LA a2, one_char_buf
    LI a3, 0xFFF1F5F9
    LI a4, 0x00000000
    LI a7, 15
    ECALL

    ADDI t0, t0, 8
    LI t4, 680
    BLT t0, t4, np_next_char

np_newline:
    LI t0, 105
    ADDI t1, t1, 18
    LI t4, 510
    BGE t1, t4, np_draw_cursor

np_next_char:
    ADDI t2, t2, 1
    J np_txt_loop

np_draw_cursor:
    LA t0, note_cur_col
    LW a0, 0(t0)
    LA t0, note_cur_row
    LW a1, 0(t0)
    LI a2, 2
    LI a3, 16
    LI a4, 0xFF38BDF8
    LI a7, 14
    ECALL

    LD s1, 8(sp)
    LD s0, 16(sp)
    LD ra, 24(sp)
    ADDI sp, sp, 32
    RET

notepad_on_key:
    ADDI sp, sp, -16
    SD ra, 8(sp)

    ; Check KEYMOD_CTRL in a4 (bit 2: value 4)
    ANDI t0, a4, 4
    BEQ t0, zero, np_no_ctrl_mod
    LI t0, 115               ; 's'
    BEQ a1, t0, np_do_save
    LI t0, 83                ; 'S'
    BEQ a1, t0, np_do_save
    LI t0, 111               ; 'o'
    BEQ a1, t0, np_do_open
    LI t0, 79                ; 'O'
    BEQ a1, t0, np_do_open
    LI t0, 110               ; 'n'
    BEQ a1, t0, np_do_new
    LI t0, 78                ; 'N'
    BEQ a1, t0, np_do_new
np_no_ctrl_mod:
    LI t0, 14                ; Ctrl+N
    BEQ a1, t0, np_do_new
    LI t0, 261               ; F2 (Save)
    BEQ a1, t0, np_do_save
    LI t0, 262               ; F3 (Save As)
    BEQ a1, t0, np_do_saveas
    LI t0, 268               ; F9 (Save)
    BEQ a1, t0, np_do_save
    LI t0, 269               ; F10 (Open dialog)
    BEQ a1, t0, np_do_open
    LI t0, 19                ; Ctrl+S from terminal/X11 backends
    BEQ a1, t0, np_do_save
    LI t0, 15                ; Ctrl+O opens the file dialog
    BEQ a1, t0, np_do_open

    LI t0, 8                 ; Backspace
    BEQ a1, t0, np_do_bksp
    LI t0, 272               ; Delete
    BEQ a1, t0, np_do_delete
    LI t0, 258               ; Left
    BEQ a1, t0, np_do_left
    LI t0, 259               ; Right
    BEQ a1, t0, np_do_right
    LI t0, 270               ; Home
    BEQ a1, t0, np_do_home
    LI t0, 271               ; End
    BEQ a1, t0, np_do_end
    LI t0, 13                ; Enter
    BEQ a1, t0, np_do_enter
    LI t0, 10
    BEQ a1, t0, np_do_enter

    LI t0, 32
    BLT a1, t0, np_key_ret
    LI t0, 255
    BGT a1, t0, np_key_ret
    LI t0, 127
    BEQ a1, t0, np_key_ret
np_insert_byte:
    LA t0, note_len
    LW t1, 0(t0)
    LI t2, 4000
    BGE t1, t2, np_key_ret
    LA t2, note_cursor
    LW t3, 0(t2)
    LA t4, note_buf
    MV t5, t1
np_ins_shift:
    BLT t5, t3, np_ins_store
    ADD t6, t4, t5
    LBU a0, 0(t6)
    SB a0, 1(t6)
    ADDI t5, t5, -1
    J np_ins_shift
np_ins_store:
    ADD t4, t4, t3
    SB a1, 0(t4)
    ADDI t1, t1, 1
    SW t1, 0(t0)
    ADDI t3, t3, 1
    SW t3, 0(t2)
    LA t0, note_dirty
    LI t1, 1
    SW t1, 0(t0)
    CALL flag_redraw
    J np_key_ret

np_do_enter:
    LI a1, 10
    J np_insert_byte

np_do_bksp:
    LA t0, note_cursor
    LW t1, 0(t0)
    BEQ t1, zero, np_key_ret
    ADDI t2, t1, -1
    LA t3, note_buf
np_bs_utf8:
    BEQ t2, zero, np_bs_remove
    ADD t4, t3, t2
    LBU t5, 0(t4)
    ANDI t5, t5, 0xC0
    LI t6, 0x80
    BNE t5, t6, np_bs_remove
    ADDI t2, t2, -1
    J np_bs_utf8
np_bs_remove:
    MV a0, t2               ; destination/new cursor
    MV a1, t1               ; source
    J np_remove_range

np_do_delete:
    LA t0, note_cursor
    LW t1, 0(t0)
    LA t2, note_len
    LW t3, 0(t2)
    BGE t1, t3, np_key_ret
    ADDI t4, t1, 1
    LA t5, note_buf
np_del_utf8:
    BGE t4, t3, np_del_go
    ADD t6, t5, t4
    LBU a0, 0(t6)
    ANDI a0, a0, 0xC0
    LI a1, 0x80
    BNE a0, a1, np_del_go
    ADDI t4, t4, 1
    J np_del_utf8
np_del_go:
    MV a0, t1
    MV a1, t4
np_remove_range:
    LA t2, note_len
    LW t3, 0(t2)
    LA t4, note_buf
    MV t5, a0
    MV t6, a1
np_rm_shift:
    BGT t6, t3, np_rm_done
    ADD a2, t4, t6
    LBU a3, 0(a2)
    ADD a2, t4, t5
    SB a3, 0(a2)
    ADDI t5, t5, 1
    ADDI t6, t6, 1
    J np_rm_shift
np_rm_done:
    SUB t3, t3, a1
    ADD t3, t3, a0
    SW t3, 0(t2)
    LA t0, note_cursor
    SW a0, 0(t0)
    LA t0, note_dirty
    LI t1, 1
    SW t1, 0(t0)
    CALL flag_redraw
    J np_key_ret

np_do_left:
    LA t0, note_cursor
    LW t1, 0(t0)
    BEQ t1, zero, np_key_ret
    ADDI t1, t1, -1
    LA t2, note_buf
np_left_utf8:
    BEQ t1, zero, np_move_store
    ADD t3, t2, t1
    LBU t4, 0(t3)
    ANDI t4, t4, 0xC0
    LI t5, 0x80
    BNE t4, t5, np_move_store
    ADDI t1, t1, -1
    J np_left_utf8
np_do_right:
    LA t0, note_cursor
    LW t1, 0(t0)
    LA t2, note_len
    LW t3, 0(t2)
    BGE t1, t3, np_key_ret
    ADDI t1, t1, 1
    LA t2, note_buf
np_right_utf8:
    BGE t1, t3, np_move_store
    ADD t4, t2, t1
    LBU t5, 0(t4)
    ANDI t5, t5, 0xC0
    LI t6, 0x80
    BNE t5, t6, np_move_store
    ADDI t1, t1, 1
    J np_right_utf8
np_do_home:
    LA t0, note_cursor
    LW t1, 0(t0)
    LA t2, note_buf
np_home_loop:
    BEQ t1, zero, np_move_store
    ADDI t3, t1, -1
    ADD t4, t2, t3
    LBU t5, 0(t4)
    LI t6, 10
    BEQ t5, t6, np_move_store
    MV t1, t3
    J np_home_loop
np_do_end:
    LA t0, note_cursor
    LW t1, 0(t0)
    LA t2, note_len
    LW t3, 0(t2)
    LA t2, note_buf
np_end_loop:
    BGE t1, t3, np_move_store
    ADD t4, t2, t1
    LBU t5, 0(t4)
    LI t6, 10
    BEQ t5, t6, np_move_store
    ADDI t1, t1, 1
    J np_end_loop
np_move_store:
    LA t0, note_cursor
    SW t1, 0(t0)
    CALL flag_redraw
    J np_key_ret

np_do_new:
    CALL notepad_request_new
    CALL flag_redraw
    J np_key_ret

np_do_saveas:
    CALL dlg_show_saveas
    CALL flag_redraw
    J np_key_ret

np_do_save:
    CALL notepad_do_save
    J np_key_ret

np_do_open:
    CALL notepad_request_open_dlg
    CALL flag_redraw
    J np_key_ret

np_key_ret:
    LD ra, 8(sp)
    ADDI sp, sp, 16
    RET

notepad_on_click:
    ADDI sp, sp, -16
    SD ra, 8(sp)

    LI t0, 82
    BLT a1, t0, np_click_ret
    LI t0, 110
    BGT a1, t0, np_click_ret

    LI t0, 95
    BLT a0, t0, np_click_ret
    LI t0, 155
    BLE a0, t0, np_click_new

    LI t0, 165
    BLT a0, t0, np_click_ret
    LI t0, 260
    BLE a0, t0, np_do_open_c

    LI t0, 270
    BLT a0, t0, np_click_ret
    LI t0, 365
    BLE a0, t0, np_do_save_c

    LI t0, 375
    BLT a0, t0, np_click_ret
    LI t0, 470
    BLE a0, t0, np_do_saveas_c
    J np_click_ret

np_do_open_c:
    CALL notepad_request_open_dlg
    CALL flag_redraw
    J np_click_ret

np_do_save_c:
    CALL notepad_do_save
    J np_click_ret

np_do_saveas_c:
    CALL dlg_show_saveas
    CALL flag_redraw
    J np_click_ret

np_click_new:
    CALL notepad_request_new
    CALL flag_redraw
    J np_click_ret

np_click_ret:
    LD ra, 8(sp)
    ADDI sp, sp, 16
    RET

; (Legacy direct save/open bodies were replaced by the path-based
; notepad_load_path / notepad_write_path service above.)

; ==============================================================================
; Application 3: File Explorer (shared FS_LIST model with validated sizes)
;
; The visible list is rebuilt from FS_LIST on every draw, so the first
; completed listing already shows correct sizes (the old direct sector
; reader lost its entry pointer across helper calls and showed zeros).
; No sleeps or placeholder rows: FS_LIST skips deleted/empty entries.
; Sizes render decimal B/KB/MB; folders show <DIR>, never a byte count.
; ==============================================================================
fileman_draw:
    ADDI sp, sp, -48
    SD ra, 40(sp)
    SD s0, 32(sp)
    SD s1, 24(sp)
    SD s2, 16(sp)
    SD s3, 8(sp)

    LI a0, 100
    LI a1, 60
    LI a2, 600
    LI a3, 460
    LA a4, str_file_title
    CALL draw_window_frame

    CALL fm_refresh

    ; Current directory line (fitted, never overflows the frame).
    LI a0, 120
    LI a1, 100
    LA a2, fm_cwd
    LI a3, 0xFF38BDF8
    LI a4, 0x00000000
    LI a5, 430
    LI a6, 16
    LI a7, 43
    ECALL

    ; [Up] button (or "(top)" at the filesystem root).
    LA t0, fm_cwd
    LBU t1, 0(t0)
    LI t2, 47
    BNE t1, t2, fm_no_up
    LBU t1, 1(t0)
    BNE t1, zero, fm_no_up
    LI a0, 570
    LI a1, 96
    LA a2, str_fm_top
    LI a3, 0xFF64748B
    LI a4, 0x00000000
    LI a7, 15
    ECALL
    J fm_after_up
fm_no_up:
    LI a0, 570
    LI a1, 96
    LI a2, 110
    LI a3, 24
    LI a4, 0xFF334155
    LI a7, 14
    ECALL
    LI a0, 578
    LI a1, 100
    LA a2, str_fm_up
    LI a3, 0xFFFFFFFF
    LI a4, 0x00000000
    LI a7, 15
    ECALL
fm_after_up:

    ; Column headers.
    LI a0, 120
    LI a1, 124
    LA a2, str_fm_hdr_name
    LI a3, 0xFF38BDF8
    LI a4, 0x00000000
    LI a7, 15
    ECALL
    LI a0, 410
    LI a1, 124
    LA a2, str_fm_hdr_size
    LI a3, 0xFF38BDF8
    LI a4, 0x00000000
    LI a7, 15
    ECALL

    ; Rows: optional ".." parent row plus cached DirEnt entries.
    LA t0, fm_cwd
    LBU t1, 0(t0)
    LI t2, 47
    BNE t1, t2, fm_dotdot_no
    LBU t1, 1(t0)
    BEQ t1, zero, fm_dotdot_no
    LI s3, 1
    J fm_rows_begin
fm_dotdot_no:
    LI s3, 0
fm_rows_begin:
    LI s0, 0
    LI s1, 146
fm_row_loop:
    LI t0, 8
    BGE s0, t0, fm_status_line
    LA t0, fm_scroll
    LW t2, 0(t0)
    ADD t2, t2, s0             ; visible row -> list position
    BNE s3, zero, fm_row_mapped
    LA t0, fm_count
    LW t3, 0(t0)
    BGE t2, t3, fm_draw_done2
    J fm_row_draw_entry
fm_row_mapped:
    BEQ t2, zero, fm_row_dotdot
    ADDI t2, t2, -1
    LA t0, fm_count
    LW t3, 0(t0)
    BGE t2, t3, fm_draw_done2
fm_row_draw_entry:
    ; t2 = cache index. entry = fm_list_buf + t2*28.
    LI t0, 28
    MUL t0, t2, t0
    LA t1, fm_list_buf
    ADD s2, t1, t0             ; s2 = entry (callee-saved across calls)
    LBU t0, 13(s2)             ; attributes
    ANDI t0, t0, 0x10
    BEQ t0, zero, fm_is_file
    LA a2, str_fm_dir_tag
    J fm_row_common
fm_is_file:
    LW a0, 20(s2)              ; size (32-bit, validated by FS_LIST)
    LA a1, fm_size_buf
    CALL fmt_size
    LA a2, fm_size_buf
fm_row_common:
    ; Selection highlight behind name+size columns.
    LA t0, fm_sel
    LW t1, 0(t0)
    LA t0, fm_scroll
    LW t3, 0(t0)
    ADD t3, t3, s0
    BNE t1, t3, fm_no_hl
    LI a0, 115
    MV a1, s1
    ADDI a1, a1, -2
    LI a2, 415
    LI a3, 24
    LI a4, 0xFF1E3A8A
    LI a7, 14
    ECALL
fm_no_hl:
    ; Name (fitted to its column).
    LI a0, 120
    MV a1, s1
    ADDI a2, s2, 0             ; entry name
    LBU t0, 13(s2)
    ANDI t0, t0, 0x10
    LI a3, 0xFF38BDF8
    BEQ t0, zero, fm_name_col
    LI a3, 0xFFFFFFFF
fm_name_col:
    LI a4, 0x00000000
    LI a5, 275
    LI a6, 16
    LI a7, 43
    ECALL
    ; Size / <DIR> tag.
    LI a0, 410
    MV a1, s1
    LBU t0, 13(s2)
    ANDI t0, t0, 0x10
    BNE t0, zero, fm_draw_dir_tag
    LA a2, fm_size_buf
    J fm_draw_size_txt
fm_draw_dir_tag:
    LA a2, str_fm_dir_tag
fm_draw_size_txt:
    LI a3, 0xFF34C759
    LI a4, 0x00000000
    LI a5, 120
    LI a6, 16
    LI a7, 43
    ECALL
    ; Action button with per-type label.
    LI a0, 545
    MV a1, s1
    ADDI a1, a1, -2
    LI a2, 135
    LI a3, 24
    LI a4, 0xFF2563EB
    LI a7, 14
    ECALL
    LBU t0, 13(s2)
    ANDI t0, t0, 0x10
    BNE t0, zero, fm_btn_open
    ADDI a0, s2, 0
    LA a1, str_ext_app
    CALL ext_is
    BNE a0, zero, fm_btn_run
    ADDI a0, s2, 0
    LA a1, str_ext_txt
    CALL ext_is
    BNE a0, zero, fm_btn_edit
    LA a2, str_fm_info_btn
    J fm_btn_draw
fm_btn_run:
    LA a2, str_fm_run_btn
    J fm_btn_draw
fm_btn_edit:
    LA a2, str_fm_edit_btn
    J fm_btn_draw
fm_btn_open:
    LA a2, str_fm_open_btn2
fm_btn_draw:
    LI a0, 555
    MV a1, s1
    LI a3, 0xFFFFFFFF
    LI a4, 0x00000000
    LI a7, 15
    ECALL
    J fm_next_row

fm_row_dotdot:
    ; Synthetic parent row (".." / "<UP>").
    LA t0, fm_sel
    LW t1, 0(t0)
    LA t0, fm_scroll
    LW t3, 0(t0)
    ADD t3, t3, s0
    BNE t1, t3, fm_dd_nohl
    LI a0, 115
    MV a1, s1
    ADDI a1, a1, -2
    LI a2, 415
    LI a3, 24
    LI a4, 0xFF1E3A8A
    LI a7, 14
    ECALL
fm_dd_nohl:
    LI a0, 120
    MV a1, s1
    LA a2, str_dotdot
    LI a3, 0xFFFFFFFF
    LI a4, 0x00000000
    LI a7, 15
    ECALL
    LI a0, 410
    MV a1, s1
    LA a2, str_fm_up_tag
    LI a3, 0xFF34C759
    LI a4, 0x00000000
    LI a7, 15
    ECALL
    LI a0, 545
    MV a1, s1
    ADDI a1, a1, -2
    LI a2, 135
    LI a3, 24
    LI a4, 0xFF2563EB
    LI a7, 14
    ECALL
    LI a0, 555
    MV a1, s1
    LA a2, str_fm_open_btn2
    LI a3, 0xFFFFFFFF
    LI a4, 0x00000000
    LI a7, 15
    ECALL
    J fm_next_row

fm_next_row:
    ADDI s0, s0, 1
    ADDI s1, s1, 30
    J fm_row_loop

fm_status_line:
    J fm_draw_status
fm_draw_done2:
    ; Fewer entries than rows: fall through to the status line.
fm_draw_status:
    LI a0, 120
    LI a1, 392
    LA a2, fm_status
    LI a3, 0xFFEF4444
    LI a4, 0x00000000
    LI a5, 560
    LI a6, 16
    LI a7, 43
    ECALL
    LI a0, 120
    LI a1, 414
    LA a2, str_fm_hint
    LI a3, 0xFF64748B
    LI a4, 0x00000000
    LI a5, 560
    LI a6, 16
    LI a7, 43
    ECALL
fm_draw_done:
    LD s3, 8(sp)
    LD s2, 16(sp)
    LD s1, 24(sp)
    LD s0, 32(sp)
    LD ra, 40(sp)
    ADDI sp, sp, 48
    RET

; fm_refresh: enumerate fm_cwd via FS_LIST into fm_list_buf (max 32).
; Preserves s0-s3. Clobbers a/t regs.
fm_refresh:
    ADDI sp, sp, -48
    SD ra, 40(sp)
    SD s0, 32(sp)
    SD s1, 24(sp)
    SD s2, 16(sp)
    SD s3, 8(sp)
    ; Lazy root init: an all-zero cwd behaves as "/" everywhere.
    LA t0, fm_cwd
    LBU t1, 0(t0)
    BNE t1, zero, fr_have_cwd
    LI t1, 47
    SB t1, 0(t0)
    SB zero, 1(t0)
fr_have_cwd:
    LI s0, 0
    LI s1, 0
fr_loop:
    LI t0, 32
    BGE s0, t0, fr_done
    LA a0, fm_cwd
    MV a1, s1
    LI t0, 28
    MUL t0, s0, t0
    LA t1, fm_list_buf
    ADD a2, t1, t0
    LI a7, 35
    ECALL
    BLT a0, zero, fr_done
    ADDI s1, s1, 1
    ; Skip "." and ".."
    LBU t2, 0(a2)
    LI t3, 46                ; '.'
    BNE t2, t3, fr_keep
    LBU t4, 1(a2)
    BEQ t4, zero, fr_loop    ; "." -> skip
    BNE t4, t3, fr_keep
    LBU t5, 2(a2)
    BEQ t5, zero, fr_loop    ; ".." -> skip
fr_keep:
    ADDI s0, s0, 1
    J fr_loop
fr_done:
    LA t0, fm_count
    SW s0, 0(t0)
    ; Clamp selection and scroll to the visible range.
    LA t0, fm_sel
    LW t1, 0(t0)
    LA t2, fm_cwd
    LBU t3, 0(t2)
    LI t4, 47
    BNE t3, t4, fr_nodot
    LBU t3, 1(t2)
    BEQ t3, zero, fr_nodot
    ADDI s0, s0, 1             ; ".." occupies position 0
fr_nodot:
    BEQ s0, zero, fr_empty
    BGE t1, s0, fr_sel_end
    BLT t1, zero, fr_sel_zero
    J fr_scroll
fr_sel_end:
    ADDI t1, s0, -1
    SW t1, 0(t0)
    J fr_scroll
fr_sel_zero:
    SW zero, 0(t0)
    J fr_scroll
fr_empty:
    SW zero, 0(t0)
fr_scroll:
    LA t0, fm_scroll
    LW t1, 0(t0)
    BLT t1, zero, fr_scr_zero
    LA t2, fm_sel
    LW t2, 0(t2)
    BLT t2, t1, fr_scr_sel
    ADDI t3, t1, 8
    BGE t2, t3, fr_scr_sel8
    J fr_ret
fr_scr_sel:
    SW t2, 0(t0)
    J fr_ret
fr_scr_sel8:
    ADDI t2, t2, -7
    SW t2, 0(t0)
    J fr_ret
fr_scr_zero:
    SW zero, 0(t0)
fr_ret:
    LD s3, 8(sp)
    LD s2, 16(sp)
    LD s1, 24(sp)
    LD s0, 32(sp)
    LD ra, 40(sp)
    ADDI sp, sp, 48
    RET

; fm_visible_count: -> a0 = rows incl. synthetic ".." when not at root.
fm_visible_count:
    LA t0, fm_count
    LW a0, 0(t0)
    LA t1, fm_cwd
    LBU t2, 0(t1)
    LI t3, 47
    BNE t2, t3, fvc_ret
    LBU t2, 1(t1)
    BEQ t2, zero, fvc_ret
    ADDI a0, a0, 1
fvc_ret:
    RET

; fm_row_entry: a0=visible row -> a0=cache index, or -1 for "..", -2 invalid.
fm_row_entry:
    LA t0, fm_cwd
    LBU t1, 0(t0)
    LI t2, 47
    BNE t1, t2, fre_plain
    LBU t1, 1(t0)
    BNE t1, zero, fre_dot
fre_plain:
    LA t1, fm_count
    LW t1, 0(t1)
    BGE a0, t1, fre_bad
    RET
fre_dot:
    BEQ a0, zero, fre_isdot
    ADDI a0, a0, -1
    LA t1, fm_count
    LW t1, 0(t1)
    BGE a0, t1, fre_bad
    RET
fre_isdot:
    LI a0, -1
    RET
fre_bad:
    LI a0, -2
    RET

; fm_select_row: a0=row -> clamp into range, store, redraw flag.
fm_select_row:
    ADDI sp, sp, -32
    SD ra, 24(sp)
    SD s0, 16(sp)
    MV s0, a0
    CALL fm_visible_count
    BEQ a0, zero, fsr_ret
    BLT s0, zero, fsr_zero
    BGE s0, a0, fsr_end
    LA t0, fm_sel
    SW s0, 0(t0)
    J fsr_scroll
fsr_end:
    ADDI t0, a0, -1
    LA t1, fm_sel
    SW t0, 0(t1)
    J fsr_scroll
fsr_zero:
    LA t0, fm_sel
    SW zero, 0(t0)
fsr_scroll:
    LA t0, fm_scroll
    LW t1, 0(t0)
    LA t2, fm_sel
    LW t2, 0(t2)
    BLT t2, t1, fsr_scr
    ADDI t3, t1, 8
    BGE t2, t3, fsr_scr8
    J fsr_flag
fsr_scr:
    SW t2, 0(t0)
    J fsr_flag
fsr_scr8:
    ADDI t2, t2, -7
    SW t2, 0(t0)
fsr_flag:
    CALL flag_redraw
fsr_ret:
    LD s0, 16(sp)
    LD ra, 24(sp)
    ADDI sp, sp, 32
    RET

fileman_on_key:
    ADDI sp, sp, -16
    SD ra, 8(sp)
    LI t0, 13
    BEQ a1, t0, fm_k_open
    LI t0, 256
    BEQ a1, t0, fm_k_up
    LI t0, 257
    BEQ a1, t0, fm_k_down
    LI t0, 273
    BEQ a1, t0, fm_k_pgup
    LI t0, 274
    BEQ a1, t0, fm_k_pgdn
    LI t0, 270
    BEQ a1, t0, fm_k_home
    LI t0, 271
    BEQ a1, t0, fm_k_end
    LI t0, 8
    BEQ a1, t0, fm_k_back
    LI t0, 265
    BEQ a1, t0, fm_k_refresh
    J fm_k_ret
fm_k_open:
    LA t0, fm_sel
    LW a0, 0(t0)
    CALL fm_open_index
    J fm_k_ret
fm_k_up:
    LA t0, fm_sel
    LW a0, 0(t0)
    ADDI a0, a0, -1
    CALL fm_select_row
    J fm_k_ret
fm_k_down:
    LA t0, fm_sel
    LW a0, 0(t0)
    ADDI a0, a0, 1
    CALL fm_select_row
    J fm_k_ret
fm_k_pgup:
    LA t0, fm_sel
    LW a0, 0(t0)
    ADDI a0, a0, -8
    CALL fm_select_row
    J fm_k_ret
fm_k_pgdn:
    LA t0, fm_sel
    LW a0, 0(t0)
    ADDI a0, a0, 8
    CALL fm_select_row
    J fm_k_ret
fm_k_home:
    LI a0, 0
    CALL fm_select_row
    J fm_k_ret
fm_k_end:
    CALL fm_visible_count
    ADDI a0, a0, -1
    CALL fm_select_row
    J fm_k_ret
fm_k_back:
    CALL fm_go_parent
    J fm_k_ret
fm_k_refresh:
    CALL fm_refresh
    CALL flag_redraw
fm_k_ret:
    LD ra, 8(sp)
    ADDI sp, sp, 16
    RET

; fm_go_parent: navigate fm_cwd up (stays at root), reset sel/scroll.
fm_go_parent:
    ADDI sp, sp, -16
    SD ra, 8(sp)
    LA a0, fm_cwd
    CALL path_parent
    LA t0, fm_sel
    SW zero, 0(t0)
    LA t0, fm_scroll
    SW zero, 0(t0)
    LA t0, fm_status
    SB zero, 0(t0)
    CALL flag_redraw
    LD ra, 8(sp)
    ADDI sp, sp, 16
    RET

fileman_on_click:
    ADDI sp, sp, -32
    SD ra, 24(sp)
    SD s0, 16(sp)
    ; [Up] button.
    LI t0, 570
    BLT a0, t0, fmc_rows
    LI t0, 680
    BGT a0, t0, fmc_rows
    LI t0, 96
    BLT a1, t0, fmc_rows
    LI t0, 120
    BGT a1, t0, fmc_rows
    CALL fm_go_parent
    J fmc_ret
fmc_rows:
    LI t0, 115
    BLT a0, t0, fmc_ret
    LI t0, 680
    BGT a0, t0, fmc_ret
    LI t0, 144
    BLT a1, t0, fmc_ret
    LI t0, 386
    BGT a1, t0, fmc_ret
    ADDI t0, a1, -144
    LI t1, 30
    DIVU t0, t0, t1          ; row 0..7
    LA t1, fm_scroll
    LW t1, 0(t1)
    ADD s0, t0, t1           ; visible position
    CALL fm_visible_count
    BGE s0, a0, fmc_ret
    ; Double-click: same row within 400 ms opens immediately.
    LI a7, 13
    ECALL
    MV t2, a0
    LA t3, fm_last_row
    LW t4, 0(t3)
    BNE t4, s0, fmc_single
    LA t3, fm_last_ms
    LD t5, 0(t3)
    SUB t5, t2, t5
    LI t6, 400
    BGT t5, t6, fmc_single
    SD zero, 0(t3)
    LA t3, fm_last_row
    LI t4, -1
    SW t4, 0(t3)
    MV a0, s0
    CALL fm_select_row
    MV a0, s0
    CALL fm_open_index
    J fmc_ret
fmc_single:
    LA t3, fm_last_ms
    SD t2, 0(t3)
    LA t3, fm_last_row
    SW s0, 0(t3)
    LI t0, 545
    BLT a0, t0, fmc_selonly
    MV a0, s0
    CALL fm_select_row
    MV a0, s0
    CALL fm_open_index
    J fmc_ret
fmc_selonly:
    MV a0, s0
    CALL fm_select_row
fmc_ret:
    LD s0, 16(sp)
    LD ra, 24(sp)
    ADDI sp, sp, 32
    RET

; fm_open_index: a0=visible row -> shared type dispatcher.
fm_open_index:
    ADDI sp, sp, -48
    SD ra, 40(sp)
    SD s0, 32(sp)
    SD s1, 24(sp)
    SD s2, 16(sp)
    MV s0, a0
    CALL fm_row_entry
    LI t0, -2
    BEQ a0, t0, foi_ret
    LI t0, -1
    BEQ a0, t0, foi_parent
    ; Cache entry -> full path in fm_tmp_path.
    LI t0, 28
    MUL t0, a0, t0
    LA t1, fm_list_buf
    ADD s1, t1, t0
    LBU t0, 13(s1)
    ANDI t0, t0, 0x10
    BNE t0, zero, foi_dir
    ; Regular file: compose path, then dispatch by extension.
    LA a0, fm_cwd
    MV a1, s1
    LA a2, fm_tmp_path
    LI a3, 256
    CALL path_join
    BLT a0, zero, foi_toolong
    MV s2, a0
    LA a0, fm_tmp_path
    LA a1, str_ext_app
    CALL ext_is
    BNE a0, zero, foi_app
    LA a0, fm_tmp_path
    LA a1, str_ext_txt
    CALL ext_is
    BNE a0, zero, foi_text
    LA a0, fm_status
    LA a1, str_fm_unsupported
    CALL str_copy
    CALL flag_redraw
    J foi_ret
foi_dir:
    LA a0, fm_cwd
    MV a1, s1
    LA a2, fm_tmp_path
    LI a3, 256
    CALL path_join
    BLT a0, zero, foi_toolong
    ; Validate before navigating.
    LA a0, fm_tmp_path
    LA a1, term_dirent
    LI a7, 32
    ECALL
    BLT a0, zero, foi_statbad
    LA a0, fm_cwd
    LA a1, fm_tmp_path
    CALL str_copy
    LA t0, fm_sel
    SW zero, 0(t0)
    LA t0, fm_scroll
    SW zero, 0(t0)
    LA t0, fm_status
    SB zero, 0(t0)
    CALL flag_redraw
    J foi_ret
foi_parent:
    CALL fm_go_parent
    J foi_ret
foi_app:
    MV a0, s2
    LA a0, fm_tmp_path
    CALL fs_launch_app
    J foi_ret
foi_text:
    LA a0, fm_tmp_path
    CALL notepad_open_path
    J foi_ret
foi_toolong:
    LA a0, fm_status
    LA a1, str_dlg_bad_name
    CALL str_copy
    CALL flag_redraw
    J foi_ret
foi_statbad:
    LA a0, fm_status
    LA a1, str_dlg_missing
    CALL str_copy
    CALL flag_redraw
foi_ret:
    LD s2, 16(sp)
    LD s1, 24(sp)
    LD s0, 32(sp)
    LD ra, 40(sp)
    ADDI sp, sp, 48
    RET

; fs_launch_app: a0=absolute DEXE path -> same validated loader as Terminal.
; Never routes executable bytes into Notepad. Desktop state is retained and
; the desktop is repainted when the standalone app exits.
fs_launch_app:
    ADDI sp, sp, -32
    SD ra, 24(sp)
    SD s0, 16(sp)
    MV s0, a0
    LI a1, 0
    LI a7, 40
    ECALL
    BLT a0, zero, fla_bad
    CALL flag_redraw
    J fla_ret
fla_bad:
    LA a0, fm_status
    LA a1, str_fm_launch_err
    CALL str_copy
    CALL flag_redraw
fla_ret:
    LD s0, 16(sp)
    LD ra, 24(sp)
    ADDI sp, sp, 32
    RET

; Legacy entry kept for compatibility; now routes through the dispatcher.
fm_open_row_notepad:
    ADDI sp, sp, -16
    SD ra, 8(sp)
    CALL fm_open_index
    LD ra, 8(sp)
    ADDI sp, sp, 16
    RET


; ==============================================================================
; Application 4: Modern Pixel Paint (TrueColor Canvas & Smooth Brushes)
; ==============================================================================
paint_init:
    ADDI sp, sp, -16
    SD ra, 8(sp)

    LA t0, paint_color
    LI t1, 0xFFFF3B30        ; Default red
    SW t1, 0(t0)

    LA t0, paint_radius
    LI t1, 3                 ; Default 3px
    SW t1, 0(t0)

    LA t0, paint_eraser
    SW zero, 0(t0)

    CALL paint_clear_canvas

    LA t0, paint_inited
    LI t1, 1
    SW t1, 0(t0)

    LD ra, 8(sp)
    ADDI sp, sp, 16
    RET

paint_clear_canvas:
    LI a0, 0x02400000        ; Canvas memory base
    LI a1, 0xFFFFFFFF        ; White pixels
    LI a2, 256000            ; 640 * 400 pixels
    LI a7, 28
    ECALL                    ; SYS_MEMSET
    RET

paint_draw:
    ADDI sp, sp, -16
    SD ra, 8(sp)

    ; Window frame (X=60, Y=35, W=680, H=520)
    LI a0, 60
    LI a1, 35
    LI a2, 680
    LI a3, 520
    LA a4, str_paint_title
    CALL draw_window_frame

    ; Toolbar Background: X=60, Y=67, W=680, H=48, 0xFF2D3748
    LI a0, 60
    LI a1, 67
    LI a2, 680
    LI a3, 48
    LI a4, 0xFF2D3748
    LI a7, 14
    ECALL

    ; 9 Swatches (24x24 each)
    ; Black, White, Red, Green, Blue, Yellow, Orange, Purple, Cyan
    LI a0, 75
    LI a1, 79
    LI a2, 24
    LI a3, 24
    LI a4, 0xFF000000
    LI a7, 14
    ECALL
    LI a0, 105
    LI a1, 79
    LI a2, 24
    LI a3, 24
    LI a4, 0xFFFFFFFF
    LI a7, 14
    ECALL
    LI a0, 135
    LI a1, 79
    LI a2, 24
    LI a3, 24
    LI a4, 0xFFFF3B30
    LI a7, 14
    ECALL
    LI a0, 165
    LI a1, 79
    LI a2, 24
    LI a3, 24
    LI a4, 0xFF34C759
    LI a7, 14
    ECALL
    LI a0, 195
    LI a1, 79
    LI a2, 24
    LI a3, 24
    LI a4, 0xFF007AFF
    LI a7, 14
    ECALL
    LI a0, 225
    LI a1, 79
    LI a2, 24
    LI a3, 24
    LI a4, 0xFFFFCC00
    LI a7, 14
    ECALL
    LI a0, 255
    LI a1, 79
    LI a2, 24
    LI a3, 24
    LI a4, 0xFFFF9500
    LI a7, 14
    ECALL
    LI a0, 285
    LI a1, 79
    LI a2, 24
    LI a3, 24
    LI a4, 0xFFAF52DE
    LI a7, 14
    ECALL
    LI a0, 315
    LI a1, 79
    LI a2, 24
    LI a3, 24
    LI a4, 0xFF5AC8FA
    LI a7, 14
    ECALL

    ; Brushes: [ 1px ] [ 3px ] [ 5px ]
    LI a0, 355
    LI a1, 79
    LI a2, 40
    LI a3, 24
    LI a4, 0xFF334155
    LI a7, 14
    ECALL
    LI a0, 362
    LI a1, 83
    LA a2, str_b1
    LI a3, 0xFFFFFFFF
    LI a4, 0
    LI a7, 15
    ECALL

    LI a0, 400
    LI a1, 79
    LI a2, 40
    LI a3, 24
    LI a4, 0xFF334155
    LI a7, 14
    ECALL
    LI a0, 407
    LI a1, 83
    LA a2, str_b3
    LI a3, 0xFFFFFFFF
    LI a4, 0
    LI a7, 15
    ECALL

    LI a0, 445
    LI a1, 79
    LI a2, 40
    LI a3, 24
    LI a4, 0xFF334155
    LI a7, 14
    ECALL
    LI a0, 452
    LI a1, 83
    LA a2, str_b5
    LI a3, 0xFFFFFFFF
    LI a4, 0
    LI a7, 15
    ECALL

    ; [ Eraser ]
    LI a0, 495
    LI a1, 79
    LI a2, 68
    LI a3, 24
    LI a4, 0xFF475569
    LI a7, 14
    ECALL
    LI a0, 502
    LI a1, 83
    LA a2, str_eraser
    LI a3, 0xFFFFFFFF
    LI a4, 0
    LI a7, 15
    ECALL

    ; [ Clear ]
    LI a0, 570
    LI a1, 79
    LI a2, 60
    LI a3, 24
    LI a4, 0xFFDC2626
    LI a7, 14
    ECALL
    LI a0, 578
    LI a1, 83
    LA a2, str_clear
    LI a3, 0xFFFFFFFF
    LI a4, 0
    LI a7, 15
    ECALL

    ; Canvas border: X=78, Y=123, W=644, H=404, 0xFF334155
    LI a0, 78
    LI a1, 123
    LI a2, 644
    LI a3, 404
    LI a4, 0xFF334155
    LI a7, 14
    ECALL

    ; Check if canvas was already initialized
    LA t0, paint_inited
    LW t1, 0(t0)
    BNE t1, zero, p_do_blit

    ; First time: fast native white fill
    LI a0, 80
    LI a1, 125
    LI a2, 640
    LI a3, 400
    LI a4, 0xFFFFFFFF
    LI a7, 14
    ECALL

    LI t1, 1
    SW t1, 0(t0)
    J p_draw_done

p_do_blit:
    ; Blit Canvas backing buffer (0x02400000) directly to LFB (0x02000000)
    ; Canvas: 640 wide x 400 high at (80, 125)
    LI a0, 80
    LI a1, 125
    LI a2, 640
    LI a3, 400
    LI a4, 0x02400000
    LI a7, 27
    ECALL                    ; SYS_GUI_BLIT
    J p_draw_done

p_draw_done:
    LD ra, 8(sp)
    ADDI sp, sp, 16
    RET

paint_on_click:
    ADDI sp, sp, -16
    SD ra, 8(sp)

    ; a0=X, a1=Y, a2=button
    ; Check toolbar clicks (Y in 79..103)
    LI t0, 79
    BLT a1, t0, p_chk_canvas
    LI t0, 103
    BGT a1, t0, p_chk_canvas

    ; Swatches
    LI t0, 75
    LI t1, 99
    BLT a0, t0, p_chk_brushes
    BLE a0, t1, sel_c0
    LI t0, 105
    LI t1, 129
    BLT a0, t0, p_chk_brushes
    BLE a0, t1, sel_c1
    LI t0, 135
    LI t1, 159
    BLT a0, t0, p_chk_brushes
    BLE a0, t1, sel_c2
    LI t0, 165
    LI t1, 189
    BLT a0, t0, p_chk_brushes
    BLE a0, t1, sel_c3
    LI t0, 195
    LI t1, 219
    BLT a0, t0, p_chk_brushes
    BLE a0, t1, sel_c4
    LI t0, 225
    LI t1, 249
    BLT a0, t0, p_chk_brushes
    BLE a0, t1, sel_c5
    LI t0, 255
    LI t1, 279
    BLT a0, t0, p_chk_brushes
    BLE a0, t1, sel_c6
    LI t0, 285
    LI t1, 309
    BLT a0, t0, p_chk_brushes
    BLE a0, t1, sel_c7
    LI t0, 315
    LI t1, 339
    BLT a0, t0, p_chk_brushes
    BLE a0, t1, sel_c8

p_chk_brushes:
    ; 1px (355..395)
    LI t0, 355
    LI t1, 395
    BLT a0, t0, p_chk_b3
    BLE a0, t1, sel_b1
p_chk_b3:
    LI t0, 400
    LI t1, 440
    BLT a0, t0, p_chk_b5
    BLE a0, t1, sel_b3
p_chk_b5:
    LI t0, 445
    LI t1, 485
    BLT a0, t0, p_chk_eraser
    BLE a0, t1, sel_b5
p_chk_eraser:
    LI t0, 495
    LI t1, 563
    BLT a0, t0, p_chk_clear
    BLE a0, t1, sel_eraser
p_chk_clear:
    LI t0, 570
    LI t1, 630
    BLT a0, t0, p_chk_canvas
    BLE a0, t1, sel_clear

p_chk_canvas:
    ; Canvas click / paint: X in 80..720, Y in 125..525
    LI t0, 80
    BLT a0, t0, p_click_ret
    LI t0, 720
    BGT a0, t0, p_click_ret
    LI t0, 125
    BLT a1, t0, p_click_ret
    LI t0, 525
    BGT a1, t0, p_click_ret

    CALL paint_apply_brush
    LI a7, 12
    ECALL                    ; SYS_GUI_FLUSH
p_click_ret:
    LD ra, 8(sp)
    ADDI sp, sp, 16
    RET

paint_on_drag:
    ADDI sp, sp, -16
    SD ra, 8(sp)
    ; a0=X, a1=Y
    LI t0, 80
    BLT a0, t0, p_drag_ret
    LI t0, 720
    BGT a0, t0, p_drag_ret
    LI t0, 125
    BLT a1, t0, p_drag_ret
    LI t0, 525
    BGT a1, t0, p_drag_ret
    CALL paint_apply_brush
    LI a7, 12
    ECALL                    ; SYS_GUI_FLUSH
p_drag_ret:
    LD ra, 8(sp)
    ADDI sp, sp, 16
    RET

paint_on_key:
    ADDI sp, sp, -16
    SD ra, 8(sp)
    LI t0, 99                ; 'c'
    BEQ a1, t0, p_k_clr
    LI t0, 67                ; 'C'
    BEQ a1, t0, p_k_clr
    J p_k_ret
p_k_clr:
    CALL paint_clear_canvas_white
p_k_ret:
    LD ra, 8(sp)
    ADDI sp, sp, 16
    RET

sel_c0:
    LI t2, 0xFF000000
    J set_p_color
sel_c1:
    LI t2, 0xFFFFFFFF
    J set_p_color
sel_c2:
    LI t2, 0xFFFF3B30
    J set_p_color
sel_c3:
    LI t2, 0xFF34C759
    J set_p_color
sel_c4:
    LI t2, 0xFF007AFF
    J set_p_color
sel_c5:
    LI t2, 0xFFFFCC00
    J set_p_color
sel_c6:
    LI t2, 0xFFFF9500
    J set_p_color
sel_c7:
    LI t2, 0xFFAF52DE
    J set_p_color
sel_c8:
    LI t2, 0xFF5AC8FA
    J set_p_color

set_p_color:
    LA t0, paint_color
    SW t2, 0(t0)
    LA t0, paint_eraser
    SW zero, 0(t0)
    CALL flag_redraw
    J p_click_ret

sel_b1:
    LI t2, 1
    LA t0, paint_radius
    SW t2, 0(t0)
    J p_click_ret
sel_b3:
    LI t2, 3
    LA t0, paint_radius
    SW t2, 0(t0)
    J p_click_ret
sel_b5:
    LI t2, 5
    LA t0, paint_radius
    SW t2, 0(t0)
    J p_click_ret

sel_eraser:
    LA t0, paint_eraser
    LI t1, 1
    SW t1, 0(t0)
    CALL flag_redraw
    J p_click_ret

sel_clear:
    CALL paint_clear_canvas_white
    J p_click_ret

paint_clear_canvas_white:
    ADDI sp, sp, -16
    SD ra, 8(sp)
    CALL paint_clear_canvas

    LI a0, 80
    LI a1, 125
    LI a2, 640
    LI a3, 400
    LI a4, 0xFFFFFFFF
    LI a7, 14
    ECALL                    ; fast white fill of canvas in LFB

    LI a7, 12
    ECALL                    ; SYS_GUI_FLUSH
    LD ra, 8(sp)
    ADDI sp, sp, 16
    RET

; Apply brush at (a0=X, a1=Y)
paint_apply_brush:
    ADDI sp, sp, -32
    SD ra, 24(sp)
    SD s0, 16(sp)
    SD s1, 8(sp)

    ; Relative coords inside 640x400 canvas:
    ADDI s0, a0, -80         ; cx
    ADDI s1, a1, -125        ; cy

    LA t0, paint_eraser
    LW t1, 0(t0)
    BNE t1, zero, p_use_white
    LA t0, paint_color
    LW t2, 0(t0)
    J p_got_col
p_use_white:
    LI t2, 0xFFFFFFFF
p_got_col:

    LA t0, paint_radius
    LW t3, 0(t0)             ; rad (1, 3, or 5)

    ; Draw square/circle of radius in backing buffer and VRAM
    NEG t4, t3               ; dy = -rad
p_dy_loop:
    BGT t4, t3, p_brush_done
    NEG t5, t3               ; dx = -rad
p_dx_loop:
    BGT t5, t3, p_next_dy

    ADD t6, s0, t5           ; px = cx + dx
    BLT t6, zero, p_next_dx
    LI a2, 640
    BGE t6, a2, p_next_dx

    ADD a3, s1, t4           ; py = cy + dy
    BLT a3, zero, p_next_dx
    LI a2, 400
    BGE a3, a2, p_next_dx

    ; Store in canvas buffer at 0x02400000 + (py * 640 + px) * 4
    LI a2, 640
    MUL a3, a3, a2
    ADD a3, a3, t6
    SLLI a3, a3, 2
    LI a2, 0x02400000
    ADD a3, a3, a2
    SW t2, 0(a3)

    ; Store directly to LFB at 0x02000000 + ((125 + s1 + dy) * 800 + (80 + s0 + dx)) * 4
    ADDI a4, s1, 125
    ADD a4, a4, t4
    LI a2, 800
    MUL a4, a4, a2
    ADDI a5, s0, 80
    ADD a4, a4, a5
    ADD a4, a4, t5
    SLLI a4, a4, 2
    LI a2, 0x02000000
    ADD a4, a4, a2
    SW t2, 0(a4)

p_next_dx:
    ADDI t5, t5, 1
    J p_dx_loop

p_next_dy:
    ADDI t4, t4, 1
    J p_dy_loop

p_brush_done:
    LD s1, 8(sp)
    LD s0, 16(sp)
    LD ra, 24(sp)
    ADDI sp, sp, 32
    RET

; ==============================================================================
; Application 5: System Info & Monitor
; ==============================================================================
sysinfo_draw:
    ADDI sp, sp, -16
    SD ra, 8(sp)

    ; Window frame (X=120, Y=70, W=560, H=440)
    LI a0, 120
    LI a1, 70
    LI a2, 560
    LI a3, 440
    LA a4, str_info_title
    CALL draw_window_frame

    LI a0, 150
    LI a1, 120
    LA a2, str_info_cpu
    LI a3, 0xFF38BDF8
    LI a4, 0
    LI a7, 15
    ECALL
    LI a0, 150
    LI a1, 150
    LA a2, str_info_ram
    LI a3, 0xFF34C759
    LI a4, 0
    LI a7, 15
    ECALL
    LI a0, 150
    LI a1, 180
    LA a2, str_info_gpu
    LI a3, 0xFFFFCC00
    LI a4, 0
    LI a7, 15
    ECALL
    LI a0, 150
    LI a1, 210
    LA a2, str_info_fs
    LI a3, 0xFFDB2777
    LI a4, 0
    LI a7, 15
    ECALL

    ; Monotonic ticks
    LI a0, 150
    LI a1, 250
    LA a2, str_info_ticks
    LI a3, 0xFFE2E8F0
    LI a4, 0
    LI a7, 15
    ECALL
    LA t0, isr_ticks
    LD a0, 0(t0)
    LA a1, num_tmp
    CALL num_to_dec
    LI a0, 360
    LI a1, 250
    LA a2, num_tmp
    LI a3, 0xFF38BDF8
    LI a4, 0
    LI a7, 15
    ECALL

    ; Worker ticks
    LI a0, 150
    LI a1, 280
    LA a2, str_info_worker
    LI a3, 0xFFE2E8F0
    LI a4, 0
    LI a7, 15
    ECALL
    LA t0, worker_ticks
    LD a0, 0(t0)
    LA a1, num_tmp
    CALL num_to_dec
    LI a0, 360
    LI a1, 280
    LA a2, num_tmp
    LI a3, 0xFF34C759
    LI a4, 0
    LI a7, 15
    ECALL

    ; Visual memory bar (X=150, Y=330, W=400, H=24)
    LI a0, 150
    LI a1, 330
    LI a2, 400
    LI a3, 24
    LI a4, 0xFF334155
    LI a7, 14
    ECALL
    LI a0, 150
    LI a1, 330
    LI a2, 120
    LI a3, 24
    LI a4, 0xFF10B981
    LI a7, 14
    ECALL
    LI a0, 150
    LI a1, 360
    LA a2, str_info_bar
    LI a3, 0xFF94A3B8
    LI a4, 0
    LI a7, 15
    ECALL

    LD ra, 8(sp)
    ADDI sp, sp, 16
    RET

sysinfo_on_click:
    RET
sysinfo_on_key:
    RET

; ==============================================================================
; Application 6: Snake Game (Integrated with Bugfix & 800x600 LFB)
; ==============================================================================
snake_init:
    LA t0, snake_len
    LI t1, 4
    SW t1, 0(t0)
    LA t0, snake_dir
    SW zero, 0(t0)
    LA t0, snake_score
    SW zero, 0(t0)
    LA t0, snake_gameover
    SW zero, 0(t0)

    LA t0, snake_x
    LI t1, 10
    SB t1, 0(t0)
    LI t1, 9
    SB t1, 1(t0)
    LI t1, 8
    SB t1, 2(t0)
    LI t1, 7
    SB t1, 3(t0)

    LA t0, snake_y
    LI t1, 5
    SB t1, 0(t0)
    SB t1, 1(t0)
    SB t1, 2(t0)
    SB t1, 3(t0)

    LA t0, food_x
    LI t1, 20
    SW t1, 0(t0)
    LA t0, food_y
    LI t1, 10
    SW t1, 0(t0)
    RET

snake_draw:
    ADDI sp, sp, -32
    SD ra, 24(sp)
    SD s0, 16(sp)
    SD s1, 8(sp)

    ; Window frame (X=120, Y=50, W=560, H=490)
    LI a0, 120
    LI a1, 50
    LI a2, 560
    LI a3, 490
    LA a4, str_snake_wtitle
    CALL draw_window_frame

    ; Score text
    LI a0, 140
    LI a1, 90
    LA a2, str_score_lbl
    LI a3, 0xFF38BDF8
    LI a4, 0
    LI a7, 15
    ECALL

    LA t0, snake_score
    LW a0, 0(t0)
    LA a1, num_tmp
    CALL num_to_dec
    LI a0, 200
    LI a1, 90
    LA a2, num_tmp
    LI a3, 0xFF34C759
    LI a4, 0
    LI a7, 15
    ECALL

    ; Controls hint
    LI a0, 310
    LI a1, 90
    LA a2, str_controls
    LI a3, 0xFF94A3B8
    LI a4, 0
    LI a7, 15
    ECALL

    ; Board background (X=160, Y=120, W=480, H=400, cells: 30x20 of 16x20 px)
    LI a0, 158
    LI a1, 118
    LI a2, 484
    LI a3, 404
    LI a4, 0xFF334155
    LI a7, 14
    ECALL

    LI a0, 160
    LI a1, 120
    LI a2, 480
    LI a3, 400
    LI a4, 0xFF0F172A
    LI a7, 14
    ECALL

    ; Food
    LA t0, food_x
    LW t1, 0(t0)
    SLLI t1, t1, 4
    ADDI t1, t1, 162
    LA t0, food_y
    LW t2, 0(t0)
    LI t3, 20
    MUL t2, t2, t3
    ADDI t2, t2, 122
    MV a0, t1
    MV a1, t2
    LI a2, 12
    LI a3, 16
    LI a4, 0xFFFF3B30
    LI a7, 14
    ECALL

    ; Snake Segments
    LA t0, snake_len
    LW s0, 0(t0)
    LI s1, 0
snk_os_seg_loop:
    BGE s1, s0, snk_os_chk_go
    LA t0, snake_x
    ADD t0, t0, s1
    LBU t1, 0(t0)
    SLLI t1, t1, 4
    ADDI t1, t1, 161
    LA t0, snake_y
    ADD t0, t0, s1
    LBU t2, 0(t0)
    LI t3, 20
    MUL t2, t2, t3
    ADDI t2, t2, 121

    MV a0, t1
    MV a1, t2
    LI a2, 14
    LI a3, 18
    BEQ s1, zero, snk_os_head_col
    LI a4, 0xFF34C759
    J snk_os_draw_it
snk_os_head_col:
    LI a4, 0xFFFFCC00
snk_os_draw_it:
    LI a7, 14
    ECALL
    ADDI s1, s1, 1
    J snk_os_seg_loop

snk_os_chk_go:
    LA t0, snake_gameover
    LW t1, 0(t0)
    BEQ t1, zero, snk_os_draw_done

    ; GAME OVER Dialog Box (center: X=240, Y=250, W=320, H=120)
    LI a0, 240
    LI a1, 250
    LI a2, 320
    LI a3, 120
    LI a4, 0xEE1E222D
    LI a7, 14
    ECALL
    LI a0, 240
    LI a1, 250
    LI a2, 320
    LI a3, 4
    LI a4, 0xFFFF3B30
    LI a7, 14
    ECALL

    LI a0, 344
    LI a1, 270
    LA a2, str_gameover
    LI a3, 0xFFFF3B30
    LI a4, 0
    LI a7, 15
    ECALL

    LI a0, 290
    LI a1, 305
    LA a2, str_restart
    LI a3, 0xFFFFFFFF
    LI a4, 0
    LI a7, 15
    ECALL

    LI a0, 305
    LI a1, 335
    LA a2, str_quit
    LI a3, 0xFF94A3B8
    LI a4, 0
    LI a7, 15
    ECALL

snk_os_draw_done:
    LD s1, 8(sp)
    LD s0, 16(sp)
    LD ra, 24(sp)
    ADDI sp, sp, 32
    RET

snake_step:
    ADDI sp, sp, -32
    SD ra, 24(sp)
    SD s0, 16(sp)
    SD s1, 8(sp)

    LA t0, snake_gameover
    LW t1, 0(t0)
    BNE t1, zero, snk_os_s_ret

    LA t0, snake_dir
    LW t0, 0(t0)
    LA t1, snake_x
    LBU t1, 0(t1)
    LA t2, snake_y
    LBU t2, 0(t2)

    LI t3, 0
    BEQ t0, t3, snk_os_r
    LI t3, 1
    BEQ t0, t3, snk_os_d
    LI t3, 2
    BEQ t0, t3, snk_os_l
    ADDI t2, t2, -1
    J snk_os_chk_w
snk_os_r:
    ADDI t1, t1, 1
    J snk_os_chk_w
snk_os_d:
    ADDI t2, t2, 1
    J snk_os_chk_w
snk_os_l:
    ADDI t1, t1, -1

snk_os_chk_w:
    BLT t1, zero, snk_os_die
    LI t3, 30
    BGE t1, t3, snk_os_die
    BLT t2, zero, snk_os_die
    LI t3, 20
    BGE t2, t3, snk_os_die

    ; Check self collision
    LA t3, snake_len
    LW s0, 0(t3)
    LI t4, 128
    BGE t4, s0, snk_os_lok
    LI s0, 128
    SW s0, 0(t3)
snk_os_lok:
    LI t4, 0
snk_os_self_l:
    BGE t4, s0, snk_os_shift
    LA t5, snake_x
    ADD t5, t5, t4
    LBU t5, 0(t5)
    BNE t5, t1, snk_os_s_next
    LA t5, snake_y
    ADD t5, t5, t4
    LBU t5, 0(t5)
    BEQ t5, t2, snk_os_die
snk_os_s_next:
    ADDI t4, t4, 1
    J snk_os_self_l

snk_os_shift:
    ADDI t4, s0, -1
snk_os_sh_l:
    BEQ t4, zero, snk_os_sh_done
    ADDI t5, t4, -1
    LA t6, snake_x
    ADD t6, t6, t5
    LBU t6, 0(t6)
    LA t3, snake_x
    ADD t3, t3, t4
    SB t6, 0(t3)

    LA t6, snake_y
    ADD t6, t6, t5
    LBU t6, 0(t6)
    LA t3, snake_y
    ADD t3, t3, t4
    SB t6, 0(t3)

    ADDI t4, t4, -1
    J snk_os_sh_l

snk_os_sh_done:
    LA t3, snake_x
    SB t1, 0(t3)
    LA t3, snake_y
    SB t2, 0(t3)

    ; Check food
    LA t3, food_x
    LW t4, 0(t3)
    BNE t1, t4, snk_os_s_ret
    LA t3, food_y
    LW t4, 0(t3)
    BNE t2, t4, snk_os_s_ret

    ; Eaten!
    LA t3, snake_score
    LW t4, 0(t3)
    ADDI t4, t4, 10
    SW t4, 0(t3)

    ; Food eating chirp (freq=1200, dur=60, wave=0, vol=200)
    LI a0, 1200
    LI a1, 60
    LI a2, 0
    LI a3, 200
    CALL sound_play_tone

    LA t3, snake_len
    LW t4, 0(t3)
    LI t5, 120
    BGE t4, t5, snk_os_new_food
    ADDI t4, t4, 1
    SW t4, 0(t3)

snk_os_new_food:
    LI a7, 13
    ECALL
    MV t3, a0
    LI t4, 7
    MUL t3, t3, t4
    ADDI t3, t3, 11
    LI t4, 30
    REM t3, t3, t4
    LA t4, food_x
    SW t3, 0(t4)

    LI a7, 13
    ECALL
    LI t4, 3
    MUL t3, a0, t4
    ADDI t3, t3, 5
    LI t4, 20
    REM t3, t3, t4
    LA t4, food_y
    SW t3, 0(t4)
    J snk_os_s_ret

snk_os_die:
    LA t0, snake_gameover
    LI t1, 1
    SW t1, 0(t0)
    ; Collision / game over buzz (freq=150, dur=250, wave=2, vol=220)
    LI a0, 150
    LI a1, 250
    LI a2, 2
    LI a3, 220
    CALL sound_play_tone
    LI a7, 12
    ECALL                    ; SYS_GUI_FLUSH

snk_os_s_ret:
    LD s1, 8(sp)
    LD s0, 16(sp)
    LD ra, 24(sp)
    ADDI sp, sp, 32
    RET

snake_on_timer:
    ADDI sp, sp, -16
    SD ra, 8(sp)
    LA t0, snake_gameover
    LW t1, 0(t0)
    BNE t1, zero, snk_t_done

    LA t0, snake_tick
    LW t1, 0(t0)
    ADDI t1, t1, 1
    SW t1, 0(t0)
    LI t2, 3                 ; step every 3 ticks (150 ms)
    BLT t1, t2, snk_t_done
    SW zero, 0(t0)

    CALL snake_step
    CALL flag_redraw

snk_t_done:
    LD ra, 8(sp)
    ADDI sp, sp, 16
    RET

snake_on_key:
    ADDI sp, sp, -16
    SD ra, 8(sp)

    ; a1 = keycode
    LA t0, snake_gameover
    LW t1, 0(t0)
    BNE t1, zero, snk_k_dead

    LI t0, 256
    BEQ a1, t0, snk_ku
    LI t0, 119
    BEQ a1, t0, snk_ku
    LI t0, 257
    BEQ a1, t0, snk_kd
    LI t0, 115
    BEQ a1, t0, snk_kd
    LI t0, 258
    BEQ a1, t0, snk_kl
    LI t0, 97
    BEQ a1, t0, snk_kl
    LI t0, 259
    BEQ a1, t0, snk_kr
    LI t0, 100
    BEQ a1, t0, snk_kr
    J snk_k_ret

snk_ku:
    LA t0, snake_dir
    LW t1, 0(t0)
    LI t2, 1
    BEQ t1, t2, snk_k_ret
    LI t1, 3
    SW t1, 0(t0)
    CALL flag_redraw
    J snk_k_ret
snk_kd:
    LA t0, snake_dir
    LW t1, 0(t0)
    LI t2, 3
    BEQ t1, t2, snk_k_ret
    LI t1, 1
    SW t1, 0(t0)
    CALL flag_redraw
    J snk_k_ret
snk_kl:
    LA t0, snake_dir
    LW t1, 0(t0)
    LI t2, 0
    BEQ t1, t2, snk_k_ret
    LI t1, 2
    SW t1, 0(t0)
    CALL flag_redraw
    J snk_k_ret
snk_kr:
    LA t0, snake_dir
    LW t1, 0(t0)
    LI t2, 2
    BEQ t1, t2, snk_k_ret
    LI t1, 0
    SW t1, 0(t0)
    CALL flag_redraw
    J snk_k_ret

snk_k_dead:
    LI t0, 114
    BEQ a1, t0, snk_restart
    LI t0, 82
    BEQ a1, t0, snk_restart
    J snk_k_ret
snk_restart:
    CALL snake_init
    CALL flag_redraw
snk_k_ret:
    LD ra, 8(sp)
    ADDI sp, sp, 16
    RET

snake_on_click:
    RET

; ==============================================================================
; Application 7: Terminal CLI
; ==============================================================================
terminal_init:
    LA t0, term_len
    SW zero, 0(t0)
    LA t0, term_lines_count
    SW zero, 0(t0)
    RET

terminal_draw:
    ADDI sp, sp, -16
    SD ra, 8(sp)

    ; Window frame (X=100, Y=60, W=600, H=460)
    LI a0, 100
    LI a1, 60
    LI a2, 600
    LI a3, 460
    LA a4, str_term_title
    CALL draw_window_frame

    ; Terminal Black Screen: X=110, Y=100, W=580, H=400
    LI a0, 110
    LI a1, 100
    LI a2, 580
    LI a3, 400
    LI a4, 0xFF000000        ; Black
    LI a7, 14
    ECALL

    ; Welcome Banner
    LI a0, 120
    LI a1, 110
    LA a2, str_term_banner1
    LI a3, 0xFF34C759        ; Green
    LI a4, 0x00000000
    LI a7, 15
    ECALL

    LI a0, 120
    LI a1, 130
    LA a2, str_term_banner2
    LI a3, 0xFF94A3B8        ; Gray
    LI a4, 0x00000000
    LI a7, 15
    ECALL

    ; Prompt
    LI a0, 120
    LI a1, 160
    LA a2, str_term_prompt
    LI a3, 0xFF38BDF8        ; Cyan
    LI a4, 0x00000000
    LI a7, 15
    ECALL

    ; Input line
    LI a0, 208
    LI a1, 160
    LA a2, term_buf
    LI a3, 0xFFFFFFFF        ; White
    LI a4, 0x00000000
    LI a7, 15
    ECALL

    ; Cursor
    LA t0, term_len
    LW t1, 0(t0)
    SLLI t1, t1, 3           ; * 8
    ADDI a0, t1, 208
    LI a1, 160
    LI a2, 2
    LI a3, 16
    LI a4, 0xFF38BDF8
    LI a7, 14
    ECALL

    ; Status or output line
    LI a0, 120
    LI a1, 190
    LA a2, term_out_buf
    LI a3, 0xFFF59E0B        ; Amber output
    LI a4, 0x00000000
    LI a7, 15
    ECALL

    LD ra, 8(sp)
    ADDI sp, sp, 16
    RET

terminal_on_key:
    ADDI sp, sp, -16
    SD ra, 8(sp)

    ; a1 = key
    LI t0, 8                 ; Backspace
    BEQ a1, t0, tm_bksp
    LI t0, 13                ; Enter
    BEQ a1, t0, tm_enter
    LI t0, 10
    BEQ a1, t0, tm_enter

    ; Printable
    LI t0, 32
    BLT a1, t0, tm_ret
    LI t0, 126
    BGT a1, t0, tm_ret

    LA t0, term_len
    LW t1, 0(t0)
    LI t2, 50
    BGE t1, t2, tm_ret
    LA t2, term_buf
    ADD t2, t2, t1
    SB a1, 0(t2)
    ADDI t1, t1, 1
    SW t1, 0(t0)
    ADDI t2, t2, 1
    SB zero, 0(t2)
    CALL flag_redraw
    J tm_ret

tm_bksp:
    LA t0, term_len
    LW t1, 0(t0)
    BEQ t1, zero, tm_ret
    ADDI t1, t1, -1
    SW t1, 0(t0)
    LA t2, term_buf
    ADD t2, t2, t1
    SB zero, 0(t2)
    CALL flag_redraw
    J tm_ret

tm_enter:
    ; Process terminal command
    CALL term_exec_cmd
    LA t0, term_len
    SW zero, 0(t0)
    LA t0, term_buf
    SB zero, 0(t0)
    CALL flag_redraw
    J tm_ret

tm_ret:
    LD ra, 8(sp)
    ADDI sp, sp, 16
    RET

term_exec_cmd:
    ADDI sp, sp, -16
    SD ra, 8(sp)

    LA a0, term_buf
    LA a1, str_cmd_help
    CALL str_eq
    BNE a0, zero, tcmd_help

    LA a0, term_buf
    LA a1, str_cmd_info
    CALL str_eq
    BNE a0, zero, tcmd_info

    LA a0, term_buf
    LA a1, str_cmd_clear
    CALL str_eq
    BNE a0, zero, tcmd_clear

    LA a0, term_buf
    LA a1, str_cmd_exit
    CALL str_eq
    BNE a0, zero, tcmd_exit

    LA a0, term_buf
    LA a1, str_cmd_pwd
    CALL str_eq
    BNE a0, zero, tcmd_pwd

    LA a0, term_buf
    LA a1, str_cmd_ls
    CALL str_eq
    BNE a0, zero, tcmd_ls

    ; Prefix command: run PATH
    LA t0, term_buf
    LBU t1, 0(t0)
    LI t2, 114
    BNE t1, t2, tcmd_unknown
    LBU t1, 1(t0)
    LI t2, 117
    BNE t1, t2, tcmd_unknown
    LBU t1, 2(t0)
    LI t2, 110
    BNE t1, t2, tcmd_unknown
    LBU t1, 3(t0)
    LI t2, 32
    BNE t1, t2, tcmd_unknown
    ADDI a0, t0, 4
    LI a1, 0
    LI a7, 40
    ECALL
    BLT a0, zero, tcmd_run_error
    LA a0, term_out_buf
    LA a1, str_term_run_ok
    CALL str_copy
    J tcmd_done
tcmd_run_error:
    LA a0, term_out_buf
    LA a1, str_term_run_error
    CALL str_copy
    J tcmd_done

tcmd_unknown:
    LA a0, term_out_buf
    LA a1, str_term_unknown
    CALL str_copy
    J tcmd_done

tcmd_help:
    LA a0, term_out_buf
    LA a1, str_term_help_out
    CALL str_copy
    J tcmd_done
tcmd_info:
    LA a0, term_out_buf
    LA a1, str_term_info_out
    CALL str_copy
    J tcmd_done
tcmd_clear:
    LA a0, term_out_buf
    SB zero, 0(a0)
    J tcmd_done
tcmd_exit:
    CALL window_close
    J tcmd_done
tcmd_pwd:
    LA a0, term_out_buf
    LA a1, str_root_path
    CALL str_copy
    J tcmd_done
tcmd_ls:
    LA a0, str_root_path
    LI a1, 0
    LA a2, term_dirent
    LI a7, 35
    ECALL
    BLT a0, zero, tcmd_ls_empty
    LA a0, term_out_buf
    LA a1, term_dirent
    CALL str_copy
    J tcmd_done
tcmd_ls_empty:
    LA a0, term_out_buf
    LA a1, str_term_ls_empty
    CALL str_copy
tcmd_done:
    LD ra, 8(sp)
    ADDI sp, sp, 16
    RET

terminal_on_click:
    RET

str_eq:
seq_l:
    LBU t0, 0(a0)
    LBU t1, 0(a1)
    BNE t0, t1, seq_diff
    BEQ t0, zero, seq_same
    ADDI a0, a0, 1
    ADDI a1, a1, 1
    J seq_l
seq_same:
    LI a0, 1
    RET
seq_diff:
    LI a0, 0
    RET

; ==============================================================================
; Number to Decimal String
; ==============================================================================
num_to_dec:
    BNE a0, zero, n2d_nonzero
    LI t0, 48                ; '0'
    SB t0, 0(a1)
    SB zero, 1(a1)
    RET
n2d_nonzero:
    ADDI sp, sp, -32
    SD ra, 24(sp)
    SD s0, 16(sp)
    MV s0, a1
    BGE a0, zero, n2d_pos
    LI t0, 45                ; '-'
    SB t0, 0(s0)
    ADDI s0, s0, 1
    NEG a0, a0
n2d_pos:
    LA t0, n2d_temp
    LI t1, 0
n2d_loop:
    BEQ a0, zero, n2d_rev
    LI t2, 10
    REM t3, a0, t2
    DIVU a0, a0, t2
    ADDI t3, t3, 48
    ADD t4, t0, t1
    SB t3, 0(t4)
    ADDI t1, t1, 1
    J n2d_loop
n2d_rev:
    LI t2, 0
n2d_rev_loop:
    BEQ t1, zero, n2d_fin
    ADDI t1, t1, -1
    ADD t4, t0, t1
    LBU t3, 0(t4)
    ADD t4, s0, t2
    SB t3, 0(t4)
    ADDI t2, t2, 1
    J n2d_rev_loop
n2d_fin:
    ADD t4, s0, t2
    SB zero, 0(t4)
    LD s0, 16(sp)
    LD ra, 24(sp)
    ADDI sp, sp, 32
    RET

; ==============================================================================
; Sound Driver: Play Tone via MMIO PSG Synthesizer (0x03FFE000)
; Inputs:
;   a0 = frequency / pitch in Hz (u32)
;   a1 = duration in milliseconds (u32)
;   a2 = waveform (0=Square, 1=Triangle, 2=Noise) (u8)
;   a3 = volume (0..255) (u8)
; ==============================================================================
sound_play_tone:
    LI t0, 0x03FFE000
    SW a0, 0(t0)        ; 0x03FFE000: Frequency
    SB a2, 4(t0)        ; 0x03FFE004: Waveform
    SB a3, 5(t0)        ; 0x03FFE005: Volume
    SW a1, 8(t0)        ; 0x03FFE008: Duration
    LI t1, 1
    SB t1, 12(t0)       ; 0x03FFE00C: Trigger playback
    RET

; ==============================================================================
; Shared file services: string/path helpers, decimal sizes, UTF-8 check.
; Clobbers: a0-a3, t0-t6 unless noted. s-registers are preserved.
; ==============================================================================

; str_len: a0=str -> a0=length (bytes before NUL)
str_len:
    MV t0, a0
    LI a0, 0
slen_loop:
    LBU t1, 0(t0)
    BEQ t1, zero, slen_done
    ADDI t0, t0, 1
    ADDI a0, a0, 1
    J slen_loop
slen_done:
    RET

; path_join: a0=dir, a1=name, a2=dst, a3=cap -> a0=len or -1 on overflow.
; Result is dir + ("/" unless dir is "/") + name. Never truncates.
path_join:
    ADDI sp, sp, -64
    SD ra, 56(sp)
    SD s0, 48(sp)
    SD s1, 40(sp)
    SD s2, 32(sp)
    SD s3, 24(sp)
    SD s4, 16(sp)
    SD s5, 8(sp)

    MV s0, a0                ; s0 = dir
    MV s1, a1                ; s1 = name
    MV s2, a2                ; s2 = dst
    MV s3, a3                ; s3 = cap

    MV a0, s0
    CALL str_len
    MV s4, a0                ; s4 = dir len

    MV a0, s1
    CALL str_len
    MV s5, a0                ; s5 = name len

    ADD t2, s4, s5
    ADDI t2, t2, 2           ; slash + NUL
    BGT t2, s3, pj_over

    MV t3, s2                ; t3 = write pointer
    MV t4, s0                ; t4 = read dir pointer

pj_dir_l:
    LBU t5, 0(t4)
    BEQ t5, zero, pj_dir_d
    SB t5, 0(t3)
    ADDI t3, t3, 1
    ADDI t4, t4, 1
    J pj_dir_l

pj_dir_d:
    LI t5, 47                ; '/'
    BEQ s4, zero, pj_slash   ; empty dir -> needs '/'

    ; If dir is exactly "/", don't add another slash
    LI t1, 1
    BNE s4, t1, pj_check_trail
    LBU t4, 0(s0)
    BEQ t4, t5, pj_name_c    ; dir is "/", already has slash!

pj_check_trail:
    ADD t4, s0, s4
    ADDI t4, t4, -1
    LBU t4, 0(t4)
    BEQ t4, t5, pj_name_c    ; already ends with '/', skip adding slash

pj_slash:
    SB t5, 0(t3)
    ADDI t3, t3, 1

pj_name_c:
    MV t4, s1
pj_name_l:
    LBU t5, 0(t4)
    BEQ t5, zero, pj_fin
    SB t5, 0(t3)
    ADDI t3, t3, 1
    ADDI t4, t4, 1
    J pj_name_l

pj_fin:
    SB zero, 0(t3)
    SUB a0, t3, s2           ; len = write_ptr - dst
    J pj_ret

pj_over:
    LI a0, -1

pj_ret:
    LD s5, 8(sp)
    LD s4, 16(sp)
    LD s3, 24(sp)
    LD s2, 32(sp)
    LD s1, 40(sp)
    LD s0, 48(sp)
    LD ra, 56(sp)
    ADDI sp, sp, 64
    RET

; path_parent: a0=mutable path -> in place; "/" stays "/".
path_parent:
    ADDI sp, sp, -32
    SD ra, 24(sp)
    SD s0, 16(sp)
    MV s0, a0
    CALL str_len
    LI t1, 1
    BLE a0, t1, pp_root
    ; strip trailing slashes if len > 1
pp_strip:
    BLE a0, t1, pp_root
    ADD t2, s0, a0
    ADDI t2, t2, -1
    LBU t3, 0(t2)
    LI t4, 47
    BEQ t3, t4, pp_strip_slash
    LI t4, 92
    BEQ t3, t4, pp_strip_slash
    J pp_scan_start
pp_strip_slash:
    SB zero, 0(t2)
    ADDI a0, a0, -1
    J pp_strip
pp_scan_start:
    ADD t1, s0, a0
    ADDI t1, t1, -1
pp_scan:
    LBU t2, 0(t1)
    LI t3, 47
    BEQ t2, t3, pp_cut
    LI t3, 92
    BEQ t2, t3, pp_cut
    BEQ t1, s0, pp_root
    ADDI t1, t1, -1
    J pp_scan
pp_cut:
    BEQ t1, s0, pp_keep_one
    SB zero, 0(t1)
    J pp_ret
pp_keep_one:
    ADDI t1, t1, 1
    SB zero, 0(t1)
    J pp_ret
pp_root:
    LI t1, 47
    SB t1, 0(s0)
    SB zero, 1(s0)
pp_ret:
    LD s0, 16(sp)
    LD ra, 24(sp)
    ADDI sp, sp, 32
    RET

; ext_is: a0=path, a1=UPPER ext ("APP") -> a0=1 match (case-insensitive).
ext_is:
    MV t0, a0
    MV t1, a1
    LI t2, 0                 ; last dot or 0
    LI t3, 0                 ; last slash pos flag
ei_scan:
    LBU t4, 0(t0)
    BEQ t4, zero, ei_have
    LI t5, 47
    BEQ t4, t5, ei_slash
    LI t5, 92
    BEQ t4, t5, ei_slash
    LI t5, 46
    BNE t4, t5, ei_next
    MV t2, t0
    J ei_next
ei_slash:
    LI t2, 0
ei_next:
    ADDI t0, t0, 1
    J ei_scan
ei_have:
    BEQ t2, zero, ei_no
    ADDI t2, t2, 1
ei_cmp:
    LBU t4, 0(t2)
    LBU t5, 0(t1)
    BEQ t5, zero, ei_endok
    BEQ t4, zero, ei_no
    LI t6, 97
    BLT t4, t6, ei_cmp2
    LI t6, 122
    BGT t4, t6, ei_cmp2
    ADDI t4, t4, -32
ei_cmp2:
    BNE t4, t5, ei_no
    ADDI t2, t2, 1
    ADDI t1, t1, 1
    J ei_cmp
ei_endok:
    BEQ t4, zero, ei_yes
ei_no:
    LI a0, 0
    RET
ei_yes:
    LI a0, 1
    RET

; fmt_size: a0=bytes, a1=dst -> NUL-terminated "N B" / "N.N KB" / "N.N MB".
; Decimal SI (1000/divisor), one fractional digit for KB/MB. Zero only for
; genuinely empty files; small sizes never round down to zero.
fmt_size:
    ADDI sp, sp, -32
    SD ra, 24(sp)
    SD s0, 16(sp)
    SD s1, 8(sp)
    SD s2, 0(sp)
    MV s0, a0
    MV s1, a1
    LI t0, 1000
    BLT s0, t0, fs_bytes
    LI t0, 1000000
    BLT s0, t0, fs_kb
    LI t0, 100000
    DIVU t0, s0, t0          ; tenths of MB
    LI t1, 77                ; 'M'
    J fs_scaled
fs_kb:
    LI t0, 100
    DIVU t0, s0, t0          ; tenths of KB
    LI t1, 75                ; 'K'
fs_scaled:
    LI t2, 10
    DIVU a0, t0, t2          ; whole units
    CALL num_to_dec_helper
    REM t3, t0, t2
    LI t4, 46
    SB t4, 0(s1)
    ADDI s1, s1, 1
    ADDI t3, t3, 48
    SB t3, 0(s1)
    ADDI s1, s1, 1
    LI t4, 32
    SB t4, 0(s1)
    ADDI s1, s1, 1
    SB t1, 0(s1)
    ADDI s1, s1, 1
    LI t4, 66
    SB t4, 0(s1)
    ADDI s1, s1, 1
    SB zero, 0(s1)
    J fs_ret
fs_bytes:
    MV a0, s0
    MV s2, s1
    MV a1, s1
    CALL num_to_dec
    MV a0, s1
    CALL str_len
    ADD s1, s1, a0
    LI t0, 32
    SB t0, 0(s1)
    ADDI s1, s1, 1
    LI t0, 66
    SB t0, 0(s1)
    ADDI s1, s1, 1
    SB zero, 0(s1)
fs_ret:
    LD s2, 0(sp)
    LD s1, 8(sp)
    LD s0, 16(sp)
    LD ra, 24(sp)
    ADDI sp, sp, 32
    RET

; num_to_dec_helper: a0=value -> appends decimal text at s1, advances s1.
; (num_to_dec and str_len only clobber a/t regs, so s1 survives.)
num_to_dec_helper:
    ADDI sp, sp, -16
    SD ra, 8(sp)
    MV a1, s1
    CALL num_to_dec
    MV a0, s1
    CALL str_len
    ADD s1, s1, a0
    LD ra, 8(sp)
    ADDI sp, sp, 16
    RET

; str_append: a0=dst(NUL-term), a1=src -> appends src at dst end.
str_append:
    MV t0, a0
sa_len:
    LBU t1, 0(t0)
    BEQ t1, zero, sa_copy
    ADDI t0, t0, 1
    J sa_len
sa_copy:
    LBU t1, 0(a1)
    SB t1, 0(t0)
    BEQ t1, zero, sa_ret
    ADDI t0, t0, 1
    ADDI a1, a1, 1
    J sa_copy
sa_ret:
    RET

; notepad_set_status: a0=message -> note_status_str.
notepad_set_status:
    MV t0, a0
    LA a0, note_status_str
    MV a1, t0
    J str_copy

; notepad_load_path: a0=absolute path -> a0=0 ok, else negative FS code.
; Probe-then-read: missing/oversize never touch note_buf, so a failed open
; keeps the existing text. On success adopts the path and clears dirty.
notepad_load_path:
    ADDI sp, sp, -48
    SD ra, 40(sp)
    SD s0, 32(sp)
    SD s1, 24(sp)
    SD s2, 16(sp)
    MV s0, a0
    ; Probe full size without touching any buffers (cap 0).
    MV a0, s0
    LI a1, 0
    LA a2, note_staging_buf
    LI a3, 0
    LI a7, 33
    ECALL
    BLT a0, zero, nlp_fail
    LI t0, 4000
    BGT a1, t0, nlp_large
    MV s1, a1                ; full size
    ; Read into staging buffer, preserving note_buf untouched.
    MV a0, s0
    LI a1, 0
    LA a2, note_staging_buf
    LI a3, 4000
    LI a7, 33
    ECALL
    BLT a0, zero, nlp_fail
    MV s2, a0                ; bytes read
    ; Encoding check on staging buffer before replacing the document.
    LA a0, note_staging_buf
    MV a1, s2
    CALL utf8_valid
    BEQ a0, zero, nlp_badenc
    ; Validation passed: copy staging buffer into note_buf.
    LA t0, note_buf
    LA t1, note_staging_buf
    MV t2, s2
nlp_cp_loop:
    BEQ t2, zero, nlp_cp_done
    LBU t3, 0(t1)
    SB t3, 0(t0)
    ADDI t0, t0, 1
    ADDI t1, t1, 1
    ADDI t2, t2, -1
    J nlp_cp_loop
nlp_cp_done:
    LA t0, note_len
    SW s2, 0(t0)
    LA t0, note_cursor
    SW zero, 0(t0)
    LA t1, note_buf
    ADD t1, t1, s2
    SB zero, 0(t1)
    LA a0, note_path
    MV a1, s0
    CALL str_copy
    LA a0, str_status_opened
    CALL notepad_set_status
    LA t0, note_dirty
    SW zero, 0(t0)
    LI a0, 0
    J nlp_ret
nlp_large:
    LA a0, str_status_too_large
    CALL notepad_set_status
    LI a0, -12
    J nlp_ret
nlp_badenc:
    LA a0, str_status_bad_enc
    CALL notepad_set_status
    LI a0, -11
    J nlp_ret
nlp_fail:
    MV s1, a0
    LI t0, -3
    BEQ a0, t0, nlp_msg_missing
    LI t0, -10
    BEQ a0, t0, nlp_msg_name
    LI t0, -1
    BEQ a0, t0, nlp_msg_disk
    LA a0, str_status_open_error
    CALL notepad_set_status
    J nlp_fail_ret
nlp_msg_missing:
    LA a0, str_status_missing
    CALL notepad_set_status
    J nlp_fail_ret
nlp_msg_name:
    LA a0, str_status_bad_name
    CALL notepad_set_status
    J nlp_fail_ret
nlp_msg_disk:
    LA a0, str_status_no_disk
    CALL notepad_set_status
nlp_fail_ret:
    MV a0, s1
nlp_ret:
    LD s2, 16(sp)
    LD s1, 24(sp)
    LD s0, 32(sp)
    LD ra, 40(sp)
    ADDI sp, sp, 48
    RET

; notepad_write_path: a0=absolute path -> a0=0 ok, else negative FS code.
; Writes note_buf[0..note_len) with create+truncate. No state adopted here.
notepad_write_path:
    ADDI sp, sp, -32
    SD ra, 24(sp)
    SD s0, 16(sp)
    SD s1, 8(sp)
    MV s0, a0
    LA t0, note_len
    LW s1, 0(t0)
    MV a0, s0
    LA a1, note_buf
    MV a2, s1
    LI a3, 3
    LI a7, 34
    ECALL
    BLT a0, zero, nwp_fail
    CALL fm_refresh
    LA a0, str_status_saved
    CALL notepad_set_status
    LI a0, 0
    J nwp_ret
nwp_fail:
    MV s1, a0
    LI t0, -8
    BEQ a0, t0, nwp_msg_ro
    LI t0, -7
    BEQ a0, t0, nwp_msg_full
    LI t0, -10
    BEQ a0, t0, nwp_msg_name
    LI t0, -1
    BEQ a0, t0, nwp_msg_disk
    LA a0, str_status_save_error
    CALL notepad_set_status
    J nwp_fail_ret
nwp_msg_ro:
    LA a0, str_status_readonly
    CALL notepad_set_status
    J nwp_fail_ret
nwp_msg_full:
    LA a0, str_status_diskfull
    CALL notepad_set_status
    J nwp_fail_ret
nwp_msg_name:
    LA a0, str_status_bad_name
    CALL notepad_set_status
    J nwp_fail_ret
nwp_msg_disk:
    LA a0, str_status_no_disk
    CALL notepad_set_status
nwp_fail_ret:
    MV a0, s1
nwp_ret:
    LD s1, 8(sp)
    LD s0, 16(sp)
    LD ra, 24(sp)
    ADDI sp, sp, 32
    RET

; notepad_do_save: Save button/F9/Ctrl+S. Untitled opens Save As.
notepad_do_save:
    ADDI sp, sp, -16
    SD ra, 8(sp)
    LA t0, note_path
    LBU t1, 0(t0)
    BEQ t1, zero, nds_saveas
    LA a0, note_path
    CALL notepad_write_path
    BLT a0, zero, nds_ret
    LA t0, note_dirty
    SW zero, 0(t0)
    J nds_ret
nds_saveas:
    CALL dlg_show_saveas
nds_ret:
    CALL flag_redraw
    LD ra, 8(sp)
    ADDI sp, sp, 16
    RET

; notepad_open_path: a0=path (dispatcher/dialog) -> dirty-aware load.
notepad_open_path:
    ADDI sp, sp, -32
    SD ra, 24(sp)
    SD s0, 16(sp)
    MV s0, a0
    LA t0, note_dirty
    LW t1, 0(t0)
    BEQ t1, zero, nop_direct
    LA a0, dlg_pending_path
    MV a1, s0
    CALL str_copy
    LI a0, 2
    CALL dlg_show_confirm
    J nop_ret
nop_direct:
    MV a0, s0
    CALL notepad_load_path
    LI a0, 2
    CALL window_open
    CALL flag_redraw
nop_ret:
    LD s0, 16(sp)
    LD ra, 24(sp)
    ADDI sp, sp, 32
    RET

; notepad_request_open_dlg: F10/Open button -> confirm first when dirty.
notepad_request_open_dlg:
    ADDI sp, sp, -16
    SD ra, 8(sp)
    LA t0, note_dirty
    LW t1, 0(t0)
    BEQ t1, zero, nrod_direct
    LA t0, dlg_pending_path
    SB zero, 0(t0)
    LI a0, 2
    CALL dlg_show_confirm
    J nrod_ret
nrod_direct:
    CALL dlg_show_open
nrod_ret:
    LD ra, 8(sp)
    ADDI sp, sp, 16
    RET

; notepad_request_new: New button -> confirm first when dirty.
notepad_request_new:
    ADDI sp, sp, -16
    SD ra, 8(sp)
    LA t0, note_dirty
    LW t1, 0(t0)
    BEQ t1, zero, nrn_clean
    LI a0, 1
    CALL dlg_show_confirm
    J nrn_ret
nrn_clean:
    CALL notepad_new_now
nrn_ret:
    LD ra, 8(sp)
    ADDI sp, sp, 16
    RET

; notepad_new_now: pending action 1. Untitled, empty, unmodified.
notepad_new_now:
    ADDI sp, sp, -16
    SD ra, 8(sp)
    LA t0, note_path
    SB zero, 0(t0)
    LA t0, note_len
    SW zero, 0(t0)
    LA t0, note_cursor
    SW zero, 0(t0)
    LA t0, note_buf
    SB zero, 0(t0)
    LA t0, note_dirty
    SW zero, 0(t0)
    LA a0, str_status_new
    CALL notepad_set_status
    CALL flag_redraw
    LD ra, 8(sp)
    ADDI sp, sp, 16
    RET

; notepad_request_close: window_close/ESC path for dirty Notepad.
; Returns a0=1 when the close was deferred to the confirm dialog.
notepad_request_close:
    ADDI sp, sp, -16
    SD ra, 8(sp)
    LA t0, active_window
    LW t1, 0(t0)
    LI t2, 2
    BNE t1, t2, nrc_no
    LA t0, note_dirty
    LW t1, 0(t0)
    BEQ t1, zero, nrc_no
    LI a0, 3
    CALL dlg_show_confirm
    LI a0, 1
    LD ra, 8(sp)
    ADDI sp, sp, 16
    RET
nrc_no:
    LI a0, 0
    LD ra, 8(sp)
    ADDI sp, sp, 16
    RET

; notepad_request_shutdown: X.Exit path with unsaved text.
notepad_request_shutdown:
    ADDI sp, sp, -16
    SD ra, 8(sp)
    LA t0, note_dirty
    LW t1, 0(t0)
    BEQ t1, zero, nrs_clean
    LI a0, 4
    CALL dlg_show_confirm
    J nrs_ret
nrs_clean:
    CALL do_quit_os_now
nrs_ret:
    LD ra, 8(sp)
    ADDI sp, sp, 16
    RET

do_quit_os_now:
    LA t0, exit_requested
    LI t1, 1
    SW t1, 0(t0)
    RET

; do_pending: resume after successful save or explicit discard.
do_pending:
    ADDI sp, sp, -16
    SD ra, 8(sp)
    LA t0, dlg_pending
    LW t1, 0(t0)
    SW zero, 0(t0)
    LI t0, 1
    BEQ t1, t0, dp_new
    LI t0, 2
    BEQ t1, t0, dp_open
    LI t0, 3
    BEQ t1, t0, dp_close
    LI t0, 4
    BEQ t1, t0, dp_exit
    J dp_ret
dp_new:
    CALL notepad_new_now
    J dp_ret
dp_open:
    LA t0, dlg_pending_path
    LBU t1, 0(t0)
    BEQ t1, zero, dp_open_dlg
    LA a0, dlg_pending_path
    CALL notepad_load_path
    CALL flag_redraw
    J dp_ret
dp_open_dlg:
    CALL dlg_show_open
    J dp_ret
dp_close:
    CALL window_close
    J dp_ret
dp_exit:
    CALL do_quit_os_now
dp_ret:
    LD ra, 8(sp)
    ADDI sp, sp, 16
    RET

; Legacy wrappers kept for init compatibility; now path-based.
notepad_save_file:
    ADDI sp, sp, -16
    SD ra, 8(sp)
    LA t0, note_path
    LBU t1, 0(t0)
    BEQ t1, zero, nsf_ret
    LA a0, note_path
    CALL notepad_write_path
    BLT a0, zero, nsf_ret
    LA t0, note_dirty
    SW zero, 0(t0)
nsf_ret:
    LD ra, 8(sp)
    ADDI sp, sp, 16
    RET

notepad_open_file:
    ADDI sp, sp, -16
    SD ra, 8(sp)
    LA t0, note_path
    LBU t1, 0(t0)
    BEQ t1, zero, nof_ret
    LA a0, note_path
    CALL notepad_load_path
nof_ret:
    LD ra, 8(sp)
    ADDI sp, sp, 16
    RET

; ==============================================================================
; Shared native file dialogs (Delphi-like Open/SaveAs + dirty confirm).
; Modal: ev_key/click/move/release route here while dlg_active != 0, so input
; never leaks to underlying controls. Background timer work continues.
; 8.3 limits are enforced by the FS layer and surfaced verbatim; names are
; never truncated or rewritten by the dialog.
; ==============================================================================
; dlg geometry: DX=120 DY=80 DW=560 DH=420 (bottom 500, above taskbar).

; field_insert: a0=buf,a1=lenA,a2=caretA,a3=cap,a4=char
field_insert:
    LW t0, 0(a1)              ; len
    BGE t0, a3, fi_ret        ; full (never truncate: refuse instead)
    LW t1, 0(a2)              ; caret
    ADD t2, a0, t0            ; &buf[len] (NUL cell)
    ADD t4, a0, t1            ; &buf[caret]
fi_shift:
    BLT t2, t4, fi_do
    LBU t3, 0(t2)
    SB t3, 1(t2)
    ADDI t2, t2, -1
    J fi_shift
fi_do:
    SB a4, 0(t4)
    ADDI t0, t0, 1
    SW t0, 0(a1)
    ADDI t1, t1, 1
    SW t1, 0(a2)
fi_ret:
    RET

; field_bs: a0=buf,a1=lenA,a2=caretA (byte-wise; dialog fields are 8.3 ASCII)
field_bs:
    LW t1, 0(a2)
    BEQ t1, zero, fb_ret
    ADDI t1, t1, -1
    SW t1, 0(a2)
    LW t0, 0(a1)
    ADD t2, a0, t1
fb_shift:
    ADDI t3, t2, 1
    LBU t4, 0(t3)
    SB t4, 0(t2)
    BEQ t4, zero, fb_done
    ADDI t2, t2, 1
    J fb_shift
fb_done:
    ADDI t0, t0, -1
    SW t0, 0(a1)
fb_ret:
    RET

; field_del: a0=buf,a1=lenA,a2=caretA
field_del:
    LW t1, 0(a2)
    LW t0, 0(a1)
    BGE t1, t0, fd_ret
    ADD t2, a0, t1
fd_shift:
    ADDI t3, t2, 1
    LBU t4, 0(t3)
    SB t4, 0(t2)
    BEQ t4, zero, fd_done
    ADDI t2, t2, 1
    J fd_shift
fd_done:
    ADDI t0, t0, -1
    SW t0, 0(a1)
fd_ret:
    RET

; field_pick: a0=len,a1=x,a2=text_x,a3=scroll -> a0=caret clamped.
field_pick:
    SUB t0, a1, a2
    ADDI t0, t0, -4
    BLT t0, zero, fp_zero
    SRLI t0, t0, 3             ; /8 px per cell
    ADD t0, t0, a3
    BGT t0, a0, fp_end
    MV a0, t0
    RET
fp_end:
    RET
fp_zero:
    LI a0, 0
    RET

; dlg_visible_copy: a0=buf,a1=len,a2=scroll,a3=dst (NUL-term substring)
dlg_visible_copy:
    ADD t0, a0, a2
    ADD t1, a0, a1
    MV t2, a3
dvc_loop:
    BGE t0, t1, dvc_fin
    LBU t3, 0(t0)
    SB t3, 0(t2)
    ADDI t0, t0, 1
    ADDI t2, t2, 1
    J dvc_loop
dvc_fin:
    SB zero, 0(t2)
    RET

; dlg_fix_scroll: a0=lenA,a1=caretA,a2=scrollA,a3=field_w (keeps caret visible)
dlg_fix_scroll:
    LW t0, 0(a0)
    LW t1, 0(a1)
    LW t2, 0(a2)
    BLT t1, t2, dfs_caret
    SUB t3, t1, t2
    SLLI t3, t3, 3
    ADDI t4, a3, -12
    BLE t3, t4, dfs_shrink
    ADDI t2, t2, 1
    SW t2, 0(a2)
    J dlg_fix_scroll
dfs_caret:
    SW t1, 0(a2)
    RET
dfs_shrink:
    BEQ t2, zero, dfs_ret
    SUB t3, t0, t2
    SLLI t3, t3, 3
    ADDI t4, a3, -12
    BGE t3, t4, dfs_ret
    ADDI t2, t2, -1
    SW t2, 0(a2)
    J dlg_fix_scroll
dfs_ret:
    RET

; ==============================================================================
; FAT 8.3 Short-Name Validation & Conversion
;
; is_fat_char: a0=char -> a0=1 if valid FAT 8.3 char, 0 if invalid
; ==============================================================================
is_fat_char:
    LI t0, 32
    BLE a0, t0, ifc_no
    LI t0, 127
    BGE a0, t0, ifc_no
    LI t0, 34                ; '"'
    BEQ a0, t0, ifc_no
    LI t0, 42                ; '*'
    BEQ a0, t0, ifc_no
    LI t0, 43                ; '+'
    BEQ a0, t0, ifc_no
    LI t0, 44                ; ','
    BEQ a0, t0, ifc_no
    LI t0, 47                ; '/'
    BEQ a0, t0, ifc_no
    LI t0, 58                ; ':'
    BEQ a0, t0, ifc_no
    LI t0, 59                ; ';'
    BEQ a0, t0, ifc_no
    LI t0, 60                ; '<'
    BEQ a0, t0, ifc_no
    LI t0, 61                ; '='
    BEQ a0, t0, ifc_no
    LI t0, 62                ; '>'
    BEQ a0, t0, ifc_no
    LI t0, 63                ; '?'
    BEQ a0, t0, ifc_no
    LI t0, 91                ; '['
    BEQ a0, t0, ifc_no
    LI t0, 92                ; '\'
    BEQ a0, t0, ifc_no
    LI t0, 93                ; ']'
    BEQ a0, t0, ifc_no
    LI t0, 124               ; '|'
    BEQ a0, t0, ifc_no
    LI a0, 1
    RET
ifc_no:
    LI a0, 0
    RET

; ==============================================================================
; fat_validate_shortname:
;   a0 = input string
;   a1 = output buffer for trimmed display string (or 0)
;   a2 = output buffer for 11-byte space-padded canonical FAT name (or 0)
; Returns a0:
;    0 = OK
;   -1 = Empty or whitespace only
;   -2 = Contains path separator ('/' or '\')
;   -3 = Base name exceeds 8 characters
;   -4 = Extension exceeds 3 characters
;   -5 = Multiple dots
;   -6 = Missing base name
;   -7 = Invalid character
; ==============================================================================
fat_validate_shortname:
    ADDI sp, sp, -80
    SD ra, 72(sp)
    SD s0, 64(sp)            ; input
    SD s1, 56(sp)            ; out_disp
    SD s2, 48(sp)            ; out_canon
    SD s3, 40(sp)            ; trimmed start
    SD s4, 32(sp)            ; trimmed end (exclusive)
    SD s5, 24(sp)            ; scan ptr
    SD s6, 16(sp)            ; seen_dot
    SD s7, 8(sp)             ; base_len
    SD s8, 0(sp)             ; ext_len

    MV s0, a0
    MV s1, a1
    MV s2, a2

    ; Trim leading whitespace
    MV s3, s0
fvs_lead:
    LBU t0, 0(s3)
    BEQ t0, zero, fvs_err_empty
    LI t1, 32                ; ' '
    BEQ t0, t1, fvs_lead_next
    LI t1, 9                 ; '\t'
    BEQ t0, t1, fvs_lead_next
    J fvs_find_end
fvs_lead_next:
    ADDI s3, s3, 1
    J fvs_lead

fvs_find_end:
    MV s4, s3
fvs_scan_end:
    LBU t0, 0(s4)
    BEQ t0, zero, fvs_trim_trail
    ADDI s4, s4, 1
    J fvs_scan_end

fvs_trim_trail:
fvs_trail_loop:
    BEQ s4, s3, fvs_err_empty
    ADDI t2, s4, -1
    LBU t0, 0(t2)
    LI t1, 32
    BEQ t0, t1, fvs_trail_step
    LI t1, 9
    BEQ t0, t1, fvs_trail_step
    J fvs_have_bounds
fvs_trail_step:
    MV s4, t2
    J fvs_trail_loop

fvs_have_bounds:
    ; Check special "." and ".."
    SUB t0, s4, s3
    LI t1, 1
    BNE t0, t1, fvs_chk_dotdot
    LBU t2, 0(s3)
    LI t3, 46                ; '.'
    BEQ t2, t3, fvs_special_dot

fvs_chk_dotdot:
    LI t1, 2
    BNE t0, t1, fvs_scan_body
    LBU t2, 0(s3)
    LBU t3, 1(s3)
    LI t4, 46
    BNE t2, t4, fvs_scan_body
    BNE t3, t4, fvs_scan_body
    J fvs_special_dotdot

fvs_scan_body:
    MV s5, s3
    LI s6, 0                 ; seen_dot = 0
    LI s7, 0                 ; base_len = 0
    LI s8, 0                 ; ext_len = 0

fvs_loop:
    BEQ s5, s4, fvs_done_scan
    LBU a0, 0(s5)

    LI t0, 47                ; '/'
    BEQ a0, t0, fvs_err_pathsep
    LI t0, 92                ; '\'
    BEQ a0, t0, fvs_err_pathsep

    LI t0, 46                ; '.'
    BNE a0, t0, fvs_char

    ; Dot
    BNE s6, zero, fvs_err_multidot
    BEQ s7, zero, fvs_err_emptybase
    LI s6, 1
    ADDI s5, s5, 1
    J fvs_loop

fvs_char:
    CALL is_fat_char
    BEQ a0, zero, fvs_err_invalchar

    BNE s6, zero, fvs_ext_char
    ADDI s7, s7, 1
    LI t0, 8
    BGT s7, t0, fvs_err_baselen
    ADDI s5, s5, 1
    J fvs_loop

fvs_ext_char:
    ADDI s8, s8, 1
    LI t0, 3
    BGT s8, t0, fvs_err_extlen
    ADDI s5, s5, 1
    J fvs_loop

fvs_done_scan:
    BEQ s7, zero, fvs_err_emptybase

    ; Copy display string if buffer provided
    BEQ s1, zero, fvs_fill_canon
    MV t0, s3
    MV t1, s1
fvs_disp_cp:
    BEQ t0, s4, fvs_disp_done
    LBU t2, 0(t0)
    SB t2, 0(t1)
    ADDI t0, t0, 1
    ADDI t1, t1, 1
    J fvs_disp_cp
fvs_disp_done:
    SB zero, 0(t1)

fvs_fill_canon:
    BEQ s2, zero, fvs_ok
    LI t0, 32
    LI t1, 0
fvs_clr_canon:
    LI t2, 11
    BGE t1, t2, fvs_cp_canon
    ADD t3, s2, t1
    SB t0, 0(t3)
    ADDI t1, t1, 1
    J fvs_clr_canon

fvs_cp_canon:
    MV t0, s3
    LI t1, 0                 ; base idx
    LI t2, 8                 ; ext idx
    LI t3, 0                 ; dot flag
fvs_canon_loop:
    BEQ t0, s4, fvs_ok
    LBU t4, 0(t0)
    LI t5, 46
    BNE t4, t5, fvs_canon_char
    LI t3, 1
    ADDI t0, t0, 1
    J fvs_canon_loop
fvs_canon_char:
    LI t5, 97
    BLT t4, t5, fvs_canon_store
    LI t5, 122
    BGT t4, t5, fvs_canon_store
    ADDI t4, t4, -32
fvs_canon_store:
    BNE t3, zero, fvs_store_ext
    ADD t5, s2, t1
    SB t4, 0(t5)
    ADDI t1, t1, 1
    ADDI t0, t0, 1
    J fvs_canon_loop
fvs_store_ext:
    ADD t5, s2, t2
    SB t4, 0(t5)
    ADDI t2, t2, 1
    ADDI t0, t0, 1
    J fvs_canon_loop

fvs_special_dot:
    BEQ s1, zero, fvs_dot_canon
    LI t0, 46
    SB t0, 0(s1)
    SB zero, 1(s1)
fvs_dot_canon:
    BEQ s2, zero, fvs_ok
    LI t0, 32
    LI t1, 1
fvs_clr_dcanon:
    LI t2, 11
    BGE t1, t2, fvs_set_dot
    ADD t3, s2, t1
    SB t0, 0(t3)
    ADDI t1, t1, 1
    J fvs_clr_dcanon
fvs_set_dot:
    LI t0, 46
    SB t0, 0(s2)
    J fvs_ok

fvs_special_dotdot:
    BEQ s1, zero, fvs_dd_canon
    LI t0, 46
    SB t0, 0(s1)
    SB t0, 1(s1)
    SB zero, 2(s1)
fvs_dd_canon:
    BEQ s2, zero, fvs_ok
    LI t0, 32
    LI t1, 2
fvs_clr_ddcanon:
    LI t2, 11
    BGE t1, t2, fvs_set_dotdot
    ADD t3, s2, t1
    SB t0, 0(t3)
    ADDI t1, t1, 1
    J fvs_clr_ddcanon
fvs_set_dotdot:
    LI t0, 46
    SB t0, 0(s2)
    SB t0, 1(s2)
    J fvs_ok

fvs_ok:
    LI a0, 0
    J fvs_ret
fvs_err_empty:
    LI a0, -1
    J fvs_ret
fvs_err_pathsep:
    LI a0, -2
    J fvs_ret
fvs_err_baselen:
    LI a0, -3
    J fvs_ret
fvs_err_extlen:
    LI a0, -4
    J fvs_ret
fvs_err_multidot:
    LI a0, -5
    J fvs_ret
fvs_err_emptybase:
    LI a0, -6
    J fvs_ret
fvs_err_invalchar:
    LI a0, -7
    J fvs_ret

fvs_ret:
    LD s8, 0(sp)
    LD s7, 8(sp)
    LD s6, 16(sp)
    LD s5, 24(sp)
    LD s4, 32(sp)
    LD s3, 40(sp)
    LD s2, 48(sp)
    LD s1, 56(sp)
    LD s0, 64(sp)
    LD ra, 72(sp)
    ADDI sp, sp, 80
    RET

; ==============================================================================
; fat_error_msg: a0=code -> a0=pointer to error string
; ==============================================================================
fat_error_msg:
    LI t0, -1
    BEQ a0, t0, fem_nofile
    LI t0, -2
    BEQ a0, t0, fem_sep
    LI t0, -3
    BEQ a0, t0, fem_baselen
    LI t0, -4
    BEQ a0, t0, fem_extlen
    LI t0, -5
    BEQ a0, t0, fem_multidot
    LI t0, -6
    BEQ a0, t0, fem_emptybase
    LI t0, -7
    BEQ a0, t0, fem_invalchar
    LA a0, str_dlg_bad_name
    RET
fem_nofile:
    LA a0, str_dlg_no_file
    RET
fem_sep:
    LA a0, str_dlg_sep_err
    RET
fem_baselen:
    LA a0, str_dlg_base_len
    RET
fem_extlen:
    LA a0, str_dlg_ext_len
    RET
fem_multidot:
    LA a0, str_dlg_multi_dot
    RET
fem_emptybase:
    LA a0, str_dlg_empty_base
    RET
fem_invalchar:
    LA a0, str_dlg_invalid_char
    RET

; dlg_fs_error: a0=negative FS code -> dlg_err message.
dlg_fs_error:
    LI t0, -3
    BEQ a0, t0, dfe_missing
    LI t0, -10
    BEQ a0, t0, dfe_name
    LI t0, -1
    BEQ a0, t0, dfe_disk
    LI t0, -8
    BEQ a0, t0, dfe_ro
    LI t0, -7
    BEQ a0, t0, dfe_full
    LI t0, -11
    BEQ a0, t0, dfe_badenc
    LI t0, -12
    BEQ a0, t0, dfe_large
    LI t0, -4
    BEQ a0, t0, dfe_exists
    LA a1, str_dlg_io
    J dfe_copy
dfe_badenc:
    LA a1, str_dlg_bad_enc
    J dfe_copy
dfe_missing:
    LA a1, str_dlg_missing
    J dfe_copy
dfe_name:
    LA a1, str_dlg_bad_name
    J dfe_copy
dfe_disk:
    LA a1, str_dlg_no_disk
    J dfe_copy
dfe_ro:
    LA a1, str_dlg_ro
    J dfe_copy
dfe_full:
    LA a1, str_dlg_full
    J dfe_copy
dfe_large:
    LA a1, str_dlg_too_large
    J dfe_copy
dfe_exists:
    LA a1, str_dlg_exists
dfe_copy:
    LA a0, dlg_err
    J str_copy

; dlg_show_open / dlg_show_saveas: modal file dialogs for Notepad.
dlg_show_open:
    ADDI sp, sp, -16
    SD ra, 8(sp)
    LI a0, 1
    CALL dlg_show_file
    LD ra, 8(sp)
    ADDI sp, sp, 16
    RET
dlg_show_saveas:
    ADDI sp, sp, -16
    SD ra, 8(sp)
    LI a0, 2
    CALL dlg_show_file
    LD ra, 8(sp)
    ADDI sp, sp, 16
    RET
; dlg_show_file: a0=1 open, 2 saveas. Preserves any pending action.
dlg_show_file:
    ADDI sp, sp, -32
    SD ra, 24(sp)
    SD s0, 16(sp)
    MV s0, a0
    LA t0, active_window
    LW t1, 0(t0)
    LA t0, dlg_owner
    SW t1, 0(t0)
    LA t0, dlg_mode
    SW s0, 0(t0)
    LI t0, 1
    LA t1, dlg_filter
    SW t0, 0(t1)               ; text files (*.TXT)
    LI t0, 2
    LA t1, dlg_focus
    SW t0, 0(t1)               ; start in the filename field
    LA t0, dlg_armed
    SW zero, 0(t0)
    LA t0, dlg_err
    SB zero, 0(t0)
    ; Start folder: dirname(note_path) for SaveAs, else Explorer cwd.
    LI t0, 2
    BNE s0, t0, dsf_explorer_dir
    LA t0, note_path
    LBU t1, 0(t0)
    BEQ t1, zero, dsf_explorer_dir
    LA a0, dlg_dir
    LA a1, note_path
    CALL str_copy
    LA a0, dlg_dir
    CALL path_parent
    LA a0, dlg_path
    LA a1, dlg_dir
    CALL str_copy
    LA a0, dlg_dir
    CALL str_len
    LA t0, dlg_path_len
    SW a0, 0(t0)
    LA t0, dlg_path_caret
    SW a0, 0(t0)
    LA t0, dlg_path_scroll
    SW zero, 0(t0)
    ; Initial name: basename of the current document.
    LA t0, note_path
    MV t1, t0
dsf_base:
    LBU t2, 0(t1)
    BEQ t2, zero, dsf_base_d
    LI t3, 47
    BEQ t2, t3, dsf_base_s
    ADDI t1, t1, 1
    J dsf_base
dsf_base_s:
    ADDI t1, t1, 1
    MV t0, t1
    J dsf_base
dsf_base_d:
    LA a0, dlg_name
    MV a1, t0
    CALL str_copy
    LA a0, dlg_name
    CALL str_len
    J dsf_name_set
dsf_explorer_dir:
    LA t0, fm_cwd
    LBU t1, 0(t0)
    BNE t1, zero, dsf_copy_fm
    LI t1, 47
    SB t1, 0(t0)
    SB zero, 1(t0)
dsf_copy_fm:
    LA a0, dlg_dir
    LA a1, fm_cwd
    CALL str_copy
    LA t0, dlg_dir
    LBU t1, 0(t0)
    BNE t1, zero, dsf_dir_ok
    LI t1, 47
    SB t1, 0(t0)
    SB zero, 1(t0)
dsf_dir_ok:
    LA a0, dlg_path
    LA a1, dlg_dir
    CALL str_copy
    LA a0, dlg_dir
    CALL str_len
    LA t0, dlg_path_len
    SW a0, 0(t0)
    LA t0, dlg_path_caret
    SW a0, 0(t0)
    LA t0, dlg_path_scroll
    SW zero, 0(t0)
    LA t0, dlg_name
    SB zero, 0(t0)
    LI a0, 0
dsf_name_set:
    LA t0, dlg_name_len
    SW a0, 0(t0)
    LA t0, dlg_name_caret
    SW a0, 0(t0)
    LA t0, dlg_sel
    SW zero, 0(t0)
    LA t0, dlg_scroll
    SW zero, 0(t0)
    LA t0, dlg_last_row
    LI t1, -1
    SW t1, 0(t0)
    CALL dlg_refresh_list
    LA t0, dlg_active
    SW s0, 0(t0)
    CALL flag_redraw
    LD s0, 16(sp)
    LD ra, 24(sp)
    ADDI sp, sp, 32
    RET

; dlg_show_confirm: a0=pending action -> Save/Discard/Cancel dialog.
dlg_show_confirm:
    ADDI sp, sp, -16
    SD ra, 8(sp)
    LA t0, active_window
    LW t1, 0(t0)
    LA t0, dlg_owner
    SW t1, 0(t0)
    LA t0, dlg_pending
    SW a0, 0(t0)
    LI t0, 0
    LA t1, dlg_focus
    SW t0, 0(t1)
    LA t0, dlg_err
    SB zero, 0(t0)
    LI t0, 3
    LA t1, dlg_active
    SW t0, 0(t1)
    CALL flag_redraw
    LD ra, 8(sp)
    ADDI sp, sp, 16
    RET

; dlg_dismiss: close dialog, drop armed, restore owner focus, redraw.
dlg_dismiss:
    ADDI sp, sp, -16
    SD ra, 8(sp)
    LA t0, dlg_active
    SW zero, 0(t0)
    LA t0, dlg_armed
    SW zero, 0(t0)
    LA t0, dlg_err
    SB zero, 0(t0)
    LA t0, dlg_owner
    LW a0, 0(t0)
    BEQ a0, zero, dd_noraise
    CALL wm_raise
dd_noraise:
    CALL flag_redraw
    LD ra, 8(sp)
    ADDI sp, sp, 16
    RET

; dlg_cancel: Esc/Cancel -> dismiss, abort pending, preserve document.
dlg_cancel:
    ADDI sp, sp, -16
    SD ra, 8(sp)
    LA t0, dlg_pending
    SW zero, 0(t0)
    CALL dlg_dismiss
    LD ra, 8(sp)
    ADDI sp, sp, 16
    RET

; dlg_refresh_list: enumerate dlg_dir through FS_LIST with text filter.
dlg_refresh_list:
    ADDI sp, sp, -48
    SD ra, 40(sp)
    SD s0, 32(sp)
    SD s1, 24(sp)
    SD s2, 16(sp)
    LI s0, 0
    LI s1, 0
    LA t0, dlg_dir
    LBU t1, 0(t0)
    BNE t1, zero, drl_have
    LI t1, 47
    SB t1, 0(t0)
    SB zero, 1(t0)
drl_have:
drl_loop:
    LI t0, 32
    BGE s0, t0, drl_done
    LA a0, dlg_dir
    MV a1, s1
    LI t0, 28
    MUL t0, s0, t0
    LA t1, dlg_list_buf
    ADD a2, t1, t0
    MV s2, a2
    LI a7, 35
    ECALL
    BLT a0, zero, drl_done
    ADDI s1, s1, 1
    ; Skip "." and ".."
    LBU t0, 0(s2)
    LI t1, 46                ; '.'
    BNE t0, t1, drl_chk_filter
    LBU t2, 1(s2)
    BEQ t2, zero, drl_loop   ; "." -> skip
    BNE t2, t1, drl_chk_filter
    LBU t3, 2(s2)
    BEQ t3, zero, drl_loop   ; ".." -> skip
drl_chk_filter:
    ; Text filter: directories always pass; files need .TXT.
    LA t0, dlg_filter
    LW t1, 0(t0)
    BEQ t1, zero, drl_keep
    LBU t0, 13(s2)
    ANDI t0, t0, 0x10
    BNE t0, zero, drl_keep
    MV a0, s2
    LA a1, str_ext_txt
    CALL ext_is
    BEQ a0, zero, drl_loop
drl_keep:
    ADDI s0, s0, 1
    J drl_loop
drl_done:
    LA t0, dlg_count
    SW s0, 0(t0)
    LA t0, dlg_sel
    LW t1, 0(t0)
    BEQ s0, zero, drl_empty
    BGE t1, s0, drl_end
    BLT t1, zero, drl_zero
    J drl_scroll
drl_end:
    ADDI t1, s0, -1
    SW t1, 0(t0)
    J drl_scroll
drl_zero:
    SW zero, 0(t0)
    J drl_scroll
drl_empty:
    SW zero, 0(t0)
drl_scroll:
    LA t0, dlg_scroll
    LW t1, 0(t0)
    BLT t1, zero, drl_szero
    LA t2, dlg_sel
    LW t2, 0(t2)
    BLT t2, t1, drl_ssel
    ADDI t3, t1, 10
    BGE t2, t3, drl_sscroll
    J drl_ret
drl_ssel:
    SW t2, 0(t0)
    J drl_ret
drl_sscroll:
    ADDI t2, t2, -9
    SW t2, 0(t0)
    J drl_ret
drl_szero:
    SW zero, 0(t0)
drl_ret:
    LD s2, 16(sp)
    LD s1, 24(sp)
    LD s0, 32(sp)
    LD ra, 40(sp)
    ADDI sp, sp, 48
    RET

; dlg_vis_count: -> a0 = rows incl. synthetic ".." when not at root.
dlg_vis_count:
    LA t0, dlg_count
    LW a0, 0(t0)
    LA t1, dlg_dir
    LBU t2, 0(t1)
    LI t3, 47
    BNE t2, t3, dvc_ret
    LBU t2, 1(t1)
    BEQ t2, zero, dvc_ret
    ADDI a0, a0, 1
dvc_ret:
    RET

; dlg_row_entry: a0=visible row -> a0=cache index, or -1 for "..", -2 invalid.
dlg_row_entry:
    LA t0, dlg_dir
    LBU t1, 0(t0)
    LI t2, 47
    BNE t1, t2, dre_plain
    LBU t1, 1(t0)
    BNE t1, zero, dre_dot
dre_plain:
    LA t1, dlg_count
    LW t1, 0(t1)
    BGE a0, t1, dre_bad
    RET
dre_dot:
    BEQ a0, zero, dre_isdot
    ADDI a0, a0, -1
    LA t1, dlg_count
    LW t1, 0(t1)
    BGE a0, t1, dre_bad
    RET
dre_isdot:
    LI a0, -1
    RET
dre_bad:
    LI a0, -2
    RET

; dlg_draw: topmost modal paint (called from os_repaint before flush).
dlg_draw:
    ADDI sp, sp, -32
    SD ra, 24(sp)
    SD s0, 16(sp)
    SD s1, 8(sp)
    LA t0, dlg_active
    LW t0, 0(t0)
    LI t1, 3
    BEQ t0, t1, dlg_draw_confirm
    LI t1, 4
    BEQ t0, t1, dlg_draw_overwrite
    CALL dlg_draw_file
    J dlg_draw_ret
dlg_draw_confirm:
    CALL dlg_draw_confirm_body
    J dlg_draw_ret
dlg_draw_overwrite:
    CALL dlg_draw_overwrite_body
dlg_draw_ret:
    LD s1, 8(sp)
    LD s0, 16(sp)
    LD ra, 24(sp)
    ADDI sp, sp, 32
    RET

; dlg_draw_box: a0=x,a1=y,a2=w,a3=h,a4=focused -> field background.
dlg_draw_box:
    ADDI sp, sp, -48
    SD ra, 40(sp)
    SD s0, 32(sp)
    SD s1, 24(sp)
    SD s2, 16(sp)
    SD s3, 8(sp)
    MV s0, a0
    MV s1, a1
    MV s2, a2
    MV s3, a3
    BEQ a4, zero, ddb_plain
    ADDI a0, s0, -2
    ADDI a1, s1, -2
    ADDI a2, s2, 4
    ADDI a3, s3, 4
    LI a4, 0xFF38BDF8
    LI a7, 14
    ECALL
ddb_plain:
    MV a0, s0
    MV a1, s1
    MV a2, s2
    MV a3, s3
    LI a4, 0xFF0F172A
    LI a7, 14
    ECALL
    LD s3, 8(sp)
    LD s2, 16(sp)
    LD s1, 24(sp)
    LD s0, 32(sp)
    LD ra, 40(sp)
    ADDI sp, sp, 48
    RET

dlg_draw_file:
    ADDI sp, sp, -48
    SD ra, 40(sp)
    SD s0, 32(sp)
    SD s1, 24(sp)
    SD s2, 16(sp)
    ; Title with visible filter tag.
    LA a0, dlg_tmp
    LA t0, dlg_mode
    LW t1, 0(t0)
    LI t2, 1
    BNE t1, t2, ddf_save_title
    LA a1, str_dlg_open_title
    CALL str_copy
    LA a0, dlg_tmp
    LA a1, str_dlg_txt_tag
    CALL str_append
    J ddf_frame
ddf_save_title:
    LA a1, str_dlg_save_title
    CALL str_copy
ddf_frame:
    LI a0, 120
    LI a1, 80
    LI a2, 560
    LI a3, 420
    LA a4, dlg_tmp
    CALL draw_window_frame
    ; Folder label + path field.
    LI a0, 132
    LI a1, 122
    LA a2, str_dlg_folder
    LI a3, 0xFFFFFFFF
    LI a4, 0x00000000
    LI a7, 15
    ECALL
    LA t0, dlg_focus
    LW t1, 0(t0)
    LI a4, 0
    BEQ t1, zero, ddf_path_box
    LI a4, 0
    J ddf_path_box2
ddf_path_box:
    LI a4, 1
ddf_path_box2:
    LI a0, 196
    LI a1, 118
    LI a2, 472
    LI a3, 24
    CALL dlg_draw_box
    LA a0, dlg_path_len
    LA a1, dlg_path_caret
    LA a2, dlg_path_scroll
    LI a3, 472
    CALL dlg_fix_scroll
    LA a0, dlg_path
    LA t0, dlg_path_len
    LW a1, 0(t0)
    LA t0, dlg_path_scroll
    LW a2, 0(t0)
    LA a3, dlg_tmp
    CALL dlg_visible_copy
    LI a0, 200
    LI a1, 122
    LA a2, dlg_tmp
    LI a3, 0xFFF1F5F9
    LI a4, 0x00000000
    LI a5, 464
    LI a6, 16
    LI a7, 43
    ECALL
    LA t0, dlg_focus
    LW t1, 0(t0)
    BNE t1, zero, ddf_list
    LA t0, dlg_path_caret
    LW t1, 0(t0)
    LA t0, dlg_path_scroll
    LW t2, 0(t0)
    SUB t1, t1, t2
    SLLI t1, t1, 3
    ADDI a0, t1, 200
    LI a1, 122
    LI a2, 2
    LI a3, 16
    LI a4, 0xFF38BDF8
    LI a7, 14
    ECALL
ddf_list:
    ; List box + rows.
    LI a0, 132
    LI a1, 150
    LI a2, 536
    LI a3, 210
    LI a4, 0xFF0F172A
    LI a7, 14
    ECALL
    CALL dlg_vis_count
    MV s0, a0                ; rows
    LI s1, 0
    LI s2, 154
ddf_row:
    LI t0, 10
    BGE s1, t0, ddf_name
    LA t0, dlg_scroll
    LW t1, 0(t0)
    ADD t1, t1, s1
    BGE t1, s0, ddf_name
    MV a0, t1
    CALL dlg_row_entry
    LI t0, -2
    BEQ a0, t0, ddf_row_next
    LI t0, -1
    BEQ a0, t0, ddf_row_dotdot
    ; Regular entry: a0 is cache index
    LI t0, 28
    MUL t0, a0, t0
    LA t1, dlg_list_buf
    ADD t0, t1, t0              ; t0 = entry pointer
    ; Highlight if selected
    LA t1, dlg_sel
    LW t1, 0(t1)
    LA t2, dlg_scroll
    LW t2, 0(t2)
    ADD t2, t2, s1
    BNE t1, t2, ddf_file_nohl
    LI a0, 134
    MV a1, s2
    ADDI a1, a1, -1
    LI a2, 532
    LI a3, 20
    LI a4, 0xFF1E3A8A
    LI a7, 14
    ECALL
ddf_file_nohl:
    LBU t1, 13(t0)
    ANDI t1, t1, 0x10
    BNE t1, zero, ddf_file_isdir
    ; Regular file: format size
    SD t0, 8(sp)
    LW a0, 20(t0)
    LA a1, fm_size_buf
    CALL fmt_size
    LD t0, 8(sp)
    ; Draw file name
    LI a0, 138
    MV a1, s2
    MV a2, t0                   ; name
    LI a3, 0xFFF1F5F9
    LI a4, 0x00000000
    LI a5, 350
    LI a6, 16
    LI a7, 43
    ECALL
    ; Draw file size
    LI a0, 500
    MV a1, s2
    LA a2, fm_size_buf
    LI a3, 0xFF34C759
    LI a4, 0x00000000
    LI a5, 150
    LI a6, 16
    LI a7, 43
    ECALL
    J ddf_row_next
ddf_file_isdir:
    ; Draw dir name
    LI a0, 138
    MV a1, s2
    MV a2, t0                   ; name
    LI a3, 0xFF38BDF8
    LI a4, 0x00000000
    LI a5, 350
    LI a6, 16
    LI a7, 43
    ECALL
    ; Draw <DIR>
    LI a0, 500
    MV a1, s2
    LA a2, str_fm_dir_tag
    LI a3, 0xFF34C759
    LI a4, 0x00000000
    LI a5, 150
    LI a6, 16
    LI a7, 43
    ECALL
    J ddf_row_next
ddf_row_dotdot:
    LA t1, dlg_sel
    LW t1, 0(t1)
    LA t2, dlg_scroll
    LW t2, 0(t2)
    ADD t2, t2, s1
    BNE t1, t2, ddf_dd_nohl
    LI a0, 134
    MV a1, s2
    ADDI a1, a1, -1
    LI a2, 532
    LI a3, 20
    LI a4, 0xFF1E3A8A
    LI a7, 14
    ECALL
ddf_dd_nohl:
    LI a0, 138
    MV a1, s2
    LA a2, str_dotdot
    LI a3, 0xFFFFFFFF
    LI a4, 0x00000000
    LI a5, 350
    LI a6, 16
    LI a7, 43
    ECALL
    LI a0, 500
    MV a1, s2
    LA a2, str_fm_up_tag
    LI a3, 0xFF34C759
    LI a4, 0x00000000
    LI a5, 150
    LI a6, 16
    LI a7, 43
    ECALL
ddf_row_next:
    ADDI s1, s1, 1
    ADDI s2, s2, 20
    J ddf_row
ddf_name:
    ; File label + name field.
    LI a0, 132
    LI a1, 370
    LA a2, str_dlg_file
    LI a3, 0xFFFFFFFF
    LI a4, 0x00000000
    LI a7, 15
    ECALL
    LA t0, dlg_focus
    LW t1, 0(t0)
    LI t2, 2
    LI a4, 0
    BEQ t1, t2, ddf_name_box_f
    J ddf_name_box2
ddf_name_box_f:
    LI a4, 1
ddf_name_box2:
    LI a0, 196
    LI a1, 366
    LI a2, 330
    LI a3, 24
    CALL dlg_draw_box
    LA a0, dlg_name_len
    LA a1, dlg_name_caret
    LA a2, dlg_name_scroll_unused
    LI a3, 330
    CALL dlg_fix_scroll_name
    LA a0, dlg_name
    LA t0, dlg_name_len
    LW a1, 0(t0)
    LI a2, 0
    LA a3, dlg_tmp
    CALL dlg_visible_copy
    LI a0, 200
    LI a1, 370
    LA a2, dlg_tmp
    LI a3, 0xFFF1F5F9
    LI a4, 0x00000000
    LI a5, 322
    LI a6, 16
    LI a7, 43
    ECALL
    LA t0, dlg_focus
    LW t1, 0(t0)
    LI t2, 2
    BNE t1, t2, ddf_buttons
    LA t0, dlg_name_caret
    LW t1, 0(t0)
    SLLI t1, t1, 3
    ADDI a0, t1, 200
    LI a1, 370
    LI a2, 2
    LI a3, 16
    LI a4, 0xFF38BDF8
    LI a7, 14
    ECALL
ddf_buttons:
    ; Primary + Cancel buttons.
    LA t0, dlg_mode
    LW t1, 0(t0)
    LI t2, 1
    LA a2, str_dlg_save_btn
    BNE t1, t2, ddf_prim
    LA a2, str_dlg_open_btn
ddf_prim:
    LI a0, 548
    LI a1, 366
    LI a2, 112
    LI a3, 24
    LI a4, 0xFF059669
    LI a7, 14
    ECALL
    LA t0, dlg_mode
    LW t1, 0(t0)
    LI t2, 1
    LA a2, str_dlg_save_btn
    BNE t1, t2, ddf_prim_t
    LA a2, str_dlg_open_btn
ddf_prim_t:
    LI a0, 556
    LI a1, 370
    LI a3, 0xFFFFFFFF
    LI a4, 0x00000000
    LI a7, 15
    ECALL
    LI a0, 548
    LI a1, 396
    LI a2, 112
    LI a3, 24
    LI a4, 0xFF334155
    LI a7, 14
    ECALL
    LI a0, 556
    LI a1, 400
    LA a2, str_dlg_cancel_btn
    LI a3, 0xFFFFFFFF
    LI a4, 0x00000000
    LI a7, 15
    ECALL
    ; Error line (fitted, always inside its bounds).
    LI a0, 132
    LI a1, 398
    LA a2, dlg_err
    LI a3, 0xFFEF4444
    LI a4, 0x00000000
    LI a5, 400
    LI a6, 16
    LI a7, 43
    ECALL
    LD s2, 16(sp)
    LD s1, 24(sp)
    LD s0, 32(sp)
    LD ra, 40(sp)
    ADDI sp, sp, 48
    RET

; dlg_fix_scroll_name: name field has no scroll word; caret always visible
; (names cap at 63 chars * 8px = 504 > 322px, so scroll by 8-char pages).
dlg_fix_scroll_name:
    LW t0, 0(a0)
    LW t1, 0(a1)
    BLT t1, t0, dfn_ok
    SW t0, 0(a1)
dfn_ok:
    RET

; dlg_draw_confirm_body: centered Save/Discard/Cancel prompt.
dlg_draw_confirm_body:
    ADDI sp, sp, -32
    SD ra, 24(sp)
    SD s0, 16(sp)
    LI a0, 200
    LI a1, 220
    LI a2, 400
    LI a3, 160
    LA a4, str_dlg_confirm_title
    CALL draw_window_frame
    LI a0, 212
    LI a1, 256
    LA a2, str_confirm_msg
    LI a3, 0xFFFFFFFF
    LI a4, 0x00000000
    LI a7, 15
    ECALL
    LI a0, 212
    LI a1, 280
    LA a2, dlg_err
    LI a3, 0xFFEF4444
    LI a4, 0x00000000
    LI a5, 376
    LI a6, 16
    LI a7, 43
    ECALL
    ; Three buttons; focused one is blue, others slate.
    LA t0, dlg_focus
    LW s0, 0(t0)
    LI a0, 212
    LI a1, 320
    LI a2, 110
    LI a3, 28
    LI t0, 0
    BEQ s0, t0, ddc_f0
    LI a4, 0xFF334155
    J ddc_b0
ddc_f0:
    LI a4, 0xFF2563EB
ddc_b0:
    LI a7, 14
    ECALL
    LI a0, 220
    LI a1, 326
    LA a2, str_dlg_yes_btn
    LI a3, 0xFFFFFFFF
    LI a4, 0x00000000
    LI a7, 15
    ECALL
    LI a0, 334
    LI a1, 320
    LI a2, 110
    LI a3, 28
    LI t0, 1
    BEQ s0, t0, ddc_f1
    LI a4, 0xFF334155
    J ddc_b1
ddc_f1:
    LI a4, 0xFF2563EB
ddc_b1:
    LI a7, 14
    ECALL
    LI a0, 342
    LI a1, 326
    LA a2, str_dlg_no_btn
    LI a3, 0xFFFFFFFF
    LI a4, 0x00000000
    LI a7, 15
    ECALL
    LI a0, 456
    LI a1, 320
    LI a2, 110
    LI a3, 28
    LI t0, 2
    BEQ s0, t0, ddc_f2
    LI a4, 0xFF334155
    J ddc_b2
ddc_f2:
    LI a4, 0xFF2563EB
ddc_b2:
    LI a7, 14
    ECALL
    LI a0, 464
    LI a1, 326
    LA a2, str_dlg_cancel_btn
    LI a3, 0xFFFFFFFF
    LI a4, 0x00000000
    LI a7, 15
    ECALL
    LD s0, 16(sp)
    LD ra, 24(sp)
    ADDI sp, sp, 32
    RET

; dlg_confirm_action: a0 = button index (0=Save, 1=Discard, 2=Cancel)
dlg_confirm_action:
    ADDI sp, sp, -32
    SD ra, 24(sp)
    SD s0, 16(sp)
    MV s0, a0
    LI t0, 2
    BEQ s0, t0, dca_cancel
    LI t0, 1
    BEQ s0, t0, dca_discard
    ; Save action (0)
    LA t0, note_path
    LBU t1, 0(t0)
    BEQ t1, zero, dca_untitled
    LA a0, note_path
    CALL notepad_write_path
    BLT a0, zero, dca_save_fail
    LA t0, note_dirty
    SW zero, 0(t0)
    CALL dlg_dismiss
    CALL do_pending
    J dca_done
dca_untitled:
    CALL dlg_show_saveas
    J dca_done
dca_save_fail:
    CALL dlg_fs_error
    CALL flag_redraw
    J dca_done
dca_discard:
    LA t0, note_dirty
    SW zero, 0(t0)
    CALL dlg_dismiss
    CALL do_pending
    J dca_done
dca_cancel:
    CALL dlg_cancel
dca_done:
    LD s0, 16(sp)
    LD ra, 24(sp)
    ADDI sp, sp, 32
    RET

dlg_on_key_confirm:
    ADDI sp, sp, -16
    SD ra, 8(sp)
    LI t0, 27                 ; Esc
    BEQ a1, t0, dokc_esc
    LI t0, 9                  ; Tab
    BEQ a1, t0, dokc_tab
    LI t0, 258                ; Left
    BEQ a1, t0, dokc_left
    LI t0, 259                ; Right
    BEQ a1, t0, dokc_right
    LI t0, 13                 ; Enter
    BEQ a1, t0, dokc_act
    LI t0, 10
    BEQ a1, t0, dokc_act
    LI t0, 32                 ; Space
    BEQ a1, t0, dokc_act
    LI t0, 83                 ; 'S'
    BEQ a1, t0, dokc_save
    LI t0, 115                ; 's'
    BEQ a1, t0, dokc_save
    LI t0, 68                 ; 'D'
    BEQ a1, t0, dokc_disc
    LI t0, 100                ; 'd'
    BEQ a1, t0, dokc_disc
    LI t0, 67                 ; 'C'
    BEQ a1, t0, dokc_canc
    LI t0, 99                 ; 'c'
    BEQ a1, t0, dokc_canc
    J dokc_ret
dokc_esc:
    LI a0, 2
    CALL dlg_confirm_action
    J dokc_ret
dokc_tab:
    LA t0, dlg_focus
    LW t1, 0(t0)
    ADDI t1, t1, 1
    LI t2, 3
    REM t1, t1, t2
    SW t1, 0(t0)
    CALL flag_redraw
    J dokc_ret
dokc_left:
    LA t0, dlg_focus
    LW t1, 0(t0)
    ADDI t1, t1, -1
    BGE t1, zero, dokc_set_f
    LI t1, 0
    J dokc_set_f
dokc_right:
    LA t0, dlg_focus
    LW t1, 0(t0)
    ADDI t1, t1, 1
    LI t2, 2
    BLE t1, t2, dokc_set_f
    LI t1, 2
dokc_set_f:
    LA t0, dlg_focus
    SW t1, 0(t0)
    CALL flag_redraw
    J dokc_ret
dokc_act:
    LA t0, dlg_focus
    LW a0, 0(t0)
    CALL dlg_confirm_action
    J dokc_ret
dokc_save:
    LI a0, 0
    CALL dlg_confirm_action
    J dokc_ret
dokc_disc:
    LI a0, 1
    CALL dlg_confirm_action
    J dokc_ret
dokc_canc:
    LI a0, 2
    CALL dlg_confirm_action
    J dokc_ret
dokc_ret:
    LD ra, 8(sp)
    ADDI sp, sp, 16
    RET

dlg_on_click_confirm:
    ADDI sp, sp, -16
    SD ra, 8(sp)
    LI t0, 320
    BLT a1, t0, docc_ret
    LI t0, 348
    BGT a1, t0, docc_ret
    LI t0, 212
    BLT a0, t0, docc_ret
    LI t0, 322
    BLE a0, t0, docc_save
    LI t0, 334
    BLT a0, t0, docc_ret
    LI t0, 444
    BLE a0, t0, docc_disc
    LI t0, 456
    BLT a0, t0, docc_ret
    LI t0, 566
    BLE a0, t0, docc_canc
    J docc_ret
docc_save:
    LI a0, 0
    CALL dlg_confirm_action
    J docc_ret
docc_disc:
    LI a0, 1
    CALL dlg_confirm_action
    J docc_ret
docc_canc:
    LI a0, 2
    CALL dlg_confirm_action
    J docc_ret
docc_ret:
    LD ra, 8(sp)
    ADDI sp, sp, 16
    RET

; ==============================================================================
; Overwrite Confirmation Dialog (Mode 4)
; ==============================================================================
dlg_draw_overwrite_body:
    ADDI sp, sp, -32
    SD ra, 24(sp)
    SD s0, 16(sp)
    LI a0, 200
    LI a1, 220
    LI a2, 400
    LI a3, 160
    LA a4, str_dlg_overwrite_title
    CALL draw_window_frame
    LI a0, 212
    LI a1, 256
    LA a2, str_dlg_exists
    LI a3, 0xFFFFFFFF
    LI a4, 0x00000000
    LI a7, 15
    ECALL
    LI a0, 212
    LI a1, 280
    LA a2, dlg_err
    LI a3, 0xFFEF4444
    LI a4, 0x00000000
    LI a5, 376
    LI a6, 16
    LI a7, 43
    ECALL
    ; Three buttons: 0=Yes, 1=No, 2=Cancel
    LA t0, dlg_focus
    LW s0, 0(t0)

    ; Button 0: [Yes] (212..322)
    LI a0, 212
    LI a1, 320
    LI a2, 110
    LI a3, 28
    LI t0, 0
    BEQ s0, t0, ddo_f0
    LI a4, 0xFF334155
    J ddo_b0
ddo_f0:
    LI a4, 0xFF2563EB
ddo_b0:
    LI a7, 14
    ECALL
    LI a0, 220
    LI a1, 326
    LA a2, str_dlg_btn_yes
    LI a3, 0xFFFFFFFF
    LI a4, 0x00000000
    LI a7, 15
    ECALL

    ; Button 1: [No] (334..444)
    LI a0, 334
    LI a1, 320
    LI a2, 110
    LI a3, 28
    LI t0, 1
    BEQ s0, t0, ddo_f1
    LI a4, 0xFF334155
    J ddo_b1
ddo_f1:
    LI a4, 0xFF2563EB
ddo_b1:
    LI a7, 14
    ECALL
    LI a0, 342
    LI a1, 326
    LA a2, str_dlg_btn_no
    LI a3, 0xFFFFFFFF
    LI a4, 0x00000000
    LI a7, 15
    ECALL

    ; Button 2: [Cancel] (456..566)
    LI a0, 456
    LI a1, 320
    LI a2, 110
    LI a3, 28
    LI t0, 2
    BEQ s0, t0, ddo_f2
    LI a4, 0xFF334155
    J ddo_b2
ddo_f2:
    LI a4, 0xFF2563EB
ddo_b2:
    LI a7, 14
    ECALL
    LI a0, 464
    LI a1, 326
    LA a2, str_dlg_cancel_btn
    LI a3, 0xFFFFFFFF
    LI a4, 0x00000000
    LI a7, 15
    ECALL

    LD s0, 16(sp)
    LD ra, 24(sp)
    ADDI sp, sp, 32
    RET

dlg_overwrite_action:
    ADDI sp, sp, -16
    SD ra, 8(sp)
    LI t0, 0
    BEQ a0, t0, doa_yes
    LI t0, 1
    BEQ a0, t0, doa_no
    ; 2: Cancel
    CALL dlg_cancel
    J doa_done
doa_yes:
    CALL dpa_do_write
    J doa_done
doa_no:
    ; Restore Save As dialog
    LI t0, 2
    LA t1, dlg_active
    SW t0, 0(t1)
    LI t0, 2                 ; focus on filename field
    LA t1, dlg_focus
    SW t0, 0(t1)
    LA t0, dlg_err
    SB zero, 0(t0)
    CALL flag_redraw
doa_done:
    LD ra, 8(sp)
    ADDI sp, sp, 16
    RET

dlg_on_key_overwrite:
    ADDI sp, sp, -16
    SD ra, 8(sp)
    LI t0, 27                 ; Esc
    BEQ a1, t0, doko_esc
    LI t0, 9                  ; Tab
    BEQ a1, t0, doko_tab
    LI t0, 258                ; Left
    BEQ a1, t0, doko_left
    LI t0, 259                ; Right
    BEQ a1, t0, doko_right
    LI t0, 13                 ; Enter
    BEQ a1, t0, doko_act
    LI t0, 10
    BEQ a1, t0, doko_act
    LI t0, 32                 ; Space
    BEQ a1, t0, doko_act
    LI t0, 121                ; 'y'
    BEQ a1, t0, doko_yes
    LI t0, 89                 ; 'Y'
    BEQ a1, t0, doko_yes
    LI t0, 110                ; 'n'
    BEQ a1, t0, doko_no
    LI t0, 78                 ; 'N'
    BEQ a1, t0, doko_no
    LI t0, 67                 ; 'C'
    BEQ a1, t0, doko_esc
    LI t0, 99                 ; 'c'
    BEQ a1, t0, doko_esc
    J doko_ret

doko_esc:
    LI a0, 2                  ; Cancel
    CALL dlg_overwrite_action
    J doko_ret

doko_yes:
    LI a0, 0                  ; Yes
    CALL dlg_overwrite_action
    J doko_ret

doko_no:
    LI a0, 1                  ; No
    CALL dlg_overwrite_action
    J doko_ret

doko_tab:
    LA t0, dlg_focus
    LW t1, 0(t0)
    ADDI t1, t1, 1
    LI t2, 3
    REM t1, t1, t2
    SW t1, 0(t0)
    CALL flag_redraw
    J doko_ret

doko_left:
    LA t0, dlg_focus
    LW t1, 0(t0)
    ADDI t1, t1, 2
    LI t2, 3
    REM t1, t1, t2
    SW t1, 0(t0)
    CALL flag_redraw
    J doko_ret

doko_right:
    LA t0, dlg_focus
    LW t1, 0(t0)
    ADDI t1, t1, 1
    LI t2, 3
    REM t1, t1, t2
    SW t1, 0(t0)
    CALL flag_redraw
    J doko_ret

doko_act:
    LA t0, dlg_focus
    LW a0, 0(t0)
    CALL dlg_overwrite_action
    J doko_ret

doko_ret:
    LD ra, 8(sp)
    ADDI sp, sp, 16
    RET

dlg_on_click_overwrite:
    ADDI sp, sp, -16
    SD ra, 8(sp)
    LI t0, 320
    BLT a1, t0, dco_chk_frame
    LI t0, 348
    BGT a1, t0, dco_chk_frame
    LI t0, 212
    BLT a0, t0, dco_chk_frame
    LI t0, 322
    BGT a0, t0, dco_chk_b1
    LI a0, 0
    CALL dlg_overwrite_action
    J dco_ret
dco_chk_b1:
    LI t0, 334
    BLT a0, t0, dco_chk_frame
    LI t0, 444
    BGT a0, t0, dco_chk_b2
    LI a0, 1
    CALL dlg_overwrite_action
    J dco_ret
dco_chk_b2:
    LI t0, 456
    BLT a0, t0, dco_chk_frame
    LI t0, 566
    BGT a0, t0, dco_chk_frame
    LI a0, 2
    CALL dlg_overwrite_action
    J dco_ret
dco_chk_frame:
    CALL flag_redraw
dco_ret:
    LD ra, 8(sp)
    ADDI sp, sp, 16
    RET

dlg_activate_sel:
    ADDI sp, sp, -32
    SD ra, 24(sp)
    SD s0, 16(sp)
    LA t0, dlg_sel
    LW a0, 0(t0)
    CALL dlg_row_entry
    LI t0, -1
    BEQ a0, t0, das_parent
    BLT a0, zero, das_ret
    LI t0, 28
    MUL t0, a0, t0
    LA t1, dlg_list_buf
    ADD s0, t1, t0
    LBU t1, 13(s0)
    ANDI t1, t1, 0x10
    BNE t1, zero, das_dir
    ; Regular file: copy name to dlg_name
    LA a0, dlg_name
    MV a1, s0
    CALL str_copy
    LA a0, dlg_name
    CALL str_len
    LA t0, dlg_name_len
    SW a0, 0(t0)
    LA t0, dlg_name_caret
    SW a0, 0(t0)
    CALL dlg_primary_action
    J das_ret
das_parent:
    LA a0, dlg_dir
    CALL path_parent
    LA a0, dlg_path
    LA a1, dlg_dir
    CALL str_copy
    LA a0, dlg_dir
    CALL str_len
    LA t0, dlg_path_len
    SW a0, 0(t0)
    LA t0, dlg_path_caret
    SW a0, 0(t0)
    LA t0, dlg_sel
    SW zero, 0(t0)
    LA t0, dlg_scroll
    SW zero, 0(t0)
    CALL dlg_refresh_list
    CALL flag_redraw
    J das_ret
das_dir:
    LA a0, dlg_dir
    MV a1, s0
    LA a2, dlg_tmp_path
    LI a3, 256
    CALL path_join
    BLT a0, zero, das_ret
    LA a0, dlg_dir
    LA a1, dlg_tmp_path
    CALL str_copy
    LA a0, dlg_path
    LA a1, dlg_tmp_path
    CALL str_copy
    LA a0, dlg_dir
    CALL str_len
    LA t0, dlg_path_len
    SW a0, 0(t0)
    LA t0, dlg_path_caret
    SW a0, 0(t0)
    LA t0, dlg_sel
    SW zero, 0(t0)
    LA t0, dlg_scroll
    SW zero, 0(t0)
    CALL dlg_refresh_list
    CALL flag_redraw
das_ret:
    LD s0, 16(sp)
    LD ra, 24(sp)
    ADDI sp, sp, 32
    RET

dlg_primary_action:
    ADDI sp, sp, -32
    SD ra, 24(sp)
    SD s0, 16(sp)
    LA a0, dlg_name
    LA a1, dlg_name           ; trimmed display string back into dlg_name
    LI a2, 0                  ; skip canonical
    CALL fat_validate_shortname
    BEQ a0, zero, dpa_name_valid
    CALL fat_error_msg
    MV a1, a0
    LA a0, dlg_err
    CALL str_copy
    CALL flag_redraw
    J dpa_ret
dpa_name_valid:
    LA a0, dlg_name
    CALL str_len
    LA t0, dlg_name_len
    SW a0, 0(t0)
    LA t0, dlg_name_caret
    SW a0, 0(t0)
    LA a0, dlg_dir
    LA a1, dlg_name
    LA a2, dlg_tmp_path
    LI a3, 256
    CALL path_join
    BLT a0, zero, dpa_badname
dpa_check_mode:
    LA t0, dlg_mode
    LW t1, 0(t0)
    LI t2, 1
    BEQ t1, t2, dpa_do_open
    ; Save As mode (2)
    LA a0, dlg_tmp_path
    LA a1, term_dirent
    LI a7, 32                 ; SYS_FS_STAT
    ECALL
    BLT a0, zero, dpa_stat_neg
    ; Destination exists!
    LA t0, term_dirent
    LBU t1, 13(t0)
    ANDI t1, t1, 0x10
    BNE t1, zero, dpa_badname ; cannot overwrite directory
    ; Show Mode 4 Overwrite Confirmation
    LI t0, 4
    LA t1, dlg_active
    SW t0, 0(t1)
    LI t0, 0                  ; focus on [Yes]
    LA t1, dlg_focus
    SW t0, 0(t1)
    LA t0, dlg_err
    SB zero, 0(t0)
    CALL flag_redraw
    J dpa_ret
dpa_stat_neg:
    LI t0, -3                 ; DFS_ERR_NOT_FOUND
    BEQ a0, t0, dpa_do_write
    J dpa_fs_fail
dpa_do_write:
    LA a0, dlg_tmp_path
    CALL notepad_write_path
    BLT a0, zero, dpa_fs_fail
    LA a0, note_path
    LA a1, dlg_tmp_path
    CALL str_copy
    LA t0, note_dirty
    SW zero, 0(t0)
    CALL dlg_dismiss
    CALL do_pending
    J dpa_ret
dpa_do_open:
    LA a0, dlg_tmp_path
    LA a1, term_dirent
    LI a7, 32                 ; SYS_FS_STAT
    ECALL
    BLT a0, zero, dpa_fs_fail
    LA t0, term_dirent
    LBU t1, 13(t0)
    ANDI t1, t1, 0x10
    BEQ t1, zero, dpa_open_file
    ; It is a directory -> navigate into it!
    LA a0, dlg_dir
    LA a1, dlg_tmp_path
    CALL str_copy
    LA a0, dlg_path
    LA a1, dlg_tmp_path
    CALL str_copy
    LA a0, dlg_dir
    CALL str_len
    LA t0, dlg_path_len
    SW a0, 0(t0)
    LA t0, dlg_path_caret
    SW a0, 0(t0)
    LA t0, dlg_sel
    SW zero, 0(t0)
    LA t0, dlg_scroll
    SW zero, 0(t0)
    LA t0, dlg_name
    SB zero, 0(t0)
    LA t0, dlg_name_len
    SW zero, 0(t0)
    LA t0, dlg_name_caret
    SW zero, 0(t0)
    CALL dlg_refresh_list
    CALL flag_redraw
    J dpa_ret
dpa_open_file:
    LA a0, dlg_tmp_path
    CALL notepad_load_path
    BLT a0, zero, dpa_fs_fail
    CALL dlg_dismiss
    CALL do_pending
    J dpa_ret
dpa_badname:
    LA a0, dlg_err
    LA a1, str_dlg_bad_name
    CALL str_copy
    CALL flag_redraw
    J dpa_ret
dpa_fs_fail:
    CALL dlg_fs_error
    CALL flag_redraw
dpa_ret:
    LD s0, 16(sp)
    LD ra, 24(sp)
    ADDI sp, sp, 32
    RET

dlg_on_key:
    ADDI sp, sp, -32
    SD ra, 24(sp)
    SD s0, 16(sp)
    SD s1, 8(sp)
    MV s0, a1                 ; keycode
    MV s1, a4                 ; modifiers
    LA t0, dlg_active
    LW t1, 0(t0)
    LI t2, 3
    BEQ t1, t2, dok_confirm
    LI t2, 4
    BEQ t1, t2, dok_overwrite
    ; File dialog (mode 1 or 2)
    LI t0, 27                 ; Esc
    BEQ s0, t0, dok_esc
    LI t0, 9                  ; Tab
    BEQ s0, t0, dok_tab
    LA t0, dlg_focus
    LW t1, 0(t0)
    BEQ t1, zero, dok_focus_path
    LI t2, 1
    BEQ t1, t2, dok_focus_list
    LI t2, 2
    BEQ t1, t2, dok_focus_name
    LI t2, 3
    BEQ t1, t2, dok_focus_prim
    LI t2, 4
    BEQ t1, t2, dok_focus_canc
    J dok_ret
dok_confirm:
    MV a1, s0
    MV a4, s1
    CALL dlg_on_key_confirm
    J dok_ret
dok_overwrite:
    MV a1, s0
    MV a4, s1
    CALL dlg_on_key_overwrite
    J dok_ret
dok_esc:
    CALL dlg_cancel
    J dok_ret
dok_tab:
    LA t0, dlg_focus
    LW t1, 0(t0)
    ADDI t1, t1, 1
    LI t2, 5
    REM t1, t1, t2
    SW t1, 0(t0)
    CALL flag_redraw
    J dok_ret
dok_focus_prim:
    LI t0, 13
    BEQ s0, t0, dok_p_act
    LI t0, 10
    BEQ s0, t0, dok_p_act
    LI t0, 32
    BEQ s0, t0, dok_p_act
    J dok_ret
dok_p_act:
    CALL dlg_primary_action
    J dok_ret
dok_focus_canc:
    LI t0, 13
    BEQ s0, t0, dok_c_act
    LI t0, 10
    BEQ s0, t0, dok_c_act
    LI t0, 32
    BEQ s0, t0, dok_c_act
    J dok_ret
dok_c_act:
    CALL dlg_cancel
    J dok_ret

dok_focus_path:
    LI t0, 13
    BEQ s0, t0, dok_path_enter
    LI t0, 10
    BEQ s0, t0, dok_path_enter
    LI t0, 258                ; Left
    BEQ s0, t0, dok_path_left
    LI t0, 259                ; Right
    BEQ s0, t0, dok_path_right
    LI t0, 270                ; Home
    BEQ s0, t0, dok_path_home
    LI t0, 271                ; End
    BEQ s0, t0, dok_path_end
    LI t0, 8                  ; Backspace
    BEQ s0, t0, dok_path_bs
    LI t0, 272                ; Delete
    BEQ s0, t0, dok_path_del
    LI t0, 32
    BLT s0, t0, dok_ret
    LI t0, 126
    BGT s0, t0, dok_ret
    LA a0, dlg_path
    LA a1, dlg_path_len
    LA a2, dlg_path_caret
    LI a3, 250
    MV a4, s0
    CALL field_insert
    CALL dok_path_fix
    CALL flag_redraw
    J dok_ret
dok_path_enter:
    ; Navigate to typed path if directory
    LA a0, dlg_path
    LA a1, term_dirent
    LI a7, 32                 ; SYS_FS_STAT
    ECALL
    BLT a0, zero, dok_path_bad
    LA t0, term_dirent
    LBU t1, 13(t0)
    ANDI t1, t1, 0x10
    BEQ t1, zero, dok_path_bad
    LA a0, dlg_dir
    LA a1, dlg_path
    CALL str_copy
    LA a0, dlg_dir
    CALL str_len
    LA t0, dlg_path_len
    SW a0, 0(t0)
    LA t0, dlg_path_caret
    SW a0, 0(t0)
    LA t0, dlg_sel
    SW zero, 0(t0)
    LA t0, dlg_scroll
    SW zero, 0(t0)
    LA t0, dlg_focus
    LI t1, 2
    SW t1, 0(t0)
    CALL dlg_refresh_list
    CALL flag_redraw
    J dok_ret
dok_path_bad:
    LA a0, dlg_err
    LA a1, str_dlg_not_found
    CALL str_copy
    CALL flag_redraw
    J dok_ret
dok_path_left:
    LA t0, dlg_path_caret
    LW t1, 0(t0)
    ADDI t1, t1, -1
    BLT t1, zero, dok_ret
    SW t1, 0(t0)
    CALL dok_path_fix
    CALL flag_redraw
    J dok_ret
dok_path_right:
    LA t0, dlg_path_len
    LW t2, 0(t0)
    LA t0, dlg_path_caret
    LW t1, 0(t0)
    ADDI t1, t1, 1
    BGT t1, t2, dok_ret
    SW t1, 0(t0)
    CALL dok_path_fix
    CALL flag_redraw
    J dok_ret
dok_path_home:
    LA t0, dlg_path_caret
    SW zero, 0(t0)
    CALL dok_path_fix
    CALL flag_redraw
    J dok_ret
dok_path_end:
    LA t0, dlg_path_len
    LW t1, 0(t0)
    LA t0, dlg_path_caret
    SW t1, 0(t0)
    CALL dok_path_fix
    CALL flag_redraw
    J dok_ret
dok_path_bs:
    LA a0, dlg_path
    LA a1, dlg_path_len
    LA a2, dlg_path_caret
    CALL field_bs
    CALL dok_path_fix
    CALL flag_redraw
    J dok_ret
dok_path_del:
    LA a0, dlg_path
    LA a1, dlg_path_len
    LA a2, dlg_path_caret
    CALL field_del
    CALL dok_path_fix
    CALL flag_redraw
    J dok_ret
dok_path_fix:
    ADDI sp, sp, -16
    SD ra, 8(sp)
    LA a0, dlg_path_len
    LA a1, dlg_path_caret
    LA a2, dlg_path_scroll
    LI a3, 472
    CALL dlg_fix_scroll
    LD ra, 8(sp)
    ADDI sp, sp, 16
    RET

dok_focus_list:
    LI t0, 13
    BEQ s0, t0, dok_list_enter
    LI t0, 10
    BEQ s0, t0, dok_list_enter
    LI t0, 256                ; Up
    BEQ s0, t0, dok_list_up
    LI t0, 257                ; Down
    BEQ s0, t0, dok_list_down
    LI t0, 273                ; PgUp
    BEQ s0, t0, dok_list_pgup
    LI t0, 274                ; PgDn
    BEQ s0, t0, dok_list_pgdn
    LI t0, 270                ; Home
    BEQ s0, t0, dok_list_home
    LI t0, 271                ; End
    BEQ s0, t0, dok_list_end
    LI t0, 8                  ; Backspace
    BEQ s0, t0, dok_list_back
    J dok_ret
dok_list_enter:
    CALL dlg_activate_sel
    J dok_ret
dok_list_up:
    LA t0, dlg_sel
    LW t1, 0(t0)
    ADDI t1, t1, -1
    BLT t1, zero, dok_ret
    SW t1, 0(t0)
    CALL dok_list_sync
    CALL flag_redraw
    J dok_ret
dok_list_down:
    CALL dlg_vis_count
    MV t2, a0
    LA t0, dlg_sel
    LW t1, 0(t0)
    ADDI t1, t1, 1
    BGE t1, t2, dok_ret
    SW t1, 0(t0)
    CALL dok_list_sync
    CALL flag_redraw
    J dok_ret
dok_list_pgup:
    LA t0, dlg_sel
    LW t1, 0(t0)
    ADDI t1, t1, -10
    BGE t1, zero, dok_lpu_ok
    LI t1, 0
dok_lpu_ok:
    LA t0, dlg_sel
    SW t1, 0(t0)
    CALL dok_list_sync
    CALL flag_redraw
    J dok_ret
dok_list_pgdn:
    CALL dlg_vis_count
    MV t2, a0
    LA t0, dlg_sel
    LW t1, 0(t0)
    ADDI t1, t1, 10
    BLT t1, t2, dok_lpd_ok
    ADDI t1, t2, -1
    BLT t1, zero, dok_ret
dok_lpd_ok:
    LA t0, dlg_sel
    SW t1, 0(t0)
    CALL dok_list_sync
    CALL flag_redraw
    J dok_ret
dok_list_home:
    LA t0, dlg_sel
    SW zero, 0(t0)
    CALL dok_list_sync
    CALL flag_redraw
    J dok_ret
dok_list_end:
    CALL dlg_vis_count
    ADDI t1, a0, -1
    BLT t1, zero, dok_ret
    LA t0, dlg_sel
    SW t1, 0(t0)
    CALL dok_list_sync
    CALL flag_redraw
    J dok_ret
dok_list_back:
    LA a0, dlg_dir
    CALL path_parent
    LA a0, dlg_path
    LA a1, dlg_dir
    CALL str_copy
    LA a0, dlg_dir
    CALL str_len
    LA t0, dlg_path_len
    SW a0, 0(t0)
    LA t0, dlg_path_caret
    SW a0, 0(t0)
    LA t0, dlg_sel
    SW zero, 0(t0)
    LA t0, dlg_scroll
    SW zero, 0(t0)
    CALL dlg_refresh_list
    CALL flag_redraw
    J dok_ret

dok_list_sync:
    ADDI sp, sp, -32
    SD ra, 24(sp)
    SD s0, 16(sp)
    LA t0, dlg_sel
    LW s0, 0(t0)
    LA t0, dlg_scroll
    LW t1, 0(t0)
    BLT s0, t1, dls_sc_up
    ADDI t2, t1, 10
    BGE s0, t2, dls_sc_down
    J dls_up_name
dls_sc_up:
    SW s0, 0(t0)
    J dls_up_name
dls_sc_down:
    ADDI t2, s0, -9
    SW t2, 0(t0)
dls_up_name:
    MV a0, s0
    CALL dlg_row_entry
    BLT a0, zero, dls_ret
    LI t0, 28
    MUL t0, a0, t0
    LA t1, dlg_list_buf
    ADD t0, t1, t0
    LBU t1, 13(t0)
    ANDI t1, t1, 0x10
    BNE t1, zero, dls_ret
    ; Selected item is a file: update dlg_name
    LA a0, dlg_name
    MV a1, t0
    CALL str_copy
    LA a0, dlg_name
    CALL str_len
    LA t0, dlg_name_len
    SW a0, 0(t0)
    LA t0, dlg_name_caret
    SW a0, 0(t0)
dls_ret:
    LD s0, 16(sp)
    LD ra, 24(sp)
    ADDI sp, sp, 32
    RET

dok_focus_name:
    LI t0, 13
    BEQ s0, t0, dok_name_enter
    LI t0, 10
    BEQ s0, t0, dok_name_enter
    LI t0, 258                ; Left
    BEQ s0, t0, dok_name_left
    LI t0, 259                ; Right
    BEQ s0, t0, dok_name_right
    LI t0, 270                ; Home
    BEQ s0, t0, dok_name_home
    LI t0, 271                ; End
    BEQ s0, t0, dok_name_end
    LI t0, 8                  ; Backspace
    BEQ s0, t0, dok_name_bs
    LI t0, 272                ; Delete
    BEQ s0, t0, dok_name_del
    LI t0, 32
    BLT s0, t0, dok_ret
    LI t0, 126
    BGT s0, t0, dok_ret
    LA a0, dlg_name
    LA a1, dlg_name_len
    LA a2, dlg_name_caret
    LI a3, 60
    MV a4, s0
    CALL field_insert
    CALL flag_redraw
    J dok_ret
dok_name_enter:
    CALL dlg_primary_action
    J dok_ret
dok_name_left:
    LA t0, dlg_name_caret
    LW t1, 0(t0)
    ADDI t1, t1, -1
    BLT t1, zero, dok_ret
    SW t1, 0(t0)
    CALL flag_redraw
    J dok_ret
dok_name_right:
    LA t0, dlg_name_len
    LW t2, 0(t0)
    LA t0, dlg_name_caret
    LW t1, 0(t0)
    ADDI t1, t1, 1
    BGT t1, t2, dok_ret
    SW t1, 0(t0)
    CALL flag_redraw
    J dok_ret
dok_name_home:
    LA t0, dlg_name_caret
    SW zero, 0(t0)
    CALL flag_redraw
    J dok_ret
dok_name_end:
    LA t0, dlg_name_len
    LW t1, 0(t0)
    LA t0, dlg_name_caret
    SW t1, 0(t0)
    CALL flag_redraw
    J dok_ret
dok_name_bs:
    LA a0, dlg_name
    LA a1, dlg_name_len
    LA a2, dlg_name_caret
    CALL field_bs
    CALL flag_redraw
    J dok_ret
dok_name_del:
    LA a0, dlg_name
    LA a1, dlg_name_len
    LA a2, dlg_name_caret
    CALL field_del
    CALL flag_redraw
    J dok_ret

dok_ret:
    LD s1, 8(sp)
    LD s0, 16(sp)
    LD ra, 24(sp)
    ADDI sp, sp, 32
    RET

dlg_on_click:
    ADDI sp, sp, -32
    SD ra, 24(sp)
    SD s0, 16(sp)
    SD s1, 8(sp)
    MV s0, a0                 ; X
    MV s1, a1                 ; Y
    LA t0, dlg_active
    LW t1, 0(t0)
    LI t2, 3
    BEQ t1, t2, doc_confirm
    LI t2, 4
    BEQ t1, t2, doc_overwrite
    ; Primary button: X: 548..660, Y: 366..390
    LI t0, 548
    BLT s0, t0, doc_chk_cancel
    LI t0, 660
    BGT s0, t0, doc_chk_cancel
    LI t0, 366
    BLT s1, t0, doc_chk_cancel
    LI t0, 390
    BGT s1, t0, doc_chk_cancel
    LA t0, dlg_focus
    LI t1, 3
    SW t1, 0(t0)
    CALL dlg_primary_action
    J doc_done

doc_chk_cancel:
    ; Cancel button: X: 548..660, Y: 396..420
    LI t0, 548
    BLT s0, t0, doc_chk_path
    LI t0, 660
    BGT s0, t0, doc_chk_path
    LI t0, 396
    BLT s1, t0, doc_chk_path
    LI t0, 420
    BGT s1, t0, doc_chk_path
    CALL dlg_cancel
    J doc_done

doc_chk_path:
    ; Path edit box: X: 196..668, Y: 118..142
    LI t0, 196
    BLT s0, t0, doc_chk_name
    LI t0, 668
    BGT s0, t0, doc_chk_name
    LI t0, 118
    BLT s1, t0, doc_chk_name
    LI t0, 142
    BGT s1, t0, doc_chk_name
    LA t0, dlg_focus
    SW zero, 0(t0)
    LA t0, dlg_path_len
    LW a0, 0(t0)
    MV a1, s0
    LI a2, 200
    LA t0, dlg_path_scroll
    LW a3, 0(t0)
    CALL field_pick
    LA t0, dlg_path_caret
    SW a0, 0(t0)
    CALL flag_redraw
    J doc_done

doc_chk_name:
    ; Filename edit box: X: 196..526, Y: 366..390
    LI t0, 196
    BLT s0, t0, doc_chk_list
    LI t0, 526
    BGT s0, t0, doc_chk_list
    LI t0, 366
    BLT s1, t0, doc_chk_list
    LI t0, 390
    BGT s1, t0, doc_chk_list
    LA t0, dlg_focus
    LI t1, 2
    SW t1, 0(t0)
    LA t0, dlg_name_len
    LW a0, 0(t0)
    MV a1, s0
    LI a2, 200
    LI a3, 0
    CALL field_pick
    LA t0, dlg_name_caret
    SW a0, 0(t0)
    CALL flag_redraw
    J doc_done

doc_chk_list:
    ; List box: X: 132..668, Y: 150..360
    LI t0, 132
    BLT s0, t0, doc_outside
    LI t0, 668
    BGT s0, t0, doc_outside
    LI t0, 154
    BLT s1, t0, doc_outside
    LI t0, 354
    BGT s1, t0, doc_outside
    ADDI t0, s1, -154
    LI t1, 20
    DIVU t0, t0, t1           ; row 0..9
    LA t1, dlg_scroll
    LW t1, 0(t1)
    ADD s0, t0, t1            ; visible row
    CALL dlg_vis_count
    BGE s0, a0, doc_outside
    ; Valid row clicked!
    LA t0, dlg_focus
    LI t1, 1
    SW t1, 0(t0)
    LI a7, 13
    ECALL                     ; current ms
    MV t2, a0
    LA t3, dlg_last_row
    LW t4, 0(t3)
    BNE t4, s0, doc_l_single
    LA t3, dlg_last_ms
    LD t5, 0(t3)
    SUB t5, t2, t5
    LI t6, 400
    BGT t5, t6, doc_l_single
    ; Double click on list row!
    LA t3, dlg_last_row
    LI t4, -1
    SW t4, 0(t3)
    LA t0, dlg_sel
    SW s0, 0(t0)
    CALL dlg_activate_sel
    J doc_done
doc_l_single:
    LA t3, dlg_last_row
    SW s0, 0(t3)
    LA t3, dlg_last_ms
    SD t2, 0(t3)
    LA t0, dlg_sel
    SW s0, 0(t0)
    CALL dok_list_sync
    CALL flag_redraw
    J doc_done

doc_confirm:
    MV a0, s0
    MV a1, s1
    MV a2, a2
    CALL dlg_on_click_confirm
    J doc_done

doc_overwrite:
    MV a0, s0
    MV a1, s1
    CALL dlg_on_click_overwrite
    J doc_done

doc_outside:
    ; Modal isolation: consume clicks outside controls
    CALL flag_redraw
doc_done:
    LD s1, 8(sp)
    LD s0, 16(sp)
    LD ra, 24(sp)
    ADDI sp, sp, 32
    RET

; utf8_valid: a0=buf, a1=len -> a0=1 valid, 0 invalid.
utf8_valid:
    MV t0, a0
    MV t1, a1
    LI t2, 0
uv_loop:
    BGE t2, t1, uv_ok
    ADD t3, t0, t2
    LBU t3, 0(t3)
    LI t4, 0x80
    BLT t3, t4, uv_next1
    LI t4, 0xC2
    BLT t3, t4, uv_bad
    LI t4, 0xE0
    BLT t3, t4, uv_need1
    LI t4, 0xF0
    BLT t3, t4, uv_need2
    LI t4, 0xF5
    BGE t3, t4, uv_bad
    J uv_need3
uv_need1:
    LI t4, 1
    J uv_check
uv_need2:
    LI t4, 2
    J uv_check
uv_need3:
    LI t4, 3
uv_check:
    ADD t5, t2, t4
    BGE t5, t1, uv_bad
    MV t6, t2
uv_cont:
    BGE t6, t5, uv_seq_ok
    ADDI t6, t6, 1
    ADD a0, t0, t6
    LBU a0, 0(a0)
    ANDI a0, a0, 0xC0
    LI t3, 0x80
    BNE a0, t3, uv_bad
    J uv_cont
uv_seq_ok:
    ; Reject overlongs and surrogates by first-byte/lead rules.
    ADD t3, t0, t2
    LBU t3, 0(t3)
    LI a0, 0xE0
    BNE t3, a0, uv_ne0
    ADD t3, t0, t2
    ADDI t3, t3, 1
    LBU t3, 0(t3)
    LI a0, 0xA0
    BLT t3, a0, uv_bad
uv_ne0:
    ADD t3, t0, t2
    LBU t3, 0(t3)
    LI a0, 0xED
    BNE t3, a0, uv_ned
    ADD t3, t0, t2
    ADDI t3, t3, 1
    LBU t3, 0(t3)
    LI a0, 0xA0
    BGE t3, a0, uv_bad
uv_ned:
    ADD t3, t0, t2
    LBU t3, 0(t3)
    LI a0, 0xF0
    BNE t3, a0, uv_nf0
    ADD t3, t0, t2
    ADDI t3, t3, 1
    LBU t3, 0(t3)
    LI a0, 0x90
    BLT t3, a0, uv_bad
uv_nf0:
    ADD t3, t0, t2
    LBU t3, 0(t3)
    LI a0, 0xF4
    BNE t3, a0, uv_adv
    ADD t3, t0, t2
    ADDI t3, t3, 1
    LBU t3, 0(t3)
    LI a0, 0x90
    BGE t3, a0, uv_bad
uv_adv:
    ADD t2, t2, t4
    ADDI t2, t2, 1
    J uv_loop
uv_next1:
    ADDI t2, t2, 1
    J uv_loop
uv_ok:
    LI a0, 1
    RET
uv_bad:
    LI a0, 0
    RET

; ==============================================================================
; Data Segment
; ==============================================================================
str_top_title:
    .string "[ DimonOS-64 ]"
str_top_apps:
    .string "F1 Menu   1-7 Launch   Alt+Tab Switch"
str_rtc_unavailable:
    .string "RTC N/A"
str_system_halted:
    .string "System halted. You may close QEMU."
str_start_btn:
    .string "[ START ]"
str_wname_desk:
    .string "[ Desktop ]"
str_wname_calc:
    .string "[ Calculator ]"
str_wname_note:
    .string "[ Notepad ]"
str_wname_file:
    .string "[ File Explorer ]"
str_wname_paint:
    .string "[ Paint Studio ]"
str_wname_info:
    .string "[ System Info ]"
str_wname_snake:
    .string "[ Snake 64 ]"
str_wname_term:
    .string "[ Terminal CLI ]"
str_task_hint:
    .string "F1: Menu | ESC: Close Win"
str_close_x:
    .string "X"
str_minimize:
    .string "-"
str_short_calc:
    .string "Calc"
str_short_note:
    .string "Notepad"
str_short_files:
    .string "Files"
str_short_paint:
    .string "Paint"
str_short_info:
    .string "Info"
str_short_snake:
    .string "Snake"
str_short_term:
    .string "Terminal"

str_sm_title:
    .string "DimonOS Applications"
str_sm_i1:
    .string "1. Calculator"
str_sm_i2:
    .string "2. Notepad"
str_sm_i3:
    .string "3. File Explorer"
str_sm_i4:
    .string "4. Modern Paint"
str_sm_i5:
    .string "5. System Info"
str_sm_i6:
    .string "6. Snake Game"
str_sm_i7:
    .string "7. Terminal CLI"
str_sm_exit:
    .string "X. Exit OS"

str_ico_calc:
    .string "Calculator"
str_ico_note:
    .string "Notepad"
str_ico_disk:
    .string "Files"
str_ico_paint:
    .string "Paint"
str_ico_info:
    .string "System Info"
str_ico_snake:
    .string "Snake"
str_ico_term:
    .string "Terminal"

str_calc_title:
    .string "Calculator - 64-bit TrueColor"
str_btn_0:
    .string "0"
str_btn_1:
    .string "1"
str_btn_2:
    .string "2"
str_btn_3:
    .string "3"
str_btn_4:
    .string "4"
str_btn_5:
    .string "5"
str_btn_6:
    .string "6"
str_btn_7:
    .string "7"
str_btn_8:
    .string "8"
str_btn_9:
    .string "9"
str_btn_add:
    .string "+"
str_btn_sub:
    .string "-"
str_btn_mul:
    .string "*"
str_btn_div:
    .string "/"
str_btn_eq:
    .string "="
str_btn_c:
    .string "C"

str_note_title:
    .string "Notepad - FAT16 Text Editor"
str_btn_new:
    .string "[ New ]"
str_btn_open:
    .string "[ Open ]"
str_btn_save:
    .string "[ Save ]"
str_btn_saveas:
    .string "[Save As]"
str_boot_note:
    .string "/NOTES.TXT"
str_status_saved:
    .string "FAT16: Saved to Disk!"
str_status_opened:
    .string "FAT16: Loaded from Disk!"
str_status_new:
    .string "FAT16: New document"
str_status_save_error:
    .string "Save failed (disk/name/I/O)"
str_status_open_error:
    .string "Open failed: file not found"
str_status_too_large:
    .string "Open refused: file exceeds 4000 bytes"
str_status_bad_enc:
    .string "Open refused: not valid UTF-8"
str_status_missing:
    .string "Open failed: not found"
str_status_bad_name:
    .string "Use 8.3 names (e.g. NOTES.TXT)"
str_status_no_disk:
    .string "Open failed: no disk"
str_status_readonly:
    .string "Save failed: read-only disk"
str_status_diskfull:
    .string "Save failed: disk full"
str_note_title_base:
    .string "Notepad - "
str_note_dirty_mark:
    .string " *"
str_sfs_f0:
    .string "notes.txt"
str_sfs_f1:
    .string "readme.txt"
str_sfs_f2:
    .string "todo.txt"
str_init_notes:
    .string "Welcome to DimonOS-64 Notepad with FAT16!\nPress Save (F9) to write changes to disk.\nPress Open (F10) to reload text from disk.\n"

str_file_title:
    .string "File Explorer - FAT16 Root Directory"
str_fm_hdr:
    .string "FILENAME         CLUSTER  SIZE     ACTION"
str_fm_open_btn:
    .string "Open in Notepad"
str_fm_empty:
    .string "(empty folder)"
str_fm_nomount:
    .string "(No FAT16 volume detected)"
str_fm_up:
    .string "[ Up ]"
str_fm_top:
    .string "(top)"
str_fm_hdr_name:
    .string "NAME"
str_fm_hdr_size:
    .string "SIZE"
str_fm_run_btn:
    .string "[Run]"
str_fm_edit_btn:
    .string "[Edit]"
str_fm_open_btn2:
    .string "[Open]"
str_fm_info_btn:
    .string "[Info]"
str_fm_dir_tag:
    .string "<DIR>"
str_fm_up_tag:
    .string "<UP>"
str_fm_hint:
    .string "Enter open  Bksp up  F5 refresh"
str_fm_unsupported:
    .string "Unsupported file type"
str_fm_launch_err:
    .string "Cannot launch (bad DEXE/slot)"
str_fm_items:
    .string "items"
str_root_path2:
    .string "/"
str_dotdot:
    .string ".."
str_ext_app:
    .string "APP"
str_ext_txt:
    .string "TXT"
str_dlg_open_title:
    .string "Open file"
str_dlg_save_title:
    .string "Save As"
str_dlg_confirm_title:
    .string "Unsaved changes"
str_dlg_folder:
    .string "Folder:"
str_dlg_file:
    .string "File:"
str_dlg_open_btn:
    .string "[Open]"
str_dlg_save_btn:
    .string "[Save]"
str_dlg_cancel_btn:
    .string "[Cancel]"
str_dlg_keep_btn:
    .string "[Keep]"
str_dlg_yes_btn:
    .string "[Save]"
str_dlg_no_btn:
    .string "[Discard]"
str_dlg_txt_tag:
    .string " (*.TXT)"
str_dlg_txt_filter:
    .string "Text files (*.TXT)"
str_dlg_all_filter:
    .string "All files"
str_dlg_83_rule:
    .string "Use 8.3 names (e.g. NOTES.TXT)"
str_dlg_not_found:
    .string "Folder not found"
str_dlg_no_file:
    .string "Select or type a file"
str_dlg_no_disk:
    .string "No disk"
str_dlg_ro:
    .string "Read-only disk"
str_dlg_full:
    .string "Disk or directory full"
str_dlg_bad_name:
    .string "Invalid 8.3 name"
str_dlg_missing:
    .string "File not found"
str_dlg_too_large:
    .string "File exceeds 4000 bytes"
str_dlg_bad_enc:
    .string "Not valid UTF-8 text"
str_dlg_io:
    .string "Disk I/O error"
str_dlg_exists:
    .string "Replace existing file?"
str_confirm_msg:
    .string "Save changes first?"
str_note_untitled:
    .string "untitled"
str_dlg_overwrite_title:
    .string "Confirm Overwrite"
str_dlg_btn_yes:
    .string "[Yes]"
str_dlg_btn_no:
    .string "[No]"
str_dlg_sep_err:
    .string "No path separators in filename"
str_dlg_base_len:
    .string "Name exceeds 8 characters"
str_dlg_ext_len:
    .string "Extension exceeds 3 characters"
str_dlg_multi_dot:
    .string "Multiple dots not allowed"
str_dlg_empty_base:
    .string "Missing base name"
str_dlg_invalid_char:
    .string "Invalid character in filename"

str_paint_title:
    .string "Paint - TrueColor Studio 800x600"
str_b1:
    .string "1px"
str_b3:
    .string "3px"
str_b5:
    .string "5px"
str_eraser:
    .string "Eraser"
str_clear:
    .string "Clear"

str_info_title:
    .string "System Information - DimonVirtualCPU-64"
str_info_cpu:
    .string "CPU: DimonVirtualCPU-64 (64-bit RISC-V)"
str_info_ram:
    .string "Memory: 64 MB Total RAM"
str_info_gpu:
    .string "Display: 800x600 TrueColor LFB @ 0x02000000"
str_info_fs:
    .string "Filesystem: Standard FAT16 (800x600 LFB)"
str_info_ticks:
    .string "Interrupt Ticks: "
str_info_worker:
    .string "Worker Counter:  "
str_info_bar:
    .string "[####################               ] 35% RAM Allocated"

str_snake_wtitle:
    .string "Snake 64 - TrueColor Edition"
str_score_lbl:
    .string "Score: "
str_controls:
    .string "Arrows/WASD: Move | R: Restart"
str_gameover:
    .string "GAME OVER!"
str_restart:
    .string "Press [R] to Play Again"
str_quit:
    .string "Press [ESC] to Exit"

str_term_title:
    .string "Terminal CLI - DimonOS Shell"
str_term_banner1:
    .string "DimonOS-64 Terminal (800x600 LFB). Type 'help'."
str_term_banner2:
    .string "Ready for commands."
str_term_prompt:
    .string "dimon64:~$ "
str_term_help_out:
    .string "Commands: help info pwd ls run PATH clear exit"
str_term_info_out:
    .string "DimonOS-64 Modern TrueColor LFB Kernel v3.0"
str_term_unknown:
    .string "Unknown command. Type 'help'."
str_term_run_ok:
    .string "Application launched."
str_term_run_error:
    .string "Launch failed (missing/invalid DEXE or no slot)."
str_term_ls_empty:
    .string "Directory is empty or unavailable."
str_root_path:
    .string "/"
str_cmd_help:
    .string "help"
str_cmd_info:
    .string "info"
str_cmd_clear:
    .string "clear"
str_cmd_exit:
    .string "exit"
str_cmd_pwd:
    .string "pwd"
str_cmd_ls:
    .string "ls"

worker_name:
    .string "worker"

    .align 4
active_window:
    .word 0
win_z_count:
    .word 0
win_z_order:
    .space 8
    .align 4
; bits: 0=open, 1=minimized, 2=initialized
win_flags:
    .word 0, 0, 0, 0, 0, 0, 0
win_x:
    .word 240, 80, 100, 60, 120, 120, 100
win_y:
    .word 70, 45, 60, 35, 70, 50, 60
win_w:
    .word 320, 640, 600, 680, 560, 560, 600
win_h:
    .word 440, 500, 460, 520, 440, 490, 460
win_default_x:
    .word 240, 80, 100, 60, 120, 120, 100
win_default_y:
    .word 70, 45, 60, 35, 70, 50, 60
drag_active:
    .word 0
drag_window:
    .word 0
drag_off_x:
    .word 0
drag_off_y:
    .word 0
hit_window:
    .word 0
drawing_window:
    .word 0
; Compact taskbar: displayed slot -> window id; count of visible buttons.
taskbar_map:
    .space 8
taskbar_count:
    .word 0
start_menu_open:
    .word 0
exit_requested:
    .word 0
need_redraw:
    .word 1

cur_win_x:
    .word 0
cur_win_y:
    .word 0
cur_win_w:
    .word 0
cur_win_h:
    .word 0

; Calculator state
    .align 8
calc_acc:
    .quad 0
calc_cur:
    .quad 0
calc_op:
    .word 0
calc_buf:
    .space 32

; Notepad state
note_len:
    .word 0
note_cursor:
    .word 0
note_dirty:
    .word 0
note_file_idx:
    .word 0
note_cur_col:
    .word 0
note_cur_row:
    .word 0
one_char_buf:
    .space 8
note_status_str:
    .space 64
note_title_buf:
    .space 96

; Paint state
paint_color:
    .word 0xFFFF3B30
paint_radius:
    .word 3
paint_eraser:
    .word 0
paint_inited:
    .word 0

; Snake state
snake_len:
    .word 4
snake_dir:
    .word 0
snake_score:
    .word 0
snake_gameover:
    .word 0
snake_tick:
    .word 0
food_x:
    .word 20
food_y:
    .word 10

; Terminal state
term_len:
    .word 0
term_lines_count:
    .word 0
term_buf:
    .space 64
term_out_buf:
    .space 128
term_dirent:
    .space 28

; Worker & Timer ticks
    .align 8
worker_ticks:
    .quad 0
isr_ticks:
    .quad 0

; Buffers
    .align 4
clock_buf:
    .space 32
num_tmp:
    .space 32
n2d_temp:
    .space 32
fm_name_buf:
    .space 32

; Snake segment coordinates (max 128)
    .align 4
snake_x:
    .space 128
snake_y:
    .space 128

; FAT16 State & Variables
    .align 4
fat16_mounted:
    .word 0
fat16_fat_start:
    .word 4
fat16_sec_per_fat:
    .word 16
fat16_root_start:
    .word 36
fat16_root_secs:
    .word 32
fat16_data_start:
    .word 68
fat16_sec_per_clus:
    .word 1
fat16_file_count:
    .word 0
note_cur_filename:
    .space 32

; FAT16 Buffers
    .align 4
fat16_sec_buf:
    .space 512
fat16_fat_buf:
    .space 512
fat16_dir_buf:
    .space 1024

; Notepad text buffer (4 KB = 4096 bytes)
    .align 4
note_buf:
    .space 4096
    .align 4
note_staging_buf:
    .space 4096

; Explorer model: visible list built from FS_LIST on every draw, so the
; first completed listing already carries validated sizes. No sleeps.
    .align 4
fm_cwd:
    .space 64
fm_count:
    .word 0
fm_sel:
    .word 0
fm_scroll:
    .word 0
fm_last_row:
    .word -1
fm_last_ms:
    .word 0
fm_status:
    .space 96
fm_list_buf:
    .space 896
fm_tmp_path:
    .space 256
fm_size_buf:
    .space 32

; Notepad document: absolute path (empty = untitled, no destination).
note_path:
    .space 256

; Shared file-dialog state (modal; see dlg_* below).
dlg_active:
    .word 0
dlg_owner:
    .word 0
dlg_mode:
    .word 0
dlg_filter:
    .word 0
dlg_focus:
    .word 0
dlg_path:
    .space 256
dlg_path_len:
    .word 0
dlg_path_caret:
    .word 0
dlg_path_scroll:
    .word 0
dlg_name:
    .space 64
dlg_name_len:
    .word 0
dlg_name_caret:
    .word 0
dlg_dir:
    .space 256
dlg_armed:
    .word 0
dlg_tmp:
    .space 256
dlg_count:
    .word 0
dlg_sel:
    .word 0
dlg_scroll:
    .word 0
dlg_last_row:
    .word -1
dlg_last_ms:
    .word 0
dlg_err:
    .space 96
dlg_list_buf:
    .space 896
dlg_tmp_path:
    .space 256
; Confirm dialog: pending destructive action + destination path.
dlg_pending:
    .word 0
dlg_pending_path:
    .space 256
dlg_name_scroll_unused:
    .word 0
