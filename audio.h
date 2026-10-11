#ifndef ASMR_AUDIO_H
#define ASMR_AUDIO_H

#include "common.h"

typedef enum {
    AUDIO_TICK = 0,
    AUDIO_PLINK,
    AUDIO_CHIME,
    AUDIO_BELL,
    AUDIO_WHOOSH,
    AUDIO_SWELL,
    AUDIO_THUNDER,
    AUDIO_BUBBLE,
    AUDIO_CRACKLE,
    AUDIO_DRIP,
    AUDIO_SCRAPE,
    AUDIO_KIND_COUNT
} audio_kind_t;

typedef enum {
    BED_RAIN = 0,
    BED_TIDE,
    BED_STARS,
    BED_RIPPLES,
    BED_DRIFT,
    BED_FIREFLY,
    BED_SNOW,
    BED_AURORA,
    BED_EMBERS,
    BED_STORM,
    BED_SAKURA,
    BED_DEEP,
    BED_LANTERN,
    BED_LAVA,
    BED_WINDOW,
    BED_CAVE,
    BED_CHIMES,
    BED_ZEN,
    BED_PENDULUM,
    BED_GALAXY,
    BED_BONSAI,
    BED_DIGITAL,
    BED_COUNT
} audio_bed_t;

/* Starts the synth thread. Audio is piped as raw s16le stereo to an external
 * player (paplay, aplay, pw-cat, play, ffplay) or to dump_path when given. */
bool audio_init(bool want, const char *dump_path);
void audio_shutdown(void);

bool audio_available(void);
const char *audio_backend(void);

void audio_set_enabled(bool on);
bool audio_enabled(void);
void audio_set_volume(float v);
float audio_volume(void);

void audio_set_bed(audio_bed_t bed);
void audio_set_energy(float e);
void audio_set_binaural(bool on); /* 8 Hz offset between the left and right drone */
bool audio_binaural(void);
float audio_peak(void); /* recent output peak, 0..1 */
/* Offline rendering: no real-time playback, audio is produced a block at a time. */
bool audio_offline_begin(const char *wav_path);
/* Exact-length raw PCM export for deterministic/headless renders. */
bool audio_offline_raw_begin(const char *raw_path);
bool audio_offline_step(int frames);
void audio_set_fade(float f); /* 0..1 master fade, used by the sleep timer */

/* pitch: 0..1, pan: -1..1, vel: 0..1. Safe to call from any thread. */
void audio_trigger(audio_kind_t kind, float pitch, float pan, float vel);

#endif
