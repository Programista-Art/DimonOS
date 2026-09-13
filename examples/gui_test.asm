; gui_test.asm - headless TrueColor GUI smoke test for DimonVirtualCPU-64
; Fills the screen blue, draws a test rectangle and banner, flushes and halts.
.org 0x0
main:
    LI a7, 10
    ECALL                    ; SYS_GUI_INIT

    ; Draw full screen background (800x600, navy blue: 0xFF003366)
    LI a0, 0
    LI a1, 0
    LI a2, 800
    LI a3, 600
    LI a4, 0xFF003366
    LI a7, 14
    ECALL

    ; Draw inner card (700x500 at 50,50, slate: 0xFF1E293B)
    LI a0, 50
    LI a1, 50
    LI a2, 700
    LI a3, 500
    LI a4, 0xFF1E293B
    LI a7, 14
    ECALL

    ; Draw text banner (x=100, y=100, white on transparent)
    LI a0, 100
    LI a1, 100
    LA a2, title
    LI a3, 0xFFFFFFFF
    LI a4, 0
    LI a7, 15
    ECALL

    ; Flush
    LI a7, 12
    ECALL

    EBREAK

title:
    .string "DimonOS GUI Test OK!"
