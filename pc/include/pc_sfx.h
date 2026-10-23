#ifndef PC_SFX_H
#define PC_SFX_H

/* One-shot sound effects from files (pc_sfx.c), mixed over the game's own audio.
 *
 * cf_sky/sfx/<name>.wav, made by pc/tools/extract_cf_sky.py from the player's City Folk disc
 * (cf_music.py). A missing file plays nothing. */

#ifdef __cplusplus
extern "C" {
#endif

/* Game thread: play cf_sky/sfx/<name>.wav once, at volume 0-1 */
void pc_sfx_play(const char* name, float volume);

/* Audio thread (pc_audio.c): add playing effects into n_samples interleaved stereo s16 */
void pc_sfx_mix(short* buf, unsigned int n_samples);

#ifdef __cplusplus
}
#endif

#endif /* PC_SFX_H */
