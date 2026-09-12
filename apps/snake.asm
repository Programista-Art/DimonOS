; ========================================================
; apps/snake.asm — Application 6: Snake Game
; ========================================================
; Board dimensions: X=20, Y=6, W=40, H=11, max len=60

snake_len:
    DW 4
snake_dir:
    DB 0                ; 0=Right, 1=Down, 2=Left, 3=Up
snake_score:
    DW 0
snake_gameover:
    DB 0
snake_tick_cnt:
    DW 0

food_x:
    DW 15
food_y:
    DW 5

snake_x:
    DS 64
snake_y:
    DS 64

snake_score_str:
    DS 16

snake_init:
    PUSH R0
    PUSH R1
    PUSH R2

    MOV [snake_len], 4
    MOV [snake_dir], 0          ; moving right
    MOV [snake_score], 0
    MOV [snake_gameover], 0
    MOV [snake_tick_cnt], 0

    ; Initial snake segments
    MOV R1, snake_x
    MOV R0, 10
    STB [R1], R0
    INC R1
    DEC R0
    STB [R1], R0
    INC R1
    DEC R0
    STB [R1], R0
    INC R1
    DEC R0
    STB [R1], R0

    MOV R1, snake_y
    MOV R0, 5
    STB [R1], R0
    INC R1
    STB [R1], R0
    INC R1
    STB [R1], R0
    INC R1
    STB [R1], R0

    ; Initial food position
    MOV [food_x], 25
    MOV [food_y], 5

    POP R2
    POP R1
    POP R0
    RET

snake_draw:
    PUSH R0
    PUSH R1
    PUSH R2
    PUSH R3

    ; Draw window frame: X=18, Y=3, W=44, H=18
    MOV R0, 0x0312      ; X=18, Y=3
    MOV R1, 0x122C      ; W=44 (0x2C), H=18 (0x12)
    MOV R2, str_snake_title
    CALL draw_window_frame

    ; Score bar at top of window (Row 5, X=20)
    MOV R0, 0x0514
    MOV R1, str_snake_score_lbl
    MOV R2, 0x1E        ; yellow
    INT 15

    ; Score count
    MOV R0, [snake_score]
    MOV R1, snake_score_str
    CALL num_to_str

    MOV R0, 0x051F      ; X=31, Y=5
    MOV R1, snake_score_str
    MOV R2, 0x1A        ; green
    INT 15

    ; Game board: X=20, Y=6, W=40, H=11 (black background 0x07)
    MOV R0, 0x0614
    MOV R1, 0x0B28      ; W=40, H=11
    MOV R2, 0x0720      ; ' ', black background
    INT 14

    ; Draw food (red heart ♥)
    MOV R0, [food_y]
    ADD R0, 6
    SHL R0, 8
    MOV R1, [food_x]
    ADD R1, 20
    OR  R0, R1          ; X | (Y << 8)
    MOV R1, 0x0101
    MOV R2, 0x0C03      ; heart ♥ (0x03), bright red on black
    INT 14

    ; Draw snake segments
    MOV R3, 0           ; i = 0
snk_draw_loop:
    MOV R0, [snake_len]
    CMP R3, R0
    JNC snk_draw_status

    ; Segment position (snake_x[i], snake_y[i])
    MOV R4, snake_x
    ADD R4, R3
    LDB R1, [R4]        ; X
    ADD R1, 20

    MOV R4, snake_y
    ADD R4, R3
    LDB R2, [R4]        ; Y
    ADD R2, 6

    MOV R0, R2
    SHL R0, 8
    OR  R0, R1          ; X | (Y << 8)
    MOV R1, 0x0101

    CMP R3, 0
    JZ snk_head_col
    MOV R2, 0x0A09      ; body: green circle ○ (0x09)
    INT 14
    JMP snk_next_seg

snk_head_col:
    MOV R2, 0x0EDB      ; head: yellow block █ (0xDB)
    INT 14

snk_next_seg:
    INC R3
    JMP snk_draw_loop

snk_draw_status:
    ; Check if Game Over
    LDB R0, [snake_gameover]
    CMP R0, 1
    JNZ snk_draw_controls

    ; Display GAME OVER message in center of board (X=26, Y=11)
    MOV R0, 0x0B1A
    MOV R1, str_snake_go
    MOV R2, 0x4F        ; white on red!
    INT 15

    MOV R0, 0x0D16      ; X=22, Y=13
    MOV R1, str_snake_restart
    MOV R2, 0x1E
    INT 15
    JMP snk_draw_done

snk_draw_controls:
    MOV R0, 0x1214      ; X=20, Y=18
    MOV R1, str_snake_ctrl
    MOV R2, 0x17
    INT 15

snk_draw_done:
    POP R3
    POP R2
    POP R1
    POP R0
    RET

; Snake movement driven by timer
snake_on_timer:
    LDB R0, [snake_gameover]
    CMP R0, 1
    JZ snk_t_ret

    ; Rate limit snake movement (move every 3 timer ticks)
    MOV R0, [snake_tick_cnt]
    INC R0
    MOV [snake_tick_cnt], R0
    CMP R0, 3
    JC snk_t_ret

    MOV [snake_tick_cnt], 0

    ; Shift snake body segments backwards: segment[i] = segment[i-1]
    MOV R3, [snake_len]
    DEC R3              ; R3 = len - 1

snk_move_body:
    CMP R3, 0
    JZ snk_move_head

    MOV R4, snake_x
    ADD R4, R3
    DEC R4              ; R4 points to x[i-1]
    LDB R0, [R4]
    INC R4              ; R4 points to x[i]
    STB [R4], R0

    MOV R4, snake_y
    ADD R4, R3
    DEC R4
    LDB R0, [R4]
    INC R4
    STB [R4], R0

    DEC R3
    JMP snk_move_body

snk_move_head:
    ; Move head according to snake_dir
    LDB R0, [snake_x]
    LDB R1, [snake_y]
    LDB R2, [snake_dir]

    CMP R2, 0           ; Right
    JZ snk_dir_r
    CMP R2, 1           ; Down
    JZ snk_dir_d
    CMP R2, 2           ; Left
    JZ snk_dir_l
    CMP R2, 3           ; Up
    JZ snk_dir_u
    JMP snk_head_moved

snk_dir_r:
    INC R0
    JMP snk_head_moved
snk_dir_d:
    INC R1
    JMP snk_head_moved
snk_dir_l:
    DEC R0
    JMP snk_head_moved
snk_dir_u:
    DEC R1

snk_head_moved:
    STB [snake_x], R0
    STB [snake_y], R1

    ; Check wall collision (0..39, 0..10)
    CMP R0, 40
    JNC snk_die
    CMP R1, 11
    JNC snk_die

    ; Check food collision
    MOV R2, [food_x]
    CMP R0, R2
    JNZ snk_no_food
    MOV R2, [food_y]
    CMP R1, R2
    JNZ snk_no_food

    ; Food eaten! Increase score
    MOV R2, [snake_score]
    ADD R2, 10
    MOV [snake_score], R2

    ; Grow snake length
    MOV R2, [snake_len]
    CMP R2, 60
    JNC snk_new_food
    INC R2
    MOV [snake_len], R2

snk_new_food:
    ; Compute pseudo-random new coordinates for food
    MOV R2, [food_x]
    ADD R2, 7
    CMP R2, 40
    JC snk_set_fx
    SUB R2, 33
snk_set_fx:
    MOV [food_x], R2

    MOV R2, [food_y]
    ADD R2, 3
    CMP R2, 11
    JC snk_set_fy
    SUB R2, 9
snk_set_fy:
    MOV [food_y], R2

snk_no_food:
    CALL os_repaint
    RET

snk_die:
    MOV [snake_gameover], 1
    CALL os_repaint
    RET

snk_t_ret:
    RET

snake_on_key:
    ; R1 = keycode
    LDB R0, [snake_gameover]
    CMP R0, 1
    JZ snk_k_dead

    ; Direction control: Arrow keys or WASD
    CMP R1, 256         ; UP
    JZ snk_k_up
    CMP R1, 119         ; 'w'
    JZ snk_k_up

    CMP R1, 257         ; DOWN
    JZ snk_k_down
    CMP R1, 115         ; 's'
    JZ snk_k_down

    CMP R1, 258         ; LEFT
    JZ snk_k_left
    CMP R1, 97          ; 'a'
    JZ snk_k_left

    CMP R1, 259         ; RIGHT
    JZ snk_k_right
    CMP R1, 100         ; 'd'
    JZ snk_k_right
    RET

snk_k_up:
    LDB R0, [snake_dir]
    CMP R0, 1           ; cannot reverse downwards into upwards
    JZ snk_k_ret
    MOV [snake_dir], 3
    RET
snk_k_down:
    LDB R0, [snake_dir]
    CMP R0, 3
    JZ snk_k_ret
    MOV [snake_dir], 1
    RET
snk_k_left:
    LDB R0, [snake_dir]
    CMP R0, 0
    JZ snk_k_ret
    MOV [snake_dir], 2
    RET
snk_k_right:
    LDB R0, [snake_dir]
    CMP R0, 2
    JZ snk_k_ret
    MOV [snake_dir], 0
    RET

snk_k_dead:
    CMP R1, 114         ; 'r' (Restart)
    JZ snk_k_restart
    CMP R1, 82          ; 'R'
    JZ snk_k_restart
    RET

snk_k_restart:
    CALL snake_init
    CALL os_repaint
snk_k_ret:
    RET

snake_on_click:
    RET

str_snake_title:
    DB " Snake Game - DimonOS ", 0

str_snake_score_lbl:
    DB "SCORE: ", 0

str_snake_go:
    DB "        GAME OVER!         ", 0
str_snake_restart:
    DB "Press [R] to play again", 0

str_snake_ctrl:
    DB "Controls: Arrows or W/A/S/D | R: Restart", 0
