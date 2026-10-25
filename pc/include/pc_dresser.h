#ifndef PC_DRESSER_H
#define PC_DRESSER_H

/* ac-plus: every dresser, wardrobe and closet in a resident's house has its own storage (pc_storage.c shows
 * it). Up to PC_DRESSER_MAX storages per town, each PC_EXT_STORAGE_COUNT slots, kept in the companion save.
 *
 * A piece of storage furniture carries a storage ID (1 to PC_DRESSER_MAX, 0 = none yet). In a room the
 * furniture actor holds it (FTR_ACTOR.pc_dresser_id); the save keeps it by house, floor, layer and the
 * furniture's unit. Picked up, it rides along with the pocket slot it went into (and the menu's hand while
 * the item is being moved), and goes back on the furniture when that is put down again.
 *
 * Storage furniture with no ID gets one the first time it is opened: a storage nothing else holds (the one
 * with the most in it: say a dresser that was sold, or the shared storage of older versions), else a new
 * empty one. Nothing is lost when an ID goes astray; its contents turn up in the next new dresser. */

#include "types.h"
#include "pc_ext_save.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PC_DRESSER_MAX 128
#define PC_DRESSER_HOUSES 5 /* the four houses and the island cottage */

void pc_dresser_reset(void);
int pc_dresser_read_chunk(const char tag[4], void* f, unsigned int size);
int pc_dresser_write_chunks(void* f);
/* After a load: storage from before (one shared storage per resident) becomes ownerless storage */
void pc_dresser_loaded(PCExtStoredItem* legacy, int count);

/* The items of storage `id` (1-based), NULL if it isn't in use */
PCExtStoredItem* pc_dresser_items(int id);
/* An ID for storage furniture that has none (see above); 0 when all PC_DRESSER_MAX are taken */
int pc_dresser_new_id(void);

/* Rooms: house 0-3 (homes[]) or 4 (the cottage), floor 0-2 (mFI_GetPlayerHouseFloorNo) */
int pc_dresser_bound(int house, int floor, int layer, int ut);
void pc_dresser_room_clear(int house, int floor);
void pc_dresser_room_bind(int house, int floor, int layer, int ut, int id);

/* Pockets (of the player playing): furniture with `id` was just picked up into `slot` */
void pc_dresser_tag_pocket(int slot, mActor_name_t item, int id);
/* The ID that went with the item in `slot`, taken off it; 0 for none */
int pc_dresser_take_pocket(int slot, mActor_name_t item);
/* Picking furniture up: the room hands over its ID, then the pocket it landed in takes it */
void pc_dresser_set_pickup(int id);
void pc_dresser_pickup_into(int slot);
/* Putting furniture down from a pocket: its ID waits here until the furniture appears */
void pc_dresser_set_pending(int id);
int pc_dresser_take_pending(void);
/* The menu's hand: picking an item up from a pocket, swapping with one, putting it back */
void pc_dresser_hand_catch(int slot);
void pc_dresser_hand_swap(int slot);
void pc_dresser_hand_put(int slot);

/* Dev: PC_DRESSER_LOG */
void pc_dresser_dev_dump(void);

#ifdef __cplusplus
}
#endif

#endif /* PC_DRESSER_H */
