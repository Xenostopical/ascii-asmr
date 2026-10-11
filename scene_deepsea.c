#include "rng.h"
#include "scene.h"

#include <stdlib.h>
#include <string.h>

#define MAX_BUB 160
#define JELLIES 4
#define PLANK 140

typedef struct {
    float x, y, vy, wob, ws;
    int size;
    bool alive;
} bub_t;

typedef struct {
    float x, y, phase, drift, tint;
    int w;
} jelly_t;

typedef struct {
    float x, y, vy, seed;
} plank_t;

typedef struct {
    int cols, rows, next_b;
    bub_t b[MAX_BUB];
    jelly_t j[JELLIES];
    plank_t pl[PLANK];
    double next_swell;
} deep_t;

static void init_jelly(deep_t *d, jelly_t *j) {
    j->x = RR(8.0f, (float)d->cols - 8.0f);
    j->y = RR(0.0f, (float)d->rows);
    j->phase = RR(0.0f, TAU);
    j->drift = RR(-1.0f, 1.0f);
    j->tint = RR(0.3f, 0.9f);
    j->w = 3 + RI(3);
}

static void deep_setup(deep_t *d, int cols, int rows) {
    d->cols = cols;
    d->rows = rows;
    memset(d->b, 0, sizeof d->b);
    for (int i = 0; i < JELLIES; i++) init_jelly(d, &d->j[i]);
    for (int i = 0; i < PLANK; i++) {
        d->pl[i] = (plank_t){RR(0.0f, (float)cols), RR(0.0f, (float)rows), RR(0.3f, 1.2f), RR(0.0f, 100.0f)};
    }
}

static void *deep_create(int cols, int rows) {
    deep_t *d = xcalloc(1, sizeof *d);
    deep_setup(d, cols, rows);
    d->next_swell = 12.0;
    return d;
}

static void deep_resize(void *self, int cols, int rows) { deep_setup(self, cols, rows); }
static void deep_destroy(void *self) { free(self); }

static void spawn_bubble(deep_t *d, float x, float y, bool sound) {
    bub_t *b = &d->b[d->next_b];
    d->next_b = (d->next_b + 1) % MAX_BUB;
    b->x = x;
    b->y = y;
    b->vy = -RR(3.0f, 8.0f);
    b->wob = RR(0.0f, TAU);
    b->ws = RR(1.0f, 3.0f);
    b->size = RI(3);
    b->alive = true;
    if (sound) audio_trigger(AUDIO_BUBBLE, RF(), pan_for((int)x, d->cols), 0.3f + 0.2f * (float)b->size);
}

static void deep_update(void *self, double dt, double t) {
    deep_t *d = self;
    float fdt = (float)dt, ft = (float)t;
    if (RC(fdt * (float)d->cols * 0.05f)) spawn_bubble(d, RR(0.0f, (float)d->cols), (float)d->rows, RC(0.3f));
    for (int i = 0; i < MAX_BUB; i++) {
        bub_t *b = &d->b[i];
        if (!b->alive) continue;
        b->wob += b->ws * fdt;
        b->y += b->vy * fdt;
        b->x += sinf(b->wob) * 1.2f * fdt;
        if (b->y < -1.0f) b->alive = false;
    }
    for (int i = 0; i < JELLIES; i++) {
        jelly_t *j = &d->j[i];
        j->phase += (0.8f + 0.1f * (float)i) * fdt;
        j->y -= (0.5f + 0.5f * sinf(j->phase)) * fdt * 2.0f;
        j->x += (j->drift + noise2(j->phase * 0.3f, (float)i)) * fdt;
        if (j->y < -9.0f) {
            j->y = (float)d->rows + 8.0f;
            j->x = RR(8.0f, (float)d->cols - 8.0f);
        }
        if (j->x < 4.0f || j->x > (float)d->cols - 4.0f) j->drift = -j->drift;
    }
    for (int i = 0; i < PLANK; i++) {
        plank_t *p = &d->pl[i];
        p->y -= p->vy * fdt;
        p->x += noise2(p->seed, ft * 0.2f) * fdt;
        if (p->y < 0.0f) {
            p->y = (float)d->rows - 1.0f;
            p->x = RR(0.0f, (float)d->cols);
        }
    }
    if (t > d->next_swell) {
        d->next_swell = t + (double)RR(18.0f, 35.0f);
        audio_trigger(AUDIO_SWELL, RR(0.0f, 1.0f), RR(-0.8f, 0.8f), 0.35f);
    }
}

static void draw_jelly(canvas_t *c, const jelly_t *j) {
    int cx = (int)j->x, y = (int)j->y;
    int w = sinf(j->phase) > 0.0f ? j->w - 1 : j->w;
    for (int dx = -w + 1; dx <= w - 1; dx++) {
        canvas_put(c, cx + dx, y, '_', 0.75f, j->tint);
        canvas_put(c, cx + dx, y + 1, '.', 0.35f, j->tint);
    }
    canvas_put(c, cx - w, y + 1, '(', 0.8f, j->tint);
    canvas_put(c, cx + w, y + 1, ')', 0.8f, j->tint);
    for (int i = -w + 1; i <= w - 1; i += 2) {
        for (int k = 0; k < 6; k++) {
            int tx = cx + i + (int)lroundf(sinf(j->phase * 1.3f + (float)k * 0.6f + (float)i));
            uint32_t g = k < 2 ? '|' : (k < 4 ? '!' : ':');
            canvas_put(c, tx, y + 2 + k, g, 0.6f * (1.0f - (float)k / 7.0f), j->tint);
        }
    }
    canvas_glow_point(c, (float)cx, (float)y + 1.0f, (float)w * 2.2f, 4.0f, 0.6f);
}

static void deep_draw(void *self, canvas_t *c, double t) {
    deep_t *d = self;
    float ft = (float)t;
    for (int y = 0; y < d->rows; y++) {
        float fade = 1.0f - (float)y / (float)d->rows;
        for (int x = 0; x < d->cols; x++) {
            float ray = 0.5f + 0.5f * sinf(((float)x + (float)y * 0.4f) * 0.18f + ft * 0.2f);
            float v = (0.05f + 0.2f * ray) * fade;
            if (v > 0.07f) canvas_put_ramp(c, x, y, " .:", v * 2.0f, 0.3f);
        }
    }
    for (int i = 0; i < PLANK; i++) canvas_put(c, (int)d->pl[i].x, (int)d->pl[i].y, '.', 0.2f + 0.15f * sinf(ft + d->pl[i].seed), 0.5f);
    for (int i = 0; i < JELLIES; i++) draw_jelly(c, &d->j[i]);
    for (int i = 0; i < MAX_BUB; i++) {
        const bub_t *b = &d->b[i];
        if (!b->alive) continue;
        static const char g[3] = {'.', 'o', 'O'};
        canvas_put(c, (int)b->x, (int)b->y, (uint32_t)g[b->size], 0.5f + 0.15f * (float)b->size, 0.75f);
    }
}

static void deep_poke(void *self, int x, int y, int strength) {
    deep_t *d = self;
    for (int i = 0; i < 6 + strength * 2; i++) spawn_bubble(d, (float)x + RR(-3.0f, 3.0f), (float)y + RR(-1.0f, 2.0f), i < 3);
}

static float deep_energy(void *self) {
    (void)self;
    return 0.45f;
}

const scene_def_t scene_deepsea_def = {
    .name = "deepsea",
    .blurb = "jellyfish and bubbles in the deep",
    .bed = BED_DEEP,
    .create = deep_create,
    .resize = deep_resize,
    .update = deep_update,
    .draw = deep_draw,
    .poke = deep_poke,
    .energy = deep_energy,
    .destroy = deep_destroy,
};
