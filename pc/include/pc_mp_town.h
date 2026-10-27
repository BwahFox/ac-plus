#ifndef PC_MP_TOWN_H
#define PC_MP_TOWN_H

/* The host's town as sent to a visitor (pc_mp_town.c) */

#include "types.h"
#include "m_common_data.h"

#ifdef __cplusplus
extern "C" {
#endif

struct game_play_s;
/* host: write this town into out; returns the size, 0 if it doesn't fit. play: the scene running, or NULL */
int pc_mp_town_pack(struct game_play_s* play, u8* out, int cap);
/* visitor: build the town into s (a copy of the visitor's own save, used for what isn't sent); 0 if unusable */
int pc_mp_town_unpack(const u8* in, int len, Save_t* s);

/* visitor, after arriving: the town's ground patterns and outdoor furniture (read with the town) */
void pc_mp_town_apply_extras(void);
/* on an online visit: the design on house `home`'s door, as the host sent it (FALSE: none, the plain mark) */
int pc_mp_town_door(int home, u8* palette, u8** tex);

/* item checks (pc_mp_items.c): TRUE if the game can safely hold this id there */
int pc_mp_item_ok(mActor_name_t item);      /* an item a player could carry or a shop sell */
int pc_mp_fg_item_ok(mActor_name_t item);   /* anything that can stand on a unit of the town */
int pc_mp_fg_natural_ok(mActor_name_t item);
int pc_mp_shop_item_ok(mActor_name_t item); /* shop goods, or a sold mark */
int pc_mp_shop_sold(mActor_name_t item);
int pc_mp_museum_byte_ok(u8 b);
void pc_mp_museum_clean(u8* bytes, int n); /* what playing can put on or take off the ground (no buildings) */
int pc_mp_room_item_ok(mActor_name_t item); /* anything that can stand in a house room */

#ifdef __cplusplus
}
#endif

#endif
