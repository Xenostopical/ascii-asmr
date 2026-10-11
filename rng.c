#include "rng.h"

static rng_t g_rng = {0x9E3779B97F4A7C15ULL, 0xD1B54A32D192ED03ULL};
static uint8_t g_perm[512];

static uint64_t splitmix64(uint64_t *x) {
    uint64_t z = (*x += 0x9E3779B97F4A7C15ULL);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
}

static inline uint64_t rotl64(uint64_t x, int k) { return (x << k) | (x >> (64 - k)); }

void rng_seed(rng_t *r, uint64_t seed) {
    uint64_t x = seed ? seed : 0xC0FFEE1234ULL;
    r->s0 = splitmix64(&x);
    r->s1 = splitmix64(&x);
}

uint64_t rng_next(rng_t *r) {
    uint64_t s0 = r->s0, s1 = r->s1;
    uint64_t result = s0 + s1;
    s1 ^= s0;
    r->s0 = rotl64(s0, 24) ^ s1 ^ (s1 << 16);
    r->s1 = rotl64(s1, 37);
    return result;
}

float rng_float(rng_t *r) { return (float)(rng_next(r) >> 40) * (1.0f / 16777216.0f); }

float rng_range(rng_t *r, float lo, float hi) { return lo + (hi - lo) * rng_float(r); }

int rng_int(rng_t *r, int n) {
    if (n <= 1) return 0;
    return (int)(rng_next(r) % (uint64_t)n);
}

bool rng_chance(rng_t *r, float p) { return rng_float(r) < p; }

float rng_gauss(rng_t *r) {
    float u1 = rng_float(r);
    float u2 = rng_float(r);
    if (u1 < 1e-7f) u1 = 1e-7f;
    return sqrtf(-2.0f * logf(u1)) * cosf(TAU * u2);
}

rng_t *rng_global(void) { return &g_rng; }

uint32_t hash_u32(uint32_t x) {
    x ^= x >> 16;
    x *= 0x7FEB352Du;
    x ^= x >> 15;
    x *= 0x846CA68Bu;
    x ^= x >> 16;
    return x;
}

float hash_float(uint32_t x) { return (float)(hash_u32(x) >> 8) * (1.0f / 16777216.0f); }

void noise_init(uint64_t seed) {
    rng_t r;
    rng_seed(&r, seed ^ 0xA5A5A5A5ULL);
    for (int i = 0; i < 256; i++) g_perm[i] = (uint8_t)i;
    for (int i = 255; i > 0; i--) {
        int j = rng_int(&r, i + 1);
        uint8_t tmp = g_perm[i];
        g_perm[i] = g_perm[j];
        g_perm[j] = tmp;
    }
    for (int i = 0; i < 256; i++) g_perm[256 + i] = g_perm[i];
}

static inline float fade(float t) { return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f); }

static inline float grad2(int h, float x, float y) {
    switch (h & 7) {
    case 0: return x + y;
    case 1: return -x + y;
    case 2: return x - y;
    case 3: return -x - y;
    case 4: return x;
    case 5: return -x;
    case 6: return y;
    default: return -y;
    }
}

static inline float grad3(int h, float x, float y, float z) {
    h &= 15;
    float u = h < 8 ? x : y;
    float v = h < 4 ? y : ((h == 12 || h == 14) ? x : z);
    return ((h & 1) ? -u : u) + ((h & 2) ? -v : v);
}

float noise2(float x, float y) {
    int xi = (int)floorf(x), yi = (int)floorf(y);
    float xf = x - (float)xi, yf = y - (float)yi;
    xi &= 255;
    yi &= 255;
    float u = fade(xf), v = fade(yf);
    int aa = g_perm[g_perm[xi] + yi];
    int ab = g_perm[g_perm[xi] + yi + 1];
    int ba = g_perm[g_perm[xi + 1] + yi];
    int bb = g_perm[g_perm[xi + 1] + yi + 1];
    float x1 = lerpf(grad2(aa, xf, yf), grad2(ba, xf - 1.0f, yf), u);
    float x2 = lerpf(grad2(ab, xf, yf - 1.0f), grad2(bb, xf - 1.0f, yf - 1.0f), u);
    return lerpf(x1, x2, v);
}

float noise1(float x) { return noise2(x, 0.37f); }

float noise3(float x, float y, float z) {
    int xi = (int)floorf(x), yi = (int)floorf(y), zi = (int)floorf(z);
    float xf = x - (float)xi, yf = y - (float)yi, zf = z - (float)zi;
    xi &= 255;
    yi &= 255;
    zi &= 255;
    float u = fade(xf), v = fade(yf), w = fade(zf);
    int a = g_perm[xi] + yi, aa = g_perm[a] + zi, ab = g_perm[a + 1] + zi;
    int b = g_perm[xi + 1] + yi, ba = g_perm[b] + zi, bb = g_perm[b + 1] + zi;
    float l1 = lerpf(grad3(g_perm[aa], xf, yf, zf), grad3(g_perm[ba], xf - 1, yf, zf), u);
    float l2 = lerpf(grad3(g_perm[ab], xf, yf - 1, zf), grad3(g_perm[bb], xf - 1, yf - 1, zf), u);
    float l3 = lerpf(grad3(g_perm[aa + 1], xf, yf, zf - 1), grad3(g_perm[ba + 1], xf - 1, yf, zf - 1), u);
    float l4 = lerpf(grad3(g_perm[ab + 1], xf, yf - 1, zf - 1), grad3(g_perm[bb + 1], xf - 1, yf - 1, zf - 1), u);
    return lerpf(lerpf(l1, l2, v), lerpf(l3, l4, v), w);
}

float fbm2(float x, float y, int octaves) {
    float sum = 0.0f, amp = 1.0f, norm = 0.0f;
    for (int i = 0; i < octaves; i++) {
        sum += noise2(x, y) * amp;
        norm += amp;
        amp *= 0.5f;
        x *= 2.03f;
        y *= 2.03f;
    }
    return norm > 0.0f ? sum / norm : 0.0f;
}

float fbm3(float x, float y, float z, int octaves) {
    float sum = 0.0f, amp = 1.0f, norm = 0.0f;
    for (int i = 0; i < octaves; i++) {
        sum += noise3(x, y, z) * amp;
        norm += amp;
        amp *= 0.5f;
        x *= 2.03f;
        y *= 2.03f;
        z *= 1.7f;
    }
    return norm > 0.0f ? sum / norm : 0.0f;
}

void curl2(float x, float y, float t, float *vx, float *vy) {
    const float e = 0.02f;
    float dy = noise3(x, y + e, t) - noise3(x, y - e, t);
    float dx = noise3(x + e, y, t) - noise3(x - e, y, t);
    *vx = dy / (2.0f * e);
    *vy = -dx / (2.0f * e);
}
