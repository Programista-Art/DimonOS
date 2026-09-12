# 03. DimonOS v2.0 Applications Suite

This document describes the technical specifications, data structures, memory usage, and user interfaces of all 7 built-in applications in **DimonOS v2.0**.

---

## Application Summary

| No. | Application | Source File | Shortcut | Description |
|:---:|---|---|:---:|---|
| **1** | **Calculator** | [apps/calc.asm](file:///home/programista/Pulpit/Dimon/apps/calc.asm) | `F1` or `1` | 16-bit integer calculator with mouse keypad |
| **2** | **Notepad** | [apps/notepad.asm](file:///home/programista/Pulpit/Dimon/apps/notepad.asm) | `F2` or `2` | Text editor with direct ISO disk load/save |
| **3** | **File Explorer** | [apps/fileman.asm](file:///home/programista/Pulpit/Dimon/apps/fileman.asm) | `F3` or `3` | ISO filesystem directory browser & file viewer |
| **4** | **Paint** | [apps/paint.asm](file:///home/programista/Pulpit/Dimon/apps/paint.asm) | `F4` or `4` | Semigraphic drawing editor with color palette |
| **5** | **System Info** | [apps/sysinfo.asm](file:///home/programista/Pulpit/Dimon/apps/sysinfo.asm) | `F5` or `5` | Hardware monitor, VM specs, and live uptime clock |
| **6** | **Snake (Game)** | [apps/snake.asm](file:///home/programista/Pulpit/Dimon/apps/snake.asm) | `F6` or `6` | Timer-driven arcade snake game with scoring |
| **7** | **Terminal CLI** | [apps/terminal.asm](file:///home/programista/Pulpit/Dimon/apps/terminal.asm) | `F7` or `7` | Embedded shell with history and commands |

---

## 1. Calculator (`calc.asm`)

### 1.1. State & Data Structures:
- `calc_val1` (`DW`): First operand (16-bit unsigned integer `0..65535`).
- `calc_val2` (`DW`): Currently entered value displayed on screen.
- `calc_op` (`DB`): Active operation code:
  - `0` — None
  - `1` — Addition (`+`)
  - `2` — Subtraction (`-`)
  - `3` — Multiplication (`*`)
  - `4` — Division (`/`)
- `calc_str_buf` (`DS 16`): Buffer for numeric string representation.

### 1.2. Layout:
- Geometry: `X = 22, Y = 4, W = 36, H = 16`.
- Display: Dark panel `(X=25, Y=6, W=30, H=3)` showing right-aligned number and current operation symbol (`+`, `-`, `*`, `/`).
- Virtual 4×4 Keypad:
  - Row 10: `[ 7 ]   [ 8 ]   [ 9 ]   [ + ]`
  - Row 11: `[ 4 ]   [ 5 ]   [ 6 ]   [ - ]`
  - Row 12: `[ 1 ]   [ 2 ]   [ 3 ]   [ * ]`
  - Row 13: `[ C ]   [ 0 ]   [ = ]   [ / ]`

### 1.3. Controls:
- **Keyboard**:
  - `0`–`9`: Appends digit (`calc_val2 = calc_val2 * 10 + digit`).
  - `+`, `-`, `*`, `/`: Sets operand and operator.
  - `Enter` or `=`: Computes result. Division by zero yields `0`.
  - `C` or `c`: Clears state.
  - `Backspace`: Removes last digit (`calc_val2 = calc_val2 / 10`).
- **Mouse**: Clicking any keypad button triggers the corresponding action.

---

## 2. Notepad (`notepad.asm`)

### 2.1. State & Data Structures:
- `note_len` (`DW`): Current text length in characters (up to 500).
- `note_cursor` (`DW`): Cursor index.
- `note_buf` (`DS 512`): Text buffer in RAM (512 bytes, 1 disk sector).
- `note_status_msg` (`DW`): Pointer to current status bar text.

### 2.2. Editor Capabilities:
- Workspace: `X = 12, Y = 7, W = 56, H = 11` (dark blue background `0x17`).
- Text wrapping: Automatically wraps lines at column 67 or at `\n`.
- Cursor: Renders blinking underscore cursor `_`.
- Editing keys:
  - Printable ASCII (`0x20`–`0x7E`): Inserts character.
  - `Enter`: Inserts newline (`\n`).
  - `Backspace`: Deletes preceding character.

### 2.3. ISO Disk Persistence:
- **`[F8:New]` / F8**: Resets buffer to empty note.
- **`[F9:Save ISO]` / F9**: Calls `INT 8` (`SYS_DISK_WRITE`) writing 512 bytes of `note_buf` to Sector 10.
- **`[F10:Load ISO]` / F10**: Calls `INT 6` (`SYS_DISK_READ`) loading Sector 10 into `note_buf`.

---

## 3. ISO File Explorer (`fileman.asm`)

### 3.1. DIMON-ISO Filesystem:
- Queries disk status with `INT 7` (`SYS_DISK_INFO`).
- Loads directory catalog at **Sector 1 (LBA 1)**.
- Parses 24-byte directory entries:
  - `0..11`: Filename (up to 11 chars + null).
  - `12..13`: Starting LBA sector.
  - `14..15`: Sector count.
  - `16..19`: File size in bytes.

### 3.2. Two-Panel Interface:
- Window size: `W = 60, H = 18` (position `X = 10, Y = 3`).
- **Left Panel (File List)**: `X = 12..30`. Selected file marked with `> `.
- **Right Panel (Preview)**: `X = 33..68`. Displays the first 512 bytes of text loaded from disk via `INT 6`.
- Navigation: `Up` / `Down` arrows select files, `Enter` loads preview, `R` refreshes catalog.

---

## 4. Paint (`paint.asm`)

### 4.1. Canvas and Tools:
- Canvas: 48 columns × 10 rows (`X = 16..63, Y = 7..16`).
- Buffers: `paint_canvas_chars` (480 B) and `paint_canvas_attrs` (480 B).
- Brushes: `[1:█]` Block, `[2:░]` Dither, `[3:*]` Star, `[4:#]` Hash, `[5:•]` Dot, `[6: ]` Eraser.
- Palette: Red (`R`), Green (`G`), Blue (`B`), Yellow (`Y`), White (`W`).

### 4.2. Controls:
- **Keyboard**:
  - Arrow keys: Move pen cursor.
  - `Space`: Plot character.
  - `1`–`6`: Select brush.
  - `R`, `G`, `B`, `Y`, `W`: Select color.
  - `C`: Clear canvas.
- **Mouse**: Clicking the canvas immediately paints the cell; clicking tool/color buttons selects them.

---

## 5. System Info (`sysinfo.asm`)

### 5.1. Hardware & System Telemetry:
- Architecture: 16-bit CISC, 8 registers (`R0`–`R7`), 64 KB RAM.
- Screen: 80×25 text mode (CP437, 16 VGA colors).
- Audio / Peripherals: PC Speaker, 8259 PIC, 8254 PIT, PS/2 Keyboard & Mouse.
- Live Uptime Counter: Uses `INT 13` (`SYS_GET_TICKS`) to calculate seconds, updating on each `EVT_TIMER` event.

---

## 6. Snake Game (`snake.asm`)

### 6.1. Gameplay Mechanics:
- Playfield: 50 columns × 15 rows with border.
- Real-time Movement: Driven by `EVT_TIMER` every 100 ms.
- Food generation: Random pseudo-generator places food `*`.
- Scoring: Increases score upon eating food and grows the snake.
- Controls: Arrow keys steer the snake; `R` restarts after Game Over.

---

## 7. Terminal CLI (`terminal.asm`)

### 7.1. Built-in Commands:
- `help` — Lists available commands.
- `info` — Shows OS & VM architecture details.
- `clear` — Clears terminal scrollback.
- `fib [N]` — Computes N-th Fibonacci number.
- `add [A] [B]` — Adds two integers.
- `exit` — Closes the terminal window.
- Interactive prompt `DimonOS> ` with cursor, typing, backspace, and history scroll.
