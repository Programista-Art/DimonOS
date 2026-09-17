# Roadmap implementation status

## Multi-window regression repair (verified)

- Minimizing no longer traps the guest in `window_minimize`: the non-leaf
  routine now preserves `ra`, clears capture, and returns to event dispatch.
  Empty/all-minimized desktops remain interactive and taskbar restore retains
  application state without duplicating Z-order entries.
- Active IDs, Z-order counts/entries, close/minimize targets, Alt+Tab scans and
  drag captures are range-checked. Focus selection is bounded even if state is
  stale, so an empty or inconsistent collection cannot create an endless scan.
- Calculator title dragging is covered end-to-end through ordered hosted input,
  real X11 XTEST input and QEMU PS/2 input. Capture continues outside the
  original title bounds, release ends it, controls do not start a drag, moved
  hit areas calculate correctly, and the frame is clamped to the work area.
- App-window Close and system Exit are tested separately. Close removes the
  window/taskbar entry and returns to a usable desktop; retained initialized
  state, including unsaved Notepad data, is not discarded. Exit draws and
  flushes an explicit terminal frame. The hosted process exits on guest
  `EBREAK`; bare metal requests QEMU ACPI S5 and falls back to a documented
  halted state if the platform does not implement that port.
- Focused regressions live in `tests/desktop_regression.py`,
  `tests/x11_regression.py`, and `tests/qemu_regression.py`. Bare-metal tests
  embed `build/regression/os.bin` and `build/regression/test-disk.img`; they do
  not read or overwrite the user's `dimon.iso`.

## Integrated

- Shared validated FAT16 API: nested traversal, unbounded directory-chain
  enumeration, stat/read/create/truncate, mkdir, empty-directory deletion,
  rename/move, file copy, allocation/release and explicit errors. Resolved
  root-directory path truncation in `split_parent()` and filename whitespace handling.
- Native Delphi-style modal guest file dialogs (`OpenDialog`, `SaveDialog`) browsing
  FAT16 volumes with editable folder/path and filename fields, keyboard/mouse list
  scrolling, text filter (*.TXT), root `/` path display, subdirectory navigation,
  canonical 8.3 name enforcement (`fat_validate_shortname` with truthful errors),
  and complete modal isolation.
- Coherent Notepad document workflow: untitled documents by default, Save writes to
  current path or opens Save As when untitled, Save As confirms overwrite via Mode 4 modal
  if file exists (`[Yes]`, `[No]`, `[Cancel]`), title-bar modified indicator `*`,
  Save/Discard/Cancel dirty prompt on New, Open, Close and Shutdown, 4000 byte buffer
  enforcement, staging buffer and UTF-8 pre-validation preventing editor buffer clobbering,
  truthful status reporting on read-only media (`Save failed: read-only disk`, dirty preserved),
  and complete keyboard shortcuts (F2/F9/Ctrl+S, F3, F10/Ctrl+O, Ctrl+N).
- File Explorer: correct non-zero decimal sizes (B/KB/MB) and `<DIR>` indicators on
  the initial view via `SYS_FS_LIST`, directory navigation, per-type action buttons
  (`[Open]`, `[Run]`, `[Edit]`, `[Info]`), and unified type dispatcher (.TXT to Notepad,
  .APP to `SYS_APP_EXEC` loader, directories navigate, unsupported show error).
- Compact taskbar: eliminated the unused reserved gap after Start; active and minimized
  applications pack tightly from x=100 with stride 78.
- Hosted X11 emulator fullscreen mode: F11 toggle, `--fullscreen`/`-f` CLI flags,
  discoverable window title indicator, centered 4:3 letterboxed/pillarboxed viewport
  preserving crisp bitmap font rendering and scaled hardware cursor, pointer coordinate
  transformation ignoring clicks in letterbox borders and releasing capture on release/FocusOut,
  and dynamic WM resize / `ConfigureNotify` handling without key desync.
- Modifier-aware input, editing keys, UTF-8 byte entry and Polish glyphs.
- A real wall-clock syscall; unavailable platforms display `RTC N/A`. Uptime
  remains available independently.
- DEXE64 output/loading/relocation, process metrics and termination, owned
  memory, guarded accelerated paths, and isolated-app fault cleanup.
- Terminal `pwd`, first-entry `ls`, and `run PATH` use shared services.
- Explicit app/image targets; image creation refuses accidental replacement.
- Retained desktop window collection for all seven embedded apps, with explicit
  stacking/focus, overlap, title-bar pointer-capture dragging, minimize,
  restore, close, per-app taskbar buttons and Alt+Tab. Apps initialize once and
  retain their editor/canvas/calculator/terminal/explorer/game state while
  obscured or minimized; Snake continues receiving timer work in background.
- Full back-to-front composition and a scoped guest drawing context translate
  each legacy app coordinate system to its window position. Rectangles, lines,
  pixels, text and blits are clipped by the VM, and mouse coordinates use the
  inverse transform. X11 and bare-metal PS/2 paths report release events and
  modifier state.
- UTF-8 glyph measurement and fitted-text services. Desktop tiles, the compact
  taskbar, top bar and window titles have explicit regions; titles
  use complete-glyph ellipsis and cannot overlap window controls.

## Not complete

Explorer does not expose write/delete/rename operations in GUI. Notepad lacks
arbitrary selection/clipboard, undo/redo, and vertical scrolling for documents
larger than one screen. Paint persistence/BMP/image viewer/shapes/fill/undo are absent.
Terminal lacks general quoted parsing, cwd mutation, history/completion/scrollback and most
file/process commands. Only Snake is a standalone DEXE64 app; the other apps
still depend on desktop internals. System Info has not become a Task Manager UI,
though the process API reports real metrics. Persistent Settings is absent.

The seven desktop applications remain embedded modules with one instance each.
The standalone DEXE64 display remains exclusive rather than being hosted in a
managed desktop window. Arbitrary window resizing/maximization is outside this milestone.

Persistent storage on bare-metal across reboots requires an ATA/AHCI block driver;
the standalone bare-metal ISO currently uses the embedded read-only root disk image
(`vm.disk_writable = 0`), whereas the hosted emulator fully supports persistent
FAT16 cluster and directory entry writes via `--disk-writable`.

Isolation applies to DEXE64 apps. PID 0 and legacy `SPAWN` tasks remain shared
for compatibility and must not be described as isolated.
