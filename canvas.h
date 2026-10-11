#ifndef ASMR_CANVAS_H
#define ASMR_CANVAS_H

#include "common.h"
#include "palette.h"

typedef struct {
    uint32_t glyph;
    float val;
    float tint;
} cell_t;

typedef struct {
    uint32_t glyph;
    rgb_t fg, bg;
    float alpha;
    bool active;
} ocell_t;

typedef struct {
    int cols, rows;
    cell_t *cells;
    ocell_t *over;
    float *extra;
    float *blur;
    uint32_t *prev_fg, *prev_bg, *prev_glyph;
    bool force;
    float master;
    float vignette;
} canvas_t;

void canvas_init(canvas_t *c, int cols, int rows);
void canvas_resize(canvas_t *c, int cols, int rows);
void canvas_free(canvas_t *c);
void canvas_invalidate(canvas_t *c);

void canvas_clear(canvas_t *c);
void canvas_clear_overlay(canvas_t *c);

void canvas_put(canvas_t *c, int x, int y, uint32_t glyph, float val, float tint);
void canvas_put_ramp(canvas_t *c, int x, int y, const char *ramp, float val, float tint);
void canvas_glow_add(canvas_t *c, int x, int y, float amount);
void canvas_glow_point(canvas_t *c, float cx, float cy, float rx, float ry, float strength);

void canvas_text(canvas_t *c, int x, int y, const char *s, rgb_t fg, rgb_t bg, float alpha);
void canvas_overlay_glyph(canvas_t *c, int x, int y, uint32_t glyph, rgb_t fg, rgb_t bg, float alpha);
void canvas_overlay_rect(canvas_t *c, int x, int y, int w, int h, rgb_t bg, float alpha);

void canvas_render(canvas_t *c, const palette_t *pal);
/* Writes base.ans (colour) and base.txt (plain) from the last rendered frame. */
bool canvas_save_ansi(const canvas_t *c, const char *base);

#endif
