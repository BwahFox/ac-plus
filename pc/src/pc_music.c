/* pc_music.c - custom hourly music packs (see pc_music.h for the layout).
 *
 * Threads:
 *   Game thread:   pc_music_seq_started() picks a file and points a group's
 *                  slot at it; decoding runs on a short-lived loader thread.
 *   Audio thread:  pc_music_mix() follows each slot's group (running state and
 *                  fade volume) and mixes the decoded PCM into the output.
 * One mutex guards the slots and the track cache. */
#include "pc_platform.h"
#include "pc_settings.h"
#include "pc_music.h"
#include "jaudio_NES/audiostruct.h"
#include "jaudio_NES/audiowork.h"

#define MUSIC_RATE       32000 /* matches PC_AUDIO_SAMPLE_RATE in pc_audio.c */
#define MUSIC_HOURS      24
#define TRACK_CACHE_NUM  4     /* playing + fading-out + prefetched next hour + spare */
#define MUSIC_PATH_MAX   260
/* How long a slot waits for its sequence to actually start (sequence and bank
 * loads are asynchronous) before it gives up, in output frames. */
#define START_GRACE_FRAMES (MUSIC_RATE * 3)

short* pc_music_decode_stereo(const char* path, int out_rate, unsigned int* out_frames);

enum { TRACK_EMPTY, TRACK_LOADING, TRACK_READY, TRACK_FAILED };

typedef struct {
    char path[MUSIC_PATH_MAX];
    s16* pcm;      /* interleaved stereo at MUSIC_RATE */
    u32 frames;
    int state;
    int refs;      /* slots playing it */
    u32 last_used;
    u32 loop_start; /* frame playback returns to at the end (<path>.loop, else 0) */
} MusicTrack;

typedef struct {
    int active;
    int track;     /* index into s_tracks */
    int seq_id;    /* the sequence this slot stands in for */
    int seen;      /* that sequence has been observed running */
    int grace;     /* frames left to wait for it to start */
    u32 pos;
    f32 gain;      /* last applied gain, ramped per buffer to avoid clicks */
    int scene;     /* stands in for whatever BGM plays in this scene (pc_music_set_scene_track) */
} MusicSlot;

static SDL_mutex* s_lock = NULL;
static MusicTrack s_tracks[TRACK_CACHE_NUM];
static MusicSlot s_slots[AUDIO_GROUP_MAX];
static volatile int s_silenced[AUDIO_GROUP_MAX];
static SDL_atomic_t s_any_active;
static u32 s_use_clock = 0;
static char s_scene_path[MUSIC_PATH_MAX]; /* the scene's own track, or "" */

static void music_lock(void) {
    if (s_lock == NULL) s_lock = SDL_CreateMutex();
    SDL_LockMutex(s_lock);
}

static void music_unlock(void) {
    SDL_UnlockMutex(s_lock);
}

/* --- file lookup --- */

static int file_exists(const char* path) {
    FILE* f = fopen(path, "rb");
    if (f == NULL) return FALSE;
    fclose(f);
    return TRUE;
}

static int find_file(char* out, const char* dir, const char* name) {
    static const char* exts[] = { "ogg", "mp3", "flac", "wav" };
    int i;

    for (i = 0; i < (int)ARRAY_COUNT(exts); i++) {
        snprintf(out, MUSIC_PATH_MAX, "music/%s/%s/%s.%s", g_pc_settings.music_pack, dir, name, exts[i]);
        if (file_exists(out)) return TRUE;
    }
    return FALSE;
}

/* rain/snow variant -> normal hourly track -> nothing (the game's own). */
static int resolve_track(char* out, int hour, int tenki) {
    char hh[4];

    snprintf(hh, sizeof(hh), "%02d", hour);
    if (tenki == 1) {
        if (find_file(out, "rain", hh) || find_file(out, "rain", "all")) return TRUE;
    } else if (tenki == 2) {
        if (find_file(out, "snow", hh) || find_file(out, "snow", "all")) return TRUE;
    }
    return find_file(out, "hourly", hh);
}

/* --- track cache (call with the lock held) --- */

typedef struct {
    int index;
    char path[MUSIC_PATH_MAX];
} LoadJob;

static int loader_thread(void* data) {
    LoadJob* job = (LoadJob*)data;
    unsigned int frames = 0;
    s16* pcm = (s16*)pc_music_decode_stereo(job->path, MUSIC_RATE, &frames);
    char loop_path[MUSIC_PATH_MAX + 8];
    unsigned int loop_start = 0;
    FILE* lf;

    snprintf(loop_path, sizeof(loop_path), "%s.loop", job->path);
    lf = fopen(loop_path, "r");
    if (lf != NULL) {
        if (fscanf(lf, "%u", &loop_start) != 1) loop_start = 0;
        fclose(lf);
    }

    music_lock();
    MusicTrack* t = &s_tracks[job->index];
    /* Loading entries are never evicted, so this is still our entry. */
    if (pcm != NULL) {
        t->pcm = pcm;
        t->frames = frames;
        t->loop_start = loop_start < frames ? loop_start : 0;
        t->state = TRACK_READY;
        printf("[MUSIC] Loaded %s (%.1fs)\n", t->path, frames / (f32)MUSIC_RATE);
    } else {
        t->state = TRACK_FAILED;
        printf("[MUSIC] Could not decode %s\n", t->path);
    }
    music_unlock();

    free(job);
    return 0;
}

static int track_get(const char* path) {
    int i;
    int victim = -1;

    for (i = 0; i < TRACK_CACHE_NUM; i++) {
        if (s_tracks[i].state != TRACK_EMPTY && strcmp(s_tracks[i].path, path) == 0) {
            s_tracks[i].last_used = ++s_use_clock;
            return i;
        }
    }

    /* Evict the least recently used entry nobody is playing or loading. */
    for (i = 0; i < TRACK_CACHE_NUM; i++) {
        MusicTrack* t = &s_tracks[i];
        if (t->refs > 0 || t->state == TRACK_LOADING) continue;
        if (victim < 0 || t->state == TRACK_EMPTY ||
            (s_tracks[victim].state != TRACK_EMPTY && t->last_used < s_tracks[victim].last_used)) {
            victim = i;
        }
    }
    if (victim < 0) return -1;

    MusicTrack* t = &s_tracks[victim];
    free(t->pcm);
    memset(t, 0, sizeof(*t));
    snprintf(t->path, sizeof(t->path), "%s", path);
    t->state = TRACK_LOADING;
    t->last_used = ++s_use_clock;

    LoadJob* job = (LoadJob*)malloc(sizeof(LoadJob));
    SDL_Thread* th = NULL;
    if (job != NULL) {
        job->index = victim;
        snprintf(job->path, sizeof(job->path), "%s", path);
        th = SDL_CreateThread(loader_thread, "MusicLoader", job);
    }
    if (th == NULL) {
        free(job);
        t->state = TRACK_FAILED;
        return victim;
    }
    SDL_DetachThread(th);
    return victim;
}

static void slot_clear(int g) {
    MusicSlot* s = &s_slots[g];

    if (s->active && s->track >= 0) s_tracks[s->track].refs--;
    memset(s, 0, sizeof(*s));
    s->track = -1;
    s_silenced[g] = FALSE;
}

static void update_any_active(void) {
    int g;
    int any = FALSE;

    for (g = 0; g < AUDIO_GROUP_MAX; g++) {
        if (s_slots[g].active) any = TRUE;
    }
    SDL_AtomicSet(&s_any_active, any);
}

/* --- game thread --- */

static int is_bgm_group(int g) {
    return g == NA_GROUP1 || g == NA_GROUP3; /* game64.c_inc's BGM0_GROUP / BGM1_GROUP */
}

/* Point a BGM group's slot at the scene's track (lock held) */
static void scene_attach(int g, int seq_id, int seen) {
    int idx = track_get(s_scene_path);
    MusicSlot* s;

    slot_clear(g);
    if (idx < 0) return;
    s = &s_slots[g];
    s->active = TRUE;
    s->track = idx;
    s->seq_id = seq_id;
    s->seen = seen;
    s->grace = START_GRACE_FRAMES;
    s->scene = TRUE;
    s_tracks[idx].refs++;
    s_silenced[g] = TRUE;
    printf("[MUSIC] Group %d (sequence %d) plays %s\n", g, seq_id, s_scene_path);
}

void pc_music_set_scene_track(const char* path) {
    int g;

    if (path == NULL) path = "";
    if (path[0] != '\0' && !file_exists(path)) path = "";
    if (strcmp(path, s_scene_path) == 0) return;

    music_lock();
    for (g = 0; g < AUDIO_GROUP_MAX; g++) {
        /* back to the game's own music, which kept running silenced underneath */
        if (s_slots[g].active && s_slots[g].scene) slot_clear(g);
    }
    snprintf(s_scene_path, sizeof(s_scene_path), "%s", path);
    if (s_scene_path[0] != '\0') {
        printf("[MUSIC] Scene track: %s\n", s_scene_path);
        for (g = 0; g < AUDIO_GROUP_MAX; g++) {
            const group* grp = &AG.groups[g];

            /* music already playing as the scene starts (a room change that keeps it going) */
            if (is_bgm_group(g) && grp->flags.enabled && grp->applied_fade_volume > 0.0f) {
                scene_attach(g, grp->seq_id, TRUE);
            }
        }
    }
    update_any_active();
    music_unlock();
}

void pc_music_seq_started(int group_id, int bgm_num, int tenki, int seq_id) {
    char path[MUSIC_PATH_MAX];

    if (group_id < 0 || group_id >= AUDIO_GROUP_MAX) return;

    music_lock();
    slot_clear(group_id);

    if (s_scene_path[0] != '\0' && is_bgm_group(group_id)) {
        scene_attach(group_id, seq_id, FALSE);
        update_any_active();
        music_unlock();
        return;
    }

    if (g_pc_settings.music_pack[0] != '\0' && bgm_num >= 1 && bgm_num <= MUSIC_HOURS) {
        int hour = bgm_num - 1;

        if (resolve_track(path, hour, tenki)) {
            int idx = track_get(path);

            if (idx >= 0) {
                MusicSlot* s = &s_slots[group_id];
                s->active = TRUE;
                s->track = idx;
                s->seq_id = seq_id;
                s->grace = START_GRACE_FRAMES;
                s_tracks[idx].refs++;
                s_silenced[group_id] = TRUE;
                printf("[MUSIC] Hour %02d (weather %d): %s\n", hour, tenki, path);

                /* Decode the next hour ahead of time so the change is seamless. */
                if (resolve_track(path, (hour + 1) % MUSIC_HOURS, 0)) track_get(path);
            }
        }
    }

    update_any_active();
    music_unlock();
}

/* --- audio thread --- */

int pc_music_group_silenced(const struct group_* grp) {
    int g = (int)(grp - AG.groups);
    return g >= 0 && g < AUDIO_GROUP_MAX && s_silenced[g];
}

static f32 group_gain(const group* grp) {
    /* Notes scale by (fade volume)^2, see __Nas_CallWaveProcess_Sub. */
    f32 v = grp->applied_fade_volume;
    f32 gain = v * v;

    if (grp->flags.muted) {
        if (grp->mute_flags & AUDIO_MUTE_FLAG_STOP_SAMPLES) {
            gain = 0.0f;
        } else if (grp->mute_flags & AUDIO_MUTE_FLAG_SOFTEN) {
            gain *= grp->mute_volume_scale;
        }
    }
    return gain * (g_pc_settings.music_volume / 100.0f);
}

void pc_music_mix(short* buf, unsigned int n_samples) {
    u32 frames = n_samples / 2;
    int g;
    int changed = FALSE;

    if (!SDL_AtomicGet(&s_any_active) || frames == 0) return;

    music_lock();
    for (g = 0; g < AUDIO_GROUP_MAX; g++) {
        MusicSlot* s = &s_slots[g];
        const group* grp = &AG.groups[g];
        int running;

        if (!s->active) continue;

        running = grp->flags.enabled && grp->seq_id == s->seq_id;
        if (!s->seen) {
            if (running) {
                s->seen = TRUE;
            } else {
                s->grace -= (int)frames;
                if (s->grace <= 0) {
                    slot_clear(g);
                    changed = TRUE;
                }
                continue;
            }
        } else if (!running) {
            /* Faded out, stopped, or something else took the group. */
            slot_clear(g);
            changed = TRUE;
            continue;
        }

        MusicTrack* t = &s_tracks[s->track];
        if (t->state == TRACK_FAILED) {
            /* Let the original sequence be heard after all. */
            slot_clear(g);
            changed = TRUE;
            continue;
        }
        if (t->state != TRACK_READY) continue;

        f32 target = group_gain(grp);
        f32 gain = s->gain;
        f32 step = (target - gain) / (f32)frames;
        u32 pos = s->pos;
        u32 i;

        for (i = 0; i < frames; i++) {
            int l = buf[i * 2 + 0] + (int)(t->pcm[pos * 2 + 0] * gain);
            int r = buf[i * 2 + 1] + (int)(t->pcm[pos * 2 + 1] * gain);

            if (l > 32767) l = 32767;
            if (l < -32768) l = -32768;
            if (r > 32767) r = 32767;
            if (r < -32768) r = -32768;
            buf[i * 2 + 0] = (s16)l;
            buf[i * 2 + 1] = (s16)r;

            gain += step;
            if (++pos >= t->frames) pos = t->loop_start;
        }
        s->pos = pos;
        s->gain = target;
    }
    if (changed) update_any_active();
    music_unlock();
}
