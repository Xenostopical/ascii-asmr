#include "rng.h"
#include "scene.h"

#include <stdlib.h>
#include <string.h>

/* Rain on a fogged window. Drops grow, start to slide, wipe the fog as they
 * go, and merge when they touch. */

#define MAX_DROPS 160
#define BOKEH 7

typedef struct {
    float x, y, mass, vy, threshold;
    bool alive, sliding;
} gdrop_t;

typedef struct {
    int cols, rows;
    gdrop_t d[MAX_DROPS];
    float *fog;
    float spawn_acc, activity;
    float bx[BOKEH], by[BOKEH], bph[BOKEH];
    double last_sound;
} window_t;

static void spawn_drop(window_t *s, float x, float y, float mass) {
    for (int i = 0; i < MAX_DROPS; i++) {
        gdrop_t *d = &s->d[i];
        if (d->alive) continue;
        d->x = x;
        d->y = y;
        d->mass = mass;
        d->vy = 0.0f;
        d->threshold = RR(0.9f, 1.4f);
        d->alive = true;
        d->sliding = false;
        return;
    }
}

static void window_setup(window_t *s, int cols, int rows) {
    free(s->fog);
    s->cols = cols;
    s->rows = rows;
    s->fog = xcalloc((size_t)cols * (size_t)rows, sizeof(float));
    for (size_t i = 0; i < (size_t)cols * (size_t)rows; i++) s->fog[i] = 1.0f;
    memset(s->d, 0, sizeof s->d);
    for (int i = 0; i < BOKEH; i++) {
        s->bx[i] = RR(0.0f, (float)cols);
        s->by[i] = RR(0.0f, (float)rows * 0.7f);
        s->bph[i] = RR(0.0f, TAU);
    }
    for (int i = 0; i < cols / 4; i++) spawn_drop(s, RR(0.0f, (float)cols), RR(0.0f, (float)rows), RR(0.2f, 0.9f));
}

static void *window_create(int cols, int rows) {
    window_t *s = xcalloc(1, sizeof *s);
    window_setup(s, cols, rows);
    return s;
}

static void window_resize(void *self, int cols, int rows) { window_setup(self, cols, rows); }

static void window_destroy(void *self) {
    window_t *s = self;
    free(s->fog);
    free(s);
}

static void wipe(window_t *s, int x, int y, float amount) {
    if (x < 0 || y < 0 || x >= s->cols || y >= s->rows) return;
    float *f = &s->fog[y * s->cols + x];
    if (*f > amount) *f = amount;
}

static void drip_sound(window_t *s, double t, float pitch, int x, float vel) {
    if (t - s->last_sound < 0.12) return;
    s->last_sound = t;
    audio_trigger(AUDIO_DRIP, pitch, pan_for(x, s->cols), vel);
}

static void window_update(void *self, double dt, double t) {
    window_t *s = self;
    float fdt = (float)dt, ft = (float)t;
    size_t n = (size_t)s->cols * (size_t)s->rows;
    for (size_t i = 0; i < n; i++) s->fog[i] = fminf(1.0f, s->fog[i] + fdt * 0.02f);

    s->spawn_acc += fdt * (float)s->cols * 0.06f;
    while (s->spawn_acc >= 1.0f) {
        s->spawn_acc -= 1.0f;
        spawn_drop(s, RR(0.0f, (float)s->cols), RR(0.0f, (float)s->rows * 0.8f), RR(0.2f, 0.7f));
    }

    int sliding = 0;
    for (int i = 0; i < MAX_DROPS; i++) {
        gdrop_t *d = &s->d[i];
        if (!d->alive) continue;
        if (!d->sliding) {
            d->mass += fdt * 0.05f;
            if (d->mass > d->threshold) {
                d->sliding = true;
                drip_sound(s, t, 0.2f + 0.5f * RF(), (int)d->x, 0.22f);
            }
            continue;
        }
        sliding++;
        float stutter = 0.3f + 0.7f * fmaxf(0.0f, noise2(d->x, ft * 0.8f + d->y * 0.1f) + 0.3f);
        d->vy = (2.0f + 6.0f * powf(d->mass, 1.5f)) * stutter;
        d->y += d->vy * fdt;
        d->x += noise2(d->y * 0.2f, d->x) * 0.4f * fdt;
        d->mass = fmaxf(0.35f, d->mass - fdt * 0.01f);
        int cx = (int)d->x, cy = (int)d->y;
        wipe(s, cx, cy, 0.0f);
        wipe(s, cx - 1, cy, 0.4f);
        wipe(s, cx + 1, cy, 0.4f);
        if (d->y >= (float)s->rows) {
            d->alive = false;
            continue;
        }
        for (int j = 0; j < MAX_DROPS; j++) {
            gdrop_t *o = &s->d[j];
            if (j == i || !o->alive) continue;
            if (fabsf(o->x - d->x) < 1.0f && fabsf(o->y - d->y) < 1.2f) {
                d->mass += o->mass * 0.8f;
                o->alive = false;
                drip_sound(s, t, clampf(0.3f + d->mass * 0.3f, 0.0f, 1.0f), (int)d->x, 0.3f);
            }
        }
    }
    s->activity = clampf((float)sliding / 10.0f, 0.1f, 1.0f);
}

static void window_draw(void *self, canvas_t *c, double t) {
    window_t *s = self;
    float ft = (float)t;
    for (int i = 0; i < BOKEH; i++) {
        float pulse = 0.5f + 0.5f * sinf(ft * 0.3f + s->bph[i]);
        canvas_glow_point(c, s->bx[i], s->by[i], 9.0f, 4.5f, 0.35f + 0.2f * pulse);
    }
    for (int y = 0; y < s->rows; y++) {
        for (int x = 0; x < s->cols; x++) {
            float f = s->fog[y * s->cols + x];
            if (f > 0.05f) canvas_put(c, x, y, f > 0.7f ? ':' : '.', 0.1f + 0.18f * f, 0.3f);
        }
    }
    for (int i = 0; i < MAX_DROPS; i++) {
        const gdrop_t *d = &s->d[i];
        if (!d->alive) continue;
        uint32_t g = d->mass < 0.5f ? '.' : (d->mass < 0.9f ? 'o' : 'O');
        canvas_put(c, (int)d->x, (int)d->y, g, 0.55f + 0.4f * saturatef(d->mass), 0.7f);
        if (d->mass > 0.6f) canvas_glow_point(c, d->x, d->y, 3.0f, 1.5f, 0.25f);
    }
}

static void window_poke(void *self, int x, int y, int strength) {
    window_t *s = self;
    int rx = 3 + strength, ry = 1 + strength / 2;
    for (int dy = -ry; dy <= ry; dy++) {
        for (int dx = -rx; dx <= rx; dx++) {
            float nx = (float)dx / (float)rx, ny = (float)dy / (float)ry;
            if (nx * nx + ny * ny <= 1.0f) wipe(s, x + dx, y + dy, 0.0f);
        }
    }
    spawn_drop(s, (float)x, (float)y, 0.8f);
    audio_trigger(AUDIO_DRIP, RR(0.3f, 0.9f), pan_for(x, s->cols), 0.4f);
}

static float window_energy(void *self) { return ((window_t *)self)->activity; }

const scene_def_t scene_window_def = {
    .name = "window",
    .blurb = "rain running down a fogged window",
    .bed = BED_WINDOW,
    .create = window_create,
    .resize = window_resize,
    .update = window_update,
    .draw = window_draw,
    .poke = window_poke,
    .energy = window_energy,
    .destroy = window_destroy,
};
