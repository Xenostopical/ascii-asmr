#ifndef ASMR_COMMON_H
#define ASMR_COMMON_H

#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define APP_NAME    "ASCII ASMR"
#define APP_VERSION "1.1.0"
#define SITE_URL    "https://gg.elitehub.page.gd/"
#define SITE_LABEL  "Visit my site"

#define ARRAY_LEN(a) (sizeof(a) / sizeof((a)[0]))
#define TAU  6.28318530717958647692f
#define PI_F 3.14159265358979323846f

typedef struct {
    uint8_t r, g, b;
} rgb_t;

static inline float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
static inline int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }
static inline int mini(int a, int b) { return a < b ? a : b; }
static inline int maxi(int a, int b) { return a > b ? a : b; }
static inline float saturatef(float v) { return clampf(v, 0.0f, 1.0f); }
static inline float lerpf(float a, float b, float t) { return a + (b - a) * t; }
static inline float fractf(float v) { return v - floorf(v); }

static inline float smoothstepf(float e0, float e1, float x) {
    /* Equal edges are a step, not a division by zero. */
    if (e0 == e1) return x < e0 ? 0.0f : 1.0f;
    float t = saturatef((x - e0) / (e1 - e0));
    return t * t * (3.0f - 2.0f * t);
}

static inline float remapf(float v, float a, float b, float c, float d) {
    if (a == b) return v < a ? c : d;
    return c + (d - c) * ((v - a) / (b - a));
}

double now_seconds(void);
void sleep_seconds(double s);
size_t utf8_encode(uint32_t cp, char *out);
size_t str_copy(char *dst, size_t cap, const char *src);

void *xmalloc(size_t n);
void *xcalloc(size_t n, size_t size);
void *xrealloc(void *p, size_t n);

#endif
