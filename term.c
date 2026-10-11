#include "term.h"

#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

#define IN_CAP 1024
#define ESC_DELAY_SECONDS 0.025
#define ESC_SEQUENCE_DELAY_SECONDS 0.20

static struct termios g_saved;
static bool g_active = false;
static bool g_headless = false;
static volatile sig_atomic_t g_resize = 0;
static volatile sig_atomic_t g_quit = 0;
static bool g_esc_pending = false;
static double g_esc_since = 0.0;
static bool g_seq_pending = false;
static double g_seq_since = 0.0;
static struct sigaction g_old_winch, g_old_int, g_old_term, g_old_hup;
static bool g_old_winch_saved = false, g_old_int_saved = false;
static bool g_old_term_saved = false, g_old_hup_saved = false;
static int g_cols = 80, g_rows = 24;

static unsigned char g_in[IN_CAP];
static size_t g_in_len = 0;

static char *g_out = NULL;
static size_t g_out_len = 0, g_out_cap = 0;

static void on_signal(int sig) {
    if (sig == SIGWINCH)
        g_resize = 1;
    else
        g_quit = 1;
}

static void write_all(const char *buf, size_t n) {
    while (n > 0) {
        ssize_t w = write(STDOUT_FILENO, buf, n);
        if (w < 0) {
            if (errno == EINTR) continue;
            return;
        }
        if (w == 0) return;
        buf += w;
        n -= (size_t)w;
    }
}

static void refresh_size(void) {
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0 && ws.ws_row > 0) {
        g_cols = ws.ws_col;
        g_rows = ws.ws_row;
    }
}

bool term_init(void) {
    if (!isatty(STDIN_FILENO) || !isatty(STDOUT_FILENO)) return false;
    g_headless = false;
    g_in_len = 0;
    g_out_len = 0;
    g_esc_pending = g_seq_pending = false;
    g_resize = g_quit = 0;
    if (tcgetattr(STDIN_FILENO, &g_saved) != 0) return false;

    struct termios raw = g_saved;
    raw.c_iflag &= ~(tcflag_t)(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    raw.c_oflag &= ~(tcflag_t)(OPOST);
    raw.c_cflag |= CS8;
    raw.c_lflag &= ~(tcflag_t)(ECHO | ICANON | IEXTEN);
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 0;
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) != 0) return false;

    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = on_signal;
    sigemptyset(&sa.sa_mask);
    g_old_winch_saved = sigaction(SIGWINCH, &sa, &g_old_winch) == 0;
    g_old_int_saved = sigaction(SIGINT, &sa, &g_old_int) == 0;
    g_old_term_saved = sigaction(SIGTERM, &sa, &g_old_term) == 0;
    g_old_hup_saved = sigaction(SIGHUP, &sa, &g_old_hup) == 0;

    const char *enter = "\x1b[?1049h\x1b[?25l\x1b[?7l\x1b[?1000h\x1b[?1002h\x1b[?1003h\x1b[?1006h\x1b[2J";
    write_all(enter, strlen(enter));

    refresh_size();
    g_active = true;
    atexit(term_shutdown);
    return true;
}

void term_headless(int cols, int rows) {
    g_headless = true;
    g_cols = cols;
    g_rows = rows;
    g_active = true;
}

void term_shutdown(void) {
    if (!g_active) return;
    g_active = false;
    if (g_headless) return;
    const char *leave = "\x1b[0m\x1b]8;;\x1b\\\x1b[?1006l\x1b[?1003l\x1b[?1002l\x1b[?1000l\x1b[?7h\x1b[?25h\x1b[?1049l";
    write_all(leave, strlen(leave));
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &g_saved);
    if (g_old_winch_saved) sigaction(SIGWINCH, &g_old_winch, NULL);
    if (g_old_int_saved) sigaction(SIGINT, &g_old_int, NULL);
    if (g_old_term_saved) sigaction(SIGTERM, &g_old_term, NULL);
    if (g_old_hup_saved) sigaction(SIGHUP, &g_old_hup, NULL);
    g_old_winch_saved = g_old_int_saved = g_old_term_saved = g_old_hup_saved = false;
}

bool term_active(void) { return g_active; }

void term_get_size(int *cols, int *rows) {
    if (cols) *cols = g_cols;
    if (rows) *rows = g_rows;
}

bool term_take_resize(void) {
    if (g_headless || !g_resize) return false;
    g_resize = 0;
    refresh_size();
    return true;
}

bool term_quit_requested(void) { return g_quit != 0; }
void term_request_quit(void) { g_quit = 1; }

void term_wait(double timeout_s) {
    if (g_headless) return;
    int ms = (int)(timeout_s * 1000.0);
    if (ms < 0) ms = 0;
    struct pollfd pfd;
    pfd.fd = STDIN_FILENO;
    pfd.events = POLLIN;
    pfd.revents = 0;
    int rc = poll(&pfd, 1, ms);
    if (rc > 0 && (pfd.revents & POLLIN) && g_in_len < IN_CAP) {
        ssize_t n = read(STDIN_FILENO, g_in + g_in_len, IN_CAP - g_in_len);
        if (n > 0) g_in_len += (size_t)n;
    }
}

static void consume(size_t n) {
    if (n >= g_in_len) {
        g_in_len = 0;
        return;
    }
    memmove(g_in, g_in + n, g_in_len - n);
    g_in_len -= n;
}

static bool parse_uint_field(const char **cursor, int *out, char delimiter) {
    const char *start = *cursor;
    char *end = NULL;
    errno = 0;
    long value = strtol(start, &end, 10);
    if (end == start || errno == ERANGE || value < 0 || value > 2147483647L) return false;
    if (delimiter ? *end != delimiter : *end != '\0') return false;
    *out = (int)value;
    *cursor = delimiter ? end + 1 : end;
    return true;
}

static bool parse_csi(term_event_t *ev, bool *unknown) {
    size_t i = 2;
    while (i < g_in_len && !(g_in[i] >= 0x40 && g_in[i] <= 0x7E)) i++;
    if (i >= g_in_len) {
        if (g_in_len > 48) consume(g_in_len);
        return false;
    }
    char params[40];
    size_t plen = i - 2;
    if (plen >= sizeof params) plen = sizeof params - 1;
    memcpy(params, g_in + 2, plen);
    params[plen] = '\0';
    unsigned char final = g_in[i];
    consume(i + 1);

    if (params[0] == '<' && (final == 'M' || final == 'm')) {
        int b = 0, x = 0, y = 0;
        const char *p = params + 1;
        if (parse_uint_field(&p, &b, ';') && parse_uint_field(&p, &x, ';') &&
            parse_uint_field(&p, &y, '\0') && x >= 1 && y >= 1) {
            ev->type = EV_MOUSE;
            ev->x = x - 1;
            ev->y = y - 1;
            ev->button = b & 3;
            if (b & 64)
                ev->mouse = (b & 1) ? MOUSE_WHEEL_DOWN : MOUSE_WHEEL_UP;
            else if (b & 32)
                ev->mouse = (b & 3) == 3 ? MOUSE_MOVE : MOUSE_DRAG;
            else
                ev->mouse = final == 'M' ? MOUSE_PRESS : MOUSE_RELEASE;
            return true;
        }
        *unknown = true;
        return false;
    }

    ev->type = EV_KEY;
    switch (final) {
    case 'A': ev->key = K_UP; return true;
    case 'B': ev->key = K_DOWN; return true;
    case 'C': ev->key = K_RIGHT; return true;
    case 'D': ev->key = K_LEFT; return true;
    case 'H': ev->key = K_HOME; return true;
    case 'F': ev->key = K_END; return true;
    case '~': {
        /* strtol avoids atoi's undefined behaviour on oversized input. CSI
         * key sequences may include modifier fields after a semicolon. */
        char *end = NULL;
        errno = 0;
        long n = strtol(params, &end, 10);
        if (errno == ERANGE || end == params || n < 0 || n > 2147483647L) break;
        if (n == 1 || n == 7) ev->key = K_HOME;
        else if (n == 4 || n == 8) ev->key = K_END;
        else if (n == 3) ev->key = K_DELETE;
        else if (n == 5) ev->key = K_PGUP;
        else if (n == 6) ev->key = K_PGDN;
        else break;
        return true;
    }
    default: break;
    }
    *unknown = true;
    return false;
}

bool term_next_event(term_event_t *ev) {
    for (;;) {
        memset(ev, 0, sizeof *ev);
        if (g_in_len == 0) return false;
        unsigned char c = g_in[0];

        if (c == 0x1B) {
            /* ESC doubles as a standalone key and the prefix for terminal
             * escape sequences. Briefly defer a lone ESC so fragmented arrow
             * and mouse sequences are not mistaken for a quit/help action. */
            if (g_in_len == 1) {
                if (!g_esc_pending) {
                    g_esc_pending = true;
                    g_esc_since = now_seconds();
                    return false;
                }
                if (now_seconds() - g_esc_since < ESC_DELAY_SECONDS) return false;
                g_esc_pending = false;
                consume(1);
                ev->type = EV_KEY;
                ev->key = K_ESC;
                return true;
            }
            g_esc_pending = false;
            if (g_in[1] == '[') {
                bool unknown = false;
                if (parse_csi(ev, &unknown)) {
                    g_seq_pending = false;
                    return true;
                }
                if (unknown) {
                    g_seq_pending = false;
                    continue;
                }
                if (g_in_len == 0) {
                    g_seq_pending = false;
                    continue;
                }
                if (!g_seq_pending) {
                    g_seq_pending = true;
                    g_seq_since = now_seconds();
                    return false;
                }
                if (now_seconds() - g_seq_since < ESC_SEQUENCE_DELAY_SECONDS) return false;
                g_seq_pending = false;
                consume(1);
                ev->type = EV_KEY;
                ev->key = K_ESC;
                return true;
            }
            if (g_in[1] == 'O') {
                if (g_in_len < 3) {
                    if (!g_seq_pending) {
                        g_seq_pending = true;
                        g_seq_since = now_seconds();
                        return false;
                    }
                    if (now_seconds() - g_seq_since < ESC_SEQUENCE_DELAY_SECONDS) return false;
                    g_seq_pending = false;
                    consume(1);
                    ev->type = EV_KEY;
                    ev->key = K_ESC;
                    return true;
                }
                g_seq_pending = false;
                unsigned char f = g_in[2];
                consume(3);
                int key = 0;
                if (f == 'A') key = K_UP;
                else if (f == 'B') key = K_DOWN;
                else if (f == 'C') key = K_RIGHT;
                else if (f == 'D') key = K_LEFT;
                if (key) {
                    ev->type = EV_KEY;
                    ev->key = key;
                    return true;
                }
                continue;
            }
            g_seq_pending = false;
            consume(1);
            ev->type = EV_KEY;
            ev->key = K_ESC;
            return true;
        }

        consume(1);
        if (c == '\r' || c == '\n') {
            ev->type = EV_KEY;
            ev->key = K_ENTER;
            return true;
        }
        if (c == '\t') {
            ev->type = EV_KEY;
            ev->key = K_TAB;
            return true;
        }
        if (c == 0x7F || c == 0x08) {
            ev->type = EV_KEY;
            ev->key = K_BACKSPACE;
            return true;
        }
        if (c == 0x03) {
            ev->type = EV_KEY;
            ev->key = 3;
            return true;
        }
        if (c >= 32 && c < 127) {
            ev->type = EV_KEY;
            ev->key = (int)c;
            return true;
        }
    }
}

static void out_reserve(size_t extra) {
    if (g_out_len + extra <= g_out_cap) return;
    size_t cap = g_out_cap ? g_out_cap : 16384;
    while (cap < g_out_len + extra) cap *= 2;
    g_out = xrealloc(g_out, cap);
    g_out_cap = cap;
}

void out_raw(const char *data, size_t len) {
    out_reserve(len);
    memcpy(g_out + g_out_len, data, len);
    g_out_len += len;
}

void out_str(const char *s) { out_raw(s, strlen(s)); }

void out_printf(const char *fmt, ...) {
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    if (n < 0) return;
    if ((size_t)n >= sizeof buf) n = (int)sizeof buf - 1;
    out_raw(buf, (size_t)n);
}

void out_move(int x, int y) { out_printf("\x1b[%d;%dH", y + 1, x + 1); }
static bool g_truecolor = true;

void term_set_truecolor(bool on) { g_truecolor = on; }
bool term_truecolor(void) { return g_truecolor; }

bool term_detect_truecolor(void) {
    const char *ct = getenv("COLORTERM");
    return ct && (strstr(ct, "truecolor") || strstr(ct, "24bit"));
}

static int cube_level(int v) {
    static const int lv[6] = {0, 95, 135, 175, 215, 255};
    int best = 0, bd = 1 << 30;
    for (int i = 0; i < 6; i++) {
        int d = (v - lv[i]) * (v - lv[i]);
        if (d < bd) {
            bd = d;
            best = i;
        }
    }
    return best;
}

int term_rgb_to_256(rgb_t c) {
    static const int lv[6] = {0, 95, 135, 175, 215, 255};
    int ri = cube_level(c.r), gi = cube_level(c.g), bi = cube_level(c.b);
    int cr = lv[ri], cg = lv[gi], cb = lv[bi];
    int cube_d = (c.r - cr) * (c.r - cr) + (c.g - cg) * (c.g - cg) + (c.b - cb) * (c.b - cb);
    int avg = (c.r + c.g + c.b) / 3;
    int gi2 = clampi((avg - 8 + 5) / 10, 0, 23);
    int gv = 8 + 10 * gi2;
    int gray_d = (c.r - gv) * (c.r - gv) + (c.g - gv) * (c.g - gv) + (c.b - gv) * (c.b - gv);
    return gray_d < cube_d ? 232 + gi2 : 16 + 36 * ri + 6 * gi + bi;
}

void out_fg(rgb_t c) {
    if (g_truecolor) out_printf("\x1b[38;2;%d;%d;%dm", c.r, c.g, c.b);
    else out_printf("\x1b[38;5;%dm", term_rgb_to_256(c));
}

void out_bg(rgb_t c) {
    if (g_truecolor) out_printf("\x1b[48;2;%d;%d;%dm", c.r, c.g, c.b);
    else out_printf("\x1b[48;5;%dm", term_rgb_to_256(c));
}
void out_reset(void) { out_str("\x1b[0m"); }

void out_glyph(uint32_t cp) {
    char buf[4];
    size_t n = utf8_encode(cp, buf);
    out_raw(buf, n);
}

size_t out_pending(void) { return g_out_len; }

void out_flush(void) {
    if (g_out_len > 0 && !g_headless) write_all(g_out, g_out_len);
    g_out_len = 0;
}
