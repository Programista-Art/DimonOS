; psg_test.asm - Test MMIO Programmable Sound Generator (0x03FFE000)
; Expected output: 440, 1, 180, 50, 1
.org 0x0

    LI t0, 0x03FFE000

    ; Set frequency: 440 Hz
    LI t1, 440
    SW t1, 0(t0)

    ; Set waveform: 1 (Triangle)
    LI t1, 1
    SB t1, 4(t0)

    ; Set volume: 180
    LI t1, 180
    SB t1, 5(t0)

    ; Set duration: 50 ms
    LI t1, 50
    SW t1, 8(t0)

    ; Read back frequency
    LW a0, 0(t0)
    LI a7, 3
    ECALL
    LI a7, 5
    ECALL

    ; Read back waveform
    LBU a0, 4(t0)
    LI a7, 3
    ECALL
    LI a7, 5
    ECALL

    ; Read back volume
    LBU a0, 5(t0)
    LI a7, 3
    ECALL
    LI a7, 5
    ECALL

    ; Read back duration
    LW a0, 8(t0)
    LI a7, 3
    ECALL
    LI a7, 5
    ECALL

    ; Trigger playback
    LI t1, 1
    SB t1, 12(t0)

    ; Read status (should be 1 while playing/busy)
    LBU a0, 12(t0)
    LI a7, 3
    ECALL
    LI a7, 5
    ECALL

    ; Exit
    LI a0, 0
    LI a7, 18
    ECALL
