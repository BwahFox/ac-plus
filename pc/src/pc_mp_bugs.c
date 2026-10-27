/* pc_mp_bugs.c - the same insects for everyone in an acre during a visit (multiplayer, pc_mp.c).
 *
 * As the fish (pc_mp_fish.c): the insect setter puts its own insects around its player, so the host's are the
 * town's. A visitor in the host's acre makes none of its own (ac_set_ovl_insect.c) but copies of the host's (the
 * setter's eight slots; the last one, for bees and the like, stays each game's own), kept where the host has them.
 * One right by a visitor is theirs to move - they're after it with their net, and it flees from them - and the
 * host's follows what they report; one that goes while they're there (caught, or flown off) is gone for the host
 * too. Ants stay each game's own. */
#include "pc_mp_fish.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ac_insect_h.h"
#include "m_common_data.h"
#include "m_field_info.h"
#include "m_play.h"
#include "m_player_lib.h"
#include "m_scene_table.h"

#define BUG_FRESH_MS 1000
#define BUG_NEAR (4.0f * 40.0f) /* a visitor this close is after it */

extern aINS_CTRL_ACTOR* pc_insect_ctrl_actor(void);

static struct {
    int valid;
    u8 type;
    f32 x, y, z;
    s16 angle_y;
    unsigned long long at;
} l_host[MP_BUG_MAX];

static int l_map[MP_BUG_MAX];
static int l_near[MP_BUG_MAX];
static int l_gone_pending[MP_BUG_MAX];
static int l_shared;

static struct {
    f32 x, y, z;
    s16 angle_y;
    unsigned long long at;
} l_taken[MP_BUG_MAX];

static aINS_INSECT_ACTOR* bug_slot(int i) {
    aINS_CTRL_ACTOR* ctrl = pc_insect_ctrl_actor();
    return ctrl != NULL && i >= 0 && i < MP_BUG_MAX ? &ctrl->insect_actor[i] : NULL;
}

static int bug_here(aINS_INSECT_ACTOR* b) {
    return b != NULL && b->exist_flag && !b->insect_flags.destruct;
}

static int fresh(unsigned long long at, unsigned long long now) {
    return at >= now || now - at < BUG_FRESH_MS;
}

static void bug_remove(GAME_PLAY* play, int i) {
    aINS_INSECT_ACTOR* b = bug_slot(i);
    if (bug_here(b) && CLIP(insect_clip) != NULL && CLIP(insect_clip)->dt_proc != NULL) {
        CLIP(insect_clip)->dt_proc((ACTOR*)b, (GAME*)play);
    }
}

void pc_mp_bugs_reset(void) {
    int i;
    memset(l_host, 0, sizeof(l_host));
    memset(l_taken, 0, sizeof(l_taken));
    memset(l_near, 0, sizeof(l_near));
    memset(l_gone_pending, 0, sizeof(l_gone_pending));
    for (i = 0; i < MP_BUG_MAX; i++) {
        l_map[i] = -1;
    }
    l_shared = FALSE;
}

int pc_mp_bugs_shared(void) {
    return l_shared;
}

/* --- host --- */

void pc_mp_bugs_collect_host(mp_fish* out) {
    int i;
    memset(out, 0, sizeof(*out));
    for (i = 0; i < MP_BUG_MAX; i++) {
        aINS_INSECT_ACTOR* b = bug_slot(i);
        if (!bug_here(b) || b->type < 0 || b->type >= aINS_INSECT_TYPE_NUM || b->type == aINS_INSECT_TYPE_ANT) {
            continue;
        }
        out->e[out->count].slot = (uint8_t)i;
        out->e[out->count].type = (uint8_t)b->type;
        out->e[out->count].x = b->tools_actor.actor_class.world.position.x;
        out->e[out->count].y = b->tools_actor.actor_class.world.position.y;
        out->e[out->count].z = b->tools_actor.actor_class.world.position.z;
        out->e[out->count].angle_y = b->tools_actor.actor_class.shape_info.rotation.y;
        out->count++;
    }
}

void pc_mp_bugs_host_received(int slot, const mp_fish* in, unsigned long long now) {
    int i;
    (void)slot;
    for (i = 0; i < in->count; i++) {
        int k = in->e[i].slot;
        aINS_INSECT_ACTOR* b = bug_slot(k);
        if (!bug_here(b) || b->type != in->e[i].type) {
            continue;
        }
        if (in->e[i].flags & MP_FISH_CAUGHT) {
            bug_remove((GAME_PLAY*)gamePT, k); /* caught by the visitor, or flown off from them */
            l_taken[k].at = 0;
        } else if (in->e[i].flags & MP_FISH_HOOKED) {
            l_taken[k].x = in->e[i].x, l_taken[k].y = in->e[i].y, l_taken[k].z = in->e[i].z;
            l_taken[k].angle_y = in->e[i].angle_y;
            l_taken[k].at = now;
        }
    }
}

/* --- visitor --- */

void pc_mp_bugs_visitor_received(const mp_fish* in, unsigned long long now) {
    int i;
    for (i = 0; i < MP_BUG_MAX; i++) {
        l_host[i].valid = FALSE;
    }
    for (i = 0; i < in->count; i++) {
        int k = in->e[i].slot;
        if (in->e[i].type >= aINS_INSECT_TYPE_NUM || in->e[i].type == aINS_INSECT_TYPE_ANT) {
            continue;
        }
        l_host[k].valid = TRUE;
        l_host[k].type = in->e[i].type;
        l_host[k].x = in->e[i].x, l_host[k].y = in->e[i].y, l_host[k].z = in->e[i].z;
        l_host[k].angle_y = in->e[i].angle_y;
        l_host[k].at = now;
    }
}

int pc_mp_bugs_collect_visitor(mp_fish* out) {
    int h;
    memset(out, 0, sizeof(*out));
    for (h = 0; h < MP_BUG_MAX; h++) {
        aINS_INSECT_ACTOR* b = l_map[h] >= 0 ? bug_slot(l_map[h]) : NULL;
        if (l_gone_pending[h]) {
            out->e[out->count].slot = (uint8_t)h;
            out->e[out->count].type = l_host[h].type;
            out->e[out->count].flags = MP_FISH_CAUGHT;
            out->count++;
            l_gone_pending[h] = FALSE;
        } else if (l_near[h] && bug_here(b)) {
            out->e[out->count].slot = (uint8_t)h;
            out->e[out->count].type = l_host[h].type;
            out->e[out->count].flags = MP_FISH_HOOKED;
            out->e[out->count].x = b->tools_actor.actor_class.world.position.x;
            out->e[out->count].y = b->tools_actor.actor_class.world.position.y;
            out->e[out->count].z = b->tools_actor.actor_class.world.position.z;
            out->e[out->count].angle_y = b->tools_actor.actor_class.shape_info.rotation.y;
            out->count++;
        }
    }
    return out->count > 0;
}

static int mapped(int i) {
    int h;
    for (h = 0; h < MP_BUG_MAX; h++) {
        if (l_map[h] == i) {
            return TRUE;
        }
    }
    return FALSE;
}

static int free_slot(void) {
    int i;
    for (i = 0; i < MP_BUG_MAX; i++) {
        aINS_INSECT_ACTOR* b = bug_slot(i);
        if (b != NULL && !b->exist_flag) {
            return i;
        }
    }
    return -1;
}

static void visitor_apply(GAME_PLAY* play, int host_in_town, f32 host_x, f32 host_z, unsigned long long now) {
    ACTOR* me = GET_PLAYER_ACTOR_GAME_ACTOR((GAME*)play);
    int bx, bz, hbx, hbz, h, i;
    xyz_t hp;
    int shared;

    hp.x = host_x, hp.y = 0.0f, hp.z = host_z;
    shared = play->scene_id == SCENE_FG && host_in_town && me != NULL && pc_insect_ctrl_actor() != NULL &&
             mFI_Wpos2BlockNum(&bx, &bz, me->world.position) && mFI_Wpos2BlockNum(&hbx, &hbz, hp) && bx == hbx &&
             bz == hbz;
    if (!shared) {
        if (l_shared) {
            for (h = 0; h < MP_BUG_MAX; h++) {
                if (l_map[h] >= 0 && !l_near[h]) {
                    bug_remove(play, l_map[h]);
                }
                l_map[h] = -1;
                l_near[h] = FALSE;
            }
        }
        l_shared = FALSE;
        return;
    }
    if (!l_shared) {
        /* into the host's acre: our own insects go, the host's are the insects here */
        for (i = 0; i < MP_BUG_MAX; i++) {
            aINS_INSECT_ACTOR* b = bug_slot(i);
            if (bug_here(b) && b->type != aINS_INSECT_TYPE_ANT) {
                bug_remove(play, i);
            }
        }
        for (h = 0; h < MP_BUG_MAX; h++) {
            l_map[h] = -1;
            l_near[h] = FALSE;
        }
        l_shared = TRUE;
    }

    for (h = 0; h < MP_BUG_MAX; h++) {
        aINS_INSECT_ACTOR* b = l_map[h] >= 0 ? bug_slot(l_map[h]) : NULL;
        int have = bug_here(b);

        if (l_near[h] && !have) {
            l_gone_pending[h] = TRUE; /* went while we were right there: caught (or flown off) */
            l_near[h] = FALSE;
            l_map[h] = -1;
            l_host[h].valid = FALSE; /* (until the host's next word: it still has it in what it last said) */
            continue;
        }
        if (have) {
            f32 dx = b->tools_actor.actor_class.world.position.x - me->world.position.x;
            f32 dz = b->tools_actor.actor_class.world.position.z - me->world.position.z;
            l_near[h] = dx * dx + dz * dz < BUG_NEAR * BUG_NEAR;
        }
        if (!l_host[h].valid || !fresh(l_host[h].at, now)) {
            if (have) {
                bug_remove(play, l_map[h]); /* gone at the host's: gone here, even from under our net */
            }
            l_map[h] = -1;
            l_near[h] = FALSE;
            continue;
        }
        if (have && b->type != l_host[h].type && !l_near[h]) {
            bug_remove(play, l_map[h]);
            have = FALSE;
            l_map[h] = -1;
        }
        if (!have) {
            aINS_Init_c init;
            int k = free_slot();
            if (k < 0 || mapped(k) || CLIP(insect_clip) == NULL || CLIP(insect_clip)->make_insect_proc == NULL) {
                continue;
            }
            init.insect_type = l_host[h].type;
            init.position.x = l_host[h].x, init.position.y = l_host[h].y, init.position.z = l_host[h].z;
            init.extra_data = aINS_INIT_NORMAL;
            init.game = (GAME*)play;
            if (CLIP(insect_clip)->make_insect_proc(&init, aINS_MAKE_NEW) != NULL && bug_here(bug_slot(k))) {
                l_map[h] = k;
            }
            continue;
        }
        if (!l_near[h]) {
            xyz_t* pos = &b->tools_actor.actor_class.world.position;
            f32 dx = l_host[h].x - pos->x, dz = l_host[h].z - pos->z;
            if (dx * dx + dz * dz > 60.0f * 60.0f) {
                pos->x = l_host[h].x, pos->z = l_host[h].z;
            } else {
                pos->x += dx * 0.3f, pos->z += dz * 0.3f;
            }
            pos->y = l_host[h].y;
            b->tools_actor.actor_class.shape_info.rotation.y = l_host[h].angle_y;
            b->tools_actor.actor_class.world.angle.y = l_host[h].angle_y;
        }
    }
}

static void host_apply(unsigned long long now) {
    int i;
    for (i = 0; i < MP_BUG_MAX; i++) {
        aINS_INSECT_ACTOR* b = bug_slot(i);
        if (!bug_here(b) || l_taken[i].at == 0 || !fresh(l_taken[i].at, now)) {
            continue;
        }
        /* a visitor is after it: it's where their game has it */
        b->tools_actor.actor_class.world.position.x = l_taken[i].x;
        b->tools_actor.actor_class.world.position.y = l_taken[i].y;
        b->tools_actor.actor_class.world.position.z = l_taken[i].z;
        b->tools_actor.actor_class.shape_info.rotation.y = l_taken[i].angle_y;
        b->tools_actor.actor_class.world.angle.y = l_taken[i].angle_y;
    }
}

void pc_mp_bugs_apply(GAME_PLAY* play, int host, int host_in_town, f32 host_x, f32 host_z, unsigned long long now) {
    static int frames;
    if (getenv("PC_MP_DEBUG") != NULL && ++frames % 180 == 0) {
        int i;
        printf("[mp] insects here%s:", host ? "" : l_shared ? " (the host's)" : " (our own)");
        for (i = 0; i < MP_BUG_MAX; i++) {
            aINS_INSECT_ACTOR* b = bug_slot(i);
            if (bug_here(b)) {
                printf(" %d@%d,%d", b->type, (int)b->tools_actor.actor_class.world.position.x,
                       (int)b->tools_actor.actor_class.world.position.z);
            }
        }
        printf("\n");
    }
    if (host) {
        host_apply(now);
    } else {
        visitor_apply(play, host_in_town, host_x, host_z, now);
    }
}

/* ctl fakecatch: an insect of the host's here is caught */
void pc_mp_bugs_dev_catch(void) {
    int h;
    for (h = 0; h < MP_BUG_MAX; h++) {
        if (l_map[h] >= 0 && bug_here(bug_slot(l_map[h]))) {
            l_near[h] = TRUE;
            bug_remove((GAME_PLAY*)gamePT, l_map[h]);
            printf("[devtest] caught the host's insect %d\n", h);
            return;
        }
    }
    printf("[devtest] no insect of the host's here\n");
}
