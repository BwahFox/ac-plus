#ifndef PC_PATTERN_H
#define PC_PATTERN_H

/* Stable pattern IDs (pc_pattern.c).
 *
 * The game refers to a player's patterns by slot (my_org 0-7), so anything made
 * from a pattern changes when that slot's design is replaced or swapped out to
 * storage. Here every pattern in the 8 slots of each resident and in the 96
 * storage spaces carries an ID that follows it between slots and storage and
 * survives edits. A library keeps a copy of every pattern something still uses,
 * so things made from a pattern keep showing it after it leaves the slots.
 *
 * IDs, the library and what uses which ID live in the ac-plus companion save;
 * the .gci keeps its slot numbers, so without the companion file everything
 * falls back to the game's own slot behaviour. */

#include "types.h"
#include "m_needlework.h"

#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Companion save (pc_ext_save.c): drop everything, read one chunk (returns TRUE
 * if it was a pattern chunk), write all pattern chunks, and settle IDs after a load. */
void pc_pattern_reset(void);
int pc_pattern_read_chunk(const char* tag, FILE* f, u32 size);
int pc_pattern_write_chunks(FILE* f);
void pc_pattern_loaded(void);

/* Match the IDs to what the slots and storage hold now. Cheap; runs whenever a
 * menu closes, before anything is bound and before saving. */
void pc_pattern_reconcile(void);
/* m_play.c via pc_designer_tick: reconciles when a submenu closes. */
void pc_pattern_tick(int submenu_open);

/* The design editor or a rename changed this slot: it keeps its ID. */
void pc_pattern_note_edit(int player_no, int image);

/* ID of a resident's slot (0 if the player isn't a resident or the library is full). */
u32 pc_pattern_slot_id(int player_no, int image);
/* The library copy of a pattern, NULL if unknown. Valid until the next save. */
const mNW_original_design_c* pc_pattern_get(u32 id);
/* Online visits: add the host's pattern id with its design for the visit (FALSE if there or no room) */
int pc_pattern_visit_add(u32 id, const mNW_original_design_c* d);

/* Worn shirt: bind when a pattern is put on; returns the bound design while the
 * player still wears that slot's shirt, NULL to use the slot. */
void pc_pattern_bind_cloth(int player_no, int image);
const mNW_original_design_c* pc_pattern_worn(int player_no, int cloth_idx);

/* Design umbrella in hand, the same way. */
void pc_pattern_bind_umbrella(int player_no, int image);
const mNW_original_design_c* pc_pattern_umbrella(int player_no, int image);

/* Things in the town and houses made from a pattern, each keyed by where it is.
 * A binding only counts while the game's own data still names the same slot
 * (player_no -1 = any player), so anything changed behind its back falls back
 * to the slot. */
enum {
    PC_PAT_USE_DOOR = 1,     /* key: house index */
    PC_PAT_USE_WALL,         /* key: room field id */
    PC_PAT_USE_FLOOR,        /* key: room field id */
    PC_PAT_USE_MANNEQUIN,    /* key: house index (one per slot per house) */
    PC_PAT_USE_UMBRELLA_STAND,
    PC_PAT_USE_SIGN,         /* key: pc_pattern_sign_key */
};
void pc_pattern_bind_use(int kind, u32 key, int player_no, int image);
const mNW_original_design_c* pc_pattern_use(int kind, u32 key, int player_no, int image);
/* Key of a signboard from its block (1-based, as stored in the sign actor) and unit. */
#define pc_pattern_sign_key(bx, bz, ux, uz) \
    ((u32)(((bx) & 0xFF) << 24 | ((bz) & 0xFF) << 16 | ((ux) & 0xFF) << 8 | ((uz) & 0xFF)))

#ifdef __cplusplus
}
#endif

#endif /* PC_PATTERN_H */
