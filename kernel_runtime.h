/* DimonOS native kernel runtime — GUI, events, disk. No CPU interpreter. */
#ifndef DIMON_KERNEL_RUNTIME_H
#define DIMON_KERNEL_RUNTIME_H

#include "dimon64.h"

/* Disk/filesystem context shared by dimonfs.c, mkiso.c and the kernel.
 * (DimonDisk lives in dimon64.h.) */

/* Full native system state (disk + framebuffer draw context + events + PSG). */
typedef struct {
    /* DimonDisk-compatible prefix (must stay first). */
    uint8_t  *mem;
    uint64_t memsize;
    uint8_t  *disk_data;
    uint32_t disk_sectors;
    uint8_t  disk_writable;
    uint8_t  storage_mode;
    Dimon64BlockDevice *block_device;
    char     disk_path[256];

    /* Event queue */
    VMEvent  event_queue[VM_EVENT_QUEUE_SIZE];
    int      event_head;
    int      event_tail;

    /* GUI draw context / framebuffer helpers */
    uint8_t  gui_dirty;
    int32_t  draw_offset_x, draw_offset_y;
    int32_t  draw_clip_x, draw_clip_y, draw_clip_w, draw_clip_h;
    uint8_t  draw_context_active;
    void   (*gui_init_cb)(void *userdata);
    void   (*gui_flush_cb)(void *userdata);
    void   (*gui_poll_cb)(void *userdata);
    void    *gui_userdata;

    /* PSG / speaker */
    uint32_t psg_freq;
    uint8_t  psg_wave;
    uint8_t  psg_vol;
    uint32_t psg_duration_ms;
    uint64_t psg_end_time_ms;
    void   (*psg_play_cb)(void *userdata, uint32_t freq, uint32_t duration_ms, uint8_t wave, uint8_t vol);
    void    *psg_userdata;
} DimonSystem;

/* Backward-compatible alias used by desktop.c. */
typedef DimonSystem VM;

/* Events */
void sys_event_push(DimonSystem *sys, uint8_t type, uint16_t code, uint16_t data);
void sys_event_push_ext(DimonSystem *sys, uint8_t type, uint16_t code, uint16_t data, uint8_t button);
void sys_event_push_mod(DimonSystem *sys, uint8_t type, uint16_t code, uint16_t data, uint8_t button, uint8_t modifiers);
int  sys_event_pop(DimonSystem *sys, uint8_t *type, uint16_t *code, uint16_t *data);
int  sys_event_pop_ext(DimonSystem *sys, uint8_t *type, uint16_t *code, uint16_t *data, uint8_t *button);
#define vm_event_push        sys_event_push
#define vm_event_push_ext    sys_event_push_ext
#define vm_event_push_mod    sys_event_push_mod
#define vm_event_pop         sys_event_pop
#define vm_event_pop_ext     sys_event_pop_ext

/* GUI (800x600 TrueColor, clipped, UTF-8 via font8x16) */
void sys_gui_draw_pixel(DimonSystem *sys, int x, int y, uint32_t color32);
void sys_gui_fill_rect(DimonSystem *sys, int x, int y, int w, int h, uint32_t color32);
void sys_gui_draw_line(DimonSystem *sys, int x0, int y0, int x1, int y1, uint32_t color32);
void sys_gui_draw_string(DimonSystem *sys, int x, int y, const char *text, uint32_t fg, uint32_t bg);
void sys_gui_draw_string_fit(DimonSystem *sys, int x, int y, const char *text, uint32_t fg, uint32_t bg, int max_w);
int  sys_gui_text_measure(const char *s);
void sys_gui_blit(DimonSystem *sys, int dx, int dy, int w, int h, const void *src);
void sys_gui_set_context(DimonSystem *sys, int dx, int dy, int x, int y, int w, int h);
#define vm_gui_draw_pixel       sys_gui_draw_pixel
#define vm_gui_fill_rect        sys_gui_fill_rect
#define vm_gui_draw_line        sys_gui_draw_line
#define vm_gui_draw_string      sys_gui_draw_string

/* Lifecycle / disk */
void sys_init(DimonSystem *sys);
void sys_free(DimonSystem *sys);
void disk_detach(DimonDisk *dk);
void disk_block_bind(DimonDisk *dk, Dimon64BlockDevice *dev);
void disk_block_unbind(DimonDisk *dk);
int  disk_attach_file(DimonDisk *dk, const char *path, int writable);
int  disk_sync(DimonDisk *dk);
#define vm_init           sys_init
#define vm_free           sys_free
#define vm_disk_detach    disk_detach
#define vm_block_bind     disk_block_bind
#define vm_block_unbind   disk_block_unbind
#define vm_disk_sync      disk_sync

#endif /* DIMON_KERNEL_RUNTIME_H */
