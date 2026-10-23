#ifndef PC_PATTERN_QR_H
#define PC_PATTERN_QR_H

/* Pattern QR codes in Animal Crossing: New Leaf's format (pc_pattern_qr.c).
 *
 * The pause menu's "Pattern QR Codes" page shows a QR code for each of the
 * player's patterns (scan it with New Leaf, the NookLink app for New Horizons or
 * a pattern website), saves them all as PNGs in qr/export/, and reads QR images
 * dropped into qr/import/ to load them into a slot. Colours are converted to the
 * closest match each way; the GameCube has 16 fixed palettes, New Leaf 159
 * colours. Only the normal 32x32 design type is used (pro designs need 64x64). */

#include "types.h"
#include "m_needlework.h"

#ifdef __cplusplus
extern "C" {
#endif

struct game_s;

/* The 620-byte New Leaf design. */
#define PC_QR_NL_SIZE 0x26C

/* GameCube design <-> New Leaf design bytes. creator/town are game strings (8 bytes). */
void pc_qr_design_to_nl(const mNW_original_design_c* design, const u8* creator, const u8* town, u8* nl);
int pc_qr_nl_to_design(const u8* nl, mNW_original_design_c* design);

/* Pause menu page (pc_pause_menu.c). */
int pc_qr_menu_available(void);
void pc_qr_menu_enter(void);
int pc_qr_menu_active(void);
/* act: 1 up, 2 down, 3 left, 4 right, 5 confirm, 6 cancel (pc_pause_menu's MenuAction). */
void pc_qr_menu_action(int act);
void pc_qr_menu_draw(struct game_s* game);

#ifdef __cplusplus
}
#endif

#endif /* PC_PATTERN_QR_H */
