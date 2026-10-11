#ifndef ASMR_JOURNEY_H
#define ASMR_JOURNEY_H

#include "common.h"

void journey_start(void);
void journey_stop(void);
bool journey_active(void);
/* Returns true when a new step begins; fills the scene and palette to use. */
bool journey_update(double dt, int *scene_idx, int *palette_idx);
int journey_step(void);
int journey_steps(void);

#endif
