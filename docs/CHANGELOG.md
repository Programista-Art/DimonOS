# Changelog - DimonOS

This document chronologically tracks all changes, improvements, and releases for **Dimon-16** and **DimonOS**.

---

## [2026-09-12] - DimonOS v2.1 (Bare-Metal Native & English Localization)

### 1. Bare-Metal x86 Native Execution
- **Multiboot 1 Compliant 32-bit x86 Kernel (`arch/x86/`)**:
  - `arch/x86/boot.S`: Multiboot header, GDT configuration, stack setup, ISR interrupt stubs, embedded `os.bin` and `dimon.iso` assets.
  - `arch/x86/kernel.c`: Freestanding C runtime (`memset`, `memcpy`, `memmove`, `strlen`), 8259 PIC remapping, IDT setup, 8254 PIT (1000 Hz) timer, PS/2 keyboard controller (Set 1 scancodes), PS/2 mouse controller, serial COM1 logging, and VM loop.
  - `arch/x86/io.h`: Inline port I/O routines (`inb`, `outb`, `io_wait`, `cli`, `sti`, `hlt`).
  - `arch/x86/linker.ld`: 32-bit ELF linker script loading at 1MB physical memory.
  - `arch/x86/grub.cfg`: GRUB bootloader configuration for automatic OS loading.
- **Physical VGA Text Mode 80×25**:
  - Direct 1:1 memory mapping of Dimon-16 VRAM (`0xE000`, 4000 bytes) to physical VGA memory (`0x000B8000`).
  - VGA Attribute Controller configuration to enable all 16 background colors.
  - Hardware text cursor synchronized with mouse pointer.
- **Standalone Bootable ISO (`make baremetal-iso`)**:
  - Creates `dimon-baremetal.iso` via `grub-mkrescue` with hybrid El Torito / MBR support.
  - Boots in QEMU (`make qemu` or `qemu-system-i386 -cdrom dimon-baremetal.iso`) and on real x86 PC hardware without any host OS.

### 2. Startup Display Fix (Black Screen Resolution)
- **Immediate Desktop Display**:
  - Ensured `ensure_gui_init()` triggers during `SYS_GUI_INIT` (`INT 10`) and `SYS_GUI_FLUSH` (`INT 12`) in `emulator.c`.
  - GUI and VRAM buffers are flushed and drawn immediately upon startup without requiring any user click or keypress.

### 3. Full System Localization to English
- **Desktop Environment & Shell**:
  - Top bar: `[ DimonOS v2.0 ] 1 Calc  2 Notes  3 Files  4 Paint  5 Info  6 Snake  7 Term`.
  - Desktop icons: `[1] Calculator`, `[2] Notepad`, `[3] File Explorer`, `[4] Paint`, `[5] System Info`, `[6] Snake (Game)`, `[7] Terminal CLI`.
  - Taskbar: `[ START ]`, `[ Desktop ]`, `[F1-F7 Start] [ESC Close]`.
  - Start Menu: English titles, separator, and `X. Exit OS`.
- **All 7 Applications Localized**:
  - Calculator, Notepad, File Explorer, Paint, System Info, Snake, Terminal CLI localized in all titles, status bars, hints, and messages.
- **C Code & Toolchain**:
  - Assembler (`assembler.c`), Emulator (`emulator.c`), VM (`vm.c`), ISO builder (`mkiso.c`), and header (`dimon16.h`) localized to English.
- **Documentation**:
  - All Markdown documents in `docs/` and `README.md` translated to English.

---

## [2026-09-12] - DimonOS v2.0 GUI

### 1. Video Memory (VRAM) & Syscalls
- VRAM buffer at `0xE000..0xEFA0` (80 columns × 25 rows × 2 bytes = 4000 bytes).
- Cell format: even byte = ASCII / CP437 character, odd byte = color attribute (0–15 foreground, 0–15 background).
- New system calls:
  - `INT 10` (`SYS_GUI_INIT`): Initialize graphics mode.
  - `INT 11` (`SYS_GUI_POLL_EVENT`): Non-blocking event retrieval.
  - `INT 12` (`SYS_GUI_FLUSH`): VRAM screen sync.
  - `INT 13` (`SYS_GET_TICKS`): System uptime in ms.
  - `INT 14` (`SYS_GUI_DRAW_RECT`): Rectangle fill in VRAM.
  - `INT 15` (`SYS_GUI_DRAW_TEXT`): Draw string at (X, Y).

### 2. Dual Rendering Backends
- **Native X11 Window**: 8×16 bitmap font, 640×400 resolution, scalable via `--scale`, mouse and keyboard events.
- **ANSI TUI Console**: Raw terminal mode, UTF-8 box-drawing translation, SGR mouse tracking.

### 3. Applications Suite
- Calculator, Notepad (with ISO disk persistence), ISO File Explorer, Paint, System Info, Snake, Terminal CLI.
