#ifndef AC_NPC_BREWSTER_H
#define AC_NPC_BREWSTER_H

/* ac-plus: Brewster, who runs the Roost in the museum's basement
 * (src/actor/npc/ac_npc_brewster.c, pc/src/pc_brewster.c). PC only. */

#include "types.h"
#include "m_actor.h"
#include "ac_npc.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct npc_brewster_actor_s NPC_BREWSTER_ACTOR;

typedef void (*aBRW_TALK_PROC)(NPC_BREWSTER_ACTOR*, GAME_PLAY*);

struct npc_brewster_actor_s {
    NPC_ACTOR npc_class;
    aBRW_TALK_PROC talk_proc;
    int wait_msg;    /* the line whose end decides what's next */
    int taste;       /* the player's line is showing: no name plate */
    int seated_talk; /* this talk began with the player on a stool */
    int greeted;     /* he has spoken to the player sitting there */
    u32 sit_frame;   /* the A press that sat the player down (not a talk) */
    int cup;         /* aBRW_CUP_*: what's on the counter */
    int cup_timer;   /* frames into drinking it */
    int stool;       /* the stool the cup is by */
    /* City Folk's animations (pc_cf_anim.h), when they were extracted */
    const char* body_anim;  /* what he's doing, with whatever he holds */
    f32 body_frame;         /* City Folk frames (60 a second) */
    f32 body_speed;         /* frames a 60th of a second; negative plays it backwards */
    int body_loop;
    const char* queue[3];   /* what he does next, in turn, while the window's away */
    int queue_num;
    const char* cup_anim;   /* the cup and saucer, when not in his animation: on the counter, or drunk from */
    f32 cup_frame;
    int player_anim;        /* the player's City Folk animation being waited for (PC_CF_PANIM_*), or -1 */
    int after_msg;          /* the line once all that's done, or -1 */
    int after_last;         /* ...and it ends the talk */
    f32 seat[3][4];         /* where the player sits (pc_cf_anim_place), for the cup */
};

extern ACTOR_PROFILE Npc_Brewster_Profile;

#ifdef __cplusplus
}
#endif

#endif /* AC_NPC_BREWSTER_H */
