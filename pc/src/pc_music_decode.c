/* pc_music_decode.c - decode a whole audio file to interleaved stereo s16.
 *
 * The vendored single-file decoders (lib/audio_decoders) are compiled here, in
 * their own translation unit, so their implementations never see the decomp
 * headers. Nothing in this file touches game state. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DR_MP3_IMPLEMENTATION
#include "dr_mp3.h"
#define DR_FLAC_IMPLEMENTATION
#include "dr_flac.h"
#define DR_WAV_IMPLEMENTATION
#include "dr_wav.h"
#include "stb_vorbis.c"

typedef enum { DEC_NONE, DEC_MP3, DEC_FLAC, DEC_WAV, DEC_OGG } DecKind;

static int ext_is(const char* ext, const char* want) {
    for (; *ext != '\0' && *want != '\0'; ext++, want++) {
        char c = *ext;
        if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
        if (c != *want) return 0;
    }
    return *ext == '\0' && *want == '\0';
}

static DecKind kind_from_path(const char* path) {
    const char* ext = strrchr(path, '.');
    if (ext == NULL) return DEC_NONE;
    ext++;
    if (ext_is(ext, "mp3")) return DEC_MP3;
    if (ext_is(ext, "flac")) return DEC_FLAC;
    if (ext_is(ext, "wav")) return DEC_WAV;
    if (ext_is(ext, "ogg")) return DEC_OGG;
    return DEC_NONE;
}

/* Returns a malloc'd buffer of out_rate Hz interleaved stereo s16, or NULL. */
short* pc_music_decode_stereo(const char* path, int out_rate, unsigned int* out_frames) {
    DecKind kind = kind_from_path(path);
    short* src = NULL;
    unsigned long long src_frames = 0;
    unsigned int channels = 0;
    unsigned int rate = 0;

    switch (kind) {
        case DEC_MP3: {
            drmp3_config cfg;
            drmp3_uint64 n = 0;
            src = drmp3_open_file_and_read_pcm_frames_s16(path, &cfg, &n, NULL);
            channels = cfg.channels;
            rate = cfg.sampleRate;
            src_frames = n;
        } break;
        case DEC_FLAC: {
            drflac_uint64 n = 0;
            src = drflac_open_file_and_read_pcm_frames_s16(path, &channels, &rate, &n, NULL);
            src_frames = n;
        } break;
        case DEC_WAV: {
            drwav_uint64 n = 0;
            src = drwav_open_file_and_read_pcm_frames_s16(path, &channels, &rate, &n, NULL);
            src_frames = n;
        } break;
        case DEC_OGG: {
            int ch = 0;
            int sr = 0;
            int n = stb_vorbis_decode_filename(path, &ch, &sr, &src);
            if (n > 0) {
                channels = (unsigned int)ch;
                rate = (unsigned int)sr;
                src_frames = (unsigned long long)n;
            } else {
                src = NULL;
            }
        } break;
        default:
            return NULL;
    }

    if (src == NULL || channels == 0 || rate == 0 || src_frames == 0) {
        free(src);
        return NULL;
    }

    /* Resample (linear) and down/up-mix to stereo in one pass. */
    unsigned long long frames = src_frames * (unsigned long long)out_rate / rate;
    if (frames == 0 || frames > 0x7FFFFFFFull / 2) {
        free(src);
        return NULL;
    }

    short* out = (short*)malloc((size_t)frames * 2 * sizeof(short));
    if (out == NULL) {
        free(src);
        return NULL;
    }

    double step = (double)rate / (double)out_rate;
    for (unsigned long long i = 0; i < frames; i++) {
        double p = (double)i * step;
        unsigned long long a = (unsigned long long)p;
        unsigned long long b = (a + 1 < src_frames) ? a + 1 : a;
        float t = (float)(p - (double)a);
        float l, r;

        if (channels == 1) {
            l = r = src[a] + (src[b] - src[a]) * t;
        } else {
            /* Extra channels beyond the first two are dropped. */
            const short* fa = &src[a * channels];
            const short* fb = &src[b * channels];
            l = fa[0] + (fb[0] - fa[0]) * t;
            r = fa[1] + (fb[1] - fa[1]) * t;
        }
        out[i * 2 + 0] = (short)l;
        out[i * 2 + 1] = (short)r;
    }

    /* dr_libs and stb_vorbis all allocate with the default malloc. */
    free(src);
    *out_frames = (unsigned int)frames;
    return out;
}
