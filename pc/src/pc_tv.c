/* pc_tv.c - City Folk's TV programs on the houses' TVs. See pc_tv.h. */
#include "pc_platform.h"
#include "pc_tv.h"
#include "pc_settings.h"
#include "pc_weather.h"

#include "graph.h"
#include "m_common_data.h"
#include "m_play.h"
#include "m_player_lib.h"
#include "libforest/gbi_extensions.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PC_TV_FILE "cf_sky/tv.cftv"
#define PC_TV_VERSION 1
#define PC_TV_SIZE 32
#define PC_TV_PICTURE_BYTES (PC_TV_SIZE * PC_TV_SIZE * 2) /* RGB5A3 */
#define PC_TV_PROGRAMS 11                                  /* City Folk's 0-10 */
#define PC_TV_WEATHER 255                                  /* City Folk's weather report */
#define PC_TV_WEATHER_KINDS 11
#define PC_TV_PROGRAM_MAX (PC_TV_PROGRAMS + PC_TV_WEATHER_KINDS)
#define PC_TV_SCREENS 8
#define PC_TV_REPORT_HALF 180 /* minutes each half of the forecast covers */

#define PC_TV_RATE 32000          /* PC_AUDIO_SAMPLE_RATE in pc_audio.c, and the rate cf_tv_sound.py writes */
#define PC_TV_SOUNDS 12           /* cf_tv_sound.py's CYCLES: City Folk's 0-10, then the weather report */
#define PC_TV_VOLUME 0.45f        /* City Folk's own, for a TV in earshot */
#define PC_TV_HEAR_FULL 120.0f    /* full volume this close (world units; a tile is 40) */
#define PC_TV_HEAR_NONE 420.0f    /* silent from this far */
#define PC_TV_PAN_WIDTH 240.0f    /* a TV this far to the side is panned the most */
#define PC_TV_PAN_MAX 0.6f
#define PC_TV_GONE_MS 150         /* no TV drawn for this long: fade out */
#define PC_TV_RESYNC (PC_TV_RATE / 4) /* off the clock by more than this: jump back to it */

typedef struct {
    u16 frame;
    u16 picture;
} pc_tv_cut_c;

typedef struct {
    int frames;
    int picture_num;
    int cut_num;
    u8* pictures;
    const u8* cuts; /* pc_tv_cut_c, little-endian and maybe unaligned in the file */
} pc_tv_program_c;

static struct {
    int state; /* 0 not tried yet, 1 loaded, -1 none */
    u8* file;
    int program_num;
    pc_tv_program_c program[PC_TV_PROGRAM_MAX];
    int slot_num[7];
    const u8* slots[7]; /* { s16 program; u8 hour, minute } */
    u32 frame_counter;
    int sec;
    int tick;
    float accum;
    int screen_num;
    struct {
        const Vtx* src;
        Vtx vtx[4];
    } screen[PC_TV_SCREENS];
} l_tv;

/* The sound: the program's cycle, played by the audio thread; the game thread says what, where and how loud */
static struct {
    SDL_mutex* lock;
    int sound;        /* the one loaded, -1 none */
    s16* pcm;         /* interleaved stereo */
    u32 frames;
    u32 pos;          /* audio thread */
    u32 want_pos;     /* where the clock says it is, when it was told */
    int resync;
    f32 volume, pan;  /* wanted */
    f32 cur_volume, cur_pan;
    u32 heard_ms;     /* when a TV was last drawn */
    u8 missing[PC_TV_SOUNDS];
    u32 best_frame;   /* the game frame best_dist is for */
    f32 best_dist;
} l_snd = { .sound = -1 };

static const char* l_sound_names[PC_TV_SOUNDS] = { "storm", "color", "news", "drama", "song",  "quiz",
                                                   "travel", "hero", "variety", "movie", "soccer", "weather" };

short* pc_music_decode_stereo(const char* path, int out_rate, unsigned int* out_frames);

/* cf_tv.py's WEATHER_KINDS: the forecast, then later */
static const char* l_weather_kinds[PC_TV_WEATHER_KINDS] = { "ff", "cc", "rr", "fc", "cf", "fr",
                                                            "cr", "rc", "ss", "cs", "sc" };

static int pc_tv_u16(const u8* p) {
    return p[0] | (p[1] << 8);
}

static int pc_tv_load(void) {
    FILE* f;
    long size;
    long at;
    int i;

    if (l_tv.state != 0) {
        return l_tv.state > 0;
    }
    l_tv.state = -1;
    f = fopen(PC_TV_FILE, "rb");
    if (f == NULL) {
        return FALSE;
    }
    if (fseek(f, 0, SEEK_END) != 0 || (size = ftell(f)) < 12 || fseek(f, 0, SEEK_SET) != 0 ||
        (l_tv.file = (u8*)malloc((size_t)size)) == NULL || fread(l_tv.file, 1, (size_t)size, f) != (size_t)size) {
        fclose(f);
        free(l_tv.file);
        l_tv.file = NULL;
        return FALSE;
    }
    fclose(f);
    if (memcmp(l_tv.file, "CFTV", 4) != 0 || pc_tv_u16(l_tv.file + 4) != PC_TV_VERSION ||
        pc_tv_u16(l_tv.file + 8) != PC_TV_PROGRAM_MAX) {
        printf("[TV] %s is from another version: run the City Folk conversion again\n", PC_TV_FILE);
        goto bad;
    }
    l_tv.program_num = PC_TV_PROGRAM_MAX;
    at = 12;
    for (i = 0; i < l_tv.program_num; i++) {
        pc_tv_program_c* p = &l_tv.program[i];

        if (size - at < 8) {
            goto bad;
        }
        p->frames = pc_tv_u16(l_tv.file + at);
        p->picture_num = pc_tv_u16(l_tv.file + at + 2);
        p->cut_num = pc_tv_u16(l_tv.file + at + 4);
        at += 8;
        if (p->frames == 0 || p->picture_num == 0 || p->cut_num == 0 ||
            size - at < (long)p->picture_num * PC_TV_PICTURE_BYTES + p->cut_num * 4) {
            goto bad;
        }
        p->pictures = l_tv.file + at;
        at += (long)p->picture_num * PC_TV_PICTURE_BYTES;
        p->cuts = l_tv.file + at;
        at += p->cut_num * 4;
    }
    for (i = 0; i < 7; i++) {
        if (size - at < 2) {
            goto bad;
        }
        l_tv.slot_num[i] = pc_tv_u16(l_tv.file + at);
        l_tv.slots[i] = l_tv.file + at + 2;
        at += 2 + l_tv.slot_num[i] * 4;
        if (at > size) {
            goto bad;
        }
    }
    l_tv.state = 1;
    return TRUE;

bad:
    printf("[TV] %s can't be read: TVs show their own static\n", PC_TV_FILE);
    free(l_tv.file);
    l_tv.file = NULL;
    return FALSE;
}

/* The weather report for the forecast: now, and the hours after */
static int pc_tv_weather_report(int now) {
    char kind[3];
    int later = now + PC_TV_REPORT_HALF;
    int k, pass;

    kind[0] = pc_weather_forecast(now, later);
    kind[1] = later < 24 * 60 ? pc_weather_forecast(later, later + PC_TV_REPORT_HALF) : kind[0];
    kind[2] = '\0';
    /* City Folk has no panel for fine after rain or snow, or rain and snow together: cloudy stands in for
     * fine, then the first weather for both */
    for (pass = 0; pass < 3; pass++) {
        for (k = 0; k < PC_TV_WEATHER_KINDS; k++) {
            if (strcmp(l_weather_kinds[k], kind) == 0) {
                return PC_TV_PROGRAMS + k;
            }
        }
        if (pass == 0) {
            kind[0] = kind[0] == 'f' ? 'c' : kind[0];
            kind[1] = kind[1] == 'f' ? 'c' : kind[1];
        } else {
            kind[1] = kind[0];
        }
    }
    return PC_TV_PROGRAMS;
}

/* The program on now: the day's last one started by this hour and minute */
static int pc_tv_program_now(void) {
    const lbRTC_time_c* rtc = &Common_Get(time.rtc_time);
    const char* dev = getenv("PC_TV_PROGRAM");
    int day = rtc->weekday % 7;
    int now = rtc->hour * 60 + rtc->min;
    int program = 0;
    int i;

    if (dev != NULL) {
        program = atoi(dev);
        return program >= 0 && program < l_tv.program_num ? program : 0;
    }
    for (i = 0; i < l_tv.slot_num[day]; i++) {
        const u8* s = l_tv.slots[day] + i * 4;

        if (s[2] * 60 + s[3] <= now) {
            program = (s16)pc_tv_u16(s);
        }
    }
    if (program == PC_TV_WEATHER) {
        return pc_tv_weather_report(now);
    }
    return program >= 0 && program < PC_TV_PROGRAMS ? program : 0;
}

/* 60ths of a second, once a frame however many TVs are on */
static void pc_tv_tick(GAME* game) {
    int ticks;

    if (game->frame_counter == l_tv.frame_counter) {
        return;
    }
    l_tv.frame_counter = game->frame_counter;
    ticks = graph_dt_60hz_ticks(game, &l_tv.accum);
    if (Common_Get(time.now_sec) != l_tv.sec) {
        l_tv.sec = Common_Get(time.now_sec);
        l_tv.tick = 0;
    } else if ((l_tv.tick += ticks) > 59) {
        l_tv.tick = 59;
    }
}

/* The TV's screen quad, its texture coordinates spread over a whole picture, top left to bottom right */
static const Vtx* pc_tv_screen_vtx(const Vtx* src) {
    int cx = 0, cy = 0, i;
    Vtx* vtx;

    for (i = 0; i < l_tv.screen_num; i++) {
        if (l_tv.screen[i].src == src) {
            return l_tv.screen[i].vtx;
        }
    }
    if (l_tv.screen_num == PC_TV_SCREENS) {
        return NULL;
    }
    vtx = l_tv.screen[l_tv.screen_num].vtx;
    l_tv.screen[l_tv.screen_num++].src = src;
    for (i = 0; i < 4; i++) {
        cx += src[i].v.ob[0];
        cy += src[i].v.ob[1];
    }
    for (i = 0; i < 4; i++) {
        vtx[i] = src[i];
        vtx[i].v.tc[0] = src[i].v.ob[0] * 4 < cx ? 0 : PC_TV_SIZE << 5;
        vtx[i].v.tc[1] = src[i].v.ob[1] * 4 > cy ? 0 : PC_TV_SIZE << 5;
    }
    return vtx;
}

int pc_tv_sound_on(void) {
    return g_pc_settings.cf_tv && pc_tv_load() && !l_snd.missing[PC_TV_SOUNDS - 1];
}

/* The program's sound (game thread): load it if it's another one */
static int pc_tv_sound_load(int sound) {
    char path[64];
    s16* pcm;
    s16* old;
    unsigned int frames = 0;

    if (sound == l_snd.sound) {
        return l_snd.pcm != NULL;
    }
    if (l_snd.missing[sound]) {
        return FALSE;
    }
    snprintf(path, sizeof(path), "cf_sky/tv/%s.wav", l_sound_names[sound]);
    pcm = (s16*)pc_music_decode_stereo(path, PC_TV_RATE, &frames);
    if (pcm == NULL || frames == 0) {
        printf("[TV] %s isn't there: TVs keep their own sound\n", path);
        free(pcm);
        memset(l_snd.missing, 1, sizeof(l_snd.missing)); /* an old conversion: none are */
        return FALSE;
    }
    if (l_snd.lock == NULL) {
        l_snd.lock = SDL_CreateMutex();
    }
    SDL_LockMutex(l_snd.lock);
    old = l_snd.pcm;
    l_snd.pcm = pcm;
    l_snd.frames = frames;
    l_snd.sound = sound;
    l_snd.resync = TRUE;
    l_snd.cur_volume = 0.0f; /* a new program comes in from silence */
    SDL_UnlockMutex(l_snd.lock);
    free(old);
    return TRUE;
}

/* A TV showing program on screen at pos: the nearest one to the player is the one heard */
static void pc_tv_hear(GAME* game, int program, const xyz_t* pos) {
    PLAYER_ACTOR* player = GET_PLAYER_ACTOR((GAME_PLAY*)game);
    int sound = program < PC_TV_PROGRAMS ? program : PC_TV_SOUNDS - 1;
    f32 dx, dz, dist, volume, pan;
    u64 clock;

    if (player == NULL || pos == NULL || !pc_tv_sound_load(sound)) {
        return;
    }
    dx = pos->x - player->actor_class.world.position.x;
    dz = pos->z - player->actor_class.world.position.z;
    dist = sqrtf(dx * dx + dz * dz);
    if (l_snd.best_frame == game->frame_counter && dist >= l_snd.best_dist) {
        return;
    }
    l_snd.best_frame = game->frame_counter;
    l_snd.best_dist = dist;

    volume = (PC_TV_HEAR_NONE - dist) / (PC_TV_HEAR_NONE - PC_TV_HEAR_FULL);
    volume = PC_TV_VOLUME * (volume < 0.0f ? 0.0f : volume > 1.0f ? 1.0f : volume);
    pan = dx / PC_TV_PAN_WIDTH;
    pan = PC_TV_PAN_MAX * (pan < -1.0f ? -1.0f : pan > 1.0f ? 1.0f : pan);
    /* the same clock as the picture: 60ths of a second, 1600/3 samples each */
    clock = ((u64)(u32)l_tv.sec * 60u + (u64)l_tv.tick) * (PC_TV_RATE / 20) / 3;

    SDL_LockMutex(l_snd.lock);
    l_snd.volume = volume;
    l_snd.pan = pan;
    l_snd.want_pos = (u32)(clock % l_snd.frames);
    l_snd.heard_ms = SDL_GetTicks();
    SDL_UnlockMutex(l_snd.lock);
}

void pc_tv_mix(short* buf, unsigned int n_samples) {
    static FILE* dump; /* dev: PC_TV_SOUND_DUMP=file writes what the TVs add, raw s16 stereo 32 kHz */
    static int dump_tried;
    u32 frames = n_samples / 2;
    f32 volume, step = 1.0f / (PC_TV_RATE / 10); /* volume and pan glide over a tenth of a second */
    u32 i;

    if (l_snd.lock == NULL) {
        return;
    }
    SDL_LockMutex(l_snd.lock);
    if (l_snd.pcm == NULL) {
        SDL_UnlockMutex(l_snd.lock);
        return;
    }
    if (!dump_tried) {
        const char* path = getenv("PC_TV_SOUND_DUMP");

        dump_tried = TRUE;
        dump = path != NULL ? fopen(path, "wb") : NULL;
    }
    volume = SDL_GetTicks() - l_snd.heard_ms > PC_TV_GONE_MS ? 0.0f : l_snd.volume;
    if (l_snd.resync || (volume > 0.0f && abs((int)l_snd.pos - (int)l_snd.want_pos) > PC_TV_RESYNC &&
                         abs((int)l_snd.pos - (int)l_snd.want_pos) < (int)l_snd.frames - PC_TV_RESYNC)) {
        l_snd.pos = l_snd.want_pos;
        l_snd.resync = FALSE;
    }
    for (i = 0; i < frames; i++) {
        const s16* in = &l_snd.pcm[l_snd.pos * 2];
        f32 gl, gr;
        int l, r;

        l_snd.cur_volume += l_snd.cur_volume < volume ? fminf(step, volume - l_snd.cur_volume)
                                                      : -fminf(step, l_snd.cur_volume - volume);
        l_snd.cur_pan += l_snd.cur_pan < l_snd.pan ? fminf(step, l_snd.pan - l_snd.cur_pan)
                                                   : -fminf(step, l_snd.cur_pan - l_snd.pan);
        if (++l_snd.pos >= l_snd.frames) {
            l_snd.pos = 0;
        }
        if (l_snd.cur_volume <= 0.0f) {
            continue;
        }
        gl = l_snd.cur_volume * (l_snd.cur_pan > 0.0f ? 1.0f - l_snd.cur_pan : 1.0f);
        gr = l_snd.cur_volume * (l_snd.cur_pan < 0.0f ? 1.0f + l_snd.cur_pan : 1.0f);
        if (dump != NULL) {
            s16 out[2] = { (s16)(in[0] * gl), (s16)(in[1] * gr) };

            fwrite(out, sizeof(out), 1, dump);
        }
        l = buf[i * 2 + 0] + (int)(in[0] * gl);
        r = buf[i * 2 + 1] + (int)(in[1] * gr);
        buf[i * 2 + 0] = (s16)(l > 32767 ? 32767 : l < -32768 ? -32768 : l);
        buf[i * 2 + 1] = (s16)(r > 32767 ? 32767 : r < -32768 ? -32768 : r);
    }
    SDL_UnlockMutex(l_snd.lock);
}

Gfx* pc_tv_screen(GAME* game, const Vtx* src, const xyz_t* pos) {
    const pc_tv_program_c* p;
    const Vtx* vtx;
    Gfx* dl;
    Gfx* gfx;
    int program, frame, picture, i;

    if (!g_pc_settings.cf_tv || !pc_tv_load() || (vtx = pc_tv_screen_vtx(src)) == NULL) {
        return NULL;
    }
    pc_tv_tick(game);
    program = pc_tv_program_now();
    pc_tv_hear(game, program, pos);
    p = &l_tv.program[program];
    frame = (int)(((u32)l_tv.sec * 60u + (u32)l_tv.tick) % (u32)p->frames);
    picture = 0;
    for (i = 0; i < p->cut_num && pc_tv_u16(p->cuts + i * 4) <= frame; i++) {
        picture = pc_tv_u16(p->cuts + i * 4 + 2);
    }
    if (picture >= p->picture_num) {
        picture = 0;
    }

    dl = gfx = (Gfx*)GRAPH_ALLOC_TYPE(game->graph, Gfx, 12);
    if (dl == NULL) {
        return NULL;
    }
    /* the TVs' own screens draw like this, with a direct colour picture for their 16 colours */
    gSPTexture(gfx++, 0, 0, 0, G_TX_RENDERTILE, G_ON);
    gDPSetRenderMode(gfx++, G_RM_FOG_SHADE_A, G_RM_AA_ZB_OPA_SURF2);
    gDPSetCombineLERP(gfx++, 0, 0, 0, TEXEL0, 0, 0, 0, TEXEL0, PRIMITIVE, 0, COMBINED, 0, 0, 0, 0, COMBINED);
    gDPSetTextureImage_Dolphin(gfx++, G_IM_FMT_RGBA, G_IM_SIZ_16b, PC_TV_SIZE, PC_TV_SIZE,
                               p->pictures + picture * PC_TV_PICTURE_BYTES);
    gDPSetTile_Dolphin(gfx++, G_DOLPHIN_TLUT_DEFAULT_MODE, 0, 0, GX_CLAMP, GX_CLAMP, 0, 0);
    gDPSetTileSize(gfx++, G_TX_RENDERTILE, 0, 0, (PC_TV_SIZE - 1) << 2, (PC_TV_SIZE - 1) << 2);
    gDPSetPrimColor(gfx++, 0, 128, 255, 255, 255, 255);
    gSPLoadGeometryMode(gfx++, G_ZBUFFER | G_SHADE | G_CULL_BACK | G_FOG | G_SHADING_SMOOTH | G_DECAL_LEQUAL);
    gSPVertex(gfx++, vtx, 4, 0);
    gSP2Triangles(gfx++, 0, 1, 2, 0, 2, 3, 0, 0);
    gSPEndDisplayList(gfx++);
    return dl;
}
