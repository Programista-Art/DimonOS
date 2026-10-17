/* DimonOS-64 native desktop — C port of os.asm.
 * Window Manager, Taskbar, Start Menu, Dialogs and 7 applications. */
#include "desktop.h"
#include "kernel_runtime.h"
#include "napp.h"
#include "dimonfs.h"
#include <stdint.h>
#include <stddef.h>

/* ------------------------------------------------------------------ */
/* Drawing helpers (map 1:1 onto the os.asm syscalls)                  */
/* ------------------------------------------------------------------ */
static VM *g_vm;

static void fill_rect(int x, int y, int w, int h, uint32_t c) {
    vm_gui_fill_rect(g_vm, x, y, w, h, c);
}
static void draw_text(int x, int y, const char *s, uint32_t fg) {
    vm_gui_draw_string(g_vm, x, y, s, fg, 0);
}
static void draw_text_bg(int x, int y, const char *s, uint32_t fg, uint32_t bg) __attribute__((unused));
static void draw_pixel(int x, int y, uint32_t c) __attribute__((unused));
static void draw_text_bg(int x, int y, const char *s, uint32_t fg, uint32_t bg) {
    vm_gui_draw_string(g_vm, x, y, s, fg, bg);
}
static void draw_pixel(int x, int y, uint32_t c) {
    vm_gui_draw_pixel(g_vm, x, y, c);
}

static size_t u8len(const unsigned char *s) {
    if (!s[0]) return 0;
    if (s[0] < 0x80) return 1;
    if ((s[0] & 0xe0) == 0xc0 && (s[1] & 0xc0) == 0x80) return 2;
    if ((s[0] & 0xf0) == 0xe0 && (s[1] & 0xc0) == 0x80 && (s[2] & 0xc0) == 0x80) return 3;
    if ((s[0] & 0xf8) == 0xf0 && (s[1] & 0xc0) == 0x80 &&
        (s[2] & 0xc0) == 0x80 && (s[3] & 0xc0) == 0x80) return 4;
    return 1;
}
static int text_measure(const char *s) {
    int n = 0;
    while (s && *s) { s += u8len((const unsigned char *)s); n++; }
    return n * 8;
}
static void text_fit(int x, int y, const char *s, uint32_t fg, int max_w) {
    if (!s || max_w <= 0) return;
    int maxg = max_w / 8;
    if (text_measure(s) <= max_w) { draw_text(x, y, s, fg); return; }
    char buf[256];
    int gi = 0, used = 0;
    const unsigned char *p = (const unsigned char *)s;
    int keep = maxg > 3 ? maxg - 3 : maxg;
    while (*p && gi < keep && used + 4 < (int)sizeof(buf)) {
        size_t n = u8len(p);
        for (size_t k = 0; k < n && used + 1 < (int)sizeof(buf); k++) buf[used++] = (char)p[k];
        p += n; gi++;
    }
    while (gi++ < maxg && used + 1 < (int)sizeof(buf)) buf[used++] = '.';
    buf[used] = 0;
    draw_text(x, y, buf, fg);
}

static void set_context(int dx, int dy, int x, int y, int w, int h) {
    if (w <= 0 || h <= 0) {
        g_vm->draw_context_active = 0;
        g_vm->draw_offset_x = g_vm->draw_offset_y = 0;
        g_vm->draw_clip_w = g_vm->draw_clip_h = 0;
        return;
    }
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > DESK_W) w = DESK_W - x;
    if (y + h > DESK_H) h = DESK_H - y;
    g_vm->draw_offset_x = dx; g_vm->draw_offset_y = dy;
    g_vm->draw_clip_x = x; g_vm->draw_clip_y = y;
    g_vm->draw_clip_w = w > 0 ? w : 0;
    g_vm->draw_clip_h = h > 0 ? h : 0;
    g_vm->draw_context_active = 1;
}

static void blit_canvas(int dx, int dy, int w, int h, const uint32_t *src) {
    int ox = g_vm->draw_offset_x, oy = g_vm->draw_offset_y;
    int cx0 = 0, cy0 = 0, cx1 = DESK_W, cy1 = DESK_H;
    if (g_vm->draw_context_active) {
        dx += ox; dy += oy;
        cx0 = g_vm->draw_clip_x; cy0 = g_vm->draw_clip_y;
        cx1 = cx0 + g_vm->draw_clip_w; cy1 = cy0 + g_vm->draw_clip_h;
    }
    int x0 = dx < 0 ? 0 : dx, y0 = dy < 0 ? 0 : dy;
    int x1 = dx + w, y1 = dy + h;
    if (x1 > DESK_W) x1 = DESK_W;
    if (y1 > DESK_H) y1 = DESK_H;
    if (x0 < cx0) x0 = cx0;
    if (y0 < cy0) y0 = cy0;
    if (x1 > cx1) x1 = cx1;
    if (y1 > cy1) y1 = cy1;
    uint32_t *vram = (uint32_t *)(g_vm->mem + DIMON64_VRAM_BASE);
    for (int sy = y0; sy < y1; sy++) {
        int srow = sy - dy;
        for (int sx = x0; sx < x1; sx++) {
            vram[sy * DESK_W + sx] = src[srow * w + (sx - dx)];
        }
    }
    g_vm->gui_dirty = 1;
}

/* ------------------------------------------------------------------ */
/* Global desktop state                                                */
/* ------------------------------------------------------------------ */
static int need_redraw = 1;
static int exit_requested = 0;
static int start_menu_open = 0;
static int active_window;
static int win_z_count;
static int win_z_order[8];
static int win_flags[WIN_COUNT + 1];
static int win_x[WIN_COUNT + 1], win_y[WIN_COUNT + 1];
static int win_w[WIN_COUNT + 1], win_h[WIN_COUNT + 1];
static int win_default_x[WIN_COUNT + 1], win_default_y[WIN_COUNT + 1];
static int drag_active, drag_window, drag_off_x, drag_off_y;
static int taskbar_map[8], taskbar_count;

static const int DEF_X[WIN_COUNT + 1] = {0, 240, 80, 100, 60, 120, 120, 100};
static const int DEF_Y[WIN_COUNT + 1] = {0, 70, 45, 60, 35, 70, 50, 60};
static const int DEF_W[WIN_COUNT + 1] = {0, 320, 640, 600, 680, 560, 560, 600};
static const int DEF_H[WIN_COUNT + 1] = {0, 440, 500, 460, 520, 440, 490, 460};
static const char *const WIN_TITLE[WIN_COUNT + 1] = {
    "", "[ Calculator ]", "[ Notepad ]", "[ File Explorer ]",
    "[ Paint Studio ]", "[ System Info ]", "[ Snake 64 ]", "[ Terminal CLI ]"
};
static const char *const WIN_SHORT[WIN_COUNT + 1] = {
    "", "Calc", "Notepad", "Files", "Paint", "Info", "Snake", "Terminal"
};

/* Dialog state (modes 1=Open 2=SaveAs 3=Confirm 4=Overwrite) */
static int dlg_active, dlg_mode, dlg_owner, dlg_focus;
static int dlg_path_len, dlg_name_len;
static int dlg_name_caret, dlg_name_scroll;
static int dlg_sel, dlg_scroll, dlg_count;
static int dlg_pending;
static char dlg_path[260], dlg_name[64], dlg_err[128];
static char dlg_list_buf[512][16];

/* Calculator */
static int64_t calc_acc, calc_cur;
static int calc_op;
static char calc_buf[32];

/* Notepad */
static char note_buf[NOTE_BUF_MAX];
static int note_len, note_cursor, note_dirty;
static char note_path[260];
static char note_status[64];
static char note_title[96];

/* File Explorer */
static char fm_cwd[260];
static int fm_count, fm_sel, fm_scroll, fm_last_row;
static uint32_t fm_last_ms;
static char fm_list[256][16];
static int fm_isdir[256];
static uint32_t fm_size[256];

/* Paint */
static uint32_t paint_canvas[PAINT_W * PAINT_H];
static uint32_t paint_color;
static int paint_radius, paint_eraser;

/* Snake */
#define SNAKE_MAX 128
#define SNAKE_W 30
#define SNAKE_H 20
static int snake_x[SNAKE_MAX], snake_y[SNAKE_MAX];
static int snake_len, snake_dir, snake_score, snake_gameover;
static int food_x, food_y, snake_tick;

/* Terminal */
static char term_buf[64];
static int term_len;
static char term_out[128];

static void flag_redraw(void) { need_redraw = 1; }

static uint8_t g_last_mods;

/* ------------------------------------------------------------------ */
/* Sound (PC speaker / PSG callback already bound by kernel)           */
/* ------------------------------------------------------------------ */
static void beep(uint32_t freq, uint32_t ms) {
    if (g_vm->psg_play_cb)
        g_vm->psg_play_cb(g_vm->psg_userdata, freq, ms, 0, 160);
}

/* ------------------------------------------------------------------ */
/* String helpers                                                      */
/* ------------------------------------------------------------------ */
static int str_eq(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return *a == *b;
}
static void str_copy(char *d, const char *s) {
    while ((*d++ = *s++) != 0) {}
}
static int str_len(const char *s) { int n = 0; while (s[n]) n++; return n; }
static void str_append(char *d, const char *s, int cap) {
    int n = str_len(d);
    while (*s && n < cap - 1) d[n++] = *s++;
    d[n] = 0;
}

static void num_to_dec(int64_t v, char *out) {
    char tmp[24];
    int n = 0, neg = 0;
    if (v < 0) { neg = 1; v = -v; }
    if (v == 0) tmp[n++] = '0';
    while (v > 0) {
        int64_t q = v / 10;
        tmp[n++] = (char)('0' + (int)(v - q * 10));
        v = q;
    }
    int o = 0;
    if (neg) out[o++] = '-';
    while (n > 0) out[o++] = tmp[--n];
    out[o] = 0;
}

static void fmt_size(uint32_t sz, char *out) {
    if (sz < 1024) {
        num_to_dec(sz, out);
        str_append(out, " B", 32);
    } else if (sz < 1024u * 1024u) {
        num_to_dec(sz / 1024, out);
        str_append(out, " KB", 32);
    } else {
        num_to_dec(sz / (1024u * 1024u), out);
        str_append(out, " MB", 32);
    }
}

/* ------------------------------------------------------------------ */
/* Window Manager                                                      */
/* ------------------------------------------------------------------ */
static void wm_raise(int id) {
    if (id < 1 || id > WIN_COUNT) return;
    for (int i = 0; i < win_z_count; i++) {
        if (win_z_order[i] == id) {
            for (int j = i; j < win_z_count - 1; j++) win_z_order[j] = win_z_order[j + 1];
            win_z_count--;
            break;
        }
    }
    win_z_order[win_z_count++] = id;
    active_window = id;
}

static void wm_activate_top(void) {
    for (int i = win_z_count - 1; i >= 0; i--) {
        int id = win_z_order[i];
        if (id >= 1 && id <= WIN_COUNT && (win_flags[id] & WIN_FLAG_OPEN) &&
            !(win_flags[id] & WIN_FLAG_MIN)) {
            active_window = id;
            return;
        }
    }
    active_window = 0;
}

static void window_open(int id) {
    if (id < 1 || id > WIN_COUNT) return;
    win_flags[id] |= WIN_FLAG_OPEN;
    win_flags[id] &= ~WIN_FLAG_MIN;
    wm_raise(id);
    if (id == WIN_SNAKE) {
        snake_len = 4; snake_dir = 1; snake_score = 0; snake_gameover = 0;
        snake_tick = 0;
        for (int i = 0; i < snake_len; i++) { snake_x[i] = 10 - i; snake_y[i] = 10; }
        food_x = 20; food_y = 10;
    }
    if (id == WIN_TERM) { term_len = 0; term_out[0] = 0; str_copy(term_out, "DimonOS Terminal. Type 'help'."); }
    if (id == WIN_NOTE && note_buf[0] == 0 && note_len == 0) {
        str_copy(note_buf, "Welcome to DimonOS-64 Notepad with FAT16!\nPress Save (F9) to write changes to disk.\nPress Open (F10) to reload text from disk.\n");
        note_len = str_len(note_buf);
        note_cursor = note_len;
    }
    flag_redraw();
}

static void window_minimize(void) {
    int id = active_window;
    if (id < 1 || id > WIN_COUNT) return;
    win_flags[id] |= WIN_FLAG_MIN;
    drag_active = 0;
    wm_activate_top();
    flag_redraw();
}

static void window_close(void) {
    int id = active_window;
    if (id < 1 || id > WIN_COUNT) return;
    win_flags[id] = 0;
    for (int i = 0; i < win_z_count; i++) {
        if (win_z_order[i] == id) {
            for (int j = i; j < win_z_count - 1; j++) win_z_order[j] = win_z_order[j + 1];
            win_z_count--;
            break;
        }
    }
    wm_activate_top();
    flag_redraw();
}

static void wm_alt_tab(void) {
    if (win_z_count == 0) return;
    int last = win_z_order[win_z_count - 1];
    for (int i = win_z_count - 1; i > 0; i--) win_z_order[i] = win_z_order[i - 1];
    win_z_order[0] = last;
    wm_activate_top();
    flag_redraw();
}

static void taskbar_rebuild(void) {
    taskbar_count = 0;
    for (int id = 1; id <= WIN_COUNT; id++) {
        if (win_flags[id] & WIN_FLAG_OPEN) taskbar_map[taskbar_count++] = id;
    }
}

static void draw_window_frame(int id, int x, int y, int w, int h, const char *title) {
    uint32_t tc = (id == active_window) ? COL_TITLE : COL_TITLE_OFF;
    fill_rect(x, y, w, h, COL_PANEL);
    fill_rect(x, y, w, TITLEBAR_H, tc);
    draw_text(x + 12, y + 8, title, COL_TEXT);
    /* [-] and [X] at right edge of title bar */
    fill_rect(x + w - 52, y + 4, 24, 24, COL_BORDER);
    draw_text(x + w - 46, y + 8, "-", COL_TEXT);
    fill_rect(x + w - 28, y + 4, 24, 24, COL_RED_DK);
    draw_text(x + w - 22, y + 8, "X", COL_TEXT);
}

/* Historical coordinates → screen via per-window translation. */
static void wm_set_context(int id) {
    int tx = win_x[id] - win_default_x[id];
    int ty = win_y[id] - win_default_y[id];
    set_context(tx, ty, win_x[id], win_y[id], win_w[id], win_h[id]);
}

static void wm_reset_context(void) { set_context(0, 0, 0, 0, 0, 0); }

/* ------------------------------------------------------------------ */
/* Applications: forward decls                                         */
/* ------------------------------------------------------------------ */
static void calc_draw(void), calc_on_key(int key), calc_on_click(int x, int y);
static void note_draw(void), note_on_key(int key, int mod), note_on_click(int x, int y);
static void files_draw(void), files_on_key(int key), files_on_click(int x, int y);
static void paint_draw(void), paint_on_key(int key), paint_on_click(int x, int y, int btn);
static void paint_on_drag(int x, int y);
static void info_draw(void);
static void snake_draw(void), snake_on_key(int key), snake_on_timer(void);
static void term_draw(void), term_on_key(int key), term_on_click(int x, int y);

static void app_draw(int id) {
    switch (id) {
        case WIN_CALC:  calc_draw(); break;
        case WIN_NOTE:  note_draw(); break;
        case WIN_FILES: files_draw(); break;
        case WIN_PAINT: paint_draw(); break;
        case WIN_INFO:  info_draw(); break;
        case WIN_SNAKE: snake_draw(); break;
        case WIN_TERM:  term_draw(); break;
    }
}

static void wm_draw_window(int id) {
    if (id < 1 || id > WIN_COUNT) return;
    if (!(win_flags[id] & WIN_FLAG_OPEN) || (win_flags[id] & WIN_FLAG_MIN)) return;
    wm_set_context(id);
    app_draw(id);
    wm_reset_context();
}

/* ------------------------------------------------------------------ */
/* Desktop composition                                                 */
/* ------------------------------------------------------------------ */
static void format_clock(char *out, int cap);

static void desktop_draw(void) {
    fill_rect(0, 0, DESK_W, DESK_H, COL_BG);
    fill_rect(0, 0, DESK_W, 28, COL_TOPBAR);
    draw_text(16, 6, "[ DimonOS-64 ]", COL_TEXT);
    text_fit(200, 6, "F1 Menu   1-7 Launch   Alt+Tab Switch", COL_DIM, 444);
    char clock[32];
    format_clock(clock, sizeof(clock));
    draw_text(680, 6, clock, COL_ACCENT);

    /* 7 desktop icons: col1 x=40, col2 x=170 */
    static const int ix[8] = {0, 40, 40, 40, 40, 40, 170, 170};
    static const int iy[8] = {0, 50, 120, 190, 260, 330, 50, 120};
    static const uint32_t ic[8] = {
        0, COL_TITLE, 0xFF059669u, 0xFFD97706u, 0xFFDB2777u,
        0xFF7C3AEDu, 0xFF0D9488u, 0xFF4F46E5u
    };
    static const char *const il[8] = {
        "", "Calculator", "Notepad", "Files", "Paint",
        "System Info", "Snake", "Terminal"
    };
    for (int i = 1; i <= 7; i++) {
        fill_rect(ix[i], iy[i], 110, 54, ic[i]);
        draw_text(ix[i] + 12, iy[i] + 18, il[i], COL_TEXT);
    }

    /* Taskbar */
    fill_rect(0, 568, DESK_W, 32, COL_PANEL);
    fill_rect(8, 572, 84, 24, COL_START);
    draw_text(18, 576, "[ START ]", COL_TEXT);
    taskbar_rebuild();
    for (int i = 0; i < taskbar_count; i++) {
        int id = taskbar_map[i];
        int bx = 100 + i * 78;
        uint32_t c = (id == active_window) ? COL_TITLE : COL_TASK_OFF;
        fill_rect(bx, 572, 74, 24, c);
        text_fit(bx + 6, 576, WIN_SHORT[id], COL_TEXT, 62);
    }

    /* Start menu popup */
    if (start_menu_open) {
        fill_rect(8, 250, 220, 314, COL_MENU);
        fill_rect(8, 250, 220, 28, COL_TITLE);
        draw_text(18, 256, "DimonOS Applications", COL_TEXT);
        static const char *const items[8] = {
            "1. Calculator", "2. Notepad", "3. File Explorer", "4. Modern Paint",
            "5. System Info", "6. Snake Game", "7. Terminal CLI", "X. Exit OS"
        };
        for (int i = 0; i < 8; i++) {
            fill_rect(12, 288 + i * 32, 212, 28, 0xFF0A0E17u);
            draw_text(22, 294 + i * 32, items[i], COL_TEXT);
        }
    }
}

static void format_clock(char *out, int cap) {
    /* YYYY-MM-DD HH:MM:SS from RTC; "RTC N/A" when unavailable. */
    int64_t t = 0;
    int ok = 0;
    if (g_vm->gui_poll_cb) {
        /* Use the same path as SYS_RTC_GET: baremetal_rtc_unix when linked. */
    }
    /* Time is filled by kernel via vm; read from RTC syscall result. */
    extern int64_t baremetal_rtc_unix(void);
    t = baremetal_rtc_unix();
    ok = (t >= 0);
    if (!ok) {
        int n = 0;
        const char *s = "RTC N/A";
        while (*s && n < cap - 1) out[n++] = *s++;
        out[n] = 0;
        return;
    }
    /* Convert Unix seconds to date/time (UTC). */
    int64_t days = t / 86400;
    int64_t rem = t % 86400;
    int hour = (int)(rem / 3600), min = (int)((rem % 3600) / 60), sec = (int)(rem % 60);
    /* civil_from_days */
    int64_t z = days + 719468;
    int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    unsigned doe = (unsigned)(z - era * 146097);
    unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    int y = (int)yoe + (int)era * 400;
    unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    unsigned mp = (5 * doy + 2) / 153;
    unsigned d = doy - (153 * mp + 2) / 5 + 1;
    unsigned m = mp + (mp < 10 ? 3 : -9);
    if (m <= 2) y += 1;
    int n = 0;
    int parts[6] = {y, (int)m, (int)d, hour, min, sec};
    int widths[6] = {4, 2, 2, 2, 2, 2};
    for (int p = 0; p < 6; p++) {
        if (p == 3 && n < cap - 1) out[n++] = ' ';
        else if (p > 0 && p < 3 && n < cap - 1) out[n++] = '-';
        else if (p > 3 && n < cap - 1) out[n++] = ':';
        int v = parts[p];
        char tmp[8];
        num_to_dec(v, tmp);
        int tl = str_len(tmp);
        if (p != 0) {
            for (int k = tl; k < widths[p] && n < cap - 1; k++) out[n++] = '0';
        }
        for (int k = 0; k < tl && n < cap - 1; k++) out[n++] = tmp[k];
    }
    out[n] = 0;
}

static void os_repaint(void) {
    desktop_draw();
    for (int i = win_z_count - 1; i >= 0; i--) wm_draw_window(win_z_order[i]);
    if (dlg_active) {
        wm_reset_context();
        /* dialogs drawn in screen space below */
    }
}

/* ------------------------------------------------------------------ */
/* Dialog system                                                       */
/* ------------------------------------------------------------------ */
/* Strict UTF-8 validation (RFC 3629).
 * Returns 1 if [buf, buf+len) is well-formed UTF-8, 0 otherwise.
 * Rejects: stray continuations, overlongs, UTF-16 surrogates (U+D800..DFFF),
 * code points above U+10FFFF, and sequences truncated at the buffer end. */
static int utf8_valid(const uint8_t *buf, int len) {
    if (!buf || len < 0) return 0;
    int i = 0;
    while (i < len) {
        uint8_t c = buf[i];
        if (c < 0x80) { i++; continue; }              /* ASCII */

        int need;                                       /* continuation count */
        if (c >= 0xC2 && c <= 0xDF) need = 1;           /* 2-byte: U+0080..U+07FF */
        else if (c >= 0xE0 && c <= 0xEF) need = 2;      /* 3-byte: U+0800..U+FFFF */
        else if (c >= 0xF0 && c <= 0xF4) need = 3;      /* 4-byte: U+10000..U+10FFFF */
        else return 0;                                  /* 0x80-0xC1 or 0xF5-0xFF */

        /* Bounds: the full sequence must fit in the buffer.
         * Continuations occupy i+1 .. i+need, so i+need < len. */
        if (i + need >= len) return 0;

        /* Continuation bytes must be 0x80..0xBF. */
        for (int k = 1; k <= need; k++) {
            uint8_t cc = buf[i + k];
            if ((cc & 0xC0) != 0x80) return 0;
        }

        /* Overlong / surrogate / range checks on the second byte. */
        uint8_t c2 = buf[i + 1];
        if (c == 0xE0 && c2 < 0xA0) return 0;           /* overlong 3-byte */
        if (c == 0xED && c2 >= 0xA0) return 0;          /* UTF-16 surrogate U+D800..DFFF */
        if (c == 0xF0 && c2 < 0x90) return 0;           /* overlong 4-byte */
        if (c == 0xF4 && c2 >= 0x90) return 0;          /* > U+10FFFF */

        i += need + 1;
    }
    return 1;
}

static int fat_validate_shortname(const char *s) {
    if (!s || !*s) return 0;
    if (str_eq(s, ".") || str_eq(s, "..")) return 0;
    int base = 0, ext = 0, seen = 0, sl = str_len(s);
    for (int i = 0; i < sl; i++) {
        char c = s[i];
        if (c == '.') {
            if (seen || base == 0) return 0;
            seen = 1; continue;
        }
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '$' || c == '%' || c == '\'' ||
              c == '-' || c == '_' || c == '@' || c == '~' || c == '`' ||
              c == '!' || c == '(' || c == ')' || c == '{' || c == '}' ||
              c == '^' || c == '#')) return 0;
        if (!seen) { if (++base > 8) return 0; }
        else { if (++ext > 3) return 0; }
    }
    return base > 0 && (!seen || ext > 0);
}

static void dlg_cancel(void) {
    dlg_active = 0;
    dlg_pending = 0;
    dlg_err[0] = 0;
    flag_redraw();
}

/* Keep the caret inside the visible name field (field_w px, 8px glyphs). */
static void dlg_fix_scroll_name(int len, int caret, int *scroll, int field_w) {
    int vis = field_w / 8;
    if (vis <= 0) return;
    if (caret < 0) caret = 0;
    if (caret > len) caret = len;
    if (*scroll > caret) *scroll = caret;
    if (*scroll < caret - vis + 1) *scroll = caret - vis + 1;
    if (*scroll < 0) *scroll = 0;
    if (len - *scroll < vis && len > vis) *scroll = len - vis;
    if (*scroll < 0) *scroll = 0;
}

static void dlg_show_file(int mode) {
    dlg_active = 1;
    dlg_mode = mode;
    dlg_owner = WIN_NOTE;
    str_copy(dlg_path, "/"); dlg_path_len = 1;
    dlg_name[0] = 0; dlg_name_len = 0;
    dlg_name_caret = 0;
    dlg_name_scroll = 0;
    dlg_count = 0; dlg_scroll = 0; dlg_sel = 0;
    for (uint32_t i = 0; i < 64; i++) {
        Dimon64DirEnt ent;
        if (dimonfs_list((DimonDisk *)g_vm, dlg_path, i, &ent) != DFS_OK) break;
        str_copy(dlg_list_buf[dlg_count], ent.name);
        dlg_count++;
    }
    flag_redraw();
}

static void dlg_draw(void) {
    if (!dlg_active) return;
    int x, y, w, h;
    if (dlg_mode == 3 || dlg_mode == 4) { x = 200; y = 220; w = 400; h = 160; }
    else { x = 120; y = 80; w = 560; h = 420; }
    fill_rect(x, y, w, h, COL_PANEL);
    fill_rect(x, y, w, TITLEBAR_H, COL_TITLE);
    const char *title = (dlg_mode == 1) ? "Open File" :
                        (dlg_mode == 2) ? "Save As" :
                        (dlg_mode == 3) ? "Confirm" : "Overwrite?";
    draw_text(x + 12, y + 8, title, COL_TEXT);
    /* [X] */
    fill_rect(x + w - 28, y + 4, 24, 24, COL_RED_DK);
    draw_text(x + w - 22, y + 8, "X", COL_TEXT);

    if (dlg_mode == 1 || dlg_mode == 2) {
        draw_text(x + 20, y + 50, "Path:", COL_DIM);
        fill_rect(x + 76, y + 38, 472, 24, COL_ENTRY);
        text_fit(x + 80, y + 42, dlg_path, COL_PANEL, 460);
        draw_text(x + 20, y + 286, "Name:", COL_DIM);
        fill_rect(x + 76, y + 286, 330, 24, COL_ENTRY);
        /* Scroll the filename field so the caret stays visible (330px / 8px). */
        dlg_fix_scroll_name(dlg_name_len, dlg_name_caret, &dlg_name_scroll, 330);
        {
            const char *p = dlg_name + dlg_name_scroll;
            text_fit(x + 80, y + 290, p, COL_PANEL, 320);
            int cx = x + 80 + (dlg_name_caret - dlg_name_scroll) * 8;
            if (cx >= x + 80 && cx < x + 400) {
                fill_rect(cx, y + 290, 2, 16, COL_ACCENT);
            }
        }
        /* file list */
        fill_rect(x + 76, y + 70, 472, 200, 0xFF0A0E17u);
        int vis = 10;
        for (int i = 0; i < vis && dlg_scroll + i < dlg_count; i++) {
            int idx = dlg_scroll + i;
            int ry = y + 74 + i * 19;
            if (idx == dlg_sel) fill_rect(x + 80, ry, 464, 18, COL_TITLE);
            draw_text(x + 88, ry + 1, dlg_list_buf[idx], COL_TEXT);
        }
        fill_rect(x + 428, y + 286, 120, 24, COL_GREEN);
        draw_text(x + 448, y + 290, (dlg_mode == 2) ? "Save" : "Open", COL_TEXT);
        fill_rect(x + 428, y + 316, 120, 24, COL_BORDER);
        draw_text(x + 452, y + 320, "Cancel", COL_TEXT);
    } else {
        draw_text(x + 20, y + 50,
                  (dlg_mode == 3) ? "Save changes before closing?" : "File exists. Overwrite?",
                  COL_TEXT);
        if (dlg_err[0]) draw_text(x + 20, y + 74, dlg_err, COL_RED);
        fill_rect(x + 20, y + 100, 110, 28, COL_GREEN);
        draw_text(x + 42, y + 106, (dlg_mode == 3) ? "Save" : "Yes", COL_TEXT);
        fill_rect(x + 140, y + 100, 110, 28, COL_TITLE);
        draw_text(x + 156, y + 106, (dlg_mode == 3) ? "Discard" : "No", COL_TEXT);
        fill_rect(x + 260, y + 100, 110, 28, COL_BORDER);
        draw_text(x + 292, y + 106, "Cancel", COL_TEXT);
    }
}

/* ------------------------------------------------------------------ */
/* Input dispatch                                                      */
/* ------------------------------------------------------------------ */
static void note_request_close(void);
static void note_do_save(void);
static void note_discard(void);
static int napp_load_file(const char *path);

static void dlg_on_click(int x, int y) {
    int fx, fy, fw;
    if (dlg_mode == 3 || dlg_mode == 4) { fx = 200; fy = 220; fw = 400; }
    else { fx = 120; fy = 80; fw = 560; }
    /* Title-bar [X] */
    if (x >= fx + fw - 28 && x < fx + fw - 4 && y >= fy + 4 && y < fy + 28) {
        dlg_cancel();
        return;
    }
    if (dlg_mode == 3 || dlg_mode == 4) {
        if (y >= fy + 100 && y < fy + 128) {
            if (x >= fx + 20 && x < fx + 130) {
                if (dlg_mode == 3) {
                    note_do_save();
                    if (dlg_pending == 1) window_close();
                    else if (dlg_pending == 2) { note_discard(); window_close(); }
                } else {
                    /* overwrite yes */
                    note_do_save();
                }
                dlg_cancel();
            } else if (x >= fx + 140 && x < fx + 250) {
                if (dlg_mode == 3) { note_discard(); window_close(); }
                dlg_cancel();
            } else if (x >= fx + 260 && x < fx + 370) {
                dlg_cancel();
            }
        }
        return;
    }
    /* Open / SaveAs */
    if (y >= fy + 286 && y < fy + 310 && x >= fx + 76 && x < fx + 406) {
        dlg_focus = 2;
        /* Place caret from click position, accounting for scroll. */
        {
            int rel = (x - (fx + 80)) / 8 + dlg_name_scroll;
            if (rel < 0) rel = 0;
            if (rel > dlg_name_len) rel = dlg_name_len;
            dlg_name_caret = rel;
            dlg_fix_scroll_name(dlg_name_len, dlg_name_caret, &dlg_name_scroll, 330);
        }
        flag_redraw();
        return;
    }
    if (y >= fy + 38 && y < fy + 62 && x >= fx + 76) { dlg_focus = 1; return; }
    if (y >= fy + 70 && y < fy + 270 && x >= fx + 76) {
        int row = (y - (fy + 74)) / 19;
        int idx = dlg_scroll + row;
        if (row >= 0 && row < 10 && idx < dlg_count) {
            dlg_sel = idx;
            str_copy(dlg_name, dlg_list_buf[idx]);
            dlg_name_len = str_len(dlg_name);
            dlg_name_caret = dlg_name_len;
            dlg_name_scroll = 0;
            flag_redraw();
        }
        return;
    }
    if (y >= fy + 286 && y < fy + 310 && x >= fx + 428 && x < fx + 548) {
        /* Open / Save primary */
        if (!fat_validate_shortname(dlg_name)) {
            str_copy(dlg_err, "Invalid 8.3 name");
            flag_redraw();
            return;
        }
        char full[300];
        str_copy(full, dlg_path);
        if (full[str_len(full) - 1] != '/') str_append(full, "/", sizeof(full));
        str_append(full, dlg_name, sizeof(full));
        if (dlg_mode == 2) {
            Dimon64DirEnt ent;
            if (dimonfs_stat((DimonDisk *)g_vm, full, &ent) == DFS_OK) {
                dlg_mode = 4;
                dlg_pending = 2;
                flag_redraw();
                return;
            }
        }
        str_copy(note_path, full);
        if (dlg_mode == 1) {
            uint32_t sz = 0;
            note_len = 0; note_cursor = 0;
            int rc = dimonfs_read((DimonDisk *)g_vm, full, 0, note_buf, NOTE_BUF_MAX - 1, &sz);
            if (rc >= 0) {
                note_len = (int)sz;
                if (note_len > NOTE_BUF_MAX - 1) note_len = NOTE_BUF_MAX - 1;
                note_buf[note_len] = 0;
                if (!utf8_valid((const uint8_t *)note_buf, note_len)) {
                    note_len = 0; note_cursor = 0; note_buf[0] = 0;
                    str_copy(note_status, "FAT16: Bad encoding");
                } else {
                    note_cursor = note_len;
                    note_dirty = 0;
                    str_copy(note_status, "FAT16: Loaded from Disk!");
                }
            } else {
                str_copy(note_status, "FAT16: Open error");
            }
        } else {
            note_do_save();
        }
        dlg_cancel();
        return;
    }
    if (y >= fy + 316 && y < fy + 340 && x >= fx + 428 && x < fx + 548) {
        dlg_cancel();
        return;
    }
}

/* ------------------------------------------------------------------ */
/* Calculator                                                          */
/* ------------------------------------------------------------------ */
static void calc_draw(void) {
    draw_window_frame(WIN_CALC, 240, 70, 320, 440, WIN_TITLE[WIN_CALC]);
    /* Display screen (X=256, Y=116, W=288, H=64) */
    fill_rect(256, 116, 288, 64, 0xFF0B0F19u);
    fill_rect(256, 116, 288, 1, COL_TASK_OFF);
    draw_text(268, 138, calc_buf, COL_ACCENT);
    /* 16 buttons (4 rows x 4 cols), body 66x48, stride 74 x 58 */
    static const char *const keys[4][4] = {
        {"7", "8", "9", "/"},
        {"4", "5", "6", "*"},
        {"1", "2", "3", "-"},
        {"C", "0", "=", "+"}
    };
    for (int r = 0; r < 4; r++) {
        for (int c = 0; c < 4; c++) {
            int bx = 256 + c * 74, by = 196 + r * 58;
            uint32_t col = (c == 3) ? COL_TITLE : COL_TASK_OFF;
            if (r == 3 && c == 2) col = COL_GREEN;
            fill_rect(bx, by, 66, 48, col);
            draw_text(bx + 28, by + 16, keys[r][c], COL_TEXT);
        }
    }
}

static void calc_update_buf(void) {
    num_to_dec(calc_cur, calc_buf);
    flag_redraw();
}

static void calc_input_digit(int d) {
    if (calc_cur >= 1000000000000000LL) return;
    calc_cur = calc_cur * 10 + d;
    calc_update_buf();
}

static void calc_on_key(int key) {
    if (key >= '0' && key <= '9') { calc_input_digit(key - '0'); return; }
    if (key == '+') { calc_acc = calc_cur; calc_cur = 0; calc_op = 1; flag_redraw(); return; }
    if (key == '-') { calc_acc = calc_cur; calc_cur = 0; calc_op = 2; flag_redraw(); return; }
    if (key == '*') { calc_acc = calc_cur; calc_cur = 0; calc_op = 3; flag_redraw(); return; }
    if (key == '/') { calc_acc = calc_cur; calc_cur = 0; calc_op = 4; flag_redraw(); return; }
    if (key == '=' || key == 13) {
        if (!calc_op) return;
        int64_t a = calc_acc, b = calc_cur;
        if (calc_op == 1) calc_cur = a + b;
        else if (calc_op == 2) calc_cur = a - b;
        else if (calc_op == 3) calc_cur = a * b;
        else if (calc_op == 4) {
            if (b == 0) calc_cur = 0; /* division by zero guard */
            else calc_cur = a / b;
        }
        calc_op = 0;
        calc_update_buf();
        return;
    }
    if (key == 8) { calc_cur = calc_cur / 10; calc_update_buf(); return; }
    if (key == 'c' || key == 'C') {
        calc_acc = calc_cur = 0; calc_op = 0;
        str_copy(calc_buf, "0");
        flag_redraw();
    }
}

static void calc_on_click(int x, int y) {
    /* Historical coords: buttons at X=256..546, Y=196..420 */
    if (y < 196 || y >= 420) return;
    if (x < 256 || x >= 546) return;
    int c, r;
    if (x < 324) c = 0;
    else if (x < 398) c = 1;
    else if (x < 472) c = 2;
    else c = 3;
    if (y < 246) r = 0;
    else if (y < 304) r = 1;
    else if (y < 362) r = 2;
    else r = 3;
    static const int codes[4][4] = {
        {'7', '8', '9', '/'},
        {'4', '5', '6', '*'},
        {'1', '2', '3', '-'},
        {'c', '0', '=', '+'}
    };
    calc_on_key(codes[r][c]);
}

/* ------------------------------------------------------------------ */
/* Notepad                                                             */
/* ------------------------------------------------------------------ */
static void note_draw(void) {
    draw_window_frame(WIN_NOTE, 80, 45, 640, 500, note_title[0] ? note_title : WIN_TITLE[WIN_NOTE]);
    /* toolbar */
    fill_rect(80, 77, 640, 36, COL_PANEL2);
    static const char *const btns[5] = {"New", "Open", "Save", "SaveAs", "Help"};
    for (int i = 0; i < 5; i++) {
        fill_rect(92 + i * 95, 82, 88, 26, COL_TASK_OFF);
        text_fit(100 + i * 95, 87, btns[i], COL_TEXT, 72);
    }
    /* editor */
    fill_rect(95, 125, 610, 400, COL_PANEL);
    int x = 105, y = 135;
    int cur_col = x, cur_row = y;
    for (int i = 0; i < note_len; ) {
        if (i == note_cursor) { cur_col = x; cur_row = y; }
        unsigned char ch = (unsigned char)note_buf[i];
        if (ch == '\n') { x = 105; y += 18; i++; if (y > 510) break; continue; }
        char one[8];
        int n = (int)u8len((const unsigned char *)&note_buf[i]);
        for (int k = 0; k < n && k < 7; k++) one[k] = note_buf[i + k];
        one[n] = 0;
        draw_text(x, y, one, COL_ENTRY);
        x += 8;
        i += n;
        if (x > 680) { x = 105; y += 18; }
        if (y > 510) break;
    }
    if (note_cursor >= note_len) { cur_col = x; cur_row = y; }
    fill_rect(cur_col, cur_row, 2, 16, COL_ACCENT);
    if (note_status[0]) draw_text(100, 530, note_status, COL_DIM);
    if (note_dirty) draw_text(400, 530, "*", COL_RED);
}

static void note_on_key(int key, int mod) {
    if (mod & KEYMOD_CTRL) {
        if (key == 's' || key == 'S') { note_do_save(); return; }
        if (key == 'o' || key == 'O') {
            dlg_show_file(1);
            return;
        }
        if (key == 'n' || key == 'N') {
            note_len = 0; note_cursor = 0; note_dirty = 0;
            note_buf[0] = 0; note_path[0] = 0;
            str_copy(note_status, "FAT16: New document");
            flag_redraw();
            return;
        }
    }
    if (key == 261) { note_do_save(); return; } /* F2 */
    if (key == 262) { /* F3 Save As */
        dlg_show_file(2);
        return;
    }
    if (key == 268) { note_do_save(); return; } /* F9 */
    if (key == 269) { /* F10 Open */
        note_on_key('o', KEYMOD_CTRL);
        return;
    }
    if (key == 13 || key == 10) key = 10;
    else if (key == KEY_LEFT) {
        if (note_cursor > 0) {
            int p = note_cursor - 1;
            while (p > 0 && ((unsigned char)note_buf[p] & 0xC0) == 0x80) p--;
            note_cursor = p;
            flag_redraw();
        }
        return;
    } else if (key == KEY_RIGHT) {
        if (note_cursor < note_len) {
            note_cursor += (int)u8len((const unsigned char *)&note_buf[note_cursor]);
            flag_redraw();
        }
        return;
    } else if (key == KEY_HOME) { note_cursor = 0; flag_redraw(); return; }
    else if (key == KEY_END) { note_cursor = note_len; flag_redraw(); return; }
    else if (key == 8) { /* backspace */
        if (note_cursor > 0) {
            int p = note_cursor - 1;
            while (p > 0 && ((unsigned char)note_buf[p] & 0xC0) == 0x80) p--;
            int n = note_cursor - p;
            for (int i = note_cursor; i < note_len; i++) note_buf[i - n] = note_buf[i];
            note_len -= n;
            note_cursor = p;
            note_dirty = 1;
            flag_redraw();
        }
        return;
    } else if (key == KEY_DELETE) { /* delete */
        if (note_cursor < note_len) {
            int n = (int)u8len((const unsigned char *)&note_buf[note_cursor]);
            for (int i = note_cursor + n; i < note_len; i++) note_buf[i - n] = note_buf[i];
            note_len -= n;
            note_dirty = 1;
            flag_redraw();
        }
        return;
    } else if (key < 32 || key > 255) return;

    /* Insert one UTF-8 byte sequence (single-byte keys here). */
    if (note_len >= NOTE_BUF_MAX - 1) return;
    for (int i = note_len; i > note_cursor; i--) note_buf[i] = note_buf[i - 1];
    note_buf[note_cursor++] = (char)key;
    note_len++;
    note_dirty = 1;
    flag_redraw();
}

static void note_on_click(int x, int y) {
    if (y >= 82 && y < 108) {
        if (x >= 92 && x < 180) {
            note_on_key('n', KEYMOD_CTRL);
        } else if (x >= 187 && x < 275) {
            note_on_key('o', KEYMOD_CTRL);
        } else if (x >= 282 && x < 370) {
            note_do_save();
        } else if (x >= 377 && x < 465) {
            note_on_key(262, 0);
        }
        return;
    }
    if (y >= 125 && y < 525 && x >= 95 && x < 705) {
        int row = (y - 135) / 18;
        int col = (x - 105) / 8;
        int i = 0, r = 0, c = 0, pos = 0;
        while (i < note_len) {
            if (r == row && c == col) { pos = i; break; }
            if (note_buf[i] == '\n') { r++; c = 0; }
            else c++;
            i++;
            pos = i;
        }
        note_cursor = pos;
        flag_redraw();
    }
}

static void note_do_save(void) {
    if (!note_path[0]) {
        str_copy(note_path, "/NOTES.TXT");
    }
    int rc = dimonfs_write((DimonDisk *)g_vm, note_path, note_buf, (uint32_t)note_len, 1, 1);
    if (rc == DFS_OK) {
        note_dirty = 0;
        str_copy(note_status, "FAT16: Saved to Disk!");
    } else {
        str_copy(note_status, "FAT16: Save error");
    }
    flag_redraw();
}

static void note_discard(void) {
    note_dirty = 0;
    flag_redraw();
}

static void note_request_close(void) {
    if (note_dirty) {
        dlg_active = 1; dlg_mode = 3; dlg_owner = WIN_NOTE;
        dlg_pending = 1;
        flag_redraw();
    } else {
        window_close();
    }
}

/* ------------------------------------------------------------------ */
/* File Explorer                                                       */
/* ------------------------------------------------------------------ */
static void files_refresh(void) {
    fm_count = 0;
    for (uint32_t i = 0; i < 256; i++) {
        Dimon64DirEnt ent;
        if (dimonfs_list((DimonDisk *)g_vm, fm_cwd, i, &ent) != DFS_OK) break;
        str_copy(fm_list[fm_count], ent.name);
        fm_isdir[fm_count] = (ent.attributes & 0x10) ? 1 : 0;
        fm_size[fm_count] = ent.size;
        fm_count++;
    }
    if (fm_sel >= fm_count) fm_sel = fm_count > 0 ? fm_count - 1 : 0;
}

static void files_draw(void) {
    draw_window_frame(WIN_FILES, 100, 60, 600, 460, WIN_TITLE[WIN_FILES]);
    fill_rect(100, 92, 600, 36, COL_PANEL2);
    fill_rect(112, 98, 80, 24, COL_TASK_OFF);
    draw_text(128, 102, "Up", COL_TEXT);
    fill_rect(200, 98, 380, 24, COL_ENTRY);
    text_fit(208, 102, fm_cwd, COL_PANEL, 360);
    /* list header */
    fill_rect(112, 140, 576, 20, COL_TASK_OFF);
    draw_text(120, 142, "FILENAME", COL_DIM);
    draw_text(320, 142, "SIZE", COL_DIM);
    draw_text(440, 142, "TYPE", COL_DIM);
    fill_rect(112, 160, 576, 280, 0xFF0A0E17u);
    int vis = 14;
    for (int i = 0; i < vis && fm_scroll + i < fm_count; i++) {
        int idx = fm_scroll + i;
        int ry = 164 + i * 19;
        if (idx == fm_sel) fill_rect(116, ry, 568, 18, COL_TITLE);
        draw_text(124, ry + 1, fm_list[idx], COL_TEXT);
        char sz[32];
        if (fm_isdir[idx]) str_copy(sz, "<DIR>");
        else fmt_size(fm_size[idx], sz);
        draw_text(320, ry + 1, sz, COL_DIM);
        draw_text(440, ry + 1, fm_isdir[idx] ? "Dir" : "File", COL_DIM);
    }
    fill_rect(112, 452, 576, 24, COL_PANEL2);
    char st[64];
    num_to_dec(fm_count, st);
    str_append(st, " items", sizeof(st));
    draw_text(120, 456, st, COL_DIM);
}

static void files_on_key(int key) {
    if (key == KEY_UP) {
        if (fm_sel > 0) { fm_sel--; if (fm_sel < fm_scroll) fm_scroll = fm_sel; flag_redraw(); }
    } else if (key == KEY_DOWN) {
        if (fm_sel < fm_count - 1) {
            fm_sel++;
            if (fm_sel >= fm_scroll + 14) fm_scroll = fm_sel - 13;
            flag_redraw();
        }
    } else if (key == 13) {
        if (fm_sel < fm_count && fm_isdir[fm_sel]) {
            char next[300];
            if (str_eq(fm_list[fm_sel], "..")) {
                int n = str_len(fm_cwd);
                while (n > 1 && fm_cwd[n - 1] != '/') n--;
                if (n > 1) n--;
                fm_cwd[n] = 0;
                if (n == 0) { fm_cwd[0] = '/'; fm_cwd[1] = 0; }
            } else {
                str_copy(next, fm_cwd);
                if (next[str_len(next) - 1] != '/') str_append(next, "/", sizeof(next));
                str_append(next, fm_list[fm_sel], sizeof(next));
                str_copy(fm_cwd, next);
            }
            fm_sel = 0; fm_scroll = 0;
            files_refresh();
            flag_redraw();
        }
    }
}

static void files_on_click(int x, int y) {
    if (y >= 98 && y < 122 && x >= 112 && x < 192) {
        /* Up: navigate to parent directory */
        int n = str_len(fm_cwd);
        if (n > 1) {
            while (n > 1 && fm_cwd[n - 1] != '/') n--;
            if (n > 1) n--;
            fm_cwd[n] = 0;
            if (!fm_cwd[0]) { fm_cwd[0] = '/'; fm_cwd[1] = 0; }
            fm_sel = 0; fm_scroll = 0;
            files_refresh();
            flag_redraw();
        }
        return;
    }
    if (y >= 160 && y < 440) {
        int row = (y - 164) / 19;
        int idx = fm_scroll + row;
        if (row >= 0 && row < 14 && idx < fm_count) {
            uint32_t now = 0;
            if (idx == fm_last_row && (now - fm_last_ms) < 400) {
                /* double click */
                fm_last_row = -1;
                if (fm_isdir[idx]) {
                    files_on_key(13);
                    fm_sel = idx;
                    files_on_key(13);
                } else {
                    /* open .TXT in notepad, .APP launch */
                    const char *nm = fm_list[idx];
                    int l = str_len(nm);
                    if (l > 4) {
                        const char *ext = nm + l - 4;
                        if ((ext[0] == '.' ) &&
                            (ext[1] == 't' || ext[1] == 'T') &&
                            (ext[2] == 'x' || ext[2] == 'X') &&
                            (ext[3] == 't' || ext[3] == 'T')) {
                            char full[300];
                            str_copy(full, fm_cwd);
                            if (full[str_len(full) - 1] != '/') str_append(full, "/", sizeof(full));
                            str_append(full, nm, sizeof(full));
                            window_open(WIN_NOTE);
                            str_copy(note_path, full);
                            uint32_t sz = 0;
                            note_len = 0;
                            dimonfs_read((DimonDisk *)g_vm, full, 0, note_buf, NOTE_BUF_MAX - 1, &sz);
                            note_len = (int)sz;
                            if (note_len > NOTE_BUF_MAX - 1) note_len = NOTE_BUF_MAX - 1;
                            note_buf[note_len] = 0;
                            if (!utf8_valid((const uint8_t *)note_buf, note_len)) {
                                note_len = 0; note_cursor = 0; note_buf[0] = 0;
                                str_copy(note_status, "FAT16: Bad encoding");
                            } else {
                                note_cursor = note_len;
                                note_dirty = 0;
                            }
                            flag_redraw();
                        } else if (ext[1] == 'a' || ext[1] == 'A') {
                            char full[300];
                            str_copy(full, fm_cwd);
                            if (full[str_len(full) - 1] != '/') str_append(full, "/", sizeof(full));
                            str_append(full, nm, sizeof(full));
                            if (napp_load_file(full) == 0) {
                                str_copy(term_out, "APP loaded");
                            }
                            flag_redraw();
                        }
                    }
                }
                return;
            }
            fm_last_row = idx;
            fm_last_ms = now;
            fm_sel = idx;
            flag_redraw();
        }
    }
}

/* ------------------------------------------------------------------ */
/* Paint                                                               */
/* ------------------------------------------------------------------ */
static void paint_clear_canvas(void) {
    for (int i = 0; i < PAINT_W * PAINT_H; i++) paint_canvas[i] = 0xFFFFFFFF;
}

static void paint_draw(void) {
    draw_window_frame(WIN_PAINT, 60, 35, 680, 520, WIN_TITLE[WIN_PAINT]);
    fill_rect(60, 67, 680, 48, COL_PANEL2);
    /* 9 swatches */
    static const uint32_t sw[9] = {
        0xFF000000u, 0xFFFFFFFFu, 0xFFFF3B30u, 0xFF34C759u, 0xFF007AFFu,
        0xFFFFCC00u, 0xFFFF9500u, 0xFFAF52DEu, 0xFF5AC8FAu
    };
    for (int i = 0; i < 9; i++) fill_rect(75 + i * 30, 79, 24, 24, sw[i]);
    /* brushes — active tool highlighted */
    {
        uint32_t b1 = (paint_radius == 1 && !paint_eraser) ? COL_TITLE : COL_TASK_OFF;
        uint32_t b3 = (paint_radius == 3 && !paint_eraser) ? COL_TITLE : COL_TASK_OFF;
        uint32_t b5 = (paint_radius == 5 && !paint_eraser) ? COL_TITLE : COL_TASK_OFF;
        uint32_t er = paint_eraser ? COL_TITLE : COL_BORDER;
        fill_rect(355, 79, 40, 24, b1); draw_text(363, 83, "1px", COL_TEXT);
        fill_rect(400, 79, 40, 24, b3); draw_text(406, 83, "3px", COL_TEXT);
        fill_rect(445, 79, 40, 24, b5); draw_text(451, 83, "5px", COL_TEXT);
        fill_rect(495, 79, 68, 24, er); draw_text(503, 83, "Eraser", COL_TEXT);
    }
    fill_rect(570, 79, 60, 24, COL_RED_DK);   draw_text(580, 83, "Clear", COL_TEXT);
    blit_canvas(80, 125, PAINT_W, PAINT_H, paint_canvas);
}

static void paint_apply_brush(int x, int y) {
    /* Historical coords: canvas at (80,125) */
    int cx = x - 80, cy = y - 125;
    uint32_t col = paint_eraser ? 0xFFFFFFFF : paint_color;
    int rad = paint_radius;
    for (int dy = -rad; dy <= rad; dy++) {
        for (int dx = -rad; dx <= rad; dx++) {
            int px = cx + dx, py = cy + dy;
            if (px < 0 || px >= PAINT_W || py < 0 || py >= PAINT_H) continue;
            paint_canvas[py * PAINT_W + px] = col;
        }
    }
    flag_redraw();
}

static void paint_on_key(int key) {
    if (key == 'c' || key == 'C') {
        paint_clear_canvas();
        flag_redraw();
    }
}

static void paint_on_click(int x, int y, int btn) {
    (void)btn;
    if (y >= 79 && y < 103) {
        for (int i = 0; i < 9; i++) {
            if (x >= 75 + i * 30 && x < 99 + i * 30) {
                static const uint32_t sw[9] = {
                    0xFF000000u, 0xFFFFFFFFu, 0xFFFF3B30u, 0xFF34C759u, 0xFF007AFFu,
                    0xFFFFCC00u, 0xFFFF9500u, 0xFFAF52DEu, 0xFF5AC8FAu
                };
                paint_color = sw[i];
                paint_eraser = 0;
                flag_redraw();
                return;
            }
        }
        if (x >= 355 && x < 395) { paint_radius = 1; return; }
        if (x >= 400 && x < 440) { paint_radius = 3; return; }
        if (x >= 445 && x < 485) { paint_radius = 5; return; }
        if (x >= 495 && x < 563) { paint_eraser = 1; flag_redraw(); return; }
        if (x >= 570 && x < 630) { paint_clear_canvas(); flag_redraw(); return; }
        return;
    }
    if (x >= 80 && x < 720 && y >= 125 && y < 525) {
        paint_apply_brush(x, y);
    }
}

static void paint_on_drag(int x, int y) {
    if (x >= 80 && x < 720 && y >= 125 && y < 525) paint_apply_brush(x, y);
}

/* ------------------------------------------------------------------ */
/* System Info                                                         */
/* ------------------------------------------------------------------ */
static void info_draw(void) {
    draw_window_frame(WIN_INFO, 120, 70, 560, 440, WIN_TITLE[WIN_INFO]);
    draw_text(150, 120, "CPU: DimonVirtualCPU-64 (RV64)", COL_ACCENT);
    draw_text(150, 150, "RAM: 64 MB continuous guest pool", COL_GREEN);
    draw_text(150, 180, "GPU: 800x600 TrueColor LFB", COL_ACCENT);
    draw_text(150, 210, "FS:  FAT16 8.3 via dimonfs", COL_GREEN);
    char line[80];
    /* storage mode */
    if (g_vm->disk_writable) draw_text(150, 240, "Storage: WRITABLE disk", COL_GREEN);
    else if (g_vm->disk_data) draw_text(150, 240, "Storage: READ-ONLY", COL_RED);
    else draw_text(150, 240, "Storage: NO DISK", COL_RED);
    /* ticks */
    extern uint32_t kernel_get_ticks_ms(void);
    num_to_dec((int64_t)kernel_get_ticks_ms(), line);
    str_append(line, " ms uptime", sizeof(line));
    draw_text(150, 270, line, COL_ACCENT);
    /* memory bar */
    draw_text(150, 300, "Memory usage:", COL_DIM);
    fill_rect(150, 330, 400, 20, COL_TASK_OFF);
    fill_rect(150, 330, 300, 20, COL_GREEN); /* ~75% visual */
    draw_text(150, 370, "DimonOS-64 Bare-Metal Native C Desktop", COL_TEXT);
}

/* ------------------------------------------------------------------ */
/* Snake                                                               */
/* ------------------------------------------------------------------ */
static void snake_draw(void) {
    draw_window_frame(WIN_SNAKE, 120, 50, 560, 490, WIN_TITLE[WIN_SNAKE]);
    /* grid 30x20 cells of 14px starting at (140, 100) */
    int gx = 140, gy = 100, cs = 14;
    fill_rect(gx - 4, gy - 4, SNAKE_W * cs + 8, SNAKE_H * cs + 8, COL_PANEL);
    for (int i = 0; i < snake_len; i++) {
        uint32_t c = (i == 0) ? COL_GREEN : COL_START_DK;
        fill_rect(gx + snake_x[i] * cs, gy + snake_y[i] * cs, cs - 1, cs - 1, c);
    }
    fill_rect(gx + food_x * cs, gy + food_y * cs, cs - 1, cs - 1, COL_RED);
    char sc[32];
    num_to_dec(snake_score, sc);
    str_append(sc, "  Score", sizeof(sc));
    draw_text(140, 84, sc, COL_TEXT);
    draw_text(300, 84, "Arrows move | R restart", COL_DIM);
    if (snake_gameover) {
        fill_rect(220, 250, 240, 80, COL_PANEL);
        fill_rect(220, 250, 240, 28, COL_RED_DK);
        draw_text(280, 256, "Game Over", COL_TEXT);
        draw_text(250, 290, "Press R to restart", COL_TEXT);
    }
}

static void snake_on_key(int key) {
    if (key == 'r' || key == 'R') {
        snake_len = 4; snake_dir = 1; snake_score = 0; snake_gameover = 0;
        for (int i = 0; i < snake_len; i++) { snake_x[i] = 10 - i; snake_y[i] = 10; }
        food_x = 20; food_y = 10;
        flag_redraw();
        return;
    }
    if (snake_gameover) return;
    if (key == KEY_UP && snake_dir != 2) snake_dir = 0;
    else if (key == KEY_RIGHT && snake_dir != 3) snake_dir = 1;
    else if (key == KEY_DOWN && snake_dir != 0) snake_dir = 2;
    else if (key == KEY_LEFT && snake_dir != 1) snake_dir = 3;
}

static void snake_on_timer(void) {
    if (snake_gameover) return;
    snake_tick++;
    if (snake_tick < 3) return; /* ~150 ms at 50 ms timer */
    snake_tick = 0;
    int nx = snake_x[0], ny = snake_y[0];
    if (snake_dir == 0) ny--;
    else if (snake_dir == 1) nx++;
    else if (snake_dir == 2) ny++;
    else nx--;
    if (nx < 0 || nx >= SNAKE_W || ny < 0 || ny >= SNAKE_H) {
        snake_gameover = 1;
        beep(200, 200);
        flag_redraw();
        return;
    }
    for (int i = 0; i < snake_len; i++) {
        if (snake_x[i] == nx && snake_y[i] == ny) {
            snake_gameover = 1;
            beep(200, 200);
            flag_redraw();
            return;
        }
    }
    for (int i = snake_len; i > 0; i--) {
        snake_x[i] = snake_x[i - 1];
        snake_y[i] = snake_y[i - 1];
    }
    snake_x[0] = nx; snake_y[0] = ny;
    if (nx == food_x && ny == food_y) {
        snake_len++;
        if (snake_len > SNAKE_MAX) snake_len = SNAKE_MAX;
        snake_score += 10;
        beep(880, 30);
        food_x = (int)((uint32_t)(nx * 7 + snake_score) % SNAKE_W);
        food_y = (int)((uint32_t)(ny * 13 + snake_score) % SNAKE_H);
    }
    flag_redraw();
}

/* ------------------------------------------------------------------ */
/* Terminal                                                            */
/* ------------------------------------------------------------------ */
static void term_draw(void) {
    draw_window_frame(WIN_TERM, 100, 60, 600, 460, WIN_TITLE[WIN_TERM]);
    fill_rect(112, 100, 576, 320, 0xFF0A0E17u);
    draw_text(124, 112, "DimonOS Terminal CLI", COL_ACCENT);
    draw_text(124, 132, term_out, COL_TEXT);
    fill_rect(112, 430, 576, 28, COL_PANEL2);
    draw_text(124, 436, "> ", COL_START);
    draw_text(148, 436, term_buf, COL_TEXT);
    draw_text(148 + term_len * 8, 436, "_", COL_TEXT);
}

static void term_on_key(int key) {
    if (key == 13) {
        /* execute */
        char cmd[64];
        str_copy(cmd, term_buf);
        if (str_eq(cmd, "help")) {
            str_copy(term_out, "help info clear exit pwd ls run");
        } else if (str_eq(cmd, "info")) {
            str_copy(term_out, "DimonOS-64 Bare-Metal Native C");
        } else if (str_eq(cmd, "clear")) {
            term_out[0] = 0;
        } else if (str_eq(cmd, "exit")) {
            window_close();
            return;
        } else if (str_eq(cmd, "pwd")) {
            str_copy(term_out, "/");
        } else if (str_eq(cmd, "ls")) {
            term_out[0] = 0;
            for (uint32_t i = 0; i < 16; i++) {
                Dimon64DirEnt ent;
                if (dimonfs_list((DimonDisk *)g_vm, "/", i, &ent) != DFS_OK) break;
                if (term_out[0]) str_append(term_out, " ", sizeof(term_out));
                str_append(term_out, ent.name, sizeof(term_out));
            }
            if (!term_out[0]) str_copy(term_out, "(empty)");
        } else if (cmd[0]) {
            str_copy(term_out, "Unknown command");
        }
        term_len = 0;
        term_buf[0] = 0;
        flag_redraw();
        return;
    }
    if (key == 8) {
        if (term_len > 0) term_buf[--term_len] = 0;
        flag_redraw();
        return;
    }
    if (key >= 32 && key < 127 && term_len < 62) {
        term_buf[term_len++] = (char)key;
        term_buf[term_len] = 0;
        flag_redraw();
    }
}

static void term_on_click(int x, int y) {
    (void)x; (void)y;
}

/* ------------------------------------------------------------------ */
/* Event handling                                                      */
/* ------------------------------------------------------------------ */
static void handle_key(int key, int mod) {
    if (key == 27) { /* ESC */
        if (dlg_active) { dlg_cancel(); return; }
        if (start_menu_open) { start_menu_open = 0; flag_redraw(); return; }
        if (active_window == WIN_NOTE) { note_request_close(); return; }
        if (active_window) window_close();
        return;
    }
    if (key == 260) { /* F1 */
        start_menu_open = !start_menu_open;
        flag_redraw();
        return;
    }
    if ((mod & KEYMOD_ALT) && key == 9) { wm_alt_tab(); return; }
    if (dlg_active) {
        if (key == 8) {
            if (dlg_focus == 2 && dlg_name_caret > 0) {
                for (int i = dlg_name_caret; i < dlg_name_len; i++)
                    dlg_name[i - 1] = dlg_name[i];
                dlg_name_len--;
                dlg_name_caret--;
                dlg_name[dlg_name_len] = 0;
                dlg_fix_scroll_name(dlg_name_len, dlg_name_caret, &dlg_name_scroll, 330);
                flag_redraw();
            } else if (dlg_focus == 1 && dlg_path_len > 1) {
                dlg_path[--dlg_path_len] = 0;
                flag_redraw();
            }
            return;
        }
        if (key >= 32 && key < 127) {
            if (dlg_focus == 2 && dlg_name_len < 62) {
                for (int i = dlg_name_len; i > dlg_name_caret; i--)
                    dlg_name[i] = dlg_name[i - 1];
                dlg_name[dlg_name_caret++] = (char)key;
                dlg_name_len++;
                dlg_name[dlg_name_len] = 0;
                dlg_fix_scroll_name(dlg_name_len, dlg_name_caret, &dlg_name_scroll, 330);
                flag_redraw();
            } else if (dlg_focus == 1 && dlg_path_len < 250) {
                dlg_path[dlg_path_len++] = (char)key;
                dlg_path[dlg_path_len] = 0;
                flag_redraw();
            }
        }
        return;
    }
    if (start_menu_open) {
        if (key >= '1' && key <= '7') {
            start_menu_open = 0;
            window_open(key - '0');
            return;
        }
        if (key == 'x' || key == 'X') {
            start_menu_open = 0;
            exit_requested = 1;
            return;
        }
    }
    if (key >= '1' && key <= '7' && active_window != WIN_CALC) {
        /* only when calculator is not consuming digits */
        if (active_window != WIN_NOTE && active_window != WIN_TERM) {
            window_open(key - '0');
            return;
        }
    }
    if (active_window == WIN_CALC) calc_on_key(key);
    else if (active_window == WIN_NOTE) note_on_key(key, mod);
    else if (active_window == WIN_FILES) files_on_key(key);
    else if (active_window == WIN_PAINT) paint_on_key(key);
    else if (active_window == WIN_SNAKE) snake_on_key(key);
    else if (active_window == WIN_TERM) term_on_key(key);
}

static void handle_click(int x, int y, int btn) {
    /* Dialog first */
    if (dlg_active) { dlg_on_click(x, y); return; }

    /* Taskbar / start button */
    if (y >= 568) {
        if (x >= 8 && x < 92) {
            start_menu_open = !start_menu_open;
            flag_redraw();
            return;
        }
        taskbar_rebuild();
        for (int i = 0; i < taskbar_count; i++) {
            int bx = 100 + i * 78;
            if (x >= bx && x < bx + 74) {
                int id = taskbar_map[i];
                if (active_window == id) window_minimize();
                else {
                    win_flags[id] &= ~WIN_FLAG_MIN;
                    wm_raise(id);
                    flag_redraw();
                }
                return;
            }
        }
        return;
    }

    /* Start menu */
    if (start_menu_open) {
        if (x >= 8 && x < 228 && y >= 250 && y < 564) {
            int row = (y - 288) / 32;
            if (row >= 0 && row < 7) {
                start_menu_open = 0;
                window_open(row + 1);
            } else if (row == 7) {
                start_menu_open = 0;
                exit_requested = 1;
            }
            flag_redraw();
            return;
        }
        start_menu_open = 0;
        /* fall through to window hit test */
    }

    /* Desktop icons */
    if (x < 160 && y >= 50 && y < 384) {
        static const int iy[8] = {0, 50, 120, 190, 260, 330, 50, 120};
        static const int ix[8] = {0, 40, 40, 40, 40, 40, 170, 170};
        for (int i = 1; i <= 7; i++) {
            if (x >= ix[i] && x < ix[i] + 110 && y >= iy[i] && y < iy[i] + 54) {
                window_open(i);
                return;
            }
        }
    }

    /* Window Z-order hit test (topmost first) */
    for (int i = win_z_count - 1; i >= 0; i--) {
        int id = win_z_order[i];
        if (!(win_flags[id] & WIN_FLAG_OPEN) || (win_flags[id] & WIN_FLAG_MIN)) continue;
        int wx = win_x[id], wy = win_y[id], ww = win_w[id], wh = win_h[id];
        if (x < wx || x >= wx + ww || y < wy || y >= wy + wh) continue;

        wm_raise(id);

        /* Title bar controls */
        if (y < wy + TITLEBAR_H) {
            if (x >= wx + ww - 28 && x < wx + ww - 4) {
                if (id == WIN_NOTE) note_request_close();
                else window_close();
                return;
            }
            if (x >= wx + ww - 52 && x < wx + ww - 28) {
                window_minimize();
                return;
            }
            /* drag start */
            drag_active = 1;
            drag_window = id;
            drag_off_x = x - wx;
            drag_off_y = y - wy;
            flag_redraw();
            return;
        }

        /* Client area → historical coords */
        int hx = x - wx + win_default_x[id];
        int hy = y - wy + win_default_y[id];
        if (id == WIN_CALC) calc_on_click(hx, hy);
        else if (id == WIN_NOTE) note_on_click(hx, hy);
        else if (id == WIN_FILES) files_on_click(hx, hy);
        else if (id == WIN_PAINT) paint_on_click(hx, hy, btn);
        else if (id == WIN_TERM) term_on_click(hx, hy);
        return;
    }
}

static void handle_drag(int x, int y, int btn) {
    (void)btn;
    if (dlg_active) return;
    if (drag_active && drag_window) {
        int id = drag_window;
        int nx = x - drag_off_x;
        int ny = y - drag_off_y;
        if (nx < 0) nx = 0;
        if (ny < WORK_TOP) ny = WORK_TOP;
        if (ny > WORK_BOT - 20) ny = WORK_BOT - 20;
        if (nx + win_w[id] > DESK_W) nx = DESK_W - win_w[id];
        win_x[id] = nx;
        win_y[id] = ny;
        flag_redraw();
        return;
    }
    /* paint drag */
    for (int i = win_z_count - 1; i >= 0; i--) {
        int id = win_z_order[i];
        if (id != WIN_PAINT) continue;
        if (!(win_flags[id] & WIN_FLAG_OPEN) || (win_flags[id] & WIN_FLAG_MIN)) continue;
        int wx = win_x[id], wy = win_y[id];
        int hx = x - wx + win_default_x[id];
        int hy = y - wy + win_default_y[id];
        paint_on_drag(hx, hy);
        break;
    }
}

static void handle_release(int x, int y, int btn) {
    (void)x; (void)y; (void)btn;
    drag_active = 0;
}

/* ------------------------------------------------------------------ */
/* Main desktop loop                                                   */
/* ------------------------------------------------------------------ */

/* ------------------------------------------------------------------ */
/* Native NAPP64 application loader (FAT16 .APP)                       */
/* ------------------------------------------------------------------ */
static uint8_t g_app_region[256 * 1024] __attribute__((aligned(16)));
static int g_app_loaded;
static NAppMainFn g_app_main;
static NAppKeyFn g_app_key;
static NAppVoidFn g_app_timer, g_app_draw;

static void app_fill_rect(void *sys, int x, int y, int w, int h, uint32_t c) {
    vm_gui_fill_rect((VM *)sys, x, y, w, h, c);
}
static void app_draw_text(void *sys, int x, int y, const char *s, uint32_t fg) {
    vm_gui_draw_string((VM *)sys, x, y, s, fg, 0);
}
static void app_beep(void *sys, uint32_t freq, uint32_t ms) {
    VM *v = (VM *)sys;
    if (v && v->psg_play_cb) v->psg_play_cb(v->psg_userdata, freq, ms, 0, 160);
}
static uint32_t app_ticks(void) {
    extern uint32_t kernel_get_ticks_ms(void);
    return kernel_get_ticks_ms();
}

static void fill_api(AppAPI *api) {
    api->sys = g_vm;
    api->fill_rect = app_fill_rect;
    api->draw_text = app_draw_text;
    api->text_measure = sys_gui_text_measure;
    api->beep = app_beep;
    api->ticks_ms = app_ticks;
}

static int napp_load_file(const char *path) {
    uint8_t raw[64 * 1024];
    uint32_t sz = 0;
    if (dimonfs_read((DimonDisk *)g_vm, path, 0, raw, sizeof(raw), (uint32_t *)&sz) < 0)
        return -1;
    if (sz < sizeof(NAppHeader)) return -2;
    NAppHeader h;
    memcpy(&h, raw, sizeof(h));
    if (memcmp(h.magic, NAPP_MAGIC, 6) != 0) return -3;
    if (h.version != NAPP_VERSION) return -4;
    if (sizeof(NAppHeader) + h.image_size > sz || h.image_size > sizeof(g_app_region)) return -5;
    memcpy(g_app_region, raw + sizeof(NAppHeader), h.image_size);
    g_app_main = (NAppMainFn)(g_app_region + h.entry_main);
    g_app_key = (NAppKeyFn)(g_app_region + h.entry_key);
    g_app_timer = (NAppVoidFn)(g_app_region + h.entry_timer);
    g_app_draw = (NAppVoidFn)(g_app_region + h.entry_draw);
    g_app_loaded = 1;
    AppAPI api;
    fill_api(&api);
    if (g_app_main) g_app_main(&api);
    return 0;
}

static void desktop_init(VM *vm) {
    g_vm = vm;
    for (int i = 1; i <= WIN_COUNT; i++) {
        win_x[i] = win_default_x[i] = DEF_X[i];
        win_y[i] = win_default_y[i] = DEF_Y[i];
        win_w[i] = DEF_W[i];
        win_h[i] = DEF_H[i];
        win_flags[i] = 0;
    }
    win_z_count = 0;
    active_window = 0;
    paint_color = 0xFFFF3B30;
    paint_radius = 3;
    paint_clear_canvas();
    str_copy(calc_buf, "0");
    str_copy(fm_cwd, "/");
    str_copy(note_title, "[ Notepad ]");
    files_refresh();
    need_redraw = 1;
}

void desktop_run(VM *vm) {
    desktop_init(vm);
    for (;;) {
        /* Kernel poll pushes hardware input and hilts when idle. */
        if (vm->gui_poll_cb) vm->gui_poll_cb(vm->gui_userdata);

        uint8_t t; uint16_t c, d; uint8_t b;
        while (vm->event_head != vm->event_tail) {
            g_last_mods = vm->event_queue[vm->event_head].modifiers;
            if (!vm_event_pop_ext(vm, &t, &c, &d, &b)) break;
            if (t == EVT_KEY) {
                handle_key(c, g_last_mods);
            } else if (t == EVT_MOUSE_CLICK) {
                handle_click(c, d, b);
            } else if (t == EVT_MOUSE_MOVE) {
                if (b == 1 || drag_active) handle_drag(c, d, b);
                else if (active_window == WIN_PAINT && b != 0) handle_drag(c, d, b);
            } else if (t == EVT_MOUSE_RELEASE) {
                handle_release(c, d, b);
            } else if (t == EVT_TIMER) {
                if ((win_flags[WIN_SNAKE] & WIN_FLAG_OPEN) && !(win_flags[WIN_SNAKE] & WIN_FLAG_MIN))
                    snake_on_timer();
                if (g_app_loaded && g_app_timer) {
                    AppAPI api; fill_api(&api);
                    g_app_timer(&api);
                }
            }
        }

        if (exit_requested) {
            fill_rect(0, 0, DESK_W, DESK_H, COL_BG);
            draw_text(300, 280, "System halted. You may close QEMU.", COL_TEXT);
            if (vm->gui_flush_cb) vm->gui_flush_cb(vm->gui_userdata);
            return;
        }

        if (need_redraw) {
            need_redraw = 0;
            os_repaint();
            if (dlg_active) { wm_reset_context(); dlg_draw(); }
            if (vm->gui_flush_cb) vm->gui_flush_cb(vm->gui_userdata);
        }
    }
}
