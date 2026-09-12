; iso_boot.asm — sector 0 bootloader for DIMON-ISO image
; Loaded at address 0x0000. Must be <= 510 B (magic 55AA appended by mkiso).
; Demonstrates INT 7 (INFO) and INT 6 (READ): loads directory from sector 1.

    MOV R0, msg_boot
    INT 2

    INT 7
    JC no_disk
    MOV R6, R0              ; sector count (lo16)

    MOV R0, msg_sec
    INT 2
    MOV R0, R6
    INT 3
    INT 5

    ; directory (sector 1) -> 0x1000
    MOV R0, 1
    MOV R1, 0x1000
    MOV R2, 1
    INT 6
    JC disk_err

    MOV R0, msg_files
    INT 2
    MOV R0, [0x1004]        ; nfiles (directory: "DM16" + u16)
    INT 3
    INT 5

    MOV R0, msg_ok
    INT 2
    HLT

no_disk:
    MOV R0, msg_nodisk
    INT 2
    HLT

disk_err:
    MOV R0, msg_err
    INT 2
    HLT

msg_boot:
    DB "Dimon-16 boot from ISO...", 10, 0
msg_sec:
    DB "Sectors: ", 0
msg_files:
    DB "Files: ", 0
msg_ok:
    DB "Boot OK.", 10, 0
msg_nodisk:
    DB "No disk!", 10, 0
msg_err:
    DB "Disk read error!", 10, 0
