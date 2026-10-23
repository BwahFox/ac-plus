/* pc_cf_lines.c - City Folk characters' lines. See pc_cf_lines.h. */
#include "pc_cf_lines.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PC_CF_CHOICE_LEN 16

static const char* l_files[PC_CF_SPEAKER_NUM] = { "cf_sky/celeste.msg", "cf_sky/brewster.msg" };

typedef struct {
    u8* body;
    int body_len;
    int choice_num;
    u8 choice[4][PC_CF_CHOICE_LEN];
} pc_cf_line_c;

typedef struct {
    int loaded; /* 0 not tried, 1 ok, -1 missing */
    int count;
    pc_cf_line_c* line;
} pc_cf_speaker_c;

static pc_cf_speaker_c l_speakers[PC_CF_SPEAKER_NUM];
static int l_talking = -1;

static pc_cf_speaker_c* pc_cf_lines_load(int who) {
    pc_cf_speaker_c* sp;
    FILE* f;
    u8 magic[4];
    u16 count = 0;
    int i;

    if (who < 0 || who >= PC_CF_SPEAKER_NUM) {
        return NULL;
    }
    sp = &l_speakers[who];
    if (sp->loaded != 0) {
        return sp;
    }
    sp->loaded = -1;
    f = fopen(l_files[who], "rb");
    if (f == NULL) {
        return sp;
    }
    if (fread(magic, 1, 4, f) != 4 || memcmp(magic, "CELE", 4) != 0 || fread(&count, 2, 1, f) != 1) {
        fclose(f);
        return sp;
    }
    sp->line = calloc(count, sizeof(pc_cf_line_c));
    for (i = 0; sp->line != NULL && i < count; i++) {
        pc_cf_line_c* l = &sp->line[i];
        u16 len = 0;
        u8 n = 0;
        int k;

        if (fread(&len, 2, 1, f) != 1 || (l->body = malloc(len + 1)) == NULL || fread(l->body, 1, len, f) != len ||
            fread(&n, 1, 1, f) != 1) {
            break;
        }
        l->body_len = len;
        l->choice_num = n > 4 ? 4 : n;
        for (k = 0; k < n; k++) {
            u8 skip[PC_CF_CHOICE_LEN];
            if (fread(k < 4 ? l->choice[k] : skip, 1, PC_CF_CHOICE_LEN, f) != PC_CF_CHOICE_LEN) {
                break;
            }
        }
    }
    fclose(f);
    if (sp->line != NULL && i == count) {
        sp->count = count;
        sp->loaded = 1;
        printf("[CF lines] %d lines from %s\n", count, l_files[who]);
    }
    return sp;
}

int pc_cf_lines_available(int who) {
    pc_cf_speaker_c* sp = pc_cf_lines_load(who);

    return sp != NULL && sp->loaded == 1;
}

int pc_cf_lines_count(int who) {
    return pc_cf_lines_available(who) ? l_speakers[who].count : 0;
}

void pc_cf_lines_set_talking(int who) {
    l_talking = who;
}

int pc_cf_lines_msg(int index, u8* buf, int buf_size, int* len) {
    int n = index - PC_CF_MSG_BASE;
    int last = FALSE;
    const pc_cf_speaker_c* sp;
    const pc_cf_line_c* l;
    int i = 0, k;

    if (l_talking < 0 || n < 0 || n >= 2 * PC_CF_MSG_LAST || !pc_cf_lines_available(l_talking)) {
        return FALSE;
    }
    sp = &l_speakers[l_talking];
    if (n >= PC_CF_MSG_LAST) {
        n -= PC_CF_MSG_LAST;
        last = TRUE;
    }
    if (n >= sp->count) {
        return FALSE;
    }
    l = &sp->line[n];
    if (l->body_len + 32 > buf_size) {
        return FALSE;
    }
    memcpy(buf, l->body, l->body_len);
    i = l->body_len;
    if (l->choice_num >= 2) {
        /* as the game's own menus: the options, B can't cancel, wait, open the choices, continue */
        buf[i++] = 0x7F;
        buf[i++] = (u8)(0x16 + l->choice_num - 2); /* SET_SELECT_STRING_2/3/4 */
        for (k = 0; k < l->choice_num; k++) {
            int id = PC_CF_CHOICE_BASE + n * 4 + k;
            buf[i++] = (u8)(id >> 8);
            buf[i++] = (u8)id;
        }
        buf[i++] = 0x7F, buf[i++] = 0x5E; /* SELECT_NO_B */
        buf[i++] = 0x7F, buf[i++] = 0x04; /* BUTTON */
        buf[i++] = 0x7F, buf[i++] = 0x0D; /* SET_SELECT_WINDOW */
        buf[i++] = 0xCD;
        buf[i++] = 0x7F, buf[i++] = 0x19; /* SET_FORCE_NEXT */
        buf[i++] = 0xCD;
        buf[i++] = 0x7F, buf[i++] = 0x01; /* CONTINUE */
    } else if (last) {
        buf[i++] = 0xCD;
        buf[i++] = 0x7F, buf[i++] = 0x00; /* LAST */
    } else {
        buf[i++] = 0x7F, buf[i++] = 0x01; /* CONTINUE: the actor picks what's next */
    }
    *len = i;
    return TRUE;
}

int pc_cf_lines_has_choices(int who, int n) {
    return pc_cf_lines_available(who) && n >= 0 && n < l_speakers[who].count && l_speakers[who].line[n].choice_num >= 2;
}

int pc_cf_lines_choice(int str_no, u8* str, int len) {
    int n = (str_no - PC_CF_CHOICE_BASE) / 4;
    int k = (str_no - PC_CF_CHOICE_BASE) % 4;
    const pc_cf_speaker_c* sp;

    if (l_talking < 0 || str_no < PC_CF_CHOICE_BASE || !pc_cf_lines_available(l_talking)) {
        return FALSE;
    }
    sp = &l_speakers[l_talking];
    if (n >= sp->count || k >= sp->line[n].choice_num) {
        return FALSE;
    }
    memset(str, ' ', len);
    memcpy(str, sp->line[n].choice[k], len < PC_CF_CHOICE_LEN ? len : PC_CF_CHOICE_LEN);
    return TRUE;
}
