#include "rng.h"
#include "scene.h"

#include <stdlib.h>
#include <string.h>

#define MAX_DRIPS 24
#define MAX_RINGS 24
#define MAX_ECHO 16
#define MAX_TIPS 96
#define CRYSTALS 8

typedef struct {
    float x, y, vy;
    bool alive;
} cdrip_t;

typedef struct {
    float x, age;
    bool alive;
} ring_t;

typedef struct {
    float delay, pitch, pan, vel;
    bool alive;
} echo_t;

typedef struct {
    int x, y;
    float phase, tint, prev;
} crystal_t;

typedef struct {
    int cols, rows, floor_y, ntips;
    int *top;
    int tips[MAX_TIPS];
    cdrip_t drip[MAX_DRIPS];
    ring_t ring[MAX_RINGS];
    echo_t echo[MAX_ECHO];
    crystal_t cr[CRYSTALS];
    double next_drip;
    float glow;
} cave_t;

static void cave_setup(cave_t *s, int cols, int rows) {
    free(s->top);
    s->cols = cols;
    s->rows = rows;
    s->floor_y = maxi(rows - 4, rows / 2);
    s->top = xcalloc((size_t)cols, sizeof(int));
    int maxh = maxi(2, (int)((float)s->floor_y * 0.3f));
    for (int x = 0; x < cols; x++) s->top[x] = 1 + (int)((float)maxh * (0.5f + 0.5f * fbm2((float)x * 0.07f, 3.0f, 3)));
    s->ntips = 0;
    for (int x = 1; x < cols - 1 && s->ntips < MAX_TIPS; x++) {
        if (s->top[x] >= s->top[x - 1] && s->top[x] >= s->top[x + 1] && s->top[x] > 2) s->tips[s->ntips++] = x;
    }
    memset(s->drip, 0, sizeof s->drip);
    memset(s->ring, 0, sizeof s->ring);
    memset(s->echo, 0, sizeof s->echo);
    for (int i = 0; i < CRYSTALS; i++) {
        s->cr[i].x = 3 + RI(maxi(1, cols - 6));
        s->cr[i].y = s->floor_y - 1 - RI(2);
        s->cr[i].phase = RR(0.0f, TAU);
        s->cr[i].tint = RR(0.3f, 1.0f);
    }
}

static void *cave_create(int cols, int rows) {
    cave_t *s = xcalloc(1, sizeof *s);
    cave_setup(s, cols, rows);
    s->next_drip = 1.0;
    return s;
}

static void cave_resize(void *self, int cols, int rows) { cave_setup(self, cols, rows); }

static void cave_destroy(void *self) {
    cave_t *s = self;
    free(s->top);
    free(s);
}

static void schedule_echo(cave_t *s, float delay, float pitch, float pan, float vel) {
    for (int i = 0; i < MAX_ECHO; i++) {
        if (s->echo[i].alive) continue;
        s->echo[i] = (echo_t){delay, pitch, pan, vel, true};
        return;
    }
}

static void spawn_drip(cave_t *s, int x, int y) {
    for (int i = 0; i < MAX_DRIPS; i++) {
        if (s->drip[i].alive) continue;
        s->drip[i] = (cdrip_t){(float)x, (float)y, 0.0f, true};
        return;
    }
}

static void cave_update(void *self, double dt, double t) {
    cave_t *s = self;
    float fdt = (float)dt, ft = (float)t;

    if (t > s->next_drip && s->ntips > 0) {
        s->next_drip = t + (double)RR(0.6f, 2.2f);
        int x = s->tips[RI(s->ntips)];
        spawn_drip(s, x, s->top[x] + 1);
    }
    for (int i = 0; i < MAX_DRIPS; i++) {
        cdrip_t *d = &s->drip[i];
        if (!d->alive) continue;
        d->vy += 22.0f * fdt;
        d->y += d->vy * fdt;
        if (d->y >= (float)s->floor_y) {
            d->alive = false;
            for (int r = 0; r < MAX_RINGS; r++) {
                if (s->ring[r].alive) continue;
                s->ring[r] = (ring_t){d->x, 0.0f, true};
                break;
            }
            float pitch = 1.0f - d->x / (float)s->cols, pan = pan_for((int)d->x, s->cols);
            audio_trigger(AUDIO_DRIP, pitch, pan, 0.5f);
            schedule_echo(s, 0.28f, pitch, -pan, 0.25f);
            schedule_echo(s, 0.56f, pitch, pan * 0.5f, 0.12f);
        }
    }
    for (int i = 0; i < MAX_RINGS; i++) {
        if (!s->ring[i].alive) continue;
        s->ring[i].age += fdt;
        if (s->ring[i].age > 1.6f) s->ring[i].alive = false;
    }
    for (int i = 0; i < MAX_ECHO; i++) {
        echo_t *e = &s->echo[i];
        if (!e->alive) continue;
        e->delay -= fdt;
        if (e->delay <= 0.0f) {
            audio_trigger(AUDIO_DRIP, e->pitch, e->pan, e->vel);
            e->alive = false;
        }
    }
    float glow = 0.0f;
    for (int i = 0; i < CRYSTALS; i++) {
        crystal_t *c = &s->cr[i];
        c->phase += fdt * (0.5f + 0.1f * (float)i);
        float pulse = 0.5f + 0.5f * sinf(c->phase);
        if (c->prev < 0.97f && pulse >= 0.97f)
            audio_trigger(AUDIO_CHIME, 1.0f - (float)c->x / (float)s->cols, pan_for(c->x, s->cols), 0.16f);
        c->prev = pulse;
        glow += pulse;
    }
    s->glow = glow / (float)CRYSTALS;
    (void)ft;
}

static void cave_draw(void *self, canvas_t *c, double t) {
    cave_t *s = self;
    float ft = (float)t;
    for (int x = 0; x < s->cols; x++) {
        int h = s->top[x];
        for (int y = 0; y < h; y++) canvas_put(c, x, y, y >= h - 2 ? '|' : '#', 0.3f - 0.02f * (float)(h - y), 0.1f);
        canvas_put(c, x, h, 'v', 0.4f, 0.15f);
        if (hash_float((uint32_t)x * 31u + 7u) < 0.12f) {
            int sh = 1 + (int)(hash_float((uint32_t)x * 17u) * 3.0f);
            for (int k = 1; k <= sh; k++) canvas_put(c, x, s->floor_y - k, k == sh ? '^' : 'A', 0.28f, 0.1f);
        }
    }
    for (int y = s->floor_y; y < s->rows; y++) {
        for (int x = 0; x < s->cols; x++) {
            float w = 0.5f + 0.5f * sinf((float)x * 0.4f + ft * 0.8f + (float)y * 1.7f);
            canvas_put(c, x, y, w > 0.6f ? '~' : '-', 0.12f + 0.14f * w, 0.25f);
        }
    }
    for (int i = 0; i < MAX_RINGS; i++) {
        const ring_t *r = &s->ring[i];
        if (!r->alive) continue;
        float rad = r->age * 8.0f, fade = 1.0f - r->age / 1.6f;
        for (int row = 0; row < 2; row++) {
            int y = s->floor_y + row;
            float k = rad * (1.0f - 0.3f * (float)row);
            canvas_put(c, (int)(r->x - k), y, '~', 0.8f * fade, 0.8f);
            canvas_put(c, (int)(r->x + k), y, '~', 0.8f * fade, 0.8f);
        }
    }
    for (int y = s->floor_y / 3; y < s->floor_y; y++) {
        for (int x = 0; x < s->cols; x++) {
            float m = noise2((float)x * 0.08f + ft * 0.05f, (float)y * 0.2f);
            if (m > 0.35f) canvas_put(c, x, y, '.', 0.12f * (m - 0.3f) * 3.0f, 0.4f);
        }
    }
    for (int i = 0; i < MAX_DRIPS; i++) {
        const cdrip_t *d = &s->drip[i];
        if (d->alive) canvas_put(c, (int)d->x, (int)d->y, d->vy > 8.0f ? ':' : '.', 0.8f, 0.8f);
    }
    for (int i = 0; i < CRYSTALS; i++) {
        const crystal_t *cr = &s->cr[i];
        float pulse = 0.5f + 0.5f * sinf(cr->phase);
        canvas_put(c, cr->x, cr->y, '*', 0.55f + 0.45f * pulse, cr->tint);
        canvas_put(c, cr->x - 1, cr->y + 1, '/', 0.4f + 0.3f * pulse, cr->tint);
        canvas_put(c, cr->x + 1, cr->y + 1, '\\', 0.4f + 0.3f * pulse, cr->tint);
        canvas_glow_point(c, (float)cr->x, (float)cr->y, 7.0f, 3.5f, 0.9f * pulse);
    }
}

static void cave_poke(void *self, int x, int y, int strength) {
    cave_t *s = self;
    (void)strength;
    spawn_drip(s, clampi(x, 0, s->cols - 1), clampi(y, 0, s->floor_y - 1));
}

static float cave_energy(void *self) { return 0.3f + 0.5f * ((cave_t *)self)->glow; }

const scene_def_t scene_cave_def = {
    .name = "cave",
    .blurb = "glowing crystals and echoing drips",
    .bed = BED_CAVE,
    .create = cave_create,
    .resize = cave_resize,
    .update = cave_update,
    .draw = cave_draw,
    .poke = cave_poke,
    .energy = cave_energy,
    .destroy = cave_destroy,
};
