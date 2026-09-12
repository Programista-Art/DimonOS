#ifndef DIMON16_H
#define DIMON16_H

#include <stdint.h>
#ifndef BAREMETAL
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#else
#include <stddef.h>
void *memset(void *s, int c, size_t n);
void *memcpy(void *dest, const void *src, size_t n);
void *memmove(void *dest, const void *src, size_t n);
size_t strlen(const char *s);
#endif

#define MEM_SIZE 65536
#define NUM_REGS 8

/* Operand addressing modes */
#define MODE_REG    0   /* R0..R7 */
#define MODE_IMM    1   /* 16-bit immediate value (SRC only) */
#define MODE_MEM    2   /* [addr16] */
#define MODE_REGIND 3   /* [Rn] */

/* Opcodes */
enum {
    OP_HLT  = 0x00,
    OP_NOP  = 0x01,
    OP_MOV  = 0x02,
    OP_ADD  = 0x03,
    OP_SUB  = 0x04,
    OP_MUL  = 0x05,
    OP_DIV  = 0x06,
    OP_AND  = 0x07,
    OP_OR   = 0x08,
    OP_XOR  = 0x09,
    OP_CMP  = 0x0A,
    OP_NOT  = 0x0B,
    OP_SHL  = 0x0C,
    OP_SHR  = 0x0D,
    OP_INC  = 0x0E,
    OP_DEC  = 0x0F,
    OP_PUSH = 0x10,
    OP_POP  = 0x11,
    OP_JMP  = 0x12,
    OP_JZ   = 0x13,
    OP_JNZ  = 0x14,
    OP_JC   = 0x15,
    OP_JNC  = 0x16,
    OP_CALL = 0x17,
    OP_RET  = 0x18,
    OP_IN   = 0x19,
    OP_OUT  = 0x1A,
    OP_INT  = 0x1B,
    OP_IRET = 0x1C,
    OP_LDB  = 0x1D,   /* LDB dst_reg, src -> load BYTE (zero-extended) */
    OP_STB  = 0x1E    /* STB dst, src -> store BYTE (low 8 bits of SRC) */
};

/* Syscalls (INT n) */
enum {
    SYS_PUTCHAR  = 0,  /* R0 = character to print */
    SYS_GETCHAR  = 1,  /* R0 = read character (0 on EOF) */
    SYS_PRINTSTR = 2,  /* R0 = address of null-terminated string */
    SYS_PRINTNUM = 3,  /* Print R0 as decimal integer */
    SYS_READNUM  = 4,  /* Read decimal integer -> R0 (C=0 ok, C=1 error) */
    SYS_NEWLINE  = 5,  /* Print newline \n */
    SYS_DISK_READ  = 6, /* R0=LBA, R1=RAM address, R2=sector count -> C=0 ok, C=1 error, R0=error code */
    SYS_DISK_INFO  = 7, /* -> R0=sector count (lo16), R1=sector size (512), R2=sector count (hi16); C=0 disk present, C=1 none */
    SYS_DISK_WRITE = 8, /* Same as READ, but writes RAM -> disk (only when writable) */
    /* GUI Syscalls (INT 10-15) */
    SYS_GUI_INIT       = 10, /* GUI initialization: -> R0=cols (80), R1=rows (25), R2=vram (0xE000) */
    SYS_GUI_POLL_EVENT = 11, /* Poll event -> R0=type, R1=code/x, R2=data/y */
    SYS_GUI_FLUSH      = 12, /* Flush physical screen buffer */
    SYS_GET_TICKS      = 13, /* System uptime in milliseconds -> R0=lo16, R1=hi16 */
    SYS_GUI_DRAW_RECT  = 14, /* Fill rectangle: R0=X|(Y<<8), R1=W|(H<<8), R2=char|(color<<8) */
    SYS_GUI_DRAW_TEXT  = 15  /* Draw text: R0=X|(Y<<8), R1=string address, R2=color */
};

/* Video Memory (VRAM) */
#define VRAM_ADDR 0xE000
#define VRAM_COLS 80
#define VRAM_ROWS 25
#define VRAM_SIZE (VRAM_COLS * VRAM_ROWS * 2) /* 4000 bytes */

/* GUI Event Types */
#define EVT_NONE        0
#define EVT_KEY         1
#define EVT_MOUSE_CLICK 2
#define EVT_MOUSE_MOVE  3
#define EVT_TIMER       4

/* Special Keycodes */
#define KEY_UP    256
#define KEY_DOWN  257
#define KEY_LEFT  258
#define KEY_RIGHT 259
#define KEY_F1    260
#define KEY_F2    261
#define KEY_F3    262
#define KEY_F4    263
#define KEY_F5    264
#define KEY_F6    265
#define KEY_F7    266
#define KEY_F8    267
#define KEY_F9    268
#define KEY_F10   269

#define VM_EVENT_QUEUE_SIZE 128

typedef struct {
    uint8_t  type;
    uint16_t code;
    uint16_t data;
} VMEvent;

/* Disk / ISO image (DIMON-ISO): 512B sectors */
#define DISK_SECTOR_SIZE 512
#define DISK_MAX_SECTORS 8192   /* ~4 MB */
#define DISK_BOOT_MAGIC0 0x55
#define DISK_BOOT_MAGIC1 0xAA
#define DISK_CAT_SECTOR  1      /* Directory catalog sector */
#define DISK_CAT_MAGIC   0x444D /* "MD" LE: catalog signature (M=0x4D, D=0x44) */
#define DISK_FNAME_LEN   12
#define DISK_FILE_MAX    21     /* 24B entries in 512B (21*24 = 504) */

/* Disk error codes (returned in R0 with C=1 for INT 6/8) */
enum {
    DISK_ERR_NONE = 0,
    DISK_ERR_NODISK = 1,
    DISK_ERR_RANGE = 2,   /* LBA or sector count out of bounds */
    DISK_ERR_RAM = 3,     /* RAM address + size exceeds 64KB */
    DISK_ERR_READONLY = 4,
    DISK_ERR_IO = 5
};

/* I/O Ports (IN/OUT) */
#define PORT_CONSOLE 0

typedef struct {
    uint16_t R[NUM_REGS];
    uint16_t PC;
    uint16_t SP;
    uint8_t  Z;      /* Zero flag */
    uint8_t  C;      /* Carry/borrow flag */
    uint8_t  halted;
    uint8_t  mem[MEM_SIZE];
    uint64_t steps;
    uint64_t max_steps;  /* 0 = unlimited */

    /* Virtual disk / ISO image (outside RAM) */
    uint8_t  *disk_data;     /* disk_sectors * 512 bytes, NULL if none */
    uint32_t disk_sectors;   /* Sector count */
    uint8_t  disk_writable;  /* 1 = INT 8 writes allowed */
    char     disk_path[256]; /* Source file path for debugging/info */

    /* GUI Subsystem and event queue */
    VMEvent  event_queue[VM_EVENT_QUEUE_SIZE];
    int      event_head;
    int      event_tail;
    uint8_t  gui_active;
    uint8_t  gui_dirty;
    void    (*gui_init_cb)(void *userdata);
    void    (*gui_flush_cb)(void *userdata);
    void    (*gui_poll_cb)(void *userdata);
    void     *gui_userdata;
} VM;

/* GUI event helper functions */
void vm_event_push(VM *vm, uint8_t type, uint16_t code, uint16_t data);
int  vm_event_pop(VM *vm, uint8_t *type, uint16_t *code, uint16_t *data);
void vm_gui_draw_rect(VM *vm, int x, int y, int w, int h, uint8_t ch, uint8_t attr);
void vm_gui_draw_text(VM *vm, int x, int y, const char *text, uint8_t attr);

/* --- vm.c --- */
void vm_init(VM *vm);
void vm_free(VM *vm);   /* Frees attached disk data */
#ifndef BAREMETAL
int  vm_load(VM *vm, const char *path, uint16_t load_addr);
void vm_dump_regs(VM *vm, FILE *out);
int  vm_disk_attach(VM *vm, const char *path, int writable); /* 0=ok, <0=error */
#endif
int  vm_load_buf(VM *vm, const uint8_t *buf, size_t len, uint16_t load_addr);
void vm_reset(VM *vm, uint16_t start_pc);

/* Disk / ISO */
void vm_disk_detach(VM *vm);          /* Detach and free disk */
int  vm_disk_boot(VM *vm, uint16_t load_addr); /* Load sector 0 to RAM; 0=ok */
int  vm_disk_read(VM *vm, uint32_t lba, uint16_t ram_addr, uint16_t count);  /* DISK_ERR_* */
int  vm_disk_write(VM *vm, uint32_t lba, uint16_t ram_addr, uint16_t count);

/* Execute 1 instruction. Returns 0 on OK, 1 on HLT, <0 on error. */
int  vm_step(VM *vm);
/* Execute until HLT/error/step limit. */
int  vm_run(VM *vm);

/* Helpers (used by debugger/disassembler) */
const char *op_name(uint8_t op);
const char *reg_name(int r);
/* Decode instruction at address into text buffer.
   Returns instruction length in bytes (min 1). */
int  vm_disasm(VM *vm, uint16_t addr, char *out, size_t outsz);

/* Byte operations */
void    vm_stb(VM *vm, uint16_t addr, uint8_t v);
uint8_t vm_ldb(VM *vm, uint16_t addr);

#endif /* DIMON16_H */
