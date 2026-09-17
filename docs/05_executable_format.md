# DEXE64 executable format and application ABI

Build a relocatable guest executable with:

```sh
./dimon-as source.asm --dexe AppName -o APP.APP
```

The file is a 56-byte little-endian `Dimon64ExecHeader`, the flat image, then
`relocation_count` 32-bit offsets. Each relocation points to an assembler-
emitted `LUI+ADDI` pair for `LA` or label-valued `LI`. Calls and branches are
PC-relative. The loader validates every relocation before applying its base.

The version-1 header contains magic `DEXE64`, version, header/image/BSS sizes,
entry offset, requested memory size, relocation count, flags and a 16-byte
name. Version 1 supports a maximum 1 MiB image slot and entry offset zero.

At entry, `sp` addresses a private 64 KiB stack, `tp` is the PID, and `a0`
points to the optional argument. Apps use syscalls for resources and exit with
syscall 18. For loaded apps, `EBREAK` also exits (it still halts a legacy
standalone image).

Fetches, loads and stores are limited to the app region and stack. Direct VRAM,
MMIO and system memory access faults the app. Syscall text/file/blit/bulk-memory
buffers are checked too. Faults terminate and clean up only the offending app.
PID 0 is essential and cannot be killed.

Version 1 loaded apps are foreground: keyboard and pointer events route to the
latest loaded app until it exits, while other processes remain scheduled. A
future window-server protocol is required for composited app child windows.
