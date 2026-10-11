#include "audio.h"
#include "breath.h"
#include "canvas.h"
#include "config.h"
#include "journey.h"
#include "palette.h"
#include "rng.h"
#include "scene.h"
#include "term.h"
#include "ui.h"

#include <errno.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/wait.h>
#include <unistd.h>

typedef struct {
    int scene, palette, frames;
    int bench, color;
    const char *offline;
    float fps, speed, sleep_min;
    bool audio, autoshuffle, list, journey, ascii;
    uint64_t seed;
    const char *dump;
} options_t;

typedef enum { PARSE_ERROR = -1, PARSE_OK = 0, PARSE_EXIT_OK = 1 } parse_result_t;

static struct {
    canvas_t canvas;
    scene_instance_t scene;
    palette_t pal_from, pal_to, pal_cur;
    int pal_index, pending_scene;
    float pal_t, fade;
    bool fading_out, quit, journey_on, idle, hud_off;
    float speed, sleep_mul;
    double scene_time, sleep_left, last_input, mouse_t;
    int mouse_x, mouse_y;
    double sim_time, next_auto, fps_acc, fps_shown;
    int fps_frames;
    char names_buf[32][16];
    const char *names[32];
    const char *blurbs[32];
} G;

static void usage(const char *argv0) {
    printf("%s %s - terminal ASMR with generative ASCII scenes\n", APP_NAME, APP_VERSION);
    printf("usage: %s [options]\n\n", argv0);
    printf("  -s NAME    start scene (see --list)\n");
    printf("  -p NAME    start palette\n");
    printf("  -f FPS     frame rate, 10-60 (default 30)\n");
    printf("  -n         no audio\n");
    printf("  -a         disable auto shuffle\n");
    printf("  -S SEED    random seed\n");
    printf("  -d FILE    write raw s16le 44.1k stereo audio to FILE instead of playing\n");
    printf("  -F N       headless: render N frames without a terminal, print stats\n");
    printf("  -O FILE    render audio offline to a WAV file (use with -F N frames, default 1800)\n");
    printf("  -t / -2    force truecolor / 256-colour output (default: auto from COLORTERM)\n");
    printf("  -A         ASCII-only interface (no block or box characters)\n");
    printf("  -v         print version\n");
    printf("  -B N       benchmark every scene for N frames and exit\n");
    printf("  -j         start the guided journey\n");
    printf("  -T MIN     sleep timer: fade out and quit after MIN minutes\n");
    printf("  -x SPEED   animation speed 0.25-3 (default 1)\n");
    printf("  -l         list scenes and palettes\n");
    printf("  -h         this help\n\n");
    printf("more at %s\n", SITE_URL);
}

static void list_all(void) {
    printf("scenes:\n");
    for (int i = 0; i < scene_count(); i++) printf("  %-10s %s\n", scene_def(i)->name, scene_def(i)->blurb);
    printf("palettes:\n");
    for (int i = 0; i < palette_count(); i++) printf("  %s\n", palette_get(i)->name);
}

static bool take_value(int argc, char **argv, int *i, const char *option, const char **out) {
    if (*i + 1 >= argc) {
        fprintf(stderr, "option '%s' requires a value (try --help)\n", option);
        return false;
    }
    *out = argv[++(*i)];
    return true;
}

static bool parse_int_value(const char *s, int min_value, int max_value, int *out) {
    if (!s || !*s) return false;
    char *end = NULL;
    errno = 0;
    long v = strtol(s, &end, 10);
    if (errno == ERANGE || end == s || *end || v < min_value || v > max_value) return false;
    *out = (int)v;
    return true;
}

static bool parse_float_value(const char *s, float min_value, float max_value, float *out) {
    if (!s || !*s) return false;
    char *end = NULL;
    errno = 0;
    float v = strtof(s, &end);
    if (errno == ERANGE || end == s || *end || !isfinite(v) || v < min_value || v > max_value) return false;
    *out = v;
    return true;
}

static bool parse_seed_value(const char *s, uint64_t *out) {
    if (!s || !*s || *s == '-') return false;
    char *end = NULL;
    errno = 0;
    unsigned long long v = strtoull(s, &end, 10);
    if (errno == ERANGE || end == s || *end) return false;
    *out = (uint64_t)v;
    return true;
}

static parse_result_t parse_args(int argc, char **argv, options_t *o) {
    memset(o, 0, sizeof *o);
    o->scene = -1;
    o->palette = -1;
    o->fps = 30.0f;
    o->audio = true;
    o->autoshuffle = true;
    o->seed = (uint64_t)time(NULL) ^ ((uint64_t)clock() << 20);

    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        const char *v = NULL;
        if (!strcmp(a, "-h") || !strcmp(a, "--help")) {
            usage(argv[0]);
            return PARSE_EXIT_OK;
        } else if (!strcmp(a, "-v") || !strcmp(a, "--version")) {
            printf("%s %s\n", APP_NAME, APP_VERSION);
            return PARSE_EXIT_OK;
        } else if (!strcmp(a, "-l") || !strcmp(a, "--list")) {
            o->list = true;
        } else if (!strcmp(a, "-n")) {
            o->audio = false;
        } else if (!strcmp(a, "-a")) {
            o->autoshuffle = false;
        } else if (!strcmp(a, "-t")) {
            o->color = 1;
        } else if (!strcmp(a, "-2")) {
            o->color = 2;
        } else if (!strcmp(a, "-A")) {
            o->ascii = true;
        } else if (!strcmp(a, "-j")) {
            o->journey = true;
        } else if (!strcmp(a, "-s")) {
            if (!take_value(argc, argv, &i, a, &v)) return PARSE_ERROR;
            o->scene = scene_find(v);
            if (o->scene < 0) {
                fprintf(stderr, "unknown scene '%s' (try --list)\n", v);
                return PARSE_ERROR;
            }
        } else if (!strcmp(a, "-p")) {
            if (!take_value(argc, argv, &i, a, &v)) return PARSE_ERROR;
            o->palette = palette_find(v);
            if (o->palette < 0) {
                fprintf(stderr, "unknown palette '%s' (try --list)\n", v);
                return PARSE_ERROR;
            }
        } else if (!strcmp(a, "-f")) {
            if (!take_value(argc, argv, &i, a, &v)) return PARSE_ERROR;
            if (!parse_float_value(v, 10.0f, 60.0f, &o->fps)) {
                fprintf(stderr, "invalid FPS '%s' (expected 10-60)\n", v);
                return PARSE_ERROR;
            }
        } else if (!strcmp(a, "-S")) {
            if (!take_value(argc, argv, &i, a, &v)) return PARSE_ERROR;
            if (!parse_seed_value(v, &o->seed)) {
                fprintf(stderr, "invalid seed '%s' (expected an unsigned integer)\n", v);
                return PARSE_ERROR;
            }
        } else if (!strcmp(a, "-d")) {
            if (!take_value(argc, argv, &i, a, &o->dump)) return PARSE_ERROR;
            if (!*o->dump) {
                fprintf(stderr, "audio output path cannot be empty\n");
                return PARSE_ERROR;
            }
        } else if (!strcmp(a, "-F")) {
            if (!take_value(argc, argv, &i, a, &v)) return PARSE_ERROR;
            if (!parse_int_value(v, 1, INT_MAX, &o->frames)) {
                fprintf(stderr, "invalid frame count '%s' (expected a positive integer)\n", v);
                return PARSE_ERROR;
            }
        } else if (!strcmp(a, "-O")) {
            if (!take_value(argc, argv, &i, a, &o->offline)) return PARSE_ERROR;
            if (!*o->offline) {
                fprintf(stderr, "WAV output path cannot be empty\n");
                return PARSE_ERROR;
            }
        } else if (!strcmp(a, "-B")) {
            if (!take_value(argc, argv, &i, a, &v)) return PARSE_ERROR;
            if (!parse_int_value(v, 1, INT_MAX, &o->bench)) {
                fprintf(stderr, "invalid benchmark frame count '%s' (expected a positive integer)\n", v);
                return PARSE_ERROR;
            }
        } else if (!strcmp(a, "-T")) {
            if (!take_value(argc, argv, &i, a, &v)) return PARSE_ERROR;
            if (!parse_float_value(v, 0.0f, 1000000.0f, &o->sleep_min)) {
                fprintf(stderr, "invalid sleep timer '%s' (expected 0 or more minutes)\n", v);
                return PARSE_ERROR;
            }
        } else if (!strcmp(a, "-x")) {
            if (!take_value(argc, argv, &i, a, &v)) return PARSE_ERROR;
            if (!parse_float_value(v, 0.25f, 3.0f, &o->speed)) {
                fprintf(stderr, "invalid speed '%s' (expected 0.25-3)\n", v);
                return PARSE_ERROR;
            }
        } else {
            fprintf(stderr, "unknown option '%s' (try --help)\n", a);
            return PARSE_ERROR;
        }
    }

    if (o->offline && o->dump) {
        fprintf(stderr, "-O and -d cannot be used together; choose WAV or raw audio output\n");
        return PARSE_ERROR;
    }
    if (o->offline && !o->audio) {
        fprintf(stderr, "-O requires audio rendering; remove -n\n");
        return PARSE_ERROR;
    }
    if (o->dump && !o->audio) {
        fprintf(stderr, "-d cannot be used with -n\n");
        return PARSE_ERROR;
    }
    return PARSE_OK;
}

static int run_bench(int frames) {
    const int cols = 120, rows = 34;
    term_headless(cols, rows);
    canvas_t c;
    canvas_init(&c, cols, rows);
    const palette_t *pal = palette_get(0);
    printf("%-10s %10s %12s\n", "scene", "ms/frame", "bytes/frame");
    double total = 0.0;
    for (int i = 0; i < scene_count(); i++) {
        scene_instance_t s;
        memset(&s, 0, sizeof s);
        scene_start(&s, i, cols, rows);
        double start = now_seconds(), t = 0.0;
        size_t bytes = 0;
        for (int f = 0; f < frames; f++) {
            t += 1.0 / 30.0;
            scene_update(&s, 1.0 / 30.0, t);
            canvas_clear(&c);
            scene_draw(&s, &c, t);
            canvas_render(&c, pal);
            bytes += out_pending();
            out_flush();
        }
        double ms = (now_seconds() - start) * 1000.0 / (double)frames;
        total += ms;
        printf("%-10s %10.2f %12zu\n", s.def->name, ms, bytes / (size_t)frames);
        scene_stop(&s);
    }
    printf("%-10s %10.2f\n", "total", total);
    canvas_free(&c);
    return 0;
}

static void apply_scene(int idx) {
    int cols, rows;
    term_get_size(&cols, &rows);
    scene_stop(&G.scene);
    scene_start(&G.scene, idx, cols, rows);
    audio_set_bed(G.scene.def->bed);
    G.pending_scene = idx;
}

static void request_scene(int idx) {
    int n = scene_count();
    idx = ((idx % n) + n) % n;
    if (idx == G.scene.index && !G.fading_out) return;
    G.pending_scene = idx;
    G.fading_out = true;
}

static void set_palette(int idx) {
    int n = palette_count();
    idx = ((idx % n) + n) % n;
    G.pal_from = G.pal_cur;
    G.pal_to = *palette_get(idx);
    G.pal_index = idx;
    G.pal_t = 0.0f;
}

static void shuffle_all(void) {
    int s = RI(scene_count() - 1);
    if (s >= G.scene.index) s++;
    request_scene(s);
    int p = RI(palette_count() - 1);
    if (p >= G.pal_index) p++;
    set_palette(p);
}

static void user_scene(bool *autoshuffle, int idx) {
    *autoshuffle = false;
    if (G.journey_on) {
        journey_stop();
        G.journey_on = false;
    }
    request_scene(idx);
}

static void cycle_sleep(void) {
    static const int opts[4] = {0, 15, 30, 60};
    static int idx = 0;
    idx = (idx + 1) % 4;
    if (opts[idx] == 0) {
        G.sleep_left = -1.0;
        G.sleep_mul = 1.0f;
        audio_set_fade(1.0f);
        ui_toast("sleep timer off");
    } else {
        G.sleep_left = opts[idx] * 60.0;
        ui_toast("sleep timer: %d min", opts[idx]);
    }
}

static void toggle_journey(bool *autoshuffle) {
    G.journey_on = !G.journey_on;
    if (G.journey_on) {
        journey_start();
        *autoshuffle = false;
        ui_toast("journey started");
    } else {
        journey_stop();
        ui_toast("journey stopped");
    }
}

static void take_screenshot(void) {
    char base[64];
    snprintf(base, sizeof base, "asmr-%ld", (long)time(NULL));
    if (canvas_save_ansi(&G.canvas, base))
        ui_toast("saved %s.ans / .txt", base);
    else
        ui_toast("could not save screenshot");
}

static void handle_action(ui_action_t a, int arg, bool *autoshuffle) {
    switch (a) {
    case UI_SCENE: user_scene(autoshuffle, arg); break;
    case UI_SHUFFLE: shuffle_all(); break;
    case UI_AUTO:
        *autoshuffle = !*autoshuffle;
        ui_toast("auto shuffle %s", *autoshuffle ? "on" : "off");
        break;
    case UI_SOUND:
        if (!audio_available()) {
            ui_toast("no audio player found");
        } else {
            audio_set_enabled(!audio_enabled());
            ui_toast("sound %s", audio_enabled() ? "on" : "off");
        }
        break;
    case UI_PALETTE:
        set_palette(G.pal_index + 1);
        ui_toast("palette: %s", palette_get(G.pal_index)->name);
        break;
    case UI_HELP: ui_toggle_help(); break;
    case UI_QUIT: G.quit = true; break;
    default: break;
    }
}

static void handle_key(int key, bool *autoshuffle) {
    if (ui_menu_open()) {
        switch (key) {
        case K_UP: ui_menu_move(-1, scene_count()); break;
        case K_DOWN: ui_menu_move(1, scene_count()); break;
        case K_ENTER:
            user_scene(autoshuffle, ui_menu_selected());
            ui_menu_close();
            break;
        case K_ESC: case 'g': ui_menu_close(); break;
        case 'q': case 'Q': case 3: G.quit = true; break;
        default: break;
        }
        return;
    }
    switch (key) {
    case 'q': case 'Q': case 3: G.quit = true; break;
    case K_ESC:
        if (ui_help_open()) ui_close_help();
        else G.quit = true;
        break;
    case ' ': case 's': handle_action(UI_SHUFFLE, 0, autoshuffle); break;
    case 'a': handle_action(UI_AUTO, 0, autoshuffle); break;
    case 'm': handle_action(UI_SOUND, 0, autoshuffle); break;
    case '?': case 'h': handle_action(UI_HELP, 0, autoshuffle); break;
    case '[':
        G.speed = clampf(G.speed - 0.25f, 0.25f, 3.0f);
        ui_toast("speed %.2fx", (double)G.speed);
        break;
    case ']':
        G.speed = clampf(G.speed + 0.25f, 0.25f, 3.0f);
        ui_toast("speed %.2fx", (double)G.speed);
        break;
    case 't': cycle_sleep(); break;
    case 'b':
        breath_toggle();
        ui_toast("breathing guide %s", breath_active() ? "on" : "off");
        break;
    case 'j': toggle_journey(autoshuffle); break;
    case 'x': take_screenshot(); break;
    case ',': user_scene(autoshuffle, G.scene.index - 1); break;
    case '.': user_scene(autoshuffle, G.scene.index + 1); break;
    case '0': user_scene(autoshuffle, 9); break;
    case 'g': ui_menu_toggle(G.scene.index); break;
    case 'r': {
        palette_t p;
        palette_random(&p, RF());
        G.pal_from = G.pal_cur;
        G.pal_to = p;
        G.pal_t = 0.0f;
        ui_toast("palette: random");
        break;
    }
    case 'u':
        G.hud_off = !G.hud_off;
        ui_toast(G.hud_off ? "interface hidden - press u to show" : "interface shown");
        break;
    case 'y':
        audio_set_binaural(!audio_binaural());
        ui_toast("binaural layer %s%s", audio_binaural() ? "on" : "off", audio_binaural() ? " (use headphones)" : "");
        break;
    case 'p':
        set_palette(G.pal_index + 1);
        ui_toast("palette: %s", palette_get(G.pal_index)->name);
        break;
    case '+': case '=':
        audio_set_volume(audio_volume() + 0.1f);
        ui_toast("volume %d%%", (int)(audio_volume() * 100.0f + 0.5f));
        break;
    case '-': case '_':
        audio_set_volume(audio_volume() - 0.1f);
        ui_toast("volume %d%%", (int)(audio_volume() * 100.0f + 0.5f));
        break;
    case K_TAB: case K_RIGHT: case K_DOWN: user_scene(autoshuffle, G.scene.index + 1); break;
    case K_LEFT: case K_UP: user_scene(autoshuffle, G.scene.index - 1); break;
    default:
        if (key >= '1' && key <= '9') user_scene(autoshuffle, key - '1');
        break;
    }
}


static bool open_site_link(void) {
    pid_t pid = fork();
    if (pid < 0) return false;
    if (pid == 0) {
#ifdef __APPLE__
        execlp("open", "open", SITE_URL, (char *)NULL);
#else
        execlp("xdg-open", "xdg-open", SITE_URL, (char *)NULL);
        execlp("open", "open", SITE_URL, (char *)NULL);
#endif
        _exit(127);
    }
    int status = 0;
    while (waitpid(pid, &status, 0) < 0) {
        if (errno == EINTR) continue;
        return false;
    }
    return WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

static void handle_mouse(const term_event_t *ev, bool *autoshuffle, bool *down, double now) {
    static double last_poke = 0.0;
    if (ev->mouse == MOUSE_WHEEL_UP || ev->mouse == MOUSE_WHEEL_DOWN) {
        audio_set_volume(audio_volume() + (ev->mouse == MOUSE_WHEEL_UP ? 0.05f : -0.05f));
        ui_toast("volume %d%%", (int)(audio_volume() * 100.0f + 0.5f));
        return;
    }
    if (ev->mouse == MOUSE_RELEASE) {
        *down = false;
        return;
    }
    if (ui_menu_open()) {
        if (ev->mouse == MOUSE_PRESS) {
            int idx = ui_menu_hit(ev->x, ev->y);
            if (idx >= 0) user_scene(autoshuffle, idx);
            ui_menu_close();
        }
        return;
    }
    if (ev->mouse == MOUSE_PRESS) {
        if (ui_link_hit(ev->x, ev->y)) {
            if (open_site_link()) ui_toast("opened %s", SITE_LABEL);
            else ui_toast("could not open link: %s", SITE_LABEL);
            return;
        }
        int arg = 0;
        ui_action_t a = ui_hit_test(ev->x, ev->y, &arg);
        if (a != UI_NONE) {
            handle_action(a, arg, autoshuffle);
            return;
        }
        *down = true;
        scene_poke(&G.scene, ev->x, ev->y, 3);
        last_poke = now;
    } else if (ev->mouse == MOUSE_DRAG && *down && now - last_poke > 0.08) {
        scene_poke(&G.scene, ev->x, ev->y, 1);
        last_poke = now;
    }
}

static void drain_events(bool *autoshuffle, bool *down, double now) {
    term_event_t ev;
    while (term_next_event(&ev)) {
        G.last_input = now;
        if (ev.type == EV_MOUSE) {
            G.mouse_x = ev.x;
            G.mouse_y = ev.y;
            G.mouse_t = now;
        }
        if (ev.type == EV_KEY) handle_key(ev.key, autoshuffle);
        else if (ev.type == EV_MOUSE) handle_mouse(&ev, autoshuffle, down, now);
    }
}

static void step_fade(float dt) {
    if (G.fading_out) {
        G.fade -= dt / 0.22f;
        if (G.fade <= 0.0f) {
            G.fade = 0.0f;
            G.fading_out = false;
            apply_scene(G.pending_scene);
        }
    } else if (G.fade < 1.0f) {
        G.fade = fminf(1.0f, G.fade + dt / 0.45f);
    }
    G.canvas.master = G.fade * G.sleep_mul;
}

int main(int argc, char **argv) {
    options_t o;
    parse_result_t parsed = parse_args(argc, argv, &o);
    if (parsed == PARSE_EXIT_OK) return 0;
    if (parsed == PARSE_ERROR) return 2;
    if (o.list) {
        list_all();
        return 0;
    }

    rng_seed(rng_global(), o.seed);
    noise_init(o.seed);

    if (o.bench > 0) return run_bench(o.bench);

    if (o.offline && o.frames <= 0) o.frames = 1800;
    bool headless = o.frames > 0;
    bool offline_audio = o.offline != NULL || (headless && o.dump != NULL);
    if (headless) {
        term_headless(110, 32);
    } else if (!term_init()) {
        fprintf(stderr, "%s needs an interactive terminal (stdin and stdout must be TTYs).\n", APP_NAME);
        return 1;
    }

    if (!headless) term_set_truecolor(o.color == 1 ? true : (o.color == 2 ? false : term_detect_truecolor()));
    bool audio_ok = o.offline ? audio_offline_begin(o.offline)
                              : ((headless && o.dump) ? audio_offline_raw_begin(o.dump) : audio_init(o.audio, o.dump));
    if (!audio_ok && (o.offline || o.dump)) {
        fprintf(stderr, "could not initialize audio output%s%s\n", o.offline ? " WAV file: " : " file: ", o.offline ? o.offline : (o.dump ? o.dump : ""));
        audio_shutdown();
        term_shutdown();
        return 1;
    }
    config_t cfg;
    config_load(&cfg);
    audio_set_volume(cfg.volume);
    if (!cfg.sound && !o.offline) audio_set_enabled(false);
    int cols, rows;
    term_get_size(&cols, &rows);
    canvas_init(&G.canvas, cols, rows);
    ui_init(cols, rows);
    ui_set_ascii(o.ascii);

    int nscenes = scene_count();
    for (int i = 0; i < nscenes && i < 32; i++) {
        G.blurbs[i] = scene_def(i)->blurb;
        snprintf(G.names_buf[i], sizeof G.names_buf[i], "%s", scene_def(i)->name);
        G.names[i] = G.names_buf[i];
    }

    int first = o.scene >= 0 ? o.scene : RI(nscenes);
    G.pal_index = o.palette >= 0 ? o.palette : RI(palette_count());
    G.pal_cur = G.pal_from = G.pal_to = *palette_get(G.pal_index);
    G.pal_t = 1.0f;
    G.fade = 0.0f;
    apply_scene(first);

    bool autoshuffle = o.autoshuffle && cfg.autoshuffle, down = false;
    const double frame_dt = 1.0 / (double)o.fps;
    double next_frame = now_seconds();
    G.next_auto = 40.0;
    G.speed = o.speed > 0.0f ? o.speed : cfg.speed;
    G.sleep_left = o.sleep_min > 0.0f ? (double)o.sleep_min * 60.0 : -1.0;
    G.sleep_mul = 1.0f;
    G.last_input = now_seconds();
    G.mouse_x = G.mouse_y = -1;
    if (o.journey) {
        G.journey_on = true;
        journey_start();
        autoshuffle = false;
    }
    int frame = 0;
    bool render_failed = false;
    double off_acc = 0.0;
    double bench_start = now_seconds();

    while (!G.quit && !term_quit_requested()) {
        double dt = frame_dt;
        G.sim_time += dt;
        G.scene_time += dt * (double)G.speed;
        if (G.sleep_left >= 0.0) {
            G.sleep_left -= dt;
            float f = clampf((float)(G.sleep_left / 30.0), 0.0f, 1.0f);
            G.sleep_mul = 0.1f + 0.9f * f;
            audio_set_fade(f);
            if (G.sleep_left <= 0.0) G.quit = true;
        }
        if (G.journey_on) {
            int js = 0, jp = 0;
            if (journey_update(dt, &js, &jp)) {
                request_scene(js);
                set_palette(jp);
            }
        }
        G.idle = !headless && !G.hud_off && !ui_help_open() && !ui_menu_open() && now_seconds() - G.last_input > 8.0;
        breath_update(dt);

        if (term_take_resize()) {
            term_get_size(&cols, &rows);
            canvas_resize(&G.canvas, cols, rows);
            scene_resize(&G.scene, cols, rows);
            ui_resize(cols, rows);
            out_str("\x1b[2J");
        }

        if (autoshuffle && !G.journey_on && G.sim_time > G.next_auto) {
            G.next_auto = G.sim_time + 35.0 + (double)RR(0.0f, 20.0f);
            shuffle_all();
        }

        step_fade((float)dt);
        if (G.pal_t < 1.0f) {
            G.pal_t = fminf(1.0f, G.pal_t + (float)dt / 2.5f);
            float e = G.pal_t * G.pal_t * (3.0f - 2.0f * G.pal_t);
            palette_blend(&G.pal_from, &G.pal_to, e, &G.pal_cur);
        } else {
            G.pal_cur = G.pal_to;
        }

        scene_update(&G.scene, dt * (double)G.speed, G.scene_time);
        audio_set_energy(scene_energy(&G.scene));
        ui_update(dt);

        canvas_clear(&G.canvas);
        scene_draw(&G.scene, &G.canvas, G.scene_time);
        if (G.mouse_x >= 0 && !headless) {
            float age = (float)(now_seconds() - G.mouse_t);
            if (age < 1.5f) {
                float k = 1.0f - age / 1.5f;
                canvas_glow_point(&G.canvas, (float)G.mouse_x, (float)G.mouse_y, 7.0f, 3.5f, 0.55f * k);
                canvas_put(&G.canvas, G.mouse_x, G.mouse_y, '+', 0.6f * k, 0.9f);
            }
        }

        G.fps_acc += dt;
        G.fps_frames++;
        if (G.fps_acc >= 1.0) {
            G.fps_shown = (double)G.fps_frames / G.fps_acc;
            G.fps_acc = 0.0;
            G.fps_frames = 0;
        }

        ui_ctx_t ctx = {
            .scene_index = G.scene.index,
            .scene_count = nscenes,
            .scene_name = G.scene.def->name,
            .scene_blurb = G.scene.def->blurb,
            .palette_name = G.pal_cur.name,
            .audio_on = audio_enabled(),
            .audio_available = audio_ok && audio_available(),
            .hud_hidden = G.hud_off,
            .volume = audio_volume(),
            .autoshuffle = autoshuffle,
            .fps = G.fps_shown,
            .idle = G.idle,
            .level = audio_peak(),
            .blurbs = G.blurbs,
            .sleep_left = G.sleep_left,
            .speed = G.speed,
            .journey_step = journey_step(),
            .journey_total = G.journey_on ? journey_steps() : 0,
        };
        canvas_clear_overlay(&G.canvas);
        breath_draw(&G.canvas, &G.pal_cur, cols, rows);
        ui_draw(&G.canvas, &G.pal_cur, &ctx, G.names);
        canvas_render(&G.canvas, &G.pal_cur);
        ui_emit_link(&G.pal_cur);
        out_flush();
        frame++;

        if (headless) {
            if (offline_audio) {
                off_acc += 44100.0 / (double)o.fps;
                int n = (int)off_acc;
                off_acc -= n;
                if (!audio_offline_step(n)) {
                    fprintf(stderr, "failed while writing offline audio output%s%s\n",
                            o.offline ? " WAV file: " : " file: ", o.offline ? o.offline : (o.dump ? o.dump : ""));
                    render_failed = true;
                    break;
                }
            }
            if (frame >= o.frames) break;
            continue;
        }
        if (o.dump && audio_ok && !audio_available()) {
            fprintf(stderr, "raw audio output stopped unexpectedly: %s\n", o.dump);
            render_failed = true;
            G.quit = true;
        }

        next_frame += frame_dt;
        double rem;
        while ((rem = next_frame - now_seconds()) > 0.0) {
            term_wait(rem);
            drain_events(&autoshuffle, &down, now_seconds());
        }
        if (now_seconds() > next_frame + 0.25) next_frame = now_seconds();
    }

    double elapsed = now_seconds() - bench_start;
    if (!headless) {
        config_t out = {audio_volume(), G.speed, autoshuffle, audio_enabled()};
        config_save(&out);
    }
    bool audio_finished_ok = audio_ok && audio_available();
    audio_shutdown();
    scene_stop(&G.scene);
    canvas_free(&G.canvas);
    term_shutdown();
    if (headless) {
        printf("rendered %d frames of '%s' in %.3fs (%.1f ms/frame), audio: %s\n", frame, scene_def(first)->name,
               elapsed, elapsed * 1000.0 / (double)(frame ? frame : 1), audio_finished_ok ? "ok" : "off");
    }
    return render_failed ? 1 : 0;
}
