; input.asm — read number, add 1
    MOV R0, prompt
    INT 2
    INT 4           ; R0 = number
    JC err
    INC R0
    MOV [val], R0
    MOV R0, result
    INT 2
    MOV R0, [val]
    INT 3
    INT 5
    HLT
err:
    MOV R0, errmsg
    INT 2
    HLT
prompt:
    DB "Enter number: ", 0
result:
    DB "Result +1 = ", 0
errmsg:
    DB "Number error!", 10, 0
val:
    DW 0
