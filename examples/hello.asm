; hello.asm — first program on Dimon-16
    MOV R0, msg
    INT 2
    HLT
msg:
    DB "Hello, Dimon-16!", 10, 0
