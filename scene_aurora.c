#include "rng.h"
#include "scene.h"

#include <stdlib.h>
#include <string.h>

#define RIBBONS 3

typedef struct {
    int cols, rows;
    float flare, flare_x, mean;
    float *ridge;
    double next_chime;
} aurora_t;

static void aurora_alloc(aurora_t *a, int cols, int rows) {
    free(a->ridge);
    a->cols = cols;
    a->rows = rows;
    a->ridge = xcalloc((size_t)cols, sizeof(float));
    for (int x = 0; x < cols; x++) a->ridge[x] = 1.0f + 2.5f * (0.5f + 0.5f * fbm2((float)x * 0.04f, 4.0f, 3));
}

static void *aurora_create(int cols, int rows) {
    aurora_t *a = xcalloc(1, sizeof *a);
    aurora_alloc(a, cols, rows);
    a->next_chime = 6.0;
    return a;
}

static void aurora_resize(void *self, int cols, int rows) { aurora_alloc(self, cols, rows); }

static void aurora_destroy(void *self) {
    aurora_t *a = self;
    free(a->ridge);
    free(a);
}

static void aurora_update(void *self, double dt, double t) {
    aurora_t *a = self;
    a->flare *= expf(-(float)dt * 0.8f);
    if (t > a->next_chime) {
        a->next_chime = t + (double)RR(6.0f, 12.0f);
        audio_trigger(AUDIO_BELL, RR(0.3f, 0.9f), RR(-0.5f, 0.5f), 0.22f);
    }
}

static float ribbon_intensity(const aurora_t *a, int r, int x, int y, float ft, float *tint) {
    float fx = (float)x, fy = (float)y, rows = (float)a->rows;
    float center = rows * (0.22f + 0.13f * (float)r) + 4.0f * noise2(fx * 0.03f + (float)r * 9.0f, ft * 0.12f) +
                   2.0f * sinf(fx * 0.05f + ft * 0.3f + (float)r);
    float width = 2.5f + 2.0f * (0.5f + 0.5f * noise2(fx * 0.05f, (float)r * 3.0f + ft * 0.1f));
    float d = (fy - center) / width;
    float body = expf(-d * d * (d < 0.0f ? 2.2f : 0.7f));
    float curtain = 0.5f + 0.5f * noise2(fx * 0.045f + (float)r * 10.0f, ft * 0.15f);
    float rays = 0.65f + 0.35f * noise2(fx * 0.9f, ft * 0.5f + (float)r);
    *tint = clampf(0.15f + 0.3f * (float)r + 0.25f * noise2(fx * 0.02f, ft * 0.1f + (float)r), 0.0f, 1.0f);
    return body * curtain * rays;
}

static void aurora_draw(void *self, canvas_t *c, double t) {
    aurora_t *a = self;
    float ft = (float)t;
    float total = 0.0f;

    for (int i = 0; i < a->cols * a->rows / 40; i++) {
        int x = (int)(hash_u32((uint32_t)i * 2u + 7u) % (uint32_t)a->cols);
        int y = (int)(hash_u32((uint32_t)i * 2u + 8u) % (uint32_t)maxi(1, a->rows - 3));
        float tw = 0.5f + 0.5f * sinf(ft * (0.4f + hash_float((uint32_t)i)) + (float)i);
        canvas_put(c, x, y, '.', 0.12f + 0.16f * tw, 0.3f);
    }

    for (int y = 0; y < a->rows - 1; y++) {
        for (int x = 0; x < a->cols; x++) {
            float best = 0.0f, tint = 0.0f;
            for (int r = 0; r < RIBBONS; r++) {
                float tn;
                float v = ribbon_intensity(a, r, x, y, ft, &tn);
                if (v > best) {
                    best = v;
                    tint = tn;
                }
            }
            float dx = (float)x - a->flare_x;
            best += a->flare * expf(-dx * dx * 0.004f) * 0.5f * expf(-powf((float)y / (float)a->rows - 0.3f, 2.0f) * 10.0f);
            total += best;
            if (best < 0.06f) continue;
            canvas_put_ramp(c, x, y, " .:;+=*#%@", best * 0.95f, tint);
            canvas_glow_add(c, x, y, best * 0.7f);
        }
    }
    a->mean = total / (float)(a->cols * a->rows) * 6.0f;

    for (int x = 0; x < a->cols; x++) {
        int h = (int)a->ridge[x];
        for (int k = 0; k <= h; k++) {
            canvas_put(c, x, a->rows - 1 - k, k == h ? '^' : '#', 0.12f, 0.0f);
        }
    }
}

static void aurora_poke(void *self, int x, int y, int strength) {
    aurora_t *a = self;
    (void)y;
    a->flare = 1.0f + (float)strength * 0.2f;
    a->flare_x = (float)x;
    audio_trigger(AUDIO_SWELL, RR(0.0f, 1.0f), pan_for(x, a->cols), 0.6f);
}

static float aurora_energy(void *self) { return clampf(((aurora_t *)self)->mean * 1.4f, 0.15f, 1.0f); }

const scene_def_t scene_aurora_def = {
    .name = "aurora",
    .blurb = "ribbons of northern light",
    .bed = BED_AURORA,
    .create = aurora_create,
    .resize = aurora_resize,
    .update = aurora_update,
    .draw = aurora_draw,
    .poke = aurora_poke,
    .energy = aurora_energy,
    .destroy = aurora_destroy,
};
