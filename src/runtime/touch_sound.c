/* CHEAT's sound (touch.c): made here, not taken from the game.
 *   on   the pocket radio's static (the game's warning that something is near) swelling up, then a heavy
 *        metal clang, as of the Great Knife set down: inharmonic partials, each dying away at its own rate
 *   off  a short burst of static, cut off
 * Played on an audio device of its own, alongside the game's. */
#if defined(SH2_IOS) || defined(SH2_TOUCH)
#include "rt.h"
#include <SDL2/SDL.h>
#include <math.h>

#define RATE 48000

static uint32_t rng = 0x5348u;
static float noise(void) {  /* xorshift: white noise in -1..1, the same each time */
    rng ^= rng << 13, rng ^= rng >> 17, rng ^= rng << 5;
    return (float)(rng & 0xFFFF) / 32768.0f - 1.0f;
}

/* Radio static: white noise kept to the radio's band, crackling (its loudness jumping every 15-60 ms). */
static void add_static(float *out, int n, float gain, float (*env)(float t)) {
    float lp = 0, hp_in = 0, hp = 0, crackle = 1;
    int next = 0;
    for (int i = 0; i < n; i++) {
        if (i >= next) crackle = 0.35f + 0.65f * fabsf(noise()), next = i + RATE / 66 + (int)(fabsf(noise()) * RATE / 22);
        float x = noise();
        lp += 0.45f * (x - lp);                 /* low-pass, about 5 kHz */
        hp = 0.97f * (hp + lp - hp_in), hp_in = lp;  /* high-pass, about 250 Hz */
        out[i] += hp * crackle * gain * env((float)i / RATE);
    }
}
static float swell(float t) { return t < 0.5f ? t / 0.5f : t < 0.62f ? 1.0f : fmaxf(0.0f, 1.0f - (t - 0.62f) / 0.45f); }
static float burst(float t) { return t < 0.02f ? t / 0.02f : fmaxf(0.0f, 1.0f - (t - 0.02f) / 0.2f); }

/* A struck slab of iron: partials not in tune with each other, the high ones gone first, and a scrape of
 * noise at the strike. */
static void add_clang(float *out, int n, float at, float gain) {
    static const float freq[] = {58, 143, 236, 371, 598, 911}, amp[] = {1, 0.7f, 0.5f, 0.32f, 0.2f, 0.12f},
                       decay[] = {0.9f, 0.6f, 0.42f, 0.28f, 0.18f, 0.1f};
    int s0 = (int)(at * RATE);
    for (int i = s0; i < n; i++) {
        float t = (float)(i - s0) / RATE, v = 0;
        for (int p = 0; p < 6; p++)
            v += amp[p] * expf(-t / decay[p]) * sinf(6.2831853f * freq[p] * t * (1.0f + 0.002f * sinf(9.0f * t)));
        v += noise() * expf(-t / 0.025f) * 0.8f;  /* the strike */
        out[i] += v * gain;
    }
}

static void normalize(float *out, int n, float peak) {
    float m = 0;
    for (int i = 0; i < n; i++) m = fmaxf(m, fabsf(out[i]));
    if (m > 0)
        for (int i = 0; i < n; i++) out[i] *= peak / m;
    for (int i = 0; i < 480 && i < n; i++) out[n - 1 - i] *= (float)i / 480;  /* no click at the end */
}

/* The samples (mono floats at 48 kHz), in a buffer the caller frees; *n their count. */
float *touch_sound_make(int on, int *n) {
    rng = 0x5348u;
    *n = (int)((on ? 1.9f : 0.3f) * RATE);
    float *s = calloc(*n, sizeof *s);
    if (!s) return NULL;
    if (on) {
        add_static(s, *n, 0.55f, swell);
        add_clang(s, *n, 0.55f, 0.5f);
    } else
        add_static(s, *n, 0.6f, burst);
    normalize(s, *n, 0.7f);
    return s;
}

static SDL_AudioDeviceID dev;

/* Main thread (the tray, or the CHEAT button's hold). */
void touch_sound_cheat(int on) {
    if (!dev) {
        if (!SDL_WasInit(SDL_INIT_AUDIO) && SDL_InitSubSystem(SDL_INIT_AUDIO)) return;
        SDL_AudioSpec want = {.freq = RATE, .format = AUDIO_F32SYS, .channels = 1, .samples = 1024};
        dev = SDL_OpenAudioDevice(NULL, 0, &want, NULL, 0);
        if (!dev) { rt_log("touch: no audio device for CHEAT's sound: %s", SDL_GetError()); return; }
        SDL_PauseAudioDevice(dev, 0);
    }
    int n;
    float *s = touch_sound_make(on, &n);
    if (!s) return;
    SDL_ClearQueuedAudio(dev);
    SDL_QueueAudio(dev, s, (Uint32)(n * sizeof *s));
    free(s);
}
#endif
