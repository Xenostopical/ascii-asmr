#include "rng.h"
#include "scene.h"

#include <stdlib.h>
#include <string.h>

typedef struct {
    float x, y, age, life, speed;
} mote_t;

typedef struct {
    int cols, rows, count;
    mote_t *m;
    float *trail;
    uint8_t *dir;
    float *hue;
    float vx, vy, vstr;
    int vcx, vcy;
    float mean_speed;
    double next_chime;
} drift_t;

static void respawn(drift_t *d, mote_t *m) {
    m->x = RR(0.0f, (float)d->cols);
    m->y = RR(0.0f, (float)d->rows);
    m->age = 0.0f;
    m->life = RR(3.0f, 9.0f);
    m->speed = RR(0.7f, 1.4f);
}

static void drift_alloc(drift_t *d, int cols, int rows) {
    free(d->m);
    free(d->trail);
    free(d->dir);
    free(d->hue);
    size_t n = (size_t)cols * (size_t)rows;
    d->cols = cols;
    d->rows = rows;
    d->count = clampi(cols * rows / 5, 60, 1600);
    d->m = xcalloc((size_t)d->count, sizeof(mote_t));
    d->trail = xcalloc(n, sizeof(float));
    d->dir = xcalloc(n, sizeof(uint8_t));
    d->hue = xcalloc(n, sizeof(float));
    for (int i = 0; i < d->count; i++) {
        respawn(d, &d->m[i]);
        d->m[i].age = RR(0.0f, d->m[i].life);
    }
}

static void *drift_create(int cols, int rows) {
    drift_t *d = xcalloc(1, sizeof *d);
    drift_alloc(d, cols, rows);
    return d;
}

static void drift_resize(void *self, int cols, int rows) { drift_alloc(self, cols, rows); }

static void drift_destroy(void *self) {
    drift_t *d = self;
    free(d->m);
    free(d->trail);
    free(d->dir);
    free(d->hue);
    free(d);
}

static uint32_t dir_glyph(float vx, float vy) {
    /* vy is scaled by cell aspect so angles read correctly on screen. */
    float ang = atan2f(vy, vx);
    float a = fabsf(ang);
    if (a < PI_F * 0.125f || a > PI_F * 0.875f) return '-';
    if (a > PI_F * 0.375f && a < PI_F * 0.625f) return '|';
    return (ang > 0.0f) == (fabsf(ang) < PI_F * 0.5f) ? '\\' : '/';
}

static void drift_update(void *self, double dt, double t) {
    drift_t *d = self;
    float fdt = (float)dt, ft = (float)t;
    float decay = expf(-fdt * 1.6f);
    size_t n = (size_t)d->cols * (size_t)d->rows;
    for (size_t i = 0; i < n; i++) d->trail[i] *= decay;
    d->vstr *= expf(-fdt * 0.5f);

    float speed_sum = 0.0f;
    for (int i = 0; i < d->count; i++) {
        mote_t *m = &d->m[i];
        m->age += fdt;
        if (m->age >= m->life) {
            respawn(d, m);
            continue;
        }
        float vx, vy;
        curl2(m->x * 0.035f, m->y * 0.07f, ft * 0.05f, &vx, &vy);
        vx *= 9.0f;
        vy *= 5.0f;
        if (d->vstr > 0.01f) {
            float dx = m->x - (float)d->vcx, dy = (m->y - (float)d->vcy) * 2.0f;
            float r2 = dx * dx + dy * dy + 12.0f;
            vx += -dy / r2 * 120.0f * d->vstr;
            vy += dx / r2 * 60.0f * d->vstr;
        }
        m->x += vx * m->speed * fdt;
        m->y += vy * m->speed * fdt;
        speed_sum += sqrtf(vx * vx + vy * vy);
        int ix = (int)m->x, iy = (int)m->y;
        if (ix < 0 || iy < 0 || ix >= d->cols || iy >= d->rows) {
            respawn(d, m);
            continue;
        }
        float life_f = m->age / m->life;
        float env = sinf(PI_F * life_f);
        size_t idx = (size_t)iy * (size_t)d->cols + (size_t)ix;
        if (env > d->trail[idx]) d->trail[idx] = env;
        d->dir[idx] = (uint8_t)dir_glyph(vx, vy * 2.0f);
        d->hue[idx] = 0.5f + 0.5f * sinf(atan2f(vy, vx));
    }
    d->mean_speed = speed_sum / (float)d->count;

    if (t > d->next_chime) {
        d->next_chime = t + (double)RR(3.0f, 7.0f);
        audio_trigger(AUDIO_CHIME, RR(0.0f, 0.6f), RR(-0.7f, 0.7f), 0.25f);
    }
}

static void drift_draw(void *self, canvas_t *c, double t) {
    drift_t *d = self;
    (void)t;
    for (int y = 0; y < d->rows; y++) {
        for (int x = 0; x < d->cols; x++) {
            size_t i = (size_t)y * (size_t)d->cols + (size_t)x;
            float v = d->trail[i];
            if (v < 0.04f) continue;
            uint32_t g = d->dir[i] ? d->dir[i] : '.';
            if (v < 0.15f) g = '.';
            canvas_put(c, x, y, g, v * 0.9f, d->hue[i]);
            if (v > 0.6f) canvas_glow_add(c, x, y, v * 0.5f);
        }
    }
}

static void drift_poke(void *self, int x, int y, int strength) {
    drift_t *d = self;
    d->vcx = x;
    d->vcy = y;
    d->vstr = 1.0f + (float)strength * 0.3f;
    audio_trigger(AUDIO_BELL, RR(0.0f, 0.5f), pan_for(x, d->cols), 0.4f);
}

static float drift_energy(void *self) { return clampf(((drift_t *)self)->mean_speed / 12.0f, 0.1f, 1.0f); }

const scene_def_t scene_drift_def = {
    .name = "drift",
    .blurb = "currents of light following a flow field",
    .bed = BED_DRIFT,
    .create = drift_create,
    .resize = drift_resize,
    .update = drift_update,
    .draw = drift_draw,
    .poke = drift_poke,
    .energy = drift_energy,
    .destroy = drift_destroy,
};
