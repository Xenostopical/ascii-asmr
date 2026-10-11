#ifndef ASMR_RNG_H
#define ASMR_RNG_H

#include "common.h"

typedef struct {
    uint64_t s0, s1;
} rng_t;

void rng_seed(rng_t *r, uint64_t seed);
uint64_t rng_next(rng_t *r);
float rng_float(rng_t *r);
float rng_range(rng_t *r, float lo, float hi);
int rng_int(rng_t *r, int n);
bool rng_chance(rng_t *r, float p);
float rng_gauss(rng_t *r);
rng_t *rng_global(void);

#define RF()     rng_float(rng_global())
#define RR(a, b) rng_range(rng_global(), (a), (b))
#define RI(n)    rng_int(rng_global(), (n))
#define RC(p)    rng_chance(rng_global(), (p))

uint32_t hash_u32(uint32_t x);
float hash_float(uint32_t x);

void noise_init(uint64_t seed);
float noise1(float x);
float noise2(float x, float y);
float noise3(float x, float y, float z);
float fbm2(float x, float y, int octaves);
float fbm3(float x, float y, float z, int octaves);
void curl2(float x, float y, float t, float *vx, float *vy);

#endif
