/* pc_nook_paint.c - "About my house" in Tom Nook's "other things" menu: he repaints the roof whenever asked
 * (ac_npc_shop_common.c). His answer depends on the house: still a loan to pay, all upgrades done, or paid off with
 * more to come. Then his own roof colour pages (as for an upgrade) and his "done by tomorrow" line; the roof
 * changes overnight, as with the paint he sometimes sells, and costs what a can of it does. */

#include "pc_nook_paint.h"
#include "pc_msg_text.h"
#include "m_font.h"

#include <stdio.h>
#include <string.h>

static int l_menu5; /* this talk's "other things" menu gets About my house */
static int l_case;  /* PC_NOOK_PAINT_* : which answer */
static u32 l_price; /* what it costs: a can of his paint */

/* Ends every answer: his first page of roof colours, as when ordering an upgrade (0x1080): Red, Orange, Yellow,
 * Another color... (pages 0x1086-0x1089), each colour going on to "a fine color, done tomorrow" (0x1085) */
#define PC_NOOK_PAINT_PICK                                                                                        \
    "{7f}{04}|{7f}{02}{7f}{09}{00}{00}{ff}It'll be %s Bells,|the same as a can of my paint."                     \
    "{7f}{04}|{7f}{02}{7f}{09}{09}{00}{01}{7f}{09}{00}{00}{0b}What color would you like?"                         \
    "{7f}{18}{00}{a1}{00}{a2}{00}{a3}{00}{e0}{7f}{04}{7f}{0d}{7f}{0f}{10}{85}{7f}{10}{10}{85}{7f}{11}{10}{85}"    \
    "{7f}{12}{10}{86}{7f}{19}{7f}{09}{00}{00}{ff}|{7f}{01}"

static const char* const l_answer[PC_NOOK_PAINT_CASES] = {
    /* still paying for the house */
    "{7f}{09}{00}{00}{04}Hm?{7f}{03}{08} What about your house?{7f}{03}{08}|You still haven't paid it|off..."
    "{7f}{04}|{7f}{02}{7f}{09}{00}{00}{17}Ah,{7f}{03}{04} well,{7f}{03}{04} I won't help you|with it until you've"
    " paid|it off,{7f}{03}{06}{7f}{09}{00}{00}{15} but maybe we can|paint it, hm?" PC_NOOK_PAINT_PICK,
    /* nothing left to build */
    "{7f}{09}{00}{00}{ff}Ah,{7f}{03}{04} well,{7f}{03}{04} as I have said,|{7f}{1a},{7f}{03}{06} I cannot upgrade|"
    "your house any more than|I have.{7f}{04}|{7f}{02}{7f}{09}{00}{00}{15}However,{7f}{03}{06} I can offer you|a"
    " different color roof!" PC_NOOK_PAINT_PICK,
    /* paid off, more to build */
    "{7f}{09}{00}{00}{04}Hm?{7f}{03}{08} Your house?{7f}{03}{08}|It's all paid off,{7f}{03}{04} so just say|the word"
    " when you're ready|to expand it!{7f}{04}|{7f}{02}{7f}{09}{00}{00}{15}In the meantime,{7f}{03}{06} how about|a"
    " fresh coat of paint, hm?" PC_NOOK_PAINT_PICK,
    /* can't pay for it */
    "{7f}{09}{00}{00}{04}Hm?{7f}{03}{08} What about your house?{7f}{03}{08}|A fresh coat of paint,|perhaps?{7f}{04}|"
    "{7f}{02}{7f}{09}{00}{00}{17}That would be %s Bells,|but you don't seem to have|that much on you...{7f}{04}|"
    "{7f}{02}{7f}{09}{00}{00}{15}Come back when you do, hm?|{7f}{00}",
};

int pc_nook_paint_msg(int index, u8* buf, int buf_size, int* len) {
    if (index != PC_NOOK_PAINT_MSG || l_case < 0 || l_case >= PC_NOOK_PAINT_CASES) {
        return FALSE;
    }
    {
        char price[16];
        char text[1024];

        if (l_price >= 1000) {
            snprintf(price, sizeof(price), "%u,%03u", (unsigned)(l_price / 1000), (unsigned)(l_price % 1000));
        } else {
            snprintf(price, sizeof(price), "%u", (unsigned)l_price);
        }
        snprintf(text, sizeof(text), l_answer[l_case], price);
        *len = pc_msg_text(text, buf, buf_size);
    }
    return TRUE;
}

int pc_nook_paint_choice(int str_no, u8* str, int len) {
    const char* name = "About my house";
    int i;

    if (str_no != PC_NOOK_PAINT_CHOICE) {
        return FALSE;
    }
    for (i = 0; i < len; i++) {
        str[i] = *name != '\0' ? (u8)*name++ : CHAR_SPACE;
    }
    return TRUE;
}

/* "As far as other things go..." (0x3E07): Turnip prices? / Hear code / Say code / Umm, hang on! becomes five,
 * About my house before the last. Called on every message the game loads; returns the new size */
int pc_nook_paint_menu(u8* buf, int size, int buf_size) {
    static const u8 choices[] = { CHAR_CONTROL_CODE, mFont_CONT_CODE_SET_SELECT_STRING_4, 0x02, 0x4E, 0x02, 0x4C,
                                  0x02, 0x4D, 0x00, 0x3D };
    int i;

    if (!l_menu5 || size + 2 > buf_size) {
        return size;
    }
    for (i = 0; i + (int)sizeof(choices) <= size; i++) {
        if (memcmp(buf + i, choices, sizeof(choices)) == 0) {
            break;
        }
    }
    if (i + (int)sizeof(choices) > size) {
        return size;
    }
    memmove(buf + i + 10, buf + i + 8, size - (i + 8));
    size += 2;
    buf[i + 1] = mFont_CONT_CODE_SET_SELECT_STRING_5;
    buf[i + 8] = PC_NOOK_PAINT_CHOICE >> 8;
    buf[i + 9] = PC_NOOK_PAINT_CHOICE & 0xFF;
    return size;
}

void pc_nook_paint_set_menu(int on) {
    l_menu5 = on;
}

int pc_nook_paint_menu_on(void) {
    return l_menu5;
}

void pc_nook_paint_set_case(int which, u32 price) {
    l_case = which;
    l_price = price;
}
