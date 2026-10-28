/* ac-plus: another player in an online visit.
 *
 * The engine has a single player actor, so the other players are puppets: the player's own skeleton and
 * animations, drawn with their face, skin and clothes (pc_mp.c keeps them, checked), placed and posed from the
 * state their game sends. A puppet only shows; it doesn't walk, collide or talk by itself. One per remote
 * player in the same scene; it goes away when they leave the scene or the session.
 * The tool in their hand is drawn at the hand the way Player_actor_Item_draw does (a model, or the item's own
 * skeleton and animation); an umbrella is the same umbrella actor villagers carry, a child of the puppet. */
#include "ac_mp_puppet.h"

#include "c_keyframe.h"
#include "libforest/gbi_extensions.h"
#include "m_actor_shadow.h"
#include "m_collision_obj.h"
#include "m_common_data.h"
#include "m_field_info.h"
#include "m_lib.h"
#include "m_play.h"
#include "m_player.h"
#include "m_player_lib.h"
#include "m_private.h"
#include "m_rcp.h"
#include "ac_tools.h"
#include "pc_mp.h"
#include "sys_matrix.h"

/* the fish models, as the hand-over item has them (a fish held up after the catch) */
#include "../src/actor/ac_gyoei_model.c_inc"

typedef struct mp_puppet_actor_s {
    ACTOR actor_class;
    int slot;
    int gender;
    cKF_SkeletonInfo_R_c kf0;
    cKF_SkeletonInfo_R_c kf1;
    s_xyz joint[mPlayer_JOINT_NUM + 1];
    s_xyz morph[mPlayer_JOINT_NUM + 1];
    s8 part_table[mPlayer_JOINT_NUM + 1];
    int anim0, anim1, part_idx;
    int posed;
    MtxF hand_mtx;
    int hand_set;
    cKF_SkeletonInfo_R_c item_kf;
    s_xyz item_joint[8];
    s_xyz item_morph[8];
    int item_shape, item_anim; /* what item_kf is set up for */
    ACTOR* umbrella;
    int umbrella_kind;
    ClObjPipe_c col_pipe; /* a body to bump into, as a villager has */
    int ghost;            /* no body while it overlaps the local player (see aMPP_actor_move) */
} MP_PUPPET_ACTOR;

extern cKF_Skeleton_R_c cKF_bs_r_boy_1;
extern cKF_Skeleton_R_c cKF_bs_r_grl_1;

static void aMPP_actor_ct(ACTOR* actorx, GAME* game);
static void aMPP_actor_dt(ACTOR* actorx, GAME* game);
static void aMPP_actor_move(ACTOR* actorx, GAME* game);
static void aMPP_actor_draw(ACTOR* actorx, GAME* game);

ACTOR_PROFILE Mp_Puppet_Profile = {
    mAc_PROFILE_MP_PUPPET,
    ACTOR_PART_CONTROL,
    /* despite the name, this flag moves an actor out of view too (Actor_info_call_actor): it follows its player
     * back into view */
    ACTOR_STATE_NO_MOVE_WHILE_CULLED | ACTOR_STATE_NO_DRAW_WHILE_CULLED,
    EMPTY_NO,
    ACTOR_OBJ_BANK_KEEP,
    sizeof(MP_PUPPET_ACTOR),
    &aMPP_actor_ct,
    &aMPP_actor_dt,
    &aMPP_actor_move,
    &aMPP_actor_draw,
    NULL,
};

/* the other player's body, for the local player only (villagers and Porter walk through it, so it never blocks their paths):
 * the local player walks around it like around a villager, and can't push it (it goes where
 * its own game says) */
static ClObjPipeData_c aMPP_pipe_data = { { ClObj_FLAG_CHECK | ClObj_FLAG_COLLIDE_PLAYER, 32, ClObj_TYPE_PIPE }, { 1 }, { 18, 40, 0, { 0, 0, 0 } } };
static StatusData_c aMPP_status_data = { 0, 18, 40, 0, MASSTYPE_HEAVY };

static void aMPP_skeleton(MP_PUPPET_ACTOR* pp, int gender) {
    pp->gender = gender;
    cKF_SkeletonInfo_R_ct(&pp->kf0, gender == mPr_SEX_FEMALE ? &cKF_bs_r_grl_1 : &cKF_bs_r_boy_1, NULL, pp->joint,
                          pp->morph);
    pp->kf1 = pp->kf0;
    pp->anim0 = pp->anim1 = pp->part_idx = -1;
    pp->posed = FALSE;
}

static void aMPP_actor_ct(ACTOR* actorx, GAME* game) {
    MP_PUPPET_ACTOR* pp = (MP_PUPPET_ACTOR*)actorx;
    pc_mp_remote* r;

    pp->slot = actorx->actor_specific;
    r = pc_mp_get_remote(pp->slot);
    actorx->scale.x = actorx->scale.y = actorx->scale.z = 0.01f;
    Shape_Info_init(actorx, 0.0f, &mAc_ActorShadowCircle, 18.0f, 18.0f);
    actorx->shape_info.ofs_y = 200.0f;
    aMPP_skeleton(pp, r != NULL ? r->gender : mPr_SEX_MALE);
    pp->item_shape = pp->item_anim = -1;
    pp->umbrella = NULL;
    pp->umbrella_kind = -1;
    pp->ghost = TRUE; /* solid once it has been apart from the local player */
    ClObjPipe_ct(game, &pp->col_pipe);
    ClObjPipe_set5(game, &pp->col_pipe, actorx, &aMPP_pipe_data);
    CollisionCheck_Status_set3(&actorx->status_data, &aMPP_status_data);
}

static void aMPP_umbrella_away(MP_PUPPET_ACTOR* pp) {
    /* the engine unlinks a child that's gone (Actor_dt), so only one that's still ours is touched */
    if (pp->umbrella != NULL && pp->actor_class.child_actor == pp->umbrella) {
        Actor_delete(pp->umbrella);
    }
    pp->umbrella = NULL;
    pp->umbrella_kind = -1;
}

/* the umbrella in their hand, if they have one out */
static void aMPP_umbrella(MP_PUPPET_ACTOR* pp, pc_mp_remote* r, GAME* game) {
    aTOL_Clip_c* tools = Common_Get(clip).tools_clip;

    if (pp->umbrella != NULL && (pp->umbrella_kind != r->umbrella || pp->actor_class.child_actor != pp->umbrella)) {
        aMPP_umbrella_away(pp);
    }
    if (r->umbrella < 0 || pp->umbrella != NULL || tools == NULL || tools->aTOL_birth_proc == NULL) {
        return;
    }
    pp->umbrella = tools->aTOL_birth_proc(r->umbrella, aTOL_ACTION_S_TAKEOUT, &pp->actor_class, game, -1, NULL);
    if (pp->umbrella != NULL) {
        pp->umbrella_kind = r->umbrella;
        pp->umbrella->world.position = pp->actor_class.world.position;
    }
}

/* the item's skeleton, posed by its animation at their frame */
static void aMPP_item_pose(MP_PUPPET_ACTOR* pp, pc_mp_remote* r) {
    if (r->item_shape < 0 || mPlib_Get_Item_DataPointerType(r->item_shape) != mPlayer_ITEM_DATA_TYPE_SKELETON ||
        r->item_anim < 0) {
        pp->item_shape = -1; /* a skeleton is only drawn posed by one of its own animations (pc_mp.c checks) */
        return;
    }
    if (r->item_shape != pp->item_shape || r->item_anim != pp->item_anim) {
        pp->item_shape = r->item_shape;
        pp->item_anim = r->item_anim;
        cKF_SkeletonInfo_R_ct(&pp->item_kf, (cKF_Skeleton_R_c*)mPlib_Get_Item_DataPointer(pp->item_shape), NULL,
                              pp->item_joint, pp->item_morph);
        if (pp->item_anim >= 0) {
            cKF_SkeletonInfo_R_init_standard_repeat_setframeandspeedandmorph(
                &pp->item_kf, (cKF_Animation_R_c*)mPlib_Get_Item_DataPointer(pp->item_anim), NULL, r->item_frame,
                0.0f, 0.0f);
        }
    }
    if (pp->item_anim >= 0) {
        pp->item_kf.frame_control.current_frame = r->item_frame;
        pp->item_kf.frame_control.speed = 0.0f;
        cKF_SkeletonInfo_R_play(&pp->item_kf);
    }
}

static void aMPP_actor_dt(ACTOR* actorx, GAME* game) {
    MP_PUPPET_ACTOR* pp = (MP_PUPPET_ACTOR*)actorx;
    pc_mp_remote* r = pc_mp_get_remote(pp->slot);

    aMPP_umbrella_away(pp);
    if (pp->item_shape >= 0) {
        cKF_SkeletonInfo_R_dt(&pp->item_kf);
    }

    if (r != NULL && r->actor == actorx) {
        r->actor = NULL;
    }
    cKF_SkeletonInfo_R_dt(&pp->kf0);
    cKF_SkeletonInfo_R_dt(&pp->kf1);
    ClObjPipe_dt(game, &pp->col_pipe);
}

static void aMPP_actor_move(ACTOR* actorx, GAME* game) {
    MP_PUPPET_ACTOR* pp = (MP_PUPPET_ACTOR*)actorx;
    GAME_PLAY* play = (GAME_PLAY*)game;
    pc_mp_remote* r = pc_mp_get_remote(pp->slot);
    xyz_t* pos = &actorx->world.position;
    f32 dx, dy, dz;

    if (r == NULL || r->actor != actorx || !r->visible || r->scene != play->scene_id || r->field != mFI_GetFieldId()) {
        if (r != NULL && r->actor == actorx) {
            r->actor = NULL;
        }
        Actor_delete(actorx);
        return;
    }
    if (r->gender != pp->gender) {
        aMPP_skeleton(pp, r->gender);
    }

    /* follow the reported position; jump if it's far (a warp, or a late start) */
    dx = r->x - pos->x, dy = r->y - pos->y, dz = r->z - pos->z;
    if (dx * dx + dy * dy + dz * dz > 120.0f * 120.0f) {
        pos->x = r->x, pos->y = r->y, pos->z = r->z;
    } else {
        pos->x += dx * 0.5f, pos->y += dy * 0.5f, pos->z += dz * 0.5f;
    }
    actorx->shape_info.rotation.y = r->angle_y;
    actorx->world.angle.y = r->angle_y;

    /* the same animations and frames as the real player shows (indices were checked in pc_mp.c) */
    if (r->part_table != pp->part_idx) {
        pp->part_idx = r->part_table;
        mPlib_DMA_player_Part_Table(pp->part_table, pp->part_idx);
    }
    if (r->anim0 != pp->anim0) {
        pp->anim0 = r->anim0;
        cKF_SkeletonInfo_R_init_standard_repeat_setframeandspeedandmorph(
            &pp->kf0, mPlib_Get_Pointer_Animation(pp->anim0), NULL, r->frame0, 0.0f, 0.0f);
    }
    if (r->anim1 != pp->anim1) {
        pp->anim1 = r->anim1;
        cKF_SkeletonInfo_R_init_standard_repeat_setframeandspeedandmorph(
            &pp->kf1, mPlib_Get_Pointer_Animation(pp->anim1), NULL, r->frame1, 0.0f, 0.0f);
    }
    pp->kf0.frame_control.current_frame = r->frame0;
    pp->kf1.frame_control.current_frame = r->frame1;
    pp->kf0.frame_control.speed = 0.0f;
    pp->kf1.frame_control.speed = 0.0f;
    cKF_SkeletonInfo_R_combine_play(&pp->kf0, &pp->kf1, pp->part_table);
    pp->posed = TRUE;
    aMPP_item_pose(pp, r);
    aMPP_umbrella(pp, r, game);
    CollisionCheck_Uty_ActorWorldPosSetPipeC(actorx, &pp->col_pipe);

    /* a heavy body that turns up on top of the local player (off the train, a warp, lag) would shove them sideways,
     * and could wedge them in a wall or the station. Until the two have stepped apart it has no body. */
    {
        PLAYER_ACTOR* player = GET_PLAYER_ACTOR(play);

        if (player != NULL) {
            f32 px = player->actor_class.world.position.x - pos->x;
            f32 pz = player->actor_class.world.position.z - pos->z;
            f32 d2 = px * px + pz * pz;

            if (d2 < 24.0f * 24.0f) {
                pp->ghost = TRUE;
            } else if (d2 > 40.0f * 40.0f) {
                pp->ghost = FALSE;
            }
        }
    }
    if (!pp->ghost) {
        CollisionCheck_setOC(game, &play->collision_check, &pp->col_pipe.collision_obj);
    }
}

/* the right hand (joint 20, as Player_actor_draw_After_hand) */
static int aMPP_draw_after(GAME* game, cKF_SkeletonInfo_R_c* kf, int joint_no, Gfx** gfx_pp, u8* work_flag,
                           void* arg, s_xyz* rot, xyz_t* pos) {
    MP_PUPPET_ACTOR* pp = (MP_PUPPET_ACTOR*)arg;
    if (joint_no == 20) {
        Matrix_get(&pp->hand_mtx);
        pp->hand_set = TRUE;
    }
    return TRUE;
}

/* a fish they caught, held up in their hands (as the player shows it: ac_handOverItem_draw.c_inc's fish) */
static void aMPP_draw_catch(MP_PUPPET_ACTOR* pp, pc_mp_remote* r, GAME* game) {
    GRAPH* graph = game->graph;
    int type = r->catch_item - ITM_FISH_START;
    Mtx* mtx;

    if (r->catch_item == 0 || type < 0 || type >= (int)(sizeof(aGYO_displayList) / sizeof(aGYO_displayList[0])) ||
        aGYO_displayList[type] == NULL) {
        return;
    }
    Matrix_push();
    Matrix_put(&pp->hand_mtx);
    Matrix_RotateZ(DEG2SHORT_ANGLE(90.0f), MTX_MULT);
    mtx = _Matrix_to_Mtx_new(graph);
    if (mtx != NULL) {
        _texture_z_light_fog_prim(graph);
        OPEN_POLY_OPA_DISP(graph);
        gSPMatrix(POLY_OPA_DISP++, mtx, G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
        gSPDisplayList(POLY_OPA_DISP++, aGYO_displayList[type]->dl_a);
        CLOSE_POLY_OPA_DISP(graph);
    }
    Matrix_pull();
}

static void aMPP_draw_item(MP_PUPPET_ACTOR* pp, pc_mp_remote* r, GAME* game) {
    GRAPH* graph = game->graph;
    Mtx* mtx;

    if (!pp->hand_set) {
        return;
    }
    aMPP_draw_catch(pp, r, game);
    if (pp->umbrella != NULL && pp->actor_class.child_actor == pp->umbrella) {
        TOOLS_ACTOR* tool = (TOOLS_ACTOR*)pp->umbrella;
        tool->matrix_work = pp->hand_mtx;
        tool->init_matrix = TRUE;
        return;
    }
    if (r->item_main == mPlayer_ITEM_MAIN_NONE || r->item_shape < 0) {
        return;
    }
    Matrix_push();
    Matrix_put(&pp->hand_mtx);
    mtx = _Matrix_to_Mtx_new(graph);
    if (mtx != NULL) {
        _texture_z_light_fog_prim(graph);
        OPEN_POLY_OPA_DISP(graph);
        gSPMatrix(POLY_OPA_DISP++, mtx, G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
        CLOSE_POLY_OPA_DISP(graph);
        if (mPlib_Get_Item_DataPointerType(r->item_shape) == mPlayer_ITEM_DATA_TYPE_GFX) {
            OPEN_POLY_OPA_DISP(graph);
            gSPDisplayList(POLY_OPA_DISP++, mPlib_Get_Item_DataPointer(r->item_shape));
            CLOSE_POLY_OPA_DISP(graph);
        } else if (pp->item_shape == r->item_shape) {
            Mtx* item_mtx = (Mtx*)GRAPH_ALLOC_TYPE(graph, Mtx, pp->item_kf.skeleton->num_shown_joints);
            if (item_mtx != NULL) {
                cKF_Si3_draw_R_SV(game, &pp->item_kf, item_mtx, NULL, NULL, &pp->actor_class);
            }
        }
    }
    Matrix_pull();
}

static void aMPP_actor_draw(ACTOR* actorx, GAME* game) {
    MP_PUPPET_ACTOR* pp = (MP_PUPPET_ACTOR*)actorx;
    pc_mp_remote* r = pc_mp_get_remote(pp->slot);
    GRAPH* graph = game->graph;
    Mtx* mtx;

    if (r == NULL || !pp->posed || !r->has_look) {
        return;
    }
    mtx = (Mtx*)GRAPH_ALLOC_TYPE(graph, Mtx, pp->kf0.skeleton->num_shown_joints);
    if (mtx == NULL) {
        return;
    }
    _texture_z_light_fog_prim(graph);
    OPEN_POLY_OPA_DISP(graph);
    gSPSegment(POLY_OPA_DISP++, ANIME_1_TXT_SEG, r->face_tex + r->eye * 0x100);
    gSPSegment(POLY_OPA_DISP++, ANIME_2_TXT_SEG, r->face_tex + (mPlayer_EYE_TEX_NUM + r->mouth) * 0x100);
    gSPSegment(POLY_OPA_DISP++, ANIME_3_TXT_SEG, r->cloth_tex);
    gSPSegment(POLY_OPA_DISP++, ANIME_4_TXT_SEG, r->cloth_pal);
    gSPSegment(POLY_OPA_DISP++, ANIME_5_TXT_SEG, r->face_pal);
    CLOSE_POLY_OPA_DISP(graph);
    pp->hand_set = FALSE;
    cKF_Si3_draw_R_SV(game, &pp->kf0, mtx, NULL, aMPP_draw_after, actorx);
    aMPP_draw_item(pp, r, game);
}
