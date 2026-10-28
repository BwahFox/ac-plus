#ifndef PC_NOOK_PAINT_H
#define PC_NOOK_PAINT_H

/* "About my house" at Nook's: a new roof colour whenever asked (pc_nook_paint.c, ac_npc_shop_common.c) */

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PC_NOOK_PAINT_MSG 0x3E9D    /* his answer (a message number the GameCube leaves empty) */
#define PC_NOOK_PAINT_CHOICE 0x7E3  /* "About my house" (after Blathers' 0x7E2) */

enum {
    PC_NOOK_PAINT_LOAN,     /* still paying for the house */
    PC_NOOK_PAINT_FINISHED, /* nothing left to build */
    PC_NOOK_PAINT_PAID,     /* paid off, more to build */
    PC_NOOK_PAINT_NO_MONEY, /* not enough Bells on the player for it */
    PC_NOOK_PAINT_CASES
};

int pc_nook_paint_msg(int index, u8* buf, int buf_size, int* len);
int pc_nook_paint_choice(int str_no, u8* str, int len);
int pc_nook_paint_menu(u8* buf, int size, int buf_size);
void pc_nook_paint_set_menu(int on);
int pc_nook_paint_menu_on(void);
void pc_nook_paint_set_case(int which, u32 price);

#ifdef __cplusplus
}
#endif

#endif /* PC_NOOK_PAINT_H */
