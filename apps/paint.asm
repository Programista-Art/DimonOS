; ========================================================
; apps/paint.asm — Application 4: Paint / Canvas
; ========================================================
; Canvas dimensions: 48 columns x 10 rows

paint_cur_char:
    DB 0xDB             ; default full block █
paint_cur_color:
    DB 0x1E             ; default yellow on blue
paint_pen_x:
    DW 0
paint_pen_y:
    DW 0

; Canvas: 48 * 10 = 480 bytes characters + 480 bytes attributes
paint_canvas_chars:
    DS 480
paint_canvas_attrs:
    DS 480

paint_init:
    PUSH R0
    PUSH R1
    PUSH R2

    MOV [paint_cur_char], 0xDB
    MOV [paint_cur_color], 0x1E
    MOV [paint_pen_x], 0
    MOV [paint_pen_y], 0

    POP R2
    POP R1
    POP R0
    RET

paint_draw:
    PUSH R0
    PUSH R1
    PUSH R2
    PUSH R3

    ; Draw window frame: X=8, Y=3, W=64, H=18
    MOV R0, 0x0308      ; X=8, Y=3
    MOV R1, 0x1240      ; W=64 (0x40), H=18 (0x12)
    MOV R2, str_paint_title
    CALL draw_window_frame

    ; Toolbar at top of window (Row 5)
    MOV R0, 0x050A      ; X=10, Y=5
    MOV R1, str_paint_tools
    MOV R2, 0x1E        ; yellow
    INT 15

    ; Color selector (Row 6)
    MOV R0, 0x060A      ; X=10, Y=6
    MOV R1, str_paint_colors
    MOV R2, 0x1F        ; white
    INT 15

    ; Frame and canvas background: X=10, Y=8, W=48, H=10
    MOV R3, 0           ; cy = 0
p_draw_canv_row:
    CMP R3, 10
    JNC p_draw_canv_done

    MOV R4, 0           ; cx = 0
p_draw_canv_col:
    CMP R4, 48
    JNC p_next_canv_row

    ; Calculate buffer offset = cy * 48 + cx
    MOV R5, R3
    MUL R5, 48
    ADD R5, R4

    ; Retrieve character and color from canvas buffer
    MOV R6, paint_canvas_chars
    ADD R6, R5
    LDB R7, [R6]
    CMP R7, 0
    JNZ p_got_char
    MOV R7, 32          ; default space
p_got_char:

    MOV R6, paint_canvas_attrs
    ADD R6, R5
    LDB R0, [R6]
    CMP R0, 0
    JNZ p_got_attr
    MOV R0, 0x07        ; gray on black
p_got_attr:

    ; Draw cell: X=10+cx, Y=8+cy
    MOV R1, 8
    ADD R1, R3
    SHL R1, 8
    MOV R2, 10
    ADD R2, R4
    OR  R1, R2          ; R1 = X | (Y << 8)

    MOV R2, 0x0101      ; W=1, H=1
    MOV R6, R0          ; color
    SHL R6, 8
    OR  R6, R7          ; char | (color << 8)

    PUSH R0
    PUSH R1
    PUSH R2
    MOV R0, R1          ; pos
    MOV R1, 0x0101      ; dim
    MOV R2, R6          ; val
    INT 14
    POP R2
    POP R1
    POP R0

    INC R4
    JMP p_draw_canv_col

p_next_canv_row:
    INC R3
    JMP p_draw_canv_row

p_draw_canv_done:
    ; Draw brush cursor
    MOV R0, [paint_pen_y]
    ADD R0, 8
    SHL R0, 8
    MOV R1, [paint_pen_x]
    ADD R1, 10
    OR  R0, R1          ; X | (Y << 8)
    MOV R1, str_paint_cursor
    MOV R2, 0x4F        ; white on red
    INT 15

    ; Bottom help bar (Row 19, X=10)
    MOV R0, 0x130A
    MOV R1, str_paint_help
    MOV R2, 0x17
    INT 15

    POP R3
    POP R2
    POP R1
    POP R0
    RET

; Plot point at current pen position
paint_plot:
    PUSH R0
    PUSH R1
    PUSH R2
    PUSH R3

    MOV R0, [paint_pen_x]
    CMP R0, 48
    JNC pp_done
    MOV R1, [paint_pen_y]
    CMP R1, 10
    JNC pp_done

    MUL R1, 48
    ADD R1, R0

    MOV R2, paint_canvas_chars
    ADD R2, R1
    LDB R3, [paint_cur_char]
    STB [R2], R3

    MOV R2, paint_canvas_attrs
    ADD R2, R1
    LDB R3, [paint_cur_color]
    STB [R2], R3

pp_done:
    POP R3
    POP R2
    POP R1
    POP R0
    RET

paint_on_key:
    ; R1 = keycode
    CMP R1, 256         ; UP
    JZ p_k_up
    CMP R1, 257         ; DOWN
    JZ p_k_down
    CMP R1, 258         ; LEFT
    JZ p_k_left
    CMP R1, 259         ; RIGHT
    JZ p_k_right

    CMP R1, 32          ; Space (draw)
    JZ p_k_space

    CMP R1, 99          ; 'c' - Clear
    JZ p_k_clear
    CMP R1, 67          ; 'C'
    JZ p_k_clear

    ; Brush selection 1..6
    CMP R1, 49          ; '1': █
    JZ p_k_b1
    CMP R1, 50          ; '2': ▓
    JZ p_k_b2
    CMP R1, 51          ; '3': ▒
    JZ p_k_b3
    CMP R1, 52          ; '4': ░
    JZ p_k_b4
    CMP R1, 53          ; '5': *
    JZ p_k_b5
    CMP R1, 54          ; '6': Space (eraser)
    JZ p_k_b6

    ; Color selection (R, G, B, Y, W)
    CMP R1, 114         ; 'r' Red
    JZ p_k_col_r
    CMP R1, 103         ; 'g' Green
    JZ p_k_col_g
    CMP R1, 98          ; 'b' Blue
    JZ p_k_col_b
    CMP R1, 121         ; 'y' Yellow
    JZ p_k_col_y
    CMP R1, 119         ; 'w' White
    JZ p_k_col_w
    RET

p_k_up:
    MOV R0, [paint_pen_y]
    CMP R0, 0
    JZ p_k_ret
    DEC R0
    MOV [paint_pen_y], R0
    CALL os_repaint
    RET

p_k_down:
    MOV R0, [paint_pen_y]
    INC R0
    CMP R0, 10
    JNC p_k_ret
    MOV [paint_pen_y], R0
    CALL os_repaint
    RET

p_k_left:
    MOV R0, [paint_pen_x]
    CMP R0, 0
    JZ p_k_ret
    DEC R0
    MOV [paint_pen_x], R0
    CALL os_repaint
    RET

p_k_right:
    MOV R0, [paint_pen_x]
    INC R0
    CMP R0, 48
    JNC p_k_ret
    MOV [paint_pen_x], R0
    CALL os_repaint
    RET

p_k_space:
    CALL paint_plot
    CALL os_repaint
    RET

p_k_clear:
    MOV R0, 0
    MOV R1, 0
p_clr_loop:
    MOV R2, paint_canvas_chars
    ADD R2, R1
    STB [R2], R0
    MOV R2, paint_canvas_attrs
    ADD R2, R1
    STB [R2], R0
    INC R1
    CMP R1, 480
    JC p_clr_loop
    CALL os_repaint
    RET

p_k_b1: MOV [paint_cur_char], 0xDB ; █
    CALL os_repaint
    RET
p_k_b2: MOV [paint_cur_char], 0xB2 ; ▓
    CALL os_repaint
    RET
p_k_b3: MOV [paint_cur_char], 0xB1 ; ▒
    CALL os_repaint
    RET
p_k_b4: MOV [paint_cur_char], 0xB0 ; ░
    CALL os_repaint
    RET
p_k_b5: MOV [paint_cur_char], 42   ; '*'
    CALL os_repaint
    RET
p_k_b6: MOV [paint_cur_char], 32   ; ' ' (eraser)
    CALL os_repaint
    RET

p_k_col_r: MOV [paint_cur_color], 0x1C ; red
    CALL os_repaint
    RET
p_k_col_g: MOV [paint_cur_color], 0x1A ; green
    CALL os_repaint
    RET
p_k_col_b: MOV [paint_cur_color], 0x19 ; light blue
    CALL os_repaint
    RET
p_k_col_y: MOV [paint_cur_color], 0x1E ; yellow
    CALL os_repaint
    RET
p_k_col_w: MOV [paint_cur_color], 0x1F ; white
    CALL os_repaint
    RET

p_k_ret:
    RET

paint_on_click:
    ; R1 = X, R2 = Y
    ; Check click on canvas (X=10..57, Y=8..17)
    CMP R1, 10
    JC p_clk_tools
    CMP R1, 58
    JNC p_clk_tools
    CMP R2, 8
    JC p_clk_tools
    CMP R2, 18
    JNC p_clk_tools

    SUB R1, 10
    MOV [paint_pen_x], R1
    SUB R2, 8
    MOV [paint_pen_y], R2
    CALL paint_plot
    CALL os_repaint
    RET

p_clk_tools:
    CMP R2, 5
    JZ p_clk_tool_row
    CMP R2, 6
    JZ p_clk_col_row
    RET

p_clk_tool_row:
    CMP R1, 19
    JC p_k_b1
    CMP R1, 26
    JC p_k_b2
    CMP R1, 33
    JC p_k_b3
    CMP R1, 40
    JC p_k_b4
    CMP R1, 47
    JC p_k_b5
    CMP R1, 56
    JC p_k_b6
    RET

p_clk_col_row:
    CMP R1, 21
    JC p_k_col_r
    CMP R1, 29
    JC p_k_col_g
    CMP R1, 37
    JC p_k_col_b
    CMP R1, 45
    JC p_k_col_y
    CMP R1, 53
    JC p_k_col_w
    RET

str_paint_title:
    DB " Paint - DimonOS Graphic Canvas ", 0

str_paint_tools:
    DB "Brush: [1:█]  [2:▓]  [3:▒]  [4:░]  [5:*]  [6:Eraser]", 0

str_paint_colors:
    DB "Color:  [R:Red]  [G:Green] [B:Blue] [Y:Yellow] [W:White]", 0

str_paint_cursor:
    DB "+", 0

str_paint_help:
    DB "Mouse: Draw/Pick | Arrows: Move | Space: Draw | C: Clear", 0
