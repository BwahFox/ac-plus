/* ac-plus: villagers having a cup at the Roost.
 *
 * One or two of the town's villagers drop in at the Roost each day, each for an hour or two somewhere
 * between 9 AM and midnight (who, when and where they sit are picked from the date and the town). They are
 * the villagers themselves (ac_npc_ctrl.c_inc gives them this profile in the café), so they talk as ever,
 * favors and all; they sit on City Folk's chairs, look about and sip as at the cherry blossom party
 * (ac_hanami_npc0.c), and clap when K.K. finishes a song (ac_npc_kk_roost.c). */
#include "ac_roost_guest.h"

#include "m_common_data.h"
#include "m_npc.h"
#include "m_name_table.h"
#include "audio.h"

#include <stdio.h>
#include <stdlib.h>


static void aRGT_actor_ct(ACTOR* actorx, GAME* game);
static void aRGT_actor_dt(ACTOR* actorx, GAME* game);
static void aRGT_actor_move(ACTOR* actorx, GAME* game);
static void aRGT_actor_draw(ACTOR* actorx, GAME* game);
static void aRGT_actor_save(ACTOR* actorx, GAME* game);
static void aRGT_actor_init(ACTOR* actorx, GAME* game);

ACTOR_PROFILE Roost_Guest_Profile = {
    mAc_PROFILE_ROOST_GUEST,
    ACTOR_PART_NPC,
    ACTOR_STATE_NONE,
    EMPTY_NO,
    ACTOR_OBJ_BANK_KEEP,
    sizeof(ROOST_GUEST_ACTOR),
    &aRGT_actor_ct,
    &aRGT_actor_dt,
    &aRGT_actor_init,
    mActor_NONE_PROC1,
    &aRGT_actor_save,
};

enum {
    aRGT_ACT_WAIT,
    aRGT_ACT_CLAP,
    aRGT_ACT_DRINK,

    aRGT_ACT_NUM
};

/* City Folk's chairs at the Roost's tables (cf_sky/rooms/idr_cafe.cfroom): seat middle and facing */
typedef struct {
    f32 x, z;
    s16 angle;
} aRGT_seat_c;

static const aRGT_seat_c l_seat[] = {
    { 141.0f, 420.0f, DEG2SHORT_ANGLE2(90.0f) },   /* the end of the table, by the wall */
    { 258.0f, 420.0f, DEG2SHORT_ANGLE2(-90.0f) },  /* the other end */
    { 180.0f, 458.0f, DEG2SHORT_ANGLE2(180.0f) },  /* the front, left */
    { 220.0f, 458.0f, DEG2SHORT_ANGLE2(180.0f) },  /* the front, right */
};
#define aRGT_SEAT_NUM ((int)(sizeof(l_seat) / sizeof(l_seat[0])))
#define aRGT_SEAT_H 21.0f /* the seats, above the café floor */

extern int aKKR_songs_done; /* ac_npc_kk_roost.c */

/* Indoors the NPC clip is ac_npc2's, whose aNPC_THINK_SPECIAL is 6 and aNPC_ACT_SPECIAL 16 (ac_npc.h with
 * aNPC_NPC2); this file sees the field NPCs' numbering, which would run off npc2's tables */
#define aRGT_THINK_SPECIAL 6
#define aRGT_ACT_SPECIAL 16

static mActor_name_t l_guest_id[aRGT_GUEST_NUM]; /* who's in now (EMPTY_NO: nobody) */
static int l_guest_animal[aRGT_GUEST_NUM];        /* their Save_t animals[] index */
static int l_guest_seat[aRGT_GUEST_NUM];
static int l_guest_until[aRGT_GUEST_NUM]; /* minute of the day they leave */

#define aRGT_OPEN (9 * 60) /* their visits fall between 9 AM and midnight */
#define aRGT_CLOSE (24 * 60)
#define aRGT_STAY_MIN 60 /* and last an hour or two */
#define aRGT_STAY_MAX 120

int pc_favor_test_guest = -1;

static u32 aRGT_hash(u32 h, u32 n) {
    h ^= n + 0x9E3779B9u + (h << 6) + (h >> 2);
    h *= 0x85EBCA6Bu;
    return h ^ (h >> 13);
}

/* Today's visits, worked out once a day from the date and the town: who, when and at which chair */
static int l_plan_date = -1;
static u32 l_plan_town; /* and the town it was for (the title screen's demo town comes first) */
static int l_plan_favor = -1; /* dev: PC_FAVOR_TEST's guest when it was made */
static int l_plan_num;
static int l_plan_animal[aRGT_GUEST_NUM];
static int l_plan_from[aRGT_GUEST_NUM];
static int l_plan_until[aRGT_GUEST_NUM];
static int l_plan_seat[aRGT_GUEST_NUM];

static void aRGT_make_plan(void) {
    const lbRTC_time_c* t = Common_GetPointer(time.rtc_time);
    int date = t->year * 400 + t->month * 32 + t->day;
    int villagers[ANIMAL_NUM_MAX];
    int count = 0, i, n;
    u32 h;

    if (date == l_plan_date && Save_Get(land_info).id == l_plan_town && pc_favor_test_guest == l_plan_favor) {
        return;
    }
    l_plan_date = date;
    l_plan_town = Save_Get(land_info).id;
    l_plan_favor = pc_favor_test_guest;
    l_plan_num = 0;
    if (getenv("PC_NO_GUESTS") != NULL) { /* dev */
        return;
    }
    if (pc_favor_test_guest >= 0) { /* dev: PC_FAVOR_TEST (pc_m_card.c) seats this villager all day */
        l_plan_animal[0] = pc_favor_test_guest;
        l_plan_from[0] = 0;
        l_plan_until[0] = aRGT_CLOSE;
        l_plan_seat[0] = 0;
        l_plan_num = 1;
        return;
    }
    for (i = 0; i < ANIMAL_NUM_MAX; i++) {
        if (!mNpc_CheckFreeAnimalInfo(Save_GetPointer(animals[i]))) {
            villagers[count++] = i;
        }
    }
    if (count == 0) {
        return;
    }
    h = aRGT_hash(aRGT_hash(aRGT_hash(Save_Get(land_info).id, t->year), t->month), t->day);
    l_plan_num = count >= 2 ? 1 + (int)((h >> 3) & 1) : 1;
    for (n = 0; n < l_plan_num; n++) {
        int pick = (int)(aRGT_hash(h, 17 + n) % (u32)count);
        /* their visit: an hour or two, somewhere between 9 AM and midnight */
        int stay = aRGT_STAY_MIN + (int)(aRGT_hash(h, 53 + n) % (aRGT_STAY_MAX - aRGT_STAY_MIN + 1));
        int from = aRGT_OPEN + (int)(aRGT_hash(h, 61 + n) % (aRGT_CLOSE - aRGT_OPEN - stay + 1));

        l_plan_animal[n] = villagers[pick];
        villagers[pick] = villagers[--count]; /* not twice */
        l_plan_from[n] = from;
        l_plan_until[n] = from + stay;
        l_plan_seat[n] = (int)(aRGT_hash(h, 31 + n) % aRGT_SEAT_NUM);
        if (n == 1 && l_plan_seat[1] == l_plan_seat[0]) {
            l_plan_seat[1] = (l_plan_seat[0] + 1 + (int)(aRGT_hash(h, 41) % (aRGT_SEAT_NUM - 1))) % aRGT_SEAT_NUM;
        }
        if (getenv("PC_GUEST_LOG") != NULL) {
            printf("[Guest] villager %d visits %02d:%02d-%02d:%02d at chair %d\n", l_plan_animal[n], from / 60,
                   from % 60, (from + stay) / 60, (from + stay) % 60, l_plan_seat[n]);
        }
    }
}

int aRGT_regist_guests(void) {
    int now = Common_Get(time.now_sec) / 60;
    int here = 0, n;

    for (n = 0; n < aRGT_GUEST_NUM; n++) {
        l_guest_id[n] = EMPTY_NO;
    }
    aRGT_make_plan();
    for (n = 0; n < l_plan_num; n++) {
        if (now >= l_plan_from[n] && now < l_plan_until[n]) {
            l_guest_id[here] = Save_Get(animals[l_plan_animal[n]]).id.npc_id;
            l_guest_animal[here] = l_plan_animal[n];
            l_guest_seat[here] = l_plan_seat[n];
            l_guest_until[here] = l_plan_until[n];
            here++;
        }
    }
    return here;
}

int aRGT_at_roost_now(mActor_name_t npc_id) {
    int now = Common_Get(time.now_sec) / 60;
    int n;

    aRGT_make_plan();
    for (n = 0; n < l_plan_num; n++) {
        if (now >= l_plan_from[n] && now < l_plan_until[n] &&
            Save_Get(animals[l_plan_animal[n]]).id.npc_id == npc_id) {
            return TRUE;
        }
    }
    return FALSE;
}

static int aRGT_guest_no(mActor_name_t npc_id) {
    int n;

    for (n = 0; n < aRGT_GUEST_NUM; n++) {
        if (l_guest_id[n] != EMPTY_NO && l_guest_id[n] == npc_id) {
            return n;
        }
    }
    return -1;
}

int aRGT_is_guest(mActor_name_t npc_id) {
    extern int pc_museum_rooms_scene(void);

    return pc_museum_rooms_scene() == SCENE_MUSEUM_CAFE && aRGT_guest_no(npc_id) >= 0;
}

void aRGT_guest(int n, mActor_name_t* npc_id, int* animal_idx) {
    *npc_id = l_guest_id[n];
    *animal_idx = l_guest_animal[n];
}

void aRGT_seat_unit(int n, int* ut_x, int* ut_z) {
    const aRGT_seat_c* seat = &l_seat[l_guest_seat[n]];

    *ut_x = (int)(seat->x / 40.0f);
    *ut_z = (int)(seat->z / 40.0f);
}

/* --- sitting, sipping and clapping, as at the cherry blossom party (ac_hanami_npc0_talk.c_inc) --- */

static void aRGT_set_request_act(ROOST_GUEST_ACTOR* actor) {
    actor->npc_class.request.act_priority = 4;
    actor->npc_class.request.act_idx = aRGT_ACT_SPECIAL;
    actor->npc_class.request.act_type = aNPC_ACT_TYPE_SEARCH;
}

static void aRGT_make_cup(ACTOR* actorx, GAME* game) {
    ROOST_GUEST_ACTOR* actor = (ROOST_GUEST_ACTOR*)actorx;

    if (actor->npc_class.right_hand.item_actor_p == NULL) {
        ACTOR* cup = CLIP(tools_clip)->aTOL_birth_proc(TOOL_TUMBLER, aTOL_ACTION_S_TAKEOUT, actorx, game, -1, NULL);

        if (cup != NULL) {
            actor->npc_class.right_hand.item_actor_p = cup;
        }
    }
}

static void aRGT_wait(ROOST_GUEST_ACTOR* actor) {
    if (actor->npc_class.draw.main_animation_state == cKF_STATE_CONTINUE) {
        if (actor->npc_class.draw.loop_flag == 0) {
            actor->npc_class.action.step = aNPC_ACTION_END_STEP;
        } else {
            actor->npc_class.draw.loop_flag--;
        }
    }
}

static void aRGT_clap(ROOST_GUEST_ACTOR* actor) {
    aRGT_wait(actor);
    sAdo_OngenPos((u32)actor, actor->clap_se_no, &actor->npc_class.actor_class.world.position);
}

static void aRGT_drink(ROOST_GUEST_ACTOR* actor) {
    if (actor->npc_class.draw.main_animation_state == cKF_STATE_STOPPED) {
        actor->npc_class.action.step = aNPC_ACTION_END_STEP;
    }
}

static void aRGT_setupAction(ROOST_GUEST_ACTOR* actor, int action) {
    static aRGT_ACT_PROC process[] = { &aRGT_wait, &aRGT_clap, &aRGT_drink };
    static int anime[] = { aNPC_ANIM_SITDOWN_WAIT1, aNPC_ANIM_SITDOWN_CLAP1, aNPC_ANIM_SITDOWN_DRINK1 };
    static int anm_loop_base[] = { 2, 3, 1 };
    static f32 anm_loop_rnd[] = { 4.0f, 2.0f, 1.0f };
    static u8 clap_se_no[] = { NA_SE_2F, NA_SE_31, NA_SE_32, NA_SE_33 };

    actor->npc_class.action.step = 0;
    actor->action = action;
    actor->act_proc = process[action];
    actor->npc_class.draw.loop_flag = anm_loop_base[action] + RANDOM(anm_loop_rnd[action]);
    NPC_CLIP->animation_init_proc((ACTOR*)actor, anime[action], FALSE);
    if (action == aRGT_ACT_CLAP) {
        actor->clap_se_no = clap_se_no[RANDOM(ARRAY_COUNT(clap_se_no))];
    }
}

static void aRGT_act_proc(NPC_ACTOR* nactorx, GAME_PLAY* play, int type) {
    ROOST_GUEST_ACTOR* actor = (ROOST_GUEST_ACTOR*)nactorx;

    switch (type) {
        case 0: /* init */
            aRGT_setupAction(actor, aRGT_ACT_WAIT);
            break;
        case 1: /* change data */
            nactorx->action.act_obj = aNPC_ACT_OBJ_PLAYER;
            break;
        default: /* main */
            actor->act_proc(actor);
            break;
    }
}

static void aRGT_think_proc(NPC_ACTOR* nactorx, GAME_PLAY* play, int type) {
    ROOST_GUEST_ACTOR* actor = (ROOST_GUEST_ACTOR*)nactorx;

    if (type == 0) { /* init */
        nactorx->think.interrupt_flags = 0;
        nactorx->action.act_proc = &aRGT_act_proc;
        aRGT_set_request_act(actor);
        return;
    }
    if (aKKR_songs_done != actor->songs_seen) {
        /* K.K.'s song is over: a hand for him */
        actor->songs_seen = aKKR_songs_done;
        aRGT_setupAction(actor, aRGT_ACT_CLAP);
        return;
    }
    if (actor->npc_class.action.step == aNPC_ACTION_END_STEP) {
        if (actor->npc_class.action.idx == aRGT_ACT_SPECIAL) {
            aRGT_setupAction(actor, RANDOM(3) == 0 ? aRGT_ACT_DRINK : aRGT_ACT_WAIT);
        }
        aRGT_set_request_act(actor);
    }
}

static void aRGT_schedule_proc(NPC_ACTOR* nactorx, GAME_PLAY* play, int type) {
    ROOST_GUEST_ACTOR* actor = (ROOST_GUEST_ACTOR*)nactorx;
    ACTOR* actorx = (ACTOR*)nactorx;

    if (type == 0) { /* init */
        const aRGT_seat_c* seat;
        int n = aRGT_guest_no(actorx->npc_id);

        n = n >= 0 ? n : 0;
        actor->seat = l_guest_seat[n];
        actor->until = l_guest_until[n];
        seat = &l_seat[actor->seat];
        actor->songs_seen = aKKR_songs_done;
        nactorx->think.think_proc = &aRGT_think_proc;
        nactorx->condition_info.hide_request = FALSE;
        nactorx->palActorIgnoreTimer = -1;
        nactorx->talk_info.turn = aNPC_TALK_TURN_HEAD;
        nactorx->talk_info.default_animation = aNPC_ANIM_SITDOWN_WAIT1;
        nactorx->condition_info.demo_flg = aNPC_COND_DEMO_SKIP_HEAD_LOOKAT | aNPC_COND_DEMO_SKIP_FORWARD_CHECK |
                                           aNPC_COND_DEMO_SKIP_BGCHECK | aNPC_COND_DEMO_SKIP_MOVE_Y |
                                           aNPC_COND_DEMO_SKIP_MOVE_CIRCLE_REV | aNPC_COND_DEMO_SKIP_MOVE_RANGE_CHECK;
        nactorx->collision.check_kind = aNPC_BG_CHECK_TYPE_NONE;
        actorx->status_data.weight = MASSTYPE_HEAVY;
        actorx->world.position.x = seat->x;
        actorx->world.position.z = seat->z;
        {
            xyz_t floor = { 300.0f, 0.0f, 400.0f }; /* the café floor */

            actor->seat_y = mCoBG_GetBgY_OnlyCenter_FromWpos2(floor, 0.0f) + aRGT_SEAT_H;
            actorx->world.position.y = actor->seat_y;
        }
        actorx->gravity = 0.0f;
        actorx->position_speed.y = 0.0f;
        actorx->shape_info.rotation.y = actorx->world.angle.y = nactorx->movement.mv_angl = seat->angle;
        NPC_CLIP->think_proc(nactorx, play, aRGT_THINK_SPECIAL, aNPC_THINK_TYPE_INIT);
    } else {
        if (!NPC_CLIP->think_proc(nactorx, play, -1, aNPC_THINK_TYPE_CHK_INTERRUPT)) {
            NPC_CLIP->think_proc(nactorx, play, -1, aNPC_THINK_TYPE_MAIN);
        }
        aRGT_make_cup(actorx, (GAME*)play);
    }
}

static void aRGT_actor_ct(ACTOR* actorx, GAME* game) {
    static aNPC_ct_data_c ct_data = {
        /* no talk procs of their own: the villager's usual talk (as Normal_Npc's) */
        &aRGT_actor_move, &aRGT_actor_draw, aNPC_CT_SCHED_TYPE_SPECIAL, NULL, NULL, NULL, 0,
    };

    if (NPC_CLIP->birth_check_proc(actorx, game) == TRUE) {
        ((ROOST_GUEST_ACTOR*)actorx)->npc_class.schedule.schedule_proc = &aRGT_schedule_proc;
        NPC_CLIP->ct_proc(actorx, game, &ct_data);
    }
}

static void aRGT_actor_save(ACTOR* actorx, GAME* game) {
    NPC_CLIP->save_proc(actorx, game);
}

static void aRGT_actor_dt(ACTOR* actorx, GAME* game) {
    NPC_CLIP->dt_proc(actorx, game);
}

static void aRGT_actor_init(ACTOR* actorx, GAME* game) {
    NPC_CLIP->init_proc(actorx, game);
}

static void aRGT_actor_move(ACTOR* actorx, GAME* game) {
    ROOST_GUEST_ACTOR* actor = (ROOST_GUEST_ACTOR*)actorx;
    const aRGT_seat_c* seat = &l_seat[actor->seat];

    NPC_CLIP->move_proc(actorx, game);
    actorx->world.position.x = seat->x; /* in their chair, whatever bumps them */
    actorx->world.position.z = seat->z;
    actorx->world.position.y = actor->seat_y; /* talking swaps in its own demo flags, and with them gravity */
    actorx->position_speed.y = 0.0f;
    if (Common_Get(time.now_sec) / 60 >= actor->until) {
        actor->npc_class.condition_info.hide_request = TRUE; /* time to go */
    }
}

static void aRGT_actor_draw(ACTOR* actorx, GAME* game) {
    NPC_CLIP->draw_proc(actorx, game);
}
