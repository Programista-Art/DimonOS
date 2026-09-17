#ifndef DIMON64_H
#define DIMON64_H

#include <stdint.h>
#include <stddef.h>
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
int snprintf(char *str, size_t size, const char *format, ...);
static inline int memcmp(const void *a, const void *b, size_t n) {
    const unsigned char *x = (const unsigned char *)a, *y = (const unsigned char *)b;
    for (size_t i = 0; i < n; i++) if (x[i] != y[i]) return (int)x[i] - (int)y[i];
    return 0;
}
static inline int strcmp(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return (unsigned char)*a - (unsigned char)*b;
}
static inline char *strcpy(char *dst, const char *src) {
    char *out = dst; while ((*dst++ = *src++) != 0) {} return out;
}
static inline int abs(int v) { return v < 0 ? -v : v; }
#endif

/* ============ Memory model ============ */
#define DIMON64_MEM_SIZE   (64u * 1024u * 1024u) /* 64 MB */
#define DIMON64_MEM_START  0x00000000ULL
#define DIMON64_MEM_END    0x04000000ULL

#define DIMON64_NUM_REGS 32

/* Video RAM: 800x600 TrueColor Linear Framebuffer (LFB) @ 32bpp ARGB8888 */
#define DIMON64_LFB_WIDTH   800
#define DIMON64_LFB_HEIGHT  600
#define DIMON64_LFB_BPP     4
#define DIMON64_VRAM_BASE   0x02000000ULL
#define DIMON64_VRAM_COLS   800
#define DIMON64_VRAM_ROWS   600
#define DIMON64_VRAM_SIZE   (DIMON64_LFB_WIDTH * DIMON64_LFB_HEIGHT * DIMON64_LFB_BPP) /* 1,920,000 bytes */

/* Process stacks: 8 x 64 KB below VRAM */
#define DIMON64_MAX_PROCS  8
#define DIMON64_STACK_SIZE 0x10000ULL /* 64 KB */
#define DIMON64_STACK_BASE 0x03E80000ULL /* stacks occupy 0x03E80000..0x03EFFFFF */

/* MMIO registers (high memory) */
#define DIMON64_MMIO_TIMER_TICKS  0x03FFF000ULL /* u64 read: monotonic ticks */
#define DIMON64_MMIO_TIMER_PERIOD 0x03FFF008ULL /* u64 rw: cycles per tick */
#define DIMON64_MMIO_TIMER_VECTOR 0x03FFF010ULL /* u64 rw: ISR address (0=none) */
#define DIMON64_MMIO_SERIAL_DATA  0x03FFFF00ULL /* u8 rw: console port */

/* Programmable Sound Generator (PSG / Tone Synthesizer) */
#define DIMON64_MMIO_PSG_FREQ     0x03FFE000ULL /* u32 rw: Ch0 frequency in Hz */
#define DIMON64_MMIO_PSG_WAVE     0x03FFE004ULL /* u8 rw: Ch0 waveform (0=Square, 1=Triangle, 2=Noise) */
#define DIMON64_MMIO_PSG_VOL      0x03FFE005ULL /* u8 rw: Ch0 volume (0..255) */
#define DIMON64_MMIO_PSG_DUR      0x03FFE008ULL /* u32 rw: Ch0 duration in ms */
#define DIMON64_MMIO_PSG_STATUS   0x03FFE00CULL /* u8 rw: trigger/status (1=busy/play) */

#define DIMON64_PSG_WAVE_SQUARE   0
#define DIMON64_PSG_WAVE_TRIANGLE 1
#define DIMON64_PSG_WAVE_NOISE    2

#define PSG_WAVE_SQUARE           DIMON64_PSG_WAVE_SQUARE
#define PSG_WAVE_TRIANGLE         DIMON64_PSG_WAVE_TRIANGLE
#define PSG_WAVE_NOISE            DIMON64_PSG_WAVE_NOISE

/* Legacy aliases */
#define VRAM_ADDR DIMON64_VRAM_BASE
#define VRAM_COLS DIMON64_VRAM_COLS
#define VRAM_ROWS DIMON64_VRAM_ROWS
#define VRAM_SIZE DIMON64_VRAM_SIZE
#define MEM_SIZE DIMON64_MEM_SIZE
#define NUM_REGS DIMON64_NUM_REGS

/* ============ FLAGS bits ============ */
#define DIMON64_FLAG_Z  (1ULL << 0) /* Zero */
#define DIMON64_FLAG_N  (1ULL << 1) /* Negative (sign) */
#define DIMON64_FLAG_C  (1ULL << 2) /* Carry / borrow */
#define DIMON64_FLAG_O  (1ULL << 3) /* Signed overflow */
#define DIMON64_FLAG_IE (1ULL << 4) /* Interrupt enable */

/* ============ RISC-V style opcodes ============ */
#define DIMON64_OPCODE_LOAD   0x03u
#define DIMON64_OPCODE_OP_IMM 0x13u
#define DIMON64_OPCODE_AUIPC  0x17u
#define DIMON64_OPCODE_STORE  0x23u
#define DIMON64_OPCODE_OP     0x33u
#define DIMON64_OPCODE_LUI    0x37u
#define DIMON64_OPCODE_BRANCH 0x63u
#define DIMON64_OPCODE_JALR   0x67u
#define DIMON64_OPCODE_JAL    0x6Fu
#define DIMON64_OPCODE_SYSTEM 0x73u

/* R-type funct3 */
#define DIMON64_F3_ADD_SUB 0x0u
#define DIMON64_F3_SLL     0x1u
#define DIMON64_F3_SLT     0x2u
#define DIMON64_F3_SLTU    0x3u
#define DIMON64_F3_XOR     0x4u
#define DIMON64_F3_SRL_SRA 0x5u
#define DIMON64_F3_OR      0x6u
#define DIMON64_F3_AND     0x7u

/* R-type funct7 */
#define DIMON64_F7_BASE 0x00u
#define DIMON64_F7_ALT  0x20u
#define DIMON64_F7_MEXT 0x01u

/* OP-IMM funct3 */
#define DIMON64_F3_ADDI  0x0u
#define DIMON64_F3_SLLI  0x1u
#define DIMON64_F3_SLTI  0x2u
#define DIMON64_F3_SLTIU 0x3u
#define DIMON64_F3_XORI  0x4u
#define DIMON64_F3_SRLI  0x5u
#define DIMON64_F3_ORI   0x6u
#define DIMON64_F3_ANDI  0x7u

/* LOAD funct3 */
#define DIMON64_F3_LB  0x0u
#define DIMON64_F3_LH  0x1u
#define DIMON64_F3_LW  0x2u
#define DIMON64_F3_LD  0x3u
#define DIMON64_F3_LBU 0x4u
#define DIMON64_F3_LHU 0x5u
#define DIMON64_F3_LWU 0x6u

/* STORE funct3 */
#define DIMON64_F3_SB 0x0u
#define DIMON64_F3_SH 0x1u
#define DIMON64_F3_SW 0x2u
#define DIMON64_F3_SD 0x3u

/* BRANCH funct3 */
#define DIMON64_F3_BEQ  0x0u
#define DIMON64_F3_BNE  0x1u
#define DIMON64_F3_BLT  0x4u
#define DIMON64_F3_BGE  0x5u
#define DIMON64_F3_BLTU 0x6u
#define DIMON64_F3_BGEU 0x7u

/* SYSTEM immediates */
#define DIMON64_SYS_ECALL  0x000u
#define DIMON64_SYS_EBREAK 0x001u
#define DIMON64_SYS_IRET   0x102u

/* ============ Syscalls (a7 = id, a0-a6 args, return in a0) ============ */
enum {
    DIMON64_SYS_PUTCHAR  = 0,
    DIMON64_SYS_GETCHAR  = 1,
    DIMON64_SYS_PRINTSTR = 2,
    DIMON64_SYS_PRINTNUM = 3,
    DIMON64_SYS_READNUM  = 4,
    DIMON64_SYS_NEWLINE  = 5,
    DIMON64_SYS_DISK_READ  = 6,
    DIMON64_SYS_DISK_INFO  = 7,
    DIMON64_SYS_DISK_WRITE = 8,
    DIMON64_SYS_GUI_INIT       = 10,
    DIMON64_SYS_GUI_POLL_EVENT = 11,
    DIMON64_SYS_GUI_FLUSH      = 12,
    DIMON64_SYS_GET_TICKS      = 13,
    DIMON64_SYS_GUI_DRAW_RECT  = 14,
    DIMON64_SYS_GUI_DRAW_TEXT  = 15,
    DIMON64_SYS_YIELD = 16,
    DIMON64_SYS_SPAWN = 17, /* a0=entry, a1=arg, a2=name_ptr -> a0=pid or -1 */
    DIMON64_SYS_EXIT  = 18, /* a0=exit code */
    DIMON64_SYS_SLEEP = 19, /* a0=ticks to sleep */
    DIMON64_SYS_GETPID = 20,/* -> a0=pid */
    DIMON64_SYS_SET_TIMER_HANDLER = 21, /* a0=vector (0 disables) */
    DIMON64_SYS_SET_TIMER_PERIOD  = 22, /* a0=cycles per tick */
    DIMON64_SYS_ENABLE_INTERRUPTS = 23, /* a0=0/1 */
    DIMON64_SYS_GET_TIMER_TICKS   = 24, /* -> a0=ticks */
    DIMON64_SYS_GUI_DRAW_PIXEL    = 25, /* a0=x, a1=y, a2=color32 */
    DIMON64_SYS_GUI_DRAW_LINE     = 26, /* a0=x0, a1=y0, a2=x1, a3=y1, a4=color32 */
    DIMON64_SYS_GUI_BLIT          = 27, /* a0=dst_x, a1=dst_y, a2=w, a3=h, a4=src_addr */
    DIMON64_SYS_MEMSET            = 28, /* a0=dst_addr, a1=val32, a2=count_words */
    DIMON64_SYS_RTC_GET           = 29, /* -> a0=Unix seconds, C=1 if unavailable */
    DIMON64_SYS_PROC_INFO         = 30, /* a0=index, a1=Dimon64ProcInfo* -> 0/error */
    DIMON64_SYS_PROC_KILL         = 31, /* a0=pid -> 0/error; pid 0 is protected */
    DIMON64_SYS_FS_STAT           = 32, /* a0=path, a1=Dimon64DirEnt* */
    DIMON64_SYS_FS_READ           = 33, /* a0=path,a1=offset,a2=buf,a3=cap -> bytes; a1=size */
    DIMON64_SYS_FS_WRITE          = 34, /* a0=path,a1=buf,a2=len,a3=flags(create/truncate) */
    DIMON64_SYS_FS_LIST           = 35, /* a0=dir,a1=index,a2=Dimon64DirEnt* */
    DIMON64_SYS_FS_MKDIR          = 36, /* a0=path */
    DIMON64_SYS_FS_REMOVE         = 37, /* a0=path; directories must be empty */
    DIMON64_SYS_FS_RENAME         = 38, /* a0=old path,a1=new path */
    DIMON64_SYS_FS_COPY           = 39, /* a0=source,a1=destination */
    DIMON64_SYS_APP_EXEC          = 40, /* a0=path,a1=argument string -> pid/error */
    DIMON64_SYS_GUI_SET_CONTEXT   = 41, /* a0=dx,a1=dy,a2=x,a3=y,a4=w,a5=h; w/h=0 resets */
    DIMON64_SYS_GUI_TEXT_MEASURE  = 42, /* a0=UTF-8 string -> width in pixels */
    DIMON64_SYS_GUI_TEXT_FIT      = 43  /* x,y,str,fg,bg,max_width; clips and ellipsizes */
};

/* Legacy aliases for assembly sources */
#define SYS_PUTCHAR  DIMON64_SYS_PUTCHAR
#define SYS_GETCHAR  DIMON64_SYS_GETCHAR
#define SYS_PRINTSTR DIMON64_SYS_PRINTSTR
#define SYS_PRINTNUM DIMON64_SYS_PRINTNUM
#define SYS_READNUM  DIMON64_SYS_READNUM
#define SYS_NEWLINE  DIMON64_SYS_NEWLINE
#define SYS_DISK_READ  DIMON64_SYS_DISK_READ
#define SYS_DISK_INFO  DIMON64_SYS_DISK_INFO
#define SYS_DISK_WRITE DIMON64_SYS_DISK_WRITE
#define SYS_GUI_INIT DIMON64_SYS_GUI_INIT
#define SYS_GUI_POLL_EVENT DIMON64_SYS_GUI_POLL_EVENT
#define SYS_GUI_FLUSH DIMON64_SYS_GUI_FLUSH
#define SYS_GET_TICKS DIMON64_SYS_GET_TICKS
#define SYS_GUI_DRAW_RECT DIMON64_SYS_GUI_DRAW_RECT
#define SYS_GUI_DRAW_TEXT DIMON64_SYS_GUI_DRAW_TEXT
#define SYS_YIELD DIMON64_SYS_YIELD
#define SYS_SPAWN DIMON64_SYS_SPAWN
#define SYS_EXIT DIMON64_SYS_EXIT
#define SYS_SLEEP DIMON64_SYS_SLEEP
#define SYS_GETPID DIMON64_SYS_GETPID
#define SYS_SET_TIMER_HANDLER DIMON64_SYS_SET_TIMER_HANDLER
#define SYS_SET_TIMER_PERIOD DIMON64_SYS_SET_TIMER_PERIOD
#define SYS_ENABLE_INTERRUPTS DIMON64_SYS_ENABLE_INTERRUPTS
#define SYS_GET_TIMER_TICKS DIMON64_SYS_GET_TIMER_TICKS
#define SYS_GUI_DRAW_PIXEL DIMON64_SYS_GUI_DRAW_PIXEL
#define SYS_GUI_DRAW_LINE DIMON64_SYS_GUI_DRAW_LINE
#define SYS_GUI_BLIT DIMON64_SYS_GUI_BLIT
#define SYS_MEMSET DIMON64_SYS_MEMSET
#define SYS_RTC_GET DIMON64_SYS_RTC_GET
#define SYS_PROC_INFO DIMON64_SYS_PROC_INFO
#define SYS_PROC_KILL DIMON64_SYS_PROC_KILL
#define SYS_FS_STAT DIMON64_SYS_FS_STAT
#define SYS_FS_READ DIMON64_SYS_FS_READ
#define SYS_FS_WRITE DIMON64_SYS_FS_WRITE
#define SYS_FS_LIST DIMON64_SYS_FS_LIST
#define SYS_FS_MKDIR DIMON64_SYS_FS_MKDIR
#define SYS_FS_REMOVE DIMON64_SYS_FS_REMOVE
#define SYS_FS_RENAME DIMON64_SYS_FS_RENAME
#define SYS_FS_COPY DIMON64_SYS_FS_COPY
#define SYS_APP_EXEC DIMON64_SYS_APP_EXEC
#define SYS_GUI_SET_CONTEXT DIMON64_SYS_GUI_SET_CONTEXT
#define SYS_GUI_TEXT_MEASURE DIMON64_SYS_GUI_TEXT_MEASURE
#define SYS_GUI_TEXT_FIT DIMON64_SYS_GUI_TEXT_FIT
#define SYSYIELD SYS_YIELD
#define SYSSPAWN SYS_SPAWN
#define SYSEXIT SYS_EXIT
#define SYSSLEEP SYS_SLEEP
#define SYSGETPID SYS_GETPID

/* Interrupts */
#define DIMON64_INT_TIMER 0

/* GUI event types */
#define EVT_NONE        0
#define EVT_KEY         1
#define EVT_MOUSE_CLICK 2
#define EVT_MOUSE_MOVE  3
#define EVT_TIMER       4
#define EVT_MOUSE_RELEASE 5

/* Special keycodes */
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
#define KEY_HOME  270
#define KEY_END   271
#define KEY_DELETE 272
#define KEY_PGUP  273
#define KEY_PGDN  274

#define KEYMOD_SHIFT 0x01u
#define KEYMOD_CTRL  0x02u
#define KEYMOD_ALT   0x04u
#define KEYMOD_META  0x08u

#define VM_EVENT_QUEUE_SIZE 128

typedef struct {
    uint8_t  type;
    uint8_t  button; /* 1=left, 2=right, 0=none */
    uint16_t code;   /* keycode or mouse X (0..799) */
    uint16_t data;   /* mouse Y (0..599) */
    uint8_t  modifiers; /* KEYMOD_* for keyboard and pointer events */
} VMEvent;

/* Stable guest-visible structures. Strings are UTF-8; FAT names are currently 8.3. */
typedef struct {
    char name[13];
    uint8_t attributes;
    uint8_t reserved[3];
    uint32_t size;
    uint32_t first_cluster;
} Dimon64DirEnt;

typedef struct {
    uint64_t pid;
    uint64_t state;
    uint64_t memory_base;
    uint64_t memory_size;
    uint64_t cpu_steps;
    uint64_t context_switches;
    char name[16];
    uint8_t essential;
    uint8_t isolated;
    uint8_t reserved[6];
} Dimon64ProcInfo;

#define DIMON64_EXEC_MAGIC "DEXE64\0"
#define DIMON64_EXEC_VERSION 1u
#define DIMON64_APP_BASE 0x02800000ULL
#define DIMON64_APP_SLOT_SIZE 0x00100000ULL

/* DEXE64 is little-endian: header, flat image, then uint32 relocation offsets.
   Relocations identify assembler-emitted LUI+ADDI absolute-address pairs. */
typedef struct {
    uint8_t magic[8];
    uint32_t version;
    uint32_t header_size;
    uint32_t image_size;
    uint32_t bss_size;
    uint32_t entry_offset;
    uint32_t memory_size;
    uint32_t relocation_count;
    uint32_t flags;
    char name[16];
} Dimon64ExecHeader;

/* Disk / ISO image (DIMON-ISO): 512B sectors */
#define DISK_SECTOR_SIZE 512
#define DISK_MAX_SECTORS 8192
#define DISK_BOOT_MAGIC0 0x55
#define DISK_BOOT_MAGIC1 0xAA
#define DISK_CAT_SECTOR  1
#define DISK_CAT_MAGIC   0x444D
#define DISK_FNAME_LEN   12
#define DISK_FILE_MAX    21

enum {
    DISK_ERR_NONE = 0,
    DISK_ERR_NODISK = 1,
    DISK_ERR_RANGE = 2,
    DISK_ERR_RAM = 3,
    DISK_ERR_READONLY = 4,
    DISK_ERR_IO = 5
};

#define PORT_CONSOLE 0

/* Process states */
enum {
    DIMON64_PROC_FREE = 0,
    DIMON64_PROC_READY = 1,
    DIMON64_PROC_RUNNING = 2,
    DIMON64_PROC_BLOCKED = 3,
    DIMON64_PROC_TERMINATED = 4
};

/* Native process control block (host-side scheduler).
   The OS-level PCB layout in guest memory is documented in docs/
   and mirrors these fields: PID, STATE, SAVEDPC, SAVEDSP,
   SAVEDREGS[32], STACKBASE, STACKSIZE, NAME[16]. */
typedef struct {
    uint64_t pid;
    uint64_t state;
    uint64_t pc;
    uint64_t flags;
    uint64_t regs[DIMON64_NUM_REGS];
    uint64_t stack_base;
    uint64_t stack_size;
    uint64_t sleep_until;
    uint64_t memory_base;
    uint64_t memory_size;
    uint64_t cpu_steps;
    uint64_t context_switches;
    char     name[16];
    uint8_t  used;
    uint8_t  essential;
    uint8_t  isolated;
    int32_t  last_fault;
} Dimon64Proc;

typedef struct {
    uint64_t regs[DIMON64_NUM_REGS]; /* R0..R31 (x0..x31) */
    uint64_t pc;      /* Program counter (byte address, 4-byte aligned) */
    uint64_t flags;   /* Z/N/C/O/IE bits */
    uint64_t timer_ticks;  /* Monotonic tick counter */
    uint64_t timer_period; /* Cycles per tick */
    uint64_t timer_vector; /* Timer ISR address (0 = none) */
    uint64_t epc;     /* Saved PC on trap */
    uint64_t eflags;  /* Saved FLAGS on trap */
    uint8_t  halted;
    uint8_t  in_isr;   /* 1 while servicing timer interrupt */
    uint8_t *mem;
    uint64_t memsize;
    uint64_t steps;
    uint64_t max_steps;
    uint64_t cycle_counter; /* Cycles since last tick */

    /* Preemptive multitasking engine (native scheduler) */
    Dimon64Proc procs[DIMON64_MAX_PROCS];
    int         cur_proc;
    uint64_t    next_pid;
    uint64_t    switches; /* Context switch counter */

    /* Virtual disk */
    uint8_t  *disk_data;
    uint32_t disk_sectors;
    uint8_t  disk_writable;
    char     disk_path[256];

    /* GUI subsystem */
    VMEvent  event_queue[VM_EVENT_QUEUE_SIZE];
    int      event_head;
    int      event_tail;
    uint64_t event_owner_pid; /* foreground isolated app, 0 = desktop */
    uint8_t  gui_active;
    uint8_t  gui_dirty;
    int32_t  draw_offset_x;
    int32_t  draw_offset_y;
    int32_t  draw_clip_x;
    int32_t  draw_clip_y;
    int32_t  draw_clip_w;
    int32_t  draw_clip_h;
    uint8_t  draw_context_active;
    void    (*gui_init_cb)(void *userdata);
    void    (*gui_flush_cb)(void *userdata);
    void    (*gui_poll_cb)(void *userdata);
    void     *gui_userdata;

    /* Sound generator (PSG) */
    uint32_t psg_freq;
    uint8_t  psg_wave;
    uint8_t  psg_vol;
    uint32_t psg_duration_ms;
    uint64_t psg_end_time_ms;
    void   (*psg_play_cb)(void *userdata, uint32_t freq, uint32_t duration_ms, uint8_t wave, uint8_t vol);
    void    *psg_userdata;
} VM;

/* GUI helpers */
void vm_event_push(VM *vm, uint8_t type, uint16_t code, uint16_t data);
void vm_event_push_ext(VM *vm, uint8_t type, uint16_t code, uint16_t data, uint8_t button);
void vm_event_push_mod(VM *vm, uint8_t type, uint16_t code, uint16_t data, uint8_t button, uint8_t modifiers);
int  vm_event_pop(VM *vm, uint8_t *type, uint16_t *code, uint16_t *data);
int  vm_event_pop_ext(VM *vm, uint8_t *type, uint16_t *code, uint16_t *data, uint8_t *button);
void vm_gui_draw_pixel(VM *vm, int x, int y, uint32_t color32);
void vm_gui_fill_rect(VM *vm, int x, int y, int w, int h, uint32_t color32);
void vm_gui_draw_line(VM *vm, int x0, int y0, int x1, int y1, uint32_t color32);
void vm_gui_draw_string(VM *vm, int x, int y, const char *text, uint32_t fg, uint32_t bg);
void vm_gui_draw_rect(VM *vm, int x, int y, int w, int h, uint8_t ch, uint8_t attr);
void vm_gui_draw_text(VM *vm, int x, int y, const char *text, uint8_t attr);

/* Lifecycle */
void vm_init(VM *vm);
void vm_free(VM *vm);
#ifndef BAREMETAL
int  vm_load(VM *vm, const char *path, uint64_t load_addr);
void vm_dump_regs(VM *vm, FILE *out);
int  vm_disk_attach(VM *vm, const char *path, int writable);
#endif
int  vm_load_buf(VM *vm, const uint8_t *buf, size_t len, uint64_t load_addr);
void vm_reset(VM *vm, uint64_t start_pc);

void vm_disk_detach(VM *vm);
int  vm_disk_boot(VM *vm, uint64_t load_addr);
int  vm_disk_read(VM *vm, uint32_t lba, uint64_t ram_addr, uint64_t count);
int  vm_disk_write(VM *vm, uint32_t lba, uint64_t ram_addr, uint64_t count);

int  vm_step(VM *vm);
int  vm_run(VM *vm);

/* Helpers */
const char *dimon64_reg_name(int r);
const char *dimon64_reg_abi(int r);
int  dimon64_disasm(VM *vm, uint64_t addr, char *out, size_t outsz);

/* Legacy compatibility shims */
#ifndef DIMON64_NO_LEGACY
const char *reg_name(int r);
int  vm_disasm(VM *vm, uint64_t addr, char *out, size_t outsz);
void vm_stb(VM *vm, uint64_t addr, uint8_t v);
uint8_t vm_ldb(VM *vm, uint64_t addr);
#endif

/* Instruction encoding helpers (little-endian 32-bit words) */
static inline uint32_t dimon64_encode_r(uint8_t rd, uint8_t funct3, uint8_t rs1,
                          uint8_t rs2, uint8_t funct7, uint8_t opcode) {
    return ((uint32_t)(funct7 & 0x7Fu) << 25) | ((uint32_t)(rs2 & 0x1Fu) << 20) |
           ((uint32_t)(rs1 & 0x1Fu) << 15) | ((uint32_t)(funct3 & 0x7u) << 12) |
           ((uint32_t)(rd & 0x1Fu) << 7) | (uint32_t)(opcode & 0x7Fu);
}
static inline uint32_t dimon64_encode_i(uint8_t rd, uint8_t funct3, uint8_t rs1,
                          int32_t imm12, uint8_t opcode) {
    uint32_t imm = (uint32_t)(imm12 & 0xFFF);
    return (imm << 20) | ((uint32_t)(rs1 & 0x1Fu) << 15) |
           ((uint32_t)(funct3 & 0x7u) << 12) | ((uint32_t)(rd & 0x1Fu) << 7) |
           (uint32_t)(opcode & 0x7Fu);
}
static inline uint32_t dimon64_encode_s(uint8_t funct3, uint8_t rs1, uint8_t rs2,
                          int32_t imm12, uint8_t opcode) {
    uint32_t imm = (uint32_t)(imm12 & 0xFFF);
    uint32_t imm_low = imm & 0x1Fu;
    uint32_t imm_high = (imm >> 5) & 0x7Fu;
    return (imm_high << 25) | ((uint32_t)(rs2 & 0x1Fu) << 20) |
           ((uint32_t)(rs1 & 0x1Fu) << 15) | ((uint32_t)(funct3 & 0x7u) << 12) |
           (imm_low << 7) | (uint32_t)(opcode & 0x7Fu);
}
static inline uint32_t dimon64_encode_b(uint8_t funct3, uint8_t rs1, uint8_t rs2,
                          int32_t offset, uint8_t opcode) {
    uint32_t imm = (uint32_t)(offset & 0x1FFE);
    uint32_t b12 = (imm >> 12) & 0x1u;
    uint32_t b11 = (imm >> 11) & 0x1u;
    uint32_t b10_5 = (imm >> 5) & 0x3Fu;
    uint32_t b4_1 = (imm >> 1) & 0xFu;
    return (b12 << 31) | (b10_5 << 25) | ((uint32_t)(rs2 & 0x1Fu) << 20) |
           ((uint32_t)(rs1 & 0x1Fu) << 15) | ((uint32_t)(funct3 & 0x7u) << 12) |
           (b4_1 << 8) | (b11 << 7) | (uint32_t)(opcode & 0x7Fu);
}
static inline uint32_t dimon64_encode_u(uint8_t rd, int32_t imm20, uint8_t opcode) {
    uint32_t imm = (uint32_t)(imm20 & 0xFFFFF);
    return (imm << 12) | ((uint32_t)(rd & 0x1Fu) << 7) | (uint32_t)(opcode & 0x7Fu);
}
static inline uint32_t dimon64_encode_j(uint8_t rd, int32_t offset, uint8_t opcode) {
    uint32_t imm = (uint32_t)(offset & 0x1FFFFE);
    uint32_t b20 = (imm >> 20) & 0x1u;
    uint32_t b19_12 = (imm >> 12) & 0xFFu;
    uint32_t b11 = (imm >> 11) & 0x1u;
    uint32_t b10_1 = (imm >> 1) & 0x3FFu;
    return (b20 << 31) | (b10_1 << 21) | (b11 << 20) | (b19_12 << 12) |
           ((uint32_t)(rd & 0x1Fu) << 7) | (uint32_t)(opcode & 0x7Fu);
}

/* Memory access with bounds checking (0 ok, <0 error) */
int vm_mem_read(VM *vm, uint64_t addr, void *out, size_t len);
int vm_mem_write(VM *vm, uint64_t addr, const void *in, size_t len);

#endif /* DIMON64_H */
