/* NAPP64 native application container. */
#ifndef DIMON_NAPP_H
#define DIMON_NAPP_H

#include <stdint.h>

#define NAPP_MAGIC "NAPP64"
#define NAPP_VERSION 1u

typedef struct {
    char     magic[8];      /* "NAPP64\0" */
    uint32_t version;
    uint32_t entry_main;    /* offset from image start */
    uint32_t entry_key;
    uint32_t entry_timer;
    uint32_t entry_draw;
    uint32_t image_size;
    char     name[16];
} NAppHeader;

typedef struct {
    void *sys;
    void (*fill_rect)(void *sys, int x, int y, int w, int h, uint32_t color);
    void (*draw_text)(void *sys, int x, int y, const char *s, uint32_t fg);
    int  (*text_measure)(const char *s);
    void (*beep)(void *sys, uint32_t freq, uint32_t ms);
    uint32_t (*ticks_ms)(void);
} AppAPI;

typedef void (*NAppMainFn)(AppAPI *api);
typedef void (*NAppKeyFn)(AppAPI *api, int key);
typedef void (*NAppVoidFn)(AppAPI *api);

#endif
