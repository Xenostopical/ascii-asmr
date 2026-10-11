#include "rng.h"
#include "scene.h"

#include <stdlib.h>
#include <string.h>

typedef struct {
    int cols, rows;
    float *surf, *kick;
    float swell, swell_prev;
    double drip_next;
} tide_t;

static void tide_alloc(tide_t *s, int cols, int rows) {
    free(s->surf);
    free(s->kick);
    s->cols = cols;
    s->rows = rows;
    s->surf = xcalloc((size_t)cols, sizeof(float));
    s->kick = xcalloc((size_t)cols, sizeof(float));
}

static void *tide_create(int cols, int rows) {
    tide_t *s = xcalloc(1, sizeof *s);
    tide_alloc(s, cols, rows);
    return s;
}

static void tide_resize(void *self, int cols, int rows) { tide_alloc(self, cols, rows); }

static void tide_destroy(void *self) {
    tide_t *s = self;
    free(s->surf);
    free(s->kick);
    free(s);
}

static void tide_update(void *self, double dt, double t) {
    tide_t *s = self;
    float fdt = (float)dt, ft = (float)t;
    float base = (float)s->rows * 0.56f;
    s->swell = sinf(ft * 0.28f);
    float amp = 0.65f + 0.35f * s->swell;
    for (int x = 0; x < s->cols; x++) {
        float fx = (float)x;
        s->surf[x] = base + 1.4f * amp * sinf(fx * 0.075f + ft * 0.62f) + 0.9f * sinf(fx * 0.16f - ft * 0.95f + 1.7f) +
                     1.6f * noise2(fx * 0.035f, ft * 0.18f) - s->kick[x];
    }
    for (int x = 1; x < s->cols - 1; x++) {
        float avg = 0.5f * (s->kick[x - 1] + s->kick[x + 1]);
        s->kick[x] += (avg - s->kick[x]) * fminf(1.0f, fdt * 3.0f);
    }
    float relax = expf(-fdt * 2.2f);
    for (int x = 0; x < s->cols; x++) s->kick[x] *= relax;

    if (s->swell_prev < 0.92f && s->swell >= 0.92f) audio_trigger(AUDIO_SWELL, RF(), RR(-0.4f, 0.4f), 0.8f);
    s->swell_prev = s->swell;
    if (t > s->drip_next) {
        s->drip_next = t + (double)RR(1.5f, 4.5f);
        audio_trigger(AUDIO_PLINK, RR(0.1f, 0.7f), RR(-0.8f, 0.8f), 0.35f);
    }
}

static void tide_draw(void *self, canvas_t *c, double t) {
    tide_t *s = self;
    float ft = (float)t;
    int cols = s->cols, rows = s->rows;
    float moon_x = (float)cols * 0.72f, moon_y = (float)rows * 0.2f;

    int nstars = cols * rows / 30;
    for (int i = 0; i < nstars; i++) {
        int x = (int)(hash_u32((uint32_t)i * 2u) % (uint32_t)cols);
        int y = (int)(hash_u32((uint32_t)i * 2u + 1u) % (uint32_t)maxi(1, rows / 2));
        if ((float)y > s->surf[x] - 2.0f) continue;
        float tw = 0.5f + 0.5f * sinf(ft * (0.5f + hash_float((uint32_t)i)) + (float)i);
        canvas_put(c, x, y, '.', 0.15f + 0.2f * tw, 0.3f);
    }

    for (int dy = -2; dy <= 2; dy++) {
        for (int dx = -4; dx <= 4; dx++) {
            float nx = (float)dx / 4.0f, ny = (float)dy / 2.0f;
            float d = nx * nx + ny * ny;
            if (d > 1.0f) continue;
            canvas_put(c, (int)moon_x + dx, (int)moon_y + dy, d < 0.45f ? '@' : 'O', 0.95f, 0.92f);
        }
    }
    canvas_glow_point(c, moon_x, moon_y, 12.0f, 6.0f, 0.55f);

    for (int x = 0; x < cols; x++) {
        float surf = s->surf[x];
        int top = (int)ceilf(surf);
        float span = fmaxf(1.0f, (float)rows - surf);
        for (int y = top; y < rows; y++) {
            float d = ((float)y - surf) / span;
            float p = 0.5f + 0.5f * sinf((float)x * 0.21f + (float)y * 0.8f + ft * 1.4f +
                                         noise2((float)x * 0.05f, (float)y * 0.2f + ft * 0.3f) * 3.0f);
            float val = (0.12f + 0.5f * p) * (1.0f - 0.75f * d);
            float refl = fabsf((float)x - moon_x) / (3.0f + d * 10.0f);
            float tint = 0.2f + 0.3f * (1.0f - d);
            if (refl < 1.0f) {
                val += (1.0f - refl) * (0.3f + 0.5f * p) * (1.0f - d * 0.6f);
                tint += (1.0f - refl) * 0.4f;
            }
            canvas_put_ramp(c, x, y, "  .:-=+*", val, tint);
        }
        int row = (int)floorf(surf);
        float slope = s->surf[mini(x + 1, cols - 1)] - s->surf[maxi(x - 1, 0)];
        float foam = saturatef(0.3f + 0.5f * fabsf(slope) + 0.4f * s->swell);
        canvas_put(c, x, row, '~', 0.45f + 0.5f * foam, 0.85f);
        if (noise2((float)x * 0.3f, ft * 0.7f) > 0.25f) canvas_put(c, x, row - 1, ',', foam * 0.6f, 0.9f);
        canvas_glow_add(c, x, top, 0.12f);
    }
}

static void tide_poke(void *self, int x, int y, int strength) {
    tide_t *s = self;
    (void)y;
    for (int i = -4; i <= 4; i++) {
        int xx = x + i;
        if (xx < 0 || xx >= s->cols) continue;
        s->kick[xx] += (float)strength * 0.6f * expf(-(float)(i * i) * 0.18f);
    }
    audio_trigger(AUDIO_PLINK, RR(0.2f, 0.9f), pan_for(x, s->cols), 0.6f);
}

static float tide_energy(void *self) { return 0.5f + 0.4f * ((tide_t *)self)->swell; }

const scene_def_t scene_tide_def = {
    .name = "tide",
    .blurb = "moonlit waves rolling in",
    .bed = BED_TIDE,
    .create = tide_create,
    .resize = tide_resize,
    .update = tide_update,
    .draw = tide_draw,
    .poke = tide_poke,
    .energy = tide_energy,
    .destroy = tide_destroy,
};
