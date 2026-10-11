#include "rng.h"
#include "scene.h"

#include <stdlib.h>
#include <string.h>

#define MAX_L 48

typedef struct {
    float x, y, vy, sway, flick, tint;
    bool alive;
} lan_t;

typedef struct {
    int cols, rows, water;
    lan_t l[MAX_L];
    double next_chime;
} lanterns_t;

static void launch(lanterns_t *s, lan_t *l, bool anywhere) {
    l->x = RR(4.0f, (float)s->cols - 4.0f);
    l->y = anywhere ? RR(2.0f, (float)s->water) : (float)s->water - 1.0f;
    l->vy = RR(1.0f, 2.5f);
    l->sway = RR(0.0f, TAU);
    l->flick = RR(0.0f, TAU);
    l->tint = RR(0.5f, 1.0f);
    l->alive = true;
}

static void lanterns_setup(lanterns_t *s, int cols, int rows) {
    s->cols = cols;
    s->rows = rows;
    s->water = maxi(6, rows - 4);
    memset(s->l, 0, sizeof s->l);
    int n = clampi(cols / 7, 6, 20);
    for (int i = 0; i < n; i++) launch(s, &s->l[i], true);
}

static void *lanterns_create(int cols, int rows) {
    lanterns_t *s = xcalloc(1, sizeof *s);
    lanterns_setup(s, cols, rows);
    s->next_chime = 5.0;
    return s;
}

static void lanterns_resize(void *self, int cols, int rows) { lanterns_setup(self, cols, rows); }
static void lanterns_destroy(void *self) { free(self); }

static void lanterns_update(void *self, double dt, double t) {
    lanterns_t *s = self;
    float fdt = (float)dt;
    for (int i = 0; i < MAX_L; i++) {
        lan_t *l = &s->l[i];
        if (!l->alive) continue;
        l->sway += fdt * 0.7f;
        l->flick += fdt * 6.0f;
        l->y -= l->vy * fdt;
        l->x += sinf(l->sway) * 0.8f * fdt + noise2(l->sway, (float)i) * 0.4f * fdt;
        if (l->y < -3.0f) launch(s, l, false);
    }
    if (t > s->next_chime) {
        s->next_chime = t + (double)RR(5.0f, 11.0f);
        audio_trigger(AUDIO_BELL, RR(0.1f, 0.8f), RR(-0.6f, 0.6f), 0.22f);
    }
}

static void lanterns_draw(void *self, canvas_t *c, double t) {
    lanterns_t *s = self;
    float ft = (float)t;
    for (int i = 0; i < s->cols * s->water / 35; i++) {
        int x = (int)(hash_u32((uint32_t)i * 2u + 3u) % (uint32_t)s->cols);
        int y = (int)(hash_u32((uint32_t)i * 2u + 4u) % (uint32_t)s->water);
        float tw = 0.5f + 0.5f * sinf(ft * (0.4f + hash_float((uint32_t)i)) + (float)i);
        canvas_put(c, x, y, '.', 0.12f + 0.16f * tw, 0.3f);
    }
    for (int y = s->water; y < s->rows; y++) {
        for (int x = 0; x < s->cols; x++) {
            float w = 0.5f + 0.5f * sinf((float)x * 0.3f + (float)y * 1.3f + ft * 1.2f);
            canvas_put_ramp(c, x, y, " .-~", 0.1f + 0.3f * w, 0.2f);
        }
    }
    for (int i = 0; i < MAX_L; i++) {
        const lan_t *l = &s->l[i];
        if (!l->alive) continue;
        float fl = 0.75f + 0.25f * sinf(l->flick);
        int x = (int)l->x, y = (int)l->y;
        canvas_put(c, x, y, '^', 0.6f * fl, l->tint);
        canvas_put(c, x - 1, y + 1, '(', 0.7f * fl, l->tint);
        canvas_put(c, x, y + 1, '@', 1.0f * fl, l->tint);
        canvas_put(c, x + 1, y + 1, ')', 0.7f * fl, l->tint);
        canvas_put(c, x - 1, y + 2, '[', 0.55f * fl, l->tint);
        canvas_put(c, x, y + 2, '=', 0.55f * fl, l->tint);
        canvas_put(c, x + 1, y + 2, ']', 0.55f * fl, l->tint);
        canvas_glow_point(c, (float)x, (float)y + 1.0f, 8.0f, 4.0f, 0.85f * fl);
        int ry = s->water + 1 + (int)((float)(s->water - y) * 0.25f);
        int rx = x + (int)lroundf(sinf(ft * 1.5f + (float)i) * 1.5f);
        if (ry < s->rows) {
            canvas_put(c, rx, ry, '~', 0.45f * fl, l->tint);
            canvas_glow_point(c, (float)rx, (float)ry, 5.0f, 1.5f, 0.3f * fl);
        }
    }
}

static void lanterns_poke(void *self, int x, int y, int strength) {
    lanterns_t *s = self;
    (void)y;
    for (int n = 0; n < 1 + strength / 2; n++) {
        for (int i = 0; i < MAX_L; i++) {
            if (s->l[i].alive) continue;
            launch(s, &s->l[i], false);
            s->l[i].x = (float)x + RR(-3.0f, 3.0f);
            break;
        }
    }
    audio_trigger(AUDIO_BELL, RR(0.2f, 0.9f), pan_for(x, s->cols), 0.35f);
}

static float lanterns_energy(void *self) {
    (void)self;
    return 0.4f;
}

const scene_def_t scene_lanterns_def = {
    .name = "lanterns",
    .blurb = "sky lanterns drifting above dark water",
    .bed = BED_LANTERN,
    .create = lanterns_create,
    .resize = lanterns_resize,
    .update = lanterns_update,
    .draw = lanterns_draw,
    .poke = lanterns_poke,
    .energy = lanterns_energy,
    .destroy = lanterns_destroy,
};
