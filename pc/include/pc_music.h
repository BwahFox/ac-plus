#ifndef PC_MUSIC_H
#define PC_MUSIC_H

/* Custom hourly music: replaces the 24 field BGM tracks with audio files.
 *
 * Files live in music/<pack>/ next to the executable, where <pack> is the
 * music_pack setting (empty = off):
 *   hourly/00..23.<ext>  normal hourly tracks (00 = midnight)
 *   rain/00..23.<ext>    rain variants, or rain/all.<ext> for every hour
 *   snow/00..23.<ext>    snow variants
 * <ext> is ogg, mp3, flac or wav. A missing file falls back down the chain
 * rain/snow -> hourly -> the game's own track, so partial packs work.
 *
 * The game's sequence keeps running with its notes silenced, and the file
 * follows that sequence's group volume. Hour-change crossfades, fade-outs on
 * entering buildings and volume ducking all keep working unchanged. */

#ifdef __cplusplus
extern "C" {
#endif

struct group_;

/* Game thread (game64.c_inc), right after a BGM sequence is queued to start
 * in group_id. bgm_num is the requested BGM before the weather swap, tenki the
 * weather (0 clear, 1 rain, 2 snow, 3 sakura, 4 leaves), seq_id the sequence
 * actually started. */
void pc_music_seq_started(int group_id, int bgm_num, int tenki, int seq_id);

/* Game thread, each frame: the scene's own music file (or NULL), played in place of whatever
 * background music runs while it's set, following that music's volume and fades. Playback
 * loops back to the frame in <path>.loop if there is one. The museum's café sets
 * cf_sky/music/roost.wav (pc_museum_rooms.c). Works without a music pack. */
void pc_music_set_scene_track(const char* path);

/* Audio thread (channel.c): TRUE if this group's notes must be silent. */
int pc_music_group_silenced(const struct group_* grp);

/* Audio thread (pc_audio.c): add music into n_samples interleaved stereo s16. */
void pc_music_mix(short* buf, unsigned int n_samples);

#ifdef __cplusplus
}
#endif

#endif /* PC_MUSIC_H */
