/* pc_fossil_id.c - Blathers identifies fossils himself, as Animal Crossing Deluxe has him do (ac_npc_curator).
 *
 * Once 15 fossils are in the museum he says, once to each player, that he has his certification. From then on his
 * menu has "Identify fossils": the player marks one or more fossils in their pockets and he hands them back
 * identified. Any unidentified fossil offered as a donation is identified the same way.
 *
 * Here: his lines (our own, in his manner), the menu choice, the four-choice menu, and the per-player "told" flags,
 * saved as FOSL in the companion save. */

#include "pc_fossil_id.h"
#include "pc_msg_text.h"
#include "m_common_data.h"
#include "m_font.h"
#include "m_msg.h"
#include "m_private.h"

#include <stdio.h>
#include <string.h>

static u8 l_told[PLAYER_NUM]; /* the certification news has been given to this player */
static int l_menu4;           /* this talk's menu (0x2F50) gets Identify fossils */
static int l_count = 1;       /* fossils being identified: one, or several */

/* Lines as pc_msg_text reads them: {xx} is a byte in hex, | a new line */
static const char* const l_told_msg =
    "{7f}{09}{00}{00}{15}Hoo! Hoo! {7f}{03}{08}{7f}{1a}, you're just|in time for some splendid news!{7f}{04}|{7f}{02}"
    "{7f}{09}{00}{00}{ff}With so many fossils donated,{7f}{03}{06}|the Farway Museum has at last|granted me my"
    " certification!{7f}{04}|{7f}{02}"
    "{7f}{09}{00}{00}{16}I may now examine fossils|myself, right here!{7f}{03}{08} No more|waiting on the post,"
    " wot!{7f}{04}|{7f}{02}"
    "{7f}{09}{00}{00}{15}Do bring me any mysterious|stones you dig up. {7f}{03}{06}Hoo!|{7f}{00}";

static const char* const l_which_msg = "{7f}{09}{00}{00}{06}Hoo! {7f}{03}{06}How exciting!{7f}{03}{10}|"
                                       "{7f}{09}{00}{00}{15}Which fossils shall I put|under my loupe?{7f}U|{7f}{01}";

/* ends: Blathers hands it back (demo order 10, as for the GameCube's "unexamined fossil"), then "any more?" */
#define FOSSIL_ID_HAND_BACK "{7f}{09}{01}{00}{0a}|{7f}{0e}{2f}{9b}{7f}s|{7f}{01}"

static const char* const l_one_msg =
    "{7f}r{7f}{09}{00}{00}{04}Hoo! {7f}{03}{06}A fossil! {7f}{03}{06}Let me see...{7f}{03}{10}|"
    "{7f}{09}{00}{00}{0f}Hmm{7f}{03}{04}.{7f}{03}{04}.{7f}{03}{04}. {7f}{03}{06}Yes{7f}{03}{04}.{7f}{03}{04}.{7f}{03}{04}."
    "|{7f}{09}{00}{00}{0c}This is {7f}1!{7f}{04}|{7f}{02}"
    "{7f}{09}{00}{00}{16}A fine specimen, {7f}{03}{04}and a|stubborn one! {7f}{03}{06}It didn't care to|be"
    " identified one bit, wot!{7f}{04}|{7f}{02}"
    "{7f}{09}{00}{00}{15}There you are. {7f}{03}{06}It's yours|to keep, {7f}{03}{04}or to donate. Hoo!" FOSSIL_ID_HAND_BACK;

static const char* const l_many_msg =
    "{7f}r{7f}{09}{00}{00}{04}Hoo! {7f}{03}{06}A whole pocketful|of fossils!{7f}{03}{10}|"
    "{7f}{09}{00}{00}{0f}Let me see{7f}{03}{04}.{7f}{03}{04}.{7f}{03}{04}.{7f}{04}|{7f}{02}"
    "{7f}{09}{00}{00}{0c}Hoo! {7f}{03}{06}Identified, every last|one of them!{7f}{03}{08} Do have a look|in your"
    " pockets.{7f}{04}|{7f}{02}"
    "{7f}{09}{00}{00}{15}My, but that was thirsty|work, wot! {7f}{03}{06}Here you are." FOSSIL_ID_HAND_BACK;

/* "any more?": Yes. / Nope!, as the GameCube's "anything else?" (0x2F66) */
static const char* const l_more_msg =
    "{7f}{09}{00}{00}{ff}Now, {7f}{03}{06}have you any more|fossils{7f}{09}{00}{00}{0b} for me to examine?"
    "{7f}{16}{00}{03}{00}Q{7f}^{7f}{04}{7f}{0d}{7f}{0f}{2f}{9e}{7f}{10}{2f}a|{7f}{19}|{7f}{01}";

int pc_fossil_id_msg(int index, u8* buf, int buf_size, int* len) {
    const char* t;

    switch (index) {
        case PC_FOSSIL_ID_MSG_TOLD:
            t = l_told_msg;
            break;
        case PC_FOSSIL_ID_MSG_WHICH:
            t = l_which_msg;
            break;
        case PC_FOSSIL_ID_MSG_DONE:
            t = l_count > 1 ? l_many_msg : l_one_msg;
            break;
        case PC_FOSSIL_ID_MSG_MORE:
            t = l_more_msg;
            break;
        default:
            return FALSE;
    }
    *len = pc_msg_text(t, buf, buf_size);
    return TRUE;
}

int pc_fossil_id_choice(int str_no, u8* str, int len) {
    const char* name = "Identify fossils";
    int i;

    if (str_no != PC_FOSSIL_ID_CHOICE) {
        return FALSE;
    }
    for (i = 0; i < len; i++) {
        str[i] = *name != '\0' ? (u8)*name++ : CHAR_SPACE;
    }
    return TRUE;
}

/* His menu (0x2F50): Donate an item / What's this? / Nope, nothing! becomes four, with Identify fossils second.
 * Called on every message the game loads; returns the new size */
int pc_fossil_id_menu(u8* buf, int size, int buf_size) {
    static const u8 choices[] = { CHAR_CONTROL_CODE, mFont_CONT_CODE_SET_SELECT_STRING_3, 0x01, 0xDC, 0x00, 0x31,
                                  0x02, 0x0D };
    int i;
    int next;

    if (!l_menu4 || size + 6 > buf_size) {
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
    /* the choice, after Donate */
    memmove(buf + i + 6, buf + i + 4, size - (i + 4));
    size += 2;
    buf[i + 1] = mFont_CONT_CODE_SET_SELECT_STRING_4;
    buf[i + 4] = PC_FOSSIL_ID_CHOICE >> 8;
    buf[i + 5] = PC_FOSSIL_ID_CHOICE & 0xFF;

    /* next messages 0 (donate), 1 (what's this) and 2 (nope) become 0, 2 and 3; 1 is "which fossils?" */
    for (next = i + sizeof(choices) + 2; next + 3 < size; next++) {
        if (buf[next] == CHAR_CONTROL_CODE && buf[next + 1] == mFont_CONT_CODE_SET_NEXT_MESSAGE_0) {
            break;
        }
    }
    if (next + 12 > size || buf[next + 4] != CHAR_CONTROL_CODE ||
        buf[next + 5] != mFont_CONT_CODE_SET_NEXT_MESSAGE_1 || buf[next + 8] != CHAR_CONTROL_CODE ||
        buf[next + 9] != mFont_CONT_CODE_SET_NEXT_MESSAGE_2) {
        return size; /* not the layout seen: the menu works, the curator still sets what comes next */
    }
    memmove(buf + next + 16, buf + next + 12, size - (next + 12));
    size += 4;
    buf[next + 12] = CHAR_CONTROL_CODE;
    buf[next + 13] = mFont_CONT_CODE_SET_NEXT_MESSAGE_3;
    buf[next + 14] = buf[next + 10];
    buf[next + 15] = buf[next + 11];
    buf[next + 9] = mFont_CONT_CODE_SET_NEXT_MESSAGE_2;
    buf[next + 10] = buf[next + 6];
    buf[next + 11] = buf[next + 7];
    buf[next + 6] = PC_FOSSIL_ID_MSG_WHICH >> 8;
    buf[next + 7] = PC_FOSSIL_ID_MSG_WHICH & 0xFF;
    return size;
}

void pc_fossil_id_set_menu(int on) {
    l_menu4 = on;
}

void pc_fossil_id_set_count(int count) {
    l_count = count;
}

static int now_player(void) {
    int i;

    for (i = 0; i < PLAYER_NUM; i++) {
        if (Common_Get(now_private) == Save_GetPointer(private_data[i])) {
            return i;
        }
    }
    return -1;
}

int pc_fossil_id_told(void) {
    int p = now_player();
    return p < 0 || l_told[p]; /* a visitor from another town isn't told here */
}

void pc_fossil_id_set_told(void) {
    int p = now_player();
    if (p >= 0) {
        l_told[p] = TRUE;
    }
}

void pc_fossil_id_forget_player(int player) {
    if (player >= 0 && player < PLAYER_NUM) {
        l_told[player] = FALSE;
    }
}

void pc_fossil_id_reset(void) {
    memset(l_told, 0, sizeof(l_told));
}

int pc_fossil_id_read_chunk(const char tag[4], void* fp, unsigned int size) {
    FILE* f = (FILE*)fp;

    if (memcmp(tag, "FOSL", 4) != 0) {
        return FALSE;
    }
    if (size != sizeof(l_told) || fread(l_told, 1, size, f) != size) {
        pc_fossil_id_reset();
        if (size != sizeof(l_told)) {
            fseek(f, (long)size, SEEK_CUR);
        }
    }
    return TRUE;
}

int pc_fossil_id_write_chunk(void* fp) {
    FILE* f = (FILE*)fp;
    u32 size = sizeof(l_told);

    return fwrite("FOSL", 1, 4, f) == 4 && fwrite(&size, 4, 1, f) == 1 && fwrite(l_told, 1, size, f) == size;
}
