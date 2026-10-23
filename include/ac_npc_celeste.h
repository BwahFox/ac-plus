#ifndef AC_NPC_CELESTE_H
#define AC_NPC_CELESTE_H

/* ac-plus: Celeste, who minds the telescope in the museum's entrance hall
 * (src/actor/npc/ac_npc_celeste.c, pc/src/pc_celeste.c). PC only. */

#include "types.h"
#include "m_actor.h"
#include "ac_npc.h"
#include "pc_constellation.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct npc_celeste_actor_s NPC_CELESTE_ACTOR;

typedef void (*aCEL_TALK_PROC)(NPC_CELESTE_ACTOR*, GAME_PLAY*);

struct npc_celeste_actor_s {
    NPC_ACTOR npc_class;
    aCEL_TALK_PROC talk_proc;
    int wait_msg;      /* the line whose end decides what's next */
    int after_close;   /* what to open once the window has closed */
    int purpose;       /* why the list or telescope was opened */
    int slot;          /* the constellation being changed or erased, -1 = a new one */
    pc_constellation_c work; /* the one being made or changed */
    u8 name[16];       /* the name being typed */
};

extern ACTOR_PROFILE Npc_Celeste_Profile;

#ifdef __cplusplus
}
#endif

#endif /* AC_NPC_CELESTE_H */
