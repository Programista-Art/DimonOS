#include <stdint.h>
#include <stddef.h>
#include "io.h"
#include "../../dimon16.h"

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

/* --- Serial Output (COM1 0x3F8) for Debugging --- */
static int g_serial_init = 0;
static void serial_init(void) {
    outb(0x3F9, 0x00);
    outb(0x3FB, 0x80);
    outb(0x3F8, 0x03);
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

/* --- PIT Timer (Channel 0 at 1000 Hz) --- */
static volatile uint32_t g_ticks_ms = 0;

uint32_t kernel_get_ticks_ms(void) {
    return g_ticks_ms;
}

static void pit_init(void) {
    /* Set PIT channel 0 to mode 3 (square wave) */
    outb(0x43, 0x36);
    /* Divisor for 1000 Hz = 1193182 / 1000 = 1193 (0x04A9) */
    uint16_t divisor = 1193;
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
    g_idt[num].offset_low = (uint16_t)(base & 0xFFFF);
    g_idt[num].selector = sel;
    g_idt[num].zero = 0;
    g_idt[num].type_attr = flags;
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
    ptr.base = (uint32_t)&g_idt;
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
#define KBD_QUEUE_SIZE 64
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
        if (sc == 0x48) kbd_push(KEY_UP);
        else if (sc == 0x50) kbd_push(KEY_DOWN);
        else if (sc == 0x4B) kbd_push(KEY_LEFT);
        else if (sc == 0x4D) kbd_push(KEY_RIGHT);
        else if (sc == 0x1C) kbd_push(13);
        else if (sc == 0x35) kbd_push('/');
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
            /* F1..F10 */
            kbd_push((uint16_t)(KEY_F1 + (sc - 0x3B)));
        } else if (sc < 128) {
            uint8_t ch = g_shift_down ? kbd_map_shift[sc] : kbd_map_normal[sc];
            if (ch != 0) kbd_push((uint16_t)ch);
        }
    }

    outb(0x20, 0x20);
}

/* --- PS/2 Mouse Driver --- */
#define MOUSE_QUEUE_SIZE 64
struct mouse_event {
    uint8_t type; /* EVT_MOUSE_CLICK or EVT_MOUSE_MOVE */
    uint16_t x;
    uint16_t y;
};
static struct mouse_event g_mouse_queue[MOUSE_QUEUE_SIZE];
static volatile int g_mouse_head = 0;
static volatile int g_mouse_tail = 0;

static int g_mouse_x = 40;
static int g_mouse_y = 12;
static uint8_t g_mouse_last_btn = 0;
static uint8_t g_mouse_cycle = 0;
static uint8_t g_mouse_bytes[3];

static void mouse_push(uint8_t type, uint16_t x, uint16_t y) {
    int next = (g_mouse_tail + 1) % MOUSE_QUEUE_SIZE;
    if (next != g_mouse_head) {
        g_mouse_queue[g_mouse_tail].type = type;
        g_mouse_queue[g_mouse_tail].x = x;
        g_mouse_queue[g_mouse_tail].y = y;
        g_mouse_tail = next;
    }
}

static void mouse_set_cursor(int x, int y) {
    uint16_t pos = (uint16_t)(y * 80 + x);
    outb(0x3D4, 0x0F);
    outb(0x3D5, (uint8_t)(pos & 0xFF));
    outb(0x3D4, 0x0E);
    outb(0x3D5, (uint8_t)((pos >> 8) & 0xFF));
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
    outb(0x64, 0xA8); /* Enable auxiliary device */

    if (!mouse_wait(1)) return;
    outb(0x64, 0x20); /* Read controller command byte */
    uint8_t status = mouse_read();
    status |= 0x02;   /* Enable IRQ12 */
    status &= ~0x20;  /* Disable mouse clock inhibit */

    if (!mouse_wait(1)) return;
    outb(0x64, 0x60);
    if (!mouse_wait(1)) return;
    outb(0x60, status);

    mouse_write(0xF6); /* Set defaults */
    mouse_read();      /* Acknowledge */

    mouse_write(0xF4); /* Enable data reporting */
    mouse_read();      /* Acknowledge */

    mouse_set_cursor(g_mouse_x, g_mouse_y);
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

            int rel_x = (int8_t)g_mouse_bytes[1];
            int rel_y = (int8_t)g_mouse_bytes[2];

            /* Sensitivity: scale movement for 80x25 grid */
            int nx = g_mouse_x + (rel_x / 2);
            int ny = g_mouse_y - (rel_y / 4);

            if (nx < 0) nx = 0;
            if (nx > 79) nx = 79;
            if (ny < 0) ny = 0;
            if (ny > 24) ny = 24;

            if (nx != g_mouse_x || ny != g_mouse_y) {
                g_mouse_x = nx;
                g_mouse_y = ny;
                mouse_set_cursor(g_mouse_x, g_mouse_y);
                mouse_push(EVT_MOUSE_MOVE, (uint16_t)g_mouse_x, (uint16_t)g_mouse_y);
            }

            uint8_t btn = g_mouse_bytes[0] & 1; /* Left button */
            if (btn && !g_mouse_last_btn) {
                mouse_push(EVT_MOUSE_CLICK, (uint16_t)g_mouse_x, (uint16_t)g_mouse_y);
            }
            g_mouse_last_btn = btn;
            break;
        }
    }

    outb(0xA0, 0x20);
    outb(0x20, 0x20);
}

/* --- VGA Text Mode (80x25) --- */
#define VGA_VRAM_PHYS ((volatile uint8_t *)0x000B8000)

static void vga_init(void) {
    /* Enable all 16 background colors instead of blinking */
    inb(0x3DA);
    outb(0x3C0, 0x10 | 0x20);
    uint8_t val = inb(0x3C1);
    val &= ~0x08;
    outb(0x3C0, val);

    /* Enable hardware text cursor */
    outb(0x3D4, 0x0A);
    outb(0x3D5, (inb(0x3D5) & 0xC0) | 0x0C);
    outb(0x3D4, 0x0B);
    outb(0x3D5, (inb(0x3D5) & 0xE0) | 0x0F);
}

/* --- VM GUI Callbacks --- */
static void on_baremetal_gui_flush(void *userdata) {
    VM *vm = (VM *)userdata;
    if (!vm) return;
    memcpy((void *)VGA_VRAM_PHYS, vm->mem + VRAM_ADDR, VRAM_SIZE);
}

static void on_baremetal_gui_init(void *userdata) {
    VM *vm = (VM *)userdata;
    if (!vm) return;
    memcpy((void *)VGA_VRAM_PHYS, vm->mem + VRAM_ADDR, VRAM_SIZE);
}

static void on_baremetal_gui_poll(void *userdata) {
    VM *vm = (VM *)userdata;
    if (!vm) return;

    /* Transfer mouse events */
    while (g_mouse_head != g_mouse_tail) {
        struct mouse_event ev = g_mouse_queue[g_mouse_head];
        g_mouse_head = (g_mouse_head + 1) % MOUSE_QUEUE_SIZE;
        vm_event_push(vm, ev.type, ev.x, ev.y);
    }

    /* Transfer keyboard events */
    while (g_kbd_head != g_kbd_tail) {
        uint16_t key = g_kbd_queue[g_kbd_head];
        g_kbd_head = (g_kbd_head + 1) % KBD_QUEUE_SIZE;
        vm_event_push(vm, EVT_KEY, key, 0);
    }

    /* Timer tick at 20 Hz (every 50 ms) */
    static uint32_t last_timer = 0;
    if (g_ticks_ms - last_timer >= 50) {
        last_timer = g_ticks_ms;
        vm_event_push(vm, EVT_TIMER, 0, 0);
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
    (void)magic;
    (void)mb_info;

    serial_init();
    serial_puts("\n[DimonOS v2.1] Native Bare-Metal Kernel booting...\n");

    vga_init();
    pic_remap();
    idt_init();
    pit_init();
    mouse_init();

    /* Enable interrupts */
    sti();

    serial_puts("[DimonOS] Hardware initialized (PIC, IDT, PIT 1000Hz, PS/2 Kbd/Mouse, VGA 80x25).\n");

    /* Initialize Dimon-16 Virtual Machine */
    static VM vm;
    vm_init(&vm);
    vm.gui_userdata = &vm;
    vm.gui_init_cb = on_baremetal_gui_init;
    vm.gui_flush_cb = on_baremetal_gui_flush;
    vm.gui_poll_cb = on_baremetal_gui_poll;

    /* Attach embedded ISO virtual disk if present */
    size_t iso_size = (size_t)(_dimon_iso_end - _dimon_iso_start);
    if (iso_size >= 512) {
        vm.disk_data = _dimon_iso_start;
        vm.disk_sectors = (uint32_t)(iso_size / 512);
        vm.disk_writable = 0;
        serial_puts("[DimonOS] Attached embedded ISO disk volume.\n");
    }

    /* Load and launch DimonOS binary */
    size_t os_size = (size_t)(_os_bin_end - _os_bin_start);
    if (os_size == 0) {
        serial_puts("[ERROR] Embedded os.bin is empty!\n");
        for (;;) hlt();
    }

    if (vm_load_buf(&vm, _os_bin_start, os_size, 0) != 0) {
        serial_puts("[ERROR] Failed to load os.bin into VM RAM!\n");
        for (;;) hlt();
    }

    serial_puts("[DimonOS] Launching DimonOS v2.1 Desktop GUI...\n");
    vm_reset(&vm, 0);

    /* Run VM execution loop */
    vm_run(&vm);

    serial_puts("[DimonOS] VM execution halted.\n");
    for (;;) {
        cli();
        hlt();
    }
}
