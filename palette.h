#ifndef ASMR_PALETTE_H
#define ASMR_PALETTE_H

#include "common.h"

typedef struct {
    const char *name;
    rgb_t bg_top, bg_bottom;
    rgb_t ink_a, ink_b;
    rgb_t accent;
} palette_t;

int palette_count(void);
const palette_t *palette_get(int idx);
int palette_find(const char *name);
void palette_random(palette_t *out, float hue);
void palette_blend(const palette_t *a, const palette_t *b, float t, palette_t *out);

rgb_t rgb_make(int r, int g, int b);
rgb_t rgb_mix(rgb_t a, rgb_t b, float t);
rgb_t rgb_scale(rgb_t c, float k);
rgb_t rgb_add(rgb_t a, rgb_t b, float k);
rgb_t hsv_to_rgb(float h, float s, float v);
float rgb_luma(rgb_t c);

#endif
