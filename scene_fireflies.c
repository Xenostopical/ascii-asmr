#include "rng.h"
#include "scene.h"

#include <stdlib.h>
#include <string.h>

/* Fireflies drift on curl noise and flash on a phase clock. A mean-field
 * Kuramoto coupling slowly pulls their flashes into sync. */

#define MAX_FLIES 90

typedef struct {
    float x, y, phase, omega, flash, seed;
    bool active;
} fly_t;

typedef struct {
    int cols, rows, count;
    fly_t f[MAX_FLIES];
    float coupling, order;
    double last_chime;
} flies_t;

static void init_fly(flies_t *s, fly_t *f) {
    f->x = RR(0.0f, (float)s->cols);
    f->y = RR((float)s->rows * 0.25f, (float)s->rows * 0.9f);
    f->phase = RF();
    f->omega = RR(0.20f, 0.32f);
    f->flash = 0.0f;
    f->seed = RR(0.0f, 100.0f);
    f->active = true;
}

static void flies_setup(flies_t *s, int cols, int rows) {
    s->cols = cols;
    s->rows = rows;
    s->count = clampi(cols * rows / 160, 12, 70);
    memset(s->f, 0, sizeof s->f);
    for (int i = 0; i < s->count; i++) init_fly(s, &s->f[i]);
}

static void *flies_create(int cols, int rows) {
    flies_t *s = xcalloc(1, sizeof *s);
    s->coupling = 0.18f;
    flies_setup(s, cols, rows);
    return s;
}

static void flies_resize(void *self, int cols, int rows) { flies_setup(self, cols, rows); }
static void flies_destroy(void *self) { free(self); }

static void flies_update(void *self, double dt, double t) {
    flies_t *s = self;
    float fdt = (float)dt, ft = (float)t;

    float cx = 0.0f, sx = 0.0f;
    int active = 0;
    for (int i = 0; i < MAX_FLIES; i++) {
        if (!s->f[i].active) continue;
        cx += cosf(TAU * s->f[i].phase);
        sx += sinf(TAU * s->f[i].phase);
        active++;
    }
    if (active == 0) return;
    cx /= (float)active;
    sx /= (float)active;
    s->order = sqrtf(cx * cx + sx * sx);
    float psi = atan2f(sx, cx);

    for (int i = 0; i < MAX_FLIES; i++) {
        fly_t *f = &s->f[i];
        if (!f->active) continue;
        float pull = s->coupling * s->order * sinf(psi - TAU * f->phase) / TAU;
        f->phase += (f->omega + pull) * fdt;
        if (f->phase >= 1.0f) {
            f->phase -= 1.0f;
            f->flash = 1.0f;
            if (t - s->last_chime > 0.3 && RC(0.5f)) {
                s->last_chime = t;
                audio_trigger(AUDIO_CHIME, 1.0f - f->y / (float)s->rows, pan_for((int)f->x, s->cols), 0.22f);
            }
        }
        f->flash *= expf(-fdt * 3.0f);
        f->x += noise2(f->seed, ft * 0.25f) * 4.0f * fdt;
        f->y += noise2(f->seed + 50.0f, ft * 0.25f) * 2.0f * fdt;
        if (f->x < 0.0f) f->x += (float)s->cols;
        if (f->x >= (float)s->cols) f->x -= (float)s->cols;
        f->y = clampf(f->y, (float)s->rows * 0.15f, (float)s->rows - 4.0f);
    }
}

static void flies_draw(void *self, canvas_t *c, double t) {
    flies_t *s = self;
    float ft = (float)t;

    for (int x = 0; x < s->cols; x++) {
        float wind = noise2((float)x * 0.08f, ft * 0.5f);
        int h = 1 + (int)(hash_float((uint32_t)x) * 3.0f);
        for (int k = 0; k < h; k++) {
            int y = s->rows - 1 - k;
            uint32_t g = '|';
            if (k > 0) g = wind > 0.25f ? '/' : (wind < -0.25f ? '\\' : '|');
            canvas_put(c, x, y, g, 0.22f + 0.06f * (float)k, 0.1f);
        }
    }

    for (int i = 0; i < MAX_FLIES; i++) {
        const fly_t *f = &s->f[i];
        if (!f->active) continue;
        float v = 0.22f + 0.78f * f->flash;
        uint32_t g = f->flash > 0.6f ? '*' : (f->flash > 0.2f ? 'o' : '.');
        canvas_put(c, (int)f->x, (int)f->y, g, v, 0.92f);
        if (f->flash > 0.05f) canvas_glow_point(c, f->x, f->y, 5.0f, 2.5f, 0.9f * f->flash);
    }
}

static void flies_poke(void *self, int x, int y, int strength) {
    flies_t *s = self;
    if (s->count < MAX_FLIES) {
        fly_t *f = &s->f[s->count++];
        init_fly(s, f);
        f->x = (float)x;
        f->y = (float)y;
    }
    for (int i = 0; i < MAX_FLIES; i++) {
        fly_t *f = &s->f[i];
        if (!f->active) continue;
        float dx = f->x - (float)x, dy = (f->y - (float)y) * 2.0f;
        if (dx * dx + dy * dy < 100.0f * (float)strength) {
            f->phase = 0.97f;
        }
    }
    audio_trigger(AUDIO_PLINK, RR(0.4f, 1.0f), pan_for(x, s->cols), 0.4f);
}

static float flies_energy(void *self) { return 0.3f + 0.6f * ((flies_t *)self)->order; }

const scene_def_t scene_fireflies_def = {
    .name = "fireflies",
    .blurb = "slowly synchronising lights over tall grass",
    .bed = BED_FIREFLY,
    .create = flies_create,
    .resize = flies_resize,
    .update = flies_update,
    .draw = flies_draw,
    .poke = flies_poke,
    .energy = flies_energy,
    .destroy = flies_destroy,
};
