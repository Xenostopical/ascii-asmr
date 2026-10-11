#include "canvas.h"

#include "term.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void alloc_buffers(canvas_t *c, int cols, int rows) {
    size_t n = (size_t)cols * (size_t)rows;
    c->cols = cols;
    c->rows = rows;
    c->cells = xcalloc(n, sizeof(cell_t));
    c->over = xcalloc(n, sizeof(ocell_t));
    c->extra = xcalloc(n, sizeof(float));
    c->blur = xcalloc(n, sizeof(float));
    c->prev_fg = xcalloc(n, sizeof(uint32_t));
    c->prev_bg = xcalloc(n, sizeof(uint32_t));
    c->prev_glyph = xcalloc(n, sizeof(uint32_t));
    for (size_t i = 0; i < n; i++) c->cells[i].glyph = ' ';
    c->force = true;
}

static void free_buffers(canvas_t *c) {
    free(c->cells);
    free(c->over);
    free(c->extra);
    free(c->blur);
    free(c->prev_fg);
    free(c->prev_bg);
    free(c->prev_glyph);
}

void canvas_init(canvas_t *c, int cols, int rows) {
    memset(c, 0, sizeof *c);
    alloc_buffers(c, maxi(cols, 1), maxi(rows, 1));
    c->master = 1.0f;
    c->vignette = 0.55f;
}

void canvas_resize(canvas_t *c, int cols, int rows) {
    float master = c->master, vig = c->vignette;
    free_buffers(c);
    alloc_buffers(c, maxi(cols, 1), maxi(rows, 1));
    c->master = master;
    c->vignette = vig;
}

void canvas_free(canvas_t *c) {
    free_buffers(c);
    memset(c, 0, sizeof *c);
}

void canvas_invalidate(canvas_t *c) { c->force = true; }

void canvas_clear(canvas_t *c) {
    size_t n = (size_t)c->cols * (size_t)c->rows;
    for (size_t i = 0; i < n; i++) {
        c->cells[i].glyph = ' ';
        c->cells[i].val = 0.0f;
        c->cells[i].tint = 0.0f;
        c->extra[i] = 0.0f;
    }
}

void canvas_clear_overlay(canvas_t *c) {
    memset(c->over, 0, (size_t)c->cols * (size_t)c->rows * sizeof(ocell_t));
}

void canvas_put(canvas_t *c, int x, int y, uint32_t glyph, float val, float tint) {
    if (x < 0 || y < 0 || x >= c->cols || y >= c->rows) return;
    cell_t *e = &c->cells[y * c->cols + x];
    if (val >= e->val) {
        e->glyph = glyph;
        e->val = val;
        e->tint = tint;
    }
}

void canvas_put_ramp(canvas_t *c, int x, int y, const char *ramp, float val, float tint) {
    size_t n = strlen(ramp);
    if (n < 2) return;
    int idx = (int)(saturatef(val) * (float)(n - 1) + 0.5f);
    if (ramp[idx] == ' ') return;
    canvas_put(c, x, y, (uint32_t)(unsigned char)ramp[idx], val, tint);
}

void canvas_glow_add(canvas_t *c, int x, int y, float amount) {
    if (x < 0 || y < 0 || x >= c->cols || y >= c->rows) return;
    c->extra[y * c->cols + x] += amount;
}

void canvas_glow_point(canvas_t *c, float cx, float cy, float rx, float ry, float strength) {
    if (!c || !c->cells || !isfinite(cx) || !isfinite(cy) || !isfinite(rx) || !isfinite(ry) ||
        !isfinite(strength) || rx <= 0.0f || ry <= 0.0f || strength <= 0.0f)
        return;
    /* Avoid unsafe float-to-int conversions for off-screen or extreme points. */
    if (cx + rx < 0.0f || cy + ry < 0.0f || cx - rx >= (float)c->cols || cy - ry >= (float)c->rows) return;
    cx = clampf(cx, -2.0f * (float)c->cols, 3.0f * (float)c->cols);
    cy = clampf(cy, -2.0f * (float)c->rows, 3.0f * (float)c->rows);
    /* Keep accidental huge radii from turning one pointer event into a full-screen stall. */
    rx = fminf(rx, (float)maxi(c->cols, 1) * 2.0f);
    ry = fminf(ry, (float)maxi(c->rows, 1) * 2.0f);
    int x0 = (int)floorf(cx - rx), x1 = (int)ceilf(cx + rx);
    int y0 = (int)floorf(cy - ry), y1 = (int)ceilf(cy + ry);
    for (int y = y0; y <= y1; y++) {
        for (int x = x0; x <= x1; x++) {
            float dx = ((float)x - cx) / rx, dy = ((float)y - cy) / ry;
            float d = sqrtf(dx * dx + dy * dy);
            if (d >= 1.0f) continue;
            float f = 1.0f - d;
            canvas_glow_add(c, x, y, strength * f * f);
        }
    }
}

void canvas_overlay_glyph(canvas_t *c, int x, int y, uint32_t glyph, rgb_t fg, rgb_t bg, float alpha) {
    if (x < 0 || y < 0 || x >= c->cols || y >= c->rows) return;
    ocell_t *o = &c->over[y * c->cols + x];
    o->glyph = glyph;
    o->fg = fg;
    o->bg = bg;
    o->alpha = alpha;
    o->active = true;
}

void canvas_text(canvas_t *c, int x, int y, const char *s, rgb_t fg, rgb_t bg, float alpha) {
    for (; *s; s++, x++) canvas_overlay_glyph(c, x, y, (uint32_t)(unsigned char)*s, fg, bg, alpha);
}

void canvas_overlay_rect(canvas_t *c, int x, int y, int w, int h, rgb_t bg, float alpha) {
    for (int yy = y; yy < y + h; yy++) {
        for (int xx = x; xx < x + w; xx++) {
            if (xx < 0 || yy < 0 || xx >= c->cols || yy >= c->rows) continue;
            ocell_t *o = &c->over[yy * c->cols + xx];
            o->active = true;
            o->bg = bg;
            o->alpha = alpha;
            o->glyph = 0;
        }
    }
}

static void compute_blur(canvas_t *c) {
    int cols = c->cols, rows = c->rows;
    for (int y = 0; y < rows; y++) {
        for (int x = 0; x < cols; x++) {
            float sum = 0.0f;
            for (int dy = -1; dy <= 1; dy++) {
                int yy = y + dy;
                if (yy < 0 || yy >= rows) continue;
                for (int dx = -1; dx <= 1; dx++) {
                    int xx = x + dx;
                    if (xx < 0 || xx >= cols) continue;
                    sum += c->cells[yy * cols + xx].val;
                }
            }
            c->blur[y * cols + x] = sum * (1.0f / 9.0f);
        }
    }
}

static inline uint32_t pack_rgb(rgb_t c) {
    return ((uint32_t)(c.r & 0xFE) << 16) | ((uint32_t)(c.g & 0xFE) << 8) | (uint32_t)(c.b & 0xFE);
}

static inline rgb_t unpack_rgb(uint32_t p) {
    rgb_t c;
    c.r = (uint8_t)((p >> 16) & 0xFF);
    c.g = (uint8_t)((p >> 8) & 0xFF);
    c.b = (uint8_t)(p & 0xFF);
    return c;
}

void canvas_render(canvas_t *c, const palette_t *pal) {
    const int cols = c->cols, rows = c->rows;
    compute_blur(c);

    rgb_t ink_mid = rgb_mix(pal->ink_a, pal->ink_b, 0.5f);
    int cur_x = -100, cur_y = -100;
    uint32_t cur_fg = 0xFFFFFFFFu, cur_bg = 0xFFFFFFFFu;

    for (int y = 0; y < rows; y++) {
        float fy = rows > 1 ? (float)y / (float)(rows - 1) : 0.0f;
        rgb_t base = rgb_mix(pal->bg_top, pal->bg_bottom, fy);
        float ndy = ((float)y + 0.5f) / (float)rows * 2.0f - 1.0f;

        for (int x = 0; x < cols; x++) {
            int idx = y * cols + x;
            float ndx = ((float)x + 0.5f) / (float)cols * 2.0f - 1.0f;
            float d = sqrtf(ndx * ndx * 0.55f + ndy * ndy);
            float vig = 1.0f - c->vignette * smoothstepf(0.55f, 1.25f, d);

            const cell_t *cell = &c->cells[idx];
            float glow = saturatef(c->extra[idx] * 0.9f + c->blur[idx] * 0.45f);
            rgb_t bg = rgb_add(base, ink_mid, glow * 0.20f * c->master);
            bg = rgb_scale(bg, vig);

            uint32_t glyph = ' ';
            rgb_t fg = bg;
            float v = cell->val * c->master;
            if (cell->glyph != ' ' && v > 0.03f) {
                rgb_t ink = rgb_mix(pal->ink_a, pal->ink_b, cell->tint);
                float k = 0.22f + 0.78f * saturatef(v);
                fg = rgb_mix(bg, rgb_scale(ink, 0.6f + 0.4f * vig), k);
                glyph = cell->glyph;
            }

            const ocell_t *o = &c->over[idx];
            if (o->active) {
                if (o->alpha > 0.0f) bg = rgb_mix(bg, o->bg, o->alpha);
                if (o->glyph != 0) {
                    glyph = o->glyph;
                    fg = o->fg;
                } else if (glyph == ' ') {
                    fg = bg;
                }
            }

            uint32_t pbg = pack_rgb(bg);
            uint32_t pfg = glyph == ' ' ? pbg : pack_rgb(fg);

            if (!c->force && c->prev_glyph[idx] == glyph && c->prev_fg[idx] == pfg && c->prev_bg[idx] == pbg)
                continue;
            c->prev_glyph[idx] = glyph;
            c->prev_fg[idx] = pfg;
            c->prev_bg[idx] = pbg;

            if (cur_x != x || cur_y != y) out_move(x, y);
            if (pbg != cur_bg) {
                out_bg(unpack_rgb(pbg));
                cur_bg = pbg;
            }
            if (glyph != ' ' && pfg != cur_fg) {
                out_fg(unpack_rgb(pfg));
                cur_fg = pfg;
            }
            out_glyph(glyph);
            cur_x = x + 1;
            cur_y = y;
        }
    }
    c->force = false;
    out_reset();
}

bool canvas_save_ansi(const canvas_t *c, const char *base) {
    char path[300];
    snprintf(path, sizeof path, "%s.ans", base);
    FILE *fa = fopen(path, "wb");
    snprintf(path, sizeof path, "%s.txt", base);
    FILE *ft = fopen(path, "wb");
    if (!fa || !ft) {
        if (fa) fclose(fa);
        if (ft) fclose(ft);
        return false;
    }
    for (int y = 0; y < c->rows; y++) {
        for (int x = 0; x < c->cols; x++) {
            int i = y * c->cols + x;
            rgb_t fg = unpack_rgb(c->prev_fg[i]), bg = unpack_rgb(c->prev_bg[i]);
            uint32_t g = c->prev_glyph[i] ? c->prev_glyph[i] : ' ';
            char u[4];
            size_t n = utf8_encode(g, u);
            fprintf(fa, "\x1b[38;2;%d;%d;%d;48;2;%d;%d;%dm", fg.r, fg.g, fg.b, bg.r, bg.g, bg.b);
            fwrite(u, 1, n, fa);
            fwrite(u, 1, n, ft);
        }
        fputs("\x1b[0m\n", fa);
        fputc('\n', ft);
    }
    fclose(fa);
    fclose(ft);
    return true;
}
