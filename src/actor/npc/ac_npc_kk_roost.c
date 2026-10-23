/* ac-plus: K.K. Slider playing at the Roost.
 *
 * From 6 PM to midnight, except on Saturdays (when he plays in town as on the GameCube), K.K. sits
 * on his crate on the Roost's stage, where City Folk's stage light falls, and plays one song after
 * another: any of his regular ones, never the same twice in a row. No requests, no airchecks, and he
 * doesn't stop to talk, as New Horizons' K.K. before his show. The songs are the GameCube's live
 * versions, and his strumming, mouth and nods follow their beat as at his Saturday shows
 * (ac_mikanbox_clip.c_inc). The Roost's own music stays off while he plays, and the room's lights go down
 * with a light on him (pc_museum_rooms.c draws the room's spot). */
#include "ac_npc_kk_roost.h"

#include "m_common_data.h"
#include "m_bgm.h"
#include "m_rcp.h"
#include "audio.h"
#include "audio_defs.h"
#include "sys_matrix.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void aKKR_actor_ct(ACTOR* actorx, GAME* game);
static void aKKR_actor_dt(ACTOR* actorx, GAME* game);
static void aKKR_actor_move(ACTOR* actorx, GAME* game);
static void aKKR_actor_draw(ACTOR* actorx, GAME* game);
static void aKKR_actor_init(ACTOR* actorx, GAME* game);
static void aKKR_actor_save(ACTOR* actorx, GAME* game);

ACTOR_PROFILE Npc_Kk_Roost_Profile = {
    mAc_PROFILE_NPC_KK_ROOST,
    ACTOR_PART_NPC,
    ACTOR_STATE_NONE,
    SP_NPC_KK_ROOST,
    ACTOR_OBJ_BANK_KEEP,
    sizeof(NPC_KK_ROOST_ACTOR),
    &aKKR_actor_ct,
    &aKKR_actor_dt,
    &aKKR_actor_init,
    mActor_NONE_PROC1,
    &aKKR_actor_save,
};

/* The stage: the platform by the piano and amps (cf_sky/rooms/idr_cafe.cfroom), in front of them */
#define aKKR_STAGE_H 12.0f /* its top, above the café floor */
#define aKKR_SONGS 52      /* his regular songs: the ones a Saturday request can turn up (ac_mikanbox_clip.c_inc) */
#define aKKR_REST 150.0f   /* a breath between songs */
#define aKKR_STOP 0x168    /* as his Saturday show starts and stops its songs */

int aKKR_playing = FALSE;
int aKKR_songs_done = 0; /* songs played to the end: the guests clap (ac_roost_guest.c) */
f32 aKKR_show = 0.0f;

#define aKKR_SHOW_DIM 0.6f  /* how far the room's lights go down */
#define aKKR_SHOW_FADE 150.0f /* 60ths of a second to fade */

extern void pc_mEnv_SetShowDim(f32 dim); /* m_kankyo.c */

f32 aKKR_room_light(void) {
    f32 s = aKKR_show * aKKR_show * (3.0f - 2.0f * aKKR_show);

    return 1.0f - aKKR_SHOW_DIM * s;
}

extern Gfx obj_e_mikanbox_model[];
extern u16 obj_e_mikanbox_pal[];

int aKKR_on_stage_hours(void) {
    const lbRTC_time_c* t = Common_GetPointer(time.rtc_time);
    const char* day = getenv("PC_KK_DAY"); /* dev: PC_KK_DAY=M/D, he plays all that day (to try him out) */

    if (day != NULL && strchr(day, '/') != NULL && atoi(day) == t->month && atoi(strchr(day, '/') + 1) == t->day) {
        return TRUE;
    }
    return t->hour >= 18 && t->weekday != lbRTC_SATURDAY;
}

static void aKKR_song_start(NPC_KK_ROOST_ACTOR* kk) {
    int song;
    int tries;

    for (tries = 0, song = RANDOM(aKKR_SONGS); song == kk->last_song && tries < 8; tries++) {
        song = RANDOM(aKKR_SONGS);
    }
    kk->song = song;
    kk->last_song = song;
    kk->started = FALSE;
    aKKR_playing = TRUE;
    mBGMPsComp_make_ps_demo(BGM_TOTAKEKE_LIVE0 + song, aKKR_STOP);
    if (getenv("PC_KK_LOG") != NULL) {
        printf("[K.K.] song %d\n", song);
        fflush(stdout);
    }
}

static void aKKR_song_stop(NPC_KK_ROOST_ACTOR* kk) {
    if (kk->song >= 0) {
        mBGMPsComp_delete_ps_demo(BGM_TOTAKEKE_LIVE0 + kk->song, aKKR_STOP);
    }
    kk->song = -1;
    kk->rest = aKKR_REST;
    aKKR_playing = FALSE;
}

/* His strumming, mouth and nods on the song's beat, as at his Saturday shows (aMKBC_clip_sound_proc) */
static void aKKR_play(NPC_KK_ROOST_ACTOR* kk) {
    ACTOR* actorx = (ACTOR*)kk;
    NPC_ACTOR* npc = &kk->npc_class;
    StaffRollInfo_c info;

    sAdos_GetStaffRollInfo(&info);
    if (info.staffroll_part != STAFFROLL_PART_FINISH) {
        kk->started = TRUE;
    } else if (kk->started) {
        aKKR_songs_done++;
        aKKR_song_stop(kk); /* over */
        return;
    } else {
        return; /* still starting */
    }

    /* his six mouths; as a song starts the sound's value isn't one yet, and a stray one picked a texture from past
     * the table (garbage, a crash in the texture cache) */
    npc->draw.tex_anim[aNPC_TEX_ANIM_MOUTH].pattern = info.mouth_num >= 0 && info.mouth_num < 6 ? info.mouth_num : 0;
    if (info.blink_flag) {
        xyz_t pos = actorx->world.position;

        pos.y += 35.0f;
        pos.z += 40.0f;
        CLIP(npc_clip)->set_head_request_act_proc(npc, 4, aNPC_HEAD_TARGET_POS, NULL, &pos);
    }
    npc->draw.main_animation.keyframe.frame_control.speed = 0.0f;
    if (info.staffroll_part == STAFFROLL_PART_FADEOUT) {
        return; /* holds the last chord */
    }
    if (info.beat == 3) {
        if (npc->draw.animation_id != aNPC_ANIM_3HAKU_E1) {
            CLIP(npc_clip)->animation_init_proc(actorx, aNPC_ANIM_3HAKU_E1, FALSE);
        }
        npc->draw.main_animation.keyframe.frame_control.current_frame = 48.0f * (f32)info.percent;
    } else if (info.beat == 4) {
        if (npc->draw.animation_id != aNPC_ANIM_4HAKU_E1) {
            CLIP(npc_clip)->animation_init_proc(actorx, aNPC_ANIM_4HAKU_E1, FALSE);
        }
        npc->draw.main_animation.keyframe.frame_control.current_frame = 64.0f * (f32)info.percent;
    }
}

/* --- sitting on his crate, facing the room --- */

static void aKKR_act_proc(NPC_ACTOR* actorx, GAME_PLAY* play, int action) {
    if (action == 0) {
        CLIP(npc_clip)->animation_init_proc((ACTOR*)actorx, aNPC_ANIM_ENSOU_E1, FALSE);
        actorx->condition_info.demo_flg = aNPC_COND_DEMO_SKIP_KUTIPAKU | aNPC_COND_DEMO_SKIP_HEAD_LOOKAT;
        actorx->movement.mv_angl = 0; /* towards the room */
        actorx->action.step = 0;
    }
}

static void aKKR_think_proc(NPC_ACTOR* actorx, GAME_PLAY* play, int think_idx) {
    if (think_idx == 0) {
        actorx->action.act_proc = &aKKR_act_proc;
    }
    if (think_idx == 0 || actorx->action.step == aNPC_ACTION_END_STEP) {
        actorx->request.act_priority = 4;
        actorx->request.act_idx = 16;
        actorx->request.act_type = aNPC_ACT_TYPE_SEARCH;
    }
}

static void aKKR_schedule_proc(NPC_ACTOR* actorx, GAME_PLAY* play, int proc_idx) {
    if (proc_idx == 0) {
        actorx->think.think_proc = &aKKR_think_proc;
        actorx->condition_info.hide_request = FALSE;
        CLIP(npc_clip)->think_proc(actorx, play, aNPC_THINK_IN_BLOCK, aNPC_THINK_TYPE_INIT);
    } else if (CLIP(npc_clip)->think_proc(actorx, play, -1, aNPC_THINK_TYPE_CHK_INTERRUPT) == FALSE) {
        CLIP(npc_clip)->think_proc(actorx, play, -1, aNPC_THINK_TYPE_MAIN);
    }
}

/* A performer: no talking */
static void aKKR_talk_request(ACTOR* actorx, GAME* game) {
}

static int aKKR_talk_init(ACTOR* actorx, GAME* game) {
    return TRUE;
}

static int aKKR_talk_end_chk(ACTOR* actorx, GAME* game) {
    return TRUE;
}

static void aKKR_actor_ct(ACTOR* actorx, GAME* game) {
    static aNPC_ct_data_c ct_data = {
        &aKKR_actor_move, &aKKR_actor_draw, 5, &aKKR_talk_request, &aKKR_talk_init, &aKKR_talk_end_chk, 0,
    };
    NPC_KK_ROOST_ACTOR* kk = (NPC_KK_ROOST_ACTOR*)actorx;

    if (getenv("PC_KK_LOG") != NULL) {
        printf("[K.K.] ct\n");
        fflush(stdout);
    }
    if (CLIP(npc_clip)->birth_check_proc(actorx, game) == TRUE) {
        kk->npc_class.schedule.schedule_proc = &aKKR_schedule_proc;
        CLIP(npc_clip)->ct_proc(actorx, game, &ct_data);
        kk->npc_class.palActorIgnoreTimer = -1;
        kk->npc_class.collision.check_kind = aNPC_BG_CHECK_TYPE_NONE;
        kk->npc_class.collision.pipe.attribute.pipe.radius = 30;
        kk->npc_class.talk_info.default_animation = aNPC_ANIM_ENSOU_E1;
        actorx->status_data.weight = 255;
        actorx->world.position.x = aKKR_STAGE_X;
        actorx->world.position.z = aKKR_STAGE_Z;
        {
            /* the café floor beside the stage (the platform itself is walled off in the room's collision) */
            xyz_t floor = { 300.0f, 0.0f, 300.0f };

            kk->floor_y = mCoBG_GetBgY_OnlyCenter_FromWpos2(floor, 0.0f) + aKKR_STAGE_H;
        }
        actorx->world.position.y = kk->floor_y; /* his sitting animation puts him on the crate */
        actorx->shape_info.rotation.y = actorx->world.angle.y = 0;
        actorx->position_speed.y = 0.0f;
        actorx->gravity = 0.0f;
        actorx->max_velocity_y = 0.0f;
        kk->song = -1;
        kk->last_song = -1;
        kk->rest = 60.0f; /* settles in, then starts */
        aKKR_show = 0.0f;
        Light_point_ct(&kk->spot, aKKR_STAGE_X, kk->floor_y + 60.0f, aKKR_STAGE_Z + 30.0f, 255, 235, 190, 0);
        kk->spot_list = Global_light_list_new(game, &((GAME_PLAY*)game)->global_light, &kk->spot);
    }
}

static void aKKR_actor_save(ACTOR* actorx, GAME* game) {
    CLIP(npc_clip)->save_proc(actorx, game);
}

static void aKKR_actor_dt(ACTOR* actorx, GAME* game) {
    NPC_KK_ROOST_ACTOR* kk = (NPC_KK_ROOST_ACTOR*)actorx;

    aKKR_song_stop(kk);
    aKKR_show = 0.0f;
    if (kk->spot_list != NULL) {
        Global_light_list_delete(&((GAME_PLAY*)game)->global_light, kk->spot_list);
        kk->spot_list = NULL;
    }
    CLIP(npc_clip)->dt_proc(actorx, game);
}

static void aKKR_actor_init(ACTOR* actorx, GAME* game) {
    CLIP(npc_clip)->init_proc(actorx, game);
}

static void aKKR_actor_move(ACTOR* actorx, GAME* game) {
    NPC_KK_ROOST_ACTOR* kk = (NPC_KK_ROOST_ACTOR*)actorx;

    CLIP(npc_clip)->move_proc(actorx, game);
    actorx->world.position.x = aKKR_STAGE_X; /* on his crate, whatever bumps him */
    actorx->world.position.z = aKKR_STAGE_Z;
    actorx->world.position.y = kk->floor_y; /* his sitting animation puts him on the crate */
    actorx->shape_info.rotation.y = actorx->world.angle.y = 0;
    if (kk->song >= 0) {
        aKKR_play(kk);
    } else if (aKKR_on_stage_hours()) {
        kk->rest -= (f32)game->graph->dt_num_60fps_frames;
        if (kk->rest <= 0.0f) {
            aKKR_song_start(kk);
        }
    } else {
        kk->npc_class.condition_info.hide_request = TRUE; /* midnight: that was the last one */
    }

    /* the lights: down while he's on (between songs too), up again once he's done for the night */
    {
        f32 step = (f32)game->graph->dt_num_60fps_frames / aKKR_SHOW_FADE;
        int on = !kk->npc_class.condition_info.hide_request;

        aKKR_show = on ? MIN(1.0f, aKKR_show + step) : MAX(0.0f, aKKR_show - step);
        pc_mEnv_SetShowDim(aKKR_SHOW_DIM * aKKR_show * aKKR_show * (3.0f - 2.0f * aKKR_show));
        Light_point_ct(&kk->spot, aKKR_STAGE_X, kk->floor_y + 60.0f, aKKR_STAGE_Z + 30.0f, 255, 235, 190,
                       (s16)(220.0f * aKKR_show));
    }
}

static void aKKR_actor_draw(ACTOR* actorx, GAME* game) {
    NPC_KK_ROOST_ACTOR* kk = (NPC_KK_ROOST_ACTOR*)actorx;
    GRAPH* graph = game->graph;
    Mtx* mtx;

    CLIP(npc_clip)->draw_proc(actorx, game);
    if (kk->npc_class.condition_info.hide_flg) {
        return;
    }
    /* his crate, as in town (ac_mikanbox_draw.c_inc) */
    Matrix_translate(aKKR_STAGE_X, kk->floor_y, aKKR_STAGE_Z, MTX_LOAD);
    Matrix_scale(0.01f, 0.01f, 0.01f, MTX_MULT);
    mtx = _Matrix_to_Mtx_new(graph);
    if (mtx != NULL) {
        _texture_z_light_fog_prim_npc(graph);
        OPEN_POLY_OPA_DISP(graph);
        gDPSetTextureAdjustMode(POLY_OPA_DISP++, G_TA_DOLPHIN);
        gDPSetTexEdgeAlpha(POLY_OPA_DISP++, 127);
        gSPMatrix(POLY_OPA_DISP++, mtx, G_MTX_LOAD | G_MTX_NOPUSH);
        gSPSegment(POLY_OPA_DISP++, ANIME_1_TXT_SEG, obj_e_mikanbox_pal);
        gSPDisplayList(POLY_OPA_DISP++, obj_e_mikanbox_model);
        gDPSetTextureAdjustMode(POLY_OPA_DISP++, G_TA_N64);
        gDPSetTexEdgeAlpha(POLY_OPA_DISP++, 144);
        CLOSE_POLY_OPA_DISP(graph);
    }
}
