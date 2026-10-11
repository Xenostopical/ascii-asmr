#include "rng.h"
#include "scene.h"

#include <stdlib.h>
#include <string.h>

/* A bonsai that grows branch by branch, leafs out, drops its leaves, and
 * starts again as a different tree. */

#define MAX_SEG 480
#define MAX_LEAF 360
#define MAX_FALL 160
#define CYCLE 70.0f

typedef struct {
    float x0, y0, x1, y1, start, dur;
    int depth;
} bseg_t;

typedef struct {
    float x, y, start;
    int kind;
    bool fallen;
} leaf_t;

typedef struct {
    float x, y, vy, ph;
    bool alive;
} fall_t;

typedef struct {
    int cols, rows, nseg, nleaf, maxd, cycle, next_fall;
    bseg_t seg[MAX_SEG];
    leaf_t leaf[MAX_LEAF];
    fall_t fall[MAX_FALL];
    float clock, prev_clock, wind;
} bonsai_t;

static void add_leaf(bonsai_t *b, float x, float y, float start) {
    if (b->nleaf >= MAX_LEAF) return;
    b->leaf[b->nleaf++] = (leaf_t){x, y, start, RI(3), false};
}

static void branch(bonsai_t *b, float x, float y, float ang, float len, int depth, float start) {
    if (b->nseg >= MAX_SEG) return;
    float x1 = x + sinf(ang) * len * 2.0f, y1 = y - cosf(ang) * len;
    if (y1 < 1.0f) y1 = 1.0f;
    float dur = 2.4f;
    b->seg[b->nseg++] = (bseg_t){x, y, x1, y1, start, dur, depth};
    float end = start + dur;
    if (depth >= b->maxd) {
        for (int i = 0; i < 3; i++) add_leaf(b, x1 + RR(-2.0f, 2.0f), y1 + RR(-1.0f, 1.0f), end + RR(0.0f, 1.5f));
        return;
    }
    int kids = (depth == 1 && RC(0.5f)) ? 3 : 2;
    for (int k = 0; k < kids; k++) {
        float a = ang + ((float)k - (float)(kids - 1) * 0.5f) * RR(0.45f, 0.8f) + RR(-0.15f, 0.15f);
        branch(b, x1, y1, a, len * RR(0.68f, 0.82f), depth + 1, end + 0.4f);
    }
}

static void regrow(bonsai_t *b) {
    b->nseg = 0;
    b->nleaf = 0;
    memset(b->fall, 0, sizeof b->fall);
    b->clock = 0.0f;
    b->maxd = b->rows >= 24 ? 6 : 4;
    branch(b, (float)b->cols * 0.5f, (float)(b->rows - 4), RR(-0.12f, 0.12f), (float)b->rows * 0.22f, 0, 0.0f);
}

static void *bonsai_create(int cols, int rows) {
    bonsai_t *b = xcalloc(1, sizeof *b);
    b->cols = cols;
    b->rows = rows;
    regrow(b);
    return b;
}

static void bonsai_resize(void *self, int cols, int rows) {
    bonsai_t *b = self;
    b->cols = cols;
    b->rows = rows;
    regrow(b);
}

static void bonsai_destroy(void *self) { free(self); }

static void drop_leaf(bonsai_t *b, const leaf_t *l) {
    fall_t *f = &b->fall[b->next_fall];
    b->next_fall = (b->next_fall + 1) % MAX_FALL;
    *f = (fall_t){l->x, l->y, RR(2.0f, 4.0f), RR(0.0f, TAU), true};
}

static void bonsai_update(void *self, double dt, double t) {
    bonsai_t *b = self;
    float fdt = (float)dt, ft = (float)t;
    b->prev_clock = b->clock;
    b->clock += fdt;
    b->wind = noise2(ft * 0.15f, 8.1f);
    if (b->clock >= CYCLE) {
        b->cycle++;
        regrow(b);
        return;
    }
    for (int i = 0; i < b->nleaf; i++) {
        leaf_t *l = &b->leaf[i];
        if (l->fallen) continue;
        if (l->start > b->prev_clock && l->start <= b->clock && RC(0.06f))
            audio_trigger(AUDIO_CHIME, 1.0f - l->y / (float)b->rows, pan_for((int)l->x, b->cols), 0.14f);
        float drop_rate = b->clock > 50.0f ? 0.5f : 0.015f * fmaxf(0.0f, b->wind);
        if (b->clock > l->start + 2.0f && RC(drop_rate * fdt)) {
            l->fallen = true;
            drop_leaf(b, l);
        }
    }
    for (int i = 0; i < MAX_FALL; i++) {
        fall_t *f = &b->fall[i];
        if (!f->alive) continue;
        f->ph += fdt * 2.0f;
        f->y += f->vy * fdt;
        f->x += (sinf(f->ph) * 1.5f + b->wind * 2.0f) * fdt;
        if (f->y >= (float)b->rows - 1.0f) f->alive = false;
    }
}

static void draw_segment(canvas_t *c, const bseg_t *s, float clock) {
    float f = saturatef((clock - s->start) / s->dur);
    if (f <= 0.0f) return;
    float dx = s->x1 - s->x0, dy = s->y1 - s->y0;
    int steps = (int)(fmaxf(fabsf(dx), fabsf(dy)) * f) + 1;
    uint32_t g = '|';
    if (fabsf(dx) > fabsf(dy) * 1.6f) g = '-';
    else if (fabsf(dx) > fabsf(dy) * 0.5f) g = dx > 0.0f ? '/' : '\\';
    float val = 0.55f - 0.04f * (float)s->depth;
    for (int i = 0; i <= steps; i++) {
        float k = (float)i / (float)maxi(1, (int)(fmaxf(fabsf(dx), fabsf(dy))));
        if (k > f) break;
        int x = (int)lroundf(s->x0 + dx * k), y = (int)lroundf(s->y0 + dy * k);
        canvas_put(c, x, y, g, val, 0.05f);
        if (s->depth <= 1) canvas_put(c, x + 1, y, '#', val * 0.9f, 0.05f);
    }
}

static void bonsai_draw(void *self, canvas_t *c, double t) {
    bonsai_t *b = self;
    (void)t;
    float season = 0.15f + 0.7f * (float)(b->cycle % 4) / 3.0f;
    for (int i = 0; i < b->nseg; i++) draw_segment(c, &b->seg[i], b->clock);
    static const char shapes[3] = {'*', 'o', '@'};
    for (int i = 0; i < b->nleaf; i++) {
        const leaf_t *l = &b->leaf[i];
        if (l->fallen || b->clock < l->start) continue;
        float grow = saturatef((b->clock - l->start) / 1.5f);
        int x = (int)lroundf(l->x + b->wind * 0.7f * sinf(l->x + b->clock));
        canvas_put(c, x, (int)lroundf(l->y), (uint32_t)shapes[l->kind], 0.35f + 0.4f * grow, season + 0.08f * (float)l->kind);
    }
    for (int i = 0; i < MAX_FALL; i++) {
        const fall_t *f = &b->fall[i];
        if (f->alive) canvas_put(c, (int)f->x, (int)f->y, '*', 0.55f, season + 0.1f);
    }
    int cx = b->cols / 2, py = b->rows - 3;
    for (int k = 0; k < 3 && py + k < b->rows; k++) {
        int half = 6 - k;
        for (int dx = -half; dx <= half; dx++) {
            uint32_t g = dx == -half ? '\\' : (dx == half ? '/' : (k == 2 ? '_' : '='));
            canvas_put(c, cx + dx, py + k, g, 0.5f, 0.05f);
        }
    }
    for (int dx = -5; dx <= 5; dx++) canvas_put(c, cx + dx, py, '~', 0.35f, 0.15f);
}

static void bonsai_poke(void *self, int x, int y, int strength) {
    bonsai_t *b = self;
    (void)y;
    int dropped = 0;
    for (int i = 0; i < b->nleaf && dropped < 8 + strength * 3; i++) {
        leaf_t *l = &b->leaf[i];
        if (l->fallen || b->clock < l->start || fabsf(l->x - (float)x) > 14.0f) continue;
        l->fallen = true;
        drop_leaf(b, l);
        dropped++;
    }
    audio_trigger(AUDIO_CHIME, RR(0.3f, 0.9f), pan_for(x, b->cols), 0.3f);
}

static float bonsai_energy(void *self) { return clampf(0.3f + 0.3f * fabsf(((bonsai_t *)self)->wind), 0.0f, 1.0f); }

const scene_def_t scene_bonsai_def = {
    .name = "bonsai",
    .blurb = "a tree growing, leafing, and letting go",
    .bed = BED_BONSAI,
    .create = bonsai_create,
    .resize = bonsai_resize,
    .update = bonsai_update,
    .draw = bonsai_draw,
    .poke = bonsai_poke,
    .energy = bonsai_energy,
    .destroy = bonsai_destroy,
};
