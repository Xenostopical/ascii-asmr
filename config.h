#ifndef ASMR_CONFIG_H
#define ASMR_CONFIG_H

#include "common.h"

typedef struct {
    float volume;
    float speed;
    bool autoshuffle;
    bool sound;
} config_t;

void config_defaults(config_t *c);
void config_load(config_t *c);
void config_save(const config_t *c);

#endif
