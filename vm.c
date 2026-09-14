/* DimonVirtualCPU-64: 64-bit RISC-V style virtual machine.
 * All code, comments and identifiers are in English.
 */
#include "dimon64.h"
#include "font8x16.h"

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
void vm_event_push_ext(VM *vm, uint8_t type, uint16_t code, uint16_t data, uint8_t button) {
    if (!vm) return;
    int next = (vm->event_tail + 1) % VM_EVENT_QUEUE_SIZE;
    if (next != vm->event_head) {
        vm->event_queue[vm->event_tail].type = type;
        vm->event_queue[vm->event_tail].button = button;
        vm->event_queue[vm->event_tail].code = code;
        vm->event_queue[vm->event_tail].data = data;
        vm->event_tail = next;
    }
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
    vm->event_head = (vm->event_head + 1) % VM_EVENT_QUEUE_SIZE;
    return 1;
}

int vm_event_pop(VM *vm, uint8_t *type, uint16_t *code, uint16_t *data) {
    return vm_event_pop_ext(vm, type, code, data, NULL);
}

void vm_gui_draw_pixel(VM *vm, int x, int y, uint32_t color32) {
    if (!vm || !vm->mem) return;
    if (x < 0 || x >= DIMON64_LFB_WIDTH || y < 0 || y >= DIMON64_LFB_HEIGHT) return;
    uint64_t addr = DIMON64_VRAM_BASE + (uint64_t)(y * DIMON64_LFB_WIDTH + x) * 4ULL;
    if (addr + 4 > vm->memsize) return;
    *(uint32_t *)(vm->mem + addr) = color32;
    vm->gui_dirty = 1;
}

void vm_gui_fill_rect(VM *vm, int x, int y, int w, int h, uint32_t color32) {
    if (!vm || !vm->mem || w <= 0 || h <= 0) return;
    int x0 = x < 0 ? 0 : x;
    int y0 = y < 0 ? 0 : y;
    int x1 = x + w;
    int y1 = y + h;
    if (x1 > DIMON64_LFB_WIDTH) x1 = DIMON64_LFB_WIDTH;
    if (y1 > DIMON64_LFB_HEIGHT) y1 = DIMON64_LFB_HEIGHT;
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
    int cur_x = x;
    int draw_bg = ((bg & 0xFF000000) != 0);

    while (*text) {
        uint8_t ch = (uint8_t)*text;
        if (cur_x + 8 > 0 && cur_x < DIMON64_LFB_WIDTH && y + 16 > 0 && y < DIMON64_LFB_HEIGHT) {
            for (int r = 0; r < 16; r++) {
                int py = y + r;
                if (py < 0 || py >= DIMON64_LFB_HEIGHT) continue;
                uint8_t bits = font8x16[ch][r];
                for (int c = 0; c < 8; c++) {
                    int px = cur_x + c;
                    if (px < 0 || px >= DIMON64_LFB_WIDTH) continue;
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
        text++;
    }
    vm->gui_dirty = 1;
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
        vm->procs[i].used = 0;
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
        if (f) {
            fseek(f, (long)(lba * DISK_SECTOR_SIZE), SEEK_SET);
            size_t w = fwrite(vm->disk_data + (size_t)lba * DISK_SECTOR_SIZE, 1, (size_t)bytes, f);
            (void)w;
            fclose(f);
        }
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

/* ---------- low-level memory with MMIO ---------- */
static int read_u8(VM *vm, uint64_t addr, uint8_t *out) {
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
    return 1;
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
            int rc = vm_disk_read(vm, lba, a1, a2);
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
            int rc = vm_disk_write(vm, lba, a1, a2);
            vm->regs[10] = (uint64_t)rc;
            if (rc == DISK_ERR_NONE) vm->flags &= ~DIMON64_FLAG_C;
            else vm->flags |= DIMON64_FLAG_C;
            break;
        }
        case DIMON64_SYS_GUI_INIT: {
            vm->gui_active = 1;
            vm->regs[10] = DIMON64_VRAM_COLS;
            vm->regs[11] = DIMON64_VRAM_ROWS;
            vm->regs[12] = DIMON64_VRAM_BASE;
            vm->flags &= ~DIMON64_FLAG_C;
            if (vm->gui_init_cb) vm->gui_init_cb(vm->gui_userdata);
            break;
        }
        case DIMON64_SYS_GUI_POLL_EVENT: {
            if (vm->gui_poll_cb) vm->gui_poll_cb(vm->gui_userdata);
            uint8_t t = 0, b = 0; uint16_t c = 0, d = 0;
            if (vm_event_pop_ext(vm, &t, &c, &d, &b)) {
                vm->regs[10] = (uint64_t)t;
                vm->regs[11] = (uint64_t)c;
                vm->regs[12] = (uint64_t)d;
                vm->regs[13] = (uint64_t)b;
            } else {
                vm->regs[10] = 0;
                vm->regs[11] = 0;
                vm->regs[12] = 0;
                vm->regs[13] = 0;
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
            if (w > 0 && h > 0 && dx >= 0 && dy >= 0 && dx + w <= DIMON64_LFB_WIDTH && dy + h <= DIMON64_LFB_HEIGHT) {
                uint32_t *vram = (uint32_t *)(vm->mem + DIMON64_VRAM_BASE);
                for (int y = 0; y < h; y++) {
                    uint64_t srow = src + (uint64_t)y * (uint64_t)w * 4;
                    if (srow + (uint64_t)w * 4 <= vm->memsize) {
                        memcpy(&vram[(dy + y) * DIMON64_LFB_WIDTH + dx], vm->mem + srow, (size_t)w * 4);
                    }
                }
                vm->gui_dirty = 1;
            }
            break;
        }
        case DIMON64_SYS_MEMSET: {
            uint64_t dst = a0;
            uint32_t val = (uint32_t)a1;
            uint64_t cnt = a2;
            if (dst + cnt * 4 <= vm->memsize) {
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
            Dimon64Proc *cur = &vm->procs[vm->cur_proc];
            cur->state = DIMON64_PROC_FREE;
            cur->used = 0;
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
            if (a0 != 0 && ((a0 % 4u) != 0 || a0 + 4 > vm->memsize)) {
                vm->regs[10] = (uint64_t)(int64_t)-1;
            } else {
                vm->timer_vector = a0;
                vm->regs[10] = 0;
            }
            break;
        }
        case DIMON64_SYS_SET_TIMER_PERIOD: {
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

/* ---------- step ---------- */
int vm_step(VM *vm) {
    if (!vm || !vm->mem) return -1;
    if (vm->halted) return 1;
    if (vm->max_steps && vm->steps >= vm->max_steps) return -100;

    uint32_t w = 0;
    int frc = fetch32(vm, vm->pc, &w);
    if (frc != 0) return frc;

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
                vm->halted = 1;
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

    if (err != 0) return err;
    vm->regs[0] = 0;
    vm->steps++;
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
