#include "rng.h"
#include "scene.h"

#include <stdlib.h>
#include <string.h>

/* A raked sand garden. An automatic rake draws grooves; dragging the mouse
 * rakes by hand. Rings are pre-raked around the stones. */

#define STONES 3

typedef struct {
    float x, y, rx, ry;
} stone_t;

typedef struct {
    int cols, rows;
    float *g, *base;
    stone_t st[STONES];
    float rx, ry, ux, uy, speed, energy;
    bool have_prev, have_user;
    double now, user_time, sound_clock;
} zen_t;

static bool in_stone(const zen_t *s, int x, int y) {
    for (int i = 0; i < STONES; i++) {
        float dx = ((float)x - s->st[i].x) / (s->st[i].rx + 0.5f), dy = ((float)y - s->st[i].y) / (s->st[i].ry + 0.5f);
        if (dx * dx + dy * dy <= 1.0f) return true;
    }
    return false;
}

static void zen_setup(zen_t *s, int cols, int rows) {
    free(s->g);
    free(s->base);
    s->cols = cols;
    s->rows = rows;
    s->g = xcalloc((size_t)cols * (size_t)rows, sizeof(float));
    s->base = xcalloc((size_t)cols * (size_t)rows, sizeof(float));
    for (int i = 0; i < STONES; i++) {
        float sz = RR(2.0f, 4.0f);
        s->st[i] = (stone_t){RR((float)cols * 0.15f, (float)cols * 0.85f), RR((float)rows * 0.25f, (float)rows * 0.75f), sz, sz * 0.5f};
    }
    for (int y = 0; y < rows; y++) {
        for (int x = 0; x < cols; x++) {
            float best = 0.0f;
            for (int i = 0; i < STONES; i++) {
                float dx = (float)x - s->st[i].x, dy = ((float)y - s->st[i].y) * 2.0f;
                float d = sqrtf(dx * dx + dy * dy) - s->st[i].rx;
                if (d > 1.0f && d < 10.0f) best = fmaxf(best, 0.5f + 0.5f * cosf(d * 1.6f));
            }
            s->base[y * cols + x] = best;
        }
    }
    s->have_prev = false;
}

static void *zen_create(int cols, int rows) {
    zen_t *s = xcalloc(1, sizeof *s);
    zen_setup(s, cols, rows);
    return s;
}

static void zen_resize(void *self, int cols, int rows) { zen_setup(self, cols, rows); }

static void zen_destroy(void *self) {
    zen_t *s = self;
    free(s->g);
    free(s->base);
    free(s);
}

static void stamp(zen_t *s, int x, int y) {
    for (int k = -2; k <= 2; k++) {
        int yy = y + k;
        if (x < 0 || yy < 0 || x >= s->cols || yy >= s->rows || in_stone(s, x, yy)) continue;
        s->g[yy * s->cols + x] = 1.0f;
    }
}

static void stamp_line(zen_t *s, float x0, float y0, float x1, float y1) {
    int steps = (int)fmaxf(fabsf(x1 - x0), fabsf(y1 - y0)) + 1;
    for (int i = 0; i <= steps; i++) {
        float f = (float)i / (float)steps;
        stamp(s, (int)lroundf(lerpf(x0, x1, f)), (int)lroundf(lerpf(y0, y1, f)));
    }
}

static void zen_update(void *self, double dt, double t) {
    zen_t *s = self;
    float fdt = (float)dt, ft = (float)t;
    s->now = t;
    size_t n = (size_t)s->cols * (size_t)s->rows;
    float decay = expf(-fdt * 0.015f);
    for (size_t i = 0; i < n; i++) s->g[i] *= decay;

    float nx = (float)s->cols * (0.5f + 0.42f * sinf(ft * 0.17f));
    float ny = (float)s->rows * (0.5f + 0.32f * sinf(ft * 0.23f + 1.3f) * cosf(ft * 0.05f));
    if (s->have_prev) {
        stamp_line(s, s->rx, s->ry, nx, ny);
        float dx = nx - s->rx, dy = (ny - s->ry) * 2.0f;
        s->speed = sqrtf(dx * dx + dy * dy) / fmaxf(fdt, 1e-4f);
    }
    s->rx = nx;
    s->ry = ny;
    s->have_prev = true;
    if (t - s->sound_clock > 0.14 && s->speed > 0.5f) {
        s->sound_clock = t;
        audio_trigger(AUDIO_SCRAPE, saturatef(0.3f + s->speed * 0.02f), pan_for((int)nx, s->cols), 0.18f);
    }
    s->energy = clampf(s->speed * 0.04f, 0.2f, 0.8f);
}

static void zen_draw(void *self, canvas_t *c, double t) {
    zen_t *s = self;
    (void)t;
    for (int y = 0; y < s->rows; y++) {
        for (int x = 0; x < s->cols; x++) {
            if (in_stone(s, x, y)) continue;
            float v = fmaxf(s->base[y * s->cols + x] * 0.7f, s->g[y * s->cols + x]);
            if (v > 0.12f) {
                canvas_put_ramp(c, x, y, " .-~=", 0.2f + 0.7f * v, 0.2f + 0.3f * v);
            } else if (hash_float((uint32_t)(y * s->cols + x)) < 0.06f) {
                canvas_put(c, x, y, '.', 0.14f, 0.1f);
            }
        }
    }
    for (int i = 0; i < STONES; i++) {
        const stone_t *st = &s->st[i];
        for (int dy = -(int)st->ry - 1; dy <= (int)st->ry + 1; dy++) {
            for (int dx = -(int)st->rx - 1; dx <= (int)st->rx + 1; dx++) {
                float nx = (float)dx / (st->rx + 0.5f), ny = (float)dy / (st->ry + 0.5f);
                float d = nx * nx + ny * ny;
                if (d > 1.0f) continue;
                canvas_put(c, (int)st->x + dx, (int)st->y + dy, d < 0.4f ? '@' : 'O', 0.85f - 0.2f * d, 0.1f);
            }
        }
        canvas_put(c, (int)st->x + (int)st->rx + 2, (int)st->y + (int)st->ry + 1, '.', 0.2f, 0.1f);
    }
    canvas_put(c, (int)s->rx, (int)s->ry, '+', 0.8f, 0.9f);
    canvas_glow_point(c, s->rx, s->ry, 5.0f, 2.5f, 0.5f);
}

static void zen_poke(void *self, int x, int y, int strength) {
    zen_t *s = self;
    if (strength >= 3) s->have_user = false;
    if (s->have_user && s->now - s->user_time < 0.4)
        stamp_line(s, s->ux, s->uy, (float)x, (float)y);
    else
        stamp(s, x, y);
    s->ux = (float)x;
    s->uy = (float)y;
    s->user_time = s->now;
    s->have_user = true;
    audio_trigger(AUDIO_SCRAPE, RR(0.2f, 0.7f), pan_for(x, s->cols), 0.3f);
}

static float zen_energy(void *self) { return ((zen_t *)self)->energy; }

const scene_def_t scene_zen_def = {
    .name = "zen",
    .blurb = "a sand garden raked in slow lines",
    .bed = BED_ZEN,
    .create = zen_create,
    .resize = zen_resize,
    .update = zen_update,
    .draw = zen_draw,
    .poke = zen_poke,
    .energy = zen_energy,
    .destroy = zen_destroy,
};
