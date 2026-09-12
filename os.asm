; ==============================================================================
; DimonOS v2.0 GUI — Advanced 16-bit Operating System for Dimon-16
; Desktop, Window Manager, Start Menu, Taskbar, Mouse and Keyboard Support
; Suite of 7 Applications: Calculator, Notepad, File Explorer, Paint, SysInfo, Snake, Terminal
; ==============================================================================

.org 0x0000
os_boot:
    ; Initialize desktop and GUI subsystem
    CALL desktop_init

    ; Initial full screen redraw and explicit flush before entering event loop
    CALL os_repaint

    ; Main operating system event loop
os_main_loop:
    CALL events_poll
    JMP os_main_loop

; ==============================================================================
; Full screen repaint (Desktop + Menu + Active Window + VRAM Flush)
; ==============================================================================
os_repaint:
    PUSH R0
    PUSH R1
    PUSH R2

    ; 1. Draw desktop background, icons, taskbar, and menu
    CALL desktop_draw

    ; 2. If any application window is active, draw it on top
    MOV R0, [active_window]
    CMP R0, 0
    JZ os_rep_flush

    CMP R0, 1
    JZ os_rep_calc
    CMP R0, 2
    JZ os_rep_notepad
    CMP R0, 3
    JZ os_rep_fileman
    CMP R0, 4
    JZ os_rep_paint
    CMP R0, 5
    JZ os_rep_sysinfo
    CMP R0, 6
    JZ os_rep_snake
    CMP R0, 7
    JZ os_rep_terminal
    JMP os_rep_flush

os_rep_calc:
    CALL calc_draw
    JMP os_rep_flush
os_rep_notepad:
    CALL notepad_draw
    JMP os_rep_flush
os_rep_fileman:
    CALL fileman_draw
    JMP os_rep_flush
os_rep_paint:
    CALL paint_draw
    JMP os_rep_flush
os_rep_sysinfo:
    CALL sysinfo_draw
    JMP os_rep_flush
os_rep_snake:
    CALL snake_draw
    JMP os_rep_flush
os_rep_terminal:
    CALL terminal_draw

os_rep_flush:
    ; 3. Trigger syscall to flush VRAM buffer to physical display
    INT 12              ; SYS_GUI_FLUSH

    POP R2
    POP R1
    POP R0
    RET

; ==============================================================================
; Include GUI modules and libraries
; ==============================================================================
.include "gui/utils.asm"
.include "gui/desktop.asm"
.include "gui/window.asm"
.include "gui/events.asm"

; ==============================================================================
; Include system applications
; ==============================================================================
.include "apps/calc.asm"
.include "apps/notepad.asm"
.include "apps/fileman.asm"
.include "apps/paint.asm"
.include "apps/sysinfo.asm"
.include "apps/snake.asm"
.include "apps/terminal.asm"
