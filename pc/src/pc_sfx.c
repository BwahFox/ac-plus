/* pc_sfx.c - one-shot sound effects from files. See pc_sfx.h. */
#include "pc_platform.h"
#include "pc_sfx.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SFX_RATE 32000 /* PC_AUDIO_SAMPLE_RATE in pc_audio.c */
#define SFX_CACHE 8
#define SFX_VOICES 4

short* pc_music_decode_stereo(const char* path, int out_rate, unsigned int* out_frames);

typedef struct {
    char name[32];
    s16* pcm; /* interleaved stereo, NULL if the file isn't there */
    unsigned int frames;
} pc_sfx_c;

typedef struct {
    const pc_sfx_c* sfx;
    u32 pos;
    f32 volume;
} pc_sfx_voice_c;

static pc_sfx_c s_cache[SFX_CACHE];
static pc_sfx_voice_c s_voices[SFX_VOICES];
static SDL_mutex* s_lock;

static const pc_sfx_c* pc_sfx_get(const char* name) {
    int i;

    for (i = 0; i < SFX_CACHE; i++) {
        if (s_cache[i].name[0] != '\0' && strcmp(s_cache[i].name, name) == 0) {
            return &s_cache[i];
        }
    }
    for (i = 0; i < SFX_CACHE; i++) {
        if (s_cache[i].name[0] == '\0') {
            char path[96];

            snprintf(path, sizeof(path), "cf_sky/sfx/%s.wav", name);
            snprintf(s_cache[i].name, sizeof(s_cache[i].name), "%s", name);
            s_cache[i].pcm = (s16*)pc_music_decode_stereo(path, SFX_RATE, &s_cache[i].frames); /* tried once */
            printf("[SFX] %s: %s\n", path, s_cache[i].pcm != NULL ? "loaded" : "not there");
            return &s_cache[i];
        }
    }
    return NULL;
}

void pc_sfx_play(const char* name, float volume) {
    const pc_sfx_c* sfx = pc_sfx_get(name);
    int i;

    if (sfx == NULL || sfx->pcm == NULL || sfx->frames == 0) {
        return;
    }
    if (s_lock == NULL) {
        s_lock = SDL_CreateMutex();
    }
    SDL_LockMutex(s_lock);
    for (i = 0; i < SFX_VOICES; i++) {
        if (s_voices[i].sfx == NULL) {
            s_voices[i].sfx = sfx;
            s_voices[i].pos = 0;
            s_voices[i].volume = volume;
            break;
        }
    }
    SDL_UnlockMutex(s_lock);
}

void pc_sfx_mix(short* buf, unsigned int n_samples) {
    u32 frames = n_samples / 2;
    int v;

    if (s_lock == NULL) {
        return;
    }
    SDL_LockMutex(s_lock);
    for (v = 0; v < SFX_VOICES; v++) {
        pc_sfx_voice_c* vc = &s_voices[v];
        u32 i;

        if (vc->sfx == NULL) {
            continue;
        }
        for (i = 0; i < frames && vc->pos < vc->sfx->frames; i++, vc->pos++) {
            int l = buf[i * 2 + 0] + (int)(vc->sfx->pcm[vc->pos * 2 + 0] * vc->volume);
            int r = buf[i * 2 + 1] + (int)(vc->sfx->pcm[vc->pos * 2 + 1] * vc->volume);

            buf[i * 2 + 0] = (s16)(l > 32767 ? 32767 : l < -32768 ? -32768 : l);
            buf[i * 2 + 1] = (s16)(r > 32767 ? 32767 : r < -32768 ? -32768 : r);
        }
        if (vc->pos >= vc->sfx->frames) {
            if (getenv("PC_SFX_LOG") != NULL) {
                printf("[SFX] %s played (%u frames)\n", vc->sfx->name, vc->sfx->frames);
            }
            vc->sfx = NULL;
        }
    }
    SDL_UnlockMutex(s_lock);
}
