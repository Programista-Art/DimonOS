/* DimonOS native kernel runtime — GUI, events, disk.
 * Extracted from the former vm.c; contains no CPU interpreter. */
#include "kernel_runtime.h"
#include "dimonfs.h"
#include "font8x16.h"
#include <stdint.h>
#include <stddef.h>

#ifndef BAREMETAL
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#endif

#ifdef BAREMETAL
void *memset(void *s, int c, size_t n);
void *memcpy(void *dest, const void *src, size_t n);
void *memmove(void *dest, const void *src, size_t n);
size_t strlen(const char *s);
int snprintf(char *str, size_t size, const char *format, ...);
#endif

/* ---------- events ---------- */
void sys_event_push_mod(DimonSystem *sys, uint8_t type, uint16_t code, uint16_t data,
                        uint8_t button, uint8_t modifiers) {
    if (!sys) return;
    int next = (sys->event_tail + 1) % VM_EVENT_QUEUE_SIZE;
    if (next != sys->event_head) {
        sys->event_queue[sys->event_tail].type = type;
        sys->event_queue[sys->event_tail].button = button;
        sys->event_queue[sys->event_tail].code = code;
        sys->event_queue[sys->event_tail].data = data;
        sys->event_queue[sys->event_tail].modifiers = modifiers;
        sys->event_tail = next;
    }
}

void sys_event_push_ext(DimonSystem *sys, uint8_t type, uint16_t code, uint16_t data, uint8_t button) {
    sys_event_push_mod(sys, type, code, data, button, 0);
}

void sys_event_push(DimonSystem *sys, uint8_t type, uint16_t code, uint16_t data) {
    sys_event_push_ext(sys, type, code, data, (type == EVT_MOUSE_CLICK) ? 1 : 0);
}

int sys_event_pop_ext(DimonSystem *sys, uint8_t *type, uint16_t *code, uint16_t *data, uint8_t *button) {
    if (!sys) return 0;
    if (sys->event_head == sys->event_tail) {
        *type = EVT_NONE; *code = 0; *data = 0; if (button) *button = 0;
        return 0;
    }
    *type = sys->event_queue[sys->event_head].type;
    *code = sys->event_queue[sys->event_head].code;
    *data = sys->event_queue[sys->event_head].data;
    if (button) *button = sys->event_queue[sys->event_head].button;
    sys->event_queue[sys->event_head].type = EVT_NONE;
    sys->event_queue[sys->event_head].code = 0;
    sys->event_queue[sys->event_head].data = 0;
    sys->event_queue[sys->event_head].button = 0;
    sys->event_queue[sys->event_head].modifiers = 0;
    sys->event_head = (sys->event_head + 1) % VM_EVENT_QUEUE_SIZE;
    return 1;
}

int sys_event_pop(DimonSystem *sys, uint8_t *type, uint16_t *code, uint16_t *data) {
    return sys_event_pop_ext(sys, type, code, data, NULL);
}

/* ---------- GUI ---------- */
void sys_gui_draw_pixel(DimonSystem *sys, int x, int y, uint32_t color32) {
    if (!sys || !sys->mem) return;
    if (sys->draw_context_active) {
        x += sys->draw_offset_x; y += sys->draw_offset_y;
        if (x < sys->draw_clip_x || y < sys->draw_clip_y ||
            x >= sys->draw_clip_x + sys->draw_clip_w ||
            y >= sys->draw_clip_y + sys->draw_clip_h) return;
    }
    if (x < 0 || x >= DIMON64_LFB_WIDTH || y < 0 || y >= DIMON64_LFB_HEIGHT) return;
    uint64_t addr = DIMON64_VRAM_BASE + (uint64_t)(y * DIMON64_LFB_WIDTH + x) * 4ULL;
    if (addr + 4 > sys->memsize) return;
    *(uint32_t *)(sys->mem + addr) = color32;
    sys->gui_dirty = 1;
}

void sys_gui_fill_rect(DimonSystem *sys, int x, int y, int w, int h, uint32_t color32) {
    if (!sys || !sys->mem || w <= 0 || h <= 0) return;
    if (sys->draw_context_active) { x += sys->draw_offset_x; y += sys->draw_offset_y; }
    int x0 = x < 0 ? 0 : x;
    int y0 = y < 0 ? 0 : y;
    int x1 = x + w;
    int y1 = y + h;
    if (x1 > DIMON64_LFB_WIDTH) x1 = DIMON64_LFB_WIDTH;
    if (y1 > DIMON64_LFB_HEIGHT) y1 = DIMON64_LFB_HEIGHT;
    if (sys->draw_context_active) {
        if (x0 < sys->draw_clip_x) x0 = sys->draw_clip_x;
        if (y0 < sys->draw_clip_y) y0 = sys->draw_clip_y;
        if (x1 > sys->draw_clip_x + sys->draw_clip_w) x1 = sys->draw_clip_x + sys->draw_clip_w;
        if (y1 > sys->draw_clip_y + sys->draw_clip_h) y1 = sys->draw_clip_y + sys->draw_clip_h;
    }
    if (x0 >= x1 || y0 >= y1) return;
    for (int cy = y0; cy < y1; cy++) {
        uint64_t row_addr = DIMON64_VRAM_BASE + (uint64_t)(cy * DIMON64_LFB_WIDTH + x0) * 4ULL;
        if (row_addr + (uint64_t)(x1 - x0) * 4ULL > sys->memsize) continue;
        uint32_t *p = (uint32_t *)(sys->mem + row_addr);
        for (int cx = 0; cx < x1 - x0; cx++) p[cx] = color32;
    }
    sys->gui_dirty = 1;
}

void sys_gui_draw_line(DimonSystem *sys, int x0, int y0, int x1, int y1, uint32_t color32) {
    int dx = x1 > x0 ? x1 - x0 : x0 - x1, sx = x0 < x1 ? 1 : -1;
    int dy = y1 > y0 ? y0 - y1 : y1 - y0, sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    for (;;) {
        sys_gui_draw_pixel(sys, x0, y0, color32);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

static size_t u8_seq_len(const unsigned char *s) {
    if (!s[0]) return 0;
    if (s[0] < 0x80) return 1;
    if ((s[0] & 0xe0) == 0xc0 && (s[1] & 0xc0) == 0x80) return 2;
    if ((s[0] & 0xf0) == 0xe0 && (s[1] & 0xc0) == 0x80 && (s[2] & 0xc0) == 0x80) return 3;
    if ((s[0] & 0xf8) == 0xf0 && (s[1] & 0xc0) == 0x80 &&
        (s[2] & 0xc0) == 0x80 && (s[3] & 0xc0) == 0x80) return 4;
    return 1;
}

int sys_gui_text_measure(const char *s) {
    int glyphs = 0;
    while (s && *s) { s += u8_seq_len((const unsigned char *)s); glyphs++; }
    return glyphs * 8;
}

void sys_gui_draw_string(DimonSystem *sys, int x, int y, const char *text, uint32_t fg, uint32_t bg) {
    if (!sys || !sys->mem || !text) return;
    if (sys->draw_context_active) { x += sys->draw_offset_x; y += sys->draw_offset_y; }
    int cur_x = x;
    int draw_bg = ((bg & 0xFF000000) != 0);
    while (*text) {
        uint32_t cp = (uint8_t)*text++;
        if ((cp & 0xe0u) == 0xc0u && ((uint8_t)*text & 0xc0u) == 0x80u) {
            cp = ((cp & 0x1fu) << 6) | ((uint8_t)*text++ & 0x3fu);
        } else if ((cp & 0xf0u) == 0xe0u && (uint8_t)text[0] < 0xC0) {
            /* handled below by u8 decode fallback */
        }
        const uint8_t *glyph = font8x16[cp <= 255u ? cp : (uint32_t)'?'];
        if (cur_x + 8 > 0 && cur_x < DIMON64_LFB_WIDTH && y + 16 > 0 && y < DIMON64_LFB_HEIGHT) {
            for (int r = 0; r < 16; r++) {
                int py = y + r;
                if (py < 0 || py >= DIMON64_LFB_HEIGHT) continue;
                uint8_t bits = glyph[r];
                for (int c = 0; c < 8; c++) {
                    int px = cur_x + c;
                    if (px < 0 || px >= DIMON64_LFB_WIDTH) continue;
                    if (sys->draw_context_active &&
                        (px < sys->draw_clip_x || py < sys->draw_clip_y ||
                         px >= sys->draw_clip_x + sys->draw_clip_w ||
                         py >= sys->draw_clip_y + sys->draw_clip_h)) continue;
                    if (bits & (0x80 >> c)) {
                        uint64_t addr = DIMON64_VRAM_BASE + (uint64_t)(py * DIMON64_LFB_WIDTH + px) * 4ULL;
                        if (addr + 4 <= sys->memsize) *(uint32_t *)(sys->mem + addr) = fg;
                    } else if (draw_bg) {
                        uint64_t addr = DIMON64_VRAM_BASE + (uint64_t)(py * DIMON64_LFB_WIDTH + px) * 4ULL;
                        if (addr + 4 <= sys->memsize) *(uint32_t *)(sys->mem + addr) = bg;
                    }
                }
            }
        }
        cur_x += 8;
        if (cur_x >= DIMON64_LFB_WIDTH) break;
    }
    sys->gui_dirty = 1;
}

void sys_gui_draw_string_fit(DimonSystem *sys, int x, int y, const char *text,
                             uint32_t fg, uint32_t bg, int max_w) {
    if (!text || max_w <= 0) return;
    if (sys_gui_text_measure(text) <= max_w) {
        sys_gui_draw_string(sys, x, y, text, fg, bg);
        return;
    }
    char fitted[256];
    int max_glyphs = max_w / 8;
    if (max_glyphs <= 0) return;
    int keep = max_glyphs > 3 ? max_glyphs - 3 : max_glyphs;
    const unsigned char *p = (const unsigned char *)text;
    size_t used = 0;
    int gi = 0;
    while (*p && gi < keep && used + 4 < sizeof(fitted)) {
        size_t n = u8_seq_len(p);
        for (size_t k = 0; k < n && used + 1 < sizeof(fitted); k++) fitted[used++] = (char)p[k];
        p += n; gi++;
    }
    while (gi++ < max_glyphs && used + 1 < sizeof(fitted)) fitted[used++] = '.';
    fitted[used] = 0;
    sys_gui_draw_string(sys, x, y, fitted, fg, bg);
}

void sys_gui_blit(DimonSystem *sys, int dx, int dy, int w, int h, const void *src) {
    if (!sys || !sys->mem || !src || w <= 0 || h <= 0) return;
    if (sys->draw_context_active) { dx += sys->draw_offset_x; dy += sys->draw_offset_y; }
    int x0 = dx < 0 ? 0 : dx, y0 = dy < 0 ? 0 : dy;
    int x1 = dx + w, y1 = dy + h;
    if (x1 > DIMON64_LFB_WIDTH) x1 = DIMON64_LFB_WIDTH;
    if (y1 > DIMON64_LFB_HEIGHT) y1 = DIMON64_LFB_HEIGHT;
    if (sys->draw_context_active) {
        if (x0 < sys->draw_clip_x) x0 = sys->draw_clip_x;
        if (y0 < sys->draw_clip_y) y0 = sys->draw_clip_y;
        if (x1 > sys->draw_clip_x + sys->draw_clip_w) x1 = sys->draw_clip_x + sys->draw_clip_w;
        if (y1 > sys->draw_clip_y + sys->draw_clip_h) y1 = sys->draw_clip_y + sys->draw_clip_h;
    }
    const uint32_t *s = (const uint32_t *)src;
    uint32_t *vram = (uint32_t *)(sys->mem + DIMON64_VRAM_BASE);
    for (int sy = y0; sy < y1; sy++) {
        int source_y = sy - dy;
        for (int sx = x0; sx < x1; sx++) {
            vram[sy * DIMON64_LFB_WIDTH + sx] = s[source_y * w + (sx - dx)];
        }
    }
    sys->gui_dirty = 1;
}

void sys_gui_set_context(DimonSystem *sys, int dx, int dy, int x, int y, int w, int h) {
    if (!sys) return;
    if ((int64_t)w <= 0 || (int64_t)h <= 0) {
        sys->draw_context_active = 0;
        sys->draw_offset_x = sys->draw_offset_y = 0;
        return;
    }
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > DIMON64_LFB_WIDTH) w = DIMON64_LFB_WIDTH - x;
    if (y + h > DIMON64_LFB_HEIGHT) h = DIMON64_LFB_HEIGHT - y;
    sys->draw_offset_x = dx; sys->draw_offset_y = dy;
    sys->draw_clip_x = x; sys->draw_clip_y = y;
    sys->draw_clip_w = w > 0 ? w : 0;
    sys->draw_clip_h = h > 0 ? h : 0;
    sys->draw_context_active = 1;
}

/* ---------- lifecycle ---------- */
void sys_init(DimonSystem *sys) {
    if (!sys) return;
    memset(sys, 0, sizeof(*sys));
    sys->memsize = DIMON64_MEM_SIZE;
#ifndef BAREMETAL
    sys->mem = (uint8_t *)calloc(1, (size_t)sys->memsize);
#endif
    sys->psg_vol = 255;
    sys->storage_mode = DIMON_STORAGE_NODISK;
}

void sys_free(DimonSystem *sys) {
    if (!sys) return;
#ifndef BAREMETAL
    if (sys->disk_data) { free(sys->disk_data); sys->disk_data = NULL; }
    if (sys->mem) { free(sys->mem); sys->mem = NULL; }
#else
    sys->mem = NULL;
#endif
    sys->disk_sectors = 0;
    sys->memsize = 0;
}

/* ---------- disk ---------- */
void disk_detach(DimonDisk *dk) {
    if (!dk) return;
#ifndef BAREMETAL
    if (dk->disk_data) { free(dk->disk_data); dk->disk_data = NULL; }
#endif
    dk->disk_sectors = 0;
    dk->disk_writable = 0;
    dk->storage_mode = DIMON_STORAGE_NODISK;
    dk->block_device = NULL;
    dk->disk_path[0] = 0;
}

void disk_block_bind(DimonDisk *dk, Dimon64BlockDevice *dev) {
    if (dk) dk->block_device = dev;
}
void disk_block_unbind(DimonDisk *dk) {
    if (dk) dk->block_device = NULL;
}

#ifndef BAREMETAL
int disk_attach_file(DimonDisk *dk, const char *path, int writable) {
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
    disk_detach(dk);
    dk->disk_data = buf;
    dk->disk_sectors = sectors;
    if (writable) {
        FILE *tf = fopen(path, "r+b");
        if (tf) { dk->disk_writable = 1; fclose(tf); }
        else dk->disk_writable = 0;
    } else dk->disk_writable = 0;
    dk->storage_mode = dk->disk_writable ? DIMON_STORAGE_WRITABLE : DIMON_STORAGE_READONLY;
    for (size_t i = 0; path[i] && i + 1 < sizeof(dk->disk_path); i++) dk->disk_path[i] = path[i];
    return 0;
}
#else
int disk_attach_file(DimonDisk *dk, const char *path, int writable) {
    (void)dk; (void)path; (void)writable; return -1;
}
#endif

int disk_sync(DimonDisk *dk) {
    if (!dk || !dk->disk_data || dk->disk_sectors == 0) return DISK_ERR_NODISK;
    if (!dk->disk_writable) return DISK_ERR_READONLY;
    if (!dk->block_device || !dk->block_device->write) return DISK_ERR_NONE;
    uint32_t remaining = dk->disk_sectors, lba = 0;
    while (remaining > 0) {
        uint32_t chunk = remaining > 32u ? 32u : remaining;
        int rc = dk->block_device->write(dk->block_device->context, lba, chunk,
                                         dk->disk_data + (size_t)lba * DISK_SECTOR_SIZE);
        if (rc != DISK_ERR_NONE) return rc;
        lba += chunk;
        remaining -= chunk;
    }
    if (dk->block_device->flush) return dk->block_device->flush(dk->block_device->context);
    return DISK_ERR_NONE;
}
