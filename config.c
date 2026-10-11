#include "config.h"

#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static bool config_path(char *out, size_t cap, bool make_dirs) {
    const char *xdg = getenv("XDG_CONFIG_HOME");
    const char *home = getenv("HOME");
    char dir[400];
    int n;
    if (xdg && *xdg) {
        if (make_dirs && mkdir(xdg, 0755) != 0 && errno != EEXIST) return false;
        n = snprintf(dir, sizeof dir, "%s/asmr", xdg);
    } else if (home && *home) {
        if (make_dirs) {
            char parent[400];
            n = snprintf(parent, sizeof parent, "%s/.config", home);
            if (n < 0 || (size_t)n >= sizeof parent) return false;
            if (mkdir(parent, 0755) != 0 && errno != EEXIST) return false;
        }
        n = snprintf(dir, sizeof dir, "%s/.config/asmr", home);
    } else {
        return false;
    }
    if (n < 0 || (size_t)n >= sizeof dir) return false;
    if (make_dirs && mkdir(dir, 0755) != 0 && errno != EEXIST) return false;
    n = snprintf(out, cap, "%s/config", dir);
    return n >= 0 && (size_t)n < cap;
}

void config_defaults(config_t *c) {
    if (!c) return;
    c->volume = 0.8f;
    c->speed = 1.0f;
    c->autoshuffle = true;
    c->sound = true;
}

static char *trim(char *s) {
    while (isspace((unsigned char)*s)) s++;
    char *end = s + strlen(s);
    while (end > s && isspace((unsigned char)end[-1])) *--end = '\0';
    return s;
}

static bool parse_float_value(const char *s, float *out) {
    char *end = NULL;
    errno = 0;
    float v = strtof(s, &end);
    if (s == end || errno == ERANGE || !isfinite(v)) return false;
    while (*end && isspace((unsigned char)*end)) end++;
    if (*end) return false;
    *out = v;
    return true;
}

static bool parse_bool_value(const char *s, bool *out) {
    if (!strcmp(s, "1") || !strcmp(s, "true") || !strcmp(s, "yes") || !strcmp(s, "on")) {
        *out = true;
        return true;
    }
    if (!strcmp(s, "0") || !strcmp(s, "false") || !strcmp(s, "no") || !strcmp(s, "off")) {
        *out = false;
        return true;
    }
    return false;
}

void config_load(config_t *c) {
    if (!c) return;
    config_defaults(c);
    char path[512];
    if (!config_path(path, sizeof path, false)) return;
    FILE *f = fopen(path, "r");
    if (!f) return;
    char line[256];
    while (fgets(line, sizeof line, f)) {
        char *entry = trim(line);
        if (!*entry || *entry == '#' || *entry == ';') continue;
        char *eq = strchr(entry, '=');
        if (!eq) continue;
        *eq = '\0';
        char *key = trim(entry);
        char *val = trim(eq + 1);
        if (!strcmp(key, "volume")) {
            float v;
            if (parse_float_value(val, &v)) c->volume = clampf(v, 0.0f, 1.0f);
        } else if (!strcmp(key, "speed")) {
            float v;
            if (parse_float_value(val, &v)) c->speed = clampf(v, 0.25f, 3.0f);
        } else if (!strcmp(key, "autoshuffle")) {
            bool v;
            if (parse_bool_value(val, &v)) c->autoshuffle = v;
        } else if (!strcmp(key, "sound")) {
            bool v;
            if (parse_bool_value(val, &v)) c->sound = v;
        }
    }
    fclose(f);
}

void config_save(const config_t *c) {
    if (!c) return;
    char path[512];
    if (!config_path(path, sizeof path, true)) return;
    FILE *f = fopen(path, "w");
    if (!f) return;
    float volume = isfinite(c->volume) ? clampf(c->volume, 0.0f, 1.0f) : 0.8f;
    float speed = isfinite(c->speed) ? clampf(c->speed, 0.25f, 3.0f) : 1.0f;
    int ok = fprintf(f, "volume=%.2f\nspeed=%.2f\nautoshuffle=%d\nsound=%d\n", (double)volume, (double)speed,
                     c->autoshuffle ? 1 : 0, c->sound ? 1 : 0);
    int close_status = fclose(f);
    (void)ok;
    (void)close_status;
}
