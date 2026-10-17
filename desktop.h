/* DimonOS-64 native desktop (Window Manager + 7 applications).
 * 1:1 C port of os.asm: same layout, colors, coordinates and behaviour.
 * Drawing goes through vm_gui_*; storage through dimonfs_*; no vm_step. */
#ifndef DIMON_DESKTOP_H
#define DIMON_DESKTOP_H

#include "kernel_runtime.h"

#define DESK_W DIMON64_LFB_WIDTH
#define DESK_H DIMON64_LFB_HEIGHT

/* Palette (exact os.asm colours) */
#define COL_BG          0xFF1E2430u
#define COL_TOPBAR      0xFF111827u
#define COL_TITLE       0xFF2563EBu
#define COL_TITLE_OFF   0xFF334155u
#define COL_PANEL       0xFF0F172Au
#define COL_PANEL2      0xFF2D3748u
#define COL_BORDER      0xFF475569u
#define COL_TEXT        0xFFFFFFFFu
#define COL_DIM         0xFF94A3B8u
#define COL_ACCENT      0xFF38BDF8u
#define COL_GREEN       0xFF34C759u
#define COL_START       0xFF10B981u
#define COL_START_DK    0xFF059669u
#define COL_RED         0xFFFF3B30u
#define COL_RED_DK      0xFFEF4444u
#define COL_MENU        0xFF1E222Du
#define COL_ENTRY       0xFFF1F5F9u
#define COL_TASK_OFF    0xFF334155u

/* Window ids (1-based, matching os.asm) */
#define WIN_NONE  0
#define WIN_CALC  1
#define WIN_NOTE  2
#define WIN_FILES 3
#define WIN_PAINT 4
#define WIN_INFO  5
#define WIN_SNAKE 6
#define WIN_TERM  7
#define WIN_COUNT 7

#define WIN_FLAG_OPEN 1
#define WIN_FLAG_MIN  2

#define TITLEBAR_H 32
#define WORK_TOP   28
#define WORK_BOT   568

#define NOTE_BUF_MAX 4000
#define PAINT_W 640
#define PAINT_H 400

void desktop_run(VM *vm);

#endif /* DIMON_DESKTOP_H */
