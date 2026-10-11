#include "rng.h"
#include "scene.h"

#include <stdlib.h>
#include <string.h>

#define MAX_DROPS 900
#define MAX_SPLASH 500

typedef struct {
    float x, y, vy;
    int len, layer;
    bool alive;
} drop_t;

typedef struct {
    float x, y, vx, vy, life, max_life;
    bool alive;
} splash_t;

typedef struct {
    int cols, rows;
    drop_t drops[MAX_DROPS];
    splash_t spl[MAX_SPLASH];
    float *ground;
    float wind, gust, intensity, spawn_acc;
    int next_drop, next_spl, sound_budget;
} rain_t;

static void rain_alloc(rain_t *r, int cols, int rows) {
    free(r->ground);
    r->cols = cols;
    r->rows = rows;
    r->ground = xcalloc((size_t)cols, sizeof(float));
}

static void *rain_create(int cols, int rows) {
    rain_t *r = xcalloc(1, sizeof *r);
    rain_alloc(r, cols, rows);
    r->intensity = 0.7f;
    return r;
}

static void rain_resize(void *self, int cols, int rows) {
    rain_t *r = self;
    rain_alloc(r, cols, rows);
    memset(r->drops, 0, sizeof r->drops);
    memset(r->spl, 0, sizeof r->spl);
}

static void rain_destroy(void *self) {
    rain_t *r = self;
    free(r->ground);
    free(r);
}

static void spawn_drop(rain_t *r, float x, float y, float boost) {
    static const float speeds[3] = {18.0f, 26.0f, 36.0f};
    static const int lens[3] = {1, 2, 4};
    float roll = RF();
    int layer = roll < 0.5f ? 0 : (roll < 0.85f ? 1 : 2);
    drop_t *d = &r->drops[r->next_drop];
    r->next_drop = (r->next_drop + 1) % MAX_DROPS;
    d->x = x;
    d->y = y;
    d->vy = speeds[layer] * RR(0.85f, 1.15f) * boost;
    d->len = lens[layer];
    d->layer = layer;
    d->alive = true;
}

static void spawn_splash(rain_t *r, float x, float y, int layer) {
    int n = 2 + layer;
    for (int i = 0; i < n; i++) {
        splash_t *s = &r->spl[r->next_spl];
        r->next_spl = (r->next_spl + 1) % MAX_SPLASH;
        float k = 0.5f + 0.4f * (float)layer;
        s->x = x;
        s->y = y;
        s->vx = RR(-6.0f, 6.0f) * k;
        s->vy = -RR(3.0f, 9.0f) * (0.6f + 0.3f * (float)layer);
        s->max_life = s->life = RR(0.25f, 0.5f);
        s->alive = true;
    }
}

static void rain_update(void *self, double dt, double t) {
    rain_t *r = self;
    float fdt = (float)dt, ft = (float)t;
    r->wind = noise2(ft * 0.06f, 3.1f) * 7.0f + r->gust;
    r->gust *= expf(-fdt * 1.2f);
    r->intensity = clampf(0.65f + 0.45f * noise2(ft * 0.035f, 9.3f), 0.15f, 1.3f);
    r->spawn_acc += (float)r->cols * 0.9f * r->intensity * fdt;
    while (r->spawn_acc >= 1.0f) {
        r->spawn_acc -= 1.0f;
        spawn_drop(r, RR(-8.0f, (float)r->cols + 8.0f), -RR(0.0f, 3.0f), 1.0f);
    }
    r->sound_budget = 3;

    for (int i = 0; i < MAX_DROPS; i++) {
        drop_t *d = &r->drops[i];
        if (!d->alive) continue;
        d->x += r->wind * (0.4f + 0.3f * (float)d->layer) * fdt;
        d->y += d->vy * fdt;
        if (d->x < -12.0f || d->x > (float)r->cols + 12.0f) {
            d->alive = false;
            continue;
        }
        if (d->y >= (float)(r->rows - 1)) {
            d->alive = false;
            int gx = clampi((int)d->x, 0, r->cols - 1);
            r->ground[gx] = 1.0f;
            spawn_splash(r, d->x, (float)(r->rows - 1), d->layer);
            if (d->layer >= 1 && r->sound_budget > 0 && RC(0.18f + 0.1f * (float)d->layer)) {
                audio_trigger(AUDIO_TICK, RF(), pan_for(gx, r->cols), 0.35f + 0.15f * (float)d->layer);
                r->sound_budget--;
            }
        }
    }

    for (int i = 0; i < MAX_SPLASH; i++) {
        splash_t *s = &r->spl[i];
        if (!s->alive) continue;
        s->vy += 40.0f * fdt;
        s->x += s->vx * fdt;
        s->y += s->vy * fdt;
        s->life -= fdt;
        if (s->life <= 0.0f || s->y >= (float)r->rows) s->alive = false;
    }
    float decay = expf(-fdt * 5.0f);
    for (int x = 0; x < r->cols; x++) r->ground[x] *= decay;
}

static void rain_draw(void *self, canvas_t *c, double t) {
    rain_t *r = self;
    (void)t;
    int rows = r->rows;
    for (int i = 0; i < MAX_DROPS; i++) {
        const drop_t *d = &r->drops[i];
        if (!d->alive) continue;
        int ix = (int)floorf(d->x), iy = (int)floorf(d->y);
        float slant = r->wind * 0.04f * (0.4f + 0.3f * (float)d->layer);
        for (int k = 0; k <= d->len; k++) {
            int xx = ix - (int)roundf(slant * (float)k);
            float v = (0.35f + 0.2f * (float)d->layer) * (1.0f - (float)k / (float)(d->len + 2));
            if (k == 0) v += 0.25f;
            uint32_t g = '|';
            if (fabsf(r->wind) > 3.0f && k <= 1) g = r->wind > 0.0f ? '\\' : '/';
            if (k == d->len && d->len > 1) g = ':';
            canvas_put(c, xx, iy - k, g, v, 0.15f + 0.25f * (float)d->layer);
        }
    }
    for (int i = 0; i < MAX_SPLASH; i++) {
        const splash_t *s = &r->spl[i];
        if (!s->alive) continue;
        float f = s->life / s->max_life;
        canvas_put(c, (int)s->x, (int)s->y, f > 0.5f ? '\'' : (f > 0.25f ? ',' : '.'), 0.25f + 0.6f * f, 0.65f);
    }
    for (int x = 0; x < r->cols; x++) {
        canvas_put(c, x, rows - 1, '_', 0.16f, 0.5f);
        float g = r->ground[x];
        if (g > 0.05f) canvas_put(c, x, rows - 1, '~', 0.3f + g * 0.6f, 0.75f);
        for (int j = 0; j < 4; j++) canvas_glow_add(c, x, rows - 1 - j, 0.30f * (1.0f - (float)j / 4.0f) * r->intensity);
    }
}

static void rain_poke(void *self, int x, int y, int strength) {
    rain_t *r = self;
    (void)y;
    int n = 10 + strength * 4;
    for (int i = 0; i < n; i++) spawn_drop(r, (float)x + RR(-6.0f, 6.0f), -RR(0.0f, 4.0f), 1.5f);
    r->gust += RR(-1.0f, 1.0f) * 6.0f;
}

static float rain_energy(void *self) { return ((rain_t *)self)->intensity / 1.3f; }

const scene_def_t scene_rain_def = {
    .name = "rain",
    .blurb = "soft rain on a quiet night",
    .bed = BED_RAIN,
    .create = rain_create,
    .resize = rain_resize,
    .update = rain_update,
    .draw = rain_draw,
    .poke = rain_poke,
    .energy = rain_energy,
    .destroy = rain_destroy,
};
