#ifndef PC_MP_WORLD_H
#define PC_MP_WORLD_H

/* Keeping the town the same in every game during a visit (pc_mp_world.c) */

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

enum { MP_WORLD_SET = 1, MP_WORLD_DEPOSIT = 2, MP_WORLD_SHOP = 3, MP_WORLD_MUSEUM = 4, MP_WORLD_ROOM = 5, MP_WORLD_SWITCH = 6, MP_WORLD_OFTR0 = 7 }; /* (and 8, 9: layers 1, 2) */ /* = MP_ACT_* (mp_wire.h) */

/* SET: a = ux, b = uz (town units); DEPOSIT: a = acre 0-29, b = row 0-15 */
typedef void (*pc_mp_world_change_fn)(void* user, int kind, int a, int b, u16 old_value, u16 new_value);

/* take the town as it is now as the starting point */
void pc_mp_world_reset(void);
/* report what changed since the last call */
void pc_mp_world_scan(pc_mp_world_change_fn fn, void* user);
/* what is there (the real item, not an on-screen placeholder); FALSE if a, b are outside the town */
int pc_mp_world_get(int kind, int a, int b, u16* value);
/* put a change from the network into the town; FALSE if it isn't allowed there */
int pc_mp_world_apply(int kind, int a, int b, u16 value);
/* may a visitor without the host's trust make this change? */
int pc_mp_world_visitor_may(int kind, int a, int b, u16 old_value, u16 new_value);
int pc_mp_world_trusted_may(int kind, int a, int b, u16 old_value, u16 new_value);
void pc_mp_world_undo_pickup(u16 item);
void pc_mp_world_undo_purchase(u16 item);

#ifdef __cplusplus
}
#endif

#endif
