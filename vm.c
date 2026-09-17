/* DimonVirtualCPU-64: 64-bit RISC-V style virtual machine.
 * All code, comments and identifiers are in English.
 */
#include "dimon64.h"
#include "font8x16.h"
#include "dimonfs.h"

#ifndef BAREMETAL
#include <inttypes.h>
#include <time.h>
#include <sys/time.h>
#else
extern uint32_t kernel_get_ticks_ms(void);
extern void baremetal_putchar(char c);
#ifndef PRIu64
#define PRIu64 "llu"
#endif
#ifndef PRIX64
#define PRIX64 "llX"
#endif
#ifndef PRId64
#define PRId64 "lld"
#endif
#endif

static uint64_t host_time_ms(void) {
#ifdef BAREMETAL
    return (uint64_t)kernel_get_ticks_ms();
#else
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint64_t)tv.tv_sec * 1000ULL + (uint64_t)tv.tv_usec / 1000ULL;
#endif
}

static uint32_t load_le32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

/* ---------- register names ---------- */
static const char *abi_names[32] = {
    "zero", "ra", "sp", "gp", "tp", "t0", "t1", "t2",
    "s0", "s1", "a0", "a1", "a2", "a3", "a4", "a5",
    "a6", "a7", "s2", "s3", "s4", "s5", "s6", "s7",
    "s8", "s9", "s10", "s11", "t3", "t4", "t5", "t6"
};

const char *dimon64_reg_abi(int r) {
    if (r < 0 || r > 31) return "??";
    return abi_names[r];
}

const char *dimon64_reg_name(int r) {
    return dimon64_reg_abi(r);
}

const char *reg_name(int r) {
    return dimon64_reg_abi(r);
}

/* ---------- events / GUI ---------- */
void vm_event_push_mod(VM *vm, uint8_t type, uint16_t code, uint16_t data,
                       uint8_t button, uint8_t modifiers) {
    if (!vm) return;
    int next = (vm->event_tail + 1) % VM_EVENT_QUEUE_SIZE;
    if (next != vm->event_head) {
        vm->event_queue[vm->event_tail].type = type;
        vm->event_queue[vm->event_tail].button = button;
        vm->event_queue[vm->event_tail].code = code;
        vm->event_queue[vm->event_tail].data = data;
        vm->event_queue[vm->event_tail].modifiers = modifiers;
        vm->event_tail = next;
    }
}

void vm_event_push_ext(VM *vm, uint8_t type, uint16_t code, uint16_t data, uint8_t button) {
    vm_event_push_mod(vm, type, code, data, button, 0);
}

void vm_event_push(VM *vm, uint8_t type, uint16_t code, uint16_t data) {
    vm_event_push_ext(vm, type, code, data, (type == EVT_MOUSE_CLICK) ? 1 : 0);
}

int vm_event_pop_ext(VM *vm, uint8_t *type, uint16_t *code, uint16_t *data, uint8_t *button) {
    if (!vm) return 0;
    if (vm->event_head == vm->event_tail) {
        *type = EVT_NONE; *code = 0; *data = 0; if (button) *button = 0;
        return 0;
    }
    *type = vm->event_queue[vm->event_head].type;
    *code = vm->event_queue[vm->event_head].code;
    *data = vm->event_queue[vm->event_head].data;
    if (button) *button = vm->event_queue[vm->event_head].button;
    vm->event_queue[vm->event_head].type = EVT_NONE;
    vm->event_queue[vm->event_head].code = 0;
    vm->event_queue[vm->event_head].data = 0;
    vm->event_queue[vm->event_head].button = 0;
    vm->event_queue[vm->event_head].modifiers = 0;
    vm->event_head = (vm->event_head + 1) % VM_EVENT_QUEUE_SIZE;
    return 1;
}

int vm_event_pop(VM *vm, uint8_t *type, uint16_t *code, uint16_t *data) {
    return vm_event_pop_ext(vm, type, code, data, NULL);
}

void vm_gui_draw_pixel(VM *vm, int x, int y, uint32_t color32) {
    if (!vm || !vm->mem) return;
    if (vm->draw_context_active) {
        x += vm->draw_offset_x; y += vm->draw_offset_y;
        if (x < vm->draw_clip_x || y < vm->draw_clip_y ||
            x >= vm->draw_clip_x + vm->draw_clip_w || y >= vm->draw_clip_y + vm->draw_clip_h) return;
    }
    if (x < 0 || x >= DIMON64_LFB_WIDTH || y < 0 || y >= DIMON64_LFB_HEIGHT) return;
    uint64_t addr = DIMON64_VRAM_BASE + (uint64_t)(y * DIMON64_LFB_WIDTH + x) * 4ULL;
    if (addr + 4 > vm->memsize) return;
    *(uint32_t *)(vm->mem + addr) = color32;
    vm->gui_dirty = 1;
}

void vm_gui_fill_rect(VM *vm, int x, int y, int w, int h, uint32_t color32) {
    if (!vm || !vm->mem || w <= 0 || h <= 0) return;
    if (vm->draw_context_active) { x += vm->draw_offset_x; y += vm->draw_offset_y; }
    int x0 = x < 0 ? 0 : x;
    int y0 = y < 0 ? 0 : y;
    int x1 = x + w;
    int y1 = y + h;
    if (x1 > DIMON64_LFB_WIDTH) x1 = DIMON64_LFB_WIDTH;
    if (y1 > DIMON64_LFB_HEIGHT) y1 = DIMON64_LFB_HEIGHT;
    if (vm->draw_context_active) {
        if (x0 < vm->draw_clip_x) x0 = vm->draw_clip_x;
        if (y0 < vm->draw_clip_y) y0 = vm->draw_clip_y;
        if (x1 > vm->draw_clip_x + vm->draw_clip_w) x1 = vm->draw_clip_x + vm->draw_clip_w;
        if (y1 > vm->draw_clip_y + vm->draw_clip_h) y1 = vm->draw_clip_y + vm->draw_clip_h;
    }
    if (x0 >= x1 || y0 >= y1) return;

    for (int cy = y0; cy < y1; cy++) {
        uint64_t row_addr = DIMON64_VRAM_BASE + (uint64_t)(cy * DIMON64_LFB_WIDTH + x0) * 4ULL;
        if (row_addr + (uint64_t)(x1 - x0) * 4ULL > vm->memsize) continue;
        uint32_t *p = (uint32_t *)(vm->mem + row_addr);
        for (int cx = 0; cx < x1 - x0; cx++) {
            p[cx] = color32;
        }
    }
    vm->gui_dirty = 1;
}

void vm_gui_draw_line(VM *vm, int x0, int y0, int x1, int y1, uint32_t color32) {
    if (!vm || !vm->mem) return;
    int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    while (1) {
        vm_gui_draw_pixel(vm, x0, y0, color32);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

void vm_gui_draw_string(VM *vm, int x, int y, const char *text, uint32_t fg, uint32_t bg) {
    if (!vm || !vm->mem || !text) return;
    if (vm->draw_context_active) { x += vm->draw_offset_x; y += vm->draw_offset_y; }
    int cur_x = x;
    int draw_bg = ((bg & 0xFF000000) != 0);

    while (*text) {
        uint32_t cp = (uint8_t)*text++;
        if ((cp & 0xe0u) == 0xc0u && ((uint8_t)*text & 0xc0u) == 0x80u) {
            cp = ((cp & 0x1fu) << 6) | ((uint8_t)*text++ & 0x3fu);
        } else if ((cp & 0xf0u) == 0xe0u && ((uint8_t)text[0] & 0xc0u) == 0x80u &&
                   ((uint8_t)text[1] & 0xc0u) == 0x80u) {
            uint8_t continuation1 = (uint8_t)text[0];
            uint8_t continuation2 = (uint8_t)text[1];
            text += 2;
            cp = ((cp & 0x0fu) << 12) | ((continuation1 & 0x3fu) << 6) |
                 (continuation2 & 0x3fu);
        }
        static const uint16_t polish_cp[18] = {
            0x0104,0x0105,0x0106,0x0107,0x0118,0x0119,0x0141,0x0142,0x0143,
            0x0144,0x00d3,0x00f3,0x015a,0x015b,0x0179,0x017a,0x017b,0x017c
        };
        const uint8_t *glyph = NULL;
        for (int gi = 0; gi < 18; gi++) if (cp == polish_cp[gi]) { glyph = font8x16_polish[gi]; break; }
        if (!glyph) glyph = font8x16[cp <= 255u ? cp : (uint32_t)'?'];
        if (cur_x + 8 > 0 && cur_x < DIMON64_LFB_WIDTH && y + 16 > 0 && y < DIMON64_LFB_HEIGHT) {
            for (int r = 0; r < 16; r++) {
                int py = y + r;
                if (py < 0 || py >= DIMON64_LFB_HEIGHT) continue;
                uint8_t bits = glyph[r];
                for (int c = 0; c < 8; c++) {
                    int px = cur_x + c;
                    if (px < 0 || px >= DIMON64_LFB_WIDTH) continue;
                    if (vm->draw_context_active &&
                        (px < vm->draw_clip_x || py < vm->draw_clip_y ||
                         px >= vm->draw_clip_x + vm->draw_clip_w ||
                         py >= vm->draw_clip_y + vm->draw_clip_h)) continue;
                    if (bits & (0x80 >> c)) {
                        uint64_t addr = DIMON64_VRAM_BASE + (uint64_t)(py * DIMON64_LFB_WIDTH + px) * 4ULL;
                        if (addr + 4 <= vm->memsize) *(uint32_t *)(vm->mem + addr) = fg;
                    } else if (draw_bg) {
                        uint64_t addr = DIMON64_VRAM_BASE + (uint64_t)(py * DIMON64_LFB_WIDTH + px) * 4ULL;
                        if (addr + 4 <= vm->memsize) *(uint32_t *)(vm->mem + addr) = bg;
                    }
                }
            }
        }
        cur_x += 8;
        if (cur_x >= DIMON64_LFB_WIDTH) break;
    }
    vm->gui_dirty = 1;
}

static size_t utf8_sequence_length(const unsigned char *s) {
    if (!s[0]) return 0;
    if (s[0] < 0x80) return 1;
    if ((s[0] & 0xe0) == 0xc0 && (s[1] & 0xc0) == 0x80) return 2;
    if ((s[0] & 0xf0) == 0xe0 && (s[1] & 0xc0) == 0x80 && (s[2] & 0xc0) == 0x80) return 3;
    if ((s[0] & 0xf8) == 0xf0 && (s[1] & 0xc0) == 0x80 &&
        (s[2] & 0xc0) == 0x80 && (s[3] & 0xc0) == 0x80) return 4;
    return 1;
}

static int utf8_text_width(const char *s) {
    int glyphs = 0;
    while (s && *s) { size_t n = utf8_sequence_length((const unsigned char *)s); s += n; glyphs++; }
    return glyphs * 8;
}

static void vm_gui_draw_string_fit(VM *vm, int x, int y, const char *text,
                                   uint32_t fg, uint32_t bg, int max_width) {
    if (!text || max_width <= 0) return;
    int max_glyphs = max_width / 8;
    if (max_glyphs <= 0) return;
    char fitted[256]; size_t used = 0;
    if (utf8_text_width(text) <= max_width) {
        while (text[used] && used + 1 < sizeof(fitted)) { fitted[used] = text[used]; used++; }
    } else {
        int keep = max_glyphs > 3 ? max_glyphs - 3 : max_glyphs;
        const unsigned char *p = (const unsigned char *)text;
        for (int glyph = 0; glyph < keep && *p; glyph++) {
            size_t n = utf8_sequence_length(p);
            if (used + n >= sizeof(fitted)) break;
            memcpy(fitted + used, p, n); used += n; p += n;
        }
        if (max_glyphs > 3 && used + 3 < sizeof(fitted)) {
            fitted[used++] = '.'; fitted[used++] = '.'; fitted[used++] = '.';
        }
    }
    fitted[used] = 0;
    int old_active = vm->draw_context_active;
    int old_x = vm->draw_clip_x, old_y = vm->draw_clip_y;
    int old_w = vm->draw_clip_w, old_h = vm->draw_clip_h;
    int screen_x = x + (old_active ? vm->draw_offset_x : 0);
    int screen_y = y + (old_active ? vm->draw_offset_y : 0);
    int nx0 = screen_x, ny0 = screen_y, nx1 = screen_x + max_width, ny1 = screen_y + 16;
    if (old_active) {
        if (nx0 < old_x) nx0 = old_x;
        if (ny0 < old_y) ny0 = old_y;
        if (nx1 > old_x + old_w) nx1 = old_x + old_w;
        if (ny1 > old_y + old_h) ny1 = old_y + old_h;
    }
    vm->draw_clip_x = nx0; vm->draw_clip_y = ny0;
    vm->draw_clip_w = nx1 > nx0 ? nx1 - nx0 : 0;
    vm->draw_clip_h = ny1 > ny0 ? ny1 - ny0 : 0; vm->draw_context_active = 1;
    vm_gui_draw_string(vm, x, y, fitted, fg, bg);
    vm->draw_context_active = (uint8_t)old_active;
    vm->draw_clip_x = old_x; vm->draw_clip_y = old_y; vm->draw_clip_w = old_w; vm->draw_clip_h = old_h;
}

void vm_gui_draw_rect(VM *vm, int x, int y, int w, int h, uint8_t ch, uint8_t attr) {
    (void)ch;
    static const uint32_t pal[16] = {
        0xFF000000, 0xFF0000AA, 0xFF00AA00, 0xFF00AAAA,
        0xFFAA0000, 0xFFAA00AA, 0xFFAA5500, 0xFFAAAAAA,
        0xFF555555, 0xFF5555FF, 0xFF55FF55, 0xFF55FFFF,
        0xFFFF5555, 0xFFFF55FF, 0xFFFFFF55, 0xFFFFFFFF
    };
    uint32_t bg = pal[(attr >> 4) & 0x0F];
    vm_gui_fill_rect(vm, x * 8, y * 16, w * 8, h * 16, bg);
}

void vm_gui_draw_text(VM *vm, int x, int y, const char *text, uint8_t attr) {
    static const uint32_t pal[16] = {
        0xFF000000, 0xFF0000AA, 0xFF00AA00, 0xFF00AAAA,
        0xFFAA0000, 0xFFAA00AA, 0xFFAA5500, 0xFFAAAAAA,
        0xFF555555, 0xFF5555FF, 0xFF55FF55, 0xFF55FFFF,
        0xFFFF5555, 0xFFFF55FF, 0xFFFFFF55, 0xFFFFFFFF
    };
    uint32_t fg = pal[attr & 0x0F];
    uint32_t bg = pal[(attr >> 4) & 0x0F];
    vm_gui_draw_string(vm, x * 8, y * 16, text, fg, bg);
}

/* ---------- lifecycle ---------- */
void vm_init(VM *vm) {
    if (!vm) return;
    memset(vm, 0, sizeof(*vm));
    vm->memsize = DIMON64_MEM_SIZE;
#ifndef BAREMETAL
    vm->mem = (uint8_t *)calloc(1, (size_t)vm->memsize);
#endif
    vm->timer_period = 500;
    vm->timer_ticks = 0;
    vm->timer_vector = 0;
    vm->psg_vol = 255;
    vm->flags = DIMON64_FLAG_IE;
    vm->cur_proc = 0;
    vm->next_pid = 1;
    for (int i = 0; i < DIMON64_MAX_PROCS; i++) {
        memset(&vm->procs[i], 0, sizeof(vm->procs[i]));
        vm->procs[i].state = DIMON64_PROC_FREE;
    }
    vm->procs[0].used = 1;
    vm->procs[0].state = DIMON64_PROC_RUNNING;
    vm->procs[0].pid = 0;
    vm->procs[0].stack_base = DIMON64_STACK_BASE;
    vm->procs[0].stack_size = DIMON64_STACK_SIZE;
    snprintf(vm->procs[0].name, sizeof(vm->procs[0].name), "init");
}

void vm_free(VM *vm) {
    if (!vm) return;
#ifndef BAREMETAL
    if (vm->disk_data) {
        free(vm->disk_data);
        vm->disk_data = NULL;
    }
#endif
    if (vm->mem) {
#ifndef BAREMETAL
        free(vm->mem);
#endif
        vm->mem = NULL;
    }
    vm->disk_sectors = 0;
    vm->memsize = 0;
}

void vm_disk_detach(VM *vm) {
    if (!vm) return;
#ifndef BAREMETAL
    if (vm->disk_data) { free(vm->disk_data); vm->disk_data = NULL; }
#endif
    vm->disk_sectors = 0;
    vm->disk_writable = 0;
    vm->disk_path[0] = 0;
}

#ifndef BAREMETAL
int vm_disk_attach(VM *vm, const char *path, int writable) {
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n < 0) { fclose(f); return -1; }
    if (n == 0 || (n % DISK_SECTOR_SIZE) != 0) { fclose(f); return -2; }
    uint32_t sectors = (uint32_t)(n / DISK_SECTOR_SIZE);
    if (sectors == 0 || sectors > DISK_MAX_SECTORS) { fclose(f); return -2; }
    uint8_t *buf = (uint8_t *)malloc((size_t)n);
    if (!buf) { fclose(f); return -3; }
    if (fread(buf, 1, (size_t)n, f) != (size_t)n) { fclose(f); free(buf); return -4; }
    fclose(f);
    vm_disk_detach(vm);
    vm->disk_data = buf;
    vm->disk_sectors = sectors;
    if (writable) {
        vm->disk_writable = 1;
    } else {
        FILE *tf = fopen(path, "r+b");
        if (tf) { vm->disk_writable = 1; fclose(tf); }
        else { vm->disk_writable = 0; }
    }
    strncpy(vm->disk_path, path, sizeof(vm->disk_path) - 1);
    return 0;
}
#endif

int vm_disk_boot(VM *vm, uint64_t load_addr) {
    if (!vm->disk_data || vm->disk_sectors < 1) return DISK_ERR_NODISK;
    if (load_addr + DISK_SECTOR_SIZE > vm->memsize) return DISK_ERR_RAM;
    memcpy(vm->mem + load_addr, vm->disk_data, DISK_SECTOR_SIZE);
    return DISK_ERR_NONE;
}

int vm_disk_read(VM *vm, uint32_t lba, uint64_t ram_addr, uint64_t count) {
    if (!vm->disk_data || vm->disk_sectors == 0) return DISK_ERR_NODISK;
    if (count == 0) return DISK_ERR_NONE;
    if (lba >= vm->disk_sectors) return DISK_ERR_RANGE;
    if ((uint64_t)lba + count > vm->disk_sectors) return DISK_ERR_RANGE;
    uint64_t bytes = count * DISK_SECTOR_SIZE;
    if (ram_addr + bytes > vm->memsize) return DISK_ERR_RAM;
    memcpy(vm->mem + ram_addr, vm->disk_data + (size_t)lba * DISK_SECTOR_SIZE, (size_t)bytes);
    return DISK_ERR_NONE;
}

int vm_disk_write(VM *vm, uint32_t lba, uint64_t ram_addr, uint64_t count) {
    if (!vm->disk_data || vm->disk_sectors == 0) return DISK_ERR_NODISK;
    if (!vm->disk_writable) return DISK_ERR_READONLY;
    if (count == 0) return DISK_ERR_NONE;
    if (lba >= vm->disk_sectors) return DISK_ERR_RANGE;
    if ((uint64_t)lba + count > vm->disk_sectors) return DISK_ERR_RANGE;
    uint64_t bytes = count * DISK_SECTOR_SIZE;
    if (ram_addr + bytes > vm->memsize) return DISK_ERR_RAM;
    memcpy(vm->disk_data + (size_t)lba * DISK_SECTOR_SIZE, vm->mem + ram_addr, (size_t)bytes);
#ifndef BAREMETAL
    if (vm->disk_path[0]) {
        FILE *f = fopen(vm->disk_path, "r+b");
        if (!f) return DISK_ERR_IO;
        if (fseek(f, (long)(lba * DISK_SECTOR_SIZE), SEEK_SET) != 0 ||
            fwrite(vm->disk_data + (size_t)lba * DISK_SECTOR_SIZE, 1, (size_t)bytes, f) != (size_t)bytes ||
            fflush(f) != 0) { fclose(f); return DISK_ERR_IO; }
        if (fclose(f) != 0) return DISK_ERR_IO;
    }
#endif
    return DISK_ERR_NONE;
}

int vm_load_buf(VM *vm, const uint8_t *buf, size_t len, uint64_t load_addr) {
    if (!vm || !vm->mem) return -1;
    if (load_addr + len > vm->memsize) return -1;
    memcpy(vm->mem + load_addr, buf, len);
    return 0;
}

#ifndef BAREMETAL
int vm_load(VM *vm, const char *path, uint64_t load_addr) {
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n < 0) { fclose(f); return -1; }
    if (load_addr + (uint64_t)n > vm->memsize) { fclose(f); return -1; }
    if (n > 0 && fread(vm->mem + load_addr, 1, (size_t)n, f) != (size_t)n) { fclose(f); return -1; }
    fclose(f);
    return 0;
}
#endif

void vm_reset(VM *vm, uint64_t start_pc) {
    if (!vm) return;
    vm->pc = start_pc;
    vm->halted = 0;
    vm->in_isr = 0;
    vm->steps = 0;
    vm->cycle_counter = 0;
    vm->switches = 0;
    vm->timer_ticks = 0;
    vm->timer_vector = 0;
    if (vm->timer_period < 10 || vm->timer_period > 1000000) vm->timer_period = 500;
    vm->psg_freq = 0;
    vm->psg_wave = 0;
    vm->psg_vol = 255;
    vm->psg_duration_ms = 0;
    vm->psg_end_time_ms = 0;
    memset(vm->regs, 0, sizeof(vm->regs));
    vm->flags = DIMON64_FLAG_IE;
    vm->epc = 0;
    vm->eflags = 0;
    vm->event_head = 0;
    vm->event_tail = 0;
    vm->event_owner_pid = 0;
    /* process 0 owns the boot context */
    for (int i = 0; i < DIMON64_MAX_PROCS; i++) {
        vm->procs[i].used = 0;
        vm->procs[i].state = DIMON64_PROC_FREE;
    }
    vm->cur_proc = 0;
    vm->next_pid = 1;
    vm->procs[0].used = 1;
    vm->procs[0].state = DIMON64_PROC_RUNNING;
    vm->procs[0].pid = 0;
    vm->procs[0].pc = start_pc;
    vm->procs[0].flags = vm->flags;
    memset(vm->procs[0].regs, 0, sizeof(vm->procs[0].regs));
    vm->procs[0].stack_base = DIMON64_STACK_BASE;
    vm->procs[0].stack_size = DIMON64_STACK_SIZE;
    vm->procs[0].sleep_until = 0;
    vm->procs[0].memory_base = 0;
    vm->procs[0].memory_size = DIMON64_MEM_SIZE;
    vm->procs[0].essential = 1;
    snprintf(vm->procs[0].name, sizeof(vm->procs[0].name), "init");
    uint64_t top = DIMON64_STACK_BASE + DIMON64_STACK_SIZE;
    top &= ~15ULL;
    vm->regs[2] = top; /* sp */
    vm->regs[4] = 0;   /* tp = proc index */
    vm->procs[0].regs[2] = top;
}

#ifndef BAREMETAL
void vm_dump_regs(VM *vm, FILE *out) {
    if (!vm || !out) return;
    fprintf(out, "PC=%016" PRIX64 " FLAGS=%016" PRIX64 " halted=%d steps=%" PRIu64
            " ticks=%" PRIu64 " switches=%" PRIu64 " proc=%d\n",
            vm->pc, vm->flags, vm->halted, vm->steps,
            vm->timer_ticks, vm->switches, vm->cur_proc);
    for (int i = 0; i < 32; i++) {
        fprintf(out, "%-4s(R%-2d)=%016" PRIX64 "  ", dimon64_reg_abi(i), i, vm->regs[i]);
        if (i % 4 == 3) fprintf(out, "\n");
    }
}
#endif

void vm_stb(VM *vm, uint64_t addr, uint8_t v) {
    if (!vm || !vm->mem) return;
    if (addr < vm->memsize) vm->mem[addr] = v;
}

uint8_t vm_ldb(VM *vm, uint64_t addr) {
    if (!vm || !vm->mem) return 0;
    if (addr < vm->memsize) return vm->mem[addr];
    return 0;
}

int vm_mem_read(VM *vm, uint64_t addr, void *out, size_t len) {
    if (!vm || !vm->mem || !out) return -1;
    if (len == 0) return 0;
    if (addr + len < addr || addr + len > vm->memsize) return -1;
    memcpy(out, vm->mem + addr, len);
    return 0;
}

int vm_mem_write(VM *vm, uint64_t addr, const void *in, size_t len) {
    if (!vm || !vm->mem || !in) return -1;
    if (len == 0) return 0;
    if (addr + len < addr || addr + len > vm->memsize) return -1;
    memcpy(vm->mem + addr, in, len);
    return 0;
}

static int guest_range_ok(VM *vm, uint64_t addr, uint64_t len) {
    if (!vm || addr + len < addr || addr + len > vm->memsize) return 0;
    if (vm->cur_proc < 0 || vm->cur_proc >= DIMON64_MAX_PROCS) return 0;
    Dimon64Proc *p = &vm->procs[vm->cur_proc];
    if (!p->used || !p->isolated) return 1;
    if (addr >= p->memory_base && addr + len <= p->memory_base + p->memory_size) return 1;
    if (addr >= p->stack_base && addr + len <= p->stack_base + p->stack_size) return 1;
    return 0;
}

/* ---------- low-level memory with MMIO ---------- */
static int read_u8(VM *vm, uint64_t addr, uint8_t *out) {
    if (!guest_range_ok(vm, addr, 1)) return -1;
    if (addr == DIMON64_MMIO_SERIAL_DATA) {
#ifdef BAREMETAL
        *out = 0;
        return 0;
#else
        int c = getchar();
        *out = (c == EOF) ? 0 : (uint8_t)(c & 0xFF);
        return 0;
#endif
    }
    if (addr >= DIMON64_MMIO_TIMER_TICKS && addr < DIMON64_MMIO_TIMER_TICKS + 8) {
        unsigned sh = (unsigned)(addr - DIMON64_MMIO_TIMER_TICKS) * 8u;
        *out = (uint8_t)((vm->timer_ticks >> sh) & 0xFFu);
        return 0;
    }
    if (addr >= DIMON64_MMIO_TIMER_PERIOD && addr < DIMON64_MMIO_TIMER_PERIOD + 8) {
        unsigned sh = (unsigned)(addr - DIMON64_MMIO_TIMER_PERIOD) * 8u;
        *out = (uint8_t)((vm->timer_period >> sh) & 0xFFu);
        return 0;
    }
    if (addr >= DIMON64_MMIO_TIMER_VECTOR && addr < DIMON64_MMIO_TIMER_VECTOR + 8) {
        unsigned sh = (unsigned)(addr - DIMON64_MMIO_TIMER_VECTOR) * 8u;
        *out = (uint8_t)((vm->timer_vector >> sh) & 0xFFu);
        return 0;
    }
    /* Programmable Sound Generator (PSG) */
    if (addr >= DIMON64_MMIO_PSG_FREQ && addr < DIMON64_MMIO_PSG_FREQ + 4) {
        unsigned sh = (unsigned)(addr - DIMON64_MMIO_PSG_FREQ) * 8u;
        *out = (uint8_t)((vm->psg_freq >> sh) & 0xFFu);
        return 0;
    }
    if (addr == DIMON64_MMIO_PSG_WAVE) {
        *out = vm->psg_wave;
        return 0;
    }
    if (addr == DIMON64_MMIO_PSG_VOL) {
        *out = vm->psg_vol;
        return 0;
    }
    if (addr >= DIMON64_MMIO_PSG_DUR && addr < DIMON64_MMIO_PSG_DUR + 4) {
        unsigned sh = (unsigned)(addr - DIMON64_MMIO_PSG_DUR) * 8u;
        *out = (uint8_t)((vm->psg_duration_ms >> sh) & 0xFFu);
        return 0;
    }
    if (addr >= DIMON64_MMIO_PSG_STATUS && addr < DIMON64_MMIO_PSG_STATUS + 4) {
        if (addr == DIMON64_MMIO_PSG_STATUS) {
            uint8_t busy = 0;
            if (vm->psg_end_time_ms != 0) {
                uint64_t now = host_time_ms();
                if (now < vm->psg_end_time_ms) {
                    busy = 1;
                } else {
                    vm->psg_end_time_ms = 0;
                }
            }
            *out = busy;
        } else {
            *out = 0;
        }
        return 0;
    }
    if (addr >= vm->memsize) return -1;
    *out = vm->mem[addr];
    return 0;
}

static int write_u8(VM *vm, uint64_t addr, uint8_t v) {
    if (!guest_range_ok(vm, addr, 1)) return -1;
    if (addr == DIMON64_MMIO_SERIAL_DATA) {
#ifdef BAREMETAL
        baremetal_putchar((char)v);
#else
        putchar(v);
        fflush(stdout);
#endif
        /* also mirror into RAM for inspection */
        if (addr < vm->memsize) vm->mem[addr] = v;
        return 0;
    }
    if (addr >= DIMON64_MMIO_TIMER_TICKS && addr < DIMON64_MMIO_TIMER_TICKS + 8) {
        return 0; /* read-only */
    }
    if (addr >= DIMON64_MMIO_TIMER_PERIOD && addr < DIMON64_MMIO_TIMER_PERIOD + 8) {
        unsigned sh = (unsigned)(addr - DIMON64_MMIO_TIMER_PERIOD) * 8u;
        uint64_t mask = ~(0xFFULL << sh);
        vm->timer_period = (vm->timer_period & mask) | ((uint64_t)v << sh);
        if (vm->timer_period < 10) vm->timer_period = 10;
        if (vm->timer_period > 1000000) vm->timer_period = 1000000;
        if (addr < vm->memsize) vm->mem[addr] = v;
        return 0;
    }
    if (addr >= DIMON64_MMIO_TIMER_VECTOR && addr < DIMON64_MMIO_TIMER_VECTOR + 8) {
        unsigned sh = (unsigned)(addr - DIMON64_MMIO_TIMER_VECTOR) * 8u;
        uint64_t mask = ~(0xFFULL << sh);
        vm->timer_vector = (vm->timer_vector & mask) | ((uint64_t)v << sh);
        if (addr < vm->memsize) vm->mem[addr] = v;
        return 0;
    }
    /* Programmable Sound Generator (PSG) */
    if (addr >= DIMON64_MMIO_PSG_FREQ && addr < DIMON64_MMIO_PSG_FREQ + 4) {
        unsigned sh = (unsigned)(addr - DIMON64_MMIO_PSG_FREQ) * 8u;
        uint32_t mask = ~(0xFFu << sh);
        vm->psg_freq = (vm->psg_freq & mask) | ((uint32_t)v << sh);
        if (addr < vm->memsize) vm->mem[addr] = v;
        return 0;
    }
    if (addr == DIMON64_MMIO_PSG_WAVE) {
        vm->psg_wave = v;
        if (addr < vm->memsize) vm->mem[addr] = v;
        return 0;
    }
    if (addr == DIMON64_MMIO_PSG_VOL) {
        vm->psg_vol = v;
        if (addr < vm->memsize) vm->mem[addr] = v;
        return 0;
    }
    if (addr >= DIMON64_MMIO_PSG_DUR && addr < DIMON64_MMIO_PSG_DUR + 4) {
        unsigned sh = (unsigned)(addr - DIMON64_MMIO_PSG_DUR) * 8u;
        uint32_t mask = ~(0xFFu << sh);
        vm->psg_duration_ms = (vm->psg_duration_ms & mask) | ((uint32_t)v << sh);
        if (addr < vm->memsize) vm->mem[addr] = v;
        return 0;
    }
    if (addr >= DIMON64_MMIO_PSG_STATUS && addr < DIMON64_MMIO_PSG_STATUS + 4) {
        if (addr == DIMON64_MMIO_PSG_STATUS) {
            if (v & 1) {
                if (vm->psg_duration_ms > 0) {
                    vm->psg_end_time_ms = host_time_ms() + vm->psg_duration_ms;
                } else {
                    vm->psg_end_time_ms = 0;
                }
                if (vm->psg_play_cb) {
                    vm->psg_play_cb(vm->psg_userdata, vm->psg_freq, vm->psg_duration_ms, vm->psg_wave, vm->psg_vol);
                }
            } else {
                vm->psg_end_time_ms = 0;
            }
        }
        if (addr < vm->memsize) vm->mem[addr] = v;
        return 0;
    }
    if (addr >= vm->memsize) return -1;
    vm->mem[addr] = v;
    return 0;
}

static int mem_load(VM *vm, uint64_t addr, unsigned size, uint64_t *out) {
    uint64_t v = 0;
    for (unsigned i = 0; i < size; i++) {
        uint8_t b = 0;
        if (read_u8(vm, addr + i, &b) != 0) return -1;
        v |= ((uint64_t)b) << (8u * i);
    }
    *out = v;
    return 0;
}

static int mem_store(VM *vm, uint64_t addr, unsigned size, uint64_t v) {
    for (unsigned i = 0; i < size; i++) {
        if (write_u8(vm, addr + i, (uint8_t)((v >> (8u * i)) & 0xFFu)) != 0) return -1;
    }
    return 0;
}

static int fetch32(VM *vm, uint64_t addr, uint32_t *out) {
    if ((addr % 4u) != 0) return -2;
    if (addr + 4 > vm->memsize) return -3;
    if (!guest_range_ok(vm, addr, 4)) return -4;
    *out = (uint32_t)vm->mem[addr] |
           ((uint32_t)vm->mem[addr + 1] << 8) |
           ((uint32_t)vm->mem[addr + 2] << 16) |
           ((uint32_t)vm->mem[addr + 3] << 24);
    return 0;
}

/* ---------- flags ---------- */
static void flags_logic(VM *vm, uint64_t res) {
    uint64_t ie = vm->flags & DIMON64_FLAG_IE;
    vm->flags &= ~(DIMON64_FLAG_Z | DIMON64_FLAG_N | DIMON64_FLAG_C | DIMON64_FLAG_O);
    if (res == 0) vm->flags |= DIMON64_FLAG_Z;
    if (res >> 63) vm->flags |= DIMON64_FLAG_N;
    vm->flags |= ie;
}

static void flags_add(VM *vm, uint64_t a, uint64_t b, uint64_t res) {
    uint64_t ie = vm->flags & DIMON64_FLAG_IE;
    vm->flags &= ~(DIMON64_FLAG_Z | DIMON64_FLAG_N | DIMON64_FLAG_C | DIMON64_FLAG_O);
    if (res == 0) vm->flags |= DIMON64_FLAG_Z;
    if (res >> 63) vm->flags |= DIMON64_FLAG_N;
    if (res < a) vm->flags |= DIMON64_FLAG_C;
    if (((~(a ^ b)) & (a ^ res)) >> 63) vm->flags |= DIMON64_FLAG_O;
    vm->flags |= ie;
}

static void flags_sub(VM *vm, uint64_t a, uint64_t b, uint64_t res) {
    uint64_t ie = vm->flags & DIMON64_FLAG_IE;
    vm->flags &= ~(DIMON64_FLAG_Z | DIMON64_FLAG_N | DIMON64_FLAG_C | DIMON64_FLAG_O);
    if (res == 0) vm->flags |= DIMON64_FLAG_Z;
    if (res >> 63) vm->flags |= DIMON64_FLAG_N;
    if (a < b) vm->flags |= DIMON64_FLAG_C; /* borrow */
    if (((a ^ b) & (a ^ res)) >> 63) vm->flags |= DIMON64_FLAG_O;
    vm->flags |= ie;
}

/* ---------- scheduler ---------- */
static void proc_save_current(VM *vm) {
    if (vm->cur_proc < 0 || vm->cur_proc >= DIMON64_MAX_PROCS) return;
    Dimon64Proc *p = &vm->procs[vm->cur_proc];
    if (!p->used) return;
    p->pc = vm->pc;
    p->flags = vm->flags;
    memcpy(p->regs, vm->regs, sizeof(vm->regs));
}

static int proc_count_ready(VM *vm) {
    int n = 0;
    for (int i = 0; i < DIMON64_MAX_PROCS; i++) {
        if (vm->procs[i].used && vm->procs[i].state == DIMON64_PROC_READY) n++;
        if (vm->procs[i].used && vm->procs[i].state == DIMON64_PROC_RUNNING) n++;
    }
    return n;
}

static int proc_schedule(VM *vm) {
    /* Find next READY process round-robin. Returns 1 if switched. */
    if (vm->cur_proc < 0 || vm->cur_proc >= DIMON64_MAX_PROCS) vm->cur_proc = 0;
    Dimon64Proc *cur = &vm->procs[vm->cur_proc];
    if (cur->used && cur->state == DIMON64_PROC_RUNNING) {
        cur->state = DIMON64_PROC_READY;
        cur->pc = vm->pc;
        cur->flags = vm->flags;
        memcpy(cur->regs, vm->regs, sizeof(vm->regs));
    }
    int start = (vm->cur_proc + 1) % DIMON64_MAX_PROCS;
    int next = -1;
    for (int k = 0; k < DIMON64_MAX_PROCS; k++) {
        int i = (start + k) % DIMON64_MAX_PROCS;
        if (vm->procs[i].used && vm->procs[i].state == DIMON64_PROC_READY) { next = i; break; }
    }
    if (next < 0) {
        /* No other READY: resume current if still usable */
        if (cur->used && (cur->state == DIMON64_PROC_READY || cur->state == DIMON64_PROC_RUNNING)) {
            cur->state = DIMON64_PROC_RUNNING;
            vm->cur_proc = (int)(cur - vm->procs);
            vm->pc = cur->pc;
            vm->flags = cur->flags;
            memcpy(vm->regs, cur->regs, sizeof(vm->regs));
            vm->regs[0] = 0;
            return 0;
        }
        return 0;
    }
    Dimon64Proc *np = &vm->procs[next];
    np->state = DIMON64_PROC_RUNNING;
    vm->cur_proc = next;
    vm->pc = np->pc;
    vm->flags = np->flags;
    memcpy(vm->regs, np->regs, sizeof(vm->regs));
    vm->regs[0] = 0;
    vm->regs[4] = np->pid; /* tp points to current PID */
    vm->switches++;
    np->context_switches++;
    return 1;
}

static int proc_find_pid(VM *vm, uint64_t pid) {
    for (int i = 0; i < DIMON64_MAX_PROCS; i++)
        if (vm->procs[i].used && vm->procs[i].pid == pid) return i;
    return -1;
}

static void proc_release(VM *vm, int slot, int32_t fault) {
    if (slot < 0 || slot >= DIMON64_MAX_PROCS) return;
    Dimon64Proc *p = &vm->procs[slot];
    if (vm->event_owner_pid == p->pid) vm->event_owner_pid = 0;
    if (p->isolated && p->memory_size && p->memory_base + p->memory_size <= vm->memsize)
        memset(vm->mem + p->memory_base, 0, (size_t)p->memory_size);
    p->last_fault = fault; p->state = DIMON64_PROC_TERMINATED; p->used = 0;
}

static int proc_resume_any(VM *vm) {
    for (int k = 0; k < DIMON64_MAX_PROCS; k++) {
        int i = (vm->cur_proc + 1 + k) % DIMON64_MAX_PROCS;
        Dimon64Proc *p = &vm->procs[i];
        if (!p->used || (p->state != DIMON64_PROC_READY && p->state != DIMON64_PROC_RUNNING)) continue;
        p->state = DIMON64_PROC_RUNNING; vm->cur_proc = i; vm->pc = p->pc; vm->flags = p->flags;
        memcpy(vm->regs, p->regs, sizeof(vm->regs)); vm->regs[0] = 0; vm->regs[4] = p->pid;
        vm->switches++; p->context_switches++; return 0;
    }
    vm->halted = 1; return -1;
}

static int guest_cstring(VM *vm, uint64_t addr, char *out, size_t cap) {
    if (!out || cap < 2 || !guest_range_ok(vm, addr, 1)) return -1;
    for (size_t i = 0; i < cap; i++) {
        if (!guest_range_ok(vm, addr + i, 1)) return -1;
        out[i] = (char)vm->mem[addr + i];
        if (!out[i]) return 0;
    }
    out[cap - 1] = 0; return -1;
}

static void syscall_status(VM *vm, int rc) {
    vm->regs[10] = (uint64_t)(int64_t)rc;
    if (rc < 0) vm->flags |= DIMON64_FLAG_C; else vm->flags &= ~DIMON64_FLAG_C;
}

static int load_dexe(VM *vm, const char *path, const char *argument) {
    Dimon64DirEnt st; int rc = dimonfs_stat(vm, path, &st); if (rc) return rc;
    if (st.attributes & 0x10u) return DFS_ERR_IS_DIR;
    if (st.size < sizeof(Dimon64ExecHeader) || st.size > 0x00800000u) return DFS_ERR_INVALID;
    const uint64_t stage = 0x03000000u;
    if (stage + st.size > vm->memsize) return DFS_ERR_TOO_LARGE;
    uint32_t size = 0; rc = dimonfs_read(vm, path, 0, vm->mem + stage, st.size, &size);
    if (rc < 0 || size != st.size || (uint32_t)rc != st.size) return rc < 0 ? rc : DFS_ERR_IO;
    Dimon64ExecHeader h; memcpy(&h, vm->mem + stage, sizeof(h));
    if (memcmp(h.magic, DIMON64_EXEC_MAGIC, 8) || h.version != DIMON64_EXEC_VERSION ||
        h.header_size != sizeof(h) || h.image_size > DIMON64_APP_SLOT_SIZE ||
        h.memory_size > DIMON64_APP_SLOT_SIZE || h.memory_size < h.image_size + h.bss_size ||
        h.entry_offset >= h.image_size || (h.entry_offset & 3u) ||
        (uint64_t)h.header_size + h.image_size + (uint64_t)h.relocation_count * 4u > size)
        return DFS_ERR_INVALID;
    int slot = -1;
    for (int i = 1; i < DIMON64_MAX_PROCS; i++) if (!vm->procs[i].used) { slot = i; break; }
    if (slot < 0) return -13;
    uint64_t base = DIMON64_APP_BASE + (uint64_t)(slot - 1) * DIMON64_APP_SLOT_SIZE;
    memset(vm->mem + base, 0, DIMON64_APP_SLOT_SIZE);
    memcpy(vm->mem + base, vm->mem + stage + h.header_size, h.image_size);
    const uint8_t *rel = vm->mem + stage + h.header_size + h.image_size;
    for (uint32_t i = 0; i < h.relocation_count; i++) {
        uint32_t off = load_le32(rel + (uint64_t)i * 4u);
        if (off + 8u > h.image_size || (off & 3u)) { memset(vm->mem + base, 0, DIMON64_APP_SLOT_SIZE); return DFS_ERR_INVALID; }
        uint32_t w1 = load_le32(vm->mem + base + off), w2 = load_le32(vm->mem + base + off + 4u);
        if ((w1 & 0x7fu) != DIMON64_OPCODE_LUI || (w2 & 0x7fu) != DIMON64_OPCODE_OP_IMM ||
            ((w2 >> 12) & 7u) != DIMON64_F3_ADDI || ((w1 >> 7) & 31u) != ((w2 >> 7) & 31u) ||
            ((w1 >> 7) & 31u) != ((w2 >> 15) & 31u)) return DFS_ERR_INVALID;
        int64_t old = (int64_t)(int32_t)(w1 & 0xfffff000u) +
                      (int64_t)((int32_t)w2 >> 20);
        int64_t value = old + (int64_t)base;
        int32_t hi = (int32_t)(((value + 0x800) >> 12) & 0xfffff);
        int32_t lo = (int32_t)(value - ((int64_t)hi << 12));
        w1 = (w1 & 0xfffu) | ((uint32_t)hi << 12);
        w2 = (w2 & 0x000fffffu) | ((uint32_t)(lo & 0xfff) << 20);
        memcpy(vm->mem + base + off, &w1, 4); memcpy(vm->mem + base + off + 4u, &w2, 4);
    }
    Dimon64Proc *p = &vm->procs[slot]; memset(p, 0, sizeof(*p));
    p->used = 1; p->state = DIMON64_PROC_READY; p->pid = vm->next_pid++;
    p->pc = base + h.entry_offset; p->flags = DIMON64_FLAG_IE;
    p->stack_base = DIMON64_STACK_BASE + (uint64_t)slot * DIMON64_STACK_SIZE;
    p->stack_size = DIMON64_STACK_SIZE; p->regs[2] = (p->stack_base + p->stack_size) & ~15ULL;
    p->regs[4] = p->pid; p->memory_base = base; p->memory_size = h.memory_size;
    p->isolated = 1; snprintf(p->name, sizeof(p->name), "%s", h.name[0] ? h.name : st.name);
    vm->event_owner_pid = p->pid;
    if (argument && *argument) {
        size_t n = strlen(argument); if (n > 255) n = 255;
        uint64_t argp = base + h.memory_size - 256u; memcpy(vm->mem + argp, argument, n); vm->mem[argp + n] = 0;
        p->regs[10] = argp;
    }
    return (int)p->pid;
}

static void proc_wake_sleepers(VM *vm) {
    for (int i = 0; i < DIMON64_MAX_PROCS; i++) {
        if (vm->procs[i].used && vm->procs[i].state == DIMON64_PROC_BLOCKED) {
            if (vm->timer_ticks >= vm->procs[i].sleep_until) {
                vm->procs[i].state = DIMON64_PROC_READY;
            }
        }
    }
}

static void timer_tick(VM *vm) {
    vm->timer_ticks++;
    /* Mirror into MMIO RAM for guest inspection */
    if (DIMON64_MMIO_TIMER_TICKS + 8 <= vm->memsize) {
        for (int i = 0; i < 8; i++)
            vm->mem[DIMON64_MMIO_TIMER_TICKS + (uint64_t)i] =
                (uint8_t)((vm->timer_ticks >> (8 * i)) & 0xFFu);
    }
    proc_wake_sleepers(vm);
    int ie = (vm->flags & DIMON64_FLAG_IE) != 0;
    if (!ie) return;
    if (vm->in_isr) return; /* no nesting */
    /* Native round-robin preemption on every tick when more than one task exists.
       This provides hardware timer-driven preemptive multitasking even when
       an OS-level ISR is installed. When a vector is installed we first
       switch to the next task and then deliver the interrupt so the ISR
       runs in the context of the newly scheduled task (it must preserve
       registers and return with IRET). */
    if (proc_count_ready(vm) > 1) {
        (void)proc_schedule(vm);
    }
    /* The desktop ISR lives in PID 0 memory. Isolated apps are already
       preempted by the native scheduler and must never execute that vector. */
    if (vm->cur_proc >= 0 && vm->cur_proc < DIMON64_MAX_PROCS &&
        vm->procs[vm->cur_proc].isolated) return;
    if (vm->timer_vector != 0) {
        /* Hardware interrupt delivery: save trap registers and jump to ISR */
        if ((vm->timer_vector % 4u) != 0) return;
        if (vm->timer_vector + 4 > vm->memsize) return;
        vm->epc = vm->pc;
        vm->eflags = vm->flags;
        vm->flags &= ~DIMON64_FLAG_IE; /* disable nested interrupts */
        vm->in_isr = 1;
        vm->pc = vm->timer_vector;
        /* Keep native proc in sync with the trap entry point */
        proc_save_current(vm);
        /* proc_save_current overwrote current proc pc with vector address;
           restore it to EPC so a later YIELD/switch keeps task resume points. */
        if (vm->cur_proc >= 0 && vm->cur_proc < DIMON64_MAX_PROCS) {
            Dimon64Proc *cp = &vm->procs[vm->cur_proc];
            if (cp->used) {
                cp->pc = vm->epc;
                cp->flags = vm->eflags;
            }
        }
    }
}

/* ---------- syscalls ---------- */
static void do_syscall(VM *vm, uint64_t id) {
    uint64_t a0 = vm->regs[10], a1 = vm->regs[11], a2 = vm->regs[12];
    uint64_t a3 = vm->regs[13], a4 = vm->regs[14], a5 = vm->regs[15], a6 = vm->regs[16];
    (void)a3; (void)a4; (void)a5; (void)a6;
    switch (id) {
        case DIMON64_SYS_PUTCHAR:
#ifdef BAREMETAL
            baremetal_putchar((char)(a0 & 0xFFu));
#else
            putchar((int)(a0 & 0xFFu));
            fflush(stdout);
#endif
            break;
        case DIMON64_SYS_GETCHAR: {
#ifdef BAREMETAL
            vm->regs[10] = 0;
#else
            int c = getchar();
            vm->regs[10] = (c == EOF) ? 0 : (uint64_t)(c & 0xFF);
#endif
            break;
        }
        case DIMON64_SYS_PRINTSTR: {
            uint64_t addr = a0;
            for (uint64_t i = 0; i < vm->memsize; i++) {
                uint8_t c = 0;
                if (mem_load(vm, addr + i, 1, &(uint64_t){0}) != 0) break;
                /* direct read for speed with MMIO fallback */
                if (read_u8(vm, addr + i, &c) != 0) break;
                if (c == 0) break;
#ifdef BAREMETAL
                baremetal_putchar((char)c);
#else
                putchar(c);
#endif
            }
#ifndef BAREMETAL
            fflush(stdout);
#endif
            break;
        }
        case DIMON64_SYS_PRINTNUM: {
            int64_t v = (int64_t)a0;
#ifdef BAREMETAL
            char buf[24];
            int neg = (v < 0);
            uint64_t u = neg ? (uint64_t)(-v) : (uint64_t)v;
            char tmp[24]; int n = 0;
            if (u == 0) tmp[n++] = '0';
            while (u > 0 && n < 23) { tmp[n++] = (char)('0' + (u % 10)); u /= 10; }
            if (neg) baremetal_putchar('-');
            while (n > 0) baremetal_putchar(tmp[--n]);
            (void)buf;
#else
            printf("%" PRId64, v);
            fflush(stdout);
#endif
            break;
        }
        case DIMON64_SYS_READNUM: {
#ifdef BAREMETAL
            vm->flags |= DIMON64_FLAG_C;
            vm->regs[10] = 0;
#else
            char buf[64];
            int pos = 0, c;
            do { c = getchar(); if (c == EOF) break; } while (c == ' ' || c == '\t');
            while (c != EOF && c != '\n' && c != '\r' && pos < (int)sizeof(buf) - 1) {
                buf[pos++] = (char)c;
                c = getchar();
            }
            buf[pos] = 0;
            if (pos == 0) { vm->flags |= DIMON64_FLAG_C; vm->regs[10] = 0; break; }
            /* support 0b binary */
            char *end = NULL;
            long long v = 0;
            if (buf[0] == '0' && (buf[1] == 'b' || buf[1] == 'B')) {
                v = 0; end = buf + 2; int ok = 0;
                int neg = 0;
                if (*end == '-') { neg = 1; end++; }
                while (*end == '0' || *end == '1') { v = v * 2 + (*end - '0'); end++; ok = 1; }
                if (neg) v = -v;
                if (!ok) end = buf;
            } else {
                v = strtoll(buf, &end, 0);
            }
            while (end && (*end == ' ' || *end == '\t')) end++;
            if (!end || *end != 0) { vm->flags |= DIMON64_FLAG_C; vm->regs[10] = 0; }
            else { vm->flags &= ~DIMON64_FLAG_C; vm->regs[10] = (uint64_t)v; }
#endif
            break;
        }
        case DIMON64_SYS_NEWLINE:
#ifdef BAREMETAL
            baremetal_putchar('\n');
#else
            putchar('\n');
            fflush(stdout);
#endif
            break;
        case DIMON64_SYS_DISK_READ: {
            uint32_t lba = (uint32_t)a0;
            int rc = guest_range_ok(vm, a1, a2 * DISK_SECTOR_SIZE) ?
                vm_disk_read(vm, lba, a1, a2) : DISK_ERR_RAM;
            vm->regs[10] = (uint64_t)rc;
            if (rc == DISK_ERR_NONE) vm->flags &= ~DIMON64_FLAG_C;
            else vm->flags |= DIMON64_FLAG_C;
            break;
        }
        case DIMON64_SYS_DISK_INFO: {
            if (a2 > 0 && a1 != 0) {
                /* Compatibility alias: allow DISK_READ via INT 7 when count > 0 */
                uint32_t lba = (uint32_t)a0;
                int rc = vm_disk_read(vm, lba, a1, a2);
                vm->regs[10] = (uint64_t)rc;
                if (rc == DISK_ERR_NONE) vm->flags &= ~DIMON64_FLAG_C;
                else vm->flags |= DIMON64_FLAG_C;
                break;
            }
            if (!vm->disk_data || vm->disk_sectors == 0) {
                vm->flags |= DIMON64_FLAG_C;
                vm->regs[10] = 0; vm->regs[11] = DISK_SECTOR_SIZE; vm->regs[12] = 0;
            } else {
                vm->flags &= ~DIMON64_FLAG_C;
                vm->regs[10] = (uint64_t)vm->disk_sectors;
                vm->regs[11] = DISK_SECTOR_SIZE;
                vm->regs[12] = 0;
            }
            break;
        }
        case DIMON64_SYS_DISK_WRITE: {
            uint32_t lba = (uint32_t)a0;
            int rc = guest_range_ok(vm, a1, a2 * DISK_SECTOR_SIZE) ?
                vm_disk_write(vm, lba, a1, a2) : DISK_ERR_RAM;
            vm->regs[10] = (uint64_t)rc;
            if (rc == DISK_ERR_NONE) vm->flags &= ~DIMON64_FLAG_C;
            else vm->flags |= DIMON64_FLAG_C;
            break;
        }
        case DIMON64_SYS_GUI_INIT: {
            vm->gui_active = 1;
            vm->draw_context_active = 0;
            vm->draw_offset_x = vm->draw_offset_y = 0;
            vm->regs[10] = DIMON64_VRAM_COLS;
            vm->regs[11] = DIMON64_VRAM_ROWS;
            vm->regs[12] = DIMON64_VRAM_BASE;
            vm->flags &= ~DIMON64_FLAG_C;
            if (vm->gui_init_cb) vm->gui_init_cb(vm->gui_userdata);
            break;
        }
        case DIMON64_SYS_GUI_POLL_EVENT: {
            if (vm->gui_poll_cb) vm->gui_poll_cb(vm->gui_userdata);
            Dimon64Proc *caller = &vm->procs[vm->cur_proc];
            if ((vm->event_owner_pid && caller->pid != vm->event_owner_pid) ||
                (!vm->event_owner_pid && caller->isolated)) {
                vm->regs[10] = vm->regs[11] = vm->regs[12] = vm->regs[13] = vm->regs[14] = 0;
                break;
            }
            uint8_t t = 0, b = 0; uint16_t c = 0, d = 0;
            uint8_t modifiers = (vm->event_head != vm->event_tail) ?
                vm->event_queue[vm->event_head].modifiers : 0;
            if (vm_event_pop_ext(vm, &t, &c, &d, &b)) {
                vm->regs[10] = (uint64_t)t;
                vm->regs[11] = (uint64_t)c;
                vm->regs[12] = (uint64_t)d;
                vm->regs[13] = (uint64_t)b;
                vm->regs[14] = (uint64_t)modifiers;
            } else {
                vm->regs[10] = 0;
                vm->regs[11] = 0;
                vm->regs[12] = 0;
                vm->regs[13] = 0;
                vm->regs[14] = 0;
            }
            break;
        }
        case DIMON64_SYS_GUI_FLUSH: {
            if (vm->gui_flush_cb) vm->gui_flush_cb(vm->gui_userdata);
            vm->gui_dirty = 0;
            break;
        }
        case DIMON64_SYS_GET_TICKS: {
            vm->regs[10] = host_time_ms();
            break;
        }
        case DIMON64_SYS_GUI_DRAW_RECT: {
            if (a3 > 0) {
                /* TrueColor fillrect(x, y, w, h, color32) */
                int x = (int)a0;
                int y = (int)a1;
                int w = (int)a2;
                int h = (int)a3;
                uint32_t col = (uint32_t)a4;
                vm_gui_fill_rect(vm, x, y, w, h, col);
            } else {
                /* Legacy packed format */
                int x = (int)(a0 & 0xFFu);
                int y = (int)((a0 >> 8) & 0xFFu);
                int w = (int)(a1 & 0xFFu);
                int h = (int)((a1 >> 8) & 0xFFu);
                uint8_t ch = (uint8_t)(a2 & 0xFFu);
                uint8_t at = (uint8_t)((a2 >> 8) & 0xFFu);
                vm_gui_draw_rect(vm, x, y, w, h, ch, at);
            }
            break;
        }
        case DIMON64_SYS_GUI_DRAW_TEXT: {
            if (a3 != 0 || a4 != 0) {
                /* TrueColor drawstring(x, y, textptr, fgcolor32, bgcolor32) */
                int x = (int)a0;
                int y = (int)a1;
                uint64_t addr = a2;
                uint32_t fg = (uint32_t)a3;
                uint32_t bg = (uint32_t)a4;
                char s[256]; int i = 0;
                while (i < 255) {
                    uint8_t c = 0;
                    if (read_u8(vm, addr + (uint64_t)i, &c) != 0) break;
                    if (c == 0) break;
                    s[i++] = (char)c;
                }
                s[i] = 0;
                vm_gui_draw_string(vm, x, y, s, fg, bg);
            } else {
                /* Legacy packed format */
                int x = (int)(a0 & 0xFFu);
                int y = (int)((a0 >> 8) & 0xFFu);
                uint64_t addr = a1;
                uint8_t at = (uint8_t)(a2 & 0xFFu);
                char s[256]; int i = 0;
                while (i < 255) {
                    uint8_t c = 0;
                    if (read_u8(vm, addr + (uint64_t)i, &c) != 0) break;
                    if (c == 0) break;
                    s[i++] = (char)c;
                }
                s[i] = 0;
                vm_gui_draw_text(vm, x, y, s, at);
            }
            break;
        }
        case DIMON64_SYS_GUI_DRAW_PIXEL: {
            int x = (int)a0;
            int y = (int)a1;
            uint32_t col = (uint32_t)a2;
            vm_gui_draw_pixel(vm, x, y, col);
            break;
        }
        case DIMON64_SYS_GUI_DRAW_LINE: {
            int x0 = (int)a0;
            int y0 = (int)a1;
            int x1 = (int)a2;
            int y1 = (int)a3;
            uint32_t col = (uint32_t)a4;
            vm_gui_draw_line(vm, x0, y0, x1, y1, col);
            break;
        }
        case DIMON64_SYS_GUI_BLIT: {
            int dx = (int)a0;
            int dy = (int)a1;
            int w = (int)a2;
            int h = (int)a3;
            uint64_t src = a4;
            uint64_t bytes = (w > 0 && h > 0) ? (uint64_t)w * (uint64_t)h * 4u : 0;
            if (w > 0 && h > 0 && guest_range_ok(vm, src, bytes)) {
                if (vm->draw_context_active) { dx += vm->draw_offset_x; dy += vm->draw_offset_y; }
                int x0 = dx < 0 ? 0 : dx, y0 = dy < 0 ? 0 : dy;
                int x1 = dx + w, y1 = dy + h;
                if (x1 > DIMON64_LFB_WIDTH) x1 = DIMON64_LFB_WIDTH;
                if (y1 > DIMON64_LFB_HEIGHT) y1 = DIMON64_LFB_HEIGHT;
                if (vm->draw_context_active) {
                    if (x0 < vm->draw_clip_x) x0 = vm->draw_clip_x;
                    if (y0 < vm->draw_clip_y) y0 = vm->draw_clip_y;
                    if (x1 > vm->draw_clip_x + vm->draw_clip_w) x1 = vm->draw_clip_x + vm->draw_clip_w;
                    if (y1 > vm->draw_clip_y + vm->draw_clip_h) y1 = vm->draw_clip_y + vm->draw_clip_h;
                }
                uint32_t *vram = (uint32_t *)(vm->mem + DIMON64_VRAM_BASE);
                for (int sy = y0; sy < y1; sy++) {
                    int source_y = sy - dy, source_x = x0 - dx;
                    uint64_t srow = src + ((uint64_t)source_y * (uint64_t)w + (uint64_t)source_x) * 4u;
                    if (x1 > x0) memcpy(&vram[sy * DIMON64_LFB_WIDTH + x0], vm->mem + srow, (size_t)(x1 - x0) * 4u);
                }
                if (x1 > x0 && y1 > y0) vm->gui_dirty = 1;
            }
            break;
        }
        case DIMON64_SYS_MEMSET: {
            uint64_t dst = a0;
            uint32_t val = (uint32_t)a1;
            uint64_t cnt = a2;
            if (cnt <= UINT64_MAX / 4u && guest_range_ok(vm, dst, cnt * 4u)) {
                uint32_t *p = (uint32_t *)(vm->mem + dst);
                for (uint64_t i = 0; i < cnt; i++) p[i] = val;
            }
            break;
        }
        case DIMON64_SYS_YIELD: {
            proc_save_current(vm);
            int sw = proc_schedule(vm);
            (void)sw;
            if (vm->in_isr) {
                vm->epc = vm->pc;
                vm->eflags = vm->flags;
            }
            break;
        }
        case DIMON64_SYS_SPAWN: {
            uint64_t entry = a0, arg = a1, name_ptr = a2;
            if (vm->procs[vm->cur_proc].isolated) { syscall_status(vm, -1); break; }
            int slot = -1;
            for (int i = 0; i < DIMON64_MAX_PROCS; i++) {
                if (!vm->procs[i].used) { slot = i; break; }
            }
            if (slot < 0) { vm->regs[10] = (uint64_t)(int64_t)-1; break; }
            if ((entry % 4u) != 0 || entry + 4 > vm->memsize) {
                vm->regs[10] = (uint64_t)(int64_t)-1;
                break;
            }
            Dimon64Proc *np = &vm->procs[slot];
            memset(np, 0, sizeof(*np));
            np->used = 1;
            np->state = DIMON64_PROC_READY;
            np->pid = vm->next_pid++;
            np->pc = entry;
            np->flags = DIMON64_FLAG_IE;
            memset(np->regs, 0, sizeof(np->regs));
            np->stack_base = DIMON64_STACK_BASE + (uint64_t)slot * DIMON64_STACK_SIZE;
            np->stack_size = DIMON64_STACK_SIZE;
            uint64_t top = np->stack_base + np->stack_size;
            top &= ~15ULL;
            np->regs[2] = top;
            np->regs[10] = arg;
            np->regs[4] = np->pid;
            np->sleep_until = 0;
            /* copy name */
            if (name_ptr != 0 && name_ptr < vm->memsize) {
                for (int i = 0; i < 15; i++) {
                    uint8_t c = 0;
                    if (read_u8(vm, name_ptr + (uint64_t)i, &c) != 0) break;
                    np->name[i] = (char)c;
                    if (c == 0) break;
                }
                np->name[15] = 0;
            } else {
                snprintf(np->name, sizeof(np->name), "task%" PRIu64, np->pid);
            }
            /* current keeps running; new task becomes READY */
            proc_save_current(vm);
            vm->regs[10] = np->pid;
            break;
        }
        case DIMON64_SYS_EXIT: {
            proc_release(vm, vm->cur_proc, 0);
            /* find next READY */
            int next = -1;
            for (int k = 0; k < DIMON64_MAX_PROCS; k++) {
                int i = (vm->cur_proc + 1 + k) % DIMON64_MAX_PROCS;
                if (vm->procs[i].used && vm->procs[i].state == DIMON64_PROC_READY) { next = i; break; }
            }
            if (next < 0) {
                /* No READY: idle until BLOCKED tasks wake (do not deadlock).
                   Fast-forward timer to the next wakeup like SLEEP does. */
                uint64_t soon = UINT64_MAX;
                for (int i = 0; i < DIMON64_MAX_PROCS; i++) {
                    if (vm->procs[i].used && vm->procs[i].state == DIMON64_PROC_BLOCKED) {
                        if (vm->procs[i].sleep_until < soon) soon = vm->procs[i].sleep_until;
                    }
                }
                if (soon != UINT64_MAX) {
                    vm->timer_ticks = soon;
                    if (DIMON64_MMIO_TIMER_TICKS + 8 <= vm->memsize) {
                        for (int b = 0; b < 8; b++)
                            vm->mem[DIMON64_MMIO_TIMER_TICKS + (uint64_t)b] =
                                (uint8_t)((vm->timer_ticks >> (8 * b)) & 0xFFu);
                    }
                    proc_wake_sleepers(vm);
                    for (int k = 0; k < DIMON64_MAX_PROCS; k++) {
                        int i = (vm->cur_proc + 1 + k) % DIMON64_MAX_PROCS;
                        if (vm->procs[i].used && vm->procs[i].state == DIMON64_PROC_READY) { next = i; break; }
                    }
                }
            }
            if (next < 0) {
                /* Halt only when no processes remain at all */
                int any = 0;
                for (int i = 0; i < DIMON64_MAX_PROCS; i++) {
                    if (vm->procs[i].used) { any = 1; break; }
                }
                if (!any) {
                    vm->halted = 1;
                } else {
                    /* Should not happen: blocked tasks remain but wakeup failed.
                       Stay alive and let timer ticks wake them. */
                    vm->halted = 0;
                    /* Keep pc past the EXIT so we do not re-execute it;
                       park current context on a fresh idle state. */
                    vm->pc += 0; /* pc already advanced before syscall */
                }
            } else {
                Dimon64Proc *np = &vm->procs[next];
                np->state = DIMON64_PROC_RUNNING;
                vm->cur_proc = next;
                vm->pc = np->pc;
                vm->flags = np->flags;
                memcpy(vm->regs, np->regs, sizeof(vm->regs));
                vm->regs[0] = 0;
                vm->regs[4] = np->pid;
                vm->switches++;
                if (vm->in_isr) {
                    vm->epc = vm->pc;
                    vm->eflags = vm->flags;
                }
            }
            break;
        }
        case DIMON64_SYS_SLEEP: {
            Dimon64Proc *cur = &vm->procs[vm->cur_proc];
            cur->sleep_until = vm->timer_ticks + a0;
            cur->state = DIMON64_PROC_BLOCKED;
            cur->pc = vm->pc;
            cur->flags = vm->flags;
            memcpy(cur->regs, vm->regs, sizeof(vm->regs));
            /* schedule next */
            int next = -1;
            for (int k = 0; k < DIMON64_MAX_PROCS; k++) {
                int i = (vm->cur_proc + 1 + k) % DIMON64_MAX_PROCS;
                if (vm->procs[i].used && vm->procs[i].state == DIMON64_PROC_READY) { next = i; break; }
            }
            if (next < 0) {
                /* idle: fast-forward to next wakeup */
                uint64_t soon = UINT64_MAX;
                for (int i = 0; i < DIMON64_MAX_PROCS; i++) {
                    if (vm->procs[i].used && vm->procs[i].state == DIMON64_PROC_BLOCKED) {
                        if (vm->procs[i].sleep_until < soon) soon = vm->procs[i].sleep_until;
                    }
                }
                if (soon != UINT64_MAX) {
                    vm->timer_ticks = soon;
                    if (DIMON64_MMIO_TIMER_TICKS + 8 <= vm->memsize) {
                        for (int b = 0; b < 8; b++)
                            vm->mem[DIMON64_MMIO_TIMER_TICKS + (uint64_t)b] =
                                (uint8_t)((vm->timer_ticks >> (8 * b)) & 0xFFu);
                    }
                    proc_wake_sleepers(vm);
                    for (int k = 0; k < DIMON64_MAX_PROCS; k++) {
                        int i = (vm->cur_proc + 1 + k) % DIMON64_MAX_PROCS;
                        if (vm->procs[i].used && vm->procs[i].state == DIMON64_PROC_READY) { next = i; break; }
                    }
                }
            }
            if (next >= 0) {
                Dimon64Proc *np = &vm->procs[next];
                np->state = DIMON64_PROC_RUNNING;
                vm->cur_proc = next;
                vm->pc = np->pc;
                vm->flags = np->flags;
                memcpy(vm->regs, np->regs, sizeof(vm->regs));
                vm->regs[0] = 0;
                vm->regs[4] = np->pid;
                vm->switches++;
                if (vm->in_isr) {
                    vm->epc = vm->pc;
                    vm->eflags = vm->flags;
                }
            } else {
                /* nobody to run: stay blocked but keep VM alive; will wake on tick */
                cur->state = DIMON64_PROC_READY;
                cur->sleep_until = 0;
            }
            break;
        }
        case DIMON64_SYS_GETPID: {
            Dimon64Proc *cur = &vm->procs[vm->cur_proc];
            vm->regs[10] = cur->used ? cur->pid : (uint64_t)vm->cur_proc;
            break;
        }
        case DIMON64_SYS_SET_TIMER_HANDLER: {
            if (vm->procs[vm->cur_proc].isolated) { syscall_status(vm, -1); break; }
            if (a0 != 0 && ((a0 % 4u) != 0 || a0 + 4 > vm->memsize)) {
                vm->regs[10] = (uint64_t)(int64_t)-1;
            } else {
                vm->timer_vector = a0;
                vm->regs[10] = 0;
            }
            break;
        }
        case DIMON64_SYS_SET_TIMER_PERIOD: {
            if (vm->procs[vm->cur_proc].isolated) { syscall_status(vm, -1); break; }
            uint64_t p = a0;
            if (p < 10) p = 10;
            if (p > 1000000) p = 1000000;
            vm->timer_period = p;
            vm->regs[10] = 0;
            break;
        }
        case DIMON64_SYS_ENABLE_INTERRUPTS: {
            if (a0) vm->flags |= DIMON64_FLAG_IE;
            else vm->flags &= ~DIMON64_FLAG_IE;
            /* keep proc copy in sync */
            proc_save_current(vm);
            vm->regs[10] = 0;
            /* restore overwrote? save already did; need to keep IE change */
            vm->procs[vm->cur_proc].flags = vm->flags;
            memcpy(vm->procs[vm->cur_proc].regs, vm->regs, sizeof(vm->regs));
            break;
        }
        case DIMON64_SYS_GET_TIMER_TICKS: {
            vm->regs[10] = vm->timer_ticks;
            break;
        }
        case DIMON64_SYS_RTC_GET: {
#ifdef BAREMETAL
            vm->regs[10] = 0; vm->flags |= DIMON64_FLAG_C;
#else
            time_t now = time(NULL);
            if (now == (time_t)-1) { vm->regs[10] = 0; vm->flags |= DIMON64_FLAG_C; }
            else { vm->regs[10] = (uint64_t)now; vm->flags &= ~DIMON64_FLAG_C; }
#endif
            break;
        }
        case DIMON64_SYS_GUI_SET_CONTEXT: {
            if ((int64_t)a4 <= 0 || (int64_t)a5 <= 0) {
                vm->draw_context_active = 0; vm->draw_offset_x = vm->draw_offset_y = 0;
            } else {
                int x = (int)a2, y = (int)a3, w = (int)a4, h = (int)a5;
                if (x < 0) { w += x; x = 0; }
                if (y < 0) { h += y; y = 0; }
                if (x + w > DIMON64_LFB_WIDTH) w = DIMON64_LFB_WIDTH - x;
                if (y + h > DIMON64_LFB_HEIGHT) h = DIMON64_LFB_HEIGHT - y;
                vm->draw_offset_x = (int32_t)a0; vm->draw_offset_y = (int32_t)a1;
                vm->draw_clip_x = x; vm->draw_clip_y = y;
                vm->draw_clip_w = w > 0 ? w : 0; vm->draw_clip_h = h > 0 ? h : 0;
                vm->draw_context_active = 1;
            }
            vm->regs[10] = 0; break;
        }
        case DIMON64_SYS_GUI_TEXT_MEASURE:
        case DIMON64_SYS_GUI_TEXT_FIT: {
            uint64_t addr = id == DIMON64_SYS_GUI_TEXT_MEASURE ? a0 : a2;
            char s[256] = {0}; int i = 0;
            while (i < 255) { uint8_t c = 0; if (read_u8(vm, addr + (uint64_t)i, &c)) break; s[i++] = (char)c; if (!c) break; }
            s[255] = 0; if (i == 255) s[254] = 0;
            if (id == DIMON64_SYS_GUI_TEXT_MEASURE) vm->regs[10] = (uint64_t)utf8_text_width(s);
            else vm_gui_draw_string_fit(vm, (int)a0, (int)a1, s, (uint32_t)a3, (uint32_t)a4, (int)a5);
            break;
        }
        case DIMON64_SYS_PROC_INFO: {
            if (a0 >= DIMON64_MAX_PROCS || !guest_range_ok(vm, a1, sizeof(Dimon64ProcInfo))) {
                syscall_status(vm, -1); break;
            }
            Dimon64Proc *p = &vm->procs[a0];
            if (!p->used) { syscall_status(vm, DFS_ERR_NOT_FOUND); break; }
            Dimon64ProcInfo info; memset(&info, 0, sizeof(info));
            info.pid = p->pid; info.state = p->state; info.memory_base = p->memory_base;
            info.memory_size = p->memory_size + p->stack_size; info.cpu_steps = p->cpu_steps;
            info.context_switches = p->context_switches; memcpy(info.name, p->name, sizeof(info.name));
            info.essential = p->essential; info.isolated = p->isolated;
            memcpy(vm->mem + a1, &info, sizeof(info)); syscall_status(vm, 0); break;
        }
        case DIMON64_SYS_PROC_KILL: {
            int slot = proc_find_pid(vm, a0);
            if (slot < 0) { syscall_status(vm, DFS_ERR_NOT_FOUND); break; }
            if (vm->procs[slot].essential || vm->procs[slot].pid == 0) { syscall_status(vm, -14); break; }
            int current = slot == vm->cur_proc; proc_release(vm, slot, 0);
            if (current) (void)proc_resume_any(vm); else syscall_status(vm, 0);
            break;
        }
        case DIMON64_SYS_FS_STAT:
        case DIMON64_SYS_FS_LIST:
        case DIMON64_SYS_FS_READ:
        case DIMON64_SYS_FS_WRITE:
        case DIMON64_SYS_FS_MKDIR:
        case DIMON64_SYS_FS_REMOVE:
        case DIMON64_SYS_FS_RENAME:
        case DIMON64_SYS_FS_COPY:
        case DIMON64_SYS_APP_EXEC: {
            char path[260], path2[260];
            if (guest_cstring(vm, a0, path, sizeof(path))) { syscall_status(vm, DFS_ERR_INVALID); break; }
            int rc = DFS_ERR_INVALID;
            if (id == DIMON64_SYS_FS_STAT) {
                if (!guest_range_ok(vm, a1, sizeof(Dimon64DirEnt))) { syscall_status(vm, DFS_ERR_INVALID); break; }
                Dimon64DirEnt ent; rc = dimonfs_stat(vm, path, &ent);
                if (!rc) memcpy(vm->mem + a1, &ent, sizeof(ent));
            } else if (id == DIMON64_SYS_FS_LIST) {
                if (!guest_range_ok(vm, a2, sizeof(Dimon64DirEnt))) { syscall_status(vm, DFS_ERR_INVALID); break; }
                Dimon64DirEnt ent; rc = dimonfs_list(vm, path, (uint32_t)a1, &ent);
                if (!rc) memcpy(vm->mem + a2, &ent, sizeof(ent));
            } else if (id == DIMON64_SYS_FS_READ) {
                if (a3 > UINT32_MAX || !guest_range_ok(vm, a2, a3)) { syscall_status(vm, DFS_ERR_INVALID); break; }
                uint32_t size = 0; rc = dimonfs_read(vm, path, (uint32_t)a1, vm->mem + a2, (uint32_t)a3, &size);
                vm->regs[11] = size;
            } else if (id == DIMON64_SYS_FS_WRITE) {
                if (a2 > UINT32_MAX || !guest_range_ok(vm, a1, a2)) { syscall_status(vm, DFS_ERR_INVALID); break; }
                rc = dimonfs_write(vm, path, vm->mem + a1, (uint32_t)a2, (a3 & 1u) != 0, (a3 & 2u) != 0);
            } else if (id == DIMON64_SYS_FS_MKDIR) rc = dimonfs_mkdir(vm, path);
            else if (id == DIMON64_SYS_FS_REMOVE) rc = dimonfs_remove(vm, path);
            else {
                if (id == DIMON64_SYS_APP_EXEC && a1 == 0) path2[0] = 0;
                else if (guest_cstring(vm, a1, path2, sizeof(path2))) { syscall_status(vm, DFS_ERR_INVALID); break; }
                if (id == DIMON64_SYS_FS_RENAME) rc = dimonfs_rename(vm, path, path2);
                else if (id == DIMON64_SYS_FS_COPY) rc = dimonfs_copy(vm, path, path2);
                else rc = load_dexe(vm, path, path2);
            }
            syscall_status(vm, rc); break;
        }
        default:
            break;
    }
    vm->regs[0] = 0;
}

/* ---------- decode helpers ---------- */
static int64_t sign_extend(uint64_t v, unsigned bits) {
    if (bits >= 64) return (int64_t)v;
    uint64_t m = 1ULL << (bits - 1);
    if (v & m) v |= (~0ULL << bits);
    return (int64_t)v;
}

static int handle_process_fault(VM *vm, int error) {
    if (vm->cur_proc < 0 || vm->cur_proc >= DIMON64_MAX_PROCS ||
        !vm->procs[vm->cur_proc].used || !vm->procs[vm->cur_proc].isolated)
        return error;
    int slot = vm->cur_proc;
#ifndef BAREMETAL
    fprintf(stderr, "Dimon64: process %s (pid=%" PRIu64 ") fault %d at 0x%" PRIX64 "\n",
            vm->procs[slot].name, vm->procs[slot].pid, error, vm->pc);
#endif
    proc_release(vm, slot, error);
    (void)proc_resume_any(vm);
    return vm->halted ? error : 0;
}

/* ---------- step ---------- */
int vm_step(VM *vm) {
    if (!vm || !vm->mem) return -1;
    if (vm->halted) return 1;
    if (vm->max_steps && vm->steps >= vm->max_steps) return -100;

    uint32_t w = 0;
    int frc = fetch32(vm, vm->pc, &w);
    if (frc != 0) return handle_process_fault(vm, frc);

    uint8_t opcode = (uint8_t)(w & 0x7Fu);
    uint8_t rd = (uint8_t)((w >> 7) & 0x1Fu);
    uint8_t funct3 = (uint8_t)((w >> 12) & 0x7u);
    uint8_t rs1 = (uint8_t)((w >> 15) & 0x1Fu);
    uint8_t rs2 = (uint8_t)((w >> 20) & 0x1Fu);
    uint8_t funct7 = (uint8_t)((w >> 25) & 0x7Fu);
    uint64_t next_pc = vm->pc + 4;
    int err = 0;

    switch (opcode) {
        case DIMON64_OPCODE_OP: {
            uint64_t a = vm->regs[rs1], b = vm->regs[rs2], res = 0;
            if (funct7 == DIMON64_F7_MEXT) {
                switch (funct3) {
                    case 0x0: /* MUL */
                        res = a * b;
                        flags_logic(vm, res);
                        break;
                    case 0x4: /* DIV signed */
                        if ((int64_t)b == 0) { res = (uint64_t)(int64_t)-1; flags_logic(vm, res); }
                        else if (a == 0x8000000000000000ULL && b == 0xFFFFFFFFFFFFFFFFULL) {
                            res = a; flags_logic(vm, res);
                        } else { res = (uint64_t)((int64_t)a / (int64_t)b); flags_logic(vm, res); }
                        break;
                    case 0x5: /* DIVU */
                        if (b == 0) { res = ~0ULL; flags_logic(vm, res); }
                        else { res = a / b; flags_logic(vm, res); }
                        break;
                    case 0x6: /* REM signed */
                        if ((int64_t)b == 0) { res = a; flags_logic(vm, res); }
                        else { res = (uint64_t)((int64_t)a % (int64_t)b); flags_logic(vm, res); }
                        break;
                    default: err = -20; break;
                }
            } else if (funct7 == DIMON64_F7_BASE || funct7 == DIMON64_F7_ALT) {
                switch (funct3) {
                    case 0x0:
                        if (funct7 == DIMON64_F7_BASE) { res = a + b; flags_add(vm, a, b, res); }
                        else { res = a - b; flags_sub(vm, a, b, res); }
                        break;
                    case 0x1: { /* SLL */
                        unsigned sh = (unsigned)(b & 63u);
                        res = a << sh;
                        flags_logic(vm, res);
                        uint64_t ie = vm->flags & DIMON64_FLAG_IE;
                        /* carry = last bit shifted out */
                        vm->flags &= ~(DIMON64_FLAG_C);
                        if (sh != 0 && sh <= 64 && ((a >> (64 - sh)) & 1u)) vm->flags |= DIMON64_FLAG_C;
                        vm->flags |= ie;
                        /* fix Z/N after C tweak */
                        break;
                    }
                    case 0x2: /* SLT */
                        res = ((int64_t)a < (int64_t)b) ? 1 : 0;
                        flags_logic(vm, res);
                        break;
                    case 0x3: /* SLTU */
                        res = (a < b) ? 1 : 0;
                        flags_logic(vm, res);
                        break;
                    case 0x4: res = a ^ b; flags_logic(vm, res); break;
                    case 0x5:
                        if (funct7 == DIMON64_F7_BASE) { /* SRL */
                            unsigned sh = (unsigned)(b & 63u);
                            if (sh != 0) {
                                if (((a >> (sh - 1)) & 1u)) vm->flags |= DIMON64_FLAG_C;
                                else vm->flags &= ~DIMON64_FLAG_C;
                            }
                            res = a >> sh;
                            uint64_t ie = vm->flags & DIMON64_FLAG_IE;
                            uint64_t c = vm->flags & DIMON64_FLAG_C;
                            flags_logic(vm, res);
                            vm->flags |= c | ie;
                        } else { /* SRA */
                            unsigned sh = (unsigned)(b & 63u);
                            if (sh != 0) {
                                if (((a >> (sh - 1)) & 1u)) vm->flags |= DIMON64_FLAG_C;
                                else vm->flags &= ~DIMON64_FLAG_C;
                            }
                            res = (uint64_t)(((int64_t)a) >> sh);
                            uint64_t ie = vm->flags & DIMON64_FLAG_IE;
                            uint64_t c = vm->flags & DIMON64_FLAG_C;
                            flags_logic(vm, res);
                            vm->flags |= c | ie;
                        }
                        break;
                    case 0x6: res = a | b; flags_logic(vm, res); break;
                    case 0x7: res = a & b; flags_logic(vm, res); break;
                    default: err = -20; break;
                }
            } else {
                err = -20;
            }
            if (!err) {
                if (rd != 0) vm->regs[rd] = res;
                vm->pc = next_pc;
            }
            break;
        }
        case DIMON64_OPCODE_OP_IMM: {
            int32_t imm12 = (int32_t)sign_extend((w >> 20) & 0xFFFu, 12);
            uint64_t a = vm->regs[rs1];
            uint64_t res = 0;
            switch (funct3) {
                case 0x0: /* ADDI */
                    res = a + (uint64_t)(int64_t)imm12;
                    flags_add(vm, a, (uint64_t)(int64_t)imm12, res);
                    if (rd != 0) vm->regs[rd] = res;
                    vm->pc = next_pc;
                    break;
                case 0x2: /* SLTI */
                    res = ((int64_t)a < (int64_t)imm12) ? 1 : 0;
                    flags_logic(vm, res);
                    if (rd != 0) vm->regs[rd] = res;
                    vm->pc = next_pc;
                    break;
                case 0x3: /* SLTIU */
                    res = (a < (uint64_t)sign_extend((uint64_t)(uint32_t)imm12, 32)) ? 1 : 0;
                    /* RISC-V SLTIU sign-extends imm then compares unsigned */
                    res = (a < (uint64_t)(int64_t)imm12) ? 1 : 0;
                    flags_logic(vm, res);
                    if (rd != 0) vm->regs[rd] = res;
                    vm->pc = next_pc;
                    break;
                case 0x4: /* XORI */
                    res = a ^ (uint64_t)(int64_t)imm12;
                    flags_logic(vm, res);
                    if (rd != 0) vm->regs[rd] = res;
                    vm->pc = next_pc;
                    break;
                case 0x6: /* ORI */
                    res = a | (uint64_t)(int64_t)imm12;
                    flags_logic(vm, res);
                    if (rd != 0) vm->regs[rd] = res;
                    vm->pc = next_pc;
                    break;
                case 0x7: /* ANDI */
                    res = a & (uint64_t)(int64_t)imm12;
                    flags_logic(vm, res);
                    if (rd != 0) vm->regs[rd] = res;
                    vm->pc = next_pc;
                    break;
                case 0x1: { /* SLLI */
                    uint32_t hi = (w >> 25) & 0x7Fu;
                    if (hi != 0x00) { err = -21; break; }
                    unsigned sh = (unsigned)(((w >> 20) & 0x1Fu) | (((w >> 25) & 0x1u) << 5));
                    /* RV64 shamt 6 bits: bit5 is imm[5], top bits must be 0 */
                    sh = (unsigned)((w >> 20) & 0x3Fu);
                    if (((w >> 25) & 0x7Eu) != 0) { err = -21; break; }
                    res = a << sh;
                    flags_logic(vm, res);
                    if (rd != 0) vm->regs[rd] = res;
                    vm->pc = next_pc;
                    break;
                }
                case 0x5: { /* SRLI / SRAI */
                    uint32_t hi = (w >> 25) & 0x7Fu;
                    unsigned sh = (unsigned)((w >> 20) & 0x3Fu);
                    if (hi == 0x00) {
                        res = a >> sh;
                        flags_logic(vm, res);
                        if (rd != 0) vm->regs[rd] = res;
                        vm->pc = next_pc;
                    } else if (hi == 0x20) {
                        res = (uint64_t)(((int64_t)a) >> sh);
                        flags_logic(vm, res);
                        if (rd != 0) vm->regs[rd] = res;
                        vm->pc = next_pc;
                    } else { err = -21; }
                    break;
                }
                default: err = -21; break;
            }
            break;
        }
        case DIMON64_OPCODE_LOAD: {
            int32_t off = (int32_t)sign_extend((w >> 20) & 0xFFFu, 12);
            uint64_t base = vm->regs[rs1];
            uint64_t addr = base + (uint64_t)(int64_t)off;
            uint64_t raw = 0;
            unsigned sz = 0;
            int is_signed = 1;
            switch (funct3) {
                case 0x0: sz = 1; is_signed = 1; break;
                case 0x1: sz = 2; is_signed = 1; break;
                case 0x2: sz = 4; is_signed = 1; break;
                case 0x3: sz = 8; is_signed = 1; break;
                case 0x4: sz = 1; is_signed = 0; break;
                case 0x5: sz = 2; is_signed = 0; break;
                case 0x6: sz = 4; is_signed = 0; break;
                default: err = -22; break;
            }
            if (!err) {
                if (mem_load(vm, addr, sz, &raw) != 0) { err = -23; break; }
                uint64_t res;
                if (is_signed) {
                    if (sz == 1) res = (uint64_t)sign_extend(raw, 8);
                    else if (sz == 2) res = (uint64_t)sign_extend(raw, 16);
                    else if (sz == 4) res = (uint64_t)sign_extend(raw, 32);
                    else res = raw;
                } else res = raw;
                if (rd != 0) vm->regs[rd] = res;
                vm->pc = next_pc;
            }
            break;
        }
        case DIMON64_OPCODE_STORE: {
            uint32_t lo5 = (w >> 7) & 0x1Fu;
            uint32_t hi7 = (w >> 25) & 0x7Fu;
            int32_t off = (int32_t)sign_extend((hi7 << 5) | lo5, 12);
            uint64_t base = vm->regs[rs1];
            uint64_t addr = base + (uint64_t)(int64_t)off;
            uint64_t val = vm->regs[rs2];
            unsigned sz = 0;
            switch (funct3) {
                case 0x0: sz = 1; break;
                case 0x1: sz = 2; break;
                case 0x2: sz = 4; break;
                case 0x3: sz = 8; break;
                default: err = -24; break;
            }
            if (!err) {
                if (mem_store(vm, addr, sz, val) != 0) { err = -25; break; }
                vm->pc = next_pc;
            }
            break;
        }
        case DIMON64_OPCODE_BRANCH: {
            uint32_t b12 = (w >> 31) & 0x1u;
            uint32_t b11 = (w >> 7) & 0x1u;
            uint32_t b10_5 = (w >> 25) & 0x3Fu;
            uint32_t b4_1 = (w >> 8) & 0xFu;
            int32_t off = (int32_t)((b12 << 12) | (b11 << 11) | (b10_5 << 5) | (b4_1 << 1));
            off = (int32_t)sign_extend((uint64_t)(uint32_t)off, 13);
            uint64_t a = vm->regs[rs1], b = vm->regs[rs2];
            int take = 0;
            switch (funct3) {
                case 0x0: take = (a == b); break;
                case 0x1: take = (a != b); break;
                case 0x4: take = ((int64_t)a < (int64_t)b); break;
                case 0x5: take = ((int64_t)a >= (int64_t)b); break;
                case 0x6: take = (a < b); break;
                case 0x7: take = (a >= b); break;
                default: err = -26; break;
            }
            if (!err) {
                if (take) {
                    uint64_t t = vm->pc + (uint64_t)(int64_t)off;
                    if ((t % 4u) != 0 || t >= vm->memsize) { err = -27; break; }
                    vm->pc = t;
                } else vm->pc = next_pc;
            }
            break;
        }
        case DIMON64_OPCODE_JALR: {
            if (funct3 != 0x0) { err = -28; break; }
            int32_t off = (int32_t)sign_extend((w >> 20) & 0xFFFu, 12);
            uint64_t base = vm->regs[rs1];
            uint64_t target = (base + (uint64_t)(int64_t)off) & ~1ULL;
            if ((target % 4u) != 0 || target >= vm->memsize) { err = -29; break; }
            uint64_t ret = vm->pc + 4;
            if (rd != 0) vm->regs[rd] = ret;
            vm->pc = target;
            break;
        }
        case DIMON64_OPCODE_JAL: {
            uint32_t b20 = (w >> 31) & 0x1u;
            uint32_t b10_1 = (w >> 21) & 0x3FFu;
            uint32_t b11 = (w >> 20) & 0x1u;
            uint32_t b19_12 = (w >> 12) & 0xFFu;
            int32_t off = (int32_t)((b20 << 20) | (b19_12 << 12) | (b11 << 11) | (b10_1 << 1));
            off = (int32_t)sign_extend((uint64_t)(uint32_t)off, 21);
            uint64_t target = vm->pc + (uint64_t)(int64_t)off;
            if ((target % 4u) != 0 || target >= vm->memsize) { err = -30; break; }
            uint64_t ret = vm->pc + 4;
            if (rd != 0) vm->regs[rd] = ret;
            vm->pc = target;
            break;
        }
        case DIMON64_OPCODE_LUI:
        case DIMON64_OPCODE_AUIPC: {
            int32_t imm20 = (int32_t)((w >> 12) & 0xFFFFFu);
            /* sign-extend 20-bit then shift */
            int64_t s = sign_extend((uint64_t)(uint32_t)imm20, 20);
            uint64_t val;
            if (opcode == DIMON64_OPCODE_LUI) {
                /* RV64: sign-extend 32-bit result to 64 */
                int32_t v32 = (int32_t)(s << 12);
                val = (uint64_t)(int64_t)v32;
                flags_logic(vm, val);
            } else {
                int32_t v32 = (int32_t)(s << 12);
                val = vm->pc + (uint64_t)(int64_t)v32;
                flags_logic(vm, val);
            }
            if (rd != 0) vm->regs[rd] = val;
            vm->pc = next_pc;
            break;
        }
        case DIMON64_OPCODE_SYSTEM: {
            if (funct3 != 0x0) { err = -31; break; }
            uint32_t imm = (w >> 20) & 0xFFFu;
            if (imm == DIMON64_SYS_ECALL) {
                uint64_t id = vm->regs[17]; /* a7 */
                vm->pc = next_pc;
                /* keep proc in sync before potential switch */
                do_syscall(vm, id);
                /* do_syscall may have changed pc via schedule; if it did,
                   pc already points to next task. Detect: YIELD/SPAWN/SLEEP/EXIT
                   schedule changes pc. Our vm->pc was set to next_pc before call,
                   but schedule overwrote it. Need to preserve scheduled pc:
                   do_syscall's schedule sets vm->pc directly, so we must not
                   overwrite after. Since we set pc before call, and schedule
                   overwrites, final pc is correct. */
                /* proc mirror already handled inside syscalls */
            } else if (imm == DIMON64_SYS_EBREAK) {
                vm->pc = next_pc;
                if (vm->procs[vm->cur_proc].isolated) {
                    int slot = vm->cur_proc; proc_release(vm, slot, 0); (void)proc_resume_any(vm);
                    vm->steps++; vm->regs[0] = 0; return vm->halted ? 1 : 0;
                } else vm->halted = 1;
                vm->steps++;
                vm->regs[0] = 0;
                return 1;
            } else if (imm == DIMON64_SYS_IRET) {
                vm->pc = vm->epc;
                vm->flags = vm->eflags;
                vm->in_isr = 0;
                vm->regs[0] = 0;
                /* Keep current proc resume point in sync with trap return */
                if (vm->cur_proc >= 0 && vm->cur_proc < DIMON64_MAX_PROCS) {
                    Dimon64Proc *cp = &vm->procs[vm->cur_proc];
                    if (cp->used) {
                        cp->pc = vm->pc;
                        cp->flags = vm->flags;
                        memcpy(cp->regs, vm->regs, sizeof(vm->regs));
                    }
                }
            } else {
                /* INT n: immediate syscall ID */
                vm->pc = next_pc;
                do_syscall(vm, (uint64_t)imm);
            }
            break;
        }
        default:
            err = -1;
            break;
    }

    if (err != 0) return handle_process_fault(vm, err);
    vm->regs[0] = 0;
    vm->steps++;
    if (vm->cur_proc >= 0 && vm->cur_proc < DIMON64_MAX_PROCS && vm->procs[vm->cur_proc].used)
        vm->procs[vm->cur_proc].cpu_steps++;
    vm->cycle_counter++;

    /* timer */
    if (vm->cycle_counter >= vm->timer_period) {
        vm->cycle_counter = 0;
        timer_tick(vm);
        vm->regs[0] = 0;
    }
    return 0;
}

int vm_run(VM *vm) {
    if (!vm) return -1;
    while (!vm->halted) {
        int rc = vm_step(vm);
        if (rc != 0) return rc;
    }
    return 1;
}

/* ---------- disassembler ---------- */
static void fmt_reg(char *buf, size_t n, int r) {
    snprintf(buf, n, "%s", dimon64_reg_abi(r));
}

int dimon64_disasm(VM *vm, uint64_t addr, char *out, size_t outsz) {
    if (!vm || !out || outsz == 0) return 4;
    if ((addr % 4u) != 0 || addr + 4 > vm->memsize) {
        snprintf(out, outsz, "??? (bad addr)");
        return 4;
    }
    uint32_t w = (uint32_t)vm->mem[addr] |
                 ((uint32_t)vm->mem[addr + 1] << 8) |
                 ((uint32_t)vm->mem[addr + 2] << 16) |
                 ((uint32_t)vm->mem[addr + 3] << 24);
    uint8_t opcode = (uint8_t)(w & 0x7Fu);
    uint8_t rd = (uint8_t)((w >> 7) & 0x1Fu);
    uint8_t f3 = (uint8_t)((w >> 12) & 0x7u);
    uint8_t rs1 = (uint8_t)((w >> 15) & 0x1Fu);
    uint8_t rs2 = (uint8_t)((w >> 20) & 0x1Fu);
    uint8_t f7 = (uint8_t)((w >> 25) & 0x7Fu);
    char s_rd[16], s_rs1[16], s_rs2[16];
    fmt_reg(s_rd, sizeof(s_rd), rd);
    fmt_reg(s_rs1, sizeof(s_rs1), rs1);
    fmt_reg(s_rs2, sizeof(s_rs2), rs2);

    switch (opcode) {
        case DIMON64_OPCODE_OP: {
            const char *mn = "???";
            if (f7 == DIMON64_F7_MEXT) {
                if (f3 == 0x0) mn = "MUL";
                else if (f3 == 0x4) mn = "DIV";
                else if (f3 == 0x5) mn = "DIVU";
                else if (f3 == 0x6) mn = "REM";
            } else if (f7 == DIMON64_F7_BASE) {
                if (f3 == 0x0) mn = "ADD";
                else if (f3 == 0x1) mn = "SLL";
                else if (f3 == 0x2) mn = "SLT";
                else if (f3 == 0x3) mn = "SLTU";
                else if (f3 == 0x4) mn = "XOR";
                else if (f3 == 0x5) mn = "SRL";
                else if (f3 == 0x6) mn = "OR";
                else if (f3 == 0x7) mn = "AND";
            } else if (f7 == DIMON64_F7_ALT) {
                if (f3 == 0x0) mn = "SUB";
                else if (f3 == 0x5) mn = "SRA";
            }
            snprintf(out, outsz, "%s %s, %s, %s", mn, s_rd, s_rs1, s_rs2);
            break;
        }
        case DIMON64_OPCODE_OP_IMM: {
            int32_t imm = (int32_t)sign_extend((w >> 20) & 0xFFFu, 12);
            if (f3 == 0x0) snprintf(out, outsz, "ADDI %s, %s, %d", s_rd, s_rs1, imm);
            else if (f3 == 0x2) snprintf(out, outsz, "SLTI %s, %s, %d", s_rd, s_rs1, imm);
            else if (f3 == 0x3) snprintf(out, outsz, "SLTIU %s, %s, %d", s_rd, s_rs1, imm);
            else if (f3 == 0x4) snprintf(out, outsz, "XORI %s, %s, %d", s_rd, s_rs1, imm);
            else if (f3 == 0x6) snprintf(out, outsz, "ORI %s, %s, %d", s_rd, s_rs1, imm);
            else if (f3 == 0x7) snprintf(out, outsz, "ANDI %s, %s, %d", s_rd, s_rs1, imm);
            else if (f3 == 0x1) {
                unsigned sh = (w >> 20) & 0x3Fu;
                snprintf(out, outsz, "SLLI %s, %s, %u", s_rd, s_rs1, sh);
            } else if (f3 == 0x5) {
                unsigned sh = (w >> 20) & 0x3Fu;
                snprintf(out, outsz, "%s %s, %s, %u", (f7 == 0x20) ? "SRAI" : "SRLI",
                         s_rd, s_rs1, sh);
            } else snprintf(out, outsz, "OP-IMM ???");
            break;
        }
        case DIMON64_OPCODE_LOAD: {
            int32_t off = (int32_t)sign_extend((w >> 20) & 0xFFFu, 12);
            const char *mn = "???";
            if (f3 == 0x0) mn = "LB";
            else if (f3 == 0x1) mn = "LH";
            else if (f3 == 0x2) mn = "LW";
            else if (f3 == 0x3) mn = "LD";
            else if (f3 == 0x4) mn = "LBU";
            else if (f3 == 0x5) mn = "LHU";
            else if (f3 == 0x6) mn = "LWU";
            snprintf(out, outsz, "%s %s, %d(%s)", mn, s_rd, off, s_rs1);
            break;
        }
        case DIMON64_OPCODE_STORE: {
            uint32_t lo = (w >> 7) & 0x1Fu, hi = (w >> 25) & 0x7Fu;
            int32_t off = (int32_t)sign_extend((hi << 5) | lo, 12);
            const char *mn = "???";
            if (f3 == 0x0) mn = "SB";
            else if (f3 == 0x1) mn = "SH";
            else if (f3 == 0x2) mn = "SW";
            else if (f3 == 0x3) mn = "SD";
            snprintf(out, outsz, "%s %s, %d(%s)", mn, s_rs2, off, s_rs1);
            break;
        }
        case DIMON64_OPCODE_BRANCH: {
            uint32_t b12 = (w >> 31) & 0x1u, b11 = (w >> 7) & 0x1u;
            uint32_t b10_5 = (w >> 25) & 0x3Fu, b4_1 = (w >> 8) & 0xFu;
            int32_t off = (int32_t)((b12 << 12) | (b11 << 11) | (b10_5 << 5) | (b4_1 << 1));
            off = (int32_t)sign_extend((uint64_t)(uint32_t)off, 13);
            const char *mn = "???";
            if (f3 == 0x0) mn = "BEQ";
            else if (f3 == 0x1) mn = "BNE";
            else if (f3 == 0x4) mn = "BLT";
            else if (f3 == 0x5) mn = "BGE";
            else if (f3 == 0x6) mn = "BLTU";
            else if (f3 == 0x7) mn = "BGEU";
            snprintf(out, outsz, "%s %s, %s, %d (0x%llX)", mn, s_rs1, s_rs2, off,
                     (unsigned long long)(addr + (int64_t)off));
            break;
        }
        case DIMON64_OPCODE_JALR:
            snprintf(out, outsz, "JALR %s, %d(%s)", s_rd,
                     (int)(int32_t)sign_extend((w >> 20) & 0xFFFu, 12), s_rs1);
            break;
        case DIMON64_OPCODE_JAL: {
            uint32_t b20 = (w >> 31) & 0x1u, b10_1 = (w >> 21) & 0x3FFu;
            uint32_t b11 = (w >> 20) & 0x1u, b19_12 = (w >> 12) & 0xFFu;
            int32_t off = (int32_t)((b20 << 20) | (b19_12 << 12) | (b11 << 11) | (b10_1 << 1));
            off = (int32_t)sign_extend((uint64_t)(uint32_t)off, 21);
            snprintf(out, outsz, "JAL %s, %d (0x%llX)", s_rd, off,
                     (unsigned long long)(addr + (int64_t)off));
            break;
        }
        case DIMON64_OPCODE_LUI:
        case DIMON64_OPCODE_AUIPC: {
            int32_t imm = (int32_t)((w >> 12) & 0xFFFFFu);
            imm = (int32_t)sign_extend((uint64_t)(uint32_t)imm, 20);
            snprintf(out, outsz, "%s %s, 0x%X", (opcode == DIMON64_OPCODE_LUI) ? "LUI" : "AUIPC",
                     s_rd, (unsigned)(imm & 0xFFFFF));
            break;
        }
        case DIMON64_OPCODE_SYSTEM: {
            uint32_t imm = (w >> 20) & 0xFFFu;
            if (imm == 0) snprintf(out, outsz, "ECALL");
            else if (imm == 1) snprintf(out, outsz, "EBREAK");
            else if (imm == 0x102) snprintf(out, outsz, "IRET");
            else snprintf(out, outsz, "INT %u", imm);
            break;
        }
        default:
            snprintf(out, outsz, ".word 0x%08X", w);
            break;
    }
    return 4;
}

int vm_disasm(VM *vm, uint64_t addr, char *out, size_t outsz) {
    return dimon64_disasm(vm, addr, out, outsz);
}
