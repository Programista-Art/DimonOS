; ==============================================================================
; apps/snake.asm - High-Resolution TrueColor Snake Game for DimonOS-64
; Non-blocking game loop, capped segment arrays (128 max), collision detection,
; immediate screen flush on Game Over, responsive restart ('R') and exit (ESC).
; ==============================================================================

.org 0x0

main:
    LI a7, 10
    ECALL                    ; SYS_GUI_INIT
    CALL snake_init
    CALL snake_draw
    LI a7, 12
    ECALL                    ; SYS_GUI_FLUSH

game_loop:
    LI a7, 11
    ECALL                    ; SYS_GUI_POLL_EVENT (non-blocking)
    BEQ a0, zero, check_tick ; if no event, advance to timing check

    LI t0, 1
    BEQ a0, t0, handle_key   ; EVT_KEY
    LI t0, 4
    BEQ a0, t0, do_step      ; EVT_TIMER

check_tick:
    LI a7, 13
    ECALL                    ; SYS_GET_TICKS (a0 = ms)
    LA t0, last_tick
    LD t1, 0(t0)
    SUB t2, a0, t1
    LI t3, 100               ; 100 ms per step
    BLT t2, t3, loop_sleep
    SD a0, 0(t0)             ; update last_tick
    J do_step

handle_key:
    ; a1 = keycode
    LI t0, 27                ; ESC
    BEQ a1, t0, exit_game

    LA t0, snake_gameover
    LW t1, 0(t0)
    BNE t1, zero, check_restart

    ; Non-blocking direction controls: Arrows or WASD
    LI t0, 256               ; KEY_UP
    BEQ a1, t0, key_up
    LI t0, 119               ; 'w'
    BEQ a1, t0, key_up

    LI t0, 257               ; KEY_DOWN
    BEQ a1, t0, key_down
    LI t0, 115               ; 's'
    BEQ a1, t0, key_down

    LI t0, 258               ; KEY_LEFT
    BEQ a1, t0, key_left
    LI t0, 97                ; 'a'
    BEQ a1, t0, key_left

    LI t0, 100               ; 'd'
    BEQ a1, t0, key_right
    LI t0, 32                ; ' ' (Space step)
    BEQ a1, t0, do_step
    J check_tick

key_up:
    LA t0, snake_dir
    LW t1, 0(t0)
    LI t2, 1                 ; cannot reverse down to up
    BEQ t1, t2, check_tick
    LI t1, 3
    SW t1, 0(t0)
    J check_tick

key_down:
    LA t0, snake_dir
    LW t1, 0(t0)
    LI t2, 3                 ; cannot reverse up to down
    BEQ t1, t2, check_tick
    LI t1, 1
    SW t1, 0(t0)
    J check_tick

key_left:
    LA t0, snake_dir
    LW t1, 0(t0)
    LI t2, 0                 ; cannot reverse right to left
    BEQ t1, t2, check_tick
    LI t1, 2
    SW t1, 0(t0)
    J check_tick

key_right:
    LA t0, snake_dir
    LW t1, 0(t0)
    LI t2, 2                 ; cannot reverse left to right
    BEQ t1, t2, check_tick
    LI t1, 0
    SW t1, 0(t0)
    J check_tick

check_restart:
    LI t0, 114               ; 'r'
    BEQ a1, t0, do_restart
    LI t0, 82                ; 'R'
    BEQ a1, t0, do_restart
    J loop_sleep

do_restart:
    CALL snake_init
    CALL snake_draw
    LI a7, 12
    ECALL                    ; SYS_GUI_FLUSH
    J game_loop

do_step:
    LA t0, snake_gameover
    LW t1, 0(t0)
    BNE t1, zero, loop_sleep
    CALL snake_step
    CALL snake_draw
    LI a7, 12
    ECALL                    ; SYS_GUI_FLUSH

loop_sleep:
    LI a0, 5
    LI a7, 19
    ECALL                    ; SYS_SLEEP 5 ms (yields CPU cleanly)
    J game_loop

exit_game:
    EBREAK

; ------------------------------------------------------------------------------
; snake_init: reset snake variables and segments
; ------------------------------------------------------------------------------
snake_init:
    LA t0, snake_len
    LI t1, 4
    SW t1, 0(t0)
    LA t0, snake_dir
    SW zero, 0(t0)           ; 0 = right
    LA t0, snake_score
    SW zero, 0(t0)
    LA t0, snake_gameover
    SW zero, 0(t0)

    ; Initial segments
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

    ; Initial food
    LA t0, food_x
    LI t1, 20
    SW t1, 0(t0)
    LA t0, food_y
    LI t1, 10
    SW t1, 0(t0)
    RET

; ------------------------------------------------------------------------------
; snake_step: advance snake by one cell
; ------------------------------------------------------------------------------
snake_step:
    ADDI sp, sp, -32
    SD ra, 24(sp)
    SD s0, 16(sp)
    SD s1, 8(sp)

    LA t0, snake_gameover
    LW t1, 0(t0)
    BNE t1, zero, snk_step_ret

    LA t0, snake_dir
    LW t0, 0(t0)
    LA t1, snake_x
    LBU t1, 0(t1)            ; head X
    LA t2, snake_y
    LBU t2, 0(t2)            ; head Y

    LI t3, 0
    BEQ t0, t3, snk_dir_r
    LI t3, 1
    BEQ t0, t3, snk_dir_d
    LI t3, 2
    BEQ t0, t3, snk_dir_l
    ADDI t2, t2, -1          ; UP
    J snk_check_wall
snk_dir_r:
    ADDI t1, t1, 1
    J snk_check_wall
snk_dir_d:
    ADDI t2, t2, 1
    J snk_check_wall
snk_dir_l:
    ADDI t1, t1, -1

snk_check_wall:
    ; Board dimensions: 30 wide x 20 high
    BLT t1, zero, snk_collide
    LI t3, 30
    BGE t1, t3, snk_collide
    BLT t2, zero, snk_collide
    LI t3, 20
    BGE t2, t3, snk_collide

    ; Check self collision
    LA t3, snake_len
    LW s0, 0(t3)
    LI t4, 128
    BGE t4, s0, snk_len_ok
    LI s0, 128
    SW s0, 0(t3)
snk_len_ok:
    LI t4, 0
snk_self_chk:
    BGE t4, s0, snk_shift_body
    LA t5, snake_x
    ADD t5, t5, t4
    LBU t5, 0(t5)
    BNE t5, t1, snk_self_next
    LA t5, snake_y
    ADD t5, t5, t4
    LBU t5, 0(t5)
    BEQ t5, t2, snk_collide
snk_self_next:
    ADDI t4, t4, 1
    J snk_self_chk

snk_shift_body:
    ; Shift body backwards: segment[i] = segment[i-1]
    ADDI t4, s0, -1          ; i = len - 1
snk_shift_loop:
    BEQ t4, zero, snk_shift_done
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
    J snk_shift_loop

snk_shift_done:
    ; Store new head
    LA t3, snake_x
    SB t1, 0(t3)
    LA t3, snake_y
    SB t2, 0(t3)

    ; Check food collision
    LA t3, food_x
    LW t4, 0(t3)
    BNE t1, t4, snk_step_ret
    LA t3, food_y
    LW t4, 0(t3)
    BNE t2, t4, snk_step_ret

    ; Food eaten! Increase score
    LA t3, snake_score
    LW t4, 0(t3)
    ADDI t4, t4, 10
    SW t4, 0(t3)

    ; Grow snake (cap at 120)
    LA t3, snake_len
    LW t4, 0(t3)
    LI t5, 120
    BGE t4, t5, snk_place_food
    ADDI t4, t4, 1
    SW t4, 0(t3)

snk_place_food:
    ; Pseudo-random new food coords
    LI a7, 13
    ECALL                    ; SYS_GET_TICKS
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
    J snk_step_ret

snk_collide:
    LA t0, snake_gameover
    LI t1, 1
    SW t1, 0(t0)

snk_step_ret:
    LD s1, 8(sp)
    LD s0, 16(sp)
    LD ra, 24(sp)
    ADDI sp, sp, 32
    RET

; ------------------------------------------------------------------------------
; snake_draw: Render TrueColor board, snake, food, score, and Game Over dialog
; ------------------------------------------------------------------------------
snake_draw:
    ADDI sp, sp, -32
    SD ra, 24(sp)
    SD s0, 16(sp)
    SD s1, 8(sp)

    ; Draw window background (X=100, Y=40, W=600, H=500)
    LI a0, 100
    LI a1, 40
    LI a2, 600
    LI a3, 500
    LI a4, 0xFF1E222D        ; Modern dark slate window background
    LI a7, 14
    ECALL                    ; fillrect

    ; Title bar (X=100, Y=40, W=600, H=32)
    LI a0, 100
    LI a1, 40
    LI a2, 600
    LI a3, 32
    LI a4, 0xFF2563EB        ; Royal blue header
    LI a7, 14
    ECALL

    ; Title text
    LI a0, 116
    LI a1, 48
    LA a2, str_title
    LI a3, 0xFFFFFFFF
    LI a4, 0x00000000
    LI a7, 15
    ECALL                    ; drawstring

    ; Close [X] button (X=670, Y=46, W=20, H=20)
    LI a0, 670
    LI a1, 46
    LI a2, 20
    LI a3, 20
    LI a4, 0xFFEF4444        ; Red
    LI a7, 14
    ECALL
    LI a0, 676
    LI a1, 48
    LA a2, str_close_x
    LI a3, 0xFFFFFFFF
    LI a4, 0x00000000
    LI a7, 15
    ECALL

    ; Score text (X=120, Y=84)
    LI a0, 120
    LI a1, 84
    LA a2, str_score_lbl
    LI a3, 0xFF38BDF8        ; Cyan
    LI a4, 0x00000000
    LI a7, 15
    ECALL

    LA t0, snake_score
    LW a0, 0(t0)
    LA a1, num_buf
    CALL num_to_dec
    LI a0, 180
    LI a1, 84
    LA a2, num_buf
    LI a3, 0xFF34C759        ; Green
    LI a4, 0x00000000
    LI a7, 15
    ECALL

    ; Controls hint (X=360, Y=84)
    LI a0, 360
    LI a1, 84
    LA a2, str_controls
    LI a3, 0xFF94A3B8        ; Slate gray
    LI a4, 0x00000000
    LI a7, 15
    ECALL

    ; Game Board: X=160, Y=110, W=480, H=400 (each cell 16x20)
    ; Board border (X=158, Y=108, W=484, H=404)
    LI a0, 158
    LI a1, 108
    LI a2, 484
    LI a3, 404
    LI a4, 0xFF334155        ; Dark border
    LI a7, 14
    ECALL

    ; Board background
    LI a0, 160
    LI a1, 110
    LI a2, 480
    LI a3, 400
    LI a4, 0xFF0F172A        ; Deep dark navy
    LI a7, 14
    ECALL

    ; Draw Food (Red circle/block: 16x20 cell)
    LA t0, food_x
    LW t1, 0(t0)
    SLLI t1, t1, 4           ; x * 16
    ADDI t1, t1, 162         ; + offset + margin
    LA t0, food_y
    LW t2, 0(t0)
    LI t3, 20
    MUL t2, t2, t3
    ADDI t2, t2, 112         ; y * 20 + offset
    MV a0, t1
    MV a1, t2
    LI a2, 12
    LI a3, 16
    LI a4, 0xFFFF3B30        ; Bright red food
    LI a7, 14
    ECALL

    ; Draw Snake segments
    LA t0, snake_len
    LW s0, 0(t0)
    LI s1, 0
draw_seg_loop:
    BGE s1, s0, draw_status_chk
    LA t0, snake_x
    ADD t0, t0, s1
    LBU t1, 0(t0)
    SLLI t1, t1, 4           ; x * 16
    ADDI t1, t1, 161
    LA t0, snake_y
    ADD t0, t0, s1
    LBU t2, 0(t0)
    LI t3, 20
    MUL t2, t2, t3
    ADDI t2, t2, 111

    MV a0, t1
    MV a1, t2
    LI a2, 14
    LI a3, 18
    BEQ s1, zero, draw_head_col
    LI a4, 0xFF34C759        ; Emerald green body
    J do_draw_seg
draw_head_col:
    LI a4, 0xFFFFCC00        ; Vibrant amber/yellow head
do_draw_seg:
    LI a7, 14
    ECALL                    ; fillrect segment
    ADDI s1, s1, 1
    J draw_seg_loop

draw_status_chk:
    LA t0, snake_gameover
    LW t1, 0(t0)
    BEQ t1, zero, draw_done

    ; GAME OVER Dialog Box (center of board: X=240, Y=240, W=320, H=120)
    LI a0, 240
    LI a1, 240
    LI a2, 320
    LI a3, 120
    LI a4, 0xEE1E222D        ; Dialog box
    LI a7, 14
    ECALL

    ; Red top accent line
    LI a0, 240
    LI a1, 240
    LI a2, 320
    LI a3, 4
    LI a4, 0xFFFF3B30
    LI a7, 14
    ECALL

    ; "GAME OVER"
    LI a0, 344
    LI a1, 260
    LA a2, str_gameover
    LI a3, 0xFFFF3B30        ; Red
    LI a4, 0x00000000
    LI a7, 15
    ECALL

    ; "Press [R] to Restart"
    LI a0, 290
    LI a1, 295
    LA a2, str_restart
    LI a3, 0xFFFFFFFF        ; White
    LI a4, 0x00000000
    LI a7, 15
    ECALL

    ; "Press [ESC] to Quit"
    LI a0, 305
    LI a1, 325
    LA a2, str_quit
    LI a3, 0xFF94A3B8        ; Gray
    LI a4, 0x00000000
    LI a7, 15
    ECALL

draw_done:
    LD s1, 8(sp)
    LD s0, 16(sp)
    LD ra, 24(sp)
    ADDI sp, sp, 32
    RET

; Helper: format number to decimal ASCII string in buffer
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
    LA t0, n2d_temp
    LI t1, 0
n2d_loop:
    BEQ a0, zero, n2d_rev
    LI t2, 10
    REM t3, a0, t2
    DIV a0, a0, t2
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

; ------------------------------------------------------------------------------
; Data segment
; ------------------------------------------------------------------------------
str_title:
    .string "Snake 64 - TrueColor Edition"
str_close_x:
    .string "X"
str_score_lbl:
    .string "Score: "
str_controls:
    .string "Arrows/WASD: Move | R: Restart | ESC: Quit"
str_gameover:
    .string "GAME OVER!"
str_restart:
    .string "Press [R] to Play Again"
str_quit:
    .string "Press [ESC] to Exit"

.align 8
last_tick:
    .quad 0
snake_len:
    .word 4
snake_dir:
    .word 0
snake_score:
    .word 0
snake_gameover:
    .word 0
food_x:
    .word 20
food_y:
    .word 10

.align 4
num_buf:
    .space 32
n2d_temp:
    .space 32

; Max 128 elements for segment coordinates
.align 4
snake_x:
    .space 128
snake_y:
    .space 128
