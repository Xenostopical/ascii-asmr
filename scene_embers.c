#include "rng.h"
#include "scene.h"

#include <stdlib.h>
#include <string.h>

/* Campfire: a classic heat-propagation fire model on a 30 Hz fixed step,
 * with rising sparks and random crackle pops. */

#define MAX_SPARKS 260
#define FIRE_STEP (1.0f / 30.0f)

typedef struct {
    float x, y, vx, vy, life, max_life;
    bool alive;
} spark_t;

typedef struct {
    int cols, rows;
    float *heat;
    spark_t sp[MAX_SPARKS];
    int next_sp;
    float acc, boost, avg;
    double next_pop;
} fire_t;

static void fire_alloc(fire_t *f, int cols, int rows) {
    free(f->heat);
    f->cols = cols;
    f->rows = rows;
    f->heat = xcalloc((size_t)cols * (size_t)rows, sizeof(float));
    memset(f->sp, 0, sizeof f->sp);
}

static void *fire_create(int cols, int rows) {
    fire_t *f = xcalloc(1, sizeof *f);
    fire_alloc(f, cols, rows);
    f->next_pop = 1.0;
    return f;
}

static void fire_resize(void *self, int cols, int rows) { fire_alloc(self, cols, rows); }

static void fire_destroy(void *self) {
    fire_t *f = self;
    free(f->heat);
    free(f);
}

static float half_width(const fire_t *f) { return fmaxf(5.0f, (float)f->cols * 0.11f * (1.0f + 0.5f * f->boost)); }

static void fire_step(fire_t *f, float t) {
    int cols = f->cols, rows = f->rows;
    float cx = (float)cols * 0.5f, half = half_width(f);
    for (int x = 0; x < cols; x++) {
        float d = fabsf((float)x - cx) / half;
        float src = d < 1.0f ? 1.0f - d * d : 0.0f;
        src *= 0.75f + 0.25f * noise2((float)x * 0.3f, t * 3.0f);
        f->heat[(rows - 1) * cols + x] = clampf(src * (1.0f + 0.3f * f->boost), 0.0f, 1.0f);
    }
    float cool_base = 1.0f / ((float)rows * 0.5f);
    float sway = noise2(t * 0.8f, 4.4f) * 1.2f;
    float total = 0.0f;
    for (int y = 0; y < rows - 1; y++) {
        int y1 = y + 1, y2 = mini(y + 2, rows - 1);
        for (int x = 0; x < cols; x++) {
            int sx = clampi(x + (int)roundf(sway + noise2((float)y * 0.1f, t) * 0.6f) + (RI(3) - 1), 0, cols - 1);
            int xl = clampi(sx - 1, 0, cols - 1), xr = clampi(sx + 1, 0, cols - 1);
            float h = (f->heat[y1 * cols + xl] + f->heat[y1 * cols + sx] + f->heat[y1 * cols + xr] +
                       f->heat[y2 * cols + sx]) * 0.25f;
            h -= cool_base * RR(0.6f, 1.4f);
            h = h > 0.0f ? h : 0.0f;
            f->heat[y * cols + x] = h;
            total += h;
        }
    }
    f->avg = total / (float)(cols * rows) * 8.0f;
}

static void add_spark(fire_t *f, float x, float y, float speed) {
    spark_t *s = &f->sp[f->next_sp];
    f->next_sp = (f->next_sp + 1) % MAX_SPARKS;
    s->x = x;
    s->y = y;
    s->vx = RR(-2.0f, 2.0f);
    s->vy = -RR(5.0f, 14.0f) * speed;
    s->max_life = s->life = RR(1.0f, 3.0f);
    s->alive = true;
}

static void fire_update(void *self, double dt, double t) {
    fire_t *f = self;
    float fdt = (float)dt, ft = (float)t;
    f->acc += fdt;
    int guard = 0;
    while (f->acc >= FIRE_STEP && guard++ < 4) {
        fire_step(f, ft);
        f->acc -= FIRE_STEP;
    }
    f->boost *= expf(-fdt * 0.6f);

    float cx = (float)f->cols * 0.5f, half = half_width(f);
    if (RC(fdt * 18.0f)) add_spark(f, cx + rng_gauss(rng_global()) * half * 0.5f, (float)f->rows - 3.0f - RR(0.0f, (float)f->rows * 0.2f), 1.0f);
    if (RC(fdt * 8.0f)) audio_trigger(AUDIO_CRACKLE, RF(), RR(-0.3f, 0.3f), 0.18f);
    if (t > f->next_pop) {
        f->next_pop = t + (double)RR(0.4f, 2.0f);
        audio_trigger(AUDIO_CRACKLE, RF(), RR(-0.4f, 0.4f), 0.4f + 0.4f * RF());
        for (int i = 0; i < 6; i++) add_spark(f, cx + RR(-half, half) * 0.6f, (float)f->rows - 4.0f, 1.3f);
    }

    for (int i = 0; i < MAX_SPARKS; i++) {
        spark_t *s = &f->sp[i];
        if (!s->alive) continue;
        s->x += (s->vx + noise2(s->y * 0.1f, ft) * 3.0f) * fdt;
        s->y += s->vy * fdt;
        s->life -= fdt;
        if (s->life <= 0.0f || s->y < 0.0f) s->alive = false;
    }
}

static void fire_draw(void *self, canvas_t *c, double t) {
    fire_t *f = self;
    (void)t;
    for (int y = 0; y < f->rows; y++) {
        for (int x = 0; x < f->cols; x++) {
            float h = f->heat[y * f->cols + x];
            if (h < 0.04f) continue;
            canvas_put_ramp(c, x, y, " .:-=+*#%@", h, h);
            canvas_glow_add(c, x, y, h * 0.9f);
        }
    }
    float cx = (float)f->cols * 0.5f, half = half_width(f);
    for (int x = (int)(cx - half * 1.2f); x <= (int)(cx + half * 1.2f); x++) {
        canvas_put(c, x, f->rows - 1, '=', 1.05f, 0.12f);
        if (fabsf((float)x - cx) < half * 0.9f) canvas_put(c, x, f->rows - 2, '#', 1.05f, 0.08f);
    }
    for (int i = 0; i < MAX_SPARKS; i++) {
        const spark_t *s = &f->sp[i];
        if (!s->alive) continue;
        float k = s->life / s->max_life;
        canvas_put(c, (int)s->x, (int)s->y, k > 0.5f ? '*' : (k > 0.2f ? '+' : '.'), 0.3f + 0.7f * k, 0.9f);
    }
}

static void fire_poke(void *self, int x, int y, int strength) {
    fire_t *f = self;
    f->boost = 1.0f;
    for (int i = 0; i < 20 + strength * 6; i++) add_spark(f, (float)x + RR(-3.0f, 3.0f), (float)y, 1.4f);
    audio_trigger(AUDIO_CRACKLE, RF(), pan_for(x, f->cols), 0.8f);
}

static float fire_energy(void *self) { return clampf(((fire_t *)self)->avg + 0.2f, 0.2f, 1.0f); }

const scene_def_t scene_embers_def = {
    .name = "embers",
    .blurb = "a crackling campfire and rising sparks",
    .bed = BED_EMBERS,
    .create = fire_create,
    .resize = fire_resize,
    .update = fire_update,
    .draw = fire_draw,
    .poke = fire_poke,
    .energy = fire_energy,
    .destroy = fire_destroy,
};
