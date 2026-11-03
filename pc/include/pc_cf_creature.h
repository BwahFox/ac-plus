#ifndef PC_CF_CREATURE_H
#define PC_CF_CREATURE_H

/* City Folk's fish and bugs the GameCube has none of (pc_cf_creature.c, cf_sky/creatures.cfcr from
 * pc/tools/cf_creatures.py and cf_creatures.tsv).
 *
 * Fish are types of their own, from aGYO_TYPE_PC_CF up. A bug is an actor of a GameCube type (it moves and is
 * caught as that one) that knows which of these it is (insect->pc_cf); make_insect takes either. */

#include "types.h"
#include "m_name_table.h"
#include "pc_cf_creature_table.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The types: past the GameCube's own (fish stop at 44, bugs at 44 with the spirits), and the items are START + type */
#define aGYO_TYPE_PC_CF 48
#define aINS_TYPE_PC_CF 48
#define PC_CF_FISH_ITEM (ITM_FISH_START + aGYO_TYPE_PC_CF)
#define PC_CF_BUG_ITEM (ITM_INSECT_START + aINS_TYPE_PC_CF)
#define PC_CF_ITEM_IS_FISH(n) ((n) >= PC_CF_FISH_ITEM && (n) < PC_CF_FISH_ITEM + PC_CF_FISH_NUM)
#define PC_CF_ITEM_IS_BUG(n) ((n) >= PC_CF_BUG_ITEM && (n) < PC_CF_BUG_ITEM + PC_CF_BUG_NUM)
/* Put in the house: City Folk furniture (pc_cf_ftr.c) from here, fish first, then bugs */
#define PC_CF_PLACED_ITEM 0x3C00
#define PC_CF_CREATURE_NUM (PC_CF_FISH_NUM + PC_CF_BUG_NUM)
#define PC_CF_ITEM_IS_PLACED(n) ((n) >= PC_CF_PLACED_ITEM && (n) < PC_CF_PLACED_ITEM + 4 * PC_CF_CREATURE_NUM)

void pc_cf_creature_load(void);
int pc_cf_creature_item(mActor_name_t item); /* one of their items, and the disc's pictures are there */
int pc_cf_creature_name(mActor_name_t item, u8* dst, int len);
u32 pc_cf_creature_price(mActor_name_t item);
/* Its picture, 128 x 128 RGB5A3, for the pocket icon: NULL if it isn't one of theirs */
u8* pc_cf_creature_icon(mActor_name_t item);

/* Fish: the three bends of the picture, as a GameCube fish's aGYO_DL_c. */
Gfx** pc_cf_fish_dl(int type);
int pc_cf_fish_in_sea(int type);
/* The i-th fish, if it's out now in the area (aSOG_SPAWN_AREA_*): its weight, 0 if not */
int pc_cf_fish_now(int i, int* area);

/* Bugs: */
int pc_cf_bug_base(int type); /* the GameCube type it is made as */
int pc_cf_bug_now(int i, int* area); /* as pc_cf_fish_now */
Gfx* pc_cf_bug_dl(int type, int draw_type); /* draw_type as the GameCube's: even a picture, odd nothing */
/* Its 3D model in town, at the matrix its flat picture would be drawn at */
struct game_s;
void pc_cf_bug_draw_model(int type, struct game_s* game);

/* What the resident playing has caught (the fish and bug pages; the town's ac-plus data: CRCL) */
void pc_cf_creature_set_caught(mActor_name_t item);
mActor_name_t pc_cf_creature_collected_nth(int fish, int n); /* the n-th's item if caught, else EMPTY_NO */

/* What's said on catching one, at the rod's or net's message msg_no; what Blathers says, at his */
void pc_cf_creature_say_caught(mActor_name_t item, int msg_no);
void pc_cf_creature_say_blathers(mActor_name_t item, int msg_no);
void pc_cf_creature_say_chip(mActor_name_t item, int msg_no);
int pc_cf_creature_msg(int index, u8* buf, int buf_size, int* len);

/* The furniture it is in the house, and back; the other way 0 if it isn't one */
mActor_name_t pc_cf_creature_placed(mActor_name_t item);
mActor_name_t pc_cf_creature_of_placed(mActor_name_t ftr_item);

/* The museum: who gave it (0 nobody, 1 + player), in the town's ac-plus data (pc_ext_save.c): CRMU */
int pc_cf_museum_donor(mActor_name_t item);
void pc_cf_museum_donate(mActor_name_t item, int player);
void pc_cf_museum_reset(void);
int pc_cf_museum_read_chunk(const char tag[4], void* fp, unsigned int size);
int pc_cf_museum_write_chunk(void* fp);
/* What's been given, drawn in the rooms (as ac_museum_fish.c's and ac_museum_insect.c's draws do theirs) */
struct game_s;
void pc_cf_museum_draw_fish(struct game_s* game, f32 scale);
void pc_cf_museum_draw_bugs(struct game_s* game, f32 scale);

#ifdef __cplusplus
}
#endif

#endif
