#include "rng.h"
#include "scene.h"

#include <stdlib.h>
#include <string.h>

/* A real 2D wave-equation simulation. Vertical coupling is weaker than
 * horizontal so rings look circular despite tall terminal cells. */

#define STEP_DT (1.0f / 60.0f)
#define COUPLE_X 1.0f
#define COUPLE_Y 0.25f
#define WAVE_K 0.35f
#define DAMPING 0.9965f

typedef struct {
    int cols, rows;
    float *h, *v;
    float acc;
    double next_drop;
    float activity;
} ripples_t;

static void ripples_alloc(ripples_t *r, int cols, int rows) {
    free(r->h);
    free(r->v);
    r->cols = cols;
    r->rows = rows;
    r->h = xcalloc((size_t)cols * (size_t)rows, sizeof(float));
    r->v = xcalloc((size_t)cols * (size_t)rows, sizeof(float));
}

static void *ripples_create(int cols, int rows) {
    ripples_t *r = xcalloc(1, sizeof *r);
    ripples_alloc(r, cols, rows);
    return r;
}

static void ripples_resize(void *self, int cols, int rows) { ripples_alloc(self, cols, rows); }

static void ripples_destroy(void *self) {
    ripples_t *r = self;
    free(r->h);
    free(r->v);
    free(r);
}

static void splat(ripples_t *r, int cx, int cy, float amp) {
    for (int dy = -1; dy <= 1; dy++) {
        for (int dx = -2; dx <= 2; dx++) {
            int x = cx + dx, y = cy + dy;
            if (x < 1 || y < 1 || x >= r->cols - 1 || y >= r->rows - 1) continue;
            float fall = expf(-((float)(dx * dx) * 0.5f + (float)(dy * dy) * 1.6f));
            r->h[y * r->cols + x] += amp * fall;
        }
    }
}

static void sim_step(ripples_t *r) {
    int cols = r->cols, rows = r->rows;
    for (int y = 1; y < rows - 1; y++) {
        for (int x = 1; x < cols - 1; x++) {
            int i = y * cols + x;
            float h = r->h[i];
            float lap = COUPLE_X * (r->h[i - 1] + r->h[i + 1] - 2.0f * h) +
                        COUPLE_Y * (r->h[i - cols] + r->h[i + cols] - 2.0f * h);
            r->v[i] = (r->v[i] + lap * WAVE_K) * DAMPING;
        }
    }
    float energy = 0.0f;
    for (int y = 1; y < rows - 1; y++) {
        for (int x = 1; x < cols - 1; x++) {
            int i = y * cols + x;
            r->h[i] += r->v[i];
            energy += fabsf(r->h[i]);
        }
    }
    r->activity = energy / (float)(cols * rows) * 6.0f;
}

static void ripples_update(void *self, double dt, double t) {
    ripples_t *r = self;
    if (t > r->next_drop) {
        r->next_drop = t + (double)RR(0.5f, 1.6f);
        int x = 3 + RI(maxi(1, r->cols - 6)), y = 2 + RI(maxi(1, r->rows - 4));
        float amp = RR(2.5f, 6.0f);
        splat(r, x, y, amp);
        audio_trigger(AUDIO_PLINK, saturatef(amp / 6.0f), pan_for(x, r->cols), 0.3f + amp * 0.05f);
    }
    r->acc += (float)dt;
    int guard = 0;
    while (r->acc >= STEP_DT && guard++ < 6) {
        sim_step(r);
        r->acc -= STEP_DT;
    }
    if (r->acc > STEP_DT) r->acc = 0.0f;
}

static void ripples_draw(void *self, canvas_t *c, double t) {
    ripples_t *r = self;
    (void)t;
    for (int y = 1; y < r->rows - 1; y++) {
        for (int x = 1; x < r->cols - 1; x++) {
            int i = y * r->cols + x;
            float h = r->h[i];
            float slope = (r->h[i + 1] - r->h[i - 1]) * 0.5f;
            float val = fabsf(h) * 0.9f + fabsf(slope) * 0.8f;
            if (val < 0.04f) continue;
            float tint = h >= 0.0f ? 0.8f : 0.2f;
            canvas_put_ramp(c, x, y, " .:oO@", val, tint);
            canvas_glow_add(c, x, y, saturatef(val * 0.5f));
        }
    }
}

static void ripples_poke(void *self, int x, int y, int strength) {
    ripples_t *r = self;
    float amp = 3.0f + (float)strength * 2.0f;
    splat(r, x, y, amp);
    audio_trigger(AUDIO_PLINK, RR(0.3f, 1.0f), pan_for(x, r->cols), 0.6f);
}

static float ripples_energy(void *self) { return clampf(((ripples_t *)self)->activity, 0.0f, 1.0f); }

const scene_def_t scene_ripples_def = {
    .name = "ripples",
    .blurb = "raindrops spreading across still water",
    .bed = BED_RIPPLES,
    .create = ripples_create,
    .resize = ripples_resize,
    .update = ripples_update,
    .draw = ripples_draw,
    .poke = ripples_poke,
    .energy = ripples_energy,
    .destroy = ripples_destroy,
};
