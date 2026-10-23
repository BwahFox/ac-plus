#ifndef AC_ROOST_GUEST_H
#define AC_ROOST_GUEST_H

/* ac-plus: villagers having a cup at the Roost (src/actor/npc/ac_roost_guest.c). PC only. */

#include "types.h"
#include "m_actor.h"
#include "ac_npc.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct roost_guest_actor_s ROOST_GUEST_ACTOR;

typedef void (*aRGT_ACT_PROC)(ROOST_GUEST_ACTOR* actor);

struct roost_guest_actor_s {
    NPC_ACTOR npc_class;
    int action;
    aRGT_ACT_PROC act_proc;
    int seat;
    f32 seat_y; /* their height in the chair */
    int until; /* minute of the day they leave */
    int songs_seen; /* K.K.'s songs over so far (aKKR_songs_done): one more, and they clap */
    u8 clap_se_no;
};

extern ACTOR_PROFILE Roost_Guest_Profile;

#define aRGT_GUEST_NUM 2

/* Today's guests in at this hour (1 or 2 of the town's villagers visit, each for an hour or two between 9 AM and
 * midnight) and the chairs they take; returns how many (aRGT_guest() says who) */
extern int aRGT_regist_guests(void);
/* Guest n: the villager and their Save_t animals[] index */
extern void aRGT_guest(int n, mActor_name_t* npc_id, int* animal_idx);
/* In the café, a villager in for a cup now: the NPC controller gives them this profile */
extern int aRGT_is_guest(mActor_name_t npc_id);
/* whether this villager is out at the Roost right now (wherever the player is): the town keeps them indoors */
extern int aRGT_at_roost_now(mActor_name_t npc_id);
/* The chair of guest n (0-3, see ac_roost_guest.c): its unit, for the scene's actor list */
extern void aRGT_seat_unit(int n, int* ut_x, int* ut_z);

#ifdef __cplusplus
}
#endif

#endif /* AC_ROOST_GUEST_H */
