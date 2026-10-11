#include "palette.h"

#include <string.h>

#define RGB(r, g, b) {(r), (g), (b)}

static const palette_t PALETTES[] = {
    {"lichen",   RGB(7, 16, 12),  RGB(26, 43, 32),  RGB(168, 201, 150), RGB(236, 214, 150), RGB(196, 224, 164)},
    {"dusk",     RGB(12, 10, 22), RGB(42, 31, 61),  RGB(176, 166, 224), RGB(246, 184, 206), RGB(214, 190, 240)},
    {"tide",     RGB(5, 15, 20),  RGB(15, 48, 56),  RGB(120, 196, 206), RGB(214, 240, 228), RGB(150, 220, 226)},
    {"ember",    RGB(18, 9, 7),   RGB(58, 29, 20),  RGB(222, 156, 112), RGB(252, 224, 164), RGB(246, 184, 128)},
    {"sakura",   RGB(20, 10, 16), RGB(58, 28, 44),  RGB(236, 160, 190), RGB(255, 226, 214), RGB(248, 186, 206)},
    {"glacier",  RGB(6, 12, 22),  RGB(22, 44, 70),  RGB(150, 190, 240), RGB(226, 244, 255), RGB(176, 212, 250)},
    {"mossgold", RGB(10, 12, 6),  RGB(40, 44, 18),  RGB(190, 200, 110), RGB(246, 226, 140), RGB(214, 220, 126)},
    {"midnight", RGB(4, 4, 12),   RGB(20, 18, 48),  RGB(120, 130, 230), RGB(190, 240, 250), RGB(150, 160, 244)},
};

int palette_count(void) { return (int)ARRAY_LEN(PALETTES); }

const palette_t *palette_get(int idx) {
    int n = palette_count();
    idx %= n;
    if (idx < 0) idx += n;
    return &PALETTES[idx];
}

int palette_find(const char *name) {
    for (int i = 0; i < palette_count(); i++) {
        if (strcmp(PALETTES[i].name, name) == 0) return i;
    }
    return -1;
}

rgb_t rgb_make(int r, int g, int b) {
    rgb_t c;
    c.r = (uint8_t)clampi(r, 0, 255);
    c.g = (uint8_t)clampi(g, 0, 255);
    c.b = (uint8_t)clampi(b, 0, 255);
    return c;
}

rgb_t rgb_mix(rgb_t a, rgb_t b, float t) {
    t = saturatef(t);
    return rgb_make((int)(a.r + (b.r - a.r) * t + 0.5f), (int)(a.g + (b.g - a.g) * t + 0.5f),
                    (int)(a.b + (b.b - a.b) * t + 0.5f));
}

rgb_t rgb_scale(rgb_t c, float k) {
    return rgb_make((int)(c.r * k + 0.5f), (int)(c.g * k + 0.5f), (int)(c.b * k + 0.5f));
}

rgb_t rgb_add(rgb_t a, rgb_t b, float k) {
    return rgb_make((int)(a.r + b.r * k + 0.5f), (int)(a.g + b.g * k + 0.5f), (int)(a.b + b.b * k + 0.5f));
}

float rgb_luma(rgb_t c) { return (0.2126f * c.r + 0.7152f * c.g + 0.0722f * c.b) / 255.0f; }

rgb_t hsv_to_rgb(float h, float s, float v) {
    h = fractf(h) * 6.0f;
    int i = (int)floorf(h);
    float f = h - (float)i;
    float p = v * (1.0f - s), q = v * (1.0f - s * f), t = v * (1.0f - s * (1.0f - f));
    float r, g, b;
    switch (i % 6) {
    case 0: r = v; g = t; b = p; break;
    case 1: r = q; g = v; b = p; break;
    case 2: r = p; g = v; b = t; break;
    case 3: r = p; g = q; b = v; break;
    case 4: r = t; g = p; b = v; break;
    default: r = v; g = p; b = q; break;
    }
    return rgb_make((int)(r * 255.0f + 0.5f), (int)(g * 255.0f + 0.5f), (int)(b * 255.0f + 0.5f));
}

void palette_blend(const palette_t *a, const palette_t *b, float t, palette_t *out) {
    t = saturatef(t);
    out->name = t < 0.5f ? a->name : b->name;
    out->bg_top = rgb_mix(a->bg_top, b->bg_top, t);
    out->bg_bottom = rgb_mix(a->bg_bottom, b->bg_bottom, t);
    out->ink_a = rgb_mix(a->ink_a, b->ink_a, t);
    out->ink_b = rgb_mix(a->ink_b, b->ink_b, t);
    out->accent = rgb_mix(a->accent, b->accent, t);
}

void palette_random(palette_t *out, float hue) {
    out->name = "random";
    out->ink_a = hsv_to_rgb(hue, 0.42f, 0.86f);
    out->ink_b = hsv_to_rgb(hue + 0.09f, 0.30f, 0.99f);
    out->accent = hsv_to_rgb(hue + 0.03f, 0.38f, 0.93f);
    out->bg_top = hsv_to_rgb(hue + 0.02f, 0.55f, 0.07f);
    out->bg_bottom = hsv_to_rgb(hue + 0.05f, 0.50f, 0.20f);
}
