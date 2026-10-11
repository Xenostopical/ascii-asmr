#ifndef ASMR_TERM_H
#define ASMR_TERM_H

#include "common.h"

typedef enum { EV_NONE = 0, EV_KEY, EV_MOUSE } ev_type_t;

typedef enum {
    MOUSE_PRESS,
    MOUSE_RELEASE,
    MOUSE_DRAG,
    MOUSE_MOVE,
    MOUSE_WHEEL_UP,
    MOUSE_WHEEL_DOWN
} mouse_kind_t;

enum {
    K_ESC = 256,
    K_UP,
    K_DOWN,
    K_LEFT,
    K_RIGHT,
    K_ENTER,
    K_TAB,
    K_BACKSPACE,
    K_HOME,
    K_END,
    K_PGUP,
    K_PGDN,
    K_DELETE
};

typedef struct {
    ev_type_t type;
    int key;
    mouse_kind_t mouse;
    int x, y;
    int button;
} term_event_t;

bool term_init(void);
void term_headless(int cols, int rows);
void term_shutdown(void);
bool term_active(void);

void term_get_size(int *cols, int *rows);
bool term_take_resize(void);
bool term_quit_requested(void);
void term_request_quit(void);

void term_wait(double timeout_s);
bool term_next_event(term_event_t *ev);

void out_flush(void);
void out_raw(const char *data, size_t len);
void out_str(const char *s);
void out_printf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
void out_move(int x, int y);
void out_fg(rgb_t c);
void term_set_truecolor(bool on);
bool term_truecolor(void);
bool term_detect_truecolor(void); /* COLORTERM says truecolor or 24bit */
int term_rgb_to_256(rgb_t c);    /* nearest xterm-256 palette index */
void out_bg(rgb_t c);
void out_reset(void);
void out_glyph(uint32_t cp);
size_t out_pending(void);

#endif
