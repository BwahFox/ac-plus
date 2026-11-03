/* pc_season_island.c - season islands (experimental): besides the summer island every town has, Tom Nook sells a
 * spring, a fall and a winter island for 1,000,000 Bells each, out of the player's savings at the post office.
 * Each is an island of its own (its own shape, islander, cottage and items), made as a new town's island would be,
 * and Kapp'n's boat goes to whichever one was picked last at Nook's ("Other things" -> "Islands").
 *
 * The island being visited is the one in the GameCube save (Save_t.island); the others wait in the companion save
 * (chunk ISLS) and are swapped in when picked. On the island, the season is the island's (pc_season_island_term:
 * m_time.c, m_field_make.c), and the winter island's rain falls as snow.
 *
 * With more than one island, Kapp'n asks which before he sets sail (pc_season_island_kappn_*, ac_npc_sendo), and no
 * two islands share an islander (pc_isl_unique). Each new island is named on its first visit, as the summer island
 * is (pc_isl_unnamed). */

#include "pc_season_island.h"
#include "pc_msg_text.h"
#include "m_common_data.h"
#include "m_font.h"
#include "m_island.h"
#include "m_field_make.h"
#include "m_private.h"
#include "m_time.h"
#include "m_kankyo.h"
#include "m_msg.h"
#include "m_choice.h"
#include "m_npc.h"

#include <dolphin/os.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PC_ISL_PRICE 1000000
#define PC_ISL_VERSION 1

static const char* const l_name[PC_ISL_SEASONS] = { "summer", "spring", "fall", "winter" };
static const char* const l_choice[PC_ISL_SEASONS] = { "Summer island", "Spring island", "Fall island", "Winter island" };
/* m_time.c's terms: high summer (the GameCube island's), late spring, deep fall, midwinter */
static const int l_term[PC_ISL_SEASONS] = { mTM_TERM_7, mTM_TERM_5, mTM_TERM_12, mTM_TERM_1 };

static u8 l_owned = 1 << PC_ISL_SUMMER; /* the summer island is every town's */
static u8 l_active = PC_ISL_SUMMER;     /* the one in Save_t.island, where Kapp'n goes */
static Island_c l_store[PC_ISL_SEASONS]; /* the others (the active one's slot is stale) */

static int l_menu6;      /* this talk's "other things" menu has Islands */
static int l_asked = -1; /* the season being shown (its message loaded) */
static int l_buy_ready;  /* "shall I?" was asked: a yes buys it, once */
static int l_kappn_next; /* Kapp'n's own question, once an island is picked (0: he isn't asking) */
static u8 l_kappn_pick[PC_ISL_SEASONS]; /* his choices: the islands owned */
static int l_kappn_picks;

static Island_c* pc_isl_of(int season) {
    return season == l_active ? Save_GetPointer(island) : &l_store[season];
}

/* An island not named yet (a new one) has no first visit either: Kapp'n asks its name on the way over, as for the
 * summer island (ac_npc_sendo: a zero renew_time) */
static void pc_isl_unnamed(Island_c* isl) {
    int i;

    for (i = 0; i < mISL_ISLAND_NAME_LEN; i++) {
        if (isl->name[i] != CHAR_SPACE) {
            return;
        }
    }
    memset(&isl->renew_time, 0, sizeof(isl->renew_time));
}

/* No two islands share an islander: `season`'s finds another while one of the others has theirs */
static void pc_isl_unique(int season) {
    Animal_c* animal = &pc_isl_of(season)->animal;
    int tries, s, same;

    for (tries = 0; tries < 64; tries++) {
        same = FALSE;
        for (s = 0; s < PC_ISL_SEASONS; s++) {
            if (s != season && (l_owned & (1 << s)) && pc_isl_of(s)->animal.id.npc_id == animal->id.npc_id) {
                same = TRUE;
            }
        }
        if (!same) {
            return;
        }
        mNpc_DecideIslandNpc(animal);
        OSReport("[islands] the %s island's islander was another island's: now %04X\n", l_name[season],
                 animal->id.npc_id);
    }
}

int pc_season_island_term(void) {
    return l_term[l_active < PC_ISL_SEASONS ? l_active : PC_ISL_SUMMER];
}

int pc_season_island_winter(void) {
    return l_active == PC_ISL_WINTER;
}

/* A new island, as a new town gets one (its acres and items, then its name, cottage and islander): made in the
 * save, which is put back as it was afterwards */
static void pc_isl_make(Island_c* out) {
    static Save_t keep; /* big: not on the stack */

    keep = *Common_GetPointer(save.save);
    mFM_InitFgCombiSaveData(NULL);
    mISL_init(Save_GetPointer(island));
    *out = Save_Get(island);
    *Common_GetPointer(save.save) = keep;
    pc_isl_unnamed(out);
    mFM_SetBlockKindLoadCombi(); /* the town's own block kinds again */
}

/* Kapp'n goes to `season` from now on */
static void pc_isl_switch(int season) {
    if (season == l_active) {
        return;
    }
    pc_isl_unique(season);
    pc_isl_unnamed(&l_store[season]);
    l_store[l_active] = Save_Get(island);
    Save_Set(island, l_store[season]);
    l_active = (u8)season;
    /* today's island weather was picked for the island before: its rain or snow is this one's (m_kankyo_weather) */
    if (season == PC_ISL_WINTER && Common_Get(island_weather) == mEnv_WEATHER_RAIN) {
        Common_Set(island_weather, mEnv_WEATHER_SNOW);
    } else if (season != PC_ISL_WINTER && Common_Get(island_weather) == mEnv_WEATHER_SNOW) {
        Common_Set(island_weather, mEnv_WEATHER_RAIN);
    }
    OSReport("[islands] now the %s island\n", l_name[season]);
}

/* --- Nook --- */

int pc_season_island_choice(int str_no, u8* str, int len) {
    const char* name;
    int i;

    if (str_no == PC_ISL_CHOICE_MENU) {
        name = "Islands";
    } else if (str_no == PC_ISL_CHOICE_NONE) {
        name = "Never mind";
    } else if (str_no >= PC_ISL_CHOICE_FIRST && str_no < PC_ISL_CHOICE_FIRST + PC_ISL_SEASONS) {
        name = l_choice[str_no - PC_ISL_CHOICE_FIRST];
    } else {
        return FALSE;
    }
    for (i = 0; i < len; i++) {
        str[i] = *name != '\0' ? (u8)*name++ : CHAR_SPACE;
    }
    return TRUE;
}

void pc_season_island_set_menu(int on) {
    l_menu6 = on;
}

int pc_season_island_menu_on(void) {
    return l_menu6;
}

/* Nook's "which island?": then, for the one picked, what happens (pc_season_island_msg) */
#define PC_ISL_PICK                                                                                               \
    "{7f}{18}{07}{e4}{07}{e5}{07}{e6}{07}{e7}{7f}{04}{7f}{0d}{7f}{0f}{3e}{18}{7f}{10}{3e}{19}{7f}{11}{3e}{1a}"     \
    "{7f}{12}{3e}{1b}{7f}{19}|{7f}{01}"

int pc_season_island_msg(int index, u8* buf, int buf_size, int* len) {
    char text[1024];

    if (index == PC_ISL_MSG_WHICH) {
        snprintf(text, sizeof(text),
                 "{7f}{09}{00}{00}{04}Islands, hm?{7f}{03}{08} Kapp'n can row you|to any island you have.{7f}{04}|"
                 "{7f}{02}{7f}{09}{00}{00}{15}And I can find you more!{7f}{03}{08}|Each is %s Bells,|out of your"
                 " savings.{7f}{04}|{7f}{02}{7f}{09}{00}{00}{0b}Which island, hm?" PC_ISL_PICK,
                 "1,000,000");
    } else if (index >= PC_ISL_MSG_SEASON && index < PC_ISL_MSG_SEASON + PC_ISL_SEASONS) {
        int s = index - PC_ISL_MSG_SEASON;

        l_asked = s;
        l_buy_ready = FALSE;
        if (s == l_active) {
            snprintf(text, sizeof(text),
                     "{7f}{09}{00}{00}{15}That's the island Kapp'n|takes you to already, yes?|{7f}{00}");
        } else if (l_owned & (1 << s)) {
            pc_isl_switch(s);
            snprintf(text, sizeof(text),
                     "{7f}{09}{00}{00}{06}The %s island it is!{7f}{03}{08}|Kapp'n will take you there|next time, hm?"
                     "|{7f}{00}",
                     l_name[s]);
        } else {
            l_buy_ready = TRUE;
            snprintf(text, sizeof(text),
                     "{7f}{09}{00}{00}{15}The %s island?{7f}{03}{08} A beauty!|That'll be 1,000,000 Bells,|from your"
                     " savings.{7f}{04}|{7f}{02}{7f}{09}{00}{00}{0b}Shall I?"
                     "{7f}{16}{00}{03}{00}Q{7f}^{7f}{04}{7f}{0d}{7f}{0f}{3e}{35}{7f}{10}{3e}{36}|{7f}{19}|{7f}{01}",
                     l_name[s]);
        }
    } else if (index == PC_ISL_MSG_BUY) {
        Private_c* priv = Now_Private;

        if (!l_buy_ready || l_asked < 0 || priv == NULL) {
            snprintf(text, sizeof(text), "{7f}{09}{00}{00}{15}Hm?|{7f}{00}");
        } else if (priv->bank_account < PC_ISL_PRICE) {
            l_buy_ready = FALSE;
            snprintf(text, sizeof(text),
                     "{7f}{09}{00}{00}{17}Hm...{7f}{03}{08} your savings don't come|to 1,000,000 Bells yet.{7f}{04}|"
                     "{7f}{02}{7f}{09}{00}{00}{15}Save up at the post office,|and come see me again, hm?|{7f}{00}");
        } else {
            l_buy_ready = FALSE;
            priv->bank_account -= PC_ISL_PRICE;
            pc_isl_make(&l_store[l_asked]);
            l_owned |= (u8)(1 << l_asked);
            pc_isl_switch(l_asked);
            OSReport("[islands] bought the %s island\n", l_name[l_asked]);
            snprintf(text, sizeof(text),
                     "{7f}{09}{00}{00}{06}Wonderful!{7f}{03}{08} The %s island|is yours!{7f}{04}|{7f}{02}"
                     "{7f}{09}{00}{00}{15}Kapp'n will take you there|next time you go to the dock.|Oh ho ho!|{7f}{00}",
                     l_name[l_asked]);
        }
    } else if (index == PC_ISL_MSG_KAPPN && l_kappn_next != 0) {
        /* the islands, then "Never mind" (B picks it too): 3 to 5 choices */
        static const u8 select_code[] = { 0x17, 0x18, 0x79 };
        int n = snprintf(text, sizeof(text), "Which island ye fancy|today, then?{7f}{5e}{7f}{%02x}",
                         select_code[l_kappn_picks - 2]);
        int k;

        for (k = 0; k < l_kappn_picks; k++) {
            n += snprintf(text + n, sizeof(text) - n, "{07}{%02x}", PC_ISL_CHOICE_FIRST + l_kappn_pick[k] - 0x700);
        }
        n += snprintf(text + n, sizeof(text) - n, "{07}{%02x}", PC_ISL_CHOICE_NONE - 0x700);
        snprintf(text + n, sizeof(text) - n, "{7f}{04}{7f}{0d}{7f}{19}|{7f}{01}");
    } else if (index == PC_ISL_MSG_KAPPN_NO) {
        snprintf(text, sizeof(text), "Suit yerself, then.|Ye know where to find me!|{7f}{00}");
    } else if (index == PC_ISL_MSG_NO) {
        l_buy_ready = FALSE;
        snprintf(text, sizeof(text), "{7f}{09}{00}{00}{15}Some other time, then!|{7f}{00}");
    } else {
        return FALSE;
    }
    *len = pc_msg_text(text, buf, buf_size);
    return TRUE;
}

/* --- the companion save: { u8 version, owned, active, pad } then each owned island but the active one --- */

void pc_season_island_reset(void) {
    l_owned = 1 << PC_ISL_SUMMER;
    l_active = PC_ISL_SUMMER;
    memset(l_store, 0, sizeof(l_store));
}

int pc_season_island_read_chunk(const char tag[4], void* fp, unsigned int size) {
    FILE* f = (FILE*)fp;
    long end;
    u8 head[4];
    int s;

    if (memcmp(tag, "ISLS", 4) != 0) {
        return FALSE;
    }
    end = ftell(f) + (long)size;
    pc_season_island_reset();
    if (fread(head, 1, 4, f) == 4 && head[0] == PC_ISL_VERSION && head[2] < PC_ISL_SEASONS) {
        u8 owned = head[1] | (1 << PC_ISL_SUMMER);
        int ok = TRUE;

        for (s = 0; s < PC_ISL_SEASONS && ok; s++) {
            if ((owned & (1 << s)) && s != head[2]) {
                ok = fread(&l_store[s], sizeof(Island_c), 1, f) == 1;
            }
        }
        if (ok) {
            l_owned = owned;
            l_active = head[2];
        } else {
            pc_season_island_reset();
        }
    }
    fseek(f, end, SEEK_SET);
    return TRUE;
}

int pc_season_island_write_chunk(void* fp) {
    FILE* f = (FILE*)fp;
    u8 head[4] = { PC_ISL_VERSION, l_owned, l_active, 0 };
    u32 size = 4;
    int s, ok;

    if (l_owned == (1 << PC_ISL_SUMMER) && l_active == PC_ISL_SUMMER) {
        return TRUE; /* nothing bought: nothing to keep */
    }
    for (s = 0; s < PC_ISL_SEASONS; s++) {
        if ((l_owned & (1 << s)) && s != l_active) {
            size += sizeof(Island_c);
        }
    }
    ok = fwrite("ISLS", 1, 4, f) == 4 && fwrite(&size, 4, 1, f) == 1 && fwrite(head, 1, 4, f) == 4;
    for (s = 0; ok && s < PC_ISL_SEASONS; s++) {
        if ((l_owned & (1 << s)) && s != l_active) {
            ok = fwrite(&l_store[s], sizeof(Island_c), 1, f) == 1;
        }
    }
    return ok;
}

/* --- Kapp'n: which island, once the player says they'll ride --- */

int pc_season_island_kappn_start(int next_msg) {
    int s;

    l_kappn_next = 0;
    l_kappn_picks = 0;
    pc_isl_unnamed(Save_GetPointer(island)); /* (one bought before islands were named) */
    for (s = 0; s < PC_ISL_SEASONS; s++) {
        if (l_owned & (1 << s)) {
            l_kappn_pick[l_kappn_picks++] = (u8)s;
        }
    }
    if (l_kappn_picks < 2) {
        return FALSE; /* only the one: as on the GameCube */
    }
    l_kappn_next = next_msg;
    return TRUE;
}

int pc_season_island_kappn(void) {
    mMsg_Window_c* msg_p = mMsg_Get_base_window_p();
    int k;

    if (l_kappn_next == 0 || mMsg_Get_msg_num(msg_p) != PC_ISL_MSG_KAPPN) {
        return 0;
    }
    if (mMsg_Check_MainNormalContinue(msg_p) == TRUE) {
        k = mChoice_Get_ChoseNum(mChoice_Get_base_window_p());
        l_kappn_next = 0;
        if (k < 0 || k >= l_kappn_picks) {
            mMsg_Set_continue_msg_num(msg_p, PC_ISL_MSG_KAPPN_NO);
            mMsg_Set_ForceNext(msg_p);
            return 3;
        }
        /* the GameCube's "yes" goes on from here (its island check, then "all right then!") */
        pc_isl_switch(l_kappn_pick[k]);
        pc_isl_unnamed(Save_GetPointer(island));
        mMsg_Set_free_str(msg_p, mMsg_FREE_STR0, Save_Get(island).name, mISL_ISLAND_NAME_LEN);
        return 2;
    }
    return 1;
}
