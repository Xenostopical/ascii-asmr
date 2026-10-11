#include "rng.h"
#include "scene.h"

#include <stdlib.h>
#include <string.h>

#define MAX_DROPS 700
#define MAX_SEG 420
#define MAX_THUNDER 6

typedef struct {
    float x, y, vy;
    bool alive;
} sdrop_t;

typedef struct {
    int x, y;
    uint32_t glyph;
    float strength;
} seg_t;

typedef struct {
    float delay, vel, pan;
    bool active;
} thunder_t;

typedef struct {
    int cols, rows, next_d, nseg;
    sdrop_t d[MAX_DROPS];
    seg_t seg[MAX_SEG];
    thunder_t th[MAX_THUNDER];
    float spawn_acc, wind, bolt_age, flash, energy;
    double next_strike;
} storm_t;

static void *storm_create(int cols, int rows) {
    storm_t *s = xcalloc(1, sizeof *s);
    s->cols = cols;
    s->rows = rows;
    s->next_strike = 3.0;
    return s;
}

static void storm_resize(void *self, int cols, int rows) {
    storm_t *s = self;
    s->cols = cols;
    s->rows = rows;
    memset(s->d, 0, sizeof s->d);
    s->nseg = 0;
}

static void storm_destroy(void *self) { free(self); }

static void add_seg(storm_t *s, int x, int y, uint32_t g, float k) {
    if (s->nseg >= MAX_SEG) return;
    s->seg[s->nseg++] = (seg_t){x, y, g, k};
}

static void strike(storm_t *s, float x0) {
    s->nseg = 0;
    float x = x0;
    for (int y = 0; y < s->rows - 1 && s->nseg < MAX_SEG - 14; y++) {
        float nx = x + RR(-1.6f, 1.6f);
        float dx = nx - x;
        add_seg(s, (int)x, y, dx > 0.6f ? '\\' : (dx < -0.6f ? '/' : '|'), 1.0f);
        if (RC(0.09f)) {
            float bx = x;
            float dir = RC(0.5f) ? -1.0f : 1.0f;
            int len = 3 + RI(7);
            for (int k = 0; k < len; k++) {
                bx += dir * RR(0.6f, 1.4f);
                add_seg(s, (int)bx, y + 1 + k, dir > 0.0f ? '\\' : '/', 0.5f);
            }
        }
        x = nx;
    }
    s->bolt_age = 0.0f;
    for (int i = 0; i < MAX_THUNDER; i++) {
        if (s->th[i].active) continue;
        s->th[i] = (thunder_t){RR(0.2f, 2.5f), RR(0.5f, 1.0f), pan_for((int)x0, s->cols), true};
        break;
    }
}

static void storm_update(void *self, double dt, double t) {
    storm_t *s = self;
    float fdt = (float)dt, ft = (float)t;
    s->wind = 6.0f + noise2(ft * 0.07f, 5.5f) * 4.0f;
    s->spawn_acc += (float)s->cols * 1.6f * fdt;
    while (s->spawn_acc >= 1.0f) {
        s->spawn_acc -= 1.0f;
        sdrop_t *d = &s->d[s->next_d];
        s->next_d = (s->next_d + 1) % MAX_DROPS;
        d->x = RR(-10.0f, (float)s->cols + 10.0f);
        d->y = -RR(0.0f, 3.0f);
        d->vy = RR(35.0f, 55.0f);
        d->alive = true;
    }
    for (int i = 0; i < MAX_DROPS; i++) {
        sdrop_t *d = &s->d[i];
        if (!d->alive) continue;
        d->x += s->wind * 0.5f * fdt * 2.0f;
        d->y += d->vy * fdt;
        if (d->y >= (float)s->rows || d->x > (float)s->cols + 12.0f) d->alive = false;
    }

    s->bolt_age += fdt;
    if (s->nseg > 0) {
        float pulse = sinf(s->bolt_age * 45.0f) > -0.3f ? 1.0f : 0.55f;
        s->flash = expf(-s->bolt_age * 5.0f) * pulse;
        if (s->bolt_age > 1.2f) {
            s->nseg = 0;
            s->flash = 0.0f;
        }
    }
    if (t > s->next_strike) {
        s->next_strike = t + (double)RR(5.0f, 14.0f);
        strike(s, RR((float)s->cols * 0.1f, (float)s->cols * 0.9f));
    }
    for (int i = 0; i < MAX_THUNDER; i++) {
        thunder_t *th = &s->th[i];
        if (!th->active) continue;
        th->delay -= fdt;
        if (th->delay <= 0.0f) {
            audio_trigger(AUDIO_THUNDER, RF(), th->pan, th->vel);
            th->active = false;
        }
    }
    s->energy = 0.55f + 0.45f * s->flash;
}

static void storm_draw(void *self, canvas_t *c, double t) {
    storm_t *s = self;
    (void)t;
    uint32_t g = '\\';
    for (int i = 0; i < MAX_DROPS; i++) {
        const sdrop_t *d = &s->d[i];
        if (!d->alive) continue;
        int x = (int)d->x, y = (int)d->y;
        canvas_put(c, x, y, g, 0.5f, 0.2f);
        canvas_put(c, x - 1, y - 1, g, 0.3f, 0.2f);
    }
    if (s->flash > 0.04f) {
        for (int i = 0; i < s->nseg; i++) {
            const seg_t *sg = &s->seg[i];
            canvas_put(c, sg->x, sg->y, sg->glyph, s->flash * sg->strength, 0.95f);
            if (sg->strength > 0.9f && (i % 3) == 0) canvas_glow_point(c, (float)sg->x, (float)sg->y, 6.0f, 3.0f, 0.5f * s->flash);
        }
        for (int y = 0; y < s->rows; y++)
            for (int x = 0; x < s->cols; x++) canvas_glow_add(c, x, y, s->flash * 0.6f);
    }
    for (int x = 0; x < s->cols; x++) canvas_put(c, x, s->rows - 1, '_', 0.2f + 0.2f * s->flash, 0.4f);
}

static void storm_poke(void *self, int x, int y, int strength) {
    storm_t *s = self;
    (void)y;
    (void)strength;
    strike(s, (float)x);
}

static float storm_energy(void *self) { return ((storm_t *)self)->energy; }

const scene_def_t scene_storm_def = {
    .name = "storm",
    .blurb = "distant lightning and rolling thunder",
    .bed = BED_STORM,
    .create = storm_create,
    .resize = storm_resize,
    .update = storm_update,
    .draw = storm_draw,
    .poke = storm_poke,
    .energy = storm_energy,
    .destroy = storm_destroy,
};
