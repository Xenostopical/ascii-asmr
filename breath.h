#ifndef ASMR_BREATH_H
#define ASMR_BREATH_H

#include "canvas.h"
#include "palette.h"

void breath_toggle(void);
bool breath_active(void);
void breath_update(double dt);
void breath_draw(canvas_t *c, const palette_t *pal, int cols, int rows);

#endif
