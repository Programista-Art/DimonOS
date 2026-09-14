#include <stdint.h>
#include <stddef.h>
#include <stdarg.h>
#include "io.h"
#include "../../dimon64.h"

/* --- Standard Memory Functions (Freestanding) --- */
void *memset(void *s, int c, size_t n) {
    uint8_t *p = (uint8_t *)s;
    for (size_t i = 0; i < n; i++) p[i] = (uint8_t)c;
    return s;
}

void *memcpy(void *dest, const void *src, size_t n) {
    uint8_t *d = (uint8_t *)dest;
    const uint8_t *s = (const uint8_t *)src;
    for (size_t i = 0; i < n; i++) d[i] = s[i];
    return dest;
}

void *memmove(void *dest, const void *src, size_t n) {
    uint8_t *d = (uint8_t *)dest;
    const uint8_t *s = (const uint8_t *)src;
    if (d < s) {
        for (size_t i = 0; i < n; i++) d[i] = s[i];
    } else if (d > s) {
        for (size_t i = n; i > 0; i--) d[i - 1] = s[i - 1];
    }
    return dest;
}

size_t strlen(const char *s) {
    size_t len = 0;
    while (s[len]) len++;
    return len;
}

/* --- 64-bit Integer Math Helpers for 32-bit GCC Freestanding Target --- */
static uint64_t __udivmoddi4(uint64_t num, uint64_t den, uint64_t *rem_p) {
    if (den == 0) {
        if (rem_p) *rem_p = 0;
        return 0;
    }
    uint64_t quot = 0, rem = 0;
    for (int i = 63; i >= 0; i--) {
        rem = (rem << 1) | ((num >> i) & 1ULL);
        if (rem >= den) {
            rem -= den;
            quot |= (1ULL << i);
        }
    }
    if (rem_p) *rem_p = rem;
    return quot;
}

uint64_t __udivdi3(uint64_t a, uint64_t b) {
    return __udivmoddi4(a, b, NULL);
}

uint64_t __umoddi3(uint64_t a, uint64_t b) {
    uint64_t r;
    __udivmoddi4(a, b, &r);
    return r;
}

int64_t __divdi3(int64_t a, int64_t b) {
    int neg = 0;
    if (a < 0) { a = -a; neg = !neg; }
    if (b < 0) { b = -b; neg = !neg; }
    uint64_t res = __udivmoddi4((uint64_t)a, (uint64_t)b, NULL);
    return neg ? -(int64_t)res : (int64_t)res;
}

int64_t __moddi3(int64_t a, int64_t b) {
    int neg = 0;
    if (a < 0) { a = -a; neg = 1; }
    if (b < 0) { b = -b; }
    uint64_t rem;
    __udivmoddi4((uint64_t)a, (uint64_t)b, &rem);
    return neg ? -(int64_t)rem : (int64_t)rem;
}

/* --- Lightweight Freestanding vsnprintf / snprintf --- */
int vsnprintf(char *str, size_t size, const char *format, va_list ap) {
    if (!str || size == 0) return 0;
    size_t pos = 0;

    for (const char *p = format; *p; p++) {
        if (*p != '%') {
            if (pos + 1 < size) str[pos] = *p;
            pos++;
            continue;
        }
        p++;
        if (*p == '%') {
            if (pos + 1 < size) str[pos] = '%';
            pos++;
            continue;
        }

        int is_long_long = 0;
        int is_long = 0;
        if (*p == 'l') {
            p++;
            if (*p == 'l') {
                is_long_long = 1;
                p++;
            } else {
                is_long = 1;
            }
        }

        if (*p == 's') {
            const char *s = va_arg(ap, const char *);
            if (!s) s = "(null)";
            while (*s) {
                if (pos + 1 < size) str[pos] = *s;
                pos++;
                s++;
            }
        } else if (*p == 'd' || *p == 'i') {
            int64_t val = is_long_long ? va_arg(ap, int64_t) : (is_long ? va_arg(ap, long) : va_arg(ap, int));
            if (val < 0) {
                if (pos + 1 < size) str[pos] = '-';
                pos++;
                val = -val;
            }
            char buf[24];
            int n = 0;
            uint64_t u = (uint64_t)val;
            if (u == 0) buf[n++] = '0';
            while (u > 0) {
                buf[n++] = (char)('0' + (u % 10));
                u /= 10;
            }
            while (n > 0) {
                if (pos + 1 < size) str[pos] = buf[--n];
                else n--;
                pos++;
            }
        } else if (*p == 'u') {
            uint64_t u = is_long_long ? va_arg(ap, uint64_t) : (is_long ? va_arg(ap, unsigned long) : va_arg(ap, unsigned int));
            char buf[24];
            int n = 0;
            if (u == 0) buf[n++] = '0';
            while (u > 0) {
                buf[n++] = (char)('0' + (u % 10));
                u /= 10;
            }
            while (n > 0) {
                if (pos + 1 < size) str[pos] = buf[--n];
                else n--;
                pos++;
            }
        } else if (*p == 'x' || *p == 'X') {
            uint64_t u = is_long_long ? va_arg(ap, uint64_t) : (is_long ? va_arg(ap, unsigned long) : va_arg(ap, unsigned int));
            char hex_digits[] = "0123456789abcdef";
            if (*p == 'X') for (int i = 10; i < 16; i++) hex_digits[i] = (char)('A' + (i - 10));
            char buf[24];
            int n = 0;
            if (u == 0) buf[n++] = '0';
            while (u > 0) {
                buf[n++] = hex_digits[u & 0x0F];
                u >>= 4;
            }
            while (n > 0) {
                if (pos + 1 < size) str[pos] = buf[--n];
                else n--;
                pos++;
            }
        } else if (*p == 'c') {
            char c = (char)va_arg(ap, int);
            if (pos + 1 < size) str[pos] = c;
            pos++;
        } else if (*p == 'p') {
            uint32_t ptr = (uint32_t)va_arg(ap, void *);
            if (pos + 1 < size) str[pos] = '0';
            pos++;
            if (pos + 1 < size) str[pos] = 'x';
            pos++;
            char buf[16];
            int n = 0;
            if (ptr == 0) buf[n++] = '0';
            while (ptr > 0) {
                buf[n++] = "0123456789ABCDEF"[ptr & 0x0F];
                ptr >>= 4;
            }
            while (n > 0) {
                if (pos + 1 < size) str[pos] = buf[--n];
                else n--;
                pos++;
            }
        }
    }

    if (pos < size) str[pos] = '\0';
    else str[size - 1] = '\0';
    return (int)pos;
}

int snprintf(char *str, size_t size, const char *format, ...) {
    va_list ap;
    va_start(ap, format);
    int rc = vsnprintf(str, size, format, ap);
    va_end(ap);
    return rc;
}

/* --- Serial Output (COM1 0x3F8) for Debugging & Telemetry --- */
static int g_serial_init = 0;
static void serial_init(void) {
    outb(0x3F9, 0x00);
    outb(0x3FB, 0x80);
    outb(0x3F8, 0x03); /* 38400 baud */
    outb(0x3F9, 0x00);
    outb(0x3FB, 0x03);
    outb(0x3FA, 0xC7);
    outb(0x3FC, 0x0B);
    g_serial_init = 1;
}

void baremetal_putchar(char c) {
    if (!g_serial_init) serial_init();
    if (c == '\n') baremetal_putchar('\r');
    uint32_t timeout = 50000;
    while ((inb(0x3FD) & 0x20) == 0 && --timeout);
    if (timeout > 0) outb(0x3F8, (uint8_t)c);
}

static void serial_puts(const char *s) {
    while (*s) baremetal_putchar(*s++);
}

static void serial_printf(const char *fmt, ...) {
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    serial_puts(buf);
}

/* --- Multiboot 1 Specification Structures --- */
#define MULTIBOOT_BOOTLOADER_MAGIC 0x2BADB002

#define MULTIBOOT_INFO_MEM      (1 << 0)
#define MULTIBOOT_INFO_BOOTDEV  (1 << 1)
#define MULTIBOOT_INFO_CMDLINE  (1 << 2)
#define MULTIBOOT_INFO_MODS     (1 << 3)
#define MULTIBOOT_INFO_AOUT     (1 << 4)
#define MULTIBOOT_INFO_ELF      (1 << 5)
#define MULTIBOOT_INFO_MMAP     (1 << 6)
#define MULTIBOOT_INFO_DRIVES   (1 << 7)
#define MULTIBOOT_INFO_CONFIG   (1 << 8)
#define MULTIBOOT_INFO_LOADER   (1 << 9)
#define MULTIBOOT_INFO_APM      (1 << 10)
#define MULTIBOOT_INFO_VBE      (1 << 11)
#define MULTIBOOT_INFO_FB       (1 << 12)

struct multiboot_mmap_entry {
    uint32_t size;
    uint64_t addr;
    uint64_t len;
    uint32_t type; /* 1 = RAM */
} __attribute__((packed));

struct vbe_mode_info {
    uint16_t attributes;
    uint8_t  winA, winB;
    uint16_t granularity;
    uint16_t winsize;
    uint16_t segmentA, segmentB;
    uint32_t realFctPtr;
    uint16_t pitch;
    uint16_t Xres, Yres;
    uint8_t  Wchar, Ychar, planes, bpp, banks;
    uint8_t  memory_model, bank_size, image_pages;
    uint8_t  reserved0;
    uint8_t  red_mask, red_position;
    uint8_t  green_mask, green_position;
    uint8_t  blue_mask, blue_position;
    uint8_t  rsv_mask, rsv_position;
    uint8_t  directcolor_attributes;
    uint32_t phys_base;
    uint32_t reserved1;
    uint16_t reserved2;
} __attribute__((packed));

struct multiboot_info {
    uint32_t flags;
    uint32_t mem_lower;
    uint32_t mem_upper;
    uint32_t boot_device;
    uint32_t cmdline;
    uint32_t mods_count;
    uint32_t mods_addr;
    uint32_t syms[4];
    uint32_t mmap_length;
    uint32_t mmap_addr;
    uint32_t drives_length;
    uint32_t drives_addr;
    uint32_t config_table;
    uint32_t boot_loader_name;
    uint32_t apm_table;
    uint32_t vbe_control_info;
    uint32_t vbe_mode_info;
    uint16_t vbe_mode;
    uint16_t vbe_interface_seg;
    uint16_t vbe_interface_off;
    uint16_t vbe_interface_len;
    uint64_t framebuffer_addr;
    uint32_t framebuffer_pitch;
    uint32_t framebuffer_width;
    uint32_t framebuffer_height;
    uint8_t  framebuffer_bpp;
    uint8_t  framebuffer_type;
    uint8_t  color_info[6];
} __attribute__((packed));

/* Linear Framebuffer Global Parameters */
static uint32_t g_lfb_base   = 0;
static uint32_t g_lfb_pitch  = 0;
static uint32_t g_lfb_width  = 0;
static uint32_t g_lfb_height = 0;
static uint8_t  g_lfb_bpp    = 0;

static void parse_multiboot(uint32_t magic, uint32_t mb_addr) {
    if (magic != MULTIBOOT_BOOTLOADER_MAGIC || mb_addr == 0) {
        serial_printf("[WARN] Invalid Multiboot magic: 0x%X (expected 0x%X)\n", magic, MULTIBOOT_BOOTLOADER_MAGIC);
        g_lfb_base   = 0xFD000000;
        g_lfb_pitch  = DIMON64_LFB_WIDTH * 4;
        g_lfb_width  = DIMON64_LFB_WIDTH;
        g_lfb_height = DIMON64_LFB_HEIGHT;
        g_lfb_bpp    = 32;
        return;
    }

    const struct multiboot_info *mbi = (const struct multiboot_info *)mb_addr;
    serial_printf("[DimonOS] Multiboot header flags: 0x%X\n", mbi->flags);

    if (mbi->flags & MULTIBOOT_INFO_LOADER) {
        const char *loader = (const char *)mbi->boot_loader_name;
        if (loader) serial_printf("[DimonOS] Bootloader: %s\n", loader);
    }

    if (mbi->flags & MULTIBOOT_INFO_MEM) {
        serial_printf("[DimonOS] Basic memory: lower=%u KB, upper=%u KB (total ~%u MB)\n",
                      mbi->mem_lower, mbi->mem_upper, (mbi->mem_upper + 1024) / 1024);
    }

    /* 1. Try Multiboot Framebuffer table (Flag bit 12) */
    if ((mbi->flags & MULTIBOOT_INFO_FB) && mbi->framebuffer_addr != 0) {
        g_lfb_base   = (uint32_t)mbi->framebuffer_addr;
        g_lfb_pitch  = mbi->framebuffer_pitch;
        g_lfb_width  = mbi->framebuffer_width;
        g_lfb_height = mbi->framebuffer_height;
        g_lfb_bpp    = mbi->framebuffer_bpp;
        serial_printf("[DimonOS] Detected LFB via Multiboot FB: base=0x%X pitch=%u res=%ux%u bpp=%u type=%u\n",
                      g_lfb_base, g_lfb_pitch, g_lfb_width, g_lfb_height, g_lfb_bpp, mbi->framebuffer_type);
    }
    /* 2. Fallback to VBE mode info table (Flag bit 11) */
    else if ((mbi->flags & MULTIBOOT_INFO_VBE) && mbi->vbe_mode_info != 0) {
        const struct vbe_mode_info *vbe = (const struct vbe_mode_info *)mbi->vbe_mode_info;
        g_lfb_base   = vbe->phys_base;
        g_lfb_pitch  = vbe->pitch;
        g_lfb_width  = vbe->Xres;
        g_lfb_height = vbe->Yres;
        g_lfb_bpp    = vbe->bpp;
        serial_printf("[DimonOS] Detected LFB via VBE: base=0x%X pitch=%u res=%ux%u bpp=%u\n",
                      g_lfb_base, g_lfb_pitch, g_lfb_width, g_lfb_height, g_lfb_bpp);
    } else {
        serial_puts("[WARN] Neither Multiboot FB nor VBE table found! Using QEMU stdvga default 0xFD000000\n");
        g_lfb_base   = 0xFD000000;
        g_lfb_pitch  = DIMON64_LFB_WIDTH * 4;
        g_lfb_width  = DIMON64_LFB_WIDTH;
        g_lfb_height = DIMON64_LFB_HEIGHT;
        g_lfb_bpp    = 32;
    }
}

/* --- 64 MB Continuous Guest RAM Pool --- */
static uint8_t g_guest_ram[DIMON64_MEM_SIZE] __attribute__((aligned(4096)));

/* --- PIT Timer (Channel 0 at 1000 Hz) --- */
static volatile uint32_t g_ticks_ms = 0;

uint32_t kernel_get_ticks_ms(void) {
    return g_ticks_ms;
}

static void pit_init(void) {
    outb(0x43, 0x36);
    uint16_t divisor = 1193; /* 1193182 / 1000 = 1193 (0x04A9) */
    outb(0x40, (uint8_t)(divisor & 0xFF));
    outb(0x40, (uint8_t)((divisor >> 8) & 0xFF));
}

/* --- PIC 8259 Remapping --- */
static void pic_remap(void) {
    outb(0x20, 0x11); io_wait();
    outb(0xA0, 0x11); io_wait();

    outb(0x21, 0x20); io_wait(); /* Master offset: 0x20 */
    outb(0xA1, 0x28); io_wait(); /* Slave offset:  0x28 */

    outb(0x21, 0x04); io_wait();
    outb(0xA1, 0x02); io_wait();

    outb(0x21, 0x01); io_wait();
    outb(0xA1, 0x01); io_wait();

    /* Unmask IRQ0 (Timer), IRQ1 (Keyboard), IRQ2 (Cascade) on Master */
    outb(0x21, 0xF8); io_wait();
    /* Unmask IRQ12 (Mouse) on Slave: bit 4 = 0 -> 0xEF */
    outb(0xA1, 0xEF); io_wait();
}

/* --- IDT (Interrupt Descriptor Table) --- */
struct idt_entry {
    uint16_t offset_low;
    uint16_t selector;
    uint8_t  zero;
    uint8_t  type_attr;
    uint16_t offset_high;
} __attribute__((packed));

struct idt_ptr {
    uint16_t limit;
    uint32_t base;
} __attribute__((packed));

static struct idt_entry g_idt[256];

extern void irq0_stub(void);
extern void irq1_stub(void);
extern void irq12_stub(void);
extern void default_isr_stub(void);

static void idt_set_gate(int num, uint32_t base, uint16_t sel, uint8_t flags) {
    g_idt[num].offset_low  = (uint16_t)(base & 0xFFFF);
    g_idt[num].selector    = sel;
    g_idt[num].zero        = 0;
    g_idt[num].type_attr   = flags;
    g_idt[num].offset_high = (uint16_t)((base >> 16) & 0xFFFF);
}

static void idt_init(void) {
    for (int i = 0; i < 256; i++) {
        idt_set_gate(i, (uint32_t)default_isr_stub, 0x08, 0x8E);
    }

    idt_set_gate(0x20, (uint32_t)irq0_stub, 0x08, 0x8E);  /* IRQ0: Timer */
    idt_set_gate(0x21, (uint32_t)irq1_stub, 0x08, 0x8E);  /* IRQ1: Keyboard */
    idt_set_gate(0x2C, (uint32_t)irq12_stub, 0x08, 0x8E); /* IRQ12: PS/2 Mouse */

    struct idt_ptr ptr;
    ptr.limit = (uint16_t)(sizeof(g_idt) - 1);
    ptr.base  = (uint32_t)&g_idt;
    __asm__ volatile("lidt %0" : : "m"(ptr));
}

void default_isr_c_handler(void) {
    outb(0x20, 0x20);
}

void irq0_c_handler(void) {
    g_ticks_ms++;
    outb(0x20, 0x20);
}

/* --- PS/2 Keyboard Driver (Set 1 Scancodes) --- */
#define KBD_QUEUE_SIZE 128
static uint16_t g_kbd_queue[KBD_QUEUE_SIZE];
static volatile int g_kbd_head = 0;
static volatile int g_kbd_tail = 0;

static void kbd_push(uint16_t key) {
    int next = (g_kbd_tail + 1) % KBD_QUEUE_SIZE;
    if (next != g_kbd_head) {
        g_kbd_queue[g_kbd_tail] = key;
        g_kbd_tail = next;
    }
}

static const uint8_t kbd_map_normal[128] = {
    0,   27,  '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', 8,   9,
    'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', 13,  0,   'a', 's',
    'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`', 0,   '\\','z', 'x', 'c', 'v',
    'b', 'n', 'm', ',', '.', '/', 0,   '*', 0,   ' ', 0,   0,   0,   0,   0,   0,
    0,   0,   0,   0,   0,   0,   0,   '7', '8', '9', '-', '4', '5', '6', '+', '1',
    '2', '3', '0', '.', 0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0
};

static const uint8_t kbd_map_shift[128] = {
    0,   27,  '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+', 8,   9,
    'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', 13,  0,   'A', 'S',
    'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '"', '~', 0,   '|', 'Z', 'X', 'C', 'V',
    'B', 'N', 'M', '<', '>', '?', 0,   '*', 0,   ' ', 0,   0,   0,   0,   0,   0,
    0,   0,   0,   0,   0,   0,   0,   '7', '8', '9', '-', '4', '5', '6', '+', '1',
    '2', '3', '0', '.', 0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0
};

static int g_shift_down = 0;
static int g_extended_prefix = 0;

void irq1_c_handler(void) {
    uint8_t sc = inb(0x60);

    if (sc == 0xE0) {
        g_extended_prefix = 1;
        outb(0x20, 0x20);
        return;
    }

    if (g_extended_prefix) {
        g_extended_prefix = 0;
        if ((sc & 0x80) == 0) {
            if (sc == 0x48)      kbd_push(KEY_UP);
            else if (sc == 0x50) kbd_push(KEY_DOWN);
            else if (sc == 0x4B) kbd_push(KEY_LEFT);
            else if (sc == 0x4D) kbd_push(KEY_RIGHT);
            else if (sc == 0x1C) kbd_push(13);  /* Keypad Enter */
            else if (sc == 0x35) kbd_push('/'); /* Keypad Slash */
        }
        outb(0x20, 0x20);
        return;
    }

    if (sc == 0x2A || sc == 0x36) {
        g_shift_down = 1;
    } else if (sc == 0xAA || sc == 0xB6) {
        g_shift_down = 0;
    } else if ((sc & 0x80) == 0) {
        /* Key press */
        if (sc >= 0x3B && sc <= 0x44) {
            /* F1..F10 -> KEY_F1 (260) .. KEY_F10 (269) */
            kbd_push((uint16_t)(KEY_F1 + (sc - 0x3B)));
        } else if (sc < 128) {
            uint8_t ch = g_shift_down ? kbd_map_shift[sc] : kbd_map_normal[sc];
            if (ch != 0) kbd_push((uint16_t)ch);
        }
    }

    outb(0x20, 0x20);
}

/* --- PS/2 Mouse Driver (800x600 Scaled with Buttons) --- */
#define MOUSE_QUEUE_SIZE 128
struct mouse_event {
    uint8_t  type;   /* EVT_MOUSE_CLICK or EVT_MOUSE_MOVE */
    uint16_t x;      /* 0..799 */
    uint16_t y;      /* 0..599 */
    uint8_t  button; /* 1=left, 2=right, 0=none */
};

static struct mouse_event g_mouse_queue[MOUSE_QUEUE_SIZE];
static volatile int g_mouse_head = 0;
static volatile int g_mouse_tail = 0;

static int g_mouse_x = DIMON64_LFB_WIDTH / 2;
static int g_mouse_y = DIMON64_LFB_HEIGHT / 2;
static uint8_t g_mouse_last_btn = 0;
static uint8_t g_mouse_cycle = 0;
static uint8_t g_mouse_bytes[3];

static void mouse_push(uint8_t type, uint16_t x, uint16_t y, uint8_t button) {
    int next = (g_mouse_tail + 1) % MOUSE_QUEUE_SIZE;
    if (next != g_mouse_head) {
        g_mouse_queue[g_mouse_tail].type   = type;
        g_mouse_queue[g_mouse_tail].x      = x;
        g_mouse_queue[g_mouse_tail].y      = y;
        g_mouse_queue[g_mouse_tail].button = button;
        g_mouse_tail = next;
    }
}

static int mouse_wait(uint8_t type) {
    uint32_t timeout = 50000;
    if (type == 0) {
        while (timeout--) {
            if ((inb(0x64) & 1) == 1) return 1;
        }
    } else {
        while (timeout--) {
            if ((inb(0x64) & 2) == 0) return 1;
        }
    }
    return 0;
}

static void mouse_write(uint8_t val) {
    if (!mouse_wait(1)) return;
    outb(0x64, 0xD4);
    if (!mouse_wait(1)) return;
    outb(0x60, val);
}

static uint8_t mouse_read(void) {
    if (!mouse_wait(0)) return 0;
    return inb(0x60);
}

static void mouse_init(void) {
    if (!mouse_wait(1)) return;
    outb(0x64, 0xA8); /* Enable auxiliary device (mouse) */

    if (!mouse_wait(1)) return;
    outb(0x64, 0x20); /* Read controller command byte */
    uint8_t status = mouse_read();
    status |= 0x02;   /* Enable IRQ12 */
    status &= ~0x20;  /* Disable mouse clock inhibit */

    if (!mouse_wait(1)) return;
    outb(0x64, 0x60); /* Write controller command byte */
    if (!mouse_wait(1)) return;
    outb(0x60, status);

    mouse_write(0xF6); /* Set defaults */
    mouse_read();      /* Acknowledge */

    mouse_write(0xF4); /* Enable data reporting */
    mouse_read();      /* Acknowledge */
}

void irq12_c_handler(void) {
    uint8_t status = inb(0x64);
    if ((status & 0x20) == 0) {
        outb(0xA0, 0x20);
        outb(0x20, 0x20);
        return;
    }

    uint8_t b = inb(0x60);
    switch (g_mouse_cycle) {
        case 0:
            if ((b & 0x08) == 0x08) {
                g_mouse_bytes[0] = b;
                g_mouse_cycle = 1;
            }
            break;
        case 1:
            g_mouse_bytes[1] = b;
            g_mouse_cycle = 2;
            break;
        case 2: {
            g_mouse_bytes[2] = b;
            g_mouse_cycle = 0;

            /* Check overflow bits (bits 6 and 7 of byte 0) */
            if ((g_mouse_bytes[0] & 0xC0) == 0) {
                int rel_x = (int8_t)g_mouse_bytes[1];
                int rel_y = (int8_t)g_mouse_bytes[2];

                int nx = g_mouse_x + rel_x;
                int ny = g_mouse_y - rel_y; /* PS/2 Y is upward, screen Y is downward */

                if (nx < 0) nx = 0;
                if (nx >= DIMON64_LFB_WIDTH) nx = DIMON64_LFB_WIDTH - 1;
                if (ny < 0) ny = 0;
                if (ny >= DIMON64_LFB_HEIGHT) ny = DIMON64_LFB_HEIGHT - 1;

                uint8_t raw_btns   = g_mouse_bytes[0];
                uint8_t left_down  = (raw_btns & 0x01) ? 1 : 0;
                uint8_t right_down = (raw_btns & 0x02) ? 2 : 0;
                uint8_t cur_btn    = left_down | right_down;

                if (nx != g_mouse_x || ny != g_mouse_y) {
                    g_mouse_x = nx;
                    g_mouse_y = ny;
                    mouse_push(EVT_MOUSE_MOVE, (uint16_t)g_mouse_x, (uint16_t)g_mouse_y, cur_btn);
                }

                if (left_down && !(g_mouse_last_btn & 0x01)) {
                    mouse_push(EVT_MOUSE_CLICK, (uint16_t)g_mouse_x, (uint16_t)g_mouse_y, 1);
                } else if (right_down && !(g_mouse_last_btn & 0x02)) {
                    mouse_push(EVT_MOUSE_CLICK, (uint16_t)g_mouse_x, (uint16_t)g_mouse_y, 2);
                }
                g_mouse_last_btn = cur_btn;
            }
            break;
        }
    }

    outb(0xA0, 0x20);
    outb(0x20, 0x20);
}

/* --- 12x19 Arrow Cursor Overlay --- */
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

/* --- Linear Framebuffer 32bpp Blitter --- */
static void on_baremetal_gui_flush(void *userdata) {
    VM *vm = (VM *)userdata;
    if (!vm || !vm->mem || !g_lfb_base) return;

    const uint8_t *src = vm->mem + DIMON64_VRAM_BASE;
    uint8_t *dst = (uint8_t *)g_lfb_base;

    int w = DIMON64_LFB_WIDTH;
    if ((int)g_lfb_width < w && g_lfb_width > 0) w = (int)g_lfb_width;
    int h = DIMON64_LFB_HEIGHT;
    if ((int)g_lfb_height < h && g_lfb_height > 0) h = (int)g_lfb_height;

    uint32_t pitch = g_lfb_pitch;
    if (pitch < (uint32_t)(w * 4)) pitch = (uint32_t)(w * 4);

    /* 1. Blit the 800x600 TrueColor guest VRAM to physical LFB */
    if (g_lfb_bpp == 32 || g_lfb_bpp == 0) {
        if (pitch == (uint32_t)(w * 4) && w == DIMON64_LFB_WIDTH && h == DIMON64_LFB_HEIGHT) {
            memcpy(dst, src, DIMON64_VRAM_SIZE);
        } else {
            for (int y = 0; y < h; y++) {
                memcpy(dst + (size_t)y * pitch, src + (size_t)y * (DIMON64_LFB_WIDTH * 4), (size_t)w * 4);
            }
        }
    } else if (g_lfb_bpp == 24) {
        for (int y = 0; y < h; y++) {
            const uint32_t *s = (const uint32_t *)(src + (size_t)y * (DIMON64_LFB_WIDTH * 4));
            uint8_t *d = dst + (size_t)y * pitch;
            for (int x = 0; x < w; x++) {
                uint32_t px = s[x];
                d[x * 3 + 0] = (uint8_t)(px & 0xFF);
                d[x * 3 + 1] = (uint8_t)((px >> 8) & 0xFF);
                d[x * 3 + 2] = (uint8_t)((px >> 16) & 0xFF);
            }
        }
    }

    /* 2. Overlay hardware mouse arrow cursor directly onto physical LFB */
    int mx = g_mouse_x;
    int my = g_mouse_y;
    for (int cy = 0; cy < 19; cy++) {
        int py = my + cy;
        if (py < 0 || py >= h) continue;
        for (int cx = 0; cx < 12; cx++) {
            int px = mx + cx;
            if (px < 0 || px >= w) continue;
            char ch = cursor_arrow[cy][cx];
            if (ch == 'X') {
                if (g_lfb_bpp == 32 || g_lfb_bpp == 0) {
                    *(uint32_t *)(dst + (size_t)py * pitch + (size_t)px * 4) = 0xFF000000;
                } else if (g_lfb_bpp == 24) {
                    uint8_t *p = dst + (size_t)py * pitch + (size_t)px * 3;
                    p[0] = 0; p[1] = 0; p[2] = 0;
                }
            } else if (ch == '.') {
                if (g_lfb_bpp == 32 || g_lfb_bpp == 0) {
                    *(uint32_t *)(dst + (size_t)py * pitch + (size_t)px * 4) = 0xFFFFFFFF;
                } else if (g_lfb_bpp == 24) {
                    uint8_t *p = dst + (size_t)py * pitch + (size_t)px * 3;
                    p[0] = 0xFF; p[1] = 0xFF; p[2] = 0xFF;
                }
            }
        }
    }
}

static void on_baremetal_gui_init(void *userdata) {
    on_baremetal_gui_flush(userdata);
}

static int s_cursor_prev_x = -1;
static int s_cursor_prev_y = -1;

static void on_baremetal_gui_poll(void *userdata) {
    VM *vm = (VM *)userdata;
    if (!vm) return;

    /* Transfer mouse events */
    while (g_mouse_head != g_mouse_tail) {
        struct mouse_event ev = g_mouse_queue[g_mouse_head];
        g_mouse_head = (g_mouse_head + 1) % MOUSE_QUEUE_SIZE;
        vm_event_push_ext(vm, ev.type, ev.x, ev.y, ev.button);
    }

    /* Transfer keyboard events */
    while (g_kbd_head != g_kbd_tail) {
        uint16_t key = g_kbd_queue[g_kbd_head];
        g_kbd_head = (g_kbd_head + 1) % KBD_QUEUE_SIZE;
        vm_event_push(vm, EVT_KEY, key, 0);
    }

    /* Host timer tick at 20 Hz (every 50 ms) */
    static uint32_t last_timer = 0;
    if (g_ticks_ms - last_timer >= 50) {
        last_timer = g_ticks_ms;
        vm_event_push(vm, EVT_TIMER, 0, 0);
    }

    /* If mouse position moved, refresh cursor immediately */
    if (s_cursor_prev_x != g_mouse_x || s_cursor_prev_y != g_mouse_y || vm->gui_dirty) {
        s_cursor_prev_x = g_mouse_x;
        s_cursor_prev_y = g_mouse_y;
        on_baremetal_gui_flush(vm);
        vm->gui_dirty = 0;
    }

    /* If event queue is empty, halt CPU until next hardware interrupt */
    if (vm->event_head == vm->event_tail) {
        hlt();
    }
}

/* --- Embedded Assets from boot.S --- */
extern uint8_t _os_bin_start[];
extern uint8_t _os_bin_end[];
extern uint8_t _dimon_iso_start[];
extern uint8_t _dimon_iso_end[];

/* --- Kernel Main --- */
void kernel_main(uint32_t magic, uint32_t mb_info) {
    serial_init();
    serial_puts("\n=======================================================\n");
    serial_puts("  DimonOS-64 Native Bare-Metal Kernel (x86_32 -> RV64)  \n");
    serial_puts("=======================================================\n");

    /* Parse Multiboot information & detect LFB */
    parse_multiboot(magic, mb_info);

    /* Initialize Hardware */
    pic_remap();
    idt_init();
    pit_init();
    mouse_init();

    /* Enable Interrupts */
    sti();
    serial_puts("[DimonOS] Hardware initialized (PIC, IDT, PIT 1000Hz, PS/2 Kbd, PS/2 Mouse 800x600).\n");

    /* Initialize DimonVirtualCPU-64 Virtual Machine */
    static VM vm;
    vm_init(&vm);

    /* Attach 64 MB Continuous Guest RAM */
    vm.mem = g_guest_ram;
    memset(vm.mem, 0, DIMON64_MEM_SIZE);
    serial_printf("[DimonOS] 64 MB Guest RAM allocated at %p (size %u MB).\n",
                  vm.mem, (unsigned)(DIMON64_MEM_SIZE / (1024 * 1024)));

    /* Configure GUI Callbacks */
    vm.gui_userdata = &vm;
    vm.gui_init_cb  = on_baremetal_gui_init;
    vm.gui_flush_cb = on_baremetal_gui_flush;
    vm.gui_poll_cb  = on_baremetal_gui_poll;

    /* Attach embedded ISO virtual disk if present */
    size_t iso_size = (size_t)(_dimon_iso_end - _dimon_iso_start);
    if (iso_size >= 512) {
        vm.disk_data = _dimon_iso_start;
        vm.disk_sectors = (uint32_t)(iso_size / 512);
        vm.disk_writable = 0;
        serial_printf("[DimonOS] Attached embedded ISO disk: %u sectors (%u KB).\n",
                      vm.disk_sectors, (unsigned)(iso_size / 1024));
    }

    /* Load and launch DimonOS-64 binary */
    size_t os_size = (size_t)(_os_bin_end - _os_bin_start);
    if (os_size == 0) {
        serial_puts("[FATAL] Embedded os.bin is empty!\n");
        for (;;) hlt();
    }

    if (vm_load_buf(&vm, _os_bin_start, os_size, 0) != 0) {
        serial_puts("[FATAL] Failed to load os.bin into VM RAM!\n");
        for (;;) hlt();
    }
    serial_printf("[DimonOS] Loaded embedded os.bin (%u bytes) into VM RAM.\n", (unsigned)os_size);

    serial_puts("[DimonOS] Resetting virtual CPU to entry 0x00000000...\n");
    vm_reset(&vm, 0);

    /* Initial screen flush */
    on_baremetal_gui_flush(&vm);

    serial_puts("[DimonOS] Starting DimonOS-64 TrueColor Desktop GUI execution loop...\n");
    vm_run(&vm);

    serial_puts("[DimonOS] VM execution halted.\n");
    for (;;) {
        cli();
        hlt();
    }
}
