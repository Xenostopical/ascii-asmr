/* Unit tests. Build and run:
 *   make test
 * or by hand, from the project root:
 *   gcc -std=gnu11 -O1 -g -I. -o asmr_test tests/test_all.c $(ls *.c | grep -v '^main.c$') -lm -lpthread
 * main.c is the only file not linked; everything else is exercised directly. */

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

#include <math.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int g_checks = 0, g_failed = 0;
static const char *g_group = "";

#define CHECK(cond)                                                                                   \
    do {                                                                                              \
        g_checks++;                                                                                   \
        if (!(cond)) {                                                                                \
            g_failed++;                                                                               \
            printf("  FAIL [%s] %s:%d  %s\n", g_group, __FILE__, __LINE__, #cond);                    \
        }                                                                                             \
    } while (0)

#define GROUP(name) (g_group = (name), printf("%s\n", name))

static void test_common(void) {
    GROUP("common");
    CHECK(strcmp(SITE_URL, "https://gg.elitehub.page.gd/") == 0);
    CHECK(strcmp(SITE_LABEL, "Visit my site") == 0);
    char buf[4];
    CHECK(utf8_encode('A', buf) == 1 && buf[0] == 'A');
    CHECK(utf8_encode(0xE9, buf) == 2);
    CHECK(utf8_encode(0x2588, buf) == 3 && (unsigned char)buf[0] == 0xE2);
    CHECK(utf8_encode(0x1F600, buf) == 4);
    CHECK(utf8_encode(0xD800, buf) == 3 && (unsigned char)buf[0] == 0xEF &&
          (unsigned char)buf[1] == 0xBF && (unsigned char)buf[2] == 0xBD);
    CHECK(utf8_encode(0x110000, buf) == 3 && (unsigned char)buf[0] == 0xEF);
    CHECK(clampf(5.0f, 0.0f, 1.0f) == 1.0f && clampf(-1.0f, 0.0f, 1.0f) == 0.0f);
    CHECK(clampi(7, 0, 3) == 3 && clampi(-2, 0, 3) == 0);
    CHECK(fabsf(smoothstepf(0.0f, 1.0f, 0.5f) - 0.5f) < 1e-6f);
    CHECK(smoothstepf(1.0f, 1.0f, 0.0f) == 0.0f && smoothstepf(1.0f, 1.0f, 1.0f) == 1.0f);
    CHECK(remapf(2.0f, 2.0f, 2.0f, 10.0f, 20.0f) == 20.0f);
    CHECK(smoothstepf(0.0f, 1.0f, -3.0f) == 0.0f && smoothstepf(0.0f, 1.0f, 3.0f) == 1.0f);
    CHECK(fabsf(fractf(-0.25f) - 0.75f) < 1e-6f);
    CHECK(fabsf(remapf(5.0f, 0.0f, 10.0f, 100.0f, 200.0f) - 150.0f) < 1e-4f);
    char small[6];
    CHECK(str_copy(small, sizeof small, "hello world") == 5 && strcmp(small, "hello") == 0);
    double a = now_seconds();
    sleep_seconds(0.02);
    CHECK(now_seconds() - a >= 0.015);
}

static void test_rng(void) {
    GROUP("rng");
    rng_t a, b, c;
    rng_seed(&a, 42);
    rng_seed(&b, 42);
    rng_seed(&c, 43);
    bool same = true, differs = false;
    for (int i = 0; i < 100; i++) {
        uint64_t x = rng_next(&a), y = rng_next(&b), z = rng_next(&c);
        if (x != y) same = false;
        if (x != z) differs = true;
    }
    CHECK(same);
    CHECK(differs);

    rng_seed(&a, 7);
    double sum = 0.0, gsum = 0.0, gsq = 0.0;
    bool in_range = true, int_ok = true;
    const int n = 20000;
    for (int i = 0; i < n; i++) {
        float f = rng_float(&a);
        if (f < 0.0f || f >= 1.0f) in_range = false;
        sum += f;
        int k = rng_int(&a, 10);
        if (k < 0 || k >= 10) int_ok = false;
        float g = rng_gauss(&a);
        gsum += g;
        gsq += (double)g * g;
    }
    CHECK(in_range);
    CHECK(int_ok);
    CHECK(fabs(sum / n - 0.5) < 0.02);
    CHECK(fabs(gsum / n) < 0.05);
    CHECK(fabs(gsq / n - 1.0) < 0.08);
    CHECK(rng_int(&a, 0) == 0 && rng_int(&a, 1) == 0);
    int hits = 0;
    for (int i = 0; i < n; i++) hits += rng_chance(&a, 0.25f);
    CHECK(abs(hits - n / 4) < 400);
    float r = rng_range(&a, 3.0f, 4.0f);
    CHECK(r >= 3.0f && r < 4.0f);
    CHECK(hash_u32(1) != hash_u32(2));
    float h = hash_float(12345);
    CHECK(h >= 0.0f && h < 1.0f);
}

static void test_noise(void) {
    GROUP("noise");
    noise_init(99);
    float lo = 10.0f, hi = -10.0f, prev = noise2(0.0f, 0.0f), max_step = 0.0f;
    bool bounded = true;
    for (int i = 0; i < 20000; i++) {
        float x = (float)i * 0.013f, y = (float)(i % 97) * 0.17f;
        float v = noise2(x, y), w = noise3(x, y, 1.3f);
        if (fabsf(v) > 1.6f || fabsf(w) > 1.6f || !isfinite(v) || !isfinite(w)) bounded = false;
        lo = fminf(lo, v);
        hi = fmaxf(hi, v);
        if (i > 0 && (i % 97) != 0) max_step = fmaxf(max_step, fabsf(v - prev));
        prev = v;
    }
    CHECK(bounded);
    CHECK(lo < -0.3f && hi > 0.3f);
    CHECK(noise2(3.0f, 4.0f) == 0.0f || fabsf(noise2(3.0f, 4.0f)) < 1e-5f);
    float f = fbm2(1.7f, 2.9f, 4), g = fbm3(1.7f, 2.9f, 0.5f, 4);
    CHECK(isfinite(f) && fabsf(f) <= 1.6f && isfinite(g) && fabsf(g) <= 1.6f);
    CHECK(fbm2(1.0f, 1.0f, 0) == 0.0f);

    float before = noise2(5.31f, 8.77f);
    noise_init(99);
    CHECK(noise2(5.31f, 8.77f) == before);
    noise_init(100);
    bool changed = false;
    for (int i = 0; i < 20 && !changed; i++) {
        float p = 0.37f + (float)i * 0.91f;
        noise_init(99);
        float one = noise2(p, p * 1.3f);
        noise_init(100);
        if (one != noise2(p, p * 1.3f)) changed = true;
    }
    CHECK(changed);

    float vx, vy;
    curl2(2.0f, 3.0f, 0.5f, &vx, &vy);
    CHECK(isfinite(vx) && isfinite(vy));
    noise_init(1);
}

static void test_palette(void) {
    GROUP("palette");
    rgb_t black = rgb_make(0, 0, 0), white = rgb_make(255, 255, 255);
    rgb_t m = rgb_mix(black, white, 0.5f);
    CHECK(m.r == 128 && m.g == 128 && m.b == 128);
    CHECK(rgb_mix(black, white, 0.0f).r == 0 && rgb_mix(black, white, 1.0f).r == 255);
    CHECK(rgb_mix(black, white, 9.0f).r == 255 && rgb_mix(black, white, -9.0f).r == 0);
    CHECK(rgb_scale(white, 2.0f).r == 255 && rgb_scale(white, 0.5f).g == 128);
    CHECK(rgb_add(rgb_make(200, 0, 0), white, 1.0f).r == 255);
    CHECK(rgb_make(-5, 300, 7).r == 0 && rgb_make(-5, 300, 7).g == 255);

    rgb_t r = hsv_to_rgb(0.0f, 1.0f, 1.0f), g = hsv_to_rgb(1.0f / 3.0f, 1.0f, 1.0f), b = hsv_to_rgb(2.0f / 3.0f, 1.0f, 1.0f);
    CHECK(r.r == 255 && r.g == 0 && r.b == 0);
    CHECK(g.g == 255 && g.r == 0);
    CHECK(b.b == 255 && b.r == 0);
    CHECK(rgb_luma(white) > 0.99f && rgb_luma(black) == 0.0f);

    int n = palette_count();
    CHECK(n >= 8);
    CHECK(palette_get(0) == palette_get(n));
    CHECK(palette_get(-1) == palette_get(n - 1));
    for (int i = 0; i < n; i++) CHECK(palette_find(palette_get(i)->name) == i);
    CHECK(palette_find("nonexistent") == -1);

    palette_t out;
    palette_blend(palette_get(0), palette_get(1), 0.0f, &out);
    CHECK(out.ink_a.r == palette_get(0)->ink_a.r);
    palette_blend(palette_get(0), palette_get(1), 1.0f, &out);
    CHECK(out.ink_a.r == palette_get(1)->ink_a.r && strcmp(out.name, palette_get(1)->name) == 0);
    palette_random(&out, 0.3f);
    CHECK(rgb_luma(out.bg_top) < rgb_luma(out.ink_a));
}

static void test_color(void) {
    GROUP("color");
    CHECK(term_rgb_to_256(rgb_make(0, 0, 0)) == 16);
    CHECK(term_rgb_to_256(rgb_make(255, 255, 255)) == 231);
    CHECK(term_rgb_to_256(rgb_make(255, 0, 0)) == 196);
    CHECK(term_rgb_to_256(rgb_make(0, 255, 0)) == 46);
    CHECK(term_rgb_to_256(rgb_make(0, 0, 255)) == 21);
    CHECK(term_rgb_to_256(rgb_make(128, 128, 128)) == 244);
    int prev = -1;
    bool in_range = true, mono_gray = true;
    for (int v = 0; v < 256; v += 5) {
        int idx = term_rgb_to_256(rgb_make(v, v, v));
        if (idx < 16 || idx > 255) in_range = false;
        if (idx >= 232) {
            if (idx < prev && prev >= 232) mono_gray = false;
            prev = idx;
        }
    }
    CHECK(in_range && mono_gray);
    for (int i = 0; i < 3000; i++) {
        rgb_t c = rgb_make(RI(256), RI(256), RI(256));
        int idx = term_rgb_to_256(c);
        if (idx < 16 || idx > 255) in_range = false;
    }
    CHECK(in_range);

    term_set_truecolor(false);
    CHECK(!term_truecolor());
    out_fg(rgb_make(255, 0, 0));
    out_bg(rgb_make(0, 0, 255));
    CHECK(out_pending() == strlen("\x1b[38;5;196m") + strlen("\x1b[48;5;21m"));
    out_flush();
    term_set_truecolor(true);
    out_fg(rgb_make(1, 2, 3));
    CHECK(out_pending() == strlen("\x1b[38;2;1;2;3m"));
    out_flush();
    setenv("COLORTERM", "truecolor", 1);
    CHECK(term_detect_truecolor());
    setenv("COLORTERM", "24bit", 1);
    CHECK(term_detect_truecolor());
    setenv("COLORTERM", "", 1);
    CHECK(!term_detect_truecolor());
    unsetenv("COLORTERM");
    CHECK(!term_detect_truecolor());
}

static void test_ui(void) {
    GROUP("ui");
    static const char *names[80], *blurbs[80];
    static char name_buf[80][24];
    for (int i = 0; i < 80; i++) {
        snprintf(name_buf[i], sizeof name_buf[i], "scene%d", i);
        names[i] = name_buf[i];
        blurbs[i] = "a description of the scene";
    }
    static const int sizes[][2] = {{120, 36}, {80, 24}, {60, 20}, {40, 12}, {24, 8}, {200, 60}};
    static const int counts[] = {1, 5, 22, 40, 60};
    const palette_t *pal = palette_get(0);
    for (size_t k = 0; k < ARRAY_LEN(sizes); k++) {
        for (size_t q = 0; q < ARRAY_LEN(counts); q++) {
            int cols = sizes[k][0], rows = sizes[k][1], count = counts[q];
            canvas_t c;
            canvas_init(&c, cols, rows);
            ui_init(cols, rows);
            ui_ctx_t ctx;
            memset(&ctx, 0, sizeof ctx);
            ctx.scene_count = count;
            ctx.scene_index = count / 2;
            ctx.scene_name = names[ctx.scene_index];
            ctx.scene_blurb = blurbs[ctx.scene_index];
            ctx.palette_name = "lichen";
            ctx.audio_on = ctx.audio_available = true;
            ctx.level = 0.6f;
            ctx.speed = 1.5f;
            ctx.sleep_left = 125.0;
            ctx.journey_step = 2;
            ctx.journey_total = 22;
            ctx.fps = 30.0;
            ctx.blurbs = blurbs;
            for (int pass = 0; pass < 4; pass++) {
                ctx.idle = pass == 3;
                if (pass == 1) ui_toggle_help();
                if (pass == 2) {
                    ui_toggle_help();
                    ui_menu_toggle(ctx.scene_index);
                }
                ui_toast("toast %d", pass);
                canvas_clear_overlay(&c);
                ui_update(0.1);
                ui_draw(&c, pal, &ctx, names);
                canvas_render(&c, pal);
                out_flush();
            }
            ui_menu_close();
            ui_set_ascii(true);
            canvas_clear_overlay(&c);
            ui_draw(&c, pal, &ctx, names);
            canvas_render(&c, pal);
            out_flush();
            canvas_free(&c);
        }
    }
    CHECK(true);

    canvas_t c;
    canvas_init(&c, 120, 36);
    ui_init(120, 36);
    ui_set_ascii(false);
    ui_ctx_t ctx;
    memset(&ctx, 0, sizeof ctx);
    ctx.scene_count = 22;
    ctx.scene_name = names[0];
    ctx.scene_blurb = blurbs[0];
    ctx.blurbs = blurbs;
    ctx.speed = 1.0f;
    ctx.sleep_left = -1.0;
    canvas_clear_overlay(&c);
    ui_draw(&c, pal, &ctx, names);
    int hits = 0, shuffle_seen = 0, palette_seen = 0, link_hits = 0;
    for (int y = 0; y < 36; y++) {
        for (int x = 0; x < 120; x++) {
            int arg = -1;
            ui_action_t a = ui_hit_test(x, y, &arg);
            if (a == UI_SCENE) {
                hits++;
                CHECK(arg >= 0 && arg < 22);
            }
            if (a == UI_SHUFFLE) shuffle_seen++;
            if (a == UI_PALETTE) palette_seen++;
            if (ui_link_hit(x, y)) link_hits++;
        }
    }
    CHECK(hits >= 18); /* previous/next are the two scene hit targets */
    CHECK(shuffle_seen > 0);
    CHECK(palette_seen > 0);
    CHECK(link_hits > 10);

    ctx.hud_hidden = true;
    canvas_clear_overlay(&c);
    ui_draw(&c, pal, &ctx, names);
    int hidden_hits = 0, hidden_link_hits = 0;
    for (int y = 0; y < 36; y++) for (int x = 0; x < 120; x++) {
        int arg = 0;
        hidden_hits += ui_hit_test(x, y, &arg) != UI_NONE;
        hidden_link_hits += ui_link_hit(x, y);
    }
    CHECK(hidden_hits == 0 && hidden_link_hits == 0);
    ctx.hud_hidden = false;
    canvas_clear_overlay(&c);
    ui_draw(&c, pal, &ctx, names);
    CHECK(ui_menu_hit(5, 5) == -1);
    ui_menu_toggle(3);
    CHECK(ui_menu_open() && ui_menu_selected() == 3);
    ui_menu_move(-5, 22);
    CHECK(ui_menu_selected() == 20);
    ui_menu_move(5, 22);
    CHECK(ui_menu_selected() == 3);
    canvas_clear_overlay(&c);
    ui_draw(&c, pal, &ctx, names);
    int inside = 0;
    for (int y = 0; y < 36; y++)
        for (int x = 0; x < 120; x++) {
            int idx = ui_menu_hit(x, y);
            if (idx >= 0) {
                inside++;
                CHECK(idx < 22);
            }
        }
    CHECK(inside > 5);
    ui_menu_close();
    CHECK(!ui_menu_open());
    ui_toggle_help();
    CHECK(ui_help_open());
    ui_close_help();
    CHECK(!ui_help_open());
    canvas_free(&c);
}

static void test_canvas(void) {
    GROUP("canvas");
    canvas_t c;
    canvas_init(&c, 20, 8);
    canvas_put(&c, 2, 2, 'a', 0.5f, 0.1f);
    canvas_put(&c, 2, 2, 'b', 0.3f, 0.2f);
    CHECK(c.cells[2 * 20 + 2].glyph == 'a');
    canvas_put(&c, 2, 2, 'c', 0.9f, 0.3f);
    CHECK(c.cells[2 * 20 + 2].glyph == 'c');
    canvas_put(&c, -1, 0, 'x', 1.0f, 0.0f);
    canvas_put(&c, 0, 99, 'x', 1.0f, 0.0f);
    canvas_put(&c, 20, 0, 'x', 1.0f, 0.0f);
    canvas_glow_add(&c, -4, 2, 1.0f);
    canvas_glow_point(&c, -3.0f, -3.0f, 4.0f, 2.0f, 1.0f);
    canvas_glow_point(&c, NAN, 2.0f, 3.0f, 3.0f, 1.0f);
    canvas_glow_point(&c, 2.0f, 2.0f, INFINITY, 3.0f, 1.0f);
    canvas_glow_point(&c, 2.0f, 2.0f, 0.0f, 3.0f, 1.0f);
    canvas_glow_point(&c, 2.0f, 2.0f, 1e30f, 1e30f, 1.0f);
    bool glow_finite = true;
    for (int i = 0; i < 20 * 8; i++) if (!isfinite(c.extra[i])) glow_finite = false;
    CHECK(glow_finite);
    canvas_put_ramp(&c, 5, 5, " .:", 0.0f, 0.0f);
    CHECK(c.cells[5 * 20 + 5].glyph == ' ');
    canvas_put_ramp(&c, 5, 5, " .:", 1.0f, 0.0f);
    CHECK(c.cells[5 * 20 + 5].glyph == ':');
    canvas_clear(&c);
    CHECK(c.cells[2 * 20 + 2].val == 0.0f && c.cells[2 * 20 + 2].glyph == ' ');

    term_headless(20, 8);
    const palette_t *pal = palette_get(0);
    canvas_put(&c, 3, 3, '*', 1.0f, 0.5f);
    canvas_text(&c, 1, 1, "hi", pal->accent, pal->bg_top, 0.0f);
    canvas_render(&c, pal);
    size_t first = out_pending();
    out_flush();
    CHECK(first > 100);
    canvas_render(&c, pal);
    size_t second = out_pending();
    out_flush();
    CHECK(second < 16);
    canvas_invalidate(&c);
    canvas_render(&c, pal);
    CHECK(out_pending() > 100);
    out_flush();

    c.master = 0.0f;
    canvas_render(&c, pal);
    CHECK(out_pending() > 0);
    out_flush();

    c.master = 1.0f;
    canvas_overlay_rect(&c, 0, 0, 5, 2, pal->bg_top, 0.8f);
    CHECK(c.over[0].active && c.over[0].glyph == 0);
    canvas_clear_overlay(&c);
    CHECK(!c.over[0].active);

    canvas_resize(&c, 33, 11);
    CHECK(c.cols == 33 && c.rows == 11 && c.force);
    canvas_resize(&c, 0, 0);
    CHECK(c.cols == 1 && c.rows == 1);

    canvas_resize(&c, 12, 4);
    canvas_put(&c, 1, 1, '#', 1.0f, 0.5f);
    canvas_render(&c, pal);
    out_flush();
    const char *base = "/tmp/asmr_test_shot";
    CHECK(canvas_save_ansi(&c, base));
    struct stat st;
    CHECK(stat("/tmp/asmr_test_shot.txt", &st) == 0 && st.st_size >= 12 * 4);
    CHECK(stat("/tmp/asmr_test_shot.ans", &st) == 0 && st.st_size > st.st_size / 2 && st.st_size > 100);
    remove("/tmp/asmr_test_shot.txt");
    remove("/tmp/asmr_test_shot.ans");
    canvas_free(&c);
}

static void test_scenes(void) {
    GROUP("scenes");
    int n = scene_count();
    CHECK(n >= 22);
    CHECK(scene_find("rain") == 0 && scene_find("nope") == -1);
    for (int i = 0; i < n; i++) CHECK(scene_find(scene_def(i)->name) == i);
    CHECK(scene_def(n) == scene_def(0) && scene_def(-1) == scene_def(n - 1));

    static const int sizes[][2] = {{110, 32}, {80, 24}, {40, 12}, {20, 8}, {10, 5}, {200, 60}};
    for (int i = 0; i < n; i++) {
        const scene_def_t *def = scene_def(i);
        CHECK(def->name && def->blurb && def->create && def->update && def->draw && def->destroy);
        CHECK((int)def->bed >= 0 && def->bed < BED_COUNT);
        for (size_t k = 0; k < ARRAY_LEN(sizes); k++) {
            int cols = sizes[k][0], rows = sizes[k][1];
            scene_instance_t s;
            memset(&s, 0, sizeof s);
            canvas_t c;
            canvas_init(&c, cols, rows);
            scene_start(&s, i, cols, rows);
            double t = 0.0;
            bool finite = true;
            for (int f = 0; f < 90; f++) {
                t += 1.0 / 30.0;
                scene_update(&s, 1.0 / 30.0, t);
                if (f == 30) scene_poke(&s, cols / 2, rows / 2, 3);
                if (f == 31) scene_poke(&s, 0, 0, 1);
                if (f == 32) scene_poke(&s, cols - 1, rows - 1, 1);
                canvas_clear(&c);
                scene_draw(&s, &c, t);
                for (size_t q = 0; q < (size_t)cols * (size_t)rows; q++) {
                    if (!isfinite(c.cells[q].val) || !isfinite(c.extra[q]) || !isfinite(c.cells[q].tint)) finite = false;
                }
                float e = scene_energy(&s);
                if (!isfinite(e) || e < 0.0f || e > 1.5f) finite = false;
            }
            if (!finite) printf("  non-finite output in %s at %dx%d\n", def->name, cols, rows);
            CHECK(finite);
            scene_resize(&s, cols / 2 + 3, rows / 2 + 2);
            canvas_resize(&c, cols / 2 + 3, rows / 2 + 2);
            for (int f = 0; f < 20; f++) {
                t += 1.0 / 30.0;
                scene_update(&s, 1.0 / 30.0, t);
                canvas_clear(&c);
                scene_draw(&s, &c, t);
            }
            scene_stop(&s);
            CHECK(s.state == NULL && s.def == NULL);
            canvas_free(&c);
        }
    }
}

static void test_scene_determinism(void) {
    GROUP("determinism");
    uint32_t sums[2];
    for (int run = 0; run < 2; run++) {
        rng_seed(rng_global(), 2024);
        noise_init(2024);
        scene_instance_t s;
        memset(&s, 0, sizeof s);
        canvas_t c;
        canvas_init(&c, 60, 20);
        scene_start(&s, scene_find("ripples"), 60, 20);
        uint32_t sum = 0;
        double t = 0.0;
        for (int f = 0; f < 120; f++) {
            t += 1.0 / 30.0;
            scene_update(&s, 1.0 / 30.0, t);
            canvas_clear(&c);
            scene_draw(&s, &c, t);
            for (int q = 0; q < 60 * 20; q++) sum = sum * 31u + (uint32_t)(c.cells[q].val * 1000.0f) + c.cells[q].glyph;
        }
        sums[run] = sum;
        scene_stop(&s);
        canvas_free(&c);
    }
    CHECK(sums[0] == sums[1]);
    CHECK(sums[0] != 0);
}

static void test_audio(void) {
    GROUP("audio");
    CHECK(!audio_init(false, NULL));
    CHECK(!audio_available());
    audio_trigger(AUDIO_CHIME, 0.5f, 0.0f, 1.0f);
    audio_set_bed(BED_RAIN);
    audio_shutdown();

    /* A missing player must fail cleanly and restore the caller's SIGPIPE policy. */
    const char *saved_path = getenv("PATH");
    char *path_copy = saved_path ? strdup(saved_path) : NULL;
    struct sigaction before_pipe, after_pipe;
    CHECK(sigaction(SIGPIPE, NULL, &before_pipe) == 0);
    setenv("PATH", "/tmp/asmr-no-audio-player-directory", 1);
    CHECK(!audio_init(true, NULL));
    CHECK(sigaction(SIGPIPE, NULL, &after_pipe) == 0);
    CHECK(before_pipe.sa_handler == after_pipe.sa_handler);
    if (path_copy) { setenv("PATH", path_copy, 1); free(path_copy); }

    const char *path = "/tmp/asmr_test_audio.raw";
    CHECK(audio_init(true, path));
    CHECK(audio_available() && strcmp(audio_backend(), "file") == 0);
    audio_set_volume(2.0f);
    CHECK(audio_volume() == 1.0f);
    audio_set_volume(0.8f);
    audio_set_binaural(true);
    CHECK(audio_binaural());
    audio_trigger((audio_kind_t)-1, NAN, INFINITY, NAN); /* Invalid events must be dropped safely. */
    audio_trigger(AUDIO_TICK, NAN, INFINITY, NAN);         /* Non-finite controls must not poison the synth. */
    for (int k = 0; k < AUDIO_KIND_COUNT; k++) audio_trigger((audio_kind_t)k, 0.4f, -0.5f + 0.25f * (float)k, 0.8f);
    for (int b = 0; b < BED_COUNT; b++) {
        audio_set_bed((audio_bed_t)b);
        audio_set_energy((float)b / (float)BED_COUNT);
        sleep_seconds(0.05);
    }
    audio_set_enabled(false);
    CHECK(!audio_enabled());
    audio_trigger(AUDIO_TICK, 0.5f, 0.0f, 1.0f);
    audio_set_enabled(true);
    audio_set_fade(0.5f);
    sleep_seconds(0.2);
    audio_shutdown();

    FILE *f = fopen(path, "rb");
    CHECK(f != NULL);
    if (f) {
        fseek(f, 0, SEEK_END);
        long size = ftell(f);
        fseek(f, 0, SEEK_SET);
        CHECK(size > 44100 * 4 / 4);
        int16_t *data = malloc((size_t)size);
        size_t got = fread(data, 1, (size_t)size, f) / 2;
        fclose(f);
        int peak = 0;
        long nonzero = 0;
        for (size_t i = 0; i < got; i++) {
            int a = abs(data[i]);
            if (a > peak) peak = a;
            if (data[i]) nonzero++;
        }
        CHECK(peak > 500);
        CHECK(peak <= 30000);
        CHECK(nonzero > (long)got / 4);
        free(data);
    }
    remove(path);
}

static void test_offline_raw(void) {
    GROUP("offline raw");
    const char *path = "/tmp/asmr_test_offline.raw";
    const int frames = 4097;
    CHECK(audio_offline_raw_begin(path));
    CHECK(audio_available() && strcmp(audio_backend(), "raw") == 0);
    audio_set_bed(BED_RAIN);
    CHECK(audio_offline_step(frames));
    CHECK(!audio_offline_step(-1));
    audio_shutdown();
    FILE *f = fopen(path, "rb");
    CHECK(f != NULL);
    if (f) {
        CHECK(fseek(f, 0, SEEK_END) == 0);
        long size = ftell(f);
        CHECK(size == (long)frames * 4L);
        fclose(f);
    }
    remove(path);
}

static unsigned le32_at(const unsigned char *p) {
    return (unsigned)p[0] | ((unsigned)p[1] << 8) | ((unsigned)p[2] << 16) | ((unsigned)p[3] << 24);
}

static void test_offline_wav(void) {
    GROUP("offline wav");
    const char *path = "/tmp/asmr_test_offline.wav";
    CHECK(audio_offline_begin(path));
    CHECK(audio_available() && strcmp(audio_backend(), "wav") == 0);
    audio_set_bed(BED_STORM);
    audio_trigger(AUDIO_THUNDER, 0.5f, 0.0f, 1.0f);
    audio_trigger(AUDIO_CRACKLE, 0.5f, 0.0f, 1.0f);
    const int total = 44100 * 2 + 777;
    int done = 0;
    while (done < total) {
        int step = 1470;
        if (done + step > total) step = total - done;
        CHECK(audio_offline_step(step));
        done += step;
    }
    CHECK(audio_peak() >= 0.0f && audio_peak() <= 1.0f);
    audio_shutdown();
    CHECK(!audio_available());
    CHECK(!audio_offline_step(100));

    FILE *f = fopen(path, "rb");
    CHECK(f != NULL);
    if (f) {
        fseek(f, 0, SEEK_END);
        long size = ftell(f);
        fseek(f, 0, SEEK_SET);
        unsigned char *d = malloc((size_t)size);
        CHECK(fread(d, 1, (size_t)size, f) == (size_t)size);
        fclose(f);
        CHECK(memcmp(d, "RIFF", 4) == 0 && memcmp(d + 8, "WAVE", 4) == 0 && memcmp(d + 12, "fmt ", 4) == 0);
        CHECK(memcmp(d + 36, "data", 4) == 0);
        CHECK(d[20] == 1 && d[22] == 2 && le32_at(d + 24) == 44100 && le32_at(d + 28) == 44100u * 4u);
        CHECK(le32_at(d + 40) == (unsigned)total * 4u);
        CHECK(le32_at(d + 4) == 36u + (unsigned)total * 4u);
        CHECK(size == 44 + (long)total * 4);
        int peak = 0;
        long nonzero = 0;
        const int16_t *s = (const int16_t *)(d + 44);
        for (long i = 0; i < (long)total * 2; i++) {
            int a = abs(s[i]);
            if (a > peak) peak = a;
            if (s[i]) nonzero++;
        }
        CHECK(peak > 500 && peak <= 30000);
        CHECK(nonzero > (long)total / 2);
        free(d);
    }
    remove(path);

    const char *path2 = "/tmp/asmr_test_live.wav";
    CHECK(audio_init(true, path2));
    audio_trigger(AUDIO_BELL, 0.3f, 0.0f, 0.8f);
    sleep_seconds(0.3);
    audio_shutdown();
    f = fopen(path2, "rb");
    CHECK(f != NULL);
    if (f) {
        unsigned char h[44];
        CHECK(fread(h, 1, 44, f) == 44);
        fseek(f, 0, SEEK_END);
        long size = ftell(f);
        fclose(f);
        CHECK(memcmp(h, "RIFF", 4) == 0 && le32_at(h + 40) == (unsigned)(size - 44));
    }
    remove(path2);
}

static void test_journey_breath_config(void) {
    GROUP("journey/breath/config");
    CHECK(!journey_active());
    int s = -1, p = -1;
    CHECK(!journey_update(1.0, &s, &p));
    journey_start();
    CHECK(journey_active());
    CHECK(journey_update(0.1, &s, &p));
    CHECK(s >= 0 && s < scene_count() && p >= 0 && p < palette_count());
    CHECK(!journey_update(0.5, &s, &p));
    int steps = 0;
    for (int i = 0; i < 100000 && steps < journey_steps() * 2; i++)
        if (journey_update(1.0, &s, &p)) steps++;
    CHECK(steps == journey_steps() * 2);
    CHECK(journey_step() >= 0 && journey_step() < journey_steps());
    journey_stop();
    CHECK(!journey_active());

    CHECK(!breath_active());
    breath_toggle();
    CHECK(breath_active());
    for (int i = 0; i < 700; i++) breath_update(1.0 / 30.0);
    canvas_t c;
    canvas_init(&c, 80, 24);
    breath_draw(&c, palette_get(0), 80, 24);
    int active = 0;
    for (int i = 0; i < 80 * 24; i++) active += c.over[i].active;
    CHECK(active > 20);
    canvas_free(&c);
    breath_toggle();
    CHECK(!breath_active());

    setenv("XDG_CONFIG_HOME", "/tmp/asmr_test_cfg", 1);
    config_t a, b;
    config_defaults(&a);
    CHECK(a.volume == 0.8f && a.speed == 1.0f && a.autoshuffle && a.sound);
    a.volume = 0.35f;
    a.speed = 1.75f;
    a.autoshuffle = false;
    a.sound = false;
    config_save(&a);
    config_load(&b);
    CHECK(fabsf(b.volume - 0.35f) < 0.01f && fabsf(b.speed - 1.75f) < 0.01f && !b.autoshuffle && !b.sound);

    FILE *bad = fopen("/tmp/asmr_test_cfg/asmr/config", "w");
    CHECK(bad != NULL);
    if (bad) {
        fputs("# malformed and out-of-range config values\n"
              "volume=nan\nspeed=not-a-number\nautoshuffle=maybe\nsound=off\n"
              " volume = 2.5 \n speed = -100 \n autoshuffle = true \nvolume=inf\n", bad);
        fclose(bad);
    }
    config_load(&b);
    CHECK(b.volume == 1.0f && b.speed == 0.25f && b.autoshuffle && !b.sound);
    b.volume = NAN;
    b.speed = INFINITY;
    config_save(&b);
    config_load(&b);
    CHECK(fabsf(b.volume - 0.8f) < 0.01f && fabsf(b.speed - 1.0f) < 0.01f);

    remove("/tmp/asmr_test_cfg/asmr/config");
    rmdir("/tmp/asmr_test_cfg/asmr");
    rmdir("/tmp/asmr_test_cfg");
    config_load(&b);
    CHECK(b.volume == 0.8f && b.autoshuffle);
}

int main(void) {
    rng_seed(rng_global(), 1);
    noise_init(1);
    term_headless(80, 24);
    test_common();
    test_rng();
    test_noise();
    test_palette();
    test_color();
    test_canvas();
    test_ui();
    test_scenes();
    test_scene_determinism();
    test_audio();
    test_offline_raw();
    test_offline_wav();
    test_journey_breath_config();
    printf("\n%d checks, %d failed\n", g_checks, g_failed);
    return g_failed ? 1 : 0;
}
