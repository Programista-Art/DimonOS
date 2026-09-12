# 04. DimonOS v2.0 User Manual

Comprehensive user guide for compiling, running, and navigating **DimonOS v2.0**.

---

## 1. Prerequisites and Build Environment

### Required Software (Ubuntu / Debian):
```bash
sudo apt update
sudo apt install build-essential libx11-dev qemu-system-x86 grub-pc-bin xorriso
```

### Building the Entire Toolchain & Systems:
```bash
make clean && make all && make progs && make iso
```
This produces:
- `dimon-as`: Dimon-16 two-pass macro assembler.
- `dimon-emu`: Dual-backend VM emulator (X11 & ANSI TUI).
- `dimon-mkiso`: ISO filesystem creator for Dimon-16.
- `os.bin`: Assembled DimonOS v2.0 GUI operating system.
- `dimon.iso`: Bootable virtual disk with directory catalog.
- `dimon-kernel.elf`: 32-bit Multiboot native bare-metal kernel.
- `dimon-baremetal.iso`: Bootable standalone PC image for QEMU & bare metal.

---

## 2. Running DimonOS

### Method 1: Host Emulator (Native X11 Window)
```bash
./dimon-emu --gui os.bin --iso dimon.iso
```
- Opens a native X11 window rendered at 640×400 (or scaled up).
- Supports mouse clicking, movement, and keyboard input.

### Method 2: Terminal Console (ANSI TUI)
```bash
./dimon-emu --tui os.bin --iso dimon.iso
```
- Renders directly in any terminal using ANSI escape codes and CP437 unicode glyphs.
- Supports SGR mouse tracking and full keyboard interaction.

### Method 3: Standalone Bare-Metal in QEMU
```bash
# Direct kernel boot
qemu-system-i386 -kernel dimon-kernel.elf

# Bootable CD-ROM ISO via GRUB
make qemu
# or manually:
qemu-system-i386 -cdrom dimon-baremetal.iso
```

### Method 4: Real x86 Hardware (USB Flash Drive)
```bash
# Write directly to USB drive (replace /dev/sdX with your USB drive)
sudo dd if=dimon-baremetal.iso of=/dev/sdX bs=4M status=progress conv=fsync
```
Boot your computer from the USB drive via BIOS or UEFI CSM.

---

## 3. Keyboard Shortcuts and Controls

### Global Navigation:
- **`ESC`**: Closes the active window or dismisses the Start Menu.
- **`F1` – `F7`**: Opens corresponding application:
  - `F1`: Calculator
  - `F2`: Notepad
  - `F3`: File Explorer
  - `F4`: Paint
  - `F5`: System Info
  - `F6`: Snake (Game)
  - `F7`: Terminal CLI
- **`1` – `7`**: When on the clean desktop, launches the application with that number.
- **`q`**: Exits the OS when on the clean desktop.

### Application-Specific Shortcuts:
- **Notepad**:
  - `F8`: Clear and start a new document.
  - `F9`: Save document to ISO sector 10.
  - `F10`: Load document from ISO sector 10.
- **File Explorer**:
  - `Up` / `Down`: Move selection cursor.
  - `Enter`: Preview selected file.
  - `R`: Refresh catalog.
- **Paint**:
  - `Arrows`: Move drawing pen.
  - `Space`: Draw at pen location.
  - `1`–`6`: Select brush character.
  - `R`, `G`, `B`, `Y`, `W`: Select color.
  - `C`: Clear canvas.
- **Snake**:
  - `Arrows`: Change direction.
  - `R`: Restart game.
- **Terminal CLI**:
  - Type commands (`help`, `info`, `fib 10`, `add 15 25`, `clear`, `exit`) and press `Enter`.
