#include "rng.h"
#include "scene.h"

#include <stdlib.h>
#include <string.h>

typedef struct {
    float x, y, vy, sway, sway_speed;
    int size;
} flake_t;

typedef struct {
    int cols, rows, count;
    flake_t *flakes;
    float *pile;
    float wind, gust, flurry;
    double next_bell;
} snow_t;

static void reset_flake(snow_t *s, flake_t *f, bool anywhere) {
    f->x = RR(-4.0f, (float)s->cols + 4.0f);
    f->y = anywhere ? RR(0.0f, (float)s->rows) : -RR(0.0f, 3.0f);
    f->size = RI(3);
    f->vy = RR(2.5f, 5.0f) * (0.7f + 0.4f * (float)f->size);
    f->sway = RR(0.0f, TAU);
    f->sway_speed = RR(0.6f, 1.6f);
}

static void snow_alloc(snow_t *s, int cols, int rows) {
    free(s->flakes);
    free(s->pile);
    s->cols = cols;
    s->rows = rows;
    s->count = clampi(cols * rows / 18, 40, 900);
    s->flakes = xcalloc((size_t)s->count, sizeof(flake_t));
    s->pile = xcalloc((size_t)cols, sizeof(float));
    for (int i = 0; i < s->count; i++) reset_flake(s, &s->flakes[i], true);
}

static void *snow_create(int cols, int rows) {
    snow_t *s = xcalloc(1, sizeof *s);
    snow_alloc(s, cols, rows);
    s->next_bell = 5.0;
    return s;
}

static void snow_resize(void *self, int cols, int rows) { snow_alloc(self, cols, rows); }

static void snow_destroy(void *self) {
    snow_t *s = self;
    free(s->flakes);
    free(s->pile);
    free(s);
}

static void snow_update(void *self, double dt, double t) {
    snow_t *s = self;
    float fdt = (float)dt, ft = (float)t;
    s->wind = noise2(ft * 0.08f, 1.7f) * 4.0f + s->gust;
    s->gust *= expf(-fdt * 0.9f);
    s->flurry = 0.5f + 0.5f * noise2(ft * 0.05f, 6.2f);

    for (int i = 0; i < s->count; i++) {
        flake_t *f = &s->flakes[i];
        f->sway += f->sway_speed * fdt;
        f->y += f->vy * fdt;
        f->x += (s->wind * (0.5f + 0.25f * (float)f->size) + sinf(f->sway) * 1.6f) * fdt;
        int gx = clampi((int)f->x, 0, s->cols - 1);
        float ground = (float)s->rows - 1.0f - s->pile[gx];
        if (f->y >= ground) {
            s->pile[gx] = fminf(s->pile[gx] + 0.025f * (1.0f + (float)f->size), 5.0f);
            reset_flake(s, f, false);
        } else if (f->x < -6.0f || f->x > (float)s->cols + 6.0f) {
            reset_flake(s, f, false);
        }
    }

    for (int x = 1; x < s->cols - 1; x++) {
        float avg = 0.5f * (s->pile[x - 1] + s->pile[x + 1]);
        s->pile[x] += (avg - s->pile[x]) * fminf(1.0f, fdt * 0.8f);
    }
    float melt = expf(-fdt * 0.01f);
    for (int x = 0; x < s->cols; x++) s->pile[x] *= melt;

    if (t > s->next_bell) {
        s->next_bell = t + (double)RR(5.0f, 10.0f);
        audio_trigger(AUDIO_BELL, RR(0.0f, 0.5f), RR(-0.6f, 0.6f), 0.28f);
    }
}

static void snow_draw(void *self, canvas_t *c, double t) {
    snow_t *s = self;
    (void)t;
    static const char glyphs[3] = {'.', '*', '+'};
    for (int i = 0; i < s->count; i++) {
        const flake_t *f = &s->flakes[i];
        canvas_put(c, (int)floorf(f->x), (int)floorf(f->y), (uint32_t)glyphs[f->size], 0.35f + 0.22f * (float)f->size,
                   0.15f + 0.25f * (float)f->size);
    }
    for (int x = 0; x < s->cols; x++) {
        int h = (int)ceilf(s->pile[x]);
        for (int k = 0; k < h; k++) {
            int y = s->rows - 1 - k;
            float frac = (float)k / fmaxf(1.0f, (float)h);
            uint32_t g = k == h - 1 ? '~' : (frac > 0.5f ? '%' : '#');
            canvas_put(c, x, y, g, 0.5f + 0.35f * (1.0f - frac), 0.75f);
        }
        canvas_glow_add(c, x, s->rows - 1 - h, 0.18f);
    }
}

static void snow_poke(void *self, int x, int y, int strength) {
    snow_t *s = self;
    (void)y;
    s->gust += RR(-1.0f, 1.0f) * (3.0f + (float)strength);
    for (int i = 0; i < 18 + strength * 6; i++) {
        flake_t *f = &s->flakes[RI(s->count)];
        reset_flake(s, f, false);
        f->x = (float)x + RR(-5.0f, 5.0f);
    }
    audio_trigger(AUDIO_BELL, RR(0.2f, 0.8f), pan_for(x, s->cols), 0.35f);
}

static float snow_energy(void *self) { return 0.25f + 0.5f * ((snow_t *)self)->flurry; }

const scene_def_t scene_snow_def = {
    .name = "snow",
    .blurb = "snowfall settling into drifts",
    .bed = BED_SNOW,
    .create = snow_create,
    .resize = snow_resize,
    .update = snow_update,
    .draw = snow_draw,
    .poke = snow_poke,
    .energy = snow_energy,
    .destroy = snow_destroy,
};
