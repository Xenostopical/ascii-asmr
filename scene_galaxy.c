#include "rng.h"
#include "scene.h"

#include <stdlib.h>
#include <string.h>

/* A spiral galaxy seen at an angle. Thousands of stars are deposited into a
 * density grid, which is mapped to glyphs so arms read as soft clouds. */

#define MAX_STARS 2800

typedef struct {
    float r, phi, bright, tint;
} gstar_t;

typedef struct {
    int cols, rows, count;
    gstar_t s[MAX_STARS];
    float *acc, *tin;
    float rot, kick, nova_age, nx, ny;
    double next_nova;
} galaxy_t;

static void galaxy_setup(galaxy_t *g, int cols, int rows) {
    free(g->acc);
    free(g->tin);
    g->cols = cols;
    g->rows = rows;
    g->acc = xcalloc((size_t)cols * (size_t)rows, sizeof(float));
    g->tin = xcalloc((size_t)cols * (size_t)rows, sizeof(float));
    g->count = clampi(cols * rows * 3 / 4, 200, MAX_STARS);
    for (int i = 0; i < g->count; i++) {
        gstar_t *s = &g->s[i];
        float roll = RF();
        if (roll < 0.12f) {
            s->r = fabsf(rng_gauss(rng_global())) * 0.12f;
            s->phi = RR(0.0f, TAU);
            s->tint = 0.9f;
        } else if (roll < 0.2f) {
            s->r = RR(0.1f, 1.0f);
            s->phi = RR(0.0f, TAU);
            s->tint = 0.4f;
        } else {
            s->r = powf(RF(), 1.5f);
            int arm = RI(3);
            float spread = 0.28f + 0.25f * (1.0f - s->r);
            s->phi = (float)arm * TAU / 3.0f + 3.2f * logf(s->r + 0.08f) + rng_gauss(rng_global()) * spread;
            s->tint = 0.15f + 0.4f * s->r;
        }
        s->r = clampf(s->r, 0.0f, 1.0f);
        s->bright = RR(0.25f, 1.0f);
    }
    g->next_nova = 6.0;
}

static void *galaxy_create(int cols, int rows) {
    galaxy_t *g = xcalloc(1, sizeof *g);
    galaxy_setup(g, cols, rows);
    return g;
}

static void galaxy_resize(void *self, int cols, int rows) { galaxy_setup(self, cols, rows); }

static void galaxy_destroy(void *self) {
    galaxy_t *g = self;
    free(g->acc);
    free(g->tin);
    free(g);
}

static void project(const galaxy_t *g, const gstar_t *s, float *x, float *y) {
    float rx = (float)g->cols * 0.46f;
    float ry = fminf((float)g->rows * 0.46f, rx * 0.5f * 0.62f);
    float th = s->phi + g->rot * (1.0f + 0.15f * (1.0f - s->r));
    *x = (float)g->cols * 0.5f + s->r * cosf(th) * rx;
    *y = (float)g->rows * 0.5f + s->r * sinf(th) * ry;
}

static void galaxy_update(void *self, double dt, double t) {
    galaxy_t *g = self;
    float fdt = (float)dt;
    g->rot += fdt * 0.12f * (1.0f + g->kick);
    g->kick *= expf(-fdt * 0.5f);
    if (g->nova_age > 0.0f) g->nova_age -= fdt;
    if (t > g->next_nova) {
        g->next_nova = t + (double)RR(8.0f, 16.0f);
        const gstar_t *s = &g->s[RI(g->count)];
        project(g, s, &g->nx, &g->ny);
        g->nova_age = 1.2f;
        audio_trigger(AUDIO_BELL, RR(0.0f, 0.4f), pan_for((int)g->nx, g->cols), 0.35f);
    }
}

static void galaxy_draw(void *self, canvas_t *c, double t) {
    galaxy_t *g = self;
    (void)t;
    size_t n = (size_t)g->cols * (size_t)g->rows;
    memset(g->acc, 0, n * sizeof(float));
    memset(g->tin, 0, n * sizeof(float));
    for (int i = 0; i < g->count; i++) {
        const gstar_t *s = &g->s[i];
        float x, y;
        project(g, s, &x, &y);
        int ix = (int)x, iy = (int)y;
        if (ix < 0 || iy < 0 || ix >= g->cols || iy >= g->rows) continue;
        float w = s->bright * 0.35f * (0.4f + (1.0f - s->r) * 0.8f);
        size_t idx = (size_t)iy * (size_t)g->cols + (size_t)ix;
        g->acc[idx] += w;
        g->tin[idx] += w * s->tint;
    }
    for (int y = 0; y < g->rows; y++) {
        for (int x = 0; x < g->cols; x++) {
            size_t idx = (size_t)y * (size_t)g->cols + (size_t)x;
            float a = g->acc[idx];
            if (a < 0.05f) continue;
            float v = 1.0f - expf(-a * 1.2f);
            canvas_put_ramp(c, x, y, " .:-=+*#%@", v, g->tin[idx] / a);
            canvas_glow_add(c, x, y, v * 0.45f);
        }
    }
    canvas_glow_point(c, (float)g->cols * 0.5f, (float)g->rows * 0.5f, (float)g->cols * 0.12f, (float)g->rows * 0.12f, 0.8f);
    if (g->nova_age > 0.0f) {
        float k = g->nova_age / 1.2f;
        canvas_put(c, (int)g->nx, (int)g->ny, '*', 1.05f, 0.95f);
        canvas_glow_point(c, g->nx, g->ny, 9.0f * (2.0f - k), 4.5f * (2.0f - k), 1.0f * k);
    }
}

static void galaxy_poke(void *self, int x, int y, int strength) {
    galaxy_t *g = self;
    g->kick = 1.0f + (float)strength * 0.5f;
    g->nx = (float)x;
    g->ny = (float)y;
    g->nova_age = 1.2f;
    audio_trigger(AUDIO_BELL, RR(0.0f, 0.6f), pan_for(x, g->cols), 0.4f);
}

static float galaxy_energy(void *self) { return clampf(0.4f + 0.3f * ((galaxy_t *)self)->kick, 0.0f, 1.0f); }

const scene_def_t scene_galaxy_def = {
    .name = "galaxy",
    .blurb = "a slowly turning spiral galaxy",
    .bed = BED_GALAXY,
    .create = galaxy_create,
    .resize = galaxy_resize,
    .update = galaxy_update,
    .draw = galaxy_draw,
    .poke = galaxy_poke,
    .energy = galaxy_energy,
    .destroy = galaxy_destroy,
};
