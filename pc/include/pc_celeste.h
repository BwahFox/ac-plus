#ifndef PC_CELESTE_H
#define PC_CELESTE_H

/* Celeste's lines and helpers (pc_celeste.c); her actor is src/actor/npc/ac_npc_celeste.c.
 *
 * Her lines are City Folk's, converted by pc/tools/extract_cf_sky.py from the player's
 * own disc into cf_sky/celeste.msg; she only comes to the museum when that file is there.
 * Her lines go through pc_cf_lines.c, as speaker PC_CF_CELESTE. */

#include "types.h"
#include "pc_constellation.h"

#ifdef __cplusplus
extern "C" {
#endif

#include "pc_cf_lines.h"

#define PC_CELESTE_MSG_BASE PC_CF_MSG_BASE
#define PC_CELESTE_MSG_LAST PC_CF_MSG_LAST
#define PC_CELESTE_CHOICE_BASE PC_CF_CHOICE_BASE

int pc_celeste_available(void);
void pc_celeste_set_talking(int on);

int pc_celeste_line_has_choices(int n);

/* Free strings for her lines, game characters, space padded to 16 */
void pc_celeste_name_str(const pc_constellation_c* c, u8 out[16]);
/* When a constellation is at its best, looking north: tonight's time (hour 0-23, minute),
 * and the date it peaks at 8 in the evening */
void pc_celeste_peak_tonight(const pc_constellation_c* c, int* hour, int* minute);
void pc_celeste_peak_date(const pc_constellation_c* c, int* month, int* day);
/* "8"/"05"/"PM", "October", "22nd" as game strings, space padded to 16 */
void pc_celeste_time_strs(int hour, int minute, u8 h[16], u8 m[16], u8 ampm[16]);
void pc_celeste_date_strs(int month, int day, u8 mon[16], u8 dstr[16]);

/* Her textures and palette (npc_draw_data.c's Celeste entry): cf_sky/celeste.tex, else Blathers' recoloured */
void pc_celeste_make_palette(void);

#ifdef __cplusplus
}
#endif

#endif /* PC_CELESTE_H */
