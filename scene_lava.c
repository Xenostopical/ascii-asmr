#include "rng.h"
#include "scene.h"

#include <stdlib.h>
#include <string.h>

/* Metaball lava lamp. Positions live in physical units (x in columns, y in
 * half-rows) so blobs stay round on tall terminal cells. */

#define MAXB 9

typedef struct {
    float x, y, vx, vy, r;
} ball_t;

typedef struct {
    int cols, rows, count;
    ball_t b[MAXB];
    bool near[MAXB][MAXB];
    float energy;
} lava_t;

static void add_ball(lava_t *s, float x, float y) {
    if (s->count >= MAXB) return;
    float base = clampf((float)s->cols * 0.08f, 5.0f, 12.0f);
    s->b[s->count++] = (ball_t){x, y, RR(-2.0f, 2.0f), RR(-1.5f, 1.5f), base * RR(0.7f, 1.2f)};
}

static void lava_setup(lava_t *s, int cols, int rows) {
    s->cols = cols;
    s->rows = rows;
    s->count = 0;
    memset(s->near, 0, sizeof s->near);
    for (int i = 0; i < 6; i++) add_ball(s, RR(8.0f, (float)cols - 8.0f), RR(6.0f, (float)rows * 2.0f - 6.0f));
}

static void *lava_create(int cols, int rows) {
    lava_t *s = xcalloc(1, sizeof *s);
    lava_setup(s, cols, rows);
    return s;
}

static void lava_resize(void *self, int cols, int rows) { lava_setup(self, cols, rows); }
static void lava_destroy(void *self) { free(self); }

static void lava_update(void *self, double dt, double t) {
    lava_t *s = self;
    float fdt = (float)dt, ft = (float)t;
    float maxy = (float)s->rows * 2.0f, maxx = (float)s->cols;
    float speed_sum = 0.0f;
    for (int i = 0; i < s->count; i++) {
        ball_t *b = &s->b[i];
        b->vx += noise2((float)i * 3.1f, ft * 0.2f) * fdt * 3.0f;
        b->vy += noise2((float)i * 5.7f + 9.0f, ft * 0.2f) * fdt * 3.0f;
        float sp = sqrtf(b->vx * b->vx + b->vy * b->vy);
        if (sp > 4.0f) {
            b->vx *= 4.0f / sp;
            b->vy *= 4.0f / sp;
        }
        b->x += b->vx * fdt;
        b->y += b->vy * fdt;
        if (b->x < b->r * 0.5f || b->x > maxx - b->r * 0.5f) b->vx = -b->vx;
        if (b->y < b->r * 0.5f || b->y > maxy - b->r * 0.5f) b->vy = -b->vy;
        b->x = clampf(b->x, 1.0f, maxx - 1.0f);
        b->y = clampf(b->y, 1.0f, maxy - 1.0f);
        speed_sum += sp;
    }
    s->energy = s->count ? speed_sum / (float)s->count / 4.0f : 0.3f;

    for (int i = 0; i < s->count; i++) {
        for (int j = i + 1; j < s->count; j++) {
            float dx = s->b[i].x - s->b[j].x, dy = s->b[i].y - s->b[j].y;
            bool now_near = sqrtf(dx * dx + dy * dy) < (s->b[i].r + s->b[j].r) * 0.9f;
            if (now_near && !s->near[i][j]) audio_trigger(AUDIO_BUBBLE, RR(0.0f, 0.6f), pan_for((int)s->b[i].x, s->cols), 0.35f);
            s->near[i][j] = now_near;
        }
    }
}

static void lava_draw(void *self, canvas_t *c, double t) {
    lava_t *s = self;
    (void)t;
    for (int y = 0; y < s->rows; y++) {
        float py = ((float)y + 0.5f) * 2.0f;
        for (int x = 0; x < s->cols; x++) {
            float px = (float)x + 0.5f, f = 0.0f;
            for (int i = 0; i < s->count; i++) {
                float dx = px - s->b[i].x, dy = py - s->b[i].y;
                f += s->b[i].r * s->b[i].r / (dx * dx + dy * dy + 1.0f);
            }
            if (f < 0.5f) {
                canvas_glow_add(c, x, y, f * 0.35f);
                continue;
            }
            float v = saturatef((f - 0.5f) / 1.5f);
            canvas_put_ramp(c, x, y, " .:-=+*#%@", 0.12f + 0.88f * v, v);
            canvas_glow_add(c, x, y, v * 0.7f);
        }
    }
}

static void lava_poke(void *self, int x, int y, int strength) {
    lava_t *s = self;
    float px = (float)x, py = (float)y * 2.0f;
    if (s->count < MAXB) {
        add_ball(s, px, py);
    } else {
        int best = 0;
        float bd = 1e9f;
        for (int i = 0; i < s->count; i++) {
            float dx = s->b[i].x - px, dy = s->b[i].y - py;
            if (dx * dx + dy * dy < bd) {
                bd = dx * dx + dy * dy;
                best = i;
            }
        }
        s->b[best].vx += RR(-3.0f, 3.0f) * (float)strength * 0.5f;
        s->b[best].vy += RR(-3.0f, 3.0f) * (float)strength * 0.5f;
    }
    audio_trigger(AUDIO_BUBBLE, RR(0.2f, 0.8f), pan_for(x, s->cols), 0.5f);
}

static float lava_energy(void *self) { return clampf(((lava_t *)self)->energy, 0.2f, 1.0f); }

const scene_def_t scene_lava_def = {
    .name = "lava",
    .blurb = "slow glowing blobs in a lava lamp",
    .bed = BED_LAVA,
    .create = lava_create,
    .resize = lava_resize,
    .update = lava_update,
    .draw = lava_draw,
    .poke = lava_poke,
    .energy = lava_energy,
    .destroy = lava_destroy,
};
