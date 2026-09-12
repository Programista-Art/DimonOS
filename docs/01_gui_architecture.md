# 01. GUI Architecture in Dimon-16

This document describes the Graphical User Interface (GUI) architecture, Video Memory (VRAM) subsystem, character cell structure, and system calls (syscalls).

---

## 1. Video Memory (VRAM)

To ensure high performance and strict compatibility with 16-bit virtual machines having 64 KB RAM, the GUI operates on a **character-attribute cell buffer** with a standard resolution of **80 columns x 25 rows**.

- **RAM Starting Address:** `0xE000`
- **RAM Ending Address:** `0xEFA0` (80 * 25 * 2 = 4000 bytes = 0x0FA0)
- **Memory Layout:**
  Every position `(X, Y)` on screen (where `0 <= X < 80` and `0 <= Y < 25`) maps to two consecutive bytes at:
  $$\text{Address} = \text{0xE000} + (Y \times 80 + X) \times 2$$

### Cell Format (2 bytes):
1. **Byte 0 (Even): Character Code (0–255)**
   - Printable ASCII (`0x20`–`0x7E`).
   - IBM CP437 box-drawing and semigraphic characters (e.g., corners `0xDA` ┌, `0xBF` ┐, lines `0xC4` ─, `0xB3` │, blocks `0xDB` █, `0xB0` ░, etc.).
2. **Byte 1 (Odd): Color Attribute (0–255)**
   - **Bits 0–3 (Low 4 bits):** Text / Foreground color (0–15)
   - **Bits 4–7 (High 4 bits):** Background color (0–15)

### Standard 16-Color Palette (VGA / ANSI):
| Value | Color | Value | Color |
|:---:|---|:---:|---|
| `0` | Black | `8` | Dark Gray |
| `1` | Blue | `9` | Light Blue |
| `2` | Green | `10` | Light Green |
| `3` | Cyan | `11` | Light Cyan |
| `4` | Red | `12` | Light Red |
| `5` | Magenta | `13` | Light Magenta |
| `6` | Brown / Amber | `14` | Yellow |
| `7` | Light Gray | `15` | White |

---

## 2. GUI System Calls (`INT 10` – `INT 15`)

Dedicated system calls provide graphics primitives, event dispatching, and timer access:

| Interrupt | Mnemonic | Input | Output | Description |
|:---:|---|---|---|---|
| **`INT 10`** | `SYS_GUI_INIT` | None | `R0` = width (80)<br>`R1` = height (25)<br>`R2` = VRAM pointer (`0xE000`) | Initializes GUI subsystem in emulator / bare-metal. |
| **`INT 11`** | `SYS_GUI_POLL_EVENT` | None | `R0` = event type<br>`R1` = code / X<br>`R2` = data / Y | Non-blocking retrieval of next event from queue. |
| **`INT 12`** | `SYS_GUI_FLUSH` | None | None | Synchronizes VRAM buffer to physical display (VGA / X11 / Terminal). |
| **`INT 13`** | `SYS_GET_TICKS` | None | `R0` = milliseconds (lo16)<br>`R1` = milliseconds (hi16) | Returns system uptime in milliseconds. |
| **`INT 14`** | `SYS_GUI_DRAW_RECT` | `R0` = X \| (Y << 8)<br>`R1` = W \| (H << 8)<br>`R2` = char \| (color << 8) | Fast rectangular fill in VRAM. |
| **`INT 15`** | `SYS_GUI_DRAW_TEXT` | `R0` = X \| (Y << 8)<br>`R1` = string pointer (null-terminated)<br>`R2` = color attribute | Draws string at coordinate `(X, Y)`. |

---

## 3. Event Protocol (`SYS_GUI_POLL_EVENT` - `INT 11`)

`INT 11` returns:
- **`R0` = Event Type:**
  - `0` — No pending events (queue empty)
  - `1` — Keyboard event (`KEY_PRESS`)
    - `R1` = ASCII character code or special keycode:
      - `1`..`26` = Ctrl+A .. Ctrl+Z
      - `27` = Escape
      - `13` = Enter
      - `8` = Backspace
      - `9` = Tab
      - `256` = Arrow Up
      - `257` = Arrow Down
      - `258` = Arrow Left
      - `259` = Arrow Right
      - `260`..`269` = Function keys F1 .. F10
  - `2` — Mouse click (`MOUSE_CLICK`)
    - `R1` = Screen column `X` (0..79)
    - `R2` = Screen row `Y` (0..24)
  - `3` — Mouse move (`MOUSE_MOVE`)
    - `R1` = Screen column `X` (0..79)
    - `R2` = Screen row `Y` (0..24)
  - `4` — Timer tick (`TIMER_TICK`)

---

## 4. Dual Rendering Backend & Bare-Metal Target

1. **Bare-Metal x86 Kernel:**
   - Multiboot 1 standard bootable ISO.
   - Direct physical VGA text mode memory at `0x000B8000`.
   - PS/2 Keyboard and PS/2 Mouse hardware drivers with 8259 PIC and 8254 PIT.
2. **Native X11 Window (Host):**
   - Built with standard `libX11`.
   - Rendered using an 8x16 font glyph bitmap matrix.
   - Supports native mouse clicks and keyboard events.
3. **ANSI TUI Terminal (Host):**
   - Runs directly in Unix terminals via raw `termios`.
   - Renders frames using ANSI escape sequences.
   - Ideal for SSH sessions and automated testing.
