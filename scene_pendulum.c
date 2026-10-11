#include "rng.h"
#include "scene.h"

#include <stdlib.h>
#include <string.h>

/* Pendulum waves: each pendulum completes one more swing than its left
 * neighbour per cycle, so the row drifts out of phase and snaps back. Each
 * pass through the centre plays a note. */

#define MAXP 24
#define CYCLE 45.0f

typedef struct {
    int cols, rows, n;
    float spacing, amp, len;
    float *trail;
    float prev_c[MAXP];
    double clock;
} pwave_t;

static void pend_setup(pwave_t *s, int cols, int rows) {
    free(s->trail);
    s->cols = cols;
    s->rows = rows;
    s->n = clampi((cols - 4) / 4, 5, MAXP);
    s->spacing = (float)(cols - 2) / (float)s->n;
    s->amp = s->spacing * 0.42f;
    s->len = (float)maxi(4, rows - 5);
    s->trail = xcalloc((size_t)cols * (size_t)rows, sizeof(float));
    for (int i = 0; i < MAXP; i++) s->prev_c[i] = 1.0f;
    s->clock = 0.0;
}

static void *pend_create(int cols, int rows) {
    pwave_t *s = xcalloc(1, sizeof *s);
    pend_setup(s, cols, rows);
    return s;
}

static void pend_resize(void *self, int cols, int rows) { pend_setup(self, cols, rows); }

static void pend_destroy(void *self) {
    pwave_t *s = self;
    free(s->trail);
    free(s);
}

static float swing(const pwave_t *s, int i) { return cosf(TAU * (10.0f + (float)i) / CYCLE * (float)s->clock); }
static float pivot_x(const pwave_t *s, int i) { return s->spacing * ((float)i + 0.5f) + 1.0f; }

static void pend_update(void *self, double dt, double t) {
    pwave_t *s = self;
    (void)t;
    s->clock += dt;
    size_t n = (size_t)s->cols * (size_t)s->rows;
    float decay = expf(-(float)dt * 2.0f);
    for (size_t i = 0; i < n; i++) s->trail[i] *= decay;
    int budget = 3;
    for (int i = 0; i < s->n; i++) {
        float c = swing(s, i);
        if (((s->prev_c[i] > 0.0f) != (c > 0.0f)) && budget > 0) {
            budget--;
            audio_trigger(AUDIO_CHIME, (float)i / (float)maxi(1, s->n - 1), pan_for((int)pivot_x(s, i), s->cols), 0.2f);
        }
        s->prev_c[i] = c;
        float bx = pivot_x(s, i) + s->amp * c;
        int ix = (int)lroundf(bx), iy = 2 + (int)lroundf(s->len);
        if (ix >= 0 && ix < s->cols && iy >= 0 && iy < s->rows) s->trail[iy * s->cols + ix] = 1.0f;
    }
}

static void pend_draw(void *self, canvas_t *c, double t) {
    pwave_t *s = self;
    (void)t;
    for (int x = 0; x < s->cols; x++) canvas_put(c, x, 1, '=', 0.5f, 0.1f);
    for (int y = 0; y < s->rows; y++) {
        for (int x = 0; x < s->cols; x++) {
            float v = s->trail[y * s->cols + x];
            if (v > 0.06f) canvas_put(c, x, y, v > 0.5f ? ':' : '.', v * 0.6f, 0.5f);
        }
    }
    int by = 2 + (int)lroundf(s->len);
    for (int i = 0; i < s->n; i++) {
        float px = pivot_x(s, i), bx = px + s->amp * swing(s, i);
        float tint = (float)i / (float)maxi(1, s->n - 1);
        int steps = (int)s->len;
        for (int k = 1; k < steps; k++) {
            float f = (float)k / (float)steps;
            float x = lerpf(px, bx, f);
            uint32_t g = fabsf(bx - px) < 0.7f ? '|' : (bx > px ? '\\' : '/');
            canvas_put(c, (int)lroundf(x), 2 + k, g, 0.22f, tint * 0.5f);
        }
        canvas_put(c, (int)lroundf(bx), by, 'O', 0.95f, tint);
        canvas_glow_point(c, bx, (float)by, 4.0f, 2.0f, 0.5f);
    }
}

static void pend_poke(void *self, int x, int y, int strength) {
    pwave_t *s = self;
    (void)y;
    (void)strength;
    s->clock += 3.0;
    audio_trigger(AUDIO_BELL, (float)x / (float)maxi(1, s->cols), pan_for(x, s->cols), 0.3f);
}

static float pend_energy(void *self) {
    (void)self;
    return 0.35f;
}

const scene_def_t scene_pendulum_def = {
    .name = "pendulum",
    .blurb = "pendulum waves drifting out of phase",
    .bed = BED_PENDULUM,
    .create = pend_create,
    .resize = pend_resize,
    .update = pend_update,
    .draw = pend_draw,
    .poke = pend_poke,
    .energy = pend_energy,
    .destroy = pend_destroy,
};
