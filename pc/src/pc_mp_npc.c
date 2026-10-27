/* pc_mp_npc.c - the town's villagers in the same places in every game during a visit (multiplayer, pc_mp.c).
 *
 * A villager is only a moving actor near a player, so each visitor tells the host where the villagers it has loaded
 * are, and the host decides who moves each one:
 *  - whoever is talking to them (a villager doesn't walk off mid-conversation);
 *  - else the host, if it has them loaded;
 *  - else the first visitor who has them loaded.
 * The host sends each visitor where the villagers it doesn't move are; every other game puts its own copy of a
 * villager there, after its actors have moved. A villager nobody has loaded stays as the town's own bookkeeping
 * has them.
 * Each game keeps its own idea of where the villagers it hasn't loaded are, and those drift apart: the host may have
 * someone right next to a visitor whose game thinks they're three acres away, or at home. So a visitor whose game
 * has no actor for a villager the host reports in the acre it is in makes one there (spawn_missing), as the
 * game's own villager manager would, and tells the manager they're out. The host is the one that says where
 * everyone is: it also sends where its town has the villagers nobody has loaded, and a visitor with one of them
 * out in another acre than that lets them go. It never takes a villager's place from a visitor's game. The other way round, a villager the host
 * has at home (in their house: not loaded, or loaded but hidden inside) is at home for everyone: a visitor's game
 * that has them outdoors sends its copy home. */
#include "pc_mp_npc.h"
#include "pc_mp.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ac_npc.h"
#include "ac_npc_anim_def.h"
#include "ac_set_npc_manager.h"
#include "m_actor.h"
#include "m_common_data.h"
#include "m_demo.h"
#include "m_field_info.h"
#include "m_npc.h"
#include "m_play.h"
#include "m_player_lib.h"
#include "m_scene_table.h"

#define REMOTE_FRESH_MS 600

static struct {
    u8 flags;
    f32 x, y, z;
    s16 angle_y;
    u8 anim;
    unsigned long long at;
} l_remote[PC_MP_SLOTS][MP_NPC_MAX]; /* host: what each visitor said; visitor: what the host said (slot 0) */

static ACTOR* l_local[MP_NPC_MAX];
static int l_hidden[MP_NPC_MAX]; /* loaded, but hidden in their house */

/* the loaded actor of the town's villager idx, if any (outdoors in town only) */
static void find_local(GAME_PLAY* play) {
    ACTOR* a;
    int i;

    memset(l_local, 0, sizeof(l_local));
    memset(l_hidden, 0, sizeof(l_hidden));
    if (play->scene_id != SCENE_FG) {
        return;
    }
    for (a = play->actor_info.list[ACTOR_PART_NPC].actor; a != NULL; a = a->next_actor) {
        for (i = 0; i < MP_NPC_MAX; i++) {
            if (Save_Get(animals[i]).id.npc_id != EMPTY_NO && a->npc_id == Save_Get(animals[i]).id.npc_id) {
                if (ITEM_NAME_GET_TYPE(a->npc_id) == NAME_TYPE_NPC && ((NPC_ACTOR*)a)->condition_info.hide_flg) {
                    l_hidden[i] = TRUE; /* inside their house: not out where anyone can see them */
                } else {
                    l_local[i] = a;
                }
            }
        }
    }
}

/* in a conversation right now: a talk running, not one only waiting in the demo's records (a villager's interrupted
 * "speak" stays there as the current request with no demo running) */
static int talking(ACTOR* a) {
    return a != NULL && mDemo_CheckDemo() && mDemo_IS_ACTOR_TALKING(a);
}

static f32 clampf(f32 v, f32 lo, f32 hi) {
    return v < lo ? lo : v > hi ? hi : v;
}

static int fresh(int slot, int i, unsigned long long now) {
    /* (received during this frame's poll, after `now`: fresh) */
    return (l_remote[slot][i].flags & MP_NPC_LOADED) && (l_remote[slot][i].at >= now || now - l_remote[slot][i].at < REMOTE_FRESH_MS);
}

/* host: who moves villager i - a slot, or -1 if nobody has them loaded */
/* host: villager i is in their house here (and so for everyone) */
static int host_home(int i) {
    return l_local[i] == NULL && (l_hidden[i] || Save_Get(animals[i]).is_home);
}

static int mover(int i, unsigned long long now) {
    int s;
    if (talking(l_local[i])) {
        return 0;
    }
    if (host_home(i)) {
        return -1;
    }
    for (s = 1; s < PC_MP_SLOTS; s++) {
        if (fresh(s, i, now) && (l_remote[s][i].flags & MP_NPC_TALKING)) {
            return s;
        }
    }
    if (l_local[i] != NULL) {
        return 0;
    }
    for (s = 1; s < PC_MP_SLOTS; s++) {
        if (fresh(s, i, now)) {
            return s;
        }
    }
    return -1;
}

/* the animations passed between games: walking, running and standing (the first ten, aNPC_ANIM_RUN1 to
 * aNPC_ANIM_WAIT_NEMU1). Anything else (talking, umbrellas, special characters') stays each game's own. */
static int moving_anim(int anim) {
    return anim >= aNPC_ANIM_RUN1 && anim <= aNPC_ANIM_WAIT_NEMU1;
}

static u8 anim_of(ACTOR* a) {
    int anim = ((NPC_ACTOR*)a)->draw.animation_id;
    return moving_anim(anim) ? (u8)anim : 0xFF;
}

static void add(mp_npcs* out, int i, u8 flags, f32 x, f32 y, f32 z, s16 angle_y, u8 anim) {
    out->e[out->count].idx = (u8)i;
    out->e[out->count].flags = flags;
    out->e[out->count].x = x;
    out->e[out->count].y = y;
    out->e[out->count].z = z;
    out->e[out->count].angle_y = angle_y;
    out->e[out->count].anim = anim;
    out->count++;
}

void pc_mp_npc_collect(GAME_PLAY* play, mp_npcs* out) {
    int i;

    find_local(play);
    memset(out, 0, sizeof(*out));
    for (i = 0; i < MP_NPC_MAX; i++) {
        ACTOR* a = l_local[i];
        if (a != NULL) {
            add(out, i, MP_NPC_LOADED | (talking(a) ? MP_NPC_TALKING : 0), a->world.position.x, a->world.position.y,
                a->world.position.z, a->shape_info.rotation.y, anim_of(a));
        }
    }
}

void pc_mp_npc_collect_for(GAME_PLAY* play, int to_slot, unsigned long long now, mp_npcs* out) {
    int i;

    find_local(play);
    memset(out, 0, sizeof(*out));
    for (i = 0; i < MP_NPC_MAX; i++) {
        int m = mover(i, now);
        if (host_home(i)) {
            const xyz_t* h = &Common_Get(npclist)[i].house_position;
            add(out, i, MP_NPC_HOME, h->x, h->y, h->z, 0, 0xFF);
            continue;
        }
        if (m < 0) {
            /* nobody has them loaded: out somewhere in the town, as the host's town has them */
            const xyz_t* p = &Common_Get(npclist)[i].position;
            if (Save_Get(animals[i]).id.npc_id != EMPTY_NO) {
                add(out, i, MP_NPC_AWAY, p->x, p->y, p->z, 0, 0xFF);
            }
            continue;
        }
        if (m == to_slot) {
            /* theirs to move while they're the only one near - if they're where the host's town has them */
            const xyz_t* p = &Common_Get(npclist)[i].position;
            xyz_t r;
            int bx, bz, rbx, rbz;
            r.x = l_remote[m][i].x, r.y = l_remote[m][i].y, r.z = l_remote[m][i].z;
            if (!talking(l_local[i]) && !(l_remote[m][i].flags & MP_NPC_TALKING) && mFI_Wpos2BlockNum(&bx, &bz, *p) &&
                mFI_Wpos2BlockNum(&rbx, &rbz, r) && (bx != rbx || bz != rbz)) {
                add(out, i, MP_NPC_AWAY, p->x, p->y, p->z, 0, 0xFF); /* no: the host's town has them elsewhere */
            }
            continue;
        }
        if (m == 0) {
            ACTOR* a = l_local[i];
            add(out, i, MP_NPC_LOADED | (talking(a) ? MP_NPC_TALKING : 0), a->world.position.x, a->world.position.y,
                a->world.position.z, a->shape_info.rotation.y, anim_of(a));
        } else {
            add(out, i, l_remote[m][i].flags, l_remote[m][i].x, l_remote[m][i].y, l_remote[m][i].z,
                l_remote[m][i].angle_y, l_remote[m][i].anim);
        }
    }
}

void pc_mp_npc_received(int from_slot, const mp_npcs* in, unsigned long long now) {
    int i;
    if (from_slot < 0 || from_slot >= PC_MP_SLOTS) {
        return;
    }
    for (i = 0; i < MP_NPC_MAX; i++) {
        l_remote[from_slot][i].flags = 0; /* not listed: not loaded there */
    }
    for (i = 0; i < in->count; i++) {
        int k = in->e[i].idx;
        l_remote[from_slot][k].flags = in->e[i].flags;
        /* inside the town's field: these move real villagers, with their collision */
        l_remote[from_slot][k].x = clampf(in->e[i].x, 0.0f, BLOCK_X_NUM * UT_X_NUM * 40.0f);
        l_remote[from_slot][k].y = clampf(in->e[i].y, -200.0f, 1000.0f);
        l_remote[from_slot][k].z = clampf(in->e[i].z, 0.0f, BLOCK_Z_NUM * UT_Z_NUM * 40.0f);
        l_remote[from_slot][k].angle_y = in->e[i].angle_y;
        l_remote[from_slot][k].anim = moving_anim(in->e[i].anim) ? in->e[i].anim : 0xFF;
        l_remote[from_slot][k].at = now;
    }
}

/* someone else is talking to this villager (host: a visitor says so; visitor: the host's list says so - it leaves
 * out the ones we talk to ourselves) */
int pc_mp_npc_busy(ACTOR* a, int host, unsigned long long now) {
    int i, s;
    if (a == NULL || ITEM_NAME_GET_TYPE(a->npc_id) != NAME_TYPE_NPC) {
        return FALSE;
    }
    for (i = 0; i < MP_NPC_MAX; i++) {
        if (Save_Get(animals[i]).id.npc_id == a->npc_id) {
            break;
        }
    }
    if (i == MP_NPC_MAX) {
        return FALSE;
    }
    for (s = host ? 1 : 0; s < (host ? PC_MP_SLOTS : 1); s++) {
        if (fresh(s, i, now) && (l_remote[s][i].flags & MP_NPC_TALKING)) {
            return TRUE;
        }
    }
    return FALSE;
}

void pc_mp_npc_forget(int slot) {
    if (slot >= 0 && slot < PC_MP_SLOTS) {
        memset(l_remote[slot], 0, sizeof(l_remote[slot]));
    }
}

void pc_mp_npc_reset(void) {
    memset(l_remote, 0, sizeof(l_remote));
}

static SET_NPC_MANAGER_ACTOR* npc_manager(GAME_PLAY* play) {
    ACTOR* a;
    for (a = play->actor_info.list[ACTOR_PART_CONTROL].actor; a != NULL; a = a->next_actor) {
        if (a->id == mAc_PROFILE_SET_NPC_MANAGER) {
            return (SET_NPC_MANAGER_ACTOR*)a;
        }
    }
    return NULL;
}

/* visitor: villagers the host has at home that we have out go home too (see the top) */
static void send_home(GAME_PLAY* play, unsigned long long now) {
    int i;
    for (i = 0; i < MP_NPC_MAX; i++) {
        ACTOR* a = l_local[i];
        if (a == NULL || !(l_remote[0][i].flags & MP_NPC_HOME) || talking(a) ||
            (l_remote[0][i].at < now && now - l_remote[0][i].at >= REMOTE_FRESH_MS)) {
            continue;
        }
        Save_Get(animals[i]).is_home = TRUE; /* (this town copy is never saved) */
        Common_Get(npclist)[i].position = Common_Get(npclist)[i].house_position;
        a->world.position = Common_Get(npclist)[i].house_position; /* (where the villager's goodbye writes them down) */
        Actor_delete(a);
        l_local[i] = NULL;
        printf("[mp] villager %d is at home at the host's: sent home here too\n", i);
    }
    {
        /* and the ones the host has out somewhere else (another acre): not here either */
        ACTOR* player = GET_PLAYER_ACTOR_GAME_ACTOR((GAME*)play);
        int pbx, pbz;
        if (player == NULL || !mFI_Wpos2BlockNum(&pbx, &pbz, player->world.position)) {
            return;
        }
        for (i = 0; i < MP_NPC_MAX; i++) {
            ACTOR* a = l_local[i];
            xyz_t p;
            int bx, bz;
            if (!(l_remote[0][i].flags & MP_NPC_AWAY) ||
                (l_remote[0][i].at < now && now - l_remote[0][i].at >= REMOTE_FRESH_MS)) {
                continue;
            }
            p.x = l_remote[0][i].x, p.y = l_remote[0][i].y, p.z = l_remote[0][i].z;
            if (a == NULL) {
                /* our town's idea of where they are follows the host's: they turn up here when they come by there */
                if (!l_hidden[i] && Save_Get(animals[i]).is_home == FALSE) {
                    Common_Get(npclist)[i].position = p;
                }
                continue;
            }
            if (talking(a)) {
                continue;
            }
            if (!mFI_Wpos2BlockNum(&bx, &bz, p) || (bx == pbx && bz == pbz)) {
                continue; /* in our acre as far as the host knows: ours to show, nobody near the host */
            }
            Common_Get(npclist)[i].position = p;
            if (ABS(bx - pbx) <= 1 && ABS(bz - pbz) <= 1) {
                /* an acre next to ours, which our game has loaded too: there, where the host has them */
                a->world.position = p;
                continue;
            }
            a->world.position = p; /* the villager's goodbye (mNpc_RenewalSetNpc) writes this down, not here */
            Actor_delete(a);
            l_local[i] = NULL;
            printf("[mp] villager %d is elsewhere at the host's (%d,%d): gone from here too\n", i, (int)p.x, (int)p.z);
        }
    }
}

/* visitor: villagers the host has near us that our game hasn't made (see the top) */
/* visitor: villagers the host has near us that our game hasn't made (see the top) */
static void spawn_missing(GAME_PLAY* play, unsigned long long now) {
    static unsigned long long tried[MP_NPC_MAX];
    SET_NPC_MANAGER_ACTOR* manager;
    ACTOR* player = GET_PLAYER_ACTOR_GAME_ACTOR((GAME*)play);
    int pbx, pbz, i;

    if (play->scene_id != SCENE_FG || player == NULL || CLIP(npc_clip) == NULL ||
        CLIP(npc_clip)->setupActor_proc == NULL || !mFI_Wpos2BlockNum(&pbx, &pbz, player->world.position)) {
        return;
    }
    manager = npc_manager(play);
    for (i = 0; i < MP_NPC_MAX; i++) {
        mActor_name_t npc_id = Save_Get(animals[i]).id.npc_id;
        xyz_t pos;
        int bx, bz, ux, uz;

        if (l_local[i] != NULL || l_hidden[i] || !fresh(0, i, now) || npc_id == EMPTY_NO ||
            ITEM_NAME_GET_TYPE(npc_id) != NAME_TYPE_NPC || (tried[i] != 0 && now - tried[i] < 1000)) {
            continue;
        }
        pos.x = l_remote[0][i].x, pos.y = l_remote[0][i].y, pos.z = l_remote[0][i].z;
        /* in our acre only: one made in a neighbouring acre is cleared again by the game within seconds */
        if (!mFI_Wpos2BlockNum(&bx, &bz, pos) || bx != pbx || bz != pbz || !mFI_Wpos2UtNum_inBlock(&ux, &uz, pos)) {
            continue;
        }
        tried[i] = now;
        Save_Get(animals[i]).is_home = FALSE; /* out, as the host has them (this town copy is never saved) */
        Common_Get(npclist)[i].position = pos;
        if (CLIP(npc_clip)->setupActor_proc(play, npc_id, (s8)i, i, -1, bx, bz, ux, uz) == TRUE) {
            Common_Get(npclist)[i].appear_flag = FALSE;
            if (manager != NULL) {
                manager->npc_info.appear |= 1 << i; /* the manager doesn't make a second one */
            }
            printf("[mp] villager %d made where the host has them (%d,%d)\n", i, (int)pos.x, (int)pos.z);
        }
    }
}

void pc_mp_npc_apply(GAME_PLAY* play, int host, unsigned long long now) {
    int i;

    find_local(play);
    if (!host) {
        send_home(play, now);
        spawn_missing(play, now);
    }
    for (i = 0; i < MP_NPC_MAX; i++) {
        ACTOR* a = l_local[i];
        int from;
        f32 dx, dz;

        if (a == NULL) {
            continue;
        }
        if (host) {
            from = mover(i, now); /* 0 is us */
        } else {
            from = fresh(0, i, now) && !talking(a) ? 0 : -1; /* the host lists what isn't ours to move */
            if (fresh(0, i, now) && !(l_remote[0][i].flags & (MP_NPC_HOME | MP_NPC_AWAY)) &&
                Save_Get(animals[i]).is_home && !talking(a)) {
                /* out where the host has them, but our game made them as leaving their house (we sent them home
                 * earlier): kept where the host has them they'd never finish leaving, nor talk. They go, and are
                 * made again out (spawn_missing) */
                Save_Get(animals[i]).is_home = FALSE;
                a->world.position.x = l_remote[0][i].x, a->world.position.z = l_remote[0][i].z;
                Actor_delete(a);
                l_local[i] = NULL;
                printf("[mp] villager %d was leaving home here but is out at the host's: made again\n", i);
                continue;
            }
        }
        if (host ? from <= 0 : from < 0) {
            continue;
        }
        dx = l_remote[from][i].x - a->world.position.x;
        dz = l_remote[from][i].z - a->world.position.z;
        if (dx * dx + dz * dz > 80.0f * 80.0f) {
            a->world.position.x = l_remote[from][i].x; /* far off: put them there */
            a->world.position.z = l_remote[from][i].z;
        } else {
            a->world.position.x += dx * 0.35f;
            a->world.position.z += dz * 0.35f;
        }
        a->world.position.y = l_remote[from][i].y;
        a->world.angle.y = l_remote[from][i].angle_y;
        a->shape_info.rotation.y = l_remote[from][i].angle_y;
        /* walking when they walk there, standing when they stand: only between those, never cutting into
         * anything else this game's villager is doing */
        if (l_remote[from][i].anim != 0xFF && moving_anim(((NPC_ACTOR*)a)->draw.animation_id) &&
            ((NPC_ACTOR*)a)->draw.animation_id != l_remote[from][i].anim && CLIP(npc_clip) != NULL &&
            CLIP(npc_clip)->animation_init_proc != NULL) {
            CLIP(npc_clip)->animation_init_proc(a, l_remote[from][i].anim, FALSE);
        }
    }
}

/* --- villagers' memories of visitors. A visitor's villagers are made new for the visit, so they wouldn't know a
 * returning visitor: the host sends each villager's memory of that visitor (theirs only: nothing about the town's
 * residents or other visitors, and no letters), and the visitor's game sends it back when it changes, for the
 * host's save. The host takes a memory only of the visitor it comes from, never of one of its own residents. --- */

static int pid_names(const PersonalID_c* pid, const u8* name, const u8* town) {
    return memcmp(pid->player_name, name, PLAYER_NAME_LEN) == 0 && memcmp(pid->land_name, town, LAND_NAME_SIZE) == 0;
}

static void memory_out(mp_memory* out, int v, const Anmmem_c* m) {
    memset(out, 0, sizeof(*out));
    out->villager = (uint8_t)v;
    memcpy(out->name, m->memory_player_id.player_name, 8);
    memcpy(out->town, m->memory_player_id.land_name, 8);
    out->player_id = m->memory_player_id.player_id;
    out->land_id = m->memory_player_id.land_id;
    out->time[0] = m->last_speak_time.sec, out->time[1] = m->last_speak_time.min;
    out->time[2] = m->last_speak_time.hour, out->time[3] = m->last_speak_time.day;
    out->time[4] = m->last_speak_time.weekday, out->time[5] = m->last_speak_time.month;
    out->year = m->last_speak_time.year;
    memcpy(out->prev_town, m->memuni.land.name, 8);
    out->town_tune = m->saved_town_tune;
    out->friendship = m->friendship;
}

static int memory_ok(const mp_memory* in) {
    return in->villager < MP_NPC_MAX && in->time[0] < 60 && in->time[1] < 60 && in->time[2] < 24 && in->time[3] >= 1 &&
           in->time[3] <= 31 && in->time[4] < 7 && in->time[5] >= 1 && in->time[5] <= 12 && in->year >= 2000 &&
           in->year <= 2099;
}

static void memory_in(Anmmem_c* m, const mp_memory* in) {
    m->last_speak_time.sec = in->time[0], m->last_speak_time.min = in->time[1];
    m->last_speak_time.hour = in->time[2], m->last_speak_time.day = in->time[3];
    m->last_speak_time.weekday = in->time[4], m->last_speak_time.month = in->time[5];
    m->last_speak_time.year = in->year;
    memcpy(m->memuni.land.name, in->prev_town, 8);
    m->saved_town_tune = in->town_tune;
    m->friendship = in->friendship;
}

/* host: villager v's memory of the visitor called name from town, to send them on joining */
int pc_mp_memory_for(int v, const u8* name, const u8* town, mp_memory* out) {
    Animal_c* a;
    int k;
    if (v < 0 || v >= MP_NPC_MAX) {
        return FALSE;
    }
    a = Save_GetPointer(animals[v]);
    if (a->id.npc_id == EMPTY_NO) {
        return FALSE;
    }
    for (k = 0; k < ANIMAL_MEMORY_NUM; k++) {
        Anmmem_c* m = &a->memories[k];
        if (!mPr_NullCheckPersonalID(&m->memory_player_id) && pid_names(&m->memory_player_id, name, town)) {
            memory_out(out, v, m);
            return TRUE;
        }
    }
    return FALSE;
}

/* host: a visitor's memory from their game, for our save */
void pc_mp_memory_host_apply(const mp_memory* in, const u8* name, const u8* town) {
    PersonalID_c pid;
    Animal_c* a;
    int idx, i;

    if (!memory_ok(in) || memcmp(in->name, name, 8) != 0 || memcmp(in->town, town, 8) != 0 ||
        in->land_id == Save_Get(land_info).id) {
        return; /* not that visitor's own, or one of ours */
    }
    for (i = 0; i < PLAYER_NUM; i++) {
        if (pid_names(&Save_Get(private_data[i]).player_ID, name, town)) {
            return;
        }
    }
    a = Save_GetPointer(animals[in->villager]);
    if (a->id.npc_id == EMPTY_NO) {
        return;
    }
    memset(&pid, 0, sizeof(pid));
    memcpy(pid.player_name, in->name, 8);
    memcpy(pid.land_name, in->town, 8);
    pid.player_id = in->player_id;
    pid.land_id = in->land_id;
    idx = mNpc_GetAnimalMemoryIdx(&pid, a->memories, ANIMAL_MEMORY_NUM);
    if (idx < 0) {
        idx = mNpc_ForceGetFreeAnimalMemoryIdx(a, a->memories, ANIMAL_MEMORY_NUM);
        if (idx < 0) {
            return;
        }
        mNpc_SetAnimalMemory(&pid, &a->id, &a->memories[idx]);
    }
    memory_in(&a->memories[idx], in);
    if (getenv("PC_MP_DEBUG") != NULL) {
        printf("[mp] villager %d remembers a visitor: friendship %d, last talk %d/%d %02d:%02d\n", in->villager,
               in->friendship, in->time[5], in->time[3], in->time[2], in->time[1]);
    }
}

static mp_memory l_mem_in[MP_NPC_MAX];
static int l_mem_in_pending[MP_NPC_MAX];
static mp_memory l_mem_sent[MP_NPC_MAX];
static int l_mem_sent_valid[MP_NPC_MAX];

/* visitor: the host's memory of us (applied once the town is ours to walk in) */
void pc_mp_memory_visitor_received(const mp_memory* in) {
    if (memory_ok(in)) {
        l_mem_in[in->villager] = *in;
        l_mem_in_pending[in->villager] = TRUE;
        if (getenv("PC_MP_DEBUG") != NULL) {
            printf("[mp] villager %d remembers us: friendship %d\n", in->villager, in->friendship);
        }
    }
}

void pc_mp_memory_reset(void) {
    memset(l_mem_in_pending, 0, sizeof(l_mem_in_pending));
    memset(l_mem_sent_valid, 0, sizeof(l_mem_sent_valid));
}

/* visitor, a few times a second once arrived: what the host remembered goes in; a memory of ours that changed goes
 * out (one at a time). TRUE when out has one to send */
int pc_mp_memory_visitor_tick(mp_memory* out) {
    PersonalID_c* me;
    int v;

    if (Now_Private == NULL) {
        return FALSE;
    }
    me = &Now_Private->player_ID;
    for (v = 0; v < MP_NPC_MAX; v++) {
        Animal_c* a = Save_GetPointer(animals[v]);
        int idx;
        if (!l_mem_in_pending[v] || a->id.npc_id == EMPTY_NO) {
            continue;
        }
        l_mem_in_pending[v] = FALSE;
        idx = mNpc_GetAnimalMemoryIdx(me, a->memories, ANIMAL_MEMORY_NUM);
        if (idx < 0) {
            idx = mNpc_ForceGetFreeAnimalMemoryIdx(a, a->memories, ANIMAL_MEMORY_NUM);
            if (idx < 0) {
                continue;
            }
            mNpc_SetAnimalMemory(me, &a->id, &a->memories[idx]);
        }
        memory_in(&a->memories[idx], &l_mem_in[v]);
        memory_out(&l_mem_sent[v], v, &a->memories[idx]); /* what the host has: nothing to send back */
        l_mem_sent_valid[v] = TRUE;
    }
    for (v = 0; v < MP_NPC_MAX; v++) {
        Animal_c* a = Save_GetPointer(animals[v]);
        mp_memory now;
        int idx;
        if (a->id.npc_id == EMPTY_NO || (idx = mNpc_GetAnimalMemoryIdx(me, a->memories, ANIMAL_MEMORY_NUM)) < 0) {
            continue;
        }
        memory_out(&now, v, &a->memories[idx]);
        if (!l_mem_sent_valid[v] || memcmp(&now, &l_mem_sent[v], sizeof(now)) != 0) {
            if (getenv("PC_MP_DEBUG") != NULL) {
                printf("[mp] villager %d's memory of us changed (friendship %d): to the host\n", v, now.friendship);
            }
            l_mem_sent[v] = now;
            l_mem_sent_valid[v] = TRUE;
            *out = now;
            return TRUE;
        }
    }
    return FALSE;
}

/* ctl memtest IDX: villager IDX gets to know us (as a first talk does), friendship 30 */
void pc_mp_memory_dev_meet(int v) {
    Animal_c* a;
    int idx;
    if (v < 0 || v >= MP_NPC_MAX || Now_Private == NULL || (a = Save_GetPointer(animals[v]))->id.npc_id == EMPTY_NO) {
        return;
    }
    idx = mNpc_GetAnimalMemoryIdx(&Now_Private->player_ID, a->memories, ANIMAL_MEMORY_NUM);
    if (idx < 0) {
        idx = mNpc_ForceGetFreeAnimalMemoryIdx(a, a->memories, ANIMAL_MEMORY_NUM);
        mNpc_SetAnimalMemory(&Now_Private->player_ID, &a->id, &a->memories[idx]);
    }
    a->memories[idx].friendship = 30;
    printf("[devtest] villager %d knows us (friendship 30)\n", v);
}
