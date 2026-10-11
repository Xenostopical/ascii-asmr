#ifndef ASMR_UI_H
#define ASMR_UI_H

#include "canvas.h"
#include "common.h"
#include "palette.h"

typedef enum {
    UI_NONE = 0,
    UI_SCENE,
    UI_SHUFFLE,
    UI_AUTO,
    UI_SOUND,
    UI_PALETTE,
    UI_HELP,
    UI_QUIT
} ui_action_t;

typedef struct {
    int scene_index;
    int scene_count;
    const char *scene_name;
    const char *scene_blurb;
    const char *palette_name;
    bool audio_on;
    bool audio_available;
    bool hud_hidden;
    float volume;
    bool autoshuffle;
    double fps;
    bool idle;
    float level;
    const char *const *blurbs;
    double sleep_left;
    float speed;
    int journey_step, journey_total;
} ui_ctx_t;

void ui_init(int cols, int rows);
void ui_resize(int cols, int rows);
void ui_update(double dt);
void ui_toast(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
void ui_set_ascii(bool on); /* use '#' instead of block characters */
void ui_toggle_help(void);
void ui_menu_toggle(int current);
bool ui_menu_open(void);
void ui_menu_close(void);
void ui_menu_move(int delta, int count);
int ui_menu_selected(void);
int ui_menu_hit(int x, int y);
bool ui_help_open(void);
void ui_close_help(void);
/* Draws on top of whatever is already in the overlay layer; the caller clears it. */
void ui_draw(canvas_t *c, const palette_t *pal, const ui_ctx_t *ctx, const char *const *scene_names);
ui_action_t ui_hit_test(int x, int y, int *arg);
bool ui_link_hit(int x, int y);
void ui_emit_link(const palette_t *pal);

#endif
