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
#include <sys/ioctl.h>
#include <pthread.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <locale.h>

#include "dimon64.h"
#include <inttypes.h>
#include "font8x16.h"

/* 12x19 32-bit Arrow Cursor */
static const char *cursor_arrow[19] = {
    "X           ",
    "XX          ",
    "X.X         ",
    "X..X        ",
    "X...X       ",
    "X....X      ",
    "X.....X     ",
    "X......X    ",
    "X.......X   ",
    "X........X  ",
    "X.....XXXXX ",
    "X..X..X     ",
    "X.X X..X    ",
    "XX   X..X   ",
    "X     X..X  ",
    "      X..X  ",
    "       XX   ",
    "            ",
    "            "
};

static const char *g_dump_vram_path = NULL;
static const char *g_dump_state_path = NULL;
static const char *g_input_log_path = NULL;
static FILE *g_input_log = NULL;
static int g_stop_when_idle = 0;
static int g_idle_completed = 0;
static const char *g_inject_keys = NULL;
static int g_click_x[64];
static int g_click_y[64];
static int g_click_count = 0;

typedef struct {
    uint8_t type;
    uint8_t button;
    uint8_t modifiers;
    uint16_t code;
    uint16_t data;
} InjectEvent;

static InjectEvent g_inject_events[128];
static int g_inject_event_count = 0;

static void input_log(const char *backend, const char *action,
                      int x, int y, unsigned button) {
    if (!g_input_log) return;
    fprintf(g_input_log, "%s %s x=%d y=%d button=%u\n",
            backend, action, x, y, button);
    fflush(g_input_log);
}

/* Ordered input used by behavioral tests. Tokens are separated by ';':
 *   k:CODE[,MODS]  p:X,Y[,BUTTON]  m:X,Y[,BUTTON]  r:X,Y[,BUTTON]
 * All events enter the normal VM queue and desktop dispatch path. */
static int parse_inject_events(const char *spec) {
    while (spec && *spec) {
        while (*spec == ' ' || *spec == ';') spec++;
        if (!*spec) break;
        if (g_inject_event_count >= (int)(sizeof(g_inject_events) / sizeof(g_inject_events[0])))
            return -1;

        char kind = *spec++;
        if (*spec++ != ':') return -1;

        InjectEvent ev;
        memset(&ev, 0, sizeof(ev));
        char *end = NULL;
        unsigned long first = strtoul(spec, &end, 0);
        if (end == spec) return -1;
        spec = end;

        if (kind == 'k') {
            if (first > UINT16_MAX) return -1;
            ev.type = EVT_KEY;
            ev.code = (uint16_t)first;
            if (*spec == ',') {
                unsigned long mods = strtoul(spec + 1, &end, 0);
                if (end == spec + 1 || mods > UINT8_MAX) return -1;
                ev.modifiers = (uint8_t)mods;
                spec = end;
            }
        } else if (kind == 'p' || kind == 'm' || kind == 'r') {
            if (first >= DIMON64_LFB_WIDTH || *spec != ',') return -1;
            unsigned long second = strtoul(spec + 1, &end, 0);
            if (end == spec + 1 || second >= DIMON64_LFB_HEIGHT) return -1;
            spec = end;
            ev.type = kind == 'p' ? EVT_MOUSE_CLICK :
                      kind == 'm' ? EVT_MOUSE_MOVE : EVT_MOUSE_RELEASE;
            ev.code = (uint16_t)first;
            ev.data = (uint16_t)second;
            ev.button = 1;
            if (*spec == ',') {
                unsigned long button = strtoul(spec + 1, &end, 0);
                if (end == spec + 1 || button > UINT8_MAX) return -1;
                ev.button = (uint8_t)button;
                spec = end;
            }
        } else {
            return -1;
        }
        if (*spec && *spec != ';') return -1;
        g_inject_events[g_inject_event_count++] = ev;
    }
    return 0;
}

static void inject_events(VM *vm) {
    for (int i = 0; i < g_inject_event_count; i++) {
        const InjectEvent *ev = &g_inject_events[i];
        vm_event_push_mod(vm, ev->type, ev->code, ev->data, ev->button, ev->modifiers);
    }
}

/* Push scripted key events (automated GUI tests).
 * Plain chars map to keycodes; backslash escapes: \n=Enter, \e=ESC,
 * \b=Backspace, \t=Tab, \\=backslash, \U/D/L/R=arrows, \1..\9=F1..F9. */
static void inject_keys(VM *vm, const char *spec) {
    for (const char *p = spec; *p; p++) {
        uint16_t code = 0;
        if (*p == '\\') {
            p++;
            if (!*p) break;
            switch (*p) {
                case 'n': code = 13; break;
                case 'e': code = 27; break;
                case 'b': code = 8; break;
                case 't': code = 9; break;
                case '\\': code = 92; break;
                case 'U': code = KEY_UP; break;
                case 'D': code = KEY_DOWN; break;
                case 'L': code = KEY_LEFT; break;
                case 'R': code = KEY_RIGHT; break;
                case '1': case '2': case '3': case '4': case '5':
                case '6': case '7': case '8': case '9':
                    code = (uint16_t)(KEY_F1 + (*p - '1'));
                    break;
                default: code = (uint8_t)*p; break;
            }
        } else {
            code = (uint8_t)*p;
        }
        if (code) vm_event_push(vm, EVT_KEY, code, 0);
    }
}

typedef struct {
    int mode;       /* 0=none, 1=X11, 2=TUI, 3=headless (VRAM memory only) */
    int req_mode;   /* -1=auto, 1=X11, 2=TUI, 3=headless */
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
    int      mouse_x;
    int      mouse_y;

    /* TUI state */
    int tui_initialized;
    struct termios orig_termios;
    uint32_t last_timer_ms;
    int      tui_cols;
    int      tui_rows;
} GuiApp;

static GuiApp g_app;

/* Forward declarations */
static void x11_flush_screen(VM *vm);
static void tui_flush_screen(VM *vm);

/* --- PSG Tone Synthesizer & Audio Backend --- */
typedef struct {
    uint32_t freq;
    uint32_t duration_ms;
    uint8_t  wave;
    uint8_t  vol;
} AudioCommand;

#define AUDIO_QUEUE_SIZE 32
static AudioCommand g_audio_queue[AUDIO_QUEUE_SIZE];
static int g_audio_q_head = 0;
static int g_audio_q_tail = 0;
static pthread_mutex_t g_audio_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  g_audio_cond = PTHREAD_COND_INITIALIZER;
static pthread_t g_audio_th;
static int g_audio_running = 0;
static FILE *g_aplay_pipe = NULL;

static void synth_waveform(uint8_t *buf, size_t n_samples, uint32_t freq, uint8_t wave, uint8_t vol) {
    if (!buf || n_samples == 0) return;
    int amp = (int)vol / 2;
    if (amp > 127) amp = 127;
    if (amp <= 0) {
        memset(buf, 128, n_samples);
        return;
    }

    const uint32_t rate = 22050;

    switch (wave) {
        case DIMON64_PSG_WAVE_SQUARE: {
            if (freq == 0) {
                memset(buf, 128, n_samples);
                return;
            }
            for (size_t i = 0; i < n_samples; i++) {
                uint32_t phase = (uint32_t)(((uint64_t)i * freq) % rate);
                buf[i] = (phase < rate / 2) ? (uint8_t)(128 + amp) : (uint8_t)(128 - amp);
            }
            break;
        }
        case DIMON64_PSG_WAVE_TRIANGLE: {
            if (freq == 0) {
                memset(buf, 128, n_samples);
                return;
            }
            uint32_t half = rate / 2;
            for (size_t i = 0; i < n_samples; i++) {
                uint32_t phase = (uint32_t)(((uint64_t)i * freq) % rate);
                if (phase < half) {
                    int delta = (int)((2LL * amp * phase) / half);
                    buf[i] = (uint8_t)(128 - amp + delta);
                } else {
                    int delta = (int)((2LL * amp * (phase - half)) / half);
                    buf[i] = (uint8_t)(128 + amp - delta);
                }
            }
            break;
        }
        case DIMON64_PSG_WAVE_NOISE: {
            uint32_t step = (freq > 0) ? (rate / freq) : 1;
            if (step < 1) step = 1;
            int cur_noise = 0;
            for (size_t i = 0; i < n_samples; i++) {
                if (i % step == 0) {
                    cur_noise = (rand() % (2 * amp + 1)) - amp;
                }
                buf[i] = (uint8_t)(128 + cur_noise);
            }
            break;
        }
        default:
            memset(buf, 128, n_samples);
            return;
    }

    /* Envelope: linear fade-in and fade-out (first/last 2 ms) to prevent speaker pop */
    size_t fade = 44;
    if (fade > n_samples / 2) fade = n_samples / 2;
    for (size_t i = 0; i < fade; i++) {
        int diff_in = (int)buf[i] - 128;
        buf[i] = (uint8_t)(128 + (diff_in * (int)i) / (int)fade);

        size_t j = n_samples - 1 - i;
        int diff_out = (int)buf[j] - 128;
        buf[j] = (uint8_t)(128 + (diff_out * (int)i) / (int)fade);
    }
}

static void *audio_worker_thread(void *arg) {
    (void)arg;
    signal(SIGPIPE, SIG_IGN);
    while (1) {
        AudioCommand cmd;
        pthread_mutex_lock(&g_audio_mutex);
        while (g_audio_q_head == g_audio_q_tail && g_audio_running) {
            pthread_cond_wait(&g_audio_cond, &g_audio_mutex);
        }
        if (!g_audio_running && g_audio_q_head == g_audio_q_tail) {
            pthread_mutex_unlock(&g_audio_mutex);
            break;
        }
        cmd = g_audio_queue[g_audio_q_head];
        g_audio_q_head = (g_audio_q_head + 1) % AUDIO_QUEUE_SIZE;
        pthread_mutex_unlock(&g_audio_mutex);

        if (cmd.duration_ms > 5000) cmd.duration_ms = 5000;
        const size_t rate = 22050;
        size_t n_samples = (size_t)((uint64_t)rate * cmd.duration_ms / 1000ULL);
        if (n_samples == 0) continue;

        uint8_t *buf = (uint8_t *)malloc(n_samples);
        if (!buf) continue;
        synth_waveform(buf, n_samples, cmd.freq, cmd.wave, cmd.vol);

        int played = 0;
        if (!g_aplay_pipe) {
            g_aplay_pipe = popen("aplay -q -t raw -f U8 -r 22050 -c 1 2>/dev/null", "w");
        }
        if (g_aplay_pipe) {
            size_t written = fwrite(buf, 1, n_samples, g_aplay_pipe);
            fflush(g_aplay_pipe);
            if (written == n_samples) {
                played = 1;
            } else {
                pclose(g_aplay_pipe);
                g_aplay_pipe = NULL;
            }
        }
        if (!played && g_app.mode == 1 && g_app.dpy) {
            XBell(g_app.dpy, 0);
            XFlush(g_app.dpy);
        }
        free(buf);
    }
    return NULL;
}

static void audio_cleanup(void) {
    if (g_audio_running) {
        pthread_mutex_lock(&g_audio_mutex);
        g_audio_running = 0;
        pthread_cond_signal(&g_audio_cond);
        pthread_mutex_unlock(&g_audio_mutex);
    }
    if (g_aplay_pipe) {
        pclose(g_aplay_pipe);
        g_aplay_pipe = NULL;
    }
}

static void on_psg_play(void *userdata, uint32_t freq, uint32_t duration_ms, uint8_t wave, uint8_t vol) {
    (void)userdata;
    /* Headless mode: update state silently without audio output or delays */
    if (g_app.req_mode == 3 || g_app.mode == 3) return;
    if (duration_ms == 0 || vol == 0) return;
    if (freq == 0 && wave != DIMON64_PSG_WAVE_NOISE) return;

    pthread_mutex_lock(&g_audio_mutex);
    if (!g_audio_running) {
        g_audio_running = 1;
        pthread_create(&g_audio_th, NULL, audio_worker_thread, NULL);
    }
    int next_tail = (g_audio_q_tail + 1) % AUDIO_QUEUE_SIZE;
    if (next_tail != g_audio_q_head) {
        g_audio_queue[g_audio_q_tail].freq = freq;
        g_audio_queue[g_audio_q_tail].duration_ms = duration_ms;
        g_audio_queue[g_audio_q_tail].wave = wave;
        g_audio_queue[g_audio_q_tail].vol = vol;
        g_audio_q_tail = next_tail;
        pthread_cond_signal(&g_audio_cond);
    }
    pthread_mutex_unlock(&g_audio_mutex);
}

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
    if (!vm || !vm->mem) return;
    struct winsize ws;
    int cols = 80, rows = 25;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0) {
        if (ws.ws_col >= 40 && ws.ws_col <= 240) cols = ws.ws_col;
        if (ws.ws_row >= 15 && ws.ws_row <= 100) rows = ws.ws_row;
    }
    g_app.tui_cols = cols;
    g_app.tui_rows = rows;

    uint32_t *lfb = (uint32_t *)(vm->mem + DIMON64_VRAM_BASE);
    static char outbuf[262144];
    int pos = 0;
    pos += snprintf(outbuf + pos, sizeof(outbuf) - pos, "\033[H");
    int last_fr = -1, last_fg = -1, last_fb = -1;
    int last_br = -1, last_bg = -1, last_bb = -1;

    for (int ty = 0; ty < rows; ty++) {
        int py_top = (ty * 2) * DIMON64_LFB_HEIGHT / (rows * 2);
        int py_bot = (ty * 2 + 1) * DIMON64_LFB_HEIGHT / (rows * 2);
        if (py_top >= DIMON64_LFB_HEIGHT) py_top = DIMON64_LFB_HEIGHT - 1;
        if (py_bot >= DIMON64_LFB_HEIGHT) py_bot = DIMON64_LFB_HEIGHT - 1;

        uint32_t *row_top = &lfb[py_top * DIMON64_LFB_WIDTH];
        uint32_t *row_bot = &lfb[py_bot * DIMON64_LFB_WIDTH];

        for (int tx = 0; tx < cols; tx++) {
            int px = tx * DIMON64_LFB_WIDTH / cols;
            if (px >= DIMON64_LFB_WIDTH) px = DIMON64_LFB_WIDTH - 1;

            uint32_t c_top = row_top[px];
            uint32_t c_bot = row_bot[px];

            int fr = (c_top >> 16) & 0xFF;
            int fg = (c_top >> 8) & 0xFF;
            int fb = c_top & 0xFF;

            int br = (c_bot >> 16) & 0xFF;
            int bg = (c_bot >> 8) & 0xFF;
            int bb = c_bot & 0xFF;

            if (fr != last_fr || fg != last_fg || fb != last_fb ||
                br != last_br || bg != last_bg || bb != last_bb) {
                pos += snprintf(outbuf + pos, sizeof(outbuf) - pos,
                                "\033[38;2;%d;%d;%dm\033[48;2;%d;%d;%dm",
                                fr, fg, fb, br, bg, bb);
                last_fr = fr; last_fg = fg; last_fb = fb;
                last_br = br; last_bg = bg; last_bb = bb;
            }

            /* UTF-8 for ▀ is \xE2\x96\x80 */
            if (pos + 4 < (int)sizeof(outbuf)) {
                outbuf[pos++] = (char)0xE2;
                outbuf[pos++] = (char)0x96;
                outbuf[pos++] = (char)0x80;
            }
        }
        if (ty < rows - 1 && pos + 4 < (int)sizeof(outbuf)) {
            outbuf[pos++] = '\r';
            outbuf[pos++] = '\n';
        }
    }
    ssize_t written = write(STDOUT_FILENO, outbuf, (size_t)pos);
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
                            int term_cols = g_app.tui_cols > 0 ? g_app.tui_cols : 80;
                            int term_rows = g_app.tui_rows > 0 ? g_app.tui_rows : 25;
                            int lfb_x = cx * DIMON64_LFB_WIDTH / term_cols;
                            int lfb_y = cy * DIMON64_LFB_HEIGHT / term_rows;
                            if (lfb_x >= 0 && lfb_x < DIMON64_LFB_WIDTH && lfb_y >= 0 && lfb_y < DIMON64_LFB_HEIGHT) {
                                if (type_ch == 'm') {
                                    vm_event_push_ext(vm, EVT_MOUSE_RELEASE, (uint16_t)lfb_x, (uint16_t)lfb_y,
                                                      btn == 2 ? 2 : 1);
                                } else if (type_ch == 'M') {
                                    if (btn == 0) {
                                        vm_event_push_ext(vm, EVT_MOUSE_CLICK, (uint16_t)lfb_x, (uint16_t)lfb_y, 1);
                                    } else if (btn == 2) {
                                        vm_event_push_ext(vm, EVT_MOUSE_CLICK, (uint16_t)lfb_x, (uint16_t)lfb_y, 2);
                                    } else if (btn == 35 || btn == 32) {
                                        vm_event_push_ext(vm, EVT_MOUSE_MOVE, (uint16_t)lfb_x, (uint16_t)lfb_y, (btn == 32) ? 1 : 0);
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
        g_app.scale = (scr_h >= 1200) ? 2 : 1;
    }

    g_app.win_w = DIMON64_LFB_WIDTH * g_app.scale;
    g_app.win_h = DIMON64_LFB_HEIGHT * g_app.scale;
    g_app.mouse_x = DIMON64_LFB_WIDTH / 2;
    g_app.mouse_y = DIMON64_LFB_HEIGHT / 2;

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

    XStoreName(g_app.dpy, g_app.win, "DimonOS-64 Modern TrueColor GUI");

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
    if (!g_app.dpy || !g_app.pixels || !vm || !vm->mem) return;

    int scale = g_app.scale;
    int win_w = g_app.win_w;
    int win_h = g_app.win_h;
    uint32_t *lfb = (uint32_t *)(vm->mem + DIMON64_VRAM_BASE);

    if (scale == 1) {
        memcpy(g_app.pixels, lfb, (size_t)DIMON64_LFB_WIDTH * DIMON64_LFB_HEIGHT * sizeof(uint32_t));
    } else {
        for (int y = 0; y < DIMON64_LFB_HEIGHT; y++) {
            uint32_t *src_row = &lfb[y * DIMON64_LFB_WIDTH];
            for (int sy = 0; sy < scale; sy++) {
                uint32_t *dst_row = &g_app.pixels[(y * scale + sy) * win_w];
                for (int x = 0; x < DIMON64_LFB_WIDTH; x++) {
                    uint32_t c = src_row[x];
                    for (int sx = 0; sx < scale; sx++) {
                        dst_row[x * scale + sx] = c;
                    }
                }
            }
        }
    }

    /* Composite hardware/32-bit mouse pointer cursor arrow */
    int mx = g_app.mouse_x * scale;
    int my = g_app.mouse_y * scale;
    for (int cy = 0; cy < 19 * scale; cy++) {
        int py = my + cy;
        if (py < 0 || py >= win_h) continue;
        int row = cy / scale;
        for (int cx = 0; cx < 12 * scale; cx++) {
            int px = mx + cx;
            if (px < 0 || px >= win_w) continue;
            int col = cx / scale;
            char ch = cursor_arrow[row][col];
            if (ch == 'X') {
                g_app.pixels[py * win_w + px] = 0xFF000000;
            } else if (ch == '.') {
                g_app.pixels[py * win_w + px] = 0xFFFFFFFF;
            }
        }
    }

    XPutImage(g_app.dpy, g_app.win, g_app.gc, g_app.ximage, 0, 0, 0, 0, win_w, win_h);
    XFlush(g_app.dpy);
}

static int s_last_motion_x = -1;
static int s_last_motion_y = -1;
static uint8_t s_last_motion_btn = 0xFF;
static uint32_t s_last_motion_time = 0;

static void x11_poll_events(VM *vm) {
    if (!g_app.dpy) return;

    struct timeval tv;
    gettimeofday(&tv, NULL);
    uint32_t now = (uint32_t)(tv.tv_sec * 1000 + tv.tv_usec / 1000);

    while (XPending(g_app.dpy) > 0) {
        XEvent ev;
        XNextEvent(g_app.dpy, &ev);

        if (ev.type == ClientMessage) {
            if ((Atom)ev.xclient.data.l[0] == g_app.wm_delete_window) {
                vm->halted = 1;
                return;
            }
        } else if (ev.type == ButtonPress) {
            int cx = ev.xbutton.x / g_app.scale;
            int cy = ev.xbutton.y / g_app.scale;
            if (cx >= 0 && cx < DIMON64_LFB_WIDTH && cy >= 0 && cy < DIMON64_LFB_HEIGHT) {
                g_app.mouse_x = cx;
                g_app.mouse_y = cy;
                uint8_t btn = (ev.xbutton.button == Button3) ? 2 : 1;
                s_last_motion_x = cx;
                s_last_motion_y = cy;
                s_last_motion_btn = btn;
                s_last_motion_time = now;
                input_log("x11", "press", cx, cy, btn);
                vm_event_push_ext(vm, EVT_MOUSE_CLICK, (uint16_t)cx, (uint16_t)cy, btn);
            }
        } else if (ev.type == ButtonRelease) {
            s_last_motion_btn = 0;
            int cx = ev.xbutton.x / g_app.scale, cy = ev.xbutton.y / g_app.scale;
            uint8_t btn = (ev.xbutton.button == Button3) ? 2 : 1;
            input_log("x11", "release", cx, cy, btn);
            if (cx >= 0 && cx < DIMON64_LFB_WIDTH && cy >= 0 && cy < DIMON64_LFB_HEIGHT)
                vm_event_push_ext(vm, EVT_MOUSE_RELEASE, (uint16_t)cx, (uint16_t)cy, btn);
        } else if (ev.type == MotionNotify) {
            int cx = ev.xmotion.x / g_app.scale;
            int cy = ev.xmotion.y / g_app.scale;
            if (cx >= 0 && cx < DIMON64_LFB_WIDTH && cy >= 0 && cy < DIMON64_LFB_HEIGHT) {
                g_app.mouse_x = cx;
                g_app.mouse_y = cy;
                uint8_t btn = 0;
                if (ev.xmotion.state & Button1Mask) btn = 1;
                else if (ev.xmotion.state & Button3Mask) btn = 2;

                /* Throttle redundant motion coordinates */
                if (cx == s_last_motion_x && cy == s_last_motion_y && btn == s_last_motion_btn) {
                    continue;
                }

                /* If no button pressed, throttle idle pointer movement (~30 FPS or >= 2px move) */
                if (btn == 0) {
                    int dx = cx - s_last_motion_x;
                    int dy = cy - s_last_motion_y;
                    if ((dx * dx + dy * dy < 4) && (now - s_last_motion_time < 33)) {
                        continue;
                    }
                }

                s_last_motion_x = cx;
                s_last_motion_y = cy;
                s_last_motion_btn = btn;
                s_last_motion_time = now;
                input_log("x11", "move", cx, cy, btn);
                vm_event_push_ext(vm, EVT_MOUSE_MOVE, (uint16_t)cx, (uint16_t)cy, btn);
            }
        } else if (ev.type == KeyPress) {
            KeySym ks;
            char str[32];
            int n = XLookupString(&ev.xkey, str, sizeof(str) - 1, &ks, NULL);
            uint16_t code = 0;
            uint8_t modifiers = 0;
            if (ev.xkey.state & ShiftMask) modifiers |= KEYMOD_SHIFT;
            if (ev.xkey.state & ControlMask) modifiers |= KEYMOD_CTRL;
            if (ev.xkey.state & Mod1Mask) modifiers |= KEYMOD_ALT;
            if (ev.xkey.state & Mod4Mask) modifiers |= KEYMOD_META;
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
                case XK_Home:      code = KEY_HOME; break;
                case XK_End:       code = KEY_END; break;
                case XK_Delete:    code = KEY_DELETE; break;
                case XK_Page_Up:   code = KEY_PGUP; break;
                case XK_Page_Down: code = KEY_PGDN; break;
                case XK_Return:
                case XK_KP_Enter:  code = 13; break;
                case XK_BackSpace: code = 8; break;
                case XK_Tab:       code = 9; break;
                case XK_Escape:    code = 27; break;
                default:
                    if (n == 1) code = (uint8_t)str[0];
                    break;
            }
            if (code > 0) {
                vm_event_push_mod(vm, EVT_KEY, code, 0, 0, modifiers);
            } else if (n > 0) {
                /* XLookupString returns bytes in the active locale. Deliver a
                   UTF-8 sequence byte-for-byte; editors retain valid UTF-8. */
                for (int i = 0; i < n; i++)
                    vm_event_push_mod(vm, EVT_KEY, (uint8_t)str[i], 0, 0, modifiers);
            }
        } else if (ev.type == Expose) {
            x11_flush_screen(g_app.vm);
        }
    }

    if (now - g_app.last_timer_ms >= 50) {
        g_app.last_timer_ms = now;
        vm_event_push(vm, EVT_TIMER, 0, 0);
    }
}

static void headless_poll_events(VM *vm) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    uint32_t now = (uint32_t)(tv.tv_sec * 1000 + tv.tv_usec / 1000);
    if (now - g_app.last_timer_ms >= 50) {
        g_app.last_timer_ms = now;
        vm_event_push(vm, EVT_TIMER, 0, 0);
    }
}

/* Ensure display backend is initialized immediately on first GUI access */
static void ensure_gui_init(GuiApp *app) {
    if (app->mode == 0) {
        if (app->req_mode == 3) {
            app->mode = 3;
            return;
        }
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
    } else if (app->mode == 3) {
        headless_poll_events(app->vm);
    }
}

static void on_gui_flush(void *userdata) {
    GuiApp *app = (GuiApp *)userdata;
    ensure_gui_init(app);

    if (app->mode == 1) {
        x11_flush_screen(app->vm);
    } else if (app->mode == 2) {
        tui_flush_screen(app->vm);
    } else if (app->mode == 3) {
        (void)0; /* headless: VRAM stays in memory, no display output */
    }
    if (g_stop_when_idle && g_inject_event_count > 0 &&
        app->vm->event_head == app->vm->event_tail) {
        /* Stop immediately after this complete frame, never halfway through
           composition. vm_step will hit -100 at the next instruction. */
        g_idle_completed = 1;
        app->vm->max_steps = app->vm->steps + 1;
    }
}

/* --- Debugger and hexdump --- */
static void usage(const char *p) {
    fprintf(stderr,
        "Usage: %s [program.bin] [options]\n"
        "  %s --iso image.iso [options]         Boot from ISO/disk image (sector 0)\n"
        "  %s program.bin --iso image.iso       Program + attached disk\n"
        "Options:\n"
        "  -g, --gui             Force X11 graphical window (also: 'gui' positional)\n"
        "  -tui, --tui           Force ANSI terminal console mode (TUI) (also: 'tui' positional)\n"
        "  -H, --headless        Memory-only GUI (no display output, for automated tests)\n"
        "  --dump-vram FILE      Write 800x600x4 ARGB VRAM to FILE on exit\n"
        "  --dump-state FILE     Write bounded-run VM/event state as JSON\n"
        "  --input-log FILE      Log bounded backend input conversion diagnostics\n"
        "  --stop-when-idle      Stop after injected input drains and a frame flushes\n"
        "  --inject-keys SPEC    Push scripted key events at startup (tests)\n"
        "  --inject-click X,Y[;...] Push scripted mouse clicks (tests)\n"
        "  --inject-events SPEC  Ordered k:/p:/m:/r: events through normal input\n"
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
        "Syscalls (a7): 0-5 console, 6-8 disk, 10-15 GUI, 16-24 multitasking/timer\n",
        p, p, p, DISK_MAX_SECTORS);
}

static uint64_t parse_u64(const char *s) {
    return (uint64_t)strtoull(s, NULL, 0);
}

static void hexdump(VM *vm, uint64_t addr, int n) {
    for (int i = 0; i < n; i += 16) {
        printf("%08" PRIX64 ": ", (uint64_t)(addr + (uint64_t)i));
        for (int j = 0; j < 16 && i + j < n; j++)
            printf("%02X ", vm->mem[addr + (uint64_t)i + (uint64_t)j]);
        printf(" | ");
        for (int j = 0; j < 16 && i + j < n; j++) {
            uint8_t c = vm->mem[addr + (uint64_t)i + (uint64_t)j];
            putchar(c >= 32 && c < 127 ? c : '.');
        }
        putchar('\n');
    }
}

#define MAX_BP 16
static uint64_t bps[MAX_BP];
static int nbp = 0;

static int at_bp(uint64_t pc) {
    for (int i = 0; i < nbp; i++) if (bps[i] == pc) return 1;
    return 0;
}

static void debugger(VM *vm) {
    char line[256];
    printf("=== DimonVirtualCPU-64 debugger ===\n"
           "Commands: s=step  c=continue  r=regs  m ADDR [N]=dump  "
           "u ADDR [N]=disasm  b ADDR=break  q=quit  h=help\n");
    for (;;) {
        char dasm[128];
        dimon64_disasm(vm, vm->pc, dasm, sizeof(dasm));
        printf("[%08" PRIX64 "] %-28s > ", vm->pc, dasm);
        fflush(stdout);
        if (!fgets(line, sizeof(line), stdin)) break;
        char cmd = 0;
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
                dimon64_disasm(vm, vm->pc, dasm, sizeof(dasm));
                printf("  %08" PRIX64 ": %s\n", vm->pc, dasm);
                int rc = vm_step(vm);
                if (rc == 1) { printf("[HLT]\n"); vm_dump_regs(vm, stdout); return; }
                if (rc != 0) { printf("[ERROR %d]\n", rc); return; }
            }
            vm_dump_regs(vm, stdout);
        } else if (cmd == 'c') {
            for (;;) {
                if (at_bp(vm->pc)) { printf("[BREAK @ %08" PRIX64 "]\n", vm->pc); break; }
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
            unsigned long long aa64 = 0;
            if (sscanf(line + 1, "%llx %d", &aa64, &n) < 1) continue;
            hexdump(vm, (uint64_t)aa64, n);
        } else if (cmd == 'u') {
            uint64_t p = vm->pc;
            int n = 8;
            char *rest = line + 1;
            while (*rest == ' ' || *rest == '\t') rest++;
            if (*rest && *rest != '\n') {
                unsigned long long aa; int nn;
                if (sscanf(rest, "%llx %d", &aa, &nn) == 2) { p = (uint64_t)aa; n = nn; }
                else if (sscanf(rest, "%llx", &aa) == 1) {
                    p = (uint64_t)aa; n = 8;
                }
            }
            for (int i = 0; i < n; i++) {
                int len = dimon64_disasm(vm, p, dasm, sizeof(dasm));
                printf("  %08" PRIX64 ": %-28s |", p, dasm);
                for (int k = 0; k < len; k++) printf(" %02X", vm->mem[p + (uint64_t)k]);
                printf("\n");
                p += (uint64_t)len;
            }
        } else if (cmd == 'b') {
            char *rest = line + 1;
            while (*rest == ' ' || *rest == '\t') rest++;
            if (*rest == 'c') { nbp = 0; printf("Cleared breakpoints\n"); }
            else if (!*rest || *rest == '\n') {
                printf("Breakpoints (%d):\n", nbp);
                for (int i = 0; i < nbp; i++) printf("  #%d @ %08" PRIX64 "\n", i, bps[i]);
            } else {
                unsigned long long aa;
                if (sscanf(rest, "%llx", &aa) == 1 && nbp < MAX_BP) {
                    bps[nbp++] = (uint64_t)aa;
                    printf("Added break @ %08llX\n", aa & 0xFFFFFFFFULL);
                }
            }
        } else {
            printf("Unknown command (h=help)\n");
        }
    }
}

int main(int argc, char **argv) {
    (void)setlocale(LC_CTYPE, "");
    if (argc < 2) { usage(argv[0]); return 1; }
    const char *path = NULL;
    const char *iso_path = NULL;
    uint64_t start = 0, load = 0;
    int debug = 0, trace = 0, dumpregs = 0, writable = 1;
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
        else if (!strcmp(argv[i], "-H") || !strcmp(argv[i], "--headless")) g_app.req_mode = 3;
        else if (!strcmp(argv[i], "--dump-vram") && i + 1 < argc) g_dump_vram_path = argv[++i];
        else if (!strcmp(argv[i], "--dump-state") && i + 1 < argc) g_dump_state_path = argv[++i];
        else if (!strcmp(argv[i], "--input-log") && i + 1 < argc) g_input_log_path = argv[++i];
        else if (!strcmp(argv[i], "--stop-when-idle")) g_stop_when_idle = 1;
        else if (!strcmp(argv[i], "--inject-keys") && i + 1 < argc) g_inject_keys = argv[++i];
        else if (!strcmp(argv[i], "--inject-events") && i + 1 < argc) {
            if (parse_inject_events(argv[++i]) != 0) {
                fprintf(stderr, "Invalid --inject-events specification\n");
                return 1;
            }
        }
        else if (!strcmp(argv[i], "--inject-click") && i + 1 < argc) {
            const char *spec = argv[++i];
            int x = 0, y = 0;
            while (*spec) {
                while (*spec == ' ' || *spec == ';' || *spec == ',') {
                    if (*spec == ',') break;
                    spec++;
                }
                if (sscanf(spec, "%d,%d", &x, &y) == 2) {
                    if (x >= 0 && x < DIMON64_LFB_WIDTH && y >= 0 && y < DIMON64_LFB_HEIGHT && g_click_count < 64) {
                        g_click_x[g_click_count] = x;
                        g_click_y[g_click_count] = y;
                        g_click_count++;
                    }
                    while (*spec && *spec != ';') spec++;
                } else break;
            }
        }
        else if (!strcmp(argv[i], "--scale") && i + 1 < argc) g_app.scale = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--disk-writable")) writable = 1;
        else if (!strcmp(argv[i], "--disk-readonly")) writable = 0;
        else if ((!strcmp(argv[i], "-i") || !strcmp(argv[i], "--iso") ||
                  !strcmp(argv[i], "--disk")) && i + 1 < argc) iso_path = argv[++i];
        else if (!strcmp(argv[i], "-s") && i + 1 < argc) { start = parse_u64(argv[++i]); have_start = 1; }
        else if (!strcmp(argv[i], "-l") && i + 1 < argc) load = parse_u64(argv[++i]);
        else if (!strcmp(argv[i], "-m") && i + 1 < argc) maxsteps = strtoull(argv[++i], NULL, 0);
        else if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) { usage(argv[0]); return 0; }
        else if (argv[i][0] == '-') { usage(argv[0]); return 1; }
        else if (!strcmp(argv[i], "tui")) { if (g_app.req_mode == -1) g_app.req_mode = 2; else { usage(argv[0]); return 1; } }
        else if (!strcmp(argv[i], "gui")) { if (g_app.req_mode == -1) g_app.req_mode = 1; else { usage(argv[0]); return 1; } }
        else if (!strcmp(argv[i], "headless")) { if (g_app.req_mode == -1) g_app.req_mode = 3; else { usage(argv[0]); return 1; } }
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
    vm.psg_userdata = &g_app;
    vm.psg_play_cb = on_psg_play;
    atexit(audio_cleanup);

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
    if (g_input_log_path) {
        g_input_log = fopen(g_input_log_path, "w");
        if (!g_input_log) {
            perror("fopen --input-log");
            vm_free(&vm);
            return 1;
        }
    }
    if (g_inject_keys) inject_keys(&vm, g_inject_keys);
    for (int ci = 0; ci < g_click_count; ci++)
        vm_event_push(&vm, EVT_MOUSE_CLICK, (uint16_t)g_click_x[ci], (uint16_t)g_click_y[ci]);
    inject_events(&vm);

    if (debug) {
        debugger(&vm);
        vm_free(&vm);
        return 0;
    }

    if (trace) {
        char dasm[128];
        while (!vm.halted) {
            dimon64_disasm(&vm, vm.pc, dasm, sizeof(dasm));
            printf("[%08" PRIX64 "] %s\n", (uint64_t)vm.pc, dasm);
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

    if (g_dump_vram_path) {
        FILE *df = fopen(g_dump_vram_path, "wb");
        if (!df) {
            perror("fopen --dump-vram");
        } else {
            size_t wn = fwrite(vm.mem + DIMON64_VRAM_BASE, 1, (size_t)DIMON64_VRAM_SIZE, df);
            if (wn != (size_t)DIMON64_VRAM_SIZE) perror("fwrite --dump-vram");
            fclose(df);
        }
    }

    if (g_dump_state_path) {
        FILE *sf = fopen(g_dump_state_path, "w");
        if (!sf) {
            perror("fopen --dump-state");
        } else {
            int pending = vm.event_tail - vm.event_head;
            if (pending < 0) pending += VM_EVENT_QUEUE_SIZE;
            fprintf(sf,
                    "{\"halted\":%u,\"steps\":%llu,\"pc\":%llu,"
                    "\"pending_events\":%d,\"run_result\":%d,"
                    "\"idle_completed\":%d}\n",
                    (unsigned)vm.halted, (unsigned long long)vm.steps,
                    (unsigned long long)vm.pc, pending, rc, g_idle_completed);
            fclose(sf);
        }
    }

    if (dumpregs) vm_dump_regs(&vm, stderr);
    if (g_input_log) {
        fclose(g_input_log);
        g_input_log = NULL;
    }
    vm_free(&vm);
    return 0;
}
