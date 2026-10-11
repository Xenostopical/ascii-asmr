#include "journey.h"

#include "palette.h"
#include "scene.h"

typedef struct {
    const char *scene;
    const char *palette;
    double seconds;
} step_t;

static const step_t STEPS[] = {
    {"rain", "tide", 150},       {"ripples", "glacier", 120}, {"fireflies", "mossgold", 150},
    {"sakura", "sakura", 120},   {"lanterns", "dusk", 150},   {"stars", "midnight", 120},
    {"aurora", "glacier", 150},  {"snow", "glacier", 120},    {"deepsea", "tide", 150},
    {"embers", "ember", 180},    {"lava", "ember", 120},      {"storm", "midnight", 150},
    {"tide", "dusk", 150},       {"drift", "lichen", 120},
    {"window", "glacier", 120},  {"cave", "midnight", 150},   {"chimes", "sakura", 120},
    {"zen", "mossgold", 150},   {"pendulum", "dusk", 120},   {"galaxy", "midnight", 150},
    {"bonsai", "mossgold", 150}, {"digital", "lichen", 120},
};

static bool g_active = false;
static int g_step = -1;
static double g_left = 0.0;

void journey_start(void) {
    g_active = true;
    g_step = -1;
    g_left = 0.0;
}

void journey_stop(void) { g_active = false; }
bool journey_active(void) { return g_active; }
int journey_step(void) { return g_step < 0 ? 0 : g_step; }
int journey_steps(void) { return (int)ARRAY_LEN(STEPS); }

bool journey_update(double dt, int *scene_idx, int *palette_idx) {
    if (!g_active) return false;
    g_left -= dt;
    if (g_left > 0.0) return false;
    g_step = (g_step + 1) % (int)ARRAY_LEN(STEPS);
    g_left = STEPS[g_step].seconds;
    int s = scene_find(STEPS[g_step].scene);
    int p = palette_find(STEPS[g_step].palette);
    *scene_idx = s < 0 ? 0 : s;
    *palette_idx = p < 0 ? 0 : p;
    return true;
}
