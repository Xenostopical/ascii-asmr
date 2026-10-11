#include "rng.h"
#include "scene.h"

#include <stdlib.h>
#include <string.h>

/* Wind chimes as coupled pendulums. Neighbouring tubes that meet with enough
 * relative speed swap momentum and ring. */

#define TUBES 7
#define SPACING 5

typedef struct {
    float a, w, len, glow, cooldown;
} tube_t;

typedef struct {
    int cols, rows;
    tube_t t[TUBES];
    float gust, wind, clapper, energy;
} chimes_t;

static void chimes_setup(chimes_t *s, int cols, int rows) {
    s->cols = cols;
    s->rows = rows;
    float sc = fminf(1.0f, (float)rows * 0.55f / 12.0f);
    for (int i = 0; i < TUBES; i++) {
        s->t[i].a = 0.0f;
        s->t[i].w = 0.0f;
        s->t[i].len = fmaxf(1.0f, (3.0f + (float)(TUBES - 1 - i) * 0.8f) * sc);
        s->t[i].glow = 0.0f;
        s->t[i].cooldown = 0.0f;
    }
}

static void *chimes_create(int cols, int rows) {
    chimes_t *s = xcalloc(1, sizeof *s);
    chimes_setup(s, cols, rows);
    return s;
}

static void chimes_resize(void *self, int cols, int rows) { chimes_setup(self, cols, rows); }
static void chimes_destroy(void *self) { free(self); }

static void ring_pair(chimes_t *s, int i, float vel) {
    for (int k = 0; k < 2; k++) {
        int idx = i + k;
        if (s->t[idx].cooldown > 0.0f) continue;
        s->t[idx].cooldown = 0.25f;
        s->t[idx].glow = 1.0f;
        float pitch = ((float)idx + 0.5f) / (float)TUBES;
        audio_trigger(AUDIO_CHIME, pitch, -0.6f + 1.2f * pitch, clampf(vel, 0.1f, 0.9f) * 0.5f);
    }
}

static void chimes_update(void *self, double dt, double t) {
    chimes_t *s = self;
    float fdt = (float)dt, ft = (float)t;
    s->wind = noise2(ft * 0.3f, 7.7f) * 1.2f + s->gust;
    s->gust *= expf(-fdt * 0.7f);
    float motion = 0.0f;
    for (int i = 0; i < TUBES; i++) {
        tube_t *u = &s->t[i];
        float torque = (noise2(ft * 0.5f + (float)i * 0.7f, 3.3f) * 0.9f + s->gust) * 2.0f;
        float k = 14.0f - u->len * 0.6f;
        u->w += (-k * u->a - 0.25f * u->w + torque) * fdt;
        u->a = clampf(u->a + u->w * fdt, -1.0f, 1.0f);
        u->glow *= expf(-fdt * 3.0f);
        u->cooldown = fmaxf(0.0f, u->cooldown - fdt);
        motion += fabsf(u->w);
    }
    for (int i = 0; i < TUBES - 1; i++) {
        tube_t *a = &s->t[i], *b = &s->t[i + 1];
        float rel = a->w - b->w;
        if (fabsf(a->a - b->a) < 0.06f && fabsf(rel) > 0.5f) {
            float tmp = a->w;
            a->w = b->w;
            b->w = tmp;
            ring_pair(s, i, fabsf(rel) * 0.5f);
        }
    }
    s->clapper += (s->wind * 0.5f - s->clapper) * fminf(1.0f, fdt * 2.0f);
    s->energy = clampf(motion / 6.0f, 0.15f, 1.0f);
}

static void chimes_draw(void *self, canvas_t *c, double t) {
    chimes_t *s = self;
    (void)t;
    int cx = s->cols / 2;
    int x0 = cx - (TUBES - 1) * SPACING / 2;
    for (int x = x0 - 3; x <= x0 + (TUBES - 1) * SPACING + 3; x++) canvas_put(c, x, 1, '=', 0.55f, 0.1f);
    for (int i = 0; i < TUBES; i++) {
        const tube_t *u = &s->t[i];
        float px = (float)(x0 + i * SPACING), sa = sinf(u->a), ca = cosf(u->a);
        for (int r = 0; r <= 2; r++) canvas_put(c, (int)lroundf(px + sa * (float)r * 2.0f), 2 + (int)lroundf(ca * (float)r), '|', 0.35f, 0.2f);
        for (int k = 0; k < (int)u->len; k++) {
            float r = 3.0f + (float)k;
            int x = (int)lroundf(px + sa * r * 2.0f), y = 2 + (int)lroundf(ca * r);
            canvas_put(c, x, y, k % 2 ? 'H' : '#', 0.55f + 0.45f * u->glow, 0.35f + 0.5f * u->glow);
            if (u->glow > 0.1f && k == (int)u->len - 1) canvas_glow_point(c, (float)x, (float)y, 7.0f, 3.5f, 0.9f * u->glow);
        }
    }
    float maxlen = 0.0f;
    for (int i = 0; i < TUBES; i++) maxlen = fmaxf(maxlen, s->t[i].len);
    float r = 3.0f + maxlen * 0.5f;
    int sx = cx + (int)lroundf(sinf(s->clapper) * r * 2.0f), sy = 2 + (int)lroundf(cosf(s->clapper) * r);
    canvas_put(c, sx, sy, 'O', 0.7f, 0.9f);
    canvas_put(c, cx, 2 + (int)(r * 0.5f), '|', 0.3f, 0.2f);
}

static void chimes_poke(void *self, int x, int y, int strength) {
    chimes_t *s = self;
    (void)y;
    float dir = x < s->cols / 2 ? 1.0f : -1.0f;
    s->gust += dir * RR(2.0f, 4.0f) * (0.5f + 0.25f * (float)strength);
}

static float chimes_energy(void *self) { return ((chimes_t *)self)->energy; }

const scene_def_t scene_chimes_def = {
    .name = "chimes",
    .blurb = "wind chimes swaying in a gentle gust",
    .bed = BED_CHIMES,
    .create = chimes_create,
    .resize = chimes_resize,
    .update = chimes_update,
    .draw = chimes_draw,
    .poke = chimes_poke,
    .energy = chimes_energy,
    .destroy = chimes_destroy,
};
