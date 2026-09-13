# 01. DimonVirtualCPU-64 ISA Manual

Complete instruction encoding for the 64-bit RISC-V style architecture.
All words are fixed 32-bit little-endian, PC is a 64-bit byte address
and must stay 4-byte aligned. `R0/zero` reads as 0 and discards writes.

## Formats

### R-Type

```
31      25 24   20 19   15 14 12 11   7 6     0
 funct7      rs2      rs1   funct3   rd   opcode(0x33)
```

| Mnemonic | funct3 | funct7 | Operation |
|---|---|---|---|
| ADD | 000 | 0000000 | rd = rs1 + rs2 |
| SUB | 000 | 0100000 | rd = rs1 - rs2 |
| SLL | 001 | 0000000 | rd = rs1 << (rs2 & 63) |
| SLT | 010 | 0000000 | rd = (rs1 < rs2, signed) |
| SLTU | 011 | 0000000 | rd = (rs1 < rs2, unsigned) |
| XOR | 100 | 0000000 | rd = rs1 ^ rs2 |
| SRL | 101 | 0000000 | rd = rs1 >> (rs2 & 63), logical |
| SRA | 101 | 0100000 | rd = rs1 >> (rs2 & 63), arithmetic |
| OR | 110 | 0000000 | rd = rs1 \| rs2 |
| AND | 111 | 0000000 | rd = rs1 & rs2 |
| MUL | 000 | 0000001 | rd = rs1 * rs2 (low 64) |
| DIV | 100 | 0000001 | rd = rs1 / rs2, signed (-1 on div-by-zero) |
| DIVU | 101 | 0000001 | rd = rs1 / rs2, unsigned (max on div-by-zero) |
| REM | 110 | 0000001 | rd = rs1 % rs2, signed |

FLAGS Z/N update on every ALU op; C/O update on ADD/SUB.

### I-Type (ALU / Loads / JALR / SYSTEM)

```
31        20 19   15 14 12 11   7 6     0
 imm[11:0]    rs1   funct3   rd   opcode
```

- ALU opcode `0x13`: ADDI(000), SLLI(001), SLTI(010), SLTIU(011),
  XORI(100), SRLI/SRAI(101), ORI(110), ANDI(111). Immediate is 12-bit
  signed (-2048..2047). Shifts use shamt 0..63.
- LOAD opcode `0x03`: LB(000), LH(001), LW(010), LD(011),
  LBU(100), LHU(101), LWU(110). Syntax `LD rd, offset(rs1)`.
- JALR opcode `0x67`, funct3 000: `JALR rd, offset(rs1)`,
  target `(rs1 + offset) & ~1`, must be 4-aligned.
- SYSTEM opcode `0x73`, funct3 000: imm `0x000` ECALL,
  `0x001` EBREAK/HALT, `0x102` IRET/SRET, otherwise `INT n`
  (immediate syscall ID, args in a0-a6).

### S-Type (Stores, opcode 0x23)

`SB(000), SH(001), SW(010), SD(011)`, syntax `SD rs2, offset(rs1)`.
Offset is 12-bit signed. Out-of-bounds accesses trap with an error.

### B-Type (Branches, opcode 0x63)

`BEQ(000), BNE(001), BLT(100), BGE(101), BLTU(110), BGEU(111)`.
Offset is 13-bit signed PC-relative (+/-4KB), LSB is zero.
Assembler computes `target - pc` and validates range.

### U-Type (LUI 0x37, AUIPC 0x17)

20-bit upper immediate. `LUI` loads `imm << 12` sign-extended
from 32 to 64 bits. `AUIPC` adds it to PC.

### J-Type (JAL 0x6F)

21-bit signed PC-relative offset (+/-1MB). `JAL rd, label`
saves `pc + 4` to `rd`.

## Pseudo-instructions (assembler expansion)

- `LI rd, imm64` — 1 word if -2048..2047, 2 words if 32-bit,
  otherwise recursive `LUI/ADDI/SLLI` chain (up to ~9 words).
- `LA rd, label` — always `LUI + ADDI` (2 words).
- `MV/NOP/NOT/NEG/SEQZ/SNEZ/J/JR/RET/CALL/HLT` — single-word aliases.
- `INT n` — SYSTEM with immediate syscall ID.

## Directives

`.org addr`, `.align N` (power-of-two bytes), `.text/.data` (no-op),
`.quad/.dword` (8B), `.word` (4B), `.half` (2B), `.byte` (1B),
`.string/.asciz` (NUL-terminated), `.ascii` (raw),
`.space/.zero` (N bytes), legacy `DB/DW/DS/RESB/RESW`,
`.equ name, value`, `.include "file"`.

## Memory Map

```
0x00000000 program load (os.bin / examples)
...
0x03E80000 8 x 64KB process stacks
0x03F00000 VRAM 80x25x2 = 4000 bytes
0x03FFF000 timer ticks (u64 RO)
0x03FFF008 timer period (u64 RW)
0x03FFF010 timer vector (u64 RW)
0x03FFFF00 serial console (u8)
0x04000000 end of 64MB RAM
```
