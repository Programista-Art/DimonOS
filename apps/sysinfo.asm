; ========================================================
; apps/sysinfo.asm — Application 5: System Information & Uptime Monitor
; ========================================================

sys_uptime_sec:
    DW 0
sys_last_tick:
    DW 0
sys_time_str:
    DS 16
sys_tmp_str:
    DS 16

sysinfo_init:
    PUSH R0
    PUSH R1
    INT 13              ; SYS_GET_TICKS -> R0=lo, R1=hi
    MOV [sys_last_tick], R0
    POP R1
    POP R0
    RET

sysinfo_draw:
    PUSH R0
    PUSH R1
    PUSH R2

    ; Draw window frame: X=13, Y=4, W=54, H=16
    MOV R0, 0x040D      ; X=13, Y=4
    MOV R1, 0x1036      ; W=54 (0x36), H=16 (0x10)
    MOV R2, str_sys_title
    CALL draw_window_frame

    ; System information details
    MOV R0, 0x060F      ; X=15, Y=6
    MOV R1, str_sys_l1
    MOV R2, 0x1F        ; white on blue
    INT 15

    MOV R0, 0x070F      ; X=15, Y=7
    MOV R1, str_sys_l2
    INT 15

    MOV R0, 0x080F      ; X=15, Y=8
    MOV R1, str_sys_l3
    INT 15

    MOV R0, 0x090F      ; X=15, Y=9
    MOV R1, str_sys_l4
    INT 15

    MOV R0, 0x0A0F      ; X=15, Y=10
    MOV R1, str_sys_l5
    INT 15

    MOV R0, 0x0B0F      ; X=15, Y=11
    MOV R1, str_sys_l6
    INT 15

    MOV R0, 0x0C0F      ; X=15, Y=12
    MOV R1, str_sys_l7
    INT 15

    ; System Uptime bar (Row 14, X=15)
    MOV R0, 0x0E0F
    MOV R1, str_sys_uptime_lbl
    MOV R2, 0x1E        ; yellow
    INT 15

    ; Format seconds count as string
    MOV R0, [sys_uptime_sec]
    MOV R1, sys_tmp_str
    CALL num_to_str

    MOV R0, 0x0E24      ; X=36, Y=14
    MOV R1, sys_tmp_str
    MOV R2, 0x1A        ; light green
    INT 15

    MOV R0, 0x0E2C      ; X=44, Y=14
    MOV R1, str_sys_sec_lbl
    MOV R2, 0x1E
    INT 15

    ; Close hint
    MOV R0, 0x120F      ; X=15, Y=18
    MOV R1, str_sys_close_hint
    MOV R2, 0x17
    INT 15

    POP R2
    POP R1
    POP R0
    RET

sysinfo_on_timer:
    PUSH R0
    PUSH R1
    PUSH R2

    ; Check time delta since last update
    INT 13              ; SYS_GET_TICKS -> R0=lo, R1=hi
    MOV R2, [sys_last_tick]
    MOV R1, R0
    SUB R1, R2          ; delta in ms
    CMP R1, 1000
    JC si_t_done

    ; At least 1 second (1000 ms) has elapsed
    MOV [sys_last_tick], R0
    MOV R0, [sys_uptime_sec]
    INC R0
    MOV [sys_uptime_sec], R0
    CALL os_repaint

si_t_done:
    POP R2
    POP R1
    POP R0
    RET

sysinfo_on_key:
    RET

sysinfo_on_click:
    RET

str_sys_title:
    DB " DimonOS System Information ", 0

str_sys_l1:
    DB "Operating System  : DimonOS v2.1 GUI (16-bit)", 0
str_sys_l2:
    DB "Processor (CPU)   : Dimon-16 Architecture", 0
str_sys_l3:
    DB "Main Memory       : 64 KB RAM (0x0000 - 0xFFFF)", 0
str_sys_l4:
    DB "Video Memory VRAM : 4000 B @ 0xE000 (80x25 ch)", 0
str_sys_l5:
    DB "Stack Pointer SP  : Initial 0xFFFE (downwards)", 0
str_sys_l6:
    DB "Graphics Engine   : Native VGA / X11 / ANSI TUI", 0
str_sys_l7:
    DB "Disk Subsystem    : DIMON-ISO (512B/sector, INT 6/7/8)", 0

str_sys_uptime_lbl:
    DB "System Uptime     :", 0
str_sys_sec_lbl:
    DB "seconds", 0

str_sys_close_hint:
    DB "Press ESC or click [X] to close", 0
