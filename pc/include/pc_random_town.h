#ifndef PC_RANDOM_TOWN_H
#define PC_RANDOM_TOWN_H

/* A freshly made town to visit by train (experimental, pc_random_town.c) */

#include "types.h"
#include "m_common_data.h"
#include "game.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Where the town goes for the trip: it's visited as a town on another memory card (Card B), and written back there
 * when the player leaves. The next trip makes a new one over it. */
#define PC_RANDOM_TOWN_PATH "save/random_town.gci"

/* A new town's save (malloc'd; the game's own data is left as it was), or NULL */
Save_t* pc_random_town_make(GAME* game);

/* Porter offers "Somewhere new" only once it's unlocked: the player's house paid off to the end (its statue up) */
int pc_random_town_unlocked(void);

#ifdef __cplusplus
}
#endif

#endif /* PC_RANDOM_TOWN_H */
