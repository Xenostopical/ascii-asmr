#include "scene.h"

#include <string.h>

static const scene_def_t *const REGISTRY[] = {
    &scene_rain_def,   &scene_tide_def,      &scene_stars_def, &scene_ripples_def,
    &scene_drift_def,  &scene_fireflies_def, &scene_snow_def,  &scene_aurora_def,
    &scene_embers_def, &scene_storm_def,     &scene_sakura_def, &scene_deepsea_def,
    &scene_lanterns_def, &scene_lava_def,   &scene_window_def, &scene_cave_def,
    &scene_chimes_def,   &scene_zen_def,    &scene_pendulum_def, &scene_galaxy_def,
    &scene_bonsai_def,   &scene_digital_def,
};

int scene_count(void) { return (int)ARRAY_LEN(REGISTRY); }

const scene_def_t *scene_def(int index) {
    int n = scene_count();
    index %= n;
    if (index < 0) index += n;
    return REGISTRY[index];
}

int scene_find(const char *name) {
    for (int i = 0; i < scene_count(); i++) {
        if (strcmp(REGISTRY[i]->name, name) == 0) return i;
    }
    return -1;
}

void scene_start(scene_instance_t *s, int index, int cols, int rows) {
    int n = scene_count();
    index %= n;
    if (index < 0) index += n;
    s->def = REGISTRY[index];
    s->index = index;
    s->cols = cols;
    s->rows = rows;
    s->state = s->def->create(cols, rows);
}

void scene_stop(scene_instance_t *s) {
    if (s->def && s->state) s->def->destroy(s->state);
    s->state = NULL;
    s->def = NULL;
}

void scene_resize(scene_instance_t *s, int cols, int rows) {
    s->cols = cols;
    s->rows = rows;
    if (s->def && s->def->resize) s->def->resize(s->state, cols, rows);
}

void scene_update(scene_instance_t *s, double dt, double t) {
    if (s->def) s->def->update(s->state, dt, t);
}

void scene_draw(scene_instance_t *s, canvas_t *c, double t) {
    if (s->def) s->def->draw(s->state, c, t);
}

void scene_poke(scene_instance_t *s, int x, int y, int strength) {
    if (s->def && s->def->poke) s->def->poke(s->state, x, y, strength);
}

float scene_energy(const scene_instance_t *s) {
    if (s->def && s->def->energy) return s->def->energy(s->state);
    return 0.5f;
}
