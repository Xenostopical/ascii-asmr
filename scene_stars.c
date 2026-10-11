#include "rng.h"
#include "scene.h"

#include <stdlib.h>
#include <string.h>

#define MAX_SHOOT 6

typedef struct {
    int x, y, kind;
    float phase, speed, base;
    bool peaked;
} star_t;

typedef struct {
    float x, y, vx, vy, life, max_life;
    int len;
    bool alive;
} shoot_t;

typedef struct {
    int cols, rows, count;
    star_t *stars;
    shoot_t shoot[MAX_SHOOT];
    double next_shoot, last_chime;
    float twinkle_avg;
} stars_t;

static void stars_alloc(stars_t *s, int cols, int rows) {
    free(s->stars);
    s->cols = cols;
    s->rows = rows;
    s->count = maxi(8, cols * rows / 22);
    s->stars = xcalloc((size_t)s->count, sizeof(star_t));
    for (int i = 0; i < s->count; i++) {
        star_t *st = &s->stars[i];
        st->x = RI(cols);
        st->y = RI(rows);
        float roll = RF();
        st->kind = roll < 0.72f ? 0 : (roll < 0.94f ? 1 : 2);
        st->phase = RR(0.0f, TAU);
        st->speed = RR(0.4f, 2.2f);
        st->base = st->kind == 0 ? RR(0.18f, 0.4f) : (st->kind == 1 ? RR(0.4f, 0.7f) : RR(0.7f, 1.0f));
    }
}

static void *stars_create(int cols, int rows) {
    stars_t *s = xcalloc(1, sizeof *s);
    stars_alloc(s, cols, rows);
    s->next_shoot = 3.0;
    return s;
}

static void stars_resize(void *self, int cols, int rows) { stars_alloc(self, cols, rows); }

static void stars_destroy(void *self) {
    stars_t *s = self;
    free(s->stars);
    free(s);
}

static void launch_shooting(stars_t *s, float x, float y) {
    for (int i = 0; i < MAX_SHOOT; i++) {
        shoot_t *m = &s->shoot[i];
        if (m->alive) continue;
        float ang = RR(0.25f, 0.6f);
        float sp = RR(45.0f, 75.0f);
        m->x = x;
        m->y = y;
        m->vx = cosf(ang) * sp * (RC(0.8f) ? 1.0f : -1.0f);
        m->vy = sinf(ang) * sp * 0.5f;
        m->max_life = m->life = RR(0.5f, 1.0f);
        m->len = 5 + RI(5);
        m->alive = true;
        audio_trigger(AUDIO_WHOOSH, RF(), pan_for((int)x, s->cols), 0.35f);
        return;
    }
}

static void stars_update(void *self, double dt, double t) {
    stars_t *s = self;
    float fdt = (float)dt, ft = (float)t;
    float sum = 0.0f;
    for (int i = 0; i < s->count; i++) {
        star_t *st = &s->stars[i];
        float tw = 0.5f + 0.5f * sinf(st->phase + ft * st->speed);
        sum += tw;
        if (tw > 0.985f && st->kind > 0 && !st->peaked && t - s->last_chime > 0.35 && RC(0.45f)) {
            s->last_chime = t;
            audio_trigger(AUDIO_CHIME, 1.0f - (float)st->y / (float)s->rows, pan_for(st->x, s->cols), 0.35f);
        }
        st->peaked = tw > 0.95f;
    }
    s->twinkle_avg = sum / (float)s->count;

    if (t > s->next_shoot) {
        s->next_shoot = t + (double)RR(4.0f, 11.0f);
        launch_shooting(s, RR(0.0f, (float)s->cols * 0.8f), RR(0.0f, (float)s->rows * 0.4f));
    }
    for (int i = 0; i < MAX_SHOOT; i++) {
        shoot_t *m = &s->shoot[i];
        if (!m->alive) continue;
        m->x += m->vx * fdt;
        m->y += m->vy * fdt;
        m->life -= fdt;
        if (m->life <= 0.0f || m->x < -10.0f || m->x > (float)s->cols + 10.0f || m->y > (float)s->rows) m->alive = false;
    }
}

static void stars_draw(void *self, canvas_t *c, double t) {
    stars_t *s = self;
    float ft = (float)t;

    for (int y = 0; y < s->rows; y++) {
        for (int x = 0; x < s->cols; x++) {
            float n = fbm2((float)x * 0.035f + ft * 0.01f, (float)y * 0.07f, 3);
            float v = (n - 0.05f) * 1.1f;
            if (v > 0.06f) canvas_put_ramp(c, x, y, " .:", v * 0.5f, 0.15f + 0.3f * v);
        }
    }

    for (int i = 0; i < s->count; i++) {
        const star_t *st = &s->stars[i];
        float tw = 0.5f + 0.5f * sinf(st->phase + ft * st->speed);
        float v = st->base * (0.35f + 0.65f * tw * tw);
        static const char glyphs[3] = {'.', '+', '*'};
        canvas_put(c, st->x, st->y, (uint32_t)glyphs[st->kind], v, 0.2f + 0.7f * st->base);
        if (st->kind == 2 && tw > 0.7f) {
            float arm = v * 0.55f;
            canvas_put(c, st->x - 1, st->y, '-', arm, 0.8f);
            canvas_put(c, st->x + 1, st->y, '-', arm, 0.8f);
            canvas_put(c, st->x, st->y - 1, '|', arm, 0.8f);
            canvas_put(c, st->x, st->y + 1, '|', arm, 0.8f);
            canvas_glow_point(c, (float)st->x, (float)st->y, 4.0f, 2.0f, 0.4f * tw);
        }
    }

    for (int i = 0; i < MAX_SHOOT; i++) {
        const shoot_t *m = &s->shoot[i];
        if (!m->alive) continue;
        float f = m->life / m->max_life;
        float dx = m->vx, dy = m->vy;
        float norm = sqrtf(dx * dx + dy * dy);
        dx /= norm;
        dy /= norm;
        static const char trail[] = "*+:.";
        for (int k = 0; k < m->len; k++) {
            int idx = k * 3 / m->len;
            if (idx > 3) idx = 3;
            float v = f * (1.0f - (float)k / (float)m->len);
            canvas_put(c, (int)roundf(m->x - dx * (float)k), (int)roundf(m->y - dy * (float)k * 0.5f), (uint32_t)trail[idx],
                       v, 0.95f);
        }
        canvas_glow_point(c, m->x, m->y, 5.0f, 2.5f, 0.6f * f);
    }
}

static void stars_poke(void *self, int x, int y, int strength) {
    stars_t *s = self;
    launch_shooting(s, (float)x, (float)y);
    audio_trigger(AUDIO_CHIME, RR(0.3f, 1.0f), pan_for(x, s->cols), 0.5f);
    if (strength > 2) audio_trigger(AUDIO_BELL, RR(0.0f, 0.5f), pan_for(x, s->cols), 0.4f);
}

static float stars_energy(void *self) { return 0.3f + 0.6f * ((stars_t *)self)->twinkle_avg; }

const scene_def_t scene_stars_def = {
    .name = "stars",
    .blurb = "a slow field of twinkling stars",
    .bed = BED_STARS,
    .create = stars_create,
    .resize = stars_resize,
    .update = stars_update,
    .draw = stars_draw,
    .poke = stars_poke,
    .energy = stars_energy,
    .destroy = stars_destroy,
};
