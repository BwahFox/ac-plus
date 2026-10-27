/* pc_mp_fish.c - the same fish for everyone in an acre during a visit (multiplayer, pc_mp.c).
 *
 * A game has two fish at a time, made by its fish setter around its player from the season's tables and random
 * numbers, so two games put different fish in the same water. The host's are the town's: it sends where they are a
 * few times a second, and a visitor in the host's acre makes none of its own (ac_set_ovl_gyoei.c asks
 * pc_mp_fish_shared) but copies of the host's, kept where the host has them. A fish going after a visitor's bobber
 * (or on it) is the visitor's to move: the visitor reports it, and the host's fish follows. A fish a visitor
 * catches is gone for the host too. Out of the host's acre a visitor's game has its own fish, as nobody else can see
 * them. */
#include "pc_mp_fish.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ac_gyoei.h"
#include "ac_gyoei_h.h"
#include "m_common_data.h"
#include "m_field_info.h"
#include "m_play.h"
#include "m_player_lib.h"
#include "m_scene_table.h"

#define FISH_FRESH_MS 1000

extern ACTOR* pc_gyoei_ctrl_actor(void);

static struct {
    int valid;
    u8 type;
    f32 x, y, z;
    s16 angle_y;
    unsigned long long at;
} l_host[MP_FISH_MAX]; /* visitor: the host's fish */

static int l_map[MP_FISH_MAX] = { -1, -1 }; /* visitor: the host's fish slot -> our copy's slot */
static int l_hooked[MP_FISH_MAX];          /* visitor: our copy of the host's fish is going for our bobber */
static int l_caught_pending[MP_FISH_MAX];  /* visitor: we caught it; tell the host */
static int l_shared;

static struct {
    int from;
    f32 x, y, z;
    s16 angle_y;
    unsigned long long at;
} l_taken[MP_FISH_MAX]; /* host: a visitor moves this fish of ours (their bobber) */

static aGYO_CTRL_ACTOR* fish_slot(int i) {
    GYOEI_ACTOR* gyoei = (GYOEI_ACTOR*)pc_gyoei_ctrl_actor();
    return gyoei != NULL && i >= 0 && i < aGYO_MAX_GYOEI ? &gyoei->ctrl[i] : NULL;
}

static int fresh(unsigned long long at, unsigned long long now) {
    return at >= now || now - at < FISH_FRESH_MS;
}

/* a fish going after a bobber (near it, touching, biting) or on the line. The big sea fish run the other program
 * (ac_gyo_kaseki.c), with its own action numbers (ac_gyoei_clip.c_inc aGYO_setupActor) */
static int fish_busy(aGYO_CTRL_ACTOR* f) {
    int kaseki = f->gyo_type == aGYO_TYPE_SALMON || f->gyo_type == aGYO_TYPE_COELACANTH ||
                 f->gyo_type == aGYO_TYPE_JELLYFISH || f->gyo_type == aGYO_TYPE_SEA_BASS ||
                 f->gyo_type == aGYO_TYPE_RED_SNAPPER || f->gyo_type == aGYO_TYPE_BARRED_KNIFEJAW ||
                 f->gyo_type == aGYO_TYPE_WHALE;
    int near = kaseki ? 7 : 3, bite = kaseki ? 9 : 5; /* aGKK_ / aGTT_ACTION_NEAR .. _BITE */
    return (f->gyo_flags & 4) != 0 || (f->action >= near && f->action <= bite);
}

static void fish_remove(GAME_PLAY* play, int i) {
    aGYO_CTRL_ACTOR* f = fish_slot(i);
    if (f != NULL && f->exist && CLIP(gyo_clip) != NULL && CLIP(gyo_clip)->dt_gyoei_proc != NULL) {
        CLIP(gyo_clip)->dt_gyoei_proc((ACTOR*)f, (GAME*)play);
    }
}

void pc_mp_fish_reset(void) {
    memset(l_host, 0, sizeof(l_host));
    memset(l_taken, 0, sizeof(l_taken));
    memset(l_hooked, 0, sizeof(l_hooked));
    memset(l_caught_pending, 0, sizeof(l_caught_pending));
    l_map[0] = l_map[1] = -1;
    l_shared = FALSE;
}

int pc_mp_fish_shared(void) {
    return l_shared;
}

/* --- host --- */

void pc_mp_fish_collect_host(mp_fish* out) {
    int i;
    memset(out, 0, sizeof(*out));
    for (i = 0; i < aGYO_MAX_GYOEI && i < MP_FISH_MAX; i++) {
        aGYO_CTRL_ACTOR* f = fish_slot(i);
        if (f == NULL || !f->exist || f->gyo_type < 0 || f->gyo_type > aGYO_TYPE_WHALE) {
            continue;
        }
        out->e[out->count].slot = (uint8_t)i;
        out->e[out->count].type = (uint8_t)f->gyo_type;
        out->e[out->count].x = f->tools_class.actor_class.world.position.x;
        out->e[out->count].y = f->tools_class.actor_class.world.position.y;
        out->e[out->count].z = f->tools_class.actor_class.world.position.z;
        out->e[out->count].angle_y = f->tools_class.actor_class.shape_info.rotation.y;
        out->count++;
    }
}

void pc_mp_fish_host_received(int slot, const mp_fish* in, unsigned long long now) {
    int i;
    for (i = 0; i < in->count; i++) {
        int k = in->e[i].slot;
        aGYO_CTRL_ACTOR* f = fish_slot(k);
        if (f == NULL || !f->exist || f->gyo_type != in->e[i].type) {
            continue; /* not (or no longer) that fish */
        }
        if (in->e[i].flags & MP_FISH_CAUGHT) {
            fish_remove((GAME_PLAY*)gamePT, k); /* the visitor has it in their pockets */
            l_taken[k].at = 0;
        } else if ((in->e[i].flags & MP_FISH_HOOKED) && !fish_busy(f)) {
            l_taken[k].from = slot;
            l_taken[k].x = in->e[i].x, l_taken[k].y = in->e[i].y, l_taken[k].z = in->e[i].z;
            l_taken[k].angle_y = in->e[i].angle_y;
            l_taken[k].at = now;
        }
    }
}

/* --- visitor --- */

void pc_mp_fish_visitor_received(const mp_fish* in, unsigned long long now) {
    int i;
    for (i = 0; i < MP_FISH_MAX; i++) {
        l_host[i].valid = FALSE;
    }
    for (i = 0; i < in->count; i++) {
        int k = in->e[i].slot;
        if (in->e[i].type > aGYO_TYPE_WHALE) {
            continue;
        }
        l_host[k].valid = TRUE;
        l_host[k].type = in->e[i].type;
        l_host[k].x = in->e[i].x, l_host[k].y = in->e[i].y, l_host[k].z = in->e[i].z;
        l_host[k].angle_y = in->e[i].angle_y;
        l_host[k].at = now;
    }
}

int pc_mp_fish_collect_visitor(mp_fish* out) {
    int h;
    memset(out, 0, sizeof(*out));
    for (h = 0; h < MP_FISH_MAX; h++) {
        aGYO_CTRL_ACTOR* f = l_map[h] >= 0 ? fish_slot(l_map[h]) : NULL;
        if (l_caught_pending[h]) {
            out->e[out->count].slot = (uint8_t)h;
            out->e[out->count].type = l_host[h].type;
            out->e[out->count].flags = MP_FISH_CAUGHT;
            out->count++;
            l_caught_pending[h] = FALSE;
        } else if (l_hooked[h] && f != NULL && f->exist) {
            out->e[out->count].slot = (uint8_t)h;
            out->e[out->count].type = l_host[h].type;
            out->e[out->count].flags = MP_FISH_HOOKED;
            out->e[out->count].x = f->tools_class.actor_class.world.position.x;
            out->e[out->count].y = f->tools_class.actor_class.world.position.y;
            out->e[out->count].z = f->tools_class.actor_class.world.position.z;
            out->e[out->count].angle_y = f->tools_class.actor_class.shape_info.rotation.y;
            out->count++;
        }
    }
    return out->count > 0;
}

static int free_slot(void) {
    int i;
    for (i = 0; i < aGYO_MAX_GYOEI; i++) {
        aGYO_CTRL_ACTOR* f = fish_slot(i);
        if (f != NULL && !f->exist) {
            return i;
        }
    }
    return -1;
}

static int mapped(int i) {
    return l_map[0] == i || l_map[1] == i;
}

static void visitor_apply(GAME_PLAY* play, int host_in_town, f32 host_x, f32 host_z, unsigned long long now) {
    ACTOR* me = GET_PLAYER_ACTOR_GAME_ACTOR((GAME*)play);
    int bx, bz, hbx, hbz, h, i;
    xyz_t hp;
    int shared;

    hp.x = host_x, hp.y = 0.0f, hp.z = host_z;
    shared = play->scene_id == SCENE_FG && host_in_town && me != NULL && pc_gyoei_ctrl_actor() != NULL &&
             mFI_Wpos2BlockNum(&bx, &bz, me->world.position) && mFI_Wpos2BlockNum(&hbx, &hbz, hp) && bx == hbx &&
             bz == hbz;
    if (!shared) {
        if (l_shared) {
            /* out of the host's acre: our copies go (unless on our line), our own fish come back */
            for (h = 0; h < MP_FISH_MAX; h++) {
                aGYO_CTRL_ACTOR* f = l_map[h] >= 0 ? fish_slot(l_map[h]) : NULL;
                if (f != NULL && f->exist && !fish_busy(f)) {
                    fish_remove(play, l_map[h]);
                }
                l_map[h] = -1;
                l_hooked[h] = FALSE;
            }
        }
        l_shared = FALSE;
        return;
    }
    if (!l_shared) {
        /* into the host's acre: our own fish go, the host's are the fish here */
        for (i = 0; i < aGYO_MAX_GYOEI; i++) {
            aGYO_CTRL_ACTOR* f = fish_slot(i);
            if (f != NULL && f->exist && !fish_busy(f)) {
                fish_remove(play, i);
            }
        }
        l_map[0] = l_map[1] = -1;
        l_shared = TRUE;
    }

    for (h = 0; h < MP_FISH_MAX; h++) {
        aGYO_CTRL_ACTOR* f = l_map[h] >= 0 ? fish_slot(l_map[h]) : NULL;
        int have = f != NULL && f->exist;

        if (l_hooked[h] && !have) {
            l_caught_pending[h] = TRUE; /* it was on our line and it's gone: caught */
            l_hooked[h] = FALSE;
            l_map[h] = -1;
            l_host[h].valid = FALSE; /* (until the host's next word: it still has it in what it last said) */
            continue;
        }
        if (have) {
            l_hooked[h] = fish_busy(f);
        }
        if (!l_host[h].valid || !fresh(l_host[h].at, now)) {
            if (have && !l_hooked[h]) {
                fish_remove(play, l_map[h]); /* the host's fish went (caught, or swam out of its acre) */
            }
            if (!l_hooked[h]) {
                l_map[h] = -1;
            }
            continue;
        }
        if (have && f->gyo_type != l_host[h].type && !l_hooked[h]) {
            fish_remove(play, l_map[h]); /* a new fish in that slot */
            have = FALSE;
            l_map[h] = -1;
        }
        if (!have) {
            aGYO_Init_c init;
            int k = free_slot();
            if (k < 0 || mapped(k) || CLIP(gyo_clip) == NULL || CLIP(gyo_clip)->make_gyoei_proc == NULL) {
                continue;
            }
            init.fish_type = l_host[h].type;
            init.position.x = l_host[h].x, init.position.y = l_host[h].y, init.position.z = l_host[h].z;
            init.extra_data = 0;
            init.game = (GAME*)play;
            if (CLIP(gyo_clip)->make_gyoei_proc(&init) && fish_slot(k)->exist) {
                l_map[h] = k;
            }
            continue;
        }
        if (!l_hooked[h]) {
            /* where the host has it */
            xyz_t* pos = &f->tools_class.actor_class.world.position;
            f32 dx = l_host[h].x - pos->x, dz = l_host[h].z - pos->z;
            if (dx * dx + dz * dz > 60.0f * 60.0f) {
                pos->x = l_host[h].x, pos->z = l_host[h].z;
            } else {
                pos->x += dx * 0.3f, pos->z += dz * 0.3f;
            }
            pos->y = l_host[h].y;
            f->tools_class.actor_class.shape_info.rotation.y = l_host[h].angle_y;
            f->tools_class.actor_class.world.angle.y = l_host[h].angle_y;
        }
    }
}

static void host_apply(unsigned long long now) {
    int i;
    for (i = 0; i < MP_FISH_MAX; i++) {
        aGYO_CTRL_ACTOR* f = fish_slot(i);
        if (f == NULL || !f->exist || !fresh(l_taken[i].at, now) || l_taken[i].at == 0 || fish_busy(f)) {
            continue;
        }
        /* a visitor is fishing for it: it's where their game has it */
        f->tools_class.actor_class.world.position.x = l_taken[i].x;
        f->tools_class.actor_class.world.position.y = l_taken[i].y;
        f->tools_class.actor_class.world.position.z = l_taken[i].z;
        f->tools_class.actor_class.shape_info.rotation.y = l_taken[i].angle_y;
        f->tools_class.actor_class.world.angle.y = l_taken[i].angle_y;
    }
}

void pc_mp_fish_apply(GAME_PLAY* play, int host, int host_in_town, f32 host_x, f32 host_z, unsigned long long now) {
    static int frames;
    if (getenv("PC_MP_DEBUG") != NULL && ++frames % 180 == 0) {
        int i;
        printf("[mp] fish here%s:", host ? "" : l_shared ? " (the host's)" : " (our own)");
        for (i = 0; i < aGYO_MAX_GYOEI; i++) {
            aGYO_CTRL_ACTOR* f = fish_slot(i);
            if (f != NULL && f->exist) {
                printf(" %d@%d,%d%s", f->gyo_type, (int)f->tools_class.actor_class.world.position.x,
                       (int)f->tools_class.actor_class.world.position.z, fish_busy(f) ? "(on a line)" : "");
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

/* ctl fakecatch (visitor): as if the host's fish in our water had just been caught here */
void pc_mp_fish_dev_catch(void) {
    int h;
    for (h = 0; h < MP_FISH_MAX; h++) {
        if (l_map[h] >= 0 && fish_slot(l_map[h]) != NULL && fish_slot(l_map[h])->exist) {
            l_hooked[h] = TRUE;
            fish_remove((GAME_PLAY*)gamePT, l_map[h]);
            printf("[devtest] caught the host's fish %d\n", h);
            return;
        }
    }
    printf("[devtest] no fish of the host's here\n");
}
