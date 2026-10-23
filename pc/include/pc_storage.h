#ifndef PC_STORAGE_H
#define PC_STORAGE_H

/* ac-plus item storage, City Folk style: a resident opening any dresser,
 * wardrobe or closet in their own house gets 6 pages of 15 slots shared by all
 * of them, shown in the gyroid's two-grid screen (pockets below, storage above).
 * The items live in the companion save (pc_ext_save.c). */

#include "types.h"
#include "m_home_h.h"
#include "m_submenu.h"
#include "game_h.h"
#include "m_play_h.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The owner opened storage furniture: TRUE if storage is open for business
 * (then open the inventory with mSM_IV_OPEN_HANIWA_ENTRUST). */
int pc_storage_begin(void);
/* Move what the GameCube kept inside a piece of furniture into storage. */
void pc_storage_take_furniture_items(mActor_name_t* items, int count);
/* Is the gyroid screen showing storage instead of a gyroid? */
int pc_storage_active(void);
/* The haniwa overlay closed. */
void pc_storage_end(void);

/* A gyroid slot of house `home`, or while storage is open, a slot of its page. */
Haniwa_Item_c* pc_storage_slot(int home, int idx);
int pc_storage_slot_count(void);
void pc_storage_turn_page(int dir);
/* Copy the shown page back into the companion save. */
void pc_storage_sync(void);
/* May this item go into storage? */
int pc_storage_accepts(mActor_name_t item);

void pc_storage_draw(Submenu* submenu, GAME* game, f32 pos_x, f32 pos_y);

/* Dev test harness: open storage without a dresser. */
void pc_item_storage_request(void);
void pc_item_storage_tick(GAME_PLAY* play);

#ifdef __cplusplus
}
#endif

#endif /* PC_STORAGE_H */
