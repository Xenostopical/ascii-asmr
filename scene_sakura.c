#include "rng.h"
#include "scene.h"

#include <stdlib.h>
#include <string.h>

#define MAX_PETALS 260

typedef struct {
    float x, y, vy, ph, ps, size;
} petal_t;

typedef struct {
    int cols, rows, count;
    petal_t p[MAX_PETALS];
    float *ground;
    float gust, wind;
    double next_chime;
} sakura_t;

static void reset_petal(sakura_t *s, petal_t *p, bool anywhere) {
    p->x = RR(-4.0f, (float)s->cols + 4.0f);
    p->y = anywhere ? RR(0.0f, (float)s->rows) : -RR(0.0f, 4.0f);
    p->vy = RR(1.5f, 3.5f);
    p->ph = RR(0.0f, TAU);
    p->ps = RR(1.0f, 3.0f);
    p->size = RF();
}

static void sakura_setup(sakura_t *s, int cols, int rows) {
    free(s->ground);
    s->cols = cols;
    s->rows = rows;
    s->count = clampi(cols * rows / 24, 30, MAX_PETALS);
    s->ground = xcalloc((size_t)cols, sizeof(float));
    for (int i = 0; i < s->count; i++) reset_petal(s, &s->p[i], true);
}

static void *sakura_create(int cols, int rows) {
    sakura_t *s = xcalloc(1, sizeof *s);
    sakura_setup(s, cols, rows);
    s->next_chime = 4.0;
    return s;
}

static void sakura_resize(void *self, int cols, int rows) { sakura_setup(self, cols, rows); }

static void sakura_destroy(void *self) {
    sakura_t *s = self;
    free(s->ground);
    free(s);
}

static void sakura_update(void *self, double dt, double t) {
    sakura_t *s = self;
    float fdt = (float)dt, ft = (float)t;
    s->wind = noise2(ft * 0.07f, 2.2f) * 3.0f + s->gust;
    s->gust *= expf(-fdt * 0.8f);
    for (int i = 0; i < s->count; i++) {
        petal_t *p = &s->p[i];
        float vx, vy;
        curl2(p->x * 0.03f, p->y * 0.06f, ft * 0.1f, &vx, &vy);
        p->ph += p->ps * fdt;
        p->x += (s->wind * 0.7f + vx * 1.5f + sinf(p->ph) * 1.2f) * fdt;
        p->y += (p->vy + vy * 0.4f) * fdt;
        if (p->x < -6.0f) p->x += (float)s->cols + 12.0f;
        if (p->x > (float)s->cols + 6.0f) p->x -= (float)s->cols + 12.0f;
        int gx = clampi((int)p->x, 0, s->cols - 1);
        if (p->y >= (float)s->rows - 1.0f) {
            s->ground[gx] = fminf(s->ground[gx] + 0.12f, 2.0f);
            reset_petal(s, p, false);
        }
    }
    if (t > s->next_chime) {
        s->next_chime = t + (double)RR(4.0f, 9.0f);
        audio_trigger(AUDIO_CHIME, RR(0.2f, 0.9f), RR(-0.6f, 0.6f), 0.18f);
    }
}

static void sakura_draw(void *self, canvas_t *c, double t) {
    sakura_t *s = self;
    float ft = (float)t;
    int span = (int)((float)s->cols * 0.55f);
    for (int x = 0; x < span; x++) {
        int y = (int)((float)s->rows * 0.22f - (float)x * 0.12f + 2.0f * noise2((float)x * 0.08f, 1.1f));
        canvas_put(c, x, y, '=', 0.3f, 0.05f);
        canvas_put(c, x, y + 1, '#', 0.25f, 0.05f);
        if (x % 5 == 0) {
            float sway = noise2((float)x * 0.2f, ft * 0.4f);
            static const char bl[3] = {'@', '*', 'o'};
            canvas_put(c, x + (int)roundf(sway), y - 1, (uint32_t)bl[(x / 5) % 3], 0.55f + 0.2f * sway, 0.7f);
            canvas_glow_point(c, (float)x, (float)(y - 1), 4.0f, 2.0f, 0.3f);
        }
    }
    static const char shapes[4] = {'*', '@', 'o', '\''};
    for (int i = 0; i < s->count; i++) {
        const petal_t *p = &s->p[i];
        int idx = (int)(fractf(p->ph / TAU) * 4.0f) & 3;
        canvas_put(c, (int)floorf(p->x), (int)floorf(p->y), (uint32_t)shapes[idx], 0.4f + 0.5f * p->size, 0.3f + 0.5f * p->size);
    }
    for (int x = 0; x < s->cols; x++) {
        float g = s->ground[x];
        if (g > 0.2f) canvas_put(c, x, s->rows - 1, g > 1.0f ? ',' : '.', 0.35f + 0.2f * g, 0.65f);
        if (g > 1.6f) canvas_put(c, x, s->rows - 2, '.', 0.3f, 0.65f);
    }
}

static void sakura_poke(void *self, int x, int y, int strength) {
    sakura_t *s = self;
    s->gust += 5.0f + (float)strength;
    for (int i = 0; i < 20 + strength * 5; i++) {
        petal_t *p = &s->p[RI(s->count)];
        reset_petal(s, p, false);
        p->x = (float)x + RR(-4.0f, 4.0f);
        p->y = (float)y + RR(-2.0f, 2.0f);
    }
    audio_trigger(AUDIO_CHIME, RR(0.3f, 1.0f), pan_for(x, s->cols), 0.35f);
}

static float sakura_energy(void *self) { return clampf(0.3f + 0.1f * fabsf(((sakura_t *)self)->wind), 0.0f, 1.0f); }

const scene_def_t scene_sakura_def = {
    .name = "sakura",
    .blurb = "cherry blossom petals on a spring breeze",
    .bed = BED_SAKURA,
    .create = sakura_create,
    .resize = sakura_resize,
    .update = sakura_update,
    .draw = sakura_draw,
    .poke = sakura_poke,
    .energy = sakura_energy,
    .destroy = sakura_destroy,
};
