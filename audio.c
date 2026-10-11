#include "audio.h"


#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define SR 44100
#define BLOCK 512
#define MAX_VOICES 40
#define QUEUE_LEN 128

/* ---------- filters and noise ---------- */

typedef struct {
    float b0, b1, b2, a1, a2, z1, z2;
} biquad_t;

enum { F_LP, F_HP, F_BP };

static void biquad_set(biquad_t *f, int type, float fc, float q) {
    fc = clampf(fc, 20.0f, SR * 0.45f);
    q = clampf(q, 0.2f, 20.0f);
    float w = TAU * fc / SR, cw = cosf(w), sw = sinf(w);
    float alpha = sw / (2.0f * q);
    float a0 = 1.0f + alpha, a1 = -2.0f * cw, a2 = 1.0f - alpha;
    float b0, b1, b2;
    switch (type) {
    case F_LP:
        b0 = (1.0f - cw) * 0.5f;
        b1 = 1.0f - cw;
        b2 = b0;
        break;
    case F_HP:
        b0 = (1.0f + cw) * 0.5f;
        b1 = -(1.0f + cw);
        b2 = b0;
        break;
    default:
        b0 = alpha;
        b1 = 0.0f;
        b2 = -alpha;
        break;
    }
    f->b0 = b0 / a0;
    f->b1 = b1 / a0;
    f->b2 = b2 / a0;
    f->a1 = a1 / a0;
    f->a2 = a2 / a0;
}

static inline float biquad_run(biquad_t *f, float x) {
    float y = f->b0 * x + f->z1;
    f->z1 = f->b1 * x - f->a1 * y + f->z2;
    f->z2 = f->b2 * x - f->a2 * y;
    return y;
}

typedef struct {
    uint32_t s;
    float p0, p1, p2, p3, p4, p5, p6;
    float brown;
} noise_t;

static inline float white(noise_t *n) {
    n->s ^= n->s << 13;
    n->s ^= n->s >> 17;
    n->s ^= n->s << 5;
    return (float)(int32_t)n->s * (1.0f / 2147483648.0f);
}

static float pink(noise_t *n) {
    float w = white(n);
    n->p0 = 0.99886f * n->p0 + w * 0.0555179f;
    n->p1 = 0.99332f * n->p1 + w * 0.0750759f;
    n->p2 = 0.96900f * n->p2 + w * 0.1538520f;
    n->p3 = 0.86650f * n->p3 + w * 0.3104856f;
    n->p4 = 0.55000f * n->p4 + w * 0.5329522f;
    n->p5 = -0.7616f * n->p5 - w * 0.0168980f;
    float out = n->p0 + n->p1 + n->p2 + n->p3 + n->p4 + n->p5 + n->p6 + w * 0.5362f;
    n->p6 = w * 0.115926f;
    return out * 0.11f;
}

static float brown(noise_t *n) {
    float w = white(n);
    n->brown = (n->brown + 0.02f * w) / 1.02f;
    return n->brown * 3.5f;
}

/* ---------- reverb ---------- */

typedef struct {
    float *buf;
    int len, pos;
    float fb, damp, store;
} comb_t;

typedef struct {
    float *buf;
    int len, pos;
} allp_t;

static float comb_run(comb_t *c, float in) {
    float out = c->buf[c->pos];
    c->store = out * (1.0f - c->damp) + c->store * c->damp;
    c->buf[c->pos] = in + c->store * c->fb;
    if (++c->pos >= c->len) c->pos = 0;
    return out;
}

static float allp_run(allp_t *a, float in) {
    float b = a->buf[a->pos];
    float out = -in + b;
    a->buf[a->pos] = in + b * 0.5f;
    if (++a->pos >= a->len) a->pos = 0;
    return out;
}

/* ---------- beds ---------- */

typedef struct {
    int noise;
    int ftype;
    float freq, q, gain;
    float lfo_rate, lfo_depth;
    float drone_gain, drone_root;
} bed_t;

static const bed_t BEDS[BED_COUNT] = {
    /* rain    */ {1, F_BP, 3800.0f, 0.5f, 0.11f, 0.07f, 0.15f, 0.000f, 110.0f},
    /* tide    */ {2, F_LP, 700.0f, 0.6f, 0.30f, 0.09f, 0.55f, 0.000f, 110.0f},
    /* stars   */ {1, F_LP, 220.0f, 0.5f, 0.03f, 0.02f, 0.20f, 0.030f, 110.0f},
    /* ripples */ {2, F_LP, 320.0f, 0.6f, 0.05f, 0.03f, 0.30f, 0.018f, 98.0f},
    /* drift   */ {1, F_BP, 900.0f, 1.1f, 0.05f, 0.05f, 0.50f, 0.028f, 87.3f},
    /* firefly */ {1, F_LP, 500.0f, 0.5f, 0.04f, 0.04f, 0.30f, 0.020f, 130.8f},
    /* snow    */ {1, F_BP, 1400.0f, 0.4f, 0.045f, 0.03f, 0.45f, 0.016f, 146.8f},
    /* aurora  */ {1, F_LP, 260.0f, 0.5f, 0.02f, 0.015f, 0.20f, 0.040f, 73.4f},
    /* embers  */ {1, F_LP, 1800.0f, 0.6f, 0.07f, 0.50f, 0.55f, 0.012f, 82.4f},
    /* storm   */ {1, F_BP, 3200.0f, 0.45f, 0.16f, 0.05f, 0.25f, 0.012f, 55.0f},
    /* sakura  */ {1, F_BP, 2400.0f, 0.5f, 0.025f, 0.04f, 0.40f, 0.022f, 164.8f},
    /* deepsea */ {2, F_LP, 160.0f, 0.6f, 0.28f, 0.04f, 0.35f, 0.035f, 55.0f},
    /* lantern */ {1, F_LP, 300.0f, 0.5f, 0.03f, 0.03f, 0.30f, 0.030f, 98.0f},
    /* lava    */ {2, F_LP, 220.0f, 0.7f, 0.12f, 0.06f, 0.50f, 0.030f, 65.4f},
    /* window  */ {1, F_BP, 2600.0f, 0.5f, 0.07f, 0.05f, 0.30f, 0.012f, 110.0f},
    /* cave    */ {2, F_LP, 260.0f, 0.8f, 0.10f, 0.03f, 0.40f, 0.040f, 61.7f},
    /* chimes  */ {1, F_BP, 1800.0f, 0.4f, 0.03f, 0.06f, 0.50f, 0.020f, 196.0f},
    /* zen     */ {1, F_LP, 700.0f, 0.5f, 0.04f, 0.03f, 0.30f, 0.028f, 110.0f},
    /* pendulum*/ {1, F_LP, 400.0f, 0.5f, 0.02f, 0.02f, 0.20f, 0.030f, 130.8f},
    /* galaxy  */ {2, F_LP, 140.0f, 0.7f, 0.10f, 0.02f, 0.30f, 0.050f, 55.0f},
    /* bonsai  */ {1, F_BP, 2200.0f, 0.4f, 0.03f, 0.05f, 0.40f, 0.020f, 146.8f},
    /* digital */ {1, F_BP, 1200.0f, 0.8f, 0.04f, 0.04f, 0.30f, 0.025f, 110.0f},
};

static const float DRONE_RATIOS[4] = {1.0f, 1.5f, 2.0f, 3.006f};

/* ---------- voices ---------- */

typedef struct {
    audio_kind_t kind;
    float pitch, pan, vel;
} event_t;

typedef struct {
    bool active;
    audio_kind_t kind;
    float t, dur, freq, vel, gl, gr;
    float ph[4];
    int cnt;
    biquad_t bq;
    noise_t nz;
} voice_t;

static const float CH_RATIO[4] = {1.0f, 2.76f, 5.40f, 8.93f};
static const float CH_AMP[4] = {1.0f, 0.40f, 0.18f, 0.07f};
static const float CH_DEC[4] = {1.3f, 2.4f, 3.8f, 5.5f};
static const float BL_RATIO[4] = {1.0f, 2.01f, 2.99f, 4.20f};
static const float BL_AMP[4] = {1.0f, 0.50f, 0.30f, 0.12f};
static const float BL_DEC[4] = {0.7f, 1.2f, 1.9f, 3.0f};

static struct {
    atomic_bool avail, running;
    bool enabled, dump_mode, wav, offline;
    bool mu_init, thread_started, reverb_ready;
    char backend[48];
    uint32_t data_bytes;
    float peak;
    FILE *pipe;
    int fd;
    uint32_t voice_seed;
    pthread_t th;
    pthread_mutex_t mu;
    event_t q[QUEUE_LEN];
    int qh, qt;
    voice_t v[MAX_VOICES];

    int bed;
    float energy_t, energy;
    float gain_cur, freq_cur, q_cur, lfo_rate_cur, lfo_depth_cur, drone_gain_cur, drone_root_cur;
    noise_t bed_noise;
    biquad_t bed_f;
    float lfo_ph;
    float drone_ph[4], drone_ph_r[4], drone_lfo[4];
    bool binaural;

    float master_cur, volume, fade;
    comb_t combL[4], combR[4];
    allp_t apL[2], apR[2];
} A;

/* Do not leak a process-wide SIGPIPE change into embedding/test applications. */
static struct sigaction g_old_sigpipe;
static bool g_sigpipe_saved = false;

static float pitch_freq(float p) {
    static const int deg[5] = {0, 2, 4, 7, 9};
    int idx = (int)(clampf(p, 0.0f, 0.999f) * 15.0f);
    int oct = idx / 5;
    return 220.0f * powf(2.0f, (12.0f * (float)oct + (float)deg[idx % 5]) / 12.0f);
}

static void spawn_voice(const event_t *e) {
    int slot = -1;
    float oldest = -1.0f;
    for (int i = 0; i < MAX_VOICES; i++) {
        if (!A.v[i].active) {
            slot = i;
            break;
        }
        if (A.v[i].t > oldest) {
            oldest = A.v[i].t;
            slot = i;
        }
    }
    voice_t *v = &A.v[slot];
    memset(v, 0, sizeof *v);
    v->active = true;
    v->kind = e->kind;
    v->vel = clampf(e->vel, 0.0f, 1.0f);
    float pan = clampf(e->pan, -1.0f, 1.0f);
    float a = (pan + 1.0f) * PI_F * 0.25f;
    v->gl = cosf(a);
    v->gr = sinf(a);
    /* Keep audio noise independent from the renderer's global RNG; the audio
     * thread and scene thread otherwise race on the same generator state. */
    A.voice_seed ^= A.voice_seed << 13;
    A.voice_seed ^= A.voice_seed >> 17;
    A.voice_seed ^= A.voice_seed << 5;
    v->nz.s = A.voice_seed | 1u;

    switch (e->kind) {
    case AUDIO_TICK:
        v->freq = 2500.0f + e->pitch * 3500.0f;
        v->dur = 0.08f;
        biquad_set(&v->bq, F_BP, v->freq, 2.0f);
        break;
    case AUDIO_PLINK:
        v->freq = 380.0f + e->pitch * 700.0f;
        v->dur = 0.7f;
        break;
    case AUDIO_CHIME:
        v->freq = pitch_freq(e->pitch) * 2.0f;
        v->dur = 4.0f;
        break;
    case AUDIO_BELL:
        v->freq = pitch_freq(e->pitch) * 0.5f;
        v->dur = 6.0f;
        break;
    case AUDIO_WHOOSH:
        v->freq = 1400.0f + e->pitch * 2800.0f;
        v->dur = 1.3f;
        biquad_set(&v->bq, F_BP, v->freq, 2.5f);
        break;
    case AUDIO_THUNDER:
        v->freq = 0.0f;
        v->dur = 7.0f;
        biquad_set(&v->bq, F_LP, 160.0f, 0.7f);
        break;
    case AUDIO_BUBBLE:
        v->freq = 300.0f + e->pitch * 900.0f;
        v->dur = 0.2f;
        break;
    case AUDIO_CRACKLE:
        v->freq = 800.0f + e->pitch * 2800.0f;
        v->dur = 0.07f;
        biquad_set(&v->bq, F_BP, v->freq, 0.9f);
        break;
    case AUDIO_DRIP:
        v->freq = 900.0f + e->pitch * 900.0f;
        v->dur = 0.25f;
        break;
    case AUDIO_SCRAPE:
        v->freq = 1200.0f + e->pitch * 2000.0f;
        v->dur = 0.16f;
        biquad_set(&v->bq, F_BP, v->freq, 1.2f);
        break;
    case AUDIO_SWELL:
    default:
        v->freq = 180.0f;
        v->dur = 5.5f;
        biquad_set(&v->bq, F_LP, v->freq, 0.8f);
        break;
    }
}

static float voice_sample(voice_t *v) {
    const float dt = 1.0f / SR;
    float t = v->t, s = 0.0f;
    switch (v->kind) {
    case AUDIO_TICK:
        s = biquad_run(&v->bq, white(&v->nz)) * expf(-t * 140.0f) * v->vel * 1.6f;
        break;
    case AUDIO_PLINK: {
        float f = v->freq * expf(-t * 0.9f);
        v->ph[0] += TAU * f * dt;
        v->ph[1] += TAU * f * 2.0f * dt;
        s = (sinf(v->ph[0]) * expf(-t * 11.0f) * 0.35f + sinf(v->ph[1]) * expf(-t * 20.0f) * 0.10f) * v->vel;
        break;
    }
    case AUDIO_CHIME:
    case AUDIO_BELL: {
        const float *ratio = v->kind == AUDIO_CHIME ? CH_RATIO : BL_RATIO;
        const float *amp = v->kind == AUDIO_CHIME ? CH_AMP : BL_AMP;
        const float *dec = v->kind == AUDIO_CHIME ? CH_DEC : BL_DEC;
        float sum = 0.0f;
        for (int i = 0; i < 4; i++) {
            v->ph[i] += TAU * v->freq * ratio[i] * dt;
            if (v->ph[i] > TAU) v->ph[i] -= TAU;
            sum += sinf(v->ph[i]) * amp[i] * expf(-t * dec[i]);
        }
        s = sum * v->vel * 0.18f * fminf(1.0f, t / 0.004f);
        break;
    }
    case AUDIO_WHOOSH: {
        if ((v->cnt++ & 31) == 0) biquad_set(&v->bq, F_BP, v->freq * (1.0f - 0.75f * t / v->dur), 2.5f);
        float env = sinf(PI_F * t / v->dur);
        s = biquad_run(&v->bq, white(&v->nz)) * env * env * v->vel * 0.9f;
        break;
    }
    case AUDIO_THUNDER: {
        float env = (1.0f - expf(-t * 6.0f)) * expf(-t * 0.75f);
        float rum = 0.6f + 0.4f * sinf(t * 23.0f + sinf(t * 7.0f) * 2.0f);
        if ((v->cnt++ & 31) == 0) biquad_set(&v->bq, F_LP, 60.0f + 160.0f * expf(-t * 0.35f), 0.7f);
        s = biquad_run(&v->bq, brown(&v->nz)) * env * rum * v->vel * 3.0f;
        break;
    }
    case AUDIO_BUBBLE: {
        float f = v->freq * (1.0f + 6.0f * t);
        v->ph[0] += TAU * f * dt;
        s = sinf(v->ph[0]) * expf(-t * 28.0f) * v->vel * 0.4f * fminf(1.0f, t / 0.002f);
        break;
    }
    case AUDIO_CRACKLE:
        s = biquad_run(&v->bq, white(&v->nz)) * expf(-t * 200.0f) * v->vel * 2.0f;
        break;
    case AUDIO_DRIP: {
        float f = v->freq * (1.0f - 0.25f * fminf(1.0f, t / 0.1f));
        v->ph[0] += TAU * f * dt;
        v->ph[1] += TAU * f * 2.1f * dt;
        s = (sinf(v->ph[0]) * expf(-t * 22.0f) * 0.35f + sinf(v->ph[1]) * expf(-t * 40.0f) * 0.10f) * v->vel * fminf(1.0f, t / 0.002f);
        break;
    }
    case AUDIO_SCRAPE: {
        float env = sinf(PI_F * t / v->dur);
        s = biquad_run(&v->bq, white(&v->nz)) * env * env * v->vel * 1.2f;
        break;
    }
    case AUDIO_SWELL:
    default: {
        float env = sinf(PI_F * t / v->dur);
        if ((v->cnt++ & 31) == 0) biquad_set(&v->bq, F_LP, 180.0f + 700.0f * env * env, 0.8f);
        s = biquad_run(&v->bq, white(&v->nz)) * powf(fmaxf(env, 0.0f), 2.5f) * v->vel * 1.4f;
        break;
    }
    }
    v->t += dt;
    if (v->t >= v->dur) v->active = false;
    return s;
}

/* ---------- block render ---------- */

static void render_block(int16_t *out, int frames) {
    pthread_mutex_lock(&A.mu);
    while (A.qh != A.qt) {
        spawn_voice(&A.q[A.qt]);
        A.qt = (A.qt + 1) % QUEUE_LEN;
    }
    /* Snapshot control values once per block. The renderer then works without
     * holding the UI/control mutex for its relatively expensive sample loop. */
    int bed_index = A.bed;
    float energy_target = A.energy_t;
    float volume = A.volume, fade = A.fade;
    bool enabled = A.enabled, binaural = A.binaural;
    pthread_mutex_unlock(&A.mu);

    const bed_t *b = &BEDS[bed_index];
    float k = 1.0f - expf(-(float)BLOCK / (SR * 0.8f));
    A.gain_cur += (b->gain - A.gain_cur) * k;
    A.freq_cur += (b->freq - A.freq_cur) * k;
    A.q_cur += (b->q - A.q_cur) * k;
    A.lfo_rate_cur += (b->lfo_rate - A.lfo_rate_cur) * k;
    A.lfo_depth_cur += (b->lfo_depth - A.lfo_depth_cur) * k;
    A.drone_gain_cur += (b->drone_gain - A.drone_gain_cur) * k;
    A.drone_root_cur += (b->drone_root - A.drone_root_cur) * k;
    A.energy += (energy_target - A.energy) * k;
    biquad_set(&A.bed_f, b->ftype, A.freq_cur, A.q_cur);

    float master_target = enabled ? volume * fade : 0.0f;
    float block_peak = 0.0f;

    for (int i = 0; i < frames; i++) {
        A.master_cur += (master_target - A.master_cur) * 0.0008f;

        float n = b->noise == 2 ? brown(&A.bed_noise) : (b->noise == 1 ? pink(&A.bed_noise) : white(&A.bed_noise));
        float s = biquad_run(&A.bed_f, n);
        A.lfo_ph += TAU * A.lfo_rate_cur / SR;
        if (A.lfo_ph > TAU) A.lfo_ph -= TAU;
        float lfo = 0.5f + 0.5f * sinf(A.lfo_ph);
        float amp = A.gain_cur * (1.0f - A.lfo_depth_cur + A.lfo_depth_cur * lfo * 2.0f);
        float bed = s * amp * (0.7f + 0.6f * A.energy);

        float d = 0.0f, dr = 0.0f;
        for (int j = 0; j < 4; j++) {
            A.drone_ph[j] += TAU * A.drone_root_cur * DRONE_RATIOS[j] / SR;
            if (A.drone_ph[j] > TAU) A.drone_ph[j] -= TAU;
            A.drone_lfo[j] += TAU * (0.03f + 0.017f * (float)j) / SR;
            if (A.drone_lfo[j] > TAU) A.drone_lfo[j] -= TAU;
            float damp = (0.5f + 0.5f * sinf(A.drone_lfo[j])) * 0.25f;
            d += sinf(A.drone_ph[j]) * damp;
            A.drone_ph_r[j] += TAU * (A.drone_root_cur * DRONE_RATIOS[j] + (binaural ? 8.0f : 0.0f)) / SR;
            if (A.drone_ph_r[j] > TAU) A.drone_ph_r[j] -= TAU;
            dr += sinf(A.drone_ph_r[j]) * damp;
        }
        float dscale = A.drone_gain_cur * (0.6f + 0.8f * A.energy) * (binaural ? 1.6f : 1.0f);
        d *= dscale;
        dr *= dscale;

        float vl = 0.0f, vr = 0.0f;
        for (int j = 0; j < MAX_VOICES; j++) {
            voice_t *v = &A.v[j];
            if (!v->active) continue;
            float vs = voice_sample(v);
            vl += vs * v->gl;
            vr += vs * v->gr;
        }

        float dry_l = bed + d + vl, dry_r = bed * 0.97f + (binaural ? dr : d) + vr;
        float send = (dry_l + dry_r) * 0.5f;
        float wl = 0.0f, wr = 0.0f;
        for (int j = 0; j < 4; j++) {
            wl += comb_run(&A.combL[j], send);
            wr += comb_run(&A.combR[j], send);
        }
        wl *= 0.25f;
        wr *= 0.25f;
        for (int j = 0; j < 2; j++) {
            wl = allp_run(&A.apL[j], wl);
            wr = allp_run(&A.apR[j], wr);
        }

        float ol = tanhf((dry_l + wl * 0.8f) * 1.1f) * A.master_cur;
        float orr = tanhf((dry_r + wr * 0.8f) * 1.1f) * A.master_cur;
        float pk = fmaxf(fabsf(ol), fabsf(orr));
        if (pk > block_peak) block_peak = pk;
        out[i * 2] = (int16_t)(clampf(ol, -1.0f, 1.0f) * 30000.0f);
        out[i * 2 + 1] = (int16_t)(clampf(orr, -1.0f, 1.0f) * 30000.0f);
    }
    pthread_mutex_lock(&A.mu);
    A.peak = fmaxf(A.peak * 0.93f, block_peak);
    pthread_mutex_unlock(&A.mu);
}

static void *audio_thread(void *arg) {
    (void)arg;
    int16_t buf[BLOCK * 2];
    while (A.running) {
        render_block(buf, BLOCK);
        size_t bytes = sizeof buf;
        const char *p = (const char *)buf;
        if (A.dump_mode) {
            size_t written = fwrite(p, 1, bytes, A.pipe);
            A.data_bytes += (uint32_t)written;
            if (written != bytes) {
                A.avail = false;
                A.running = false;
                return NULL;
            }
            sleep_seconds((double)BLOCK / SR);
            continue;
        }
        while (bytes > 0 && A.running) {
            ssize_t w = write(A.fd, p, bytes);
            if (w < 0) {
                if (errno == EINTR) continue;
                A.avail = false;
                A.running = false;
                return NULL;
            }
            if (w == 0) {
                A.avail = false;
                A.running = false;
                return NULL;
            }
            p += w;
            bytes -= (size_t)w;
        }
    }
    return NULL;
}

/* ---------- player discovery ---------- */

static bool find_in_path(const char *name) {
    const char *path = getenv("PATH");
    if (!path) return false;
    char buf[512];
    while (*path) {
        const char *e = strchr(path, ':');
        size_t n = e ? (size_t)(e - path) : strlen(path);
        if (n > 0 && n + strlen(name) + 2 < sizeof buf) {
            memcpy(buf, path, n);
            buf[n] = '/';
            strcpy(buf + n + 1, name);
            if (access(buf, X_OK) == 0) return true;
        }
        if (!e) break;
        path = e + 1;
    }
    return false;
}

typedef struct {
    const char *bin;
    const char *cmd;
} player_t;

static const player_t PLAYERS[] = {
    {"paplay", "paplay --raw --rate=44100 --format=s16le --channels=2 --latency-msec=60 >/dev/null 2>&1"},
    {"aplay", "aplay -q -t raw -f S16_LE -c 2 -r 44100 -B 60000 >/dev/null 2>&1"},
    {"pw-cat", "pw-cat -p --raw --rate 44100 --channels 2 --format s16 --latency 60ms - >/dev/null 2>&1"},
    {"play", "play -q -t raw -r 44100 -e signed -b 16 -c 2 - >/dev/null 2>&1"},
    {"ffplay", "ffplay -nodisp -loglevel quiet -f s16le -ar 44100 -ac 2 -i - >/dev/null 2>&1"},
};

static void free_reverb(void) {
    for (int i = 0; i < 4; i++) {
        free(A.combL[i].buf);
        free(A.combR[i].buf);
        A.combL[i].buf = A.combR[i].buf = NULL;
    }
    for (int i = 0; i < 2; i++) {
        free(A.apL[i].buf);
        free(A.apR[i].buf);
        A.apL[i].buf = A.apR[i].buf = NULL;
    }
}

static void init_reverb(void) {
    static const int cl[4] = {1557, 1617, 1491, 1422};
    static const int al[2] = {225, 556};
    for (int i = 0; i < 4; i++) {
        A.combL[i].len = cl[i];
        A.combR[i].len = cl[i] + 23;
        A.combL[i].fb = A.combR[i].fb = 0.80f;
        A.combL[i].damp = A.combR[i].damp = 0.35f;
        A.combL[i].buf = xcalloc((size_t)A.combL[i].len, sizeof(float));
        A.combR[i].buf = xcalloc((size_t)A.combR[i].len, sizeof(float));
    }
    for (int i = 0; i < 2; i++) {
        A.apL[i].len = al[i];
        A.apR[i].len = al[i] + 23;
        A.apL[i].buf = xcalloc((size_t)A.apL[i].len, sizeof(float));
        A.apR[i].buf = xcalloc((size_t)A.apR[i].len, sizeof(float));
    }
}

static void le16(unsigned char *p, unsigned v) {
    p[0] = (unsigned char)(v & 0xFF);
    p[1] = (unsigned char)((v >> 8) & 0xFF);
}

static void le32(unsigned char *p, uint32_t v) {
    le16(p, v & 0xFFFF);
    le16(p + 2, v >> 16);
}

static void wav_header(FILE *f, uint32_t data_bytes) {
    unsigned char h[44];
    memcpy(h, "RIFF", 4);
    le32(h + 4, 36 + data_bytes);
    memcpy(h + 8, "WAVEfmt ", 8);
    le32(h + 16, 16);
    le16(h + 20, 1);
    le16(h + 22, 2);
    le32(h + 24, SR);
    le32(h + 28, SR * 4);
    le16(h + 32, 4);
    le16(h + 34, 16);
    memcpy(h + 36, "data", 4);
    le32(h + 40, data_bytes);
    fwrite(h, 1, sizeof h, f);
}

static bool ends_with(const char *s, const char *suffix) {
    size_t a = strlen(s), b = strlen(suffix);
    return a >= b && strcmp(s + a - b, suffix) == 0;
}

static void init_runtime(void) {
    init_reverb();
    A.reverb_ready = true;
    const bed_t *b = &BEDS[0];
    A.gain_cur = b->gain;
    A.freq_cur = b->freq;
    A.q_cur = b->q;
    A.lfo_rate_cur = b->lfo_rate;
    A.lfo_depth_cur = b->lfo_depth;
    A.drone_gain_cur = b->drone_gain;
    A.drone_root_cur = b->drone_root;
}

static bool reset_state(void) {
    /* Reinitialization is supported: stop/join the old backend before the
     * struct is cleared, then create a fresh mutex for the new lifetime. */
    if (A.mu_init || A.pipe || A.thread_started) audio_shutdown();
    memset(&A, 0, sizeof A);
    atomic_init(&A.avail, false);
    atomic_init(&A.running, false);
    A.enabled = true;
    A.volume = 0.8f;
    A.fade = 1.0f;
    A.energy_t = A.energy = 0.5f;
    A.bed_noise.s = 0x1234567u;
    A.voice_seed = 0xA341316Cu;
    A.fd = -1;
    if (pthread_mutex_init(&A.mu, NULL) != 0) return false;
    A.mu_init = true;
    snprintf(A.backend, sizeof A.backend, "none");
    return true;
}

static bool audio_offline_begin_internal(const char *path, bool wav) {
    if (!path || !*path || !reset_state()) return false;
    A.pipe = fopen(path, "wb");
    if (!A.pipe) {
        audio_shutdown();
        return false;
    }
    A.dump_mode = true;
    A.offline = true;
    A.wav = wav;
    snprintf(A.backend, sizeof A.backend, "%s", wav ? "wav" : "raw");
    if (wav) {
        wav_header(A.pipe, 0);
        if (ferror(A.pipe)) {
            audio_shutdown();
            return false;
        }
    }
    init_runtime();
    A.avail = true;
    return true;
}

bool audio_offline_begin(const char *wav_path) {
    return audio_offline_begin_internal(wav_path, true);
}

bool audio_offline_raw_begin(const char *raw_path) {
    return audio_offline_begin_internal(raw_path, false);
}

bool audio_offline_step(int frames) {
    if (!A.offline || !A.pipe || frames < 0) return false;
    int16_t buf[BLOCK * 2];
    while (frames > 0) {
        int n = frames < BLOCK ? frames : BLOCK;
        render_block(buf, n);
        if (fwrite(buf, 4, (size_t)n, A.pipe) != (size_t)n) return false;
        A.data_bytes += (uint32_t)n * 4u;
        frames -= n;
    }
    return true;
}

static void lock_if_ready(void) { if (A.mu_init) pthread_mutex_lock(&A.mu); }
static void unlock_if_ready(void) { if (A.mu_init) pthread_mutex_unlock(&A.mu); }

float audio_peak(void) {
    lock_if_ready();
    float peak = A.peak > 1.0f ? 1.0f : A.peak;
    unlock_if_ready();
    return peak;
}

bool audio_init(bool want, const char *dump_path) {
    if (!reset_state()) return false;
    if (!want) {
        audio_shutdown();
        return false;
    }

    if (!dump_path) {
        struct sigaction ignore_pipe;
        memset(&ignore_pipe, 0, sizeof ignore_pipe);
        ignore_pipe.sa_handler = SIG_IGN;
        sigemptyset(&ignore_pipe.sa_mask);
        if (sigaction(SIGPIPE, &ignore_pipe, &g_old_sigpipe) == 0) g_sigpipe_saved = true;
    }

    if (dump_path) {
        A.pipe = fopen(dump_path, "wb");
        if (!A.pipe) {
            audio_shutdown();
            return false;
        }
        A.dump_mode = true;
        if (ends_with(dump_path, ".wav")) {
            A.wav = true;
            wav_header(A.pipe, 0);
        }
        snprintf(A.backend, sizeof A.backend, "file");
    } else {
        for (size_t i = 0; i < ARRAY_LEN(PLAYERS); i++) {
            if (!find_in_path(PLAYERS[i].bin)) continue;
            A.pipe = popen(PLAYERS[i].cmd, "w");
            if (!A.pipe) continue;
            A.fd = fileno(A.pipe);
            snprintf(A.backend, sizeof A.backend, "%s", PLAYERS[i].bin);
            break;
        }
        if (!A.pipe) {
            audio_shutdown();
            return false;
        }
    }

    init_runtime();

    A.avail = true;
    A.running = true;
    A.thread_started = true;
    if (pthread_create(&A.th, NULL, audio_thread, NULL) != 0) {
        A.thread_started = false;
        A.running = false;
        A.avail = false;
        audio_shutdown();
        return false;
    }
    return true;
}

void audio_shutdown(void) {
    if (A.thread_started) {
        A.running = false;
        pthread_join(A.th, NULL);
        A.thread_started = false;
    }
    if (A.pipe != NULL) {
        if (A.dump_mode && A.wav) {
            fflush(A.pipe);
            fseek(A.pipe, 0, SEEK_SET);
            wav_header(A.pipe, A.data_bytes);
        }
        if (A.dump_mode)
            fclose(A.pipe);
        else if (!A.offline)
            pclose(A.pipe);
        A.pipe = NULL;
    }
    A.avail = false;
    A.running = false;
    if (A.reverb_ready) {
        free_reverb();
        A.reverb_ready = false;
    }
    if (A.mu_init) {
        pthread_mutex_destroy(&A.mu);
        A.mu_init = false;
    }
    if (g_sigpipe_saved) {
        sigaction(SIGPIPE, &g_old_sigpipe, NULL);
        g_sigpipe_saved = false;
    }
}

bool audio_available(void) { return atomic_load(&A.avail); }
const char *audio_backend(void) { return A.backend; }

void audio_set_enabled(bool on) { lock_if_ready(); A.enabled = on; unlock_if_ready(); }
bool audio_enabled(void) { lock_if_ready(); bool v = A.enabled; unlock_if_ready(); return v; }
void audio_set_volume(float v) {
    if (!isfinite(v)) v = 0.8f;
    lock_if_ready(); A.volume = clampf(v, 0.0f, 1.0f); unlock_if_ready();
}
float audio_volume(void) { lock_if_ready(); float v = A.volume; unlock_if_ready(); return v; }

void audio_set_bed(audio_bed_t bed) {
    if ((int)bed >= 0 && bed < BED_COUNT) { lock_if_ready(); A.bed = (int)bed; unlock_if_ready(); }
}

void audio_set_energy(float e) { if (!isfinite(e)) e = 0.5f; lock_if_ready(); A.energy_t = saturatef(e); unlock_if_ready(); }
void audio_set_fade(float f) { if (!isfinite(f)) f = 1.0f; lock_if_ready(); A.fade = saturatef(f); unlock_if_ready(); }
void audio_set_binaural(bool on) { lock_if_ready(); A.binaural = on; unlock_if_ready(); }
bool audio_binaural(void) { lock_if_ready(); bool v = A.binaural; unlock_if_ready(); return v; }

void audio_trigger(audio_kind_t kind, float pitch, float pan, float vel) {
    /* The audio API can be called by scene plugins or other threads: never let
     * invalid enums or NaN controls poison the synth state or PCM conversion. */
    if ((int)kind < 0 || kind >= AUDIO_KIND_COUNT) return;
    if (!isfinite(pitch)) pitch = 0.5f;
    if (!isfinite(pan)) pan = 0.0f;
    if (!isfinite(vel)) vel = 0.0f;
    pitch = saturatef(pitch);
    pan = clampf(pan, -1.0f, 1.0f);
    vel = saturatef(vel);
    if (!atomic_load(&A.avail) || !A.mu_init) return;
    pthread_mutex_lock(&A.mu);
    if (!A.enabled) { pthread_mutex_unlock(&A.mu); return; }
    int next = (A.qh + 1) % QUEUE_LEN;
    if (next != A.qt) {
        A.q[A.qh].kind = kind;
        A.q[A.qh].pitch = pitch;
        A.q[A.qh].pan = pan;
        A.q[A.qh].vel = vel;
        A.qh = next;
    }
    pthread_mutex_unlock(&A.mu);
}
