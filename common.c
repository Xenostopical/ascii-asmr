#include "common.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

double now_seconds(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

void sleep_seconds(double s) {
    if (s <= 0.0) return;
    struct timespec ts;
    ts.tv_sec = (time_t)s;
    ts.tv_nsec = (long)((s - (double)ts.tv_sec) * 1e9);
    while (nanosleep(&ts, &ts) == -1 && errno == EINTR) {
    }
}

size_t utf8_encode(uint32_t cp, char *out) {
    /* Never emit invalid UTF-8 (surrogates and codepoints above Unicode). */
    if (cp > 0x10FFFFu || (cp >= 0xD800u && cp <= 0xDFFFu)) cp = 0xFFFDu;
    if (cp < 0x80) {
        out[0] = (char)cp;
        return 1;
    }
    if (cp < 0x800) {
        out[0] = (char)(0xC0 | (cp >> 6));
        out[1] = (char)(0x80 | (cp & 0x3F));
        return 2;
    }
    if (cp < 0x10000) {
        out[0] = (char)(0xE0 | (cp >> 12));
        out[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
        out[2] = (char)(0x80 | (cp & 0x3F));
        return 3;
    }
    out[0] = (char)(0xF0 | (cp >> 18));
    out[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
    out[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
    out[3] = (char)(0x80 | (cp & 0x3F));
    return 4;
}

size_t str_copy(char *dst, size_t cap, const char *src) {
    if (cap == 0) return 0;
    size_t n = strlen(src);
    if (n >= cap) n = cap - 1;
    memcpy(dst, src, n);
    dst[n] = '\0';
    return n;
}

static void oom(void) {
    fprintf(stderr, "asmr: out of memory\n");
    exit(1);
}

void *xmalloc(size_t n) {
    void *p = malloc(n ? n : 1);
    if (!p) oom();
    return p;
}

void *xcalloc(size_t n, size_t size) {
    void *p = calloc(n ? n : 1, size ? size : 1);
    if (!p) oom();
    return p;
}

void *xrealloc(void *p, size_t n) {
    void *q = realloc(p, n ? n : 1);
    if (!q) oom();
    return q;
}
