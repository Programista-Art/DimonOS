    INT 10
    MOV R0, 0
    MOV R1, 0x1950
    MOV R2, 0x1F20
    INT 14
    MOV R0, 0x0205
    MOV R1, title
    MOV R2, 0x1E
    INT 15
    INT 12
    HLT
title:
    DB "DimonOS GUI Test OK!", 0
