/*
 * Switch 7zip - Nintendo Switch homebrew file manager and archive utility.
 * Author: Xhulio
 * Status: pre-1.0 preview; not fully tested across every firmware, CFW,
 * archive format, multipart set, or very large SD-card workload.
 */

#include "alert_audio.h"

#include <SDL.h>
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static SDL_AudioDeviceID g_audio = 0;
static SDL_AudioSpec g_spec;

void alert_audio_init(void) {
    if (g_audio) return;

    SDL_AudioSpec want;
    SDL_zero(want);
    want.freq = 48000;
    want.format = AUDIO_S16SYS;
    want.channels = 2;
    want.samples = 1024;

    g_audio = SDL_OpenAudioDevice(NULL, 0, &want, &g_spec, 0);
    if (g_audio) {
        SDL_PauseAudioDevice(g_audio, 0);
    }
}

void alert_audio_quit(void) {
    if (g_audio) {
        SDL_CloseAudioDevice(g_audio);
        g_audio = 0;
    }
}

static void queue_tone(float hz, int ms, float volume) {
    if (!g_audio || hz <= 0.0f || ms <= 0) return;

    int frames = (g_spec.freq * ms) / 1000;
    if (frames <= 0) return;
    int channels = g_spec.channels ? g_spec.channels : 2;
    size_t sample_count = (size_t)frames * (size_t)channels;
    int16_t *samples = (int16_t *)calloc(sample_count, sizeof(int16_t));
    if (!samples) return;

    const float two_pi = 6.28318530718f;
    for (int i = 0; i < frames; ++i) {
        float t = (float)i / (float)g_spec.freq;
        float fade = 1.0f;
        int fade_frames = g_spec.freq / 120;
        if (fade_frames > 0) {
            if (i < fade_frames) fade = (float)i / (float)fade_frames;
            if (frames - i < fade_frames) fade = (float)(frames - i) / (float)fade_frames;
            if (fade < 0.0f) fade = 0.0f;
        }
        int16_t v = (int16_t)(sinf(two_pi * hz * t) * 32767.0f * volume * fade);
        for (int c = 0; c < channels; ++c) samples[(size_t)i * channels + c] = v;
    }

    SDL_QueueAudio(g_audio, samples, (Uint32)(sample_count * sizeof(int16_t)));
    free(samples);
}

static void queue_silence(int ms) {
    if (!g_audio || ms <= 0) return;
    int frames = (g_spec.freq * ms) / 1000;
    int channels = g_spec.channels ? g_spec.channels : 2;
    size_t sample_count = (size_t)frames * (size_t)channels;
    int16_t *samples = (int16_t *)calloc(sample_count, sizeof(int16_t));
    if (!samples) return;
    SDL_QueueAudio(g_audio, samples, (Uint32)(sample_count * sizeof(int16_t)));
    free(samples);
}

void alert_audio_play(bool success) {
    if (!g_audio) return;
    SDL_ClearQueuedAudio(g_audio);
    if (success) {
        queue_tone(659.25f, 90, 0.22f);
        queue_silence(35);
        queue_tone(987.77f, 140, 0.22f);
    } else {
        queue_tone(220.00f, 160, 0.24f);
        queue_silence(45);
        queue_tone(164.81f, 220, 0.24f);
    }
}
