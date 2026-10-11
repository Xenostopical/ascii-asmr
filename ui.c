#include "ui.h"

#include "term.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#define MAX_BOXES 64
#define BLOCK_GLYPH 0x2588u

typedef struct {
    int x, w;
    ui_action_t action;
    int arg;
} hitbox_t;

static struct {
    int cols, rows;
    char toast[96];
    double toast_left;
    bool help;
    double age;
    hitbox_t boxes[MAX_BOXES];
    int nboxes, dock_y;
    int link_x, link_y, link_w;
    bool link_visible;
    bool menu, ascii;
    float meter[8];
    int meter_tick;
    int menu_sel, menu_x, menu_y, menu_w, menu_start, menu_visible;
} U;

/* 3x5 bitmap font, rows top to bottom, bit 2 = left column. */
static const uint8_t FONT[26][5] = {
    {2, 5, 7, 5, 5}, {6, 5, 6, 5, 6}, {3, 4, 4, 4, 3}, {6, 5, 5, 5, 6}, {7, 4, 6, 4, 7}, {7, 4, 6, 4, 4},
    {3, 4, 5, 5, 3}, {5, 5, 7, 5, 5}, {7, 2, 2, 2, 7}, {1, 1, 1, 5, 2}, {5, 5, 6, 5, 5}, {4, 4, 4, 4, 7},
    {5, 7, 7, 5, 5}, {7, 5, 5, 5, 5}, {2, 5, 5, 5, 2}, {6, 5, 6, 4, 4}, {2, 5, 5, 6, 3}, {6, 5, 6, 5, 5},
    {3, 4, 2, 1, 6}, {7, 2, 2, 2, 2}, {5, 5, 5, 5, 7}, {5, 5, 5, 5, 2}, {5, 5, 7, 7, 5}, {5, 5, 2, 5, 5},
    {5, 5, 2, 2, 2}, {7, 1, 2, 4, 7},
};

void ui_init(int cols, int rows) {
    memset(&U, 0, sizeof U);
    U.cols = maxi(cols, 1);
    U.rows = maxi(rows, 1);
    U.dock_y = -1;
}

void ui_resize(int cols, int rows) {
    U.cols = maxi(cols, 1);
    U.rows = maxi(rows, 1);
    U.nboxes = 0;
    U.dock_y = -1;
    U.link_visible = false;
}

void ui_update(double dt) {
    if (dt < 0.0) dt = 0.0;
    U.age += dt;
    if (U.toast_left > 0.0) U.toast_left = fmax(0.0, U.toast_left - dt);
}

void ui_toast(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(U.toast, sizeof U.toast, fmt, ap);
    va_end(ap);
    U.toast_left = 2.2;
}

void ui_set_ascii(bool on) { U.ascii = on; }

void ui_toggle_help(void) {
    U.help = !U.help;
    if (U.help) U.menu = false;
}

void ui_menu_toggle(int current) {
    U.menu = !U.menu;
    if (U.menu) {
        U.menu_sel = maxi(current, 0);
        U.help = false;
    }
}

bool ui_menu_open(void) { return U.menu; }
void ui_menu_close(void) { U.menu = false; }
int ui_menu_selected(void) { return U.menu_sel; }

void ui_menu_move(int delta, int count) {
    if (count <= 0) return;
    U.menu_sel = ((U.menu_sel + delta) % count + count) % count;
}

int ui_menu_hit(int x, int y) {
    if (!U.menu || U.menu_w <= 0 || U.menu_visible <= 0 || x < U.menu_x || x >= U.menu_x + U.menu_w) return -1;
    int row = y - U.menu_y;
    if (row < 0 || row >= U.menu_visible) return -1;
    int idx = U.menu_start + row;
    return idx < U.menu_start + U.menu_visible ? idx : -1;
}

bool ui_help_open(void) { return U.help; }
void ui_close_help(void) { U.help = false; }

static uint32_t glyph(char ascii, uint32_t unicode) { return U.ascii ? (uint32_t)(unsigned char)ascii : unicode; }

static void text_clip(canvas_t *c, int x, int y, const char *s, int max_chars, rgb_t fg, rgb_t bg, float alpha) {
    if (!s || max_chars <= 0) return;
    int n = (int)strlen(s);
    if (n > max_chars) n = max_chars;
    for (int i = 0; i < n; i++) canvas_overlay_glyph(c, x + i, y, (uint32_t)(unsigned char)s[i], fg, bg, alpha);
}

static void fill_rect(canvas_t *c, int x, int y, int w, int h, rgb_t bg, float alpha) {
    if (w <= 0 || h <= 0) return;
    int x0 = maxi(0, x), y0 = maxi(0, y);
    int x1 = mini(c->cols, x + w), y1 = mini(c->rows, y + h);
    for (int yy = y0; yy < y1; yy++) {
        for (int xx = x0; xx < x1; xx++) {
            /* A normal rectangle only tints the scene; a UI panel must also
             * mask scene glyphs underneath its blank cells for legibility. */
            canvas_overlay_glyph(c, xx, yy, ' ', bg, bg, alpha);
        }
    }
}

static void line_h(canvas_t *c, int x, int y, int w, rgb_t fg, rgb_t bg, float alpha) {
    if (w <= 0) return;
    uint32_t g = glyph('-', 0x2500u);
    for (int i = 0; i < w; i++) canvas_overlay_glyph(c, x + i, y, g, fg, bg, alpha);
}

static void line_v(canvas_t *c, int x, int y, int h, rgb_t fg, rgb_t bg, float alpha) {
    if (h <= 0) return;
    uint32_t g = glyph('|', 0x2502u);
    for (int i = 0; i < h; i++) canvas_overlay_glyph(c, x, y + i, g, fg, bg, alpha);
}

static void panel(canvas_t *c, int x, int y, int w, int h, const palette_t *pal, float alpha) {
    if (w < 2 || h < 2) return;
    fill_rect(c, x, y, w, h, pal->bg_top, alpha);
    uint32_t tl = glyph('+', 0x256D), tr = glyph('+', 0x256E);
    uint32_t bl = glyph('+', 0x2570), br = glyph('+', 0x256F);
    for (int i = 1; i < w - 1; i++) {
        canvas_overlay_glyph(c, x + i, y, glyph('-', 0x2500), pal->accent, pal->bg_top, alpha);
        canvas_overlay_glyph(c, x + i, y + h - 1, glyph('-', 0x2500), pal->accent, pal->bg_top, alpha);
    }
    for (int j = 1; j < h - 1; j++) {
        canvas_overlay_glyph(c, x, y + j, glyph('|', 0x2502), pal->accent, pal->bg_top, alpha);
        canvas_overlay_glyph(c, x + w - 1, y + j, glyph('|', 0x2502), pal->accent, pal->bg_top, alpha);
    }
    canvas_overlay_glyph(c, x, y, tl, pal->accent, pal->bg_top, alpha);
    canvas_overlay_glyph(c, x + w - 1, y, tr, pal->accent, pal->bg_top, alpha);
    canvas_overlay_glyph(c, x, y + h - 1, bl, pal->accent, pal->bg_top, alpha);
    canvas_overlay_glyph(c, x + w - 1, y + h - 1, br, pal->accent, pal->bg_top, alpha);
}

static int pixel_title_width(const char *name) {
    int w = 0;
    for (const char *p = name; *p; p++) w += (*p >= 'a' && *p <= 'z') ? 8 : 4;
    return w;
}

static void draw_pixel_title(canvas_t *c, int x0, int y0, int available, const char *name, const palette_t *pal) {
    int width = pixel_title_width(name);
    if (width > available || U.rows < 15) {
        text_clip(c, x0, y0 + 1, name, maxi(1, available), pal->ink_b, pal->bg_top, 0.0f);
        return;
    }
    int x = x0;
    for (const char *p = name; *p; p++) {
        if (*p < 'a' || *p > 'z') {
            x += 4;
            continue;
        }
        const uint8_t *bits = FONT[*p - 'a'];
        for (int row = 0; row < 5; row++) {
            for (int col = 0; col < 3; col++) {
                if (!(bits[row] & (4 >> col))) continue;
                float f = (float)(x - x0 + col * 2) / (float)maxi(1, width);
                rgb_t fg = rgb_scale(rgb_mix(pal->ink_a, pal->ink_b, f), 0.95f);
                uint32_t block = U.ascii ? (uint32_t)'#' : BLOCK_GLYPH;
                canvas_overlay_glyph(c, x + col * 2, y0 + row, block, fg, fg, 0.0f);
                canvas_overlay_glyph(c, x + col * 2 + 1, y0 + row, block, fg, fg, 0.0f);
            }
        }
        x += 8;
    }
}

static void draw_link(canvas_t *c, const palette_t *pal) {
    char label[48];
    snprintf(label, sizeof label, " %s ", SITE_LABEL);
    U.link_w = (int)strlen(label);
    U.link_x = U.cols - U.link_w - 2;
    U.link_y = 0;
    U.link_visible = U.cols >= 78 && U.link_x > 38;
    if (!U.link_visible) return;
    fill_rect(c, U.link_x, 0, U.link_w, 1, pal->accent, 0.94f);
    text_clip(c, U.link_x, 0, label, U.link_w, pal->bg_top, pal->accent, 0.94f);
}

static void draw_header(canvas_t *c, const palette_t *pal) {
    if (U.rows < 2 || U.cols < 18) return;
    fill_rect(c, 0, 0, U.cols, 2, pal->bg_top, 0.78f);
    canvas_overlay_glyph(c, 2, 0, glyph('*', 0x25C6), pal->accent, pal->bg_top, 0.0f);
    text_clip(c, 4, 0, "ASCII ASMR", 12, pal->ink_b, pal->bg_top, 0.78f);
    if (U.cols >= 48) text_clip(c, 17, 0, "/  GENERATIVE AMBIENCE", maxi(1, U.cols - 38), rgb_scale(pal->ink_a, 0.65f), pal->bg_top, 0.78f);
    line_h(c, 2, 2, U.cols - 4, rgb_scale(pal->accent, 0.48f), pal->bg_top, 0.35f);
    draw_link(c, pal);
}

static void draw_meter(canvas_t *c, int x, int y, int count, float value, const palette_t *pal) {
    count = maxi(1, count);
    int lit = clampi((int)(saturatef(value) * (float)count + 0.5f), 0, count);
    for (int i = 0; i < count; i++) {
        bool on = i < lit;
        canvas_overlay_glyph(c, x + i, y, on ? glyph('#', BLOCK_GLYPH) : glyph('-', 0x2500),
                             on ? pal->accent : rgb_scale(pal->ink_a, 0.26f), pal->bg_top, on ? 0.0f : 0.45f);
    }
}

static void draw_scene_panel(canvas_t *c, const palette_t *pal, const ui_ctx_t *ctx) {
    if (U.cols >= 72 && U.rows >= 21) {
        int x = 2, y = 3, w = U.cols - 4, h = 10;
        panel(c, x, y, w, h, pal, 0.88f);
        int split = x + (w * 61) / 100;
        line_v(c, split, y + 1, h - 2, rgb_scale(pal->accent, 0.45f), pal->bg_top, 0.35f);

        char label[48];
        snprintf(label, sizeof label, " NOW PLAYING  /  %02d OF %02d ", ctx->scene_index + 1, ctx->scene_count);
        text_clip(c, x + 3, y + 1, label, split - x - 6, pal->accent, pal->bg_top, 0.88f);
        draw_pixel_title(c, x + 3, y + 2, split - x - 6, ctx->scene_name ? ctx->scene_name : "ambient", pal);
        text_clip(c, x + 3, y + 8, ctx->scene_blurb ? ctx->scene_blurb : "generative ambience", split - x - 6,
                  rgb_scale(pal->ink_a, 0.72f), pal->bg_top, 0.88f);

        int rx = split + 3, rw = x + w - rx - 3;
        text_clip(c, rx, y + 1, "ATMOSPHERE", rw, pal->ink_b, pal->bg_top, 0.88f);
        char stat[64];
        snprintf(stat, sizeof stat, "palette  %s", ctx->palette_name ? ctx->palette_name : "default");
        text_clip(c, rx, y + 3, stat, rw, pal->ink_a, pal->bg_top, 0.72f);
        snprintf(stat, sizeof stat, "motion   %.2fx", (double)ctx->speed);
        text_clip(c, rx, y + 4, stat, rw, pal->ink_a, pal->bg_top, 0.72f);
        snprintf(stat, sizeof stat, "volume   %d%%", (int)(clampf(ctx->volume, 0.0f, 1.0f) * 100.0f + 0.5f));
        text_clip(c, rx, y + 5, stat, rw, pal->ink_a, pal->bg_top, 0.72f);
        snprintf(stat, sizeof stat, "sound    %s", !ctx->audio_available ? "unavailable" : (ctx->audio_on ? "enabled" : "muted"));
        text_clip(c, rx, y + 6, stat, rw, !ctx->audio_available ? rgb_scale(pal->ink_a, 0.55f) : pal->ink_a, pal->bg_top, 0.88f);
        text_clip(c, rx, y + 8, "output level", maxi(1, rw - 13), rgb_scale(pal->ink_a, 0.6f), pal->bg_top, 0.88f);
        draw_meter(c, rx + mini(13, maxi(1, rw - 1)), y + 8, maxi(1, rw - mini(13, maxi(1, rw - 1))),
                   ctx->audio_available && ctx->audio_on ? ctx->level : 0.0f, pal);
        return;
    }

    /* Smaller terminals use a clean, text-first scene card rather than trying
     * to cram the large bitmap title into too little space. */
    if (U.rows >= 8 && U.cols >= 24) {
        int x = 2, y = U.rows >= 15 ? 4 : 3, w = U.cols - 4;
        char line[100];
        snprintf(line, sizeof line, "[%02d/%02d] %s", ctx->scene_index + 1, ctx->scene_count,
                 ctx->scene_name ? ctx->scene_name : "ambient");
        text_clip(c, x, y, line, w, pal->ink_b, pal->bg_top, 0.0f);
        if (U.rows >= 12 && ctx->scene_blurb) text_clip(c, x, y + 1, ctx->scene_blurb, w, rgb_scale(pal->ink_a, 0.65f), pal->bg_top, 0.0f);
    } else if (U.rows >= 2) {
        text_clip(c, 1, 1, ctx->scene_name ? ctx->scene_name : "ambient", maxi(1, U.cols - 2), pal->ink_b, pal->bg_top, 0.0f);
    }
}

static void add_box(int x, int w, ui_action_t a, int arg) {
    if (U.nboxes >= MAX_BOXES || w <= 0) return;
    U.boxes[U.nboxes++] = (hitbox_t){x, w, a, arg};
}

typedef struct {
    const char *label;
    ui_action_t action;
    int arg;
    bool active;
} dock_item_t;

static void draw_dock(canvas_t *c, const palette_t *pal, const ui_ctx_t *ctx) {
    dock_item_t items[7];
    int n = 0;
    bool full = U.cols >= 70;
    bool medium = U.cols >= 47;
    items[n++] = (dock_item_t){full ? "  < PREV  " : (medium ? " < prev " : " < "), UI_SCENE,
                                (ctx->scene_index + ctx->scene_count - 1) % maxi(1, ctx->scene_count), false};
    items[n++] = (dock_item_t){full ? "  NEXT >  " : (medium ? " next > " : " > "), UI_SCENE,
                                (ctx->scene_index + 1) % maxi(1, ctx->scene_count), false};
    items[n++] = (dock_item_t){full ? "  SHUFFLE  " : (medium ? " shuffle " : " ~ "), UI_SHUFFLE, 0, false};
    if (U.cols >= 35) items[n++] = (dock_item_t){full ? "  PALETTE  " : (medium ? " palette " : " p "), UI_PALETTE, 0, false};
    if (U.cols >= 70) items[n++] = (dock_item_t){ctx->autoshuffle ? "  AUTO ON  " : "  AUTO OFF ", UI_AUTO, 0, ctx->autoshuffle};
    else if (U.cols >= 47) items[n++] = (dock_item_t){ctx->autoshuffle ? " auto:on " : " auto:off", UI_AUTO, 0, ctx->autoshuffle};
    if (U.cols >= 47) items[n++] = (dock_item_t){!ctx->audio_available ? " sound:n/a " : (ctx->audio_on ? " sound:on " : " sound:off "), UI_SOUND, 0, ctx->audio_on && ctx->audio_available};
    items[n++] = (dock_item_t){full ? "  HELP ?  " : (medium ? " help ? " : " ? "), UI_HELP, 0, U.help};

    int total = 0;
    for (int i = 0; i < n; i++) total += (int)strlen(items[i].label);
    total += (n - 1) + 2;
    if (total > U.cols - 2 || U.rows < 3) {
        U.nboxes = 0;
        U.dock_y = -1;
        return;
    }

    int x = (U.cols - total) / 2;
    int y = U.rows - 3;
    U.dock_y = y;
    fill_rect(c, x, y, total, 1, pal->bg_top, 0.88f);
    x += 1;
    for (int i = 0; i < n; i++) {
        int w = (int)strlen(items[i].label);
        rgb_t fg = items[i].active ? pal->bg_top : pal->ink_a;
        rgb_t bg = items[i].active ? pal->accent : rgb_mix(pal->bg_top, pal->bg_bottom, 0.25f);
        fill_rect(c, x, y, w, 1, bg, items[i].active ? 0.98f : 0.62f);
        text_clip(c, x, y, items[i].label, w, fg, bg, items[i].active ? 0.98f : 0.62f);
        add_box(x, w, items[i].action, items[i].arg);
        x += w + 1;
    }
}

static void draw_status(canvas_t *c, const palette_t *pal, const ui_ctx_t *ctx) {
    if (U.rows < 6 || U.cols < 34) return;
    char status[192];
    int len = 0;
    len += snprintf(status + len, sizeof status - (size_t)len, "scene %02d/%02d   %s   %.2fx",
                    ctx->scene_index + 1, ctx->scene_count, ctx->palette_name ? ctx->palette_name : "palette", (double)ctx->speed);
    if (ctx->journey_total > 0 && len < (int)sizeof status - 24)
        len += snprintf(status + len, sizeof status - (size_t)len, "   journey %d/%d", ctx->journey_step + 1, ctx->journey_total);
    if (ctx->sleep_left >= 0.0 && len < (int)sizeof status - 24) {
        long long sec = (long long)fmax(0.0, ctx->sleep_left);
        (void)snprintf(status + len, sizeof status - (size_t)len, "   sleep %lld:%02lld", sec / 60, sec % 60);
    }
    fill_rect(c, 0, U.rows - 1, U.cols, 1, pal->bg_top, 0.78f);
    text_clip(c, 2, U.rows - 1, status, U.cols - 4, rgb_scale(pal->ink_a, 0.62f), pal->bg_top, 0.78f);
    if (ctx->fps > 0.0 && U.cols >= 80) {
        char fps[24];
        snprintf(fps, sizeof fps, "%.0f fps", ctx->fps);
        int x = U.cols - (int)strlen(fps) - 2;
        text_clip(c, x, U.rows - 1, fps, (int)strlen(fps), rgb_scale(pal->ink_a, 0.45f), pal->bg_top, 0.78f);
    }
}

static void draw_toast(canvas_t *c, const palette_t *pal) {
    if (U.toast_left <= 0.0 || !U.toast[0]) return;
    int len = (int)strlen(U.toast);
    int max_len = maxi(1, U.cols - 4);
    if (len > max_len) len = max_len;
    int w = len + 4;
    int x = maxi(0, (U.cols - w) / 2);
    int y = U.rows >= 18 ? U.rows - 5 : maxi(0, U.rows - 2);
    fill_rect(c, x, y, mini(w, U.cols), 1, pal->bg_top, 0.94f);
    canvas_overlay_glyph(c, x, y, glyph('[', 0x25C2), pal->accent, pal->bg_top, 0.0f);
    text_clip(c, x + 2, y, U.toast, len, pal->ink_b, pal->bg_top, 0.0f);
    canvas_overlay_glyph(c, x + w - 1, y, glyph(']', 0x25B8), pal->accent, pal->bg_top, 0.0f);
}

static void draw_help(canvas_t *c, const palette_t *pal) {
    static const char *lines[] = {
        "ARROWS / TAB / , .     move between scenes",
        "G                     open the scene browser",
        "SPACE / S             shuffle the atmosphere",
        "P / R                 next palette / random palette",
        "A / M                 auto shuffle / sound on-off",
        "+ - / mouse wheel     adjust the volume",
        "[ ]                   slow down / speed up motion",
        "J / B                 guided journey / breathing",
        "T / X / U             sleep timer / screenshot / hide UI",
        "CLICK / DRAG          interact with the scene",
        "Q / ESC               quit",
    };
    int w = mini(U.cols - 2, 68);
    if (w < 22 || U.rows < 7) {
        text_clip(c, 1, maxi(0, U.rows / 2), "? help   q quit", maxi(1, U.cols - 2), pal->accent, pal->bg_top, 0.0f);
        return;
    }
    int max_lines = maxi(1, U.rows - 6);
    int n = mini((int)ARRAY_LEN(lines), max_lines);
    int h = n + 5;
    int x = (U.cols - w) / 2, y = (U.rows - h) / 2;
    panel(c, x, y, w, h, pal, 0.96f);
    text_clip(c, x + 3, y + 1, APP_NAME "  /  KEYBOARD GUIDE", w - 6, pal->accent, pal->bg_top, 0.96f);
    for (int i = 0; i < n; i++) text_clip(c, x + 3, y + 3 + i, lines[i], w - 6, pal->ink_a, pal->bg_top, 0.96f);
}

static void draw_menu(canvas_t *c, const palette_t *pal, const ui_ctx_t *ctx, const char *const *scene_names) {
    U.menu_visible = 0;
    U.menu_w = 0;
    int count = maxi(0, ctx->scene_count);
    if (count <= 0 || U.cols < 10 || U.rows < 5) return;
    if (U.menu_sel >= count) U.menu_sel = count - 1;
    int visible = mini(count, maxi(1, U.rows - 7));
    int start = clampi(U.menu_sel - visible / 2, 0, maxi(0, count - visible));
    int w = mini(U.cols - 2, 72), h = visible + 4;
    int x = (U.cols - w) / 2, y = (U.rows - h) / 2;
    if (w < 16 || y < 0) return;

    panel(c, x, y, w, h, pal, 0.97f);
    text_clip(c, x + 2, y + 1, "SCENE BROWSER  /  UP-DOWN TO PREVIEW, ENTER TO SELECT", w - 4, pal->accent, pal->bg_top, 0.97f);
    U.menu_x = x;
    U.menu_y = y + 3;
    U.menu_w = w;
    U.menu_start = start;
    U.menu_visible = visible;
    for (int i = 0; i < visible; i++) {
        int idx = start + i;
        char line[128];
        const char *name = scene_names && scene_names[idx] ? scene_names[idx] : "unknown";
        const char *blurb = ctx->blurbs && ctx->blurbs[idx] ? ctx->blurbs[idx] : "";
        snprintf(line, sizeof line, " %02d  %-12s  %s", idx + 1, name, blurb);
        bool selected = idx == U.menu_sel;
        bool current = idx == ctx->scene_index;
        rgb_t fg = selected ? pal->bg_top : (current ? pal->accent : pal->ink_a);
        rgb_t bg = selected ? pal->accent : pal->bg_top;
        if (selected) fill_rect(c, x + 1, U.menu_y + i, w - 2, 1, bg, 0.98f);
        text_clip(c, x + 2, U.menu_y + i, line, w - 4, fg, bg, selected ? 0.98f : 0.97f);
    }
    if (count > visible) {
        char page[48];
        snprintf(page, sizeof page, "%d-%d / %d", start + 1, start + visible, count);
        text_clip(c, x + w - (int)strlen(page) - 2, y + 2, page, (int)strlen(page),
                  rgb_scale(pal->ink_a, 0.6f), pal->bg_top, 0.97f);
    }
}

void ui_draw(canvas_t *c, const palette_t *pal, const ui_ctx_t *ctx, const char *const *scene_names) {
    U.nboxes = 0;
    U.dock_y = -1;
    U.link_visible = false;

    if (ctx->hud_hidden) {
        draw_toast(c, pal);
        return;
    }

    draw_header(c, pal);
    draw_scene_panel(c, pal, ctx);
    if (!ctx->idle || U.help || U.menu) draw_dock(c, pal, ctx);

    if (U.rows >= 16 && U.cols >= 52 && U.toast_left <= 0.0 && !U.help && !U.menu && !ctx->idle) {
        const char *hint = "g scenes   space shuffle   p palette   m sound   ? help   q quit";
        int x = maxi(0, (U.cols - (int)strlen(hint)) / 2);
        text_clip(c, x, U.rows - 5, hint, U.cols - x - 1, rgb_scale(pal->ink_a, 0.48f), pal->bg_top, 0.0f);
    }

    draw_status(c, pal, ctx);
    draw_toast(c, pal);
    if (U.help) draw_help(c, pal);
    if (U.menu) draw_menu(c, pal, ctx, scene_names);
}

ui_action_t ui_hit_test(int x, int y, int *arg) {
    if (arg) *arg = 0;
    if (ui_link_hit(x, y)) return UI_NONE;
    if (y != U.dock_y) return UI_NONE;
    for (int i = 0; i < U.nboxes; i++) {
        if (x >= U.boxes[i].x && x < U.boxes[i].x + U.boxes[i].w) {
            if (arg) *arg = U.boxes[i].arg;
            return U.boxes[i].action;
        }
    }
    return UI_NONE;
}

bool ui_link_hit(int x, int y) {
    return U.link_visible && y == U.link_y && x >= U.link_x && x < U.link_x + U.link_w;
}

/* Terminals supporting OSC 8 render this as an actual hyperlink. */
void ui_emit_link(const palette_t *pal) {
    if (!U.link_visible) return;
    char label[48];
    snprintf(label, sizeof label, " %s ", SITE_LABEL);
    out_move(U.link_x, U.link_y);
    out_str("\x1b]8;;" SITE_URL "\x1b\\");
    out_fg(pal->bg_top);
    out_bg(pal->accent);
    out_str(label);
    out_str("\x1b]8;;\x1b\\");
    out_reset();
}
