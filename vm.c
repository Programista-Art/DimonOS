#include "dimon16.h"

#ifndef BAREMETAL
#include <time.h>
#include <sys/time.h>
#else
extern uint32_t kernel_get_ticks_ms(void);
extern void baremetal_putchar(char c);
#endif

static uint32_t get_time_ms(void) {
#ifdef BAREMETAL
    return kernel_get_ticks_ms();
#else
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint32_t)(tv.tv_sec * 1000 + tv.tv_usec / 1000);
#endif
}

void vm_event_push(VM *vm, uint8_t type, uint16_t code, uint16_t data) {
    int next = (vm->event_tail + 1) % VM_EVENT_QUEUE_SIZE;
    if (next != vm->event_head) {
        vm->event_queue[vm->event_tail].type = type;
        vm->event_queue[vm->event_tail].code = code;
        vm->event_queue[vm->event_tail].data = data;
        vm->event_tail = next;
    }
}

int vm_event_pop(VM *vm, uint8_t *type, uint16_t *code, uint16_t *data) {
    if (vm->event_head == vm->event_tail) {
        *type = EVT_NONE;
        *code = 0;
        *data = 0;
        return 0;
    }
    *type = vm->event_queue[vm->event_head].type;
    *code = vm->event_queue[vm->event_head].code;
    *data = vm->event_queue[vm->event_head].data;
    vm->event_head = (vm->event_head + 1) % VM_EVENT_QUEUE_SIZE;
    return 1;
}

void vm_gui_draw_rect(VM *vm, int x, int y, int w, int h, uint8_t ch, uint8_t attr) {
    for (int r = 0; r < h; r++) {
        int cy = y + r;
        if (cy < 0 || cy >= VRAM_ROWS) continue;
        for (int c = 0; c < w; c++) {
            int cx = x + c;
            if (cx < 0 || cx >= VRAM_COLS) continue;
            uint16_t addr = (uint16_t)(VRAM_ADDR + (cy * VRAM_COLS + cx) * 2);
            vm->mem[addr] = ch;
            vm->mem[addr + 1] = attr;
        }
    }
    vm->gui_dirty = 1;
}

void vm_gui_draw_text(VM *vm, int x, int y, const char *text, uint8_t attr) {
    if (y < 0 || y >= VRAM_ROWS) return;
    int cx = x;
    while (*text && cx < VRAM_COLS) {
        if (cx >= 0) {
            uint16_t addr = (uint16_t)(VRAM_ADDR + (y * VRAM_COLS + cx) * 2);
            vm->mem[addr] = (uint8_t)*text;
            vm->mem[addr + 1] = attr;
        }
        cx++;
        text++;
    }
    vm->gui_dirty = 1;
}

void vm_init(VM *vm) {
    memset(vm, 0, sizeof(*vm));
    vm->SP = 0xFFFE;
    vm->PC = 0x0000;
    vm->max_steps = 0;
    vm->disk_data = NULL;
    vm->disk_sectors = 0;
    vm->disk_writable = 0;
    vm->disk_path[0] = 0;
    vm->event_head = 0;
    vm->event_tail = 0;
    vm->gui_active = 0;
    vm->gui_dirty = 0;
    vm->gui_init_cb = NULL;
    vm->gui_flush_cb = NULL;
    vm->gui_poll_cb = NULL;
    vm->gui_userdata = NULL;
}

void vm_free(VM *vm) {
#ifndef BAREMETAL
    if (vm && vm->disk_data) {
        free(vm->disk_data);
        vm->disk_data = NULL;
    }
#endif
    if (vm) vm->disk_sectors = 0;
}

void vm_disk_detach(VM *vm) {
    vm_free(vm);
    if (vm) {
        vm->disk_writable = 0;
        vm->disk_path[0] = 0;
    }
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
    if (fread(buf, 1, (size_t)n, f) != (size_t)n) {
        fclose(f); free(buf); return -4;
    }
    fclose(f);
    vm_disk_detach(vm);
    vm->disk_data = buf;
    vm->disk_sectors = sectors;
    vm->disk_writable = writable ? 1 : 0;
    strncpy(vm->disk_path, path, sizeof(vm->disk_path) - 1);
    return 0;
}
#endif

/* Load sector 0 (boot) to RAM at load_addr (typically 0). */
int vm_disk_boot(VM *vm, uint16_t load_addr) {
    if (!vm->disk_data || vm->disk_sectors < 1) return DISK_ERR_NODISK;
    if ((size_t)load_addr + DISK_SECTOR_SIZE > MEM_SIZE) return DISK_ERR_RAM;
    memcpy(vm->mem + load_addr, vm->disk_data, DISK_SECTOR_SIZE);
    return DISK_ERR_NONE;
}

int vm_disk_read(VM *vm, uint32_t lba, uint16_t ram_addr, uint16_t count) {
    if (!vm->disk_data || vm->disk_sectors == 0) return DISK_ERR_NODISK;
    if (count == 0) return DISK_ERR_NONE;
    if (lba >= vm->disk_sectors) return DISK_ERR_RANGE;
    if ((uint64_t)lba + count > vm->disk_sectors) return DISK_ERR_RANGE;
    uint64_t bytes = (uint64_t)count * DISK_SECTOR_SIZE;
    if ((uint32_t)ram_addr + bytes > MEM_SIZE) return DISK_ERR_RAM;
    memcpy(vm->mem + ram_addr,
           vm->disk_data + (size_t)lba * DISK_SECTOR_SIZE,
           (size_t)bytes);
    return DISK_ERR_NONE;
}

int vm_disk_write(VM *vm, uint32_t lba, uint16_t ram_addr, uint16_t count) {
    if (!vm->disk_data || vm->disk_sectors == 0) return DISK_ERR_NODISK;
    if (!vm->disk_writable) return DISK_ERR_READONLY;
    if (count == 0) return DISK_ERR_NONE;
    if (lba >= vm->disk_sectors) return DISK_ERR_RANGE;
    if ((uint64_t)lba + count > vm->disk_sectors) return DISK_ERR_RANGE;
    uint64_t bytes = (uint64_t)count * DISK_SECTOR_SIZE;
    if ((uint32_t)ram_addr + bytes > MEM_SIZE) return DISK_ERR_RAM;
    memcpy(vm->disk_data + (size_t)lba * DISK_SECTOR_SIZE,
           vm->mem + ram_addr,
           (size_t)bytes);
#ifndef BAREMETAL
    /* Persist to host disk image file if writable */
    if (vm->disk_path[0]) {
        FILE *f = fopen(vm->disk_path, "r+b");
        if (f) {
            fseek(f, (long)(lba * DISK_SECTOR_SIZE), SEEK_SET);
            size_t w = fwrite(vm->disk_data + (size_t)lba * DISK_SECTOR_SIZE,
                              1, (size_t)bytes, f);
            (void)w;
            fclose(f);
        }
    }
#endif
    return DISK_ERR_NONE;
}

int vm_load_buf(VM *vm, const uint8_t *buf, size_t len, uint16_t load_addr) {
    if ((size_t)load_addr + len > MEM_SIZE) return -1;
    memcpy(vm->mem + load_addr, buf, len);
    return 0;
}

#ifndef BAREMETAL
int vm_load(VM *vm, const char *path, uint16_t load_addr) {
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n < 0) { fclose(f); return -1; }
    if ((size_t)load_addr + (size_t)n > MEM_SIZE) { fclose(f); return -1; }
    if (fread(vm->mem + load_addr, 1, (size_t)n, f) != (size_t)n) {
        fclose(f);
        return -1;
    }
    fclose(f);
    return 0;
}
#endif

void vm_reset(VM *vm, uint16_t start_pc) {
    vm->PC = start_pc;
    vm->SP = 0xFFFE;
    vm->halted = 0;
    vm->steps = 0;
    memset(vm->R, 0, sizeof(vm->R));
    vm->Z = 0;
    vm->C = 0;
}

#ifndef BAREMETAL
void vm_dump_regs(VM *vm, FILE *out) {
    fprintf(out, "PC=%04X SP=%04X Z=%d C=%d halted=%d steps=%llu\n",
            vm->PC, vm->SP, vm->Z, vm->C, vm->halted,
            (unsigned long long)vm->steps);
    for (int i = 0; i < NUM_REGS; i++) {
        fprintf(out, "R%d=%04X (%5u)  ", i, vm->R[i], vm->R[i]);
        if (i % 4 == 3) fprintf(out, "\n");
    }
    if (NUM_REGS % 4) fprintf(out, "\n");
}
#endif

void vm_stb(VM *vm, uint16_t addr, uint8_t v) { vm->mem[addr] = v; }
uint8_t vm_ldb(VM *vm, uint16_t addr) { return vm->mem[addr]; }

const char *op_name(uint8_t op) {
    switch (op) {
        case OP_HLT: return "HLT";
        case OP_NOP: return "NOP";
        case OP_MOV: return "MOV";
        case OP_ADD: return "ADD";
        case OP_SUB: return "SUB";
        case OP_MUL: return "MUL";
        case OP_DIV: return "DIV";
        case OP_AND: return "AND";
        case OP_OR:  return "OR";
        case OP_XOR: return "XOR";
        case OP_CMP: return "CMP";
        case OP_NOT: return "NOT";
        case OP_SHL: return "SHL";
        case OP_SHR: return "SHR";
        case OP_INC: return "INC";
        case OP_DEC: return "DEC";
        case OP_PUSH: return "PUSH";
        case OP_POP: return "POP";
        case OP_JMP: return "JMP";
        case OP_JZ:  return "JZ";
        case OP_JNZ: return "JNZ";
        case OP_JC:  return "JC";
        case OP_JNC: return "JNC";
        case OP_CALL: return "CALL";
        case OP_RET: return "RET";
        case OP_IN:  return "IN";
        case OP_OUT: return "OUT";
        case OP_INT: return "INT";
        case OP_IRET: return "IRET";
        case OP_LDB: return "LDB";
        case OP_STB: return "STB";
        default: return "???";
    }
}

const char *reg_name(int r) {
    static const char *n[] = {"R0","R1","R2","R3","R4","R5","R6","R7"};
    if (r < 0 || r > 7) return "R?";
    return n[r];
}

/* Memory fetch helpers */
static uint8_t fetch8(VM *vm, uint16_t *pc, int *err) {
    (void)err;
    *pc = (uint16_t)(*pc + 1);
    return vm->mem[(uint16_t)(*pc - 1)];
}

static uint16_t fetch16(VM *vm, uint16_t *pc, int *err) {
    uint8_t lo = fetch8(vm, pc, err);
    uint8_t hi = fetch8(vm, pc, err);
    return (uint16_t)(lo | ((uint16_t)hi << 8));
}

static uint16_t mem_read16(VM *vm, uint16_t addr, int *err) {
    (void)err;
    uint8_t lo = vm->mem[addr];
    uint8_t hi = vm->mem[(uint16_t)(addr + 1)];
    return (uint16_t)(lo | ((uint16_t)hi << 8));
}

static void mem_write16(VM *vm, uint16_t addr, uint16_t v) {
    vm->mem[addr] = (uint8_t)(v & 0xFF);
    vm->mem[(uint16_t)(addr + 1)] = (uint8_t)((v >> 8) & 0xFF);
}

/* Read SRC operand value */
static uint16_t read_operand(VM *vm, int mode, uint16_t *pc, int *err) {
    switch (mode) {
        case MODE_REG: {
            uint8_t r = fetch8(vm, pc, err);
            if (r >= NUM_REGS) { *err = -10; return 0; }
            return vm->R[r];
        }
        case MODE_IMM:
            return fetch16(vm, pc, err);
        case MODE_MEM: {
            uint16_t a = fetch16(vm, pc, err);
            return mem_read16(vm, a, err);
        }
        case MODE_REGIND: {
            uint8_t r = fetch8(vm, pc, err);
            if (r >= NUM_REGS) { *err = -10; return 0; }
            return mem_read16(vm, vm->R[r], err);
        }
        default: *err = -11; return 0;
    }
}

/* Write to DST operand. For CMP, write is skipped. */
static void write_operand(VM *vm, int mode, uint16_t *pc, uint16_t val, int *err) {
    switch (mode) {
        case MODE_REG: {
            uint8_t r = fetch8(vm, pc, err);
            if (r >= NUM_REGS) { *err = -10; return; }
            vm->R[r] = val;
            break;
        }
        case MODE_MEM: {
            uint16_t a = fetch16(vm, pc, err);
            mem_write16(vm, a, val);
            break;
        }
        case MODE_REGIND: {
            uint8_t r = fetch8(vm, pc, err);
            if (r >= NUM_REGS) { *err = -10; return; }
            mem_write16(vm, vm->R[r], val);
            break;
        }
        default:
            *err = -12; /* Writing to IMM is illegal */
            break;
    }
}

/* Retrieve jump target address / push value without write */
static uint16_t read_single(VM *vm, int mode, uint16_t *pc, int *err) {
    return read_operand(vm, mode, pc, err);
}

static void push16(VM *vm, uint16_t v, int *err) {
    vm->SP = (uint16_t)(vm->SP - 2);
    mem_write16(vm, vm->SP, v);
    (void)err;
}

static uint16_t pop16(VM *vm, int *err) {
    uint16_t v = mem_read16(vm, vm->SP, err);
    vm->SP = (uint16_t)(vm->SP + 2);
    return v;
}

static void do_syscall(VM *vm, uint8_t n) {
    switch (n) {
        case SYS_PUTCHAR:
#ifdef BAREMETAL
            baremetal_putchar((char)(vm->R[0] & 0xFF));
#else
            putchar(vm->R[0] & 0xFF);
            fflush(stdout);
#endif
            break;
        case SYS_GETCHAR: {
#ifdef BAREMETAL
            vm->R[0] = 0;
#else
            int c = getchar();
            if (c == EOF) vm->R[0] = 0;
            else vm->R[0] = (uint16_t)(c & 0xFF);
#endif
            break;
        }
        case SYS_PRINTSTR: {
            uint16_t a = vm->R[0];
            for (int i = 0; i < MEM_SIZE; i++) {
                uint8_t c = vm->mem[a];
                if (c == 0) break;
#ifdef BAREMETAL
                baremetal_putchar((char)c);
#else
                putchar(c);
#endif
                a = (uint16_t)(a + 1);
            }
#ifndef BAREMETAL
            fflush(stdout);
#endif
            break;
        }
        case SYS_PRINTNUM: {
#ifdef BAREMETAL
            uint16_t v = vm->R[0];
            char buf[8];
            int i = 0;
            if (v == 0) {
                baremetal_putchar('0');
            } else {
                while (v > 0 && i < 7) {
                    buf[i++] = (char)('0' + (v % 10));
                    v /= 10;
                }
                while (i > 0) {
                    baremetal_putchar(buf[--i]);
                }
            }
#else
            printf("%u", vm->R[0]);
            fflush(stdout);
#endif
            break;
        }
        case SYS_READNUM: {
#ifdef BAREMETAL
            vm->C = 1;
            vm->R[0] = 0;
#else
            char buf[32];
            int pos = 0, c;
            do { c = getchar(); if (c == EOF) break; } while (c == ' ' || c == '\t');
            while (c != EOF && c != '\n' && c != '\r' && pos < (int)sizeof(buf) - 1) {
                buf[pos++] = (char)c;
                c = getchar();
            }
            buf[pos] = 0;
            char *end = NULL;
            long v = strtol(buf, &end, 0);
            if (buf[0] == '0' && (buf[1] == 'b' || buf[1] == 'B')) {
                v = 0; end = buf + 2;
                int ok = 0;
                while (*end == '0' || *end == '1') { v = v * 2 + (*end - '0'); end++; ok = 1; }
                if (!ok) end = buf;
            }
            if (end == buf || v < 0 || v > 0xFFFF) {
                vm->C = 1;
                vm->R[0] = 0;
            } else {
                while (*end == ' ' || *end == '\t') end++;
                if (*end != 0) { vm->C = 1; vm->R[0] = 0; }
                else { vm->C = 0; vm->R[0] = (uint16_t)v; }
            }
#endif
            break;
        }
        case SYS_NEWLINE:
#ifdef BAREMETAL
            baremetal_putchar('\n');
#else
            putchar('\n');
            fflush(stdout);
#endif
            break;
        case SYS_DISK_READ: {
            uint32_t lba = vm->R[0];
            int rc = vm_disk_read(vm, lba, vm->R[1], vm->R[2]);
            vm->R[0] = (uint16_t)rc;
            vm->C = (rc == DISK_ERR_NONE) ? 0 : 1;
            break;
        }
        case SYS_DISK_INFO: {
            if (!vm->disk_data || vm->disk_sectors == 0) {
                vm->C = 1;
                vm->R[0] = 0; vm->R[1] = DISK_SECTOR_SIZE; vm->R[2] = 0;
            } else {
                vm->C = 0;
                vm->R[0] = (uint16_t)(vm->disk_sectors & 0xFFFF);
                vm->R[2] = (uint16_t)((vm->disk_sectors >> 16) & 0xFFFF);
                vm->R[1] = DISK_SECTOR_SIZE;
            }
            break;
        }
        case SYS_DISK_WRITE: {
            uint32_t lba = vm->R[0];
            int rc = vm_disk_write(vm, lba, vm->R[1], vm->R[2]);
            vm->R[0] = (uint16_t)rc;
            vm->C = (rc == DISK_ERR_NONE) ? 0 : 1;
            break;
        }
        case SYS_GUI_INIT: {
            vm->gui_active = 1;
            vm->R[0] = VRAM_COLS;
            vm->R[1] = VRAM_ROWS;
            vm->R[2] = VRAM_ADDR;
            vm->C = 0;
            if (vm->gui_init_cb) {
                vm->gui_init_cb(vm->gui_userdata);
            }
            break;
        }
        case SYS_GUI_POLL_EVENT: {
            if (vm->gui_poll_cb) {
                vm->gui_poll_cb(vm->gui_userdata);
            }
            uint8_t type = 0;
            uint16_t code = 0, data = 0;
            vm_event_pop(vm, &type, &code, &data);
            vm->R[0] = type;
            vm->R[1] = code;
            vm->R[2] = data;
            break;
        }
        case SYS_GUI_FLUSH: {
            if (vm->gui_flush_cb) {
                vm->gui_flush_cb(vm->gui_userdata);
            }
            vm->gui_dirty = 0;
            break;
        }
        case SYS_GET_TICKS: {
            uint32_t t = get_time_ms();
            vm->R[0] = (uint16_t)(t & 0xFFFF);
            vm->R[1] = (uint16_t)((t >> 16) & 0xFFFF);
            break;
        }
        case SYS_GUI_DRAW_RECT: {
            int x = (int)(vm->R[0] & 0xFF);
            int y = (int)((vm->R[0] >> 8) & 0xFF);
            int w = (int)(vm->R[1] & 0xFF);
            int h = (int)((vm->R[1] >> 8) & 0xFF);
            uint8_t ch = (uint8_t)(vm->R[2] & 0xFF);
            uint8_t attr = (uint8_t)((vm->R[2] >> 8) & 0xFF);
            vm_gui_draw_rect(vm, x, y, w, h, ch, attr);
            break;
        }
        case SYS_GUI_DRAW_TEXT: {
            int x = (int)(vm->R[0] & 0xFF);
            int y = (int)((vm->R[0] >> 8) & 0xFF);
            uint16_t addr = vm->R[1];
            uint8_t attr = (uint8_t)(vm->R[2] & 0xFF);
            char s[256];
            int i = 0;
            while (i < 255 && vm->mem[(uint16_t)(addr + i)]) {
                s[i] = (char)vm->mem[(uint16_t)(addr + i)];
                i++;
            }
            s[i] = '\0';
            vm_gui_draw_text(vm, x, y, s, attr);
            break;
        }
        default:
            break;
    }
}

int vm_step(VM *vm) {
    if (vm->halted) return 1;
    if (vm->max_steps && vm->steps >= vm->max_steps) return -100;

    int err = 0;
    uint16_t pc = vm->PC;
    uint8_t op = fetch8(vm, &pc, &err);

    switch (op) {
        case OP_HLT:
            vm->PC = pc;
            vm->halted = 1;
            vm->steps++;
            return 1;
        case OP_NOP:
            vm->PC = pc;
            break;
        case OP_RET:
        case OP_IRET: {
            uint16_t ret = pop16(vm, &err);
            if (err) break;
            vm->PC = ret;
            break;
        }
        case OP_IN: {
            uint8_t r = fetch8(vm, &pc, &err);
            uint8_t port = fetch8(vm, &pc, &err);
            if (err) break;
            if (r >= NUM_REGS) { err = -10; break; }
            if (port == PORT_CONSOLE) {
#ifdef BAREMETAL
                vm->R[r] = 0;
#else
                int c = getchar();
                vm->R[r] = (c == EOF) ? 0 : (uint16_t)(c & 0xFF);
#endif
            } else {
                vm->R[r] = 0;
            }
            vm->PC = pc;
            break;
        }
        case OP_OUT: {
            uint8_t port = fetch8(vm, &pc, &err);
            uint8_t r = fetch8(vm, &pc, &err);
            if (err) break;
            if (r >= NUM_REGS) { err = -10; break; }
            if (port == PORT_CONSOLE) {
#ifdef BAREMETAL
                baremetal_putchar((char)(vm->R[r] & 0xFF));
#else
                putchar(vm->R[r] & 0xFF);
                fflush(stdout);
#endif
            }
            vm->PC = pc;
            break;
        }
        case OP_INT: {
            uint8_t n = fetch8(vm, &pc, &err);
            if (err) break;
            vm->PC = pc;
            do_syscall(vm, n);
            break;
        }
        case OP_PUSH: {
            uint8_t m = fetch8(vm, &pc, &err);
            if (err) break;
            uint16_t val = read_single(vm, m, &pc, &err);
            if (err) break;
            push16(vm, val, &err);
            if (err) break;
            vm->PC = pc;
            break;
        }
        case OP_POP: {
            uint8_t m = fetch8(vm, &pc, &err);
            if (err) break;
            uint16_t val = pop16(vm, &err);
            if (err) break;
            write_operand(vm, m, &pc, val, &err);
            if (err) break;
            vm->PC = pc;
            break;
        }
        case OP_JMP:
        case OP_JZ:
        case OP_JNZ:
        case OP_JC:
        case OP_JNC:
        case OP_CALL: {
            uint8_t m = fetch8(vm, &pc, &err);
            if (err) break;
            uint16_t target = read_single(vm, m, &pc, &err);
            if (err) break;
            int take = 0;
            switch (op) {
                case OP_JMP:  take = 1; break;
                case OP_JZ:   take = (vm->Z == 1); break;
                case OP_JNZ:  take = (vm->Z == 0); break;
                case OP_JC:   take = (vm->C == 1); break;
                case OP_JNC:  take = (vm->C == 0); break;
                case OP_CALL:
                    push16(vm, pc, &err);
                    if (err) break;
                    take = 1;
                    break;
            }
            if (err) break;
            if (take) vm->PC = target;
            else vm->PC = pc;
            break;
        }
        case OP_INC:
        case OP_DEC:
        case OP_NOT: {
            uint8_t m = fetch8(vm, &pc, &err);
            if (err) break;
            uint16_t save_pc = pc;
            uint16_t val = read_operand(vm, m, &pc, &err);
            if (err) break;
            uint32_t res = 0;
            if (op == OP_INC) {
                res = (uint32_t)val + 1;
                vm->C = (res > 0xFFFF) ? 1 : 0;
                val = (uint16_t)res;
                vm->Z = (val == 0);
            } else if (op == OP_DEC) {
                vm->C = (val == 0) ? 1 : 0;
                val = (uint16_t)(val - 1);
                vm->Z = (val == 0);
            } else {
                val = (uint16_t)~val;
                vm->Z = (val == 0);
            }
            pc = save_pc;
            write_operand(vm, m, &pc, val, &err);
            if (err) break;
            vm->PC = pc;
            break;
        }
        case OP_MOV:
        case OP_ADD:
        case OP_SUB:
        case OP_MUL:
        case OP_DIV:
        case OP_AND:
        case OP_OR:
        case OP_XOR:
        case OP_CMP:
        case OP_SHL:
        case OP_SHR: {
            uint8_t modes = fetch8(vm, &pc, &err);
            if (err) break;
            int dst_m = (modes >> 4) & 0x0F;
            int src_m = modes & 0x0F;

            uint16_t dst_pc = pc;
            int skip_read_dst = (op == OP_MOV);
            uint16_t dst_val = 0;
            if (!skip_read_dst) {
                dst_val = read_operand(vm, dst_m, &pc, &err);
                if (err) break;
            } else {
                if (dst_m == MODE_REG || dst_m == MODE_REGIND) pc++;
                else if (dst_m == MODE_MEM) pc += 2;
                else { err = -13; break; }
            }

            uint16_t src_val = read_operand(vm, src_m, &pc, &err);
            if (err) break;

            uint32_t res = 0;
            switch (op) {
                case OP_MOV:
                    res = src_val;
                    break;
                case OP_ADD:
                    res = (uint32_t)dst_val + (uint32_t)src_val;
                    vm->C = (res > 0xFFFF) ? 1 : 0;
                    vm->Z = ((uint16_t)res == 0);
                    break;
                case OP_SUB:
                    res = (uint32_t)dst_val - (uint32_t)src_val;
                    vm->C = (dst_val < src_val) ? 1 : 0;
                    vm->Z = ((uint16_t)res == 0);
                    break;
                case OP_MUL:
                    res = (uint32_t)dst_val * (uint32_t)src_val;
                    vm->C = (res > 0xFFFF) ? 1 : 0;
                    vm->Z = ((uint16_t)res == 0);
                    break;
                case OP_DIV:
                    if (src_val == 0) {
                        res = 0;
                        vm->C = 1;
                        vm->Z = 1;
                    } else {
                        res = dst_val / src_val;
                        vm->C = 0;
                        vm->Z = ((uint16_t)res == 0);
                    }
                    break;
                case OP_AND:
                    res = dst_val & src_val;
                    vm->Z = ((uint16_t)res == 0);
                    break;
                case OP_OR:
                    res = dst_val | src_val;
                    vm->Z = ((uint16_t)res == 0);
                    break;
                case OP_XOR:
                    res = dst_val ^ src_val;
                    vm->Z = ((uint16_t)res == 0);
                    break;
                case OP_CMP:
                    vm->C = (dst_val < src_val) ? 1 : 0;
                    vm->Z = (dst_val == src_val) ? 1 : 0;
                    vm->PC = pc;
                    vm->steps++;
                    return 0;
                case OP_SHL: {
                    uint16_t cnt = src_val & 0x0F;
                    if (cnt == 0) {
                        res = dst_val;
                    } else if (cnt <= 16) {
                        vm->C = (uint8_t)((dst_val >> (16 - cnt)) & 1);
                        res = (uint32_t)dst_val << cnt;
                    } else {
                        vm->C = 0; res = 0;
                    }
                    vm->Z = ((uint16_t)res == 0);
                    break;
                }
                case OP_SHR: {
                    uint16_t cnt = src_val & 0x0F;
                    if (cnt == 0) {
                        res = dst_val;
                    } else if (cnt <= 16) {
                        vm->C = (uint8_t)((dst_val >> (cnt - 1)) & 1);
                        res = dst_val >> cnt;
                    } else {
                        vm->C = 0; res = 0;
                    }
                    vm->Z = ((uint16_t)res == 0);
                    break;
                }
            }

            uint16_t after_src_pc = pc;
            pc = dst_pc;
            write_operand(vm, dst_m, &pc, (uint16_t)res, &err);
            if (err) break;
            vm->PC = after_src_pc;
            break;
        }
        case OP_LDB: {
            uint8_t modes = fetch8(vm, &pc, &err);
            if (err) break;
            int dst_r = (modes >> 4) & 0x0F;
            int src_m = modes & 0x0F;
            if (dst_r >= NUM_REGS) { err = -10; break; }

            uint8_t b = 0;
            switch (src_m) {
                case MODE_REG: {
                    uint8_t sr = fetch8(vm, &pc, &err);
                    if (sr >= NUM_REGS) { err = -10; break; }
                    b = (uint8_t)(vm->R[sr] & 0xFF);
                    break;
                }
                case MODE_IMM: {
                    b = fetch8(vm, &pc, &err);
                    break;
                }
                case MODE_MEM: {
                    uint16_t a = fetch16(vm, &pc, &err);
                    b = vm->mem[a];
                    break;
                }
                case MODE_REGIND: {
                    uint8_t sr = fetch8(vm, &pc, &err);
                    if (sr >= NUM_REGS) { err = -10; break; }
                    b = vm->mem[vm->R[sr]];
                    break;
                }
                default: err = -11; break;
            }
            if (err) break;
            vm->R[dst_r] = (uint16_t)b;
            vm->Z = (b == 0);
            vm->PC = pc;
            break;
        }
        case OP_STB: {
            uint8_t modes = fetch8(vm, &pc, &err);
            if (err) break;
            int dst_m = (modes >> 4) & 0x0F;
            int src_m = modes & 0x0F;

            uint16_t dst_pc = pc;
            if (dst_m == MODE_REG || dst_m == MODE_REGIND) pc++;
            else if (dst_m == MODE_MEM) pc += 2;
            else { err = -13; break; }

            uint16_t sval = read_operand(vm, src_m, &pc, &err);
            if (err) break;
            uint8_t b = (uint8_t)(sval & 0xFF);

            uint16_t after_src_pc = pc;
            pc = dst_pc;
            switch (dst_m) {
                case MODE_REG: {
                    uint8_t dr = fetch8(vm, &pc, &err);
                    if (dr >= NUM_REGS) { err = -10; break; }
                    vm->R[dr] = (uint16_t)((vm->R[dr] & 0xFF00) | b);
                    break;
                }
                case MODE_MEM: {
                    uint16_t a = fetch16(vm, &pc, &err);
                    vm->mem[a] = b;
                    break;
                }
                case MODE_REGIND: {
                    uint8_t dr = fetch8(vm, &pc, &err);
                    if (dr >= NUM_REGS) { err = -10; break; }
                    vm->mem[vm->R[dr]] = b;
                    break;
                }
                default: err = -12; break;
            }
            if (err) break;
            vm->PC = after_src_pc;
            break;
        }
        default:
            err = -1;
            break;
    }

    if (err) return err;
    vm->steps++;
    return 0;
}

int vm_run(VM *vm) {
    while (!vm->halted) {
        int rc = vm_step(vm);
        if (rc != 0) return rc;
    }
    return 1;
}

#ifndef BAREMETAL
int vm_disasm(VM *vm, uint16_t addr, char *out, size_t outsz) {
    int err = 0;
    uint16_t pc = addr;
    uint8_t op = fetch8(vm, &pc, &err);
    if (err) { snprintf(out, outsz, "???"); return 1; }

    const char *name = op_name(op);
    switch (op) {
        case OP_HLT:
        case OP_NOP:
        case OP_RET:
        case OP_IRET:
            snprintf(out, outsz, "%s", name);
            break;
        case OP_IN: {
            uint8_t r = fetch8(vm, &pc, &err);
            uint8_t p = fetch8(vm, &pc, &err);
            snprintf(out, outsz, "%s %s, %u", name, reg_name(r), p);
            break;
        }
        case OP_OUT: {
            uint8_t p = fetch8(vm, &pc, &err);
            uint8_t r = fetch8(vm, &pc, &err);
            snprintf(out, outsz, "%s %u, %s", name, p, reg_name(r));
            break;
        }
        case OP_INT: {
            uint8_t n = fetch8(vm, &pc, &err);
            snprintf(out, outsz, "%s %u", name, n);
            break;
        }
        case OP_PUSH:
        case OP_POP:
        case OP_JMP:
        case OP_JZ:
        case OP_JNZ:
        case OP_JC:
        case OP_JNC:
        case OP_CALL:
        case OP_INC:
        case OP_DEC:
        case OP_NOT: {
            uint8_t m = fetch8(vm, &pc, &err);
            char opstr[32];
            switch (m) {
                case MODE_REG: {
                    uint8_t r = fetch8(vm, &pc, &err);
                    snprintf(opstr, sizeof(opstr), "%s", reg_name(r));
                    break;
                }
                case MODE_IMM: {
                    uint16_t v = fetch16(vm, &pc, &err);
                    snprintf(opstr, sizeof(opstr), "0x%04X", v);
                    break;
                }
                case MODE_MEM: {
                    uint16_t a = fetch16(vm, &pc, &err);
                    snprintf(opstr, sizeof(opstr), "[0x%04X]", a);
                    break;
                }
                case MODE_REGIND: {
                    uint8_t r = fetch8(vm, &pc, &err);
                    snprintf(opstr, sizeof(opstr), "[%s]", reg_name(r));
                    break;
                }
                default:
                    snprintf(opstr, sizeof(opstr), "???");
                    break;
            }
            snprintf(out, outsz, "%s %s", name, opstr);
            break;
        }
        case OP_LDB:
        case OP_STB:
        case OP_MOV:
        case OP_ADD:
        case OP_SUB:
        case OP_MUL:
        case OP_DIV:
        case OP_AND:
        case OP_OR:
        case OP_XOR:
        case OP_CMP:
        case OP_SHL:
        case OP_SHR: {
            uint8_t modes = fetch8(vm, &pc, &err);
            int dst_m = (modes >> 4) & 0x0F;
            int src_m = modes & 0x0F;

            char dstr[32], sstr[32];
            if (op == OP_LDB) {
                snprintf(dstr, sizeof(dstr), "%s", reg_name(dst_m));
            } else {
                switch (dst_m) {
                    case MODE_REG: {
                        uint8_t r = fetch8(vm, &pc, &err);
                        snprintf(dstr, sizeof(dstr), "%s", reg_name(r));
                        break;
                    }
                    case MODE_MEM: {
                        uint16_t a = fetch16(vm, &pc, &err);
                        snprintf(dstr, sizeof(dstr), "[0x%04X]", a);
                        break;
                    }
                    case MODE_REGIND: {
                        uint8_t r = fetch8(vm, &pc, &err);
                        snprintf(dstr, sizeof(dstr), "[%s]", reg_name(r));
                        break;
                    }
                    default: snprintf(dstr, sizeof(dstr), "???"); break;
                }
            }

            switch (src_m) {
                case MODE_REG: {
                    uint8_t r = fetch8(vm, &pc, &err);
                    snprintf(sstr, sizeof(sstr), "%s", reg_name(r));
                    break;
                }
                case MODE_IMM: {
                    if (op == OP_LDB) {
                        uint8_t b = fetch8(vm, &pc, &err);
                        snprintf(sstr, sizeof(sstr), "0x%02X", b);
                    } else {
                        uint16_t v = fetch16(vm, &pc, &err);
                        snprintf(sstr, sizeof(sstr), "0x%04X", v);
                    }
                    break;
                }
                case MODE_MEM: {
                    uint16_t a = fetch16(vm, &pc, &err);
                    snprintf(sstr, sizeof(sstr), "[0x%04X]", a);
                    break;
                }
                case MODE_REGIND: {
                    uint8_t r = fetch8(vm, &pc, &err);
                    snprintf(sstr, sizeof(sstr), "[%s]", reg_name(r));
                    break;
                }
                default: snprintf(sstr, sizeof(sstr), "???"); break;
            }

            snprintf(out, outsz, "%s %s, %s", name, dstr, sstr);
            break;
        }
        default:
            snprintf(out, outsz, "DB 0x%02X", op);
            break;
    }
    return (int)(pc - addr);
}
#endif
