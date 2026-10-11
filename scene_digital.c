#include "rng.h"
#include "scene.h"

#include <stdlib.h>
#include <string.h>

/* Falling character columns. Every so often your site address surfaces from
 * the rain, letter by letter, then dissolves back into it. */

#define MAX_COLS 240
#define MSG_LEN 6.0f

typedef struct {
    float y, speed;
    int len;
} dcol_t;

typedef struct {
    int cols, rows;
    dcol_t c[MAX_COLS];
    uint8_t *glyph;
    float msg_age;
    double next_msg;
    int tick_budget;
} digital_t;

static const char GLYPHS[] = "01234567890abcdef:;<>+-*=/\\|[]{}#$%&@";

static void reset_col(digital_t *d, dcol_t *c, bool anywhere) {
    c->y = anywhere ? RR(-(float)d->rows, (float)d->rows) : -RR(0.0f, (float)d->rows * 0.5f);
    c->speed = RR(8.0f, 24.0f);
    c->len = 6 + RI(maxi(1, (int)((float)d->rows * 0.7f)));
}

static void digital_setup(digital_t *d, int cols, int rows) {
    free(d->glyph);
    d->cols = cols;
    d->rows = rows;
    d->glyph = xcalloc((size_t)cols * (size_t)rows, 1);
    for (size_t i = 0; i < (size_t)cols * (size_t)rows; i++) d->glyph[i] = (uint8_t)GLYPHS[RI((int)sizeof(GLYPHS) - 1)];
    for (int x = 0; x < mini(cols, MAX_COLS); x++) reset_col(d, &d->c[x], true);
    d->msg_age = -1.0f;
    d->next_msg = 8.0;
}

static void *digital_create(int cols, int rows) {
    digital_t *d = xcalloc(1, sizeof *d);
    digital_setup(d, cols, rows);
    return d;
}

static void digital_resize(void *self, int cols, int rows) { digital_setup(self, cols, rows); }

static void digital_destroy(void *self) {
    digital_t *d = self;
    free(d->glyph);
    free(d);
}

static void digital_update(void *self, double dt, double t) {
    digital_t *d = self;
    float fdt = (float)dt;
    d->tick_budget = 2;
    int ncols = mini(d->cols, MAX_COLS);
    for (int x = 0; x < ncols; x++) {
        dcol_t *c = &d->c[x];
        float before = c->y;
        c->y += c->speed * fdt;
        if ((int)before != (int)c->y && d->tick_budget > 0 && RC(0.012f)) {
            d->tick_budget--;
            audio_trigger(AUDIO_TICK, (float)x / (float)ncols, pan_for(x, d->cols), 0.1f);
        }
        if (c->y - (float)c->len > (float)d->rows) reset_col(d, c, false);
    }
    size_t n = (size_t)d->cols * (size_t)d->rows;
    for (size_t i = 0; i < n / 80 + 1; i++) d->glyph[RI((int)n)] = (uint8_t)GLYPHS[RI((int)sizeof(GLYPHS) - 1)];

    if (d->msg_age >= 0.0f) {
        d->msg_age += fdt;
        if (d->msg_age > MSG_LEN) {
            d->msg_age = -1.0f;
            d->next_msg = t + (double)RR(18.0f, 30.0f);
        }
    } else if (t > d->next_msg) {
        d->msg_age = 0.0f;
    }
}

static void digital_draw(void *self, canvas_t *c, double t) {
    digital_t *d = self;
    (void)t;
    int ncols = mini(d->cols, MAX_COLS);
    for (int x = 0; x < ncols; x++) {
        const dcol_t *col = &d->c[x];
        int head = (int)col->y;
        for (int k = 0; k < col->len; k++) {
            int y = head - k;
            if (y < 0 || y >= d->rows) continue;
            float f = 1.0f - (float)k / (float)col->len;
            float v = k == 0 ? 1.0f : powf(f, 1.4f) * 0.8f;
            canvas_put(c, x, y, d->glyph[y * d->cols + x], v, k == 0 ? 0.95f : 0.25f + 0.3f * f);
            if (k == 0) canvas_glow_add(c, x, y, 0.5f);
        }
    }
    if (d->msg_age >= 0.0f) {
        const char *msg = SITE_LABEL;
        int len = (int)strlen(msg);
        float env = sinf(PI_F * d->msg_age / MSG_LEN);
        int shown = (int)(saturatef(env * 1.6f) * (float)len + 0.5f);
        int x0 = (d->cols - len) / 2, y0 = d->rows / 2;
        for (int i = 0; i < shown && i < len; i++) {
            canvas_put(c, x0 + i, y0, (uint32_t)(unsigned char)msg[i], 1.05f, 0.95f);
            canvas_glow_add(c, x0 + i, y0, 0.9f * env);
        }
    }
}

static void digital_poke(void *self, int x, int y, int strength) {
    digital_t *d = self;
    (void)y;
    for (int dx = -3 - strength; dx <= 3 + strength; dx++) {
        int xx = x + dx;
        if (xx < 0 || xx >= mini(d->cols, MAX_COLS)) continue;
        d->c[xx].y = 0.0f;
        d->c[xx].speed = RR(20.0f, 34.0f);
    }
    audio_trigger(AUDIO_TICK, RR(0.0f, 1.0f), pan_for(x, d->cols), 0.4f);
}

static float digital_energy(void *self) {
    (void)self;
    return 0.4f;
}

const scene_def_t scene_digital_def = {
    .name = "digital",
    .blurb = "falling code that sometimes spells a name",
    .bed = BED_DIGITAL,
    .create = digital_create,
    .resize = digital_resize,
    .update = digital_update,
    .draw = digital_draw,
    .poke = digital_poke,
    .energy = digital_energy,
    .destroy = digital_destroy,
};
