#include "breath.h"

#include "audio.h"

#include <stdio.h>

/* 4-7-8 breathing pacer: inhale 4s, hold 7s, exhale 8s. */
#define T_IN 4.0
#define T_HOLD 7.0
#define T_OUT 8.0
#define T_TOTAL (T_IN + T_HOLD + T_OUT)

static bool g_on = false;
static double g_t = 0.0;
static int g_last_phase = -1;

static int phase_of(double t) { return t < T_IN ? 0 : (t < T_IN + T_HOLD ? 1 : 2); }

static float level_of(double t) {
    if (t < T_IN) return smoothstepf(0.0f, (float)T_IN, (float)t);
    if (t < T_IN + T_HOLD) return 1.0f;
    return 1.0f - smoothstepf((float)(T_IN + T_HOLD), (float)T_TOTAL, (float)t);
}

void breath_toggle(void) {
    g_on = !g_on;
    g_t = 0.0;
    g_last_phase = -1;
}

bool breath_active(void) { return g_on; }

void breath_update(double dt) {
    if (!g_on) return;
    g_t += dt;
    if (g_t >= T_TOTAL) g_t -= T_TOTAL;
    int ph = phase_of(g_t);
    if (ph != g_last_phase) {
        g_last_phase = ph;
        audio_trigger(AUDIO_BELL, ph == 0 ? 0.1f : (ph == 1 ? 0.35f : 0.0f), 0.0f, 0.2f);
    }
}

static void ring(canvas_t *c, int cx, int cy, float r, uint32_t glyph, rgb_t fg) {
    int steps = (int)(TAU * r * 2.6f) + 24;
    for (int i = 0; i < steps; i++) {
        float a = TAU * (float)i / (float)steps;
        int x = cx + (int)lroundf(cosf(a) * r * 2.0f);
        int y = cy + (int)lroundf(sinf(a) * r);
        canvas_overlay_glyph(c, x, y, glyph, fg, fg, 0.0f);
    }
}

void breath_draw(canvas_t *c, const palette_t *pal, int cols, int rows) {
    if (!g_on) return;
    float lvl = level_of(g_t);
    int ph = phase_of(g_t);
    int cx = cols / 2, cy = rows / 2;
    float rmax = fminf((float)rows * 0.38f, (float)cols * 0.22f);
    if (rmax < 3.0f) return;
    float r = 2.0f + (rmax - 2.0f) * lvl;
    rgb_t col = rgb_mix(pal->ink_a, pal->ink_b, lvl);

    ring(c, cx, cy, r, 'o', col);
    ring(c, cx, cy, r * 0.72f, '.', rgb_scale(col, 0.7f));
    ring(c, cx, cy, rmax + 1.0f, '.', rgb_scale(pal->ink_a, 0.28f));

    static const char *names[3] = {"inhale", "hold", "exhale"};
    static const double starts[3] = {0.0, T_IN, T_IN + T_HOLD};
    static const double lens[3] = {T_IN, T_HOLD, T_OUT};
    int left = (int)(lens[ph] - (g_t - starts[ph])) + 1;
    char buf[32];
    snprintf(buf, sizeof buf, "%s %d", names[ph], left);
    canvas_text(c, cx - (int)(sizeof(buf) > 0 ? 4 : 0), cy, buf, pal->accent, pal->bg_top, 0.0f);
}
