#define _DEFAULT_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include <termios.h>
#include <signal.h>
#include <sys/time.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>

#include "dimon16.h"
#include "font8x16.h"

static const uint32_t vga_palette[16] = {
    0x000000, /* 0: Black */
    0x0000AA, /* 1: Blue */
    0x00AA00, /* 2: Green */
    0x00AAAA, /* 3: Cyan */
    0xAA0000, /* 4: Red */
    0xAA00AA, /* 5: Magenta */
    0xAA5500, /* 6: Brown */
    0xAAAAAA, /* 7: Light Gray */
    0x555555, /* 8: Dark Gray */
    0x5555FF, /* 9: Light Blue */
    0x55FF55, /* 10: Light Green */
    0x55FFFF, /* 11: Light Cyan */
    0xFF5555, /* 12: Light Red */
    0xFF55FF, /* 13: Light Magenta */
    0xFFFF55, /* 14: Yellow */
    0xFFFFFF  /* 15: White */
};

/* --- Convert CP437 character to UTF-8 for ANSI terminal TUI --- */
static const char *cp437_to_utf8(uint8_t ch) {
    if (ch >= 0x20 && ch <= 0x7E) {
        static char s[2];
        s[0] = (char)ch;
        s[1] = 0;
        return s;
    }
    switch (ch) {
        case 0: return " ";
        case 1: return "☺";
        case 2: return "☻";
        case 3: return "♥";
        case 4: return "♦";
        case 5: return "♣";
        case 6: return "♠";
        case 7: return "•";
        case 8: return "◘";
        case 9: return "○";
        case 10: return "◙";
        case 11: return "♂";
        case 12: return "♀";
        case 13: return "♪";
        case 14: return "♫";
        case 15: return "☼";
        case 16: return "►";
        case 17: return "◄";
        case 18: return "↕";
        case 19: return "‼";
        case 20: return "¶";
        case 21: return "§";
        case 22: return "▬";
        case 23: return "↨";
        case 24: return "↑";
        case 25: return "↓";
        case 26: return "→";
        case 27: return "←";
        case 28: return "∟";
        case 29: return "↔";
        case 30: return "▲";
        case 31: return "▼";
        case 0x7F: return "⌂";
        /* Box drawing and semigraphics */
        case 0xB0: return "░";
        case 0xB1: return "▒";
        case 0xB2: return "▓";
        case 0xB3: return "│";
        case 0xB4: return "┤";
        case 0xB5: return "╡";
        case 0xB6: return "╢";
        case 0xB7: return "╖";
        case 0xB8: return "╕";
        case 0xB9: return "╣";
        case 0xBA: return "║";
        case 0xBB: return "╗";
        case 0xBC: return "╝";
        case 0xBD: return "╜";
        case 0xBE: return "╛";
        case 0xBF: return "┐";
        case 0xC0: return "└";
        case 0xC1: return "┴";
        case 0xC2: return "┬";
        case 0xC3: return "├";
        case 0xC4: return "─";
        case 0xC5: return "┼";
        case 0xC6: return "╞";
        case 0xC7: return "╟";
        case 0xC8: return "╚";
        case 0xC9: return "╔";
        case 0xCA: return "╩";
        case 0xCB: return "╦";
        case 0xCC: return "╠";
        case 0xCD: return "═";
        case 0xCE: return "╬";
        case 0xCF: return "╧";
        case 0xD0: return "╨";
        case 0xD1: return "╤";
        case 0xD2: return "╥";
        case 0xD3: return "╙";
        case 0xD4: return "╘";
        case 0xD5: return "╒";
        case 0xD6: return "╓";
        case 0xD7: return "╫";
        case 0xD8: return "╪";
        case 0xD9: return "┘";
        case 0xDA: return "┌";
        case 0xDB: return "█";
        case 0xDC: return "▄";
        case 0xDD: return "▌";
        case 0xDE: return "▐";
        case 0xDF: return "▀";
        case 0xFA: return "·";
        case 0xFB: return "√";
        case 0xFC: return "ⁿ";
        case 0xFD: return "²";
        case 0xFE: return "■";
        default: {
            static char def[2];
            def[0] = (ch >= 32 && ch < 127) ? (char)ch : ' ';
            def[1] = 0;
            return def;
        }
    }
}

typedef struct {
    int mode;       /* 0=none, 1=X11, 2=TUI */
    int req_mode;   /* -1=auto, 1=X11, 2=TUI */
    int scale;      /* 1 or 2 */
    VM  *vm;

    /* X11 state */
    Display *dpy;
    Window   win;
    GC       gc;
    XImage  *ximage;
    uint32_t *pixels;
    int      win_w;
    int      win_h;
    Atom     wm_delete_window;

    /* TUI state */
    int tui_initialized;
    struct termios orig_termios;
    uint32_t last_timer_ms;
} GuiApp;

static GuiApp g_app;

/* Forward declarations */
static void x11_flush_screen(VM *vm);
static void tui_flush_screen(VM *vm);

/* --- TUI Backend --- */
static void tui_cleanup(void) {
    if (!g_app.tui_initialized) return;
    g_app.tui_initialized = 0;
    printf("\033[?1000l\033[?1002l\033[?1006l"); /* Disable mouse tracking */
    printf("\033[?25h");                         /* Enable cursor */
    printf("\033[0m\r\n");                       /* Reset attributes */
    fflush(stdout);
    tcsetattr(STDIN_FILENO, TCSANOW, &g_app.orig_termios);
}

static void tui_init(void) {
    if (g_app.tui_initialized) return;
    tcgetattr(STDIN_FILENO, &g_app.orig_termios);
    atexit(tui_cleanup);

    struct termios raw = g_app.orig_termios;
    raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    raw.c_oflag &= ~(OPOST);
    raw.c_cflag |= (CS8);
    raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 0;
    tcsetattr(STDIN_FILENO, TCSANOW, &raw);

    /* Non-blocking stdin */
    int flags = fcntl(STDIN_FILENO, F_GETFL, 0);
    fcntl(STDIN_FILENO, F_SETFL, flags | O_NONBLOCK);

    printf("\033[?25l");                         /* Hide cursor */
    printf("\033[?1000h\033[?1002h\033[?1006h"); /* Mouse SGR */
    printf("\033[2J\033[H");                     /* Clear screen */
    fflush(stdout);
    g_app.tui_initialized = 1;

    if (g_app.vm) {
        tui_flush_screen(g_app.vm);
    }
}

static void tui_flush_screen(VM *vm) {
    if (!vm) return;
    char outbuf[65536];
    int pos = 0;
    pos += snprintf(outbuf + pos, sizeof(outbuf) - pos, "\033[H");
    int last_fg = -1, last_bg = -1;

    for (int y = 0; y < VRAM_ROWS; y++) {
        for (int x = 0; x < VRAM_COLS; x++) {
            uint16_t addr = (uint16_t)(VRAM_ADDR + (y * VRAM_COLS + x) * 2);
            uint8_t ch = vm->mem[addr];
            uint8_t attr = vm->mem[addr + 1];
            int fg = attr & 0x0F;
            int bg = (attr >> 4) & 0x0F;

            if (fg != last_fg || bg != last_bg) {
                int ansi_fg = (fg < 8) ? (30 + fg) : (90 + fg - 8);
                int ansi_bg = (bg < 8) ? (40 + bg) : (100 + bg - 8);
                pos += snprintf(outbuf + pos, sizeof(outbuf) - pos, "\033[%d;%dm", ansi_fg, ansi_bg);
                last_fg = fg;
                last_bg = bg;
            }

            const char *u = cp437_to_utf8(ch);
            int ulen = (int)strlen(u);
            if (pos + ulen < (int)sizeof(outbuf) - 64) {
                memcpy(outbuf + pos, u, ulen);
                pos += ulen;
            }
        }
        if (y < VRAM_ROWS - 1) {
            pos += snprintf(outbuf + pos, sizeof(outbuf) - pos, "\r\n");
        }
    }
    ssize_t written = write(STDOUT_FILENO, outbuf, pos);
    (void)written;
}

static void tui_poll_events(VM *vm) {
    uint8_t buf[256];
    ssize_t n = read(STDIN_FILENO, buf, sizeof(buf) - 1);
    if (n > 0) {
        buf[n] = 0;
        int i = 0;
        while (i < n) {
            if (buf[i] == 27) {
                /* Escape sequence */
                if (i + 1 >= n) {
                    /* Lone ESC */
                    vm_event_push(vm, EVT_KEY, 27, 0);
                    i++;
                    continue;
                }
                if (buf[i + 1] == '[') {
                    if (i + 2 < n && buf[i + 2] == '<') {
                        /* SGR mouse event: \033[<btn;x;yM or m */
                        int btn = 0, x = 0, y = 0;
                        char type_ch = 0;
                        int consumed = 0;
                        if (sscanf((char *)buf + i + 3, "%d;%d;%d%c%n", &btn, &x, &y, &type_ch, &consumed) >= 4) {
                            int cx = x - 1;
                            int cy = y - 1;
                            if (cx >= 0 && cx < VRAM_COLS && cy >= 0 && cy < VRAM_ROWS) {
                                if (type_ch == 'M') {
                                    if (btn == 0) {
                                        vm_event_push(vm, EVT_MOUSE_CLICK, (uint16_t)cx, (uint16_t)cy);
                                    } else if (btn == 35 || btn == 32) {
                                        vm_event_push(vm, EVT_MOUSE_MOVE, (uint16_t)cx, (uint16_t)cy);
                                    }
                                }
                            }
                            i += 3 + consumed;
                            continue;
                        }
                    } else if (i + 2 < n) {
                        /* Arrow keys */
                        if (buf[i + 2] == 'A') { vm_event_push(vm, EVT_KEY, KEY_UP, 0); i += 3; continue; }
                        if (buf[i + 2] == 'B') { vm_event_push(vm, EVT_KEY, KEY_DOWN, 0); i += 3; continue; }
                        if (buf[i + 2] == 'C') { vm_event_push(vm, EVT_KEY, KEY_RIGHT, 0); i += 3; continue; }
                        if (buf[i + 2] == 'D') { vm_event_push(vm, EVT_KEY, KEY_LEFT, 0); i += 3; continue; }
                    }
                } else if (buf[i + 1] == 'O' && i + 2 < n) {
                    /* F1..F4 */
                    if (buf[i + 2] >= 'P' && buf[i + 2] <= 'S') {
                        vm_event_push(vm, EVT_KEY, KEY_F1 + (buf[i + 2] - 'P'), 0);
                        i += 3;
                        continue;
                    }
                }
                /* Lone ESC key */
                vm_event_push(vm, EVT_KEY, 27, 0);
                i++;
                continue;
            } else {
                uint16_t code = buf[i];
                if (code == 127) code = 8; /* Backspace */
                else if (code == '\n') code = 13;
                else if (code == 3) { /* Ctrl+C */
                    vm->halted = 1;
                    return;
                }
                vm_event_push(vm, EVT_KEY, code, 0);
                i++;
            }
        }
    }

    struct timeval tv;
    gettimeofday(&tv, NULL);
    uint32_t now = (uint32_t)(tv.tv_sec * 1000 + tv.tv_usec / 1000);

    if (now - g_app.last_timer_ms >= 50) {
        g_app.last_timer_ms = now;
        vm_event_push(vm, EVT_TIMER, 0, 0);
    }

    static uint32_t last_tui_sleep_ms = 0;
    if (n <= 0) {
        if (now - last_tui_sleep_ms >= 10) {
            last_tui_sleep_ms = now;
            usleep(1000);
        }
    }
}

/* --- X11 Backend --- */
static int x11_init(void) {
    if (g_app.dpy) return 0;
    const char *disp_name = getenv("DISPLAY");
    if (!disp_name || !disp_name[0]) return -1;

    g_app.dpy = XOpenDisplay(NULL);
    if (!g_app.dpy) return -1;

    int screen = DefaultScreen(g_app.dpy);
    int scr_h = DisplayHeight(g_app.dpy, screen);

    if (g_app.scale <= 0) {
        g_app.scale = (scr_h >= 850) ? 2 : 1;
    }

    g_app.win_w = VRAM_COLS * 8 * g_app.scale;
    g_app.win_h = VRAM_ROWS * 16 * g_app.scale;

    g_app.pixels = (uint32_t *)calloc(g_app.win_w * g_app.win_h, sizeof(uint32_t));
    if (!g_app.pixels) {
        XCloseDisplay(g_app.dpy);
        g_app.dpy = NULL;
        return -1;
    }

    g_app.win = XCreateSimpleWindow(
        g_app.dpy, RootWindow(g_app.dpy, screen),
        100, 100, g_app.win_w, g_app.win_h, 1,
        BlackPixel(g_app.dpy, screen),
        BlackPixel(g_app.dpy, screen)
    );

    XStoreName(g_app.dpy, g_app.win, "DimonOS v2.0 GUI");

    /* Prevent window resize */
    XSizeHints *hints = XAllocSizeHints();
    if (hints) {
        hints->flags = PMinSize | PMaxSize;
        hints->min_width = hints->max_width = g_app.win_w;
        hints->min_height = hints->max_height = g_app.win_h;
        XSetWMNormalHints(g_app.dpy, g_app.win, hints);
        XFree(hints);
    }

    g_app.wm_delete_window = XInternAtom(g_app.dpy, "WM_DELETE_WINDOW", False);
    XSetWMProtocols(g_app.dpy, g_app.win, &g_app.wm_delete_window, 1);

    long event_mask = KeyPressMask | KeyReleaseMask | ButtonPressMask |
                      ButtonReleaseMask | PointerMotionMask | ExposureMask |
                      StructureNotifyMask;
    XSelectInput(g_app.dpy, g_app.win, event_mask);

    g_app.gc = XCreateGC(g_app.dpy, g_app.win, 0, NULL);
    g_app.ximage = XCreateImage(
        g_app.dpy, DefaultVisual(g_app.dpy, screen),
        DefaultDepth(g_app.dpy, screen), ZPixmap, 0,
        (char *)g_app.pixels, g_app.win_w, g_app.win_h, 32, 0
    );

    XMapWindow(g_app.dpy, g_app.win);
    XFlush(g_app.dpy);

    /* Wait for first window mapping event */
    XEvent ev;
    while (1) {
        XNextEvent(g_app.dpy, &ev);
        if (ev.type == MapNotify) break;
    }

    /* Immediately render current VRAM buffer to window to eliminate initial black screen */
    if (g_app.vm) {
        x11_flush_screen(g_app.vm);
    }

    return 0;
}

static void x11_cleanup(void) {
    if (g_app.dpy) {
        if (g_app.ximage) {
            XDestroyImage(g_app.ximage);
            g_app.ximage = NULL;
            g_app.pixels = NULL; /* Freed by XDestroyImage */
        }
        if (g_app.gc) { XFreeGC(g_app.dpy, g_app.gc); g_app.gc = NULL; }
        if (g_app.win) { XDestroyWindow(g_app.dpy, g_app.win); g_app.win = 0; }
        XCloseDisplay(g_app.dpy);
        g_app.dpy = NULL;
    }
}

static void x11_flush_screen(VM *vm) {
    if (!g_app.dpy || !g_app.pixels || !vm) return;

    int scale = g_app.scale;
    int win_w = g_app.win_w;

    for (int cy = 0; cy < VRAM_ROWS; cy++) {
        for (int cx = 0; cx < VRAM_COLS; cx++) {
            uint16_t addr = (uint16_t)(VRAM_ADDR + (cy * VRAM_COLS + cx) * 2);
            uint8_t ch = vm->mem[addr];
            uint8_t attr = vm->mem[addr + 1];
            uint32_t fg = vga_palette[attr & 0x0F];
            uint32_t bg = vga_palette[(attr >> 4) & 0x0F];

            for (int r = 0; r < 16; r++) {
                uint8_t bits = font8x16[ch][r];
                for (int c = 0; c < 8; c++) {
                    uint32_t col = (bits & (0x80 >> c)) ? fg : bg;
                    for (int sy = 0; sy < scale; sy++) {
                        int py = (cy * 16 + r) * scale + sy;
                        uint32_t *line = &g_app.pixels[py * win_w];
                        for (int sx = 0; sx < scale; sx++) {
                            int px = (cx * 8 + c) * scale + sx;
                            line[px] = col;
                        }
                    }
                }
            }
        }
    }

    XPutImage(g_app.dpy, g_app.win, g_app.gc, g_app.ximage, 0, 0, 0, 0, g_app.win_w, g_app.win_h);
    XFlush(g_app.dpy);
}

static void x11_poll_events(VM *vm) {
    if (!g_app.dpy) return;

    int events_handled = 0;
    while (XPending(g_app.dpy) > 0) {
        XEvent ev;
        XNextEvent(g_app.dpy, &ev);
        events_handled++;

        if (ev.type == ClientMessage) {
            if ((Atom)ev.xclient.data.l[0] == g_app.wm_delete_window) {
                vm->halted = 1;
                return;
            }
        } else if (ev.type == ButtonPress) {
            int cx = ev.xbutton.x / (8 * g_app.scale);
            int cy = ev.xbutton.y / (16 * g_app.scale);
            if (cx >= 0 && cx < VRAM_COLS && cy >= 0 && cy < VRAM_ROWS) {
                vm_event_push(vm, EVT_MOUSE_CLICK, (uint16_t)cx, (uint16_t)cy);
            }
        } else if (ev.type == MotionNotify) {
            int cx = ev.xmotion.x / (8 * g_app.scale);
            int cy = ev.xmotion.y / (16 * g_app.scale);
            if (cx >= 0 && cx < VRAM_COLS && cy >= 0 && cy < VRAM_ROWS) {
                vm_event_push(vm, EVT_MOUSE_MOVE, (uint16_t)cx, (uint16_t)cy);
            }
        } else if (ev.type == KeyPress) {
            KeySym ks;
            char str[32];
            int n = XLookupString(&ev.xkey, str, sizeof(str) - 1, &ks, NULL);
            uint16_t code = 0;
            switch (ks) {
                case XK_Up:        code = KEY_UP; break;
                case XK_Down:      code = KEY_DOWN; break;
                case XK_Left:      code = KEY_LEFT; break;
                case XK_Right:     code = KEY_RIGHT; break;
                case XK_F1:        code = KEY_F1; break;
                case XK_F2:        code = KEY_F2; break;
                case XK_F3:        code = KEY_F3; break;
                case XK_F4:        code = KEY_F4; break;
                case XK_F5:        code = KEY_F5; break;
                case XK_F6:        code = KEY_F6; break;
                case XK_F7:        code = KEY_F7; break;
                case XK_F8:        code = KEY_F8; break;
                case XK_F9:        code = KEY_F9; break;
                case XK_F10:       code = KEY_F10; break;
                case XK_Return:
                case XK_KP_Enter:  code = 13; break;
                case XK_BackSpace: code = 8; break;
                case XK_Tab:       code = 9; break;
                case XK_Escape:    code = 27; break;
                default:
                    if (n == 1) {
                        code = (uint8_t)str[0];
                    }
                    break;
            }
            if (code > 0) {
                vm_event_push(vm, EVT_KEY, code, 0);
            }
        } else if (ev.type == Expose) {
            x11_flush_screen(g_app.vm);
        }
    }

    struct timeval tv;
    gettimeofday(&tv, NULL);
    uint32_t now = (uint32_t)(tv.tv_sec * 1000 + tv.tv_usec / 1000);

    if (now - g_app.last_timer_ms >= 50) {
        g_app.last_timer_ms = now;
        vm_event_push(vm, EVT_TIMER, 0, 0);
    }

    static uint32_t last_x11_sleep_ms = 0;
    if (!events_handled) {
        if (now - last_x11_sleep_ms >= 10) {
            last_x11_sleep_ms = now;
            usleep(1000);
        }
    }
}

/* Ensure display backend is initialized immediately on first GUI access */
static void ensure_gui_init(GuiApp *app) {
    if (app->mode == 0) {
        if (app->req_mode == 1 || (app->req_mode == -1 && getenv("DISPLAY"))) {
            if (x11_init() == 0) {
                app->mode = 1;
            } else {
                tui_init();
                app->mode = 2;
            }
        } else {
            tui_init();
            app->mode = 2;
        }
    }
}

/* --- Callbacks connected to VM --- */
static void on_gui_init(void *userdata) {
    GuiApp *app = (GuiApp *)userdata;
    ensure_gui_init(app);
}

static void on_gui_poll(void *userdata) {
    GuiApp *app = (GuiApp *)userdata;
    ensure_gui_init(app);

    if (app->mode == 1) {
        x11_poll_events(app->vm);
    } else if (app->mode == 2) {
        tui_poll_events(app->vm);
    }
}

static void on_gui_flush(void *userdata) {
    GuiApp *app = (GuiApp *)userdata;
    ensure_gui_init(app);

    if (app->mode == 1) {
        x11_flush_screen(app->vm);
    } else if (app->mode == 2) {
        tui_flush_screen(app->vm);
    }
}

/* --- Debugger and hexdump --- */
static void usage(const char *p) {
    fprintf(stderr,
        "Usage: %s [program.bin] [options]\n"
        "  %s --iso image.iso [options]         Boot from ISO/disk image (sector 0)\n"
        "  %s program.bin --iso image.iso       Program + attached disk\n"
        "Options:\n"
        "  -g, --gui             Force X11 graphical window\n"
        "  -tui, --tui           Force ANSI terminal console mode (TUI)\n"
        "  --scale N             X11 window scale factor (1 or 2, default auto-fit)\n"
        "  -s ADDR               Start execution address (default 0, e.g. 0x100)\n"
        "  -l ADDR               Load address (default 0)\n"
        "  -i FILE, --iso FILE, --disk FILE\n"
        "                        Attach ISO/disk image (512B/sector, %d max sectors)\n"
        "  --disk-writable       Allow INT 8 writes (default read-only + file persistence)\n"
        "  -d                    Interactive debugger mode\n"
        "  -t                    Trace execution: print each instruction\n"
        "  -r                    Dump registers on exit\n"
        "  -m N                  Step limit (infinite loop protection)\n"
        "Syscalls: INT 0-5 I/O, INT 6-8 Disk, INT 10-15 GUI\n",
        p, p, p, DISK_MAX_SECTORS);
}

static int parse_u16(const char *s) {
    return (int)strtol(s, NULL, 0);
}

static void hexdump(VM *vm, uint16_t addr, int n) {
    for (int i = 0; i < n; i += 16) {
        printf("%04X: ", (unsigned)(addr + i));
        for (int j = 0; j < 16 && i + j < n; j++)
            printf("%02X ", vm->mem[(uint16_t)(addr + i + j)]);
        printf(" | ");
        for (int j = 0; j < 16 && i + j < n; j++) {
            uint8_t c = vm->mem[(uint16_t)(addr + i + j)];
            putchar(c >= 32 && c < 127 ? c : '.');
        }
        putchar('\n');
    }
}

#define MAX_BP 16
static uint16_t bps[MAX_BP];
static int nbp = 0;

static int at_bp(uint16_t pc) {
    for (int i = 0; i < nbp; i++) if (bps[i] == pc) return 1;
    return 0;
}

static void debugger(VM *vm) {
    char line[256];
    printf("=== Dimon-16 debugger ===\n"
           "Commands: s=step  c=continue  r=regs  m ADDR [N]=dump  "
           "u ADDR [N]=disasm  b ADDR=break  q=quit  h=help\n");
    for (;;) {
        char dasm[96];
        vm_disasm(vm, vm->PC, dasm, sizeof(dasm));
        printf("[%04X] %-20s > ", vm->PC, dasm);
        fflush(stdout);
        if (!fgets(line, sizeof(line), stdin)) break;
        char cmd = 0;
        unsigned a = 0;
        if (sscanf(line, " %c", &cmd) != 1) continue;
        if (cmd == 'q') break;
        else if (cmd == 'h') {
            printf("s [N] - execute N steps (default 1)\n"
                   "c - continue to HLT/error/breakpoint\n"
                   "r - registers\n"
                   "m ADDR [N] - memory hexdump\n"
                   "u [ADDR] [N] - disassemble\n"
                   "b ADDR - set breakpoint (repeat list, 'b c' clears all)\n"
                   "q - quit\n");
        } else if (cmd == 'r') {
            vm_dump_regs(vm, stdout);
        } else if (cmd == 's') {
            int n = 1;
            sscanf(line + 1, "%d", &n);
            if (n < 1) n = 1;
            for (int i = 0; i < n; i++) {
                vm_disasm(vm, vm->PC, dasm, sizeof(dasm));
                printf("  %04X: %s\n", vm->PC, dasm);
                int rc = vm_step(vm);
                if (rc == 1) { printf("[HLT]\n"); vm_dump_regs(vm, stdout); return; }
                if (rc != 0) { printf("[ERROR %d]\n", rc); return; }
            }
            vm_dump_regs(vm, stdout);
        } else if (cmd == 'c') {
            for (;;) {
                if (at_bp(vm->PC)) { printf("[BREAK @ %04X]\n", vm->PC); break; }
                int rc = vm_step(vm);
                if (rc == 1) { printf("[HLT]\n"); vm_dump_regs(vm, stdout); return; }
                if (rc != 0) { printf("[ERROR %d]\n", rc); return; }
                if (vm->max_steps && vm->steps >= vm->max_steps) {
                    printf("[STEP LIMIT]\n"); return;
                }
            }
            vm_dump_regs(vm, stdout);
        } else if (cmd == 'm') {
            int n = 64;
            if (sscanf(line + 1, "%x %d", &a, &n) < 1) continue;
            hexdump(vm, (uint16_t)a, n);
        } else if (cmd == 'u') {
            uint16_t p = vm->PC;
            int n = 8;
            char *rest = line + 1;
            while (*rest == ' ' || *rest == '\t') rest++;
            if (*rest && *rest != '\n') {
                unsigned aa; int nn;
                if (sscanf(rest, "%x %d", &aa, &nn) == 2) { p = (uint16_t)aa; n = nn; }
                else if (sscanf(rest, "%x", &aa) == 1) {
                    p = (uint16_t)aa; n = 8;
                }
            }
            for (int i = 0; i < n; i++) {
                int len = vm_disasm(vm, p, dasm, sizeof(dasm));
                printf("  %04X: %-20s |", p, dasm);
                for (int k = 0; k < len; k++) printf(" %02X", vm->mem[(uint16_t)(p + k)]);
                printf("\n");
                p = (uint16_t)(p + len);
            }
        } else if (cmd == 'b') {
            char *rest = line + 1;
            while (*rest == ' ' || *rest == '\t') rest++;
            if (*rest == 'c') { nbp = 0; printf("Cleared breakpoints\n"); }
            else if (!*rest || *rest == '\n') {
                printf("Breakpoints (%d):\n", nbp);
                for (int i = 0; i < nbp; i++) printf("  #%d @ %04X\n", i, bps[i]);
            } else {
                unsigned aa;
                if (sscanf(rest, "%x", &aa) == 1 && nbp < MAX_BP) {
                    bps[nbp++] = (uint16_t)aa;
                    printf("Added break @ %04X\n", aa & 0xFFFF);
                }
            }
        } else {
            printf("Unknown command (h=help)\n");
        }
    }
}

int main(int argc, char **argv) {
    if (argc < 2) { usage(argv[0]); return 1; }
    const char *path = NULL;
    const char *iso_path = NULL;
    uint16_t start = 0, load = 0;
    int debug = 0, trace = 0, dumpregs = 0, writable = 0;
    int have_start = 0;
    uint64_t maxsteps = 0; /* 0 = unlimited */

    memset(&g_app, 0, sizeof(g_app));
    g_app.req_mode = -1; /* auto-detect */
    g_app.scale = 0;

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-d")) debug = 1;
        else if (!strcmp(argv[i], "-t")) trace = 1;
        else if (!strcmp(argv[i], "-r")) dumpregs = 1;
        else if (!strcmp(argv[i], "-g") || !strcmp(argv[i], "--gui")) g_app.req_mode = 1;
        else if (!strcmp(argv[i], "-tui") || !strcmp(argv[i], "--tui")) g_app.req_mode = 2;
        else if (!strcmp(argv[i], "--scale") && i + 1 < argc) g_app.scale = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--disk-writable")) writable = 1;
        else if ((!strcmp(argv[i], "-i") || !strcmp(argv[i], "--iso") ||
                  !strcmp(argv[i], "--disk")) && i + 1 < argc) iso_path = argv[++i];
        else if (!strcmp(argv[i], "-s") && i + 1 < argc) { start = (uint16_t)parse_u16(argv[++i]); have_start = 1; }
        else if (!strcmp(argv[i], "-l") && i + 1 < argc) load = (uint16_t)parse_u16(argv[++i]);
        else if (!strcmp(argv[i], "-m") && i + 1 < argc) maxsteps = strtoull(argv[++i], NULL, 0);
        else if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) { usage(argv[0]); return 0; }
        else if (argv[i][0] == '-') { usage(argv[0]); return 1; }
        else if (!path) path = argv[i];
        else { usage(argv[0]); return 1; }
    }

    if (!path && !iso_path) { usage(argv[0]); return 1; }

    VM vm;
    vm_init(&vm);
    g_app.vm = &vm;
    vm.gui_userdata = &g_app;
    vm.gui_init_cb = on_gui_init;
    vm.gui_poll_cb = on_gui_poll;
    vm.gui_flush_cb = on_gui_flush;

    int rc = 0;

    if (iso_path) {
        if (vm_disk_attach(&vm, iso_path, writable) != 0) {
            perror("vm_disk_attach");
            fprintf(stderr, "Cannot open ISO image: %s\n", iso_path);
            vm_free(&vm);
            return 1;
        }
        fprintf(stderr, "[ISO] attached %s (%u sectors, %s)\n",
                iso_path, vm.disk_sectors,
                writable ? "rw" : "ro");
    }

    if (!path) {
        int brc = vm_disk_boot(&vm, load);
        if (brc != DISK_ERR_NONE) {
            fprintf(stderr, "Boot from ISO failed (code %d)\n", brc);
            vm_free(&vm);
            return 1;
        }
        if (vm.mem[load + 510] != DISK_BOOT_MAGIC0 ||
            vm.mem[load + 511] != DISK_BOOT_MAGIC1) {
            fprintf(stderr, "[ISO] warning: missing boot magic 55AA in sector 0\n");
        }
        if (!have_start) start = load;
    } else {
        if (vm_load(&vm, path, load) != 0) { perror("vm_load"); vm_free(&vm); return 1; }
    }
    vm_reset(&vm, start);
    vm.max_steps = maxsteps;

    if (debug) {
        debugger(&vm);
        vm_free(&vm);
        return 0;
    }

    if (trace) {
        char dasm[96];
        while (!vm.halted) {
            vm_disasm(&vm, vm.PC, dasm, sizeof(dasm));
            printf("[%04X] %s\n", vm.PC, dasm);
            rc = vm_step(&vm);
            if (rc == 1) break;
            if (rc != 0) { fprintf(stderr, "Execution error: %d\n", rc); vm_free(&vm); return 1; }
        }
    } else {
        rc = vm_run(&vm);
        if (rc != 1 && rc != 0) {
            if (rc == -100) {
                /* Step limit reached */
            } else {
                fprintf(stderr, "Execution error: %d\n", rc);
                vm_free(&vm);
                return 1;
            }
        }
    }

    if (g_app.mode == 1) {
        x11_cleanup();
    } else if (g_app.mode == 2) {
        tui_cleanup();
    }

    if (dumpregs) vm_dump_regs(&vm, stderr);
    vm_free(&vm);
    return 0;
}
