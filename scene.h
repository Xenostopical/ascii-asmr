#ifndef ASMR_SCENE_H
#define ASMR_SCENE_H

#include "audio.h"
#include "canvas.h"
#include "common.h"

typedef struct {
    const char *name;
    const char *blurb;
    audio_bed_t bed;
    void *(*create)(int cols, int rows);
    void (*resize)(void *self, int cols, int rows);
    void (*update)(void *self, double dt, double t);
    void (*draw)(void *self, canvas_t *c, double t);
    void (*poke)(void *self, int x, int y, int strength);
    float (*energy)(void *self);
    void (*destroy)(void *self);
} scene_def_t;

typedef struct {
    const scene_def_t *def;
    void *state;
    int index;
    int cols, rows;
} scene_instance_t;

extern const scene_def_t scene_rain_def;
extern const scene_def_t scene_tide_def;
extern const scene_def_t scene_stars_def;
extern const scene_def_t scene_ripples_def;
extern const scene_def_t scene_drift_def;
extern const scene_def_t scene_fireflies_def;
extern const scene_def_t scene_snow_def;
extern const scene_def_t scene_aurora_def;
extern const scene_def_t scene_embers_def;
extern const scene_def_t scene_storm_def;
extern const scene_def_t scene_sakura_def;
extern const scene_def_t scene_deepsea_def;
extern const scene_def_t scene_lanterns_def;
extern const scene_def_t scene_lava_def;
extern const scene_def_t scene_window_def;
extern const scene_def_t scene_cave_def;
extern const scene_def_t scene_chimes_def;
extern const scene_def_t scene_zen_def;
extern const scene_def_t scene_pendulum_def;
extern const scene_def_t scene_galaxy_def;
extern const scene_def_t scene_bonsai_def;
extern const scene_def_t scene_digital_def;

int scene_count(void);
const scene_def_t *scene_def(int index);
int scene_find(const char *name);

void scene_start(scene_instance_t *s, int index, int cols, int rows);
void scene_stop(scene_instance_t *s);
void scene_resize(scene_instance_t *s, int cols, int rows);
void scene_update(scene_instance_t *s, double dt, double t);
void scene_draw(scene_instance_t *s, canvas_t *c, double t);
void scene_poke(scene_instance_t *s, int x, int y, int strength);
float scene_energy(const scene_instance_t *s);

/* Pan from a column: -1 (left) .. 1 (right). */
static inline float pan_for(int x, int cols) {
    return cols > 1 ? ((float)x / (float)(cols - 1)) * 2.0f - 1.0f : 0.0f;
}

#endif
