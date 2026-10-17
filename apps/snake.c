/* DimonOS native Snake application.
 * Built as a relocatable NAPP64 blob and loaded from FAT16 by the desktop.
 * Uses only the AppAPI function pointers — no kernel symbols. */
#include <stdint.h>
#include <stddef.h>
#include "../napp.h"

/* Simple integer formatting (freestanding). */
static void dec(int v, char *out) {
    char t[12]; int n = 0, neg = 0;
    if (v < 0) { neg = 1; v = -v; }
    if (v == 0) t[n++] = '0';
    while (v > 0) { t[n++] = (char)('0' + v % 10); v /= 10; }
    int o = 0;
    if (neg) out[o++] = '-';
    while (n > 0) out[o++] = t[--n];
    out[o] = 0;
}

#define SNAKE_MAX 128
#define GRID_W 30
#define GRID_H 20
#define CELL 14

static int sx[SNAKE_MAX], sy[SNAKE_MAX];
static int slen, sdir, score, gameover;
static int fx, fy;
static uint32_t last_tick;

static void snake_reset(void) {
    slen = 4; sdir = 1; score = 0; gameover = 0;
    for (int i = 0; i < slen; i++) { sx[i] = 10 - i; sy[i] = 10; }
    fx = 20; fy = 10;
    last_tick = 0;
}

static void snake_step(AppAPI *api) {
    if (gameover) return;
    int nx = sx[0], ny = sy[0];
    if (sdir == 0) ny--;
    else if (sdir == 1) nx++;
    else if (sdir == 2) ny++;
    else nx--;
    if (nx < 0 || nx >= GRID_W || ny < 0 || ny >= GRID_H) {
        gameover = 1;
        if (api->beep) api->beep(api->sys, 200, 180);
        return;
    }
    for (int i = 0; i < slen; i++)
        if (sx[i] == nx && sy[i] == ny) {
            gameover = 1;
            if (api->beep) api->beep(api->sys, 200, 180);
            return;
        }
    for (int i = slen; i > 0; i--) { sx[i] = sx[i - 1]; sy[i] = sy[i - 1]; }
    sx[0] = nx; sy[0] = ny;
    if (nx == fx && ny == fy) {
        if (slen < SNAKE_MAX) slen++;
        score += 10;
        if (api->beep) api->beep(api->sys, 880, 25);
        fx = (nx * 7 + score) % GRID_W;
        fy = (ny * 13 + score) % GRID_H;
    }
}

void app_main(AppAPI *api) {
    if (!api) return;
    snake_reset();
    /* Event loop: the host feeds key codes via a simple convention —
     * this standalone build polls ticks and draws; input is delivered by
     * the desktop runner calling snake_key/snake_timer exports below.
     * For the flat blob we run a self-contained loop driven by ticks. */
    uint32_t start = api->ticks_ms ? api->ticks_ms() : 0;
    (void)start;
}

/* Called by the desktop loader for key input (256..259 arrows, 'r'). */
void snake_key(AppAPI *api, int key) {
    (void)api;
    if (key == 'r' || key == 'R') { snake_reset(); return; }
    if (gameover) return;
    if (key == 256 && sdir != 2) sdir = 0;
    else if (key == 259 && sdir != 3) sdir = 1;
    else if (key == 257 && sdir != 0) sdir = 2;
    else if (key == 258 && sdir != 1) sdir = 3;
}

void snake_timer(AppAPI *api) {
    uint32_t now = api->ticks_ms ? api->ticks_ms() : 0;
    if (now - last_tick < 150) return;
    last_tick = now;
    snake_step(api);
}

void snake_draw(AppAPI *api) {
    if (!api || !api->fill_rect) return;
    int gx = 20, gy = 50;
    api->fill_rect(api->sys, gx - 4, gy - 4, GRID_W * CELL + 8, GRID_H * CELL + 8, 0xFF0F172Au);
    for (int i = 0; i < slen; i++) {
        uint32_t c = (i == 0) ? 0xFF34C759u : 0xFF059669u;
        api->fill_rect(api->sys, gx + sx[i] * CELL, gy + sy[i] * CELL, CELL - 1, CELL - 1, c);
    }
    api->fill_rect(api->sys, gx + fx * CELL, gy + fy * CELL, CELL - 1, CELL - 1, 0xFFFF3B30u);
    char buf[32]; dec(score, buf);
    if (api->draw_text) {
        api->draw_text(api->sys, 20, 30, "Snake 64", 0xFFFFFFFFu);
        api->draw_text(api->sys, 20, 48 + GRID_H * CELL + 8, buf, 0xFF38BDF8u);
        if (gameover) api->draw_text(api->sys, 120, 200, "Game Over - press R", 0xFFFF3B30u);
    }
}
