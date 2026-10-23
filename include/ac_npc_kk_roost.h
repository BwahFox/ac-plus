#ifndef AC_NPC_KK_ROOST_H
#define AC_NPC_KK_ROOST_H

/* ac-plus: K.K. Slider playing at the Roost (src/actor/npc/ac_npc_kk_roost.c). PC only. */

#include "types.h"
#include "m_actor.h"
#include "ac_npc.h"
#include "m_lights.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct npc_kk_roost_actor_s NPC_KK_ROOST_ACTOR;

struct npc_kk_roost_actor_s {
    NPC_ACTOR npc_class;
    int song;      /* the one playing, or -1 between songs */
    int last_song; /* never the same twice in a row */
    int started;   /* the song has been heard (its staff roll info is its own) */
    f32 rest;      /* 60ths of a second until the next song */
    f32 floor_y;   /* the stage floor, where his crate stands */
    Lights spot;   /* a light on him while the room's are down */
    Light_list* spot_list;
};

extern ACTOR_PROFILE Npc_Kk_Roost_Profile;

/* 6 PM to midnight, except on Saturdays (when he plays in town as on the GameCube) */
extern int aKKR_on_stage_hours(void);
/* A song is playing: the Roost's own music stays off (pc_museum_rooms.c) */
extern int aKKR_playing;
/* His show: 0 (lights up) to 1 (the room dim, a light on him); where he sits, for the room's spot */
extern f32 aKKR_show;
/* The Roost's light for what the GameCube's lights don't reach (the City Folk room, Brewster): 1, or down for the
 * show */
extern f32 aKKR_room_light(void);
#define aKKR_STAGE_X 165.0f
#define aKKR_STAGE_Z 255.0f

#ifdef __cplusplus
}
#endif

#endif /* AC_NPC_KK_ROOST_H */
