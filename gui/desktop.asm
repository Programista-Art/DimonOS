; ========================================================
; gui/desktop.asm — Desktop, Menu Bar, Taskbar, Start Menu
; ========================================================

desktop_init:
    PUSH R0
    PUSH R1
    PUSH R2
    ; Initialize GUI subsystem
    INT 10              ; SYS_GUI_INIT -> R0=cols, R1=rows, R2=vram
    MOV [start_menu_open], 0
    POP R2
    POP R1
    POP R0
    RET

desktop_draw:
    PUSH R0
    PUSH R1
    PUSH R2

    ; 1. Top menu bar (row 0, width 80, gray background 0x70)
    MOV R0, 0           ; X=0, Y=0
    MOV R1, 0x0150      ; W=80 (0x50), H=1 (0x01)
    MOV R2, 0x7020      ; char ' ' (0x20), color 0x70 (black on light gray)
    INT 14              ; SYS_GUI_DRAW_RECT

    ; Title on top bar
    MOV R0, 0x0001      ; X=1, Y=0
    MOV R1, str_top_title
    MOV R2, 0x70        ; color 0x70
    INT 15              ; SYS_GUI_DRAW_TEXT

    ; Application shortcuts on top bar
    MOV R0, 0x0012      ; X=18, Y=0
    MOV R1, str_top_apps
    MOV R2, 0x71        ; dark blue on light gray
    INT 15

    ; 2. Desktop background (rows 1..23, width 80, blue background 0x17)
    MOV R0, 0x0100      ; X=0, Y=1
    MOV R1, 0x1750      ; W=80 (0x50), H=23 (0x17)
    MOV R2, 0x17B0      ; char 0xB0 (░), color 0x17 (light gray on dark blue)
    INT 14

    ; 3. Desktop icons (Column 1: X=4)
    ; Icon 1: Calculator (Y=3)
    MOV R0, 0x0304
    MOV R1, str_ico_calc
    MOV R2, 0x1F        ; white on blue
    INT 15

    ; Icon 2: Notepad (Y=6)
    MOV R0, 0x0604
    MOV R1, str_ico_note
    MOV R2, 0x1F
    INT 15

    ; Icon 3: File Explorer (Y=9)
    MOV R0, 0x0904
    MOV R1, str_ico_disk
    MOV R2, 0x1F
    INT 15

    ; Icon 4: Paint (Y=12)
    MOV R0, 0x0C04
    MOV R1, str_ico_paint
    MOV R2, 0x1F
    INT 15

    ; Icon 5: System Info (Y=15)
    MOV R0, 0x0F04
    MOV R1, str_ico_info
    MOV R2, 0x1F
    INT 15

    ; Desktop icons (Column 2: X=24)
    ; Icon 6: Snake (Y=3)
    MOV R0, 0x0318
    MOV R1, str_ico_snake
    MOV R2, 0x1E        ; yellow on blue
    INT 15

    ; Icon 7: Terminal (Y=6)
    MOV R0, 0x0618
    MOV R1, str_ico_term
    MOV R2, 0x1A        ; light green on blue
    INT 15

    ; 4. Bottom taskbar (row 24, width 80, background 0x70)
    MOV R0, 0x1800      ; X=0, Y=24
    MOV R1, 0x0150      ; W=80, H=1
    MOV R2, 0x7020      ; ' ', 0x70
    INT 14

    ; START button
    MOV R0, 0x1801      ; X=1, Y=24
    LDB R1, [start_menu_open]
    CMP R1, 1
    JZ dt_start_active
    MOV R1, str_btn_start
    MOV R2, 0x0F        ; black on white
    INT 15
    JMP dt_task_mid
dt_start_active:
    MOV R1, str_btn_start_act
    MOV R2, 0x2F        ; green/inverted
    INT 15

dt_task_mid:
    ; Active window indicator on taskbar
    MOV R0, 0x180C      ; X=12, Y=24
    LDB R1, [active_window]
    CMP R1, 0
    JZ dt_task_none
    CMP R1, 1
    JZ dt_task_c1
    CMP R1, 2
    JZ dt_task_c2
    CMP R1, 3
    JZ dt_task_c3
    CMP R1, 4
    JZ dt_task_c4
    CMP R1, 5
    JZ dt_task_c5
    CMP R1, 6
    JZ dt_task_c6
    CMP R1, 7
    JZ dt_task_c7
    JMP dt_task_end

dt_task_none:
    MOV R1, str_win_none
    JMP dt_task_print
dt_task_c1:
    MOV R1, str_win_c1
    JMP dt_task_print
dt_task_c2:
    MOV R1, str_win_c2
    JMP dt_task_print
dt_task_c3:
    MOV R1, str_win_c3
    JMP dt_task_print
dt_task_c4:
    MOV R1, str_win_c4
    JMP dt_task_print
dt_task_c5:
    MOV R1, str_win_c5
    JMP dt_task_print
dt_task_c6:
    MOV R1, str_win_c6
    JMP dt_task_print
dt_task_c7:
    MOV R1, str_win_c7
dt_task_print:
    MOV R2, 0x1F        ; white on blue
    INT 15

dt_task_end:
    ; Shortcut help on the right side of taskbar
    MOV R0, 0x1834      ; X=52, Y=24
    MOV R1, str_task_help
    MOV R2, 0x70        ; black on gray
    INT 15

    ; 5. If Start Menu is open, draw on top
    LDB R1, [start_menu_open]
    CMP R1, 1
    JNZ dt_draw_done
    CALL desktop_draw_start_menu

dt_draw_done:
    POP R2
    POP R1
    POP R0
    RET

; Draw Start Menu popup window (X=1, Y=13, W=21, H=11)
desktop_draw_start_menu:
    PUSH R0
    PUSH R1
    PUSH R2

    ; Background
    MOV R0, 0x0D01      ; X=1, Y=13
    MOV R1, 0x0B15      ; W=21 (0x15), H=11 (0x0B)
    MOV R2, 0x7020      ; ' ', 0x70 (light gray)
    INT 14

    ; Top border
    MOV R0, 0x0D01
    MOV R1, str_sm_top
    MOV R2, 0x70
    INT 15

    ; Menu items
    MOV R0, 0x0E01
    MOV R1, str_sm_i1
    INT 15
    MOV R0, 0x0F01
    MOV R1, str_sm_i2
    INT 15
    MOV R0, 0x1001
    MOV R1, str_sm_i3
    INT 15
    MOV R0, 0x1101
    MOV R1, str_sm_i4
    INT 15
    MOV R0, 0x1201
    MOV R1, str_sm_i5
    INT 15
    MOV R0, 0x1301
    MOV R1, str_sm_i6
    INT 15
    MOV R0, 0x1401
    MOV R1, str_sm_i7
    INT 15
    MOV R0, 0x1501
    MOV R1, str_sm_sep
    INT 15
    MOV R0, 0x1601
    MOV R1, str_sm_exit
    INT 15
    MOV R0, 0x1701
    MOV R1, str_sm_bot
    INT 15

    POP R2
    POP R1
    POP R0
    RET

; Desktop text data
str_top_title:
    DB "[ DimonOS v2.0 ]", 0
str_top_apps:
    DB "1 Calc  2 Notes  3 Files  4 Paint  5 Info  6 Snake  7 Term", 0

str_ico_calc:
    DB "[1] Calculator  ", 0
str_ico_note:
    DB "[2] Notepad     ", 0
str_ico_disk:
    DB "[3] File Explorer", 0
str_ico_paint:
    DB "[4] Paint       ", 0
str_ico_info:
    DB "[5] System Info ", 0
str_ico_snake:
    DB "[6] Snake (Game)", 0
str_ico_term:
    DB "[7] Terminal CLI", 0

str_btn_start:
    DB " [ START ] ", 0
str_btn_start_act:
    DB " < START > ", 0

str_win_none:
    DB " [ Desktop ] ", 0
str_win_c1:
    DB " [ Calculator ] ", 0
str_win_c2:
    DB " [ Notepad ] ", 0
str_win_c3:
    DB " [ File Explorer ] ", 0
str_win_c4:
    DB " [ Paint ] ", 0
str_win_c5:
    DB " [ System Info ] ", 0
str_win_c6:
    DB " [ Snake ] ", 0
str_win_c7:
    DB " [ Terminal ] ", 0

str_task_help:
    DB "[F1-F7 Start] [ESC Close]", 0

str_sm_top:
    DB "+-------------------+", 0
str_sm_i1:
    DB "| 1. Calculator     |", 0
str_sm_i2:
    DB "| 2. Notepad        |", 0
str_sm_i3:
    DB "| 3. File Explorer  |", 0
str_sm_i4:
    DB "| 4. Paint          |", 0
str_sm_i5:
    DB "| 5. System Info    |", 0
str_sm_i6:
    DB "| 6. Snake (Game)   |", 0
str_sm_i7:
    DB "| 7. Terminal CLI   |", 0
str_sm_sep:
    DB "|-------------------|", 0
str_sm_exit:
    DB "| X. Exit OS        |", 0
str_sm_bot:
    DB "+-------------------+", 0

start_menu_open:
    DW 0
