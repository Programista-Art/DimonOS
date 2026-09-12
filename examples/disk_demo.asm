; disk_demo.asm — program + disk: loads first file from ISO and prints it
; Usage: ./dimon-emu examples/disk_demo.bin --iso dimon.iso
; (dimon.iso contains a README text file as the first file, see `make iso`)
; The text file in the data sector is null-terminated (mkiso pads sectors with zeros),
; so it can be directly printed using INT 2.

    MOV R0, msg
    INT 2

    INT 7
    JC no_disk

    ; directory (sector 1) -> 0x1000
    MOV R0, 1
    MOV R1, 0x1000
    MOV R2, 1
    INT 6
    JC err

    ; first entry: name[12] @0x1006, LBA @0x1012, size @0x1014
    MOV R0, [0x1012]
    MOV R6, R0              ; file LBA
    MOV R0, msg_load
    INT 2
    MOV R0, R6
    INT 3
    INT 5

    ; file data -> 0x3000 (1 sector is enough for files < 512 B)
    MOV R0, R6
    MOV R1, 0x3000
    MOV R2, 1
    INT 6
    JC err

    MOV R0, msg_run
    INT 2
    MOV R0, 0x3000
    INT 2
    INT 5
    HLT

no_disk:
    MOV R0, msg_nodisk
    INT 2
    HLT

err:
    MOV R0, msg_err
    INT 2
    HLT

msg:
    DB "disk_demo: loading first file from ISO...", 10, 0
msg_load:
    DB "File LBA: ", 0
msg_run:
    DB "File contents:", 10, 0
msg_nodisk:
    DB "No disk! Use --iso image.iso", 10, 0
msg_err:
    DB "Disk read error!", 10, 0
