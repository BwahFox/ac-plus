#ifndef PC_TV_H
#define PC_TV_H

/* City Folk's TV programs on the TVs in the houses (pc_tv.c).
 *
 * cf_sky/tv.cftv (pc/tools/cf_tv.py, made with the other City Folk files on the first start) holds City
 * Folk's programs, a few 32x32 pictures each and the cuts between them, and its week's schedule: news in
 * the morning, a hero show after school, a movie on weekend nights, static after the late show... A TV
 * that's switched on shows what's on now, the same on every TV, as if broadcast: where a program is comes
 * from the clock. The weather report's forecast panel shows the weather pc_weather.c has coming.
 *
 * The sound is City Folk's too: cf_sky/tv/<program>.wav (pc/tools/cf_tv_sound.py), each a whole cycle of
 * the program's sound rendered the way City Folk plays it, kept in step with the picture. It comes from
 * the switched-on TV nearest the player, louder up close and panned to its side, in place of the TVs'
 * own hum.
 *
 * Without the file, or with the City Folk TV setting off, TVs show their own static as before.
 *
 * dev: PC_TV_PROGRAM=n always shows program n (0-10 City Folk's, 11-21 the weather report for each
 * forecast in cf_tv.py's WEATHER_KINDS); PC_TV_SOUND_DUMP=file writes the TV sound mixed in, raw s16 stereo
 * at 32 kHz. */

#include "types.h"
#include "game.h"
#include "PR/mbi.h"
#include "m_lib.h"

#ifdef __cplusplus
extern "C" {
#endif

/* A display list drawing the program now on the screen quad src (the TV's own 4 vertices, kept for its
 * shape), or NULL to draw the TV's own screen. Call it from the TV's draw while it's switched on, with
 * its matrix loaded; pos is where it stands, for the sound. */
Gfx* pc_tv_screen(GAME* game, const Vtx* src, const xyz_t* pos);

/* City Folk's program sound plays from the TVs: theirs would be in the way */
int pc_tv_sound_on(void);

/* Audio thread (pc_audio.c): add the TV sound into n_samples interleaved stereo s16 */
void pc_tv_mix(short* buf, unsigned int n_samples);

#ifdef __cplusplus
}
#endif

#endif /* PC_TV_H */
