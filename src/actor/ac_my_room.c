#include "ac_my_room.h"

#include "m_bgm.h"
#include "m_name_table.h"
#include "ac_furniture.h"
#include "f_furniture.h"
#include "m_common_data.h"
#include "libultra/libultra.h"
#include "m_malloc.h"
#include "m_cockroach.h"
#include "m_msg.h"
#include "dolphin/card.h"
#include "m_string.h"
#include "m_house.h"
#include "m_player_lib.h"
#include "GBA2/gba2.h"
#include "m_debug.h"
#include "m_mark_room.h"
#include "m_police_box.h"
#include "sys_matrix.h"
#include "m_rcp.h"
#include <stdio.h>
#ifdef TARGET_PC
#include "pc_storage.h"
#include "pc_dresser.h"
#include "pc_skin.h"
#include "pc_outdoor_ftr.h"
#include "pc_sky.h"
#endif

#ifdef TARGET_PC
/* Outdoors the grid is the whole town (pc_outdoor_ftr.c) instead of one room block */
static int aMR_pc_field;
static int aMR_pc_home; /* field mode in the player's own town (another town's or the island's: a throwaway grid) */
/* Field mode: the acre the player is in (its furniture and the eight acres' around are loaded), -1 at first */
static int aMR_pc_acre_x = -1;
static int aMR_pc_acre_z = -1;
static int aMR_ut_x_num = UT_X_NUM;
static int aMR_ut_z_num = UT_Z_NUM;
#define aMR_UT_X_NUM aMR_ut_x_num
#define aMR_UT_Z_NUM aMR_ut_z_num
#define aMR_UT_TOTAL_NUM (aMR_ut_x_num * aMR_ut_z_num)
#define aMR_LOCAL_UT(ut) (aMR_pc_field ? (ut) : ((ut) & 0xF))
#else
#define aMR_UT_X_NUM UT_X_NUM
#define aMR_UT_Z_NUM UT_Z_NUM
#define aMR_UT_TOTAL_NUM UT_TOTAL_NUM
#define aMR_LOCAL_UT(ut) ((ut) & 0xF)
#endif

enum {
    aMR_ICON_LEAF,
    aMR_ICON_HANIWA,
    aMR_ICON_CLOTH,
    aMR_ICON_UMBRELLA,
    aMR_ICON_BONE,
    aMR_ICON_DIARY,

    aMR_ICON_NUM
};

/* Inclusive bounds */
#define aMR_MIN_BOUND 1
#define aMR_MAX_BOUND 8

#ifdef TARGET_PC
/* Kinds of furniture loaded at once: outdoors every loaded piece may be a different one (pc_outdoor_ftr.c) */
#define aMR_FTR_BANK_NUM 1280
#define aMR_NO_BANK 0xFFFF
typedef u16 aMR_bank_idx_t;
#else
#define aMR_FTR_BANK_NUM 100
#define aMR_NO_BANK 255
typedef u8 aMR_bank_idx_t;
#endif
#define aMR_FTR_BANK_SIZE 0x800

#define aMR_BOUNDS_OK(x, z) \
    ((x) > (aMR_MIN_BOUND - 1) && (x) < (aMR_MAX_BOUND + 1) && (z) > (aMR_MIN_BOUND - 1) && (z) < (aMR_MAX_BOUND + 1))

enum {
    aMR_MSG_STATE_NONE,
    aMR_MSG_STATE_WAIT_MSG,
    aMR_MSG_STATE_OWNER_NO_ITEM,
    aMR_MSG_STATE_OWNER_EXIST_ITEM,
    aMR_MSG_STATE_OWNER_EXIST_ITEM_ITEM_COUNT2,
    aMR_MSG_STATE_OWNER_WHICH_PUT_OUT1,
    aMR_MSG_STATE_OWNER_WHICH_PUT_OUT2,
    aMR_MSG_STATE_OWNER_WHICH_PUT_OUT3,
    aMR_MSG_STATE_OWNER_WAIT_WHICH_PUT_OUT,
    aMR_MSG_STATE_OTHER_NO_ITEM,
    aMR_MSG_STATE_OTHER_EXIST_ITEM1,
    aMR_MSG_STATE_OTHER_EXIST_ITEM2,
    aMR_MSG_STATE_OTHER_EXIST_ITEM3,
    aMR_MSG_STATE_FULL_PLAYER_ITEM,
    aMR_MSG_STATE_OPEN_SUBMENU,
    aMR_MSG_STATE_ITEM_PUT_IN,
    aMR_MSG_STATE_WAIT_CLOSE_FTR,
    aMR_MSG_STATE_REQUEST_CLOSE_FTR,
    aMR_MSG_STATE_REQUEST_FULL_BAG,
    aMR_MSG_STATE_CAN_NOT_CLEAN,
    aMR_MSG_STATE_CAN_NOT_CLEAN_MD,
    aMR_MSG_STATE_OPEN_SET_ITEM_SUBMENU,
    aMR_MSG_STATE_ITEM_SET_SUBMENU,
    aMR_MSG_STATE_WAIT_MD,
    aMR_MSG_STATE_OWNER_NO_MD,
    aMR_MSG_STATE_OWNER_EXIST_MD,
    aMR_MSG_STATE_OWNER_EXIST_MD2,
    aMR_MSG_STATE_OTHER_NO_MD,
    aMR_MSG_STATE_OTHER_EXIST_MD,
    aMR_MSG_STATE_FULL_PLAYER_MD,
    aMR_MSG_STATE_OPENMD_SUBMENU,
    aMR_MSG_STATE_MD_PUT_IN,
    aMR_MSG_STATE_OPEN_MUSIC_BOX,
    aMR_MSG_STATE_MUSIC_BOX_MD_PUT_IN,
    aMR_MSG_STATE_WAIT_CLOSE_MD,
    aMR_MSG_STATE_REQUEST_CLOSE_MD,
    aMR_MSG_STATE_REQUEST_FULL_BAG_MD,
    aMR_MSG_STATE_CAN_NOT_MD_CLEAN,
    aMR_MSG_STATE_OPEN_EXCHANGE_MD_SUBMENU,
    aMR_MSG_STATE_MD_EXCHANGE_SUBMENU,
    aMR_MSG_STATE_QQQ_EMULATOR,
    aMR_MSG_STATE_EXPLAIN_EMULATOR,
    aMR_MSG_STATE_QQQ_EMULATOR_MEMORY1,
    aMR_MSG_STATE_EXPLAIN_EMULATOR_MEMORY,
    aMR_MSG_STATE_QQQ_EMULATOR_MEMORY2,
    aMR_MSG_STATE_QQQ_EMULATOR_MEMORY_OVER3,
    aMR_MSG_STATE_QQQ_REPEAT_DISPLAY1,
    aMR_MSG_STATE_QQQ_REPEAT_DISPLAY1_2,
    aMR_MSG_STATE_QQQ_REPEAT_DISPLAY2,
    aMR_MSG_STATE_QQQ_REPEAT_DISPLAY2_2,
    aMR_MSG_STATE_NO_PACK_NO_DATA,
    aMR_MSG_STATE_WARNING_CANNOT_MAKE_SAVE_FILE,
    aMR_MSG_STATE_NO_PACK_NO_DATA1,
    aMR_MSG_STATE_NO_PACK_NO_DATA2,
    aMR_MSG_STATE_HITOKOTO,
    aMR_MSG_STATE_HITOKOTO1,
    aMR_MSG_STATE_HITOKOTO2,
    aMR_MSG_STATE_SAVE_FAMICOM,
    aMR_MSG_STATE_WAIT_FAMICOM_BATU,
    aMR_MSG_STATE_PREPARE_COMMUNICATION,
    aMR_MSG_STATE_NOT_CONNECT_AGB,
    aMR_MSG_STATE_CHECK_AGB_PROGRAM,
    aMR_MSG_STATE_DELETE_AGB_PROGRAMQ,
    aMR_MSG_STATE_START_EMU_DOWN_LOAD,
    aMR_MSG_STATE_RECHECK_AGB_CONNECT,

    aMR_MSG_STATE_NUM
};

static void My_Room_Actor_ct(ACTOR*, GAME*);
static void My_Room_Actor_dt(ACTOR*, GAME*);
static void My_Room_Actor_move(ACTOR*, GAME*);
static void My_Room_Actor_draw(ACTOR*, GAME*);

// clang-format off
ACTOR_PROFILE My_Room_Profile = {
    mAc_PROFILE_MY_ROOM,
    ACTOR_PART_BG,
    ACTOR_STATE_CAN_MOVE_IN_DEMO_SCENES | ACTOR_STATE_NO_DRAW_WHILE_CULLED | ACTOR_STATE_NO_MOVE_WHILE_CULLED,
    ETC_MY_ROOM,
    ACTOR_OBJ_BANK_KEEP,
    sizeof(MY_ROOM_ACTOR),
    &My_Room_Actor_ct,
    &My_Room_Actor_dt,
    &My_Room_Actor_move,
    &My_Room_Actor_draw,
    NULL
};
// clang-format on

#include "../src/actor/ac_furniture_data.c_inc"
#include "../src/actor/ac_my_room_data.c_inc"

typedef struct my_room_work_s {
    FTR_ACTOR* ftr_actor_list;
    u8* used_list;
    int list_size;
} aMR_work_c;

static aMR_work_c l_aMR_work;
static aMR_bank_idx_t l_bank_index_table[FTR_NUM];
static u8* l_bank_address_table[aMR_FTR_BANK_NUM];

// clang-format off
const aMR_contact_info_c l_cntInf_default = {
    FALSE,
    100,
    0,
    0,
    0.0f,
    NULL,
    { 0.0f, 0.0f },
    { 0.0f, 0.0f },
    0
};
// clang-format on
const f32 aMR_angle_table[4] = { 0.0f, 90.0f, 180.0f, 270.0f };
const u8 l_typeB0_table[4] = { aFTR_SHAPE_TYPEB_0, aFTR_SHAPE_TYPEB_90, aFTR_SHAPE_TYPEB_180, aFTR_SHAPE_TYPEB_270 };

static int aMR_UnitNum2FtrItemNoFtrID(mActor_name_t* item_no, int* ftr_id, int ut_x, int ut_z, int layer);
static void aMR_ReserveDefaultBgm(ACTOR* actorx, FTR_ACTOR* ftr_actor);
static void aMR_ChangeMDBgm(ACTOR* actorx, FTR_ACTOR* ftr_actor);
static int aMR_SetFurniture2FG(FTR_ACTOR* ftr_actor, xyz_t pos, int on_flag);
#ifdef TARGET_PC
static void aMR_pc_set_grid(int field);
static void aMR_pc_field_ct(void);
static void aMR_pc_field_dt(void);
static void aMR_pc_draw_items_on_top(GAME* game);
static void aMR_pc_field_stream(ACTOR* actorx, GAME* game);
static void aMR_pc_full_acre_notice(int from_ut, int to_ut);
static void aMR_pc_md_range(MY_ROOM_ACTOR* my_room, FTR_ACTOR* md_ftr_actor, GAME* game);
static void aMR_pc_take_items(FTR_ACTOR* ftr_actor);
static int aMR_pc_md_far; /* outdoors: the playing stereo is out of earshot, the hourly music is back */
#define aMR_PC_MD_FAR (12.5f * mFI_UT_WORLDSIZE_X_F)
#define aMR_PC_MD_NEAR (11.0f * mFI_UT_WORLDSIZE_X_F)
static f32 aMR_GetDistXZ(xyz_t* pos0, xyz_t* pos1);
#endif
static int aMR_FtrIdx2ChangeFtrSwitch(ACTOR* actorx, int ftr_id);
static aFTR_PROFILE* aMR_GetFurnitureProfile(u16 ftr_no);
static mActor_name_t* aMR_GetLayerTopFg(s16 layer);
static void aMR_TidyItemInFurniture(FTR_ACTOR* ftr_actor);
static int aMR_GetItemCountInFurniture(FTR_ACTOR* ftr_actor);
static int aMR_ItemPutInFurniture(FTR_ACTOR* ftr_actor, mActor_name_t item);
static void aMR_AllMDSwitchOff(void);
static void aMR_OneMDSwitchOn_TheOtherSwitchOff(FTR_ACTOR* ftr_actor);
static void aMR_ReserveBgm(ACTOR* actorx, int bgm_no, FTR_ACTOR* ftr_actor, s16 timer);
static void aMR_SetMDFtrDemoData(MY_ROOM_ACTOR* my_room, FTR_ACTOR* ftr_actor, aMR_contact_info_c* contact_info);
static int aMR_CheckHikidashi(FTR_ACTOR* ftr_actor, PLAYER_ACTOR* player, aMR_contact_info_c* contact_info);
static int aMR_JudgeDemoStart(MY_ROOM_ACTOR* my_room, aMR_contact_info_c* contact_info, GAME* game,
                              PLAYER_ACTOR* player);
static void aMR_PlacePushFurniture(MY_ROOM_ACTOR* my_room, aMR_contact_info_c* contact_info, f32* point,
                                   PLAYER_ACTOR* player, GAME* game);
static void aMR_PlacePullFurniture(MY_ROOM_ACTOR* my_room, aMR_contact_info_c* contact_info, PLAYER_ACTOR* player,
                                   GAME* game);
static void aMR_PlaceKurukuruFurniture(MY_ROOM_ACTOR* my_room, aMR_contact_info_c* contact_info, PLAYER_ACTOR* player,
                                       GAME* game);
static void aMR_SitDownFurniture(MY_ROOM_ACTOR* my_room, aMR_contact_info_c* contact_info, PLAYER_ACTOR* player,
                                 GAME* game);
static void aMR_JudgeGoToBed(MY_ROOM_ACTOR* my_room, aMR_contact_info_c* contact_info, PLAYER_ACTOR* player,
                             GAME* game);
static void aMR_SetMelodyData(u8* melody_data);
static void aMR_GokiInfoCt(ACTOR* actorx, GAME* game);
static void aMR_GokiInfoDt(void);
static void aMR_CheckFtrAndGoki(ACTOR* actorx, FTR_ACTOR* ftr_actor, GAME* game);
static void aMR_MakeGokiburi(xyz_t* pos, GAME* game, s16 arg);
static void aMR_RequestPlayerBikkuri(ACTOR* actorx, GAME* game);
static void aMR_GetFtrShape4Position(xyz_t* p0, xyz_t* p1, xyz_t* p2, xyz_t* p3, FTR_ACTOR* ftr_actor);
static u8 aMR_JudgeStickFull(int direct, GAME* game);
static int aMR_JudgeFurnitureMove(u8 type, int ut);
static int aMR_RequestItemToFitFurniture(ACTOR* actorx, FTR_ACTOR* ftr_actor);
static void aMR_SetPullMoveAnime(FTR_ACTOR* ftr_actor, GAME* game, MY_ROOM_ACTOR* my_room,
                                 aMR_contact_info_c* contact_info);
static int aMR_PullDirect2PushDirect(int pull_direct);
static void aMR_SetPushMoveAnime(FTR_ACTOR* ftr_actor, GAME* game, MY_ROOM_ACTOR* my_room,
                                 aMR_contact_info_c* contact_info);
static void aMR_MoveShapeCenter(FTR_ACTOR* ftr_actor);
static void aMR_RotateY(f32* xz, f32 amount);
static int aMR_3DStickNuetral(void);
static void aMR_SetNicePos(xyz_t* nice_pos, xyz_t player_pos, f32* col_start_xz, f32* col_end_xz,
                           aMR_contact_info_c* contact_info, int type);
static int aMR_GetPlayerDirect(const f32* normal_xz);
static int aMR_Get3dDirectStatus(int direct);

static aFTR_PROFILE* aMR_GetFurnitureProfile(u16 ftr_no) {
    if (ftr_no < FTR_NUM) {
        if (furniture_quality[ftr_no] != NULL) {
            return furniture_quality[ftr_no];
        }
    } else {
        return &iam_dummy;
    }

    return &iam_dummy;
}

static int aMR_GetItemCountInFurniture(FTR_ACTOR* ftr_actor) {
    int count = 0;
    int i;

    for (i = 0; i < aFTR_KEEP_ITEM_COUNT; i++) {
        if (ftr_actor->items[i] != EMPTY_NO) {
            count++;
        }
    }

    return count;
}

static void aMR_TidyItemInFurniture(FTR_ACTOR* ftr_actor) {
    mActor_name_t temp_items[aFTR_KEEP_ITEM_COUNT];
    mActor_name_t* dst_p;
    int i;

    for (i = 0; i < aFTR_KEEP_ITEM_COUNT; i++) {
        temp_items[i] = ftr_actor->items[i];
        ftr_actor->items[i] = EMPTY_NO;
    }

    dst_p = ftr_actor->items;
    for (i = 0; i < aFTR_KEEP_ITEM_COUNT; i++) {
        if (temp_items[i] != EMPTY_NO) {
            *dst_p++ = temp_items[i];
        }
    }
}

static int aMR_ItemPutInFurniture(FTR_ACTOR* ftr_actor, mActor_name_t item) {
    int i;

    for (i = 0; i < aFTR_KEEP_ITEM_COUNT; i++) {
        if (ftr_actor->items[i] == EMPTY_NO) {
            ftr_actor->items[i] = item;
            return TRUE;
        }
    }

    return FALSE;
}

static int aMR_ItemNo2IconNo(mActor_name_t item_no) {
    if (ITEM_IS_FTR(item_no)) {
        if (item_no >= FTR_START(FTR_NOG_COLLEGENOTE) && item_no <= FTR_END(FTR_IKE_NIKKI_WAFU1)) {
            return aMR_ICON_DIARY;
        }

        if (item_no >= HANIWA_START && item_no <= HANIWA_END) {
            return aMR_ICON_HANIWA;
        }

        if ((item_no >= FTR_UMBRELLA_START && item_no <= FTR_UMBRELLA_END) ||
            (item_no >= FTR_UMBRELLA_START && item_no <= FTR_MYUMBRELLA_END)) {
            return aMR_ICON_UMBRELLA;
        }

        if ((item_no >= FTR_CLOTH_START && item_no <= FTR_CLOTH_END) ||
            (item_no >= FTR_CLOTH_START && item_no <= FTR_CLOTH_MYMANNIQUIN_END)) {
            return aMR_ICON_CLOTH;
        }

        if (item_no >= FTR_DINO_START && item_no <= FTR_DINO_END) {
            return aMR_ICON_BONE;
        }

        return aMR_ICON_LEAF;
    }

    return aMR_ICON_LEAF;
}

extern Gfx* aMR_IconNo2Gfx1(int icon_no) {
    aMR_icon_display_data_c* icon;

    if (icon_no < 0) {
        icon_no = 0;
    } else if (icon_no >= aMR_ICON_NUM) {
        icon_no = aMR_ICON_NUM - 1;
    }

    return aMR_icon_display_data[icon_no].mat_gfx;
}

extern Gfx* aMR_IconNo2Gfx2(int icon_no) {
    if (icon_no < 0) {
        icon_no = 0;
    } else if (icon_no >= aMR_ICON_NUM) {
        icon_no = aMR_ICON_NUM - 1;
    }

    return aMR_icon_display_data[icon_no].vtx_gfx;
}

static mActor_name_t* aMR_GetLayerTopFg(s16 layer) {
#ifdef TARGET_PC
    if (aMR_pc_field) {
        /* chosen as the actor started: the next scene may already be set as it ends */
        return pc_oftr_grid(aMR_pc_home, layer);
    }
#endif
    return mFI_BkNum2UtFGTop_layer(0, 0, layer);
}

static u64* aMR_GetBitSwitchTable(int layer, MY_ROOM_ACTOR* my_room) {
    mActor_name_t field_id = mFI_GetFieldId();

    if (my_room->scene == SCENE_COTTAGE_MY) {
        return &(&Save_Get(island).cottage.room.layer_main)[layer].ftr_switch;
    }

    if ((mFI_GET_TYPE(field_id) == mFI_FIELD_PLAYER0_ROOM)) {
        int idx = (mFI_GetFieldId() - mFI_FIELD_PLAYER0_ROOM) & 3;

        if (aMR_CLIP != NULL && aMR_CLIP->my_room_actor_p != NULL) {
            int floor_no = mFI_GetPlayerHouseFloorNo(((MY_ROOM_ACTOR*)aMR_CLIP->my_room_actor_p)->scene);

            if (floor_no != -1) {
                return &(&mHm_FLOOR(idx, floor_no).layer_main)[layer].ftr_switch;
            }
        }
    }

    return NULL;
}

static u32* aMR_GetHaniwaStepSaveData(s16 layer, MY_ROOM_ACTOR* my_room) {
    mActor_name_t field_id = mFI_GetFieldId();

    if (my_room->scene == SCENE_COTTAGE_MY) {
        return (&Save_Get(island).cottage.room.layer_main)[layer].haniwa_step;
    }

    if ((mFI_GET_TYPE(field_id) == mFI_FIELD_PLAYER0_ROOM)) {
        int idx = (mFI_GetFieldId() - mFI_FIELD_PLAYER0_ROOM) & 3;
        int floor_no = mFI_GetPlayerHouseFloorNo(my_room->scene);

        if (floor_no != -1) {
            return (&mHm_FLOOR(idx, floor_no).layer_main)[layer].haniwa_step;
        }
    }

    return NULL;
}

static void aMR_ClearBitSwitch(FTR_ACTOR* ftr_actor) {
    ftr_actor->switch_bit = FALSE;
    ftr_actor->haniwa_step = -1;
}

static void aMR_GetSwitchBit(FTR_ACTOR* ftr_actor, s16 placing_flag, MY_ROOM_ACTOR* my_room) {
    aFTR_PROFILE* profile = aMR_GetFurnitureProfile(ftr_actor->name);

    if (placing_flag == TRUE) {
        ftr_actor->switch_bit = TRUE;
    } else {
        u64* ftr_bit_switch_table = aMR_GetBitSwitchTable(ftr_actor->layer, my_room);
        xyz_t pos = ftr_actor->position;
        int ut_x;
        int ut_z;

        pos.x -= 1.0f;
        pos.z -= 1.0f;

        if (ftr_bit_switch_table != NULL && ftr_actor->layer < mCoBG_LAYER2 &&
            mFI_Wpos2UtNum_inBlock(&ut_x, &ut_z, pos) && aMR_BOUNDS_OK(ut_x, ut_z)) {
            if (((*ftr_bit_switch_table) >> ((ut_x - 1 + (ut_z - 1) * 8) & 0x3F) & 1)) {
                ftr_actor->switch_bit = TRUE;
            } else {
                ftr_actor->switch_bit = FALSE;
            }
        } else if ((mFI_CheckShop() == TRUE || mFI_GetFieldId() == mFI_FIELD_ROOM_BROKER_SHOP) && profile != NULL &&
                   aFTR_CHECK_INTERACTION(profile->interaction_type, aFTR_INTERACTION_TYPE_MUSIC_DISK)) {
            ftr_actor->switch_bit = FALSE; // disable music players in shops
        } else {
            ftr_actor->switch_bit = TRUE;
        }
    }
}

static void aMR_GetHaniwaStep(FTR_ACTOR* ftr_actor, s16 placing_flag, int* step_idx, MY_ROOM_ACTOR* my_room) {
    if (placing_flag == TRUE) {
        ftr_actor->haniwa_step = -1;
    } else {
        int layer = ftr_actor->layer;
        int ut_x;
        int ut_z;

        if (layer < mCoBG_LAYER2 && mFI_Wpos2UtNum_inBlock(&ut_x, &ut_z, ftr_actor->position) &&
            aMR_BOUNDS_OK(ut_x, ut_z)) {
            u32* step_table = aMR_GetHaniwaStepSaveData(layer, my_room);

            if (step_table != NULL) {
                int shift = (ut_x - 1) * 4;
                int idx = ut_z - 1;

                ftr_actor->haniwa_step = (step_table[idx] >> shift) & 0xF;
            } else if (*step_idx < mFM_HANIWA_STEP_NUM) {
                u8* step = mFI_GetHaniwaStepBlock(0, 0);

                ftr_actor->haniwa_step = step[*step_idx];
                (*step_idx)++;
            } else {
                ftr_actor->haniwa_step = -1;
            }
        } else {
            ftr_actor->haniwa_step = -1;
        }
    }
}

static void aMR_OperateSwitchBit(FTR_ACTOR* ftr_actor) {
    if (ftr_actor->switch_bit == FALSE) {
        ftr_actor->switch_bit = TRUE;
    } else {
        ftr_actor->switch_bit = FALSE;
    }
}

static void aMR_SaveHaniwaStepData(MY_ROOM_ACTOR* my_room) {
    FTR_ACTOR* ftr_actor = l_aMR_work.ftr_actor_list;
    u8* used = l_aMR_work.used_list;
    int i;

    for (i = 0; i < l_aMR_work.list_size; i++) {
        if (*used == TRUE && ftr_actor->name < FTR_NUM) {
            aFTR_PROFILE* profile = aMR_GetFurnitureProfile(ftr_actor->name);

            if (profile != NULL && aFTR_CHECK_INTERACTION(profile->interaction_type, aFTR_INTERACTION_TYPE_HANIWA)) {
                int ut_x;
                int ut_z;

                if (mFI_Wpos2UtNum_inBlock(&ut_x, &ut_z, ftr_actor->position) && aMR_BOUNDS_OK(ut_x, ut_z)) {
                    u32* step_data = aMR_GetHaniwaStepSaveData(ftr_actor->layer, my_room);

                    if (step_data != NULL) {
                        int shift = (ut_x - 1) * 4;
                        int idx = ut_z - 1;

                        step_data[idx] &= ~(0xF << shift);
                        step_data[idx] |= (ftr_actor->haniwa_step & 0xF) << shift;
                    }
                }
            }
        }

        ftr_actor++;
        used++;
    }
}

static void aMR_SaveOneFtrSwitchData(mActor_name_t ftr_name, int ut_x, int ut_z, s16 layer, u64* switch_bit_table) {
    if (ITEM_IS_FTR(ftr_name)) {
        mActor_name_t name;
        int ftr_id;

        if (aMR_UnitNum2FtrItemNoFtrID(&name, &ftr_id, ut_x, ut_z, layer)) {
            FTR_ACTOR* ftr_actor = &l_aMR_work.ftr_actor_list[ftr_id];

            if (aMR_BOUNDS_OK(ut_x, ut_z)) {
                int shift = (ut_x - 1 + (ut_z - 1) * 8) & 0x3F;

                if (ftr_actor->switch_bit) {
                    (*switch_bit_table) |= 1ull << shift;
                }
            }
        }
    }
}

static void aMR_SaveSwitchData(MY_ROOM_ACTOR* my_room) {
    if (l_aMR_work.ftr_actor_list != NULL && l_aMR_work.used_list != NULL) {
        u64* bit_switch_table;
        int ut_x;
        int ut_z;
        int layer;
        mActor_name_t* fg_p;

        for (layer = mCoBG_LAYER0; layer < mCoBG_LAYER2; layer++) {
            bit_switch_table = aMR_GetBitSwitchTable(layer, my_room);

            if (bit_switch_table != NULL) {
                *bit_switch_table = 0;
                fg_p = aMR_GetLayerTopFg(layer);

                if (fg_p != NULL) {
                    for (ut_z = aMR_MIN_BOUND; ut_z <= aMR_MAX_BOUND; ut_z++) {
                        for (ut_x = aMR_MIN_BOUND; ut_x <= aMR_MAX_BOUND; ut_x++) {
                            aMR_SaveOneFtrSwitchData(fg_p[ut_x + ut_z * aMR_UT_X_NUM], ut_x, ut_z, layer, bit_switch_table);
                        }
                    }
                }
            }
        }
    }
}

static int aMR_CountFriendFurniture(FTR_ACTOR* ftr_actor, u8 friend_type) {
    u8* used = l_aMR_work.used_list;
    FTR_ACTOR* check_ftr_actor = l_aMR_work.ftr_actor_list;
    int count = 0;
    int i = 0;

    if (check_ftr_actor != NULL && used != NULL) {
        for (i; i < l_aMR_work.list_size; i++) {
            if (*used == TRUE && i != ftr_actor->id) {
                if (check_ftr_actor->name == ftr_actor->name) {
                    if (friend_type == aMR_FRIEND_ALL) {
                        count++;
                    } else if (friend_type == aMR_FRIEND_ON) {
                        if (check_ftr_actor->switch_bit != FALSE) {
                            count++;
                        }
                    } else { /* aMR_FRIEND_OFF */
                        if (check_ftr_actor->switch_bit == FALSE) {
                            count++;
                        }
                    }
                }
            }

            check_ftr_actor++;
            used++;
        }

        return count;
    }

    return 0;
}

static int aMR_GetWeight(int type) {
    return 1;
}

static void aMR_PlussWeight(ACTOR* actorx, FTR_ACTOR* ftr_actor) {
    MY_ROOM_ACTOR* my_room = (MY_ROOM_ACTOR*)actorx;

    my_room->weight += aMR_GetWeight(ftr_actor->shape_type);
}

static void aMR_MinusWeight(ACTOR* actorx, FTR_ACTOR* ftr_actor) {
    MY_ROOM_ACTOR* my_room = (MY_ROOM_ACTOR*)actorx;

    my_room->weight -= aMR_GetWeight(ftr_actor->shape_type);
}

static int aMR_WeightPossible(ACTOR* actorx, int type) {
    MY_ROOM_ACTOR* my_room = (MY_ROOM_ACTOR*)actorx;

    if (aMR_GetWeight(type) + my_room->weight <= l_aMR_work.list_size) {
        return TRUE;
    }

    return FALSE;
}

static int aMR_CheckFurnitureBankExist(u16 idx) {
    if (l_bank_index_table[idx] != aMR_NO_BANK) {
        return TRUE;
    }

    return FALSE;
}

static int aMR_GetFtrBankID(u16 idx) {
    if (l_bank_index_table[idx] == aMR_NO_BANK) {
        return -1;
    }

    return l_bank_index_table[idx];
}

static u8* aMR_BankNo2BankAddress(int bank_no) {
    if (bank_no == -1) {
        return NULL;
    }

    return l_bank_address_table[bank_no];
}

static u8* aMR_FtrNo2BankAddress(u16 ftr_no) {
    return aMR_BankNo2BankAddress(aMR_GetFtrBankID(ftr_no));
}

static int aMR_SearchFurnitureBankVacancy(void) {
    int i;
    int count = 0;

    for (i = 0; i < FTR_NUM; i++) {
        if (aMR_CheckFurnitureBankExist(i)) {
            count++;
        }
    }

    if (count < l_aMR_work.list_size) {
        return TRUE;
    }

    return FALSE;
}

static int aMR_SearchFreeFurnitureBankIdx(void) {
    int i;

#ifdef TARGET_PC
    /* A bank no kind uses, among those there are (outdoors hundreds: one pass, not one per bank) */
    {
        static u8 in_use[aMR_FTR_BANK_NUM];
        int banks = l_aMR_work.list_size < aMR_FTR_BANK_NUM ? l_aMR_work.list_size : aMR_FTR_BANK_NUM;

        memset(in_use, 0, sizeof(in_use));
        for (i = 0; i < FTR_NUM; i++) {
            if (l_bank_index_table[i] < aMR_FTR_BANK_NUM) {
                in_use[l_bank_index_table[i]] = TRUE;
            }
        }
        for (i = 0; i < banks; i++) {
            if (!in_use[i]) {
                return i;
            }
        }
        return -1;
    }
#endif

    for (i = 0; i < l_aMR_work.list_size; i++) {
        int j;
        int count = 0;

        for (j = 0; j < FTR_NUM; j++) {
            if (l_bank_index_table[j] == i) {
                count++;
            }
        }

        if (count == 0) {
            return i;
        }
    }

    return -1;
}

static int aMR_SearchFreeFurnitureActorNumber(void) {
    int i;

    for (i = 0; i < l_aMR_work.list_size; i++) {
        if (l_aMR_work.used_list[i] == FALSE) {
            return i;
        }
    }

    return -1;
}

// Part 1

static int aMR_CountAppointFurniture(u16 ftr_no) {
    FTR_ACTOR* ftr_actor = l_aMR_work.ftr_actor_list;
    u8* used = l_aMR_work.used_list;
    int i;
    int count = 0;

    for (i = 0; i < l_aMR_work.list_size; i++) {
        if (used[i]) {
            if (ftr_no == ftr_actor[i].name) {
                count++;
            }
        }
    }

    return count;
}

static int aMR_CountFurniture(void) {
    FTR_ACTOR* ftr_actor = l_aMR_work.ftr_actor_list;
    u8* used = l_aMR_work.used_list;
    int i;
    int count = 0;

    if (ftr_actor != NULL && used != NULL) {
        for (i = 0; i < l_aMR_work.list_size; i++) {
            if (*used) {
                count++;
            }

            ftr_actor++;
            used++;
        }
    }

    return count;
}

static void aMR_UnitNumber2Position(xyz_t* pos, u8 type, int ut_x, int ut_z) {
    pos->x = (f32)ut_x * mFI_UT_WORLDSIZE_X_F + mFI_UT_WORLDSIZE_HALF_X_F;
    pos->y = 0.0f;
    pos->z = (f32)ut_z * mFI_UT_WORLDSIZE_Z_F + mFI_UT_WORLDSIZE_HALF_Z_F;

    if (type == aFTR_SHAPE_TYPEC) {
        pos->x += mFI_UT_WORLDSIZE_HALF_X_F;
        pos->z += mFI_UT_WORLDSIZE_HALF_Z_F;
    }
#ifdef TARGET_PC
    if (aMR_pc_field) {
        pos->y = mCoBG_GetBgY_OnlyCenter_FromWpos2(*pos, 0.0f);
    }
#endif
}

static int aMR_Wpos2PlaceNumber(int* ut_x, int* ut_z, xyz_t pos, u8 type) {
    if (type == aFTR_SHAPE_TYPEC) {
        pos.x -= mFI_UT_WORLDSIZE_HALF_X_F;
        pos.z -= mFI_UT_WORLDSIZE_HALF_Z_F;
    }

    *ut_x = (int)(pos.x / mFI_UT_WORLDSIZE_X_F);
    *ut_z = (int)(pos.z / mFI_UT_WORLDSIZE_Z_F);

    if (*ut_x < aMR_UT_X_NUM && *ut_z < aMR_UT_Z_NUM) {
        return TRUE;
    }

    return FALSE;
}

static aMR_place_t* aMR_GetLayerPlaceTable(s16 layer) {
    if (layer == mCoBG_LAYER0) {
        return aMR_place_table[0];
    }

    if (layer == mCoBG_LAYER1) {
        return aMR_place_table[1];
    }

    return NULL;
}

static int aMR_Direct2PlussUnit(int* ut_x, int* ut_z, int direct) {
    switch (direct) {
        case aMR_DIRECT_UP:
            (*ut_z)--;
            return TRUE;
        case aMR_DIRECT_LEFT:
            (*ut_x)--;
            return TRUE;
        case aMR_DIRECT_DOWN:
            (*ut_z)++;
            return TRUE;
        case aMR_DIRECT_RIGHT:
            (*ut_x)++;
            return TRUE;
    }

    return FALSE;
}

static int aMR_GetTypeBPlaceInfo(int* x0, int* z0, int* x1, int* z1, u8 type, xyz_t pos) {
    *x0 = (int)(pos.x / mFI_UT_WORLDSIZE_X_F);
    *z0 = (int)(pos.z / mFI_UT_WORLDSIZE_Z_F);

    switch (type) {
        case aFTR_SHAPE_TYPEB_90:
            *x1 = *x0;
            *z1 = *z0 - 1;
            return TRUE;
        case aFTR_SHAPE_TYPEB_180:
            *x1 = *x0 - 1;
            *z1 = *z0;
            return TRUE;
        case aFTR_SHAPE_TYPEB_270:
            *x1 = *x0;
            *z1 = *z0 + 1;
            return TRUE;
        case aFTR_SHAPE_TYPEB_0:
            *x1 = *x0 + 1;
            *z1 = *z0;
            return TRUE;
    }

    return FALSE;
}

static void aMR_SetInfoFurnitureTable(u8 type, int idx, int id, s16 layer) {
    aMR_place_t* place_table = aMR_GetLayerPlaceTable(layer);

    switch (type) {
        case aFTR_SHAPE_TYPEA:
            place_table[idx] = id;
            break;
        case aFTR_SHAPE_TYPEB_90:
            place_table[idx] = id;
            place_table[idx - aMR_UT_X_NUM] = id;
            break;
        case aFTR_SHAPE_TYPEB_180:
            place_table[idx] = id;
            place_table[idx - 1] = id;
            break;
        case aFTR_SHAPE_TYPEB_270:
            place_table[idx] = id;
            place_table[idx + aMR_UT_X_NUM] = id;
            break;
        case aFTR_SHAPE_TYPEB_0:
            place_table[idx] = id;
            place_table[idx + 1] = id;
            break;
        case aFTR_SHAPE_TYPEC:
            place_table[idx] = id;
            place_table[idx + 1] = id;
            place_table[idx + aMR_UT_X_NUM] = id;
            place_table[idx + aMR_UT_X_NUM + 1] = id;
            break;
    }
}

static void aMR_SetFurnitureType(FTR_ACTOR* ftr_actor, int angle) {
    aFTR_PROFILE* profile = aMR_GetFurnitureProfile(ftr_actor->name);

    if (profile->shape == aFTR_SHAPE_TYPEB_0) {
        ftr_actor->shape_type = l_typeB0_table[angle];
        ftr_actor->original_shape_type = profile->shape;
    } else {
        ftr_actor->shape_type = profile->shape;
        ftr_actor->original_shape_type = profile->shape;
    }
}

static void aMR_SetFirstScale(FTR_ACTOR* ftr_actor) {
    static xyz_t xyz0 = { 0.0f, 0.0f, 0.0f };
    static xyz_t xyz1 = { 1.0f, 1.0f, 1.0f };

    if (ftr_actor->state == aFTR_STATE_BIRTH || ftr_actor->state == aFTR_STATE_BIRTH_WAIT) {
        ftr_actor->scale = xyz0;
    } else {
        ftr_actor->scale = xyz1;
    }

    if (ftr_actor->shape_type >= aFTR_SHAPE_TYPEB_90 && ftr_actor->shape_type <= aFTR_SHAPE_TYPEB_0) {
        ftr_actor->birth_scale_modifier = 0.5f;
        ftr_actor->birth_anim_step = 6000;
    } else {
        ftr_actor->birth_scale_modifier = 0.5f;
        ftr_actor->birth_anim_step = 6000;
    }

    ftr_actor->birth_anim_counter = 0;
    ftr_actor->birth_anim_counter_f = 0.0f;
    ftr_actor->dust_timer = 0;
    ftr_actor->dust_timer_f = 0.0f;
}

static void aMR_DeleteFurnitureBank(u16 ftr_no) {
    if (aMR_CountAppointFurniture(ftr_no) == 0) {
        l_bank_index_table[ftr_no] = aMR_NO_BANK;
    }
}

static void aMR_InitHaniwaOnTable(ACTOR* actorx) {
    MY_ROOM_ACTOR* my_room = (MY_ROOM_ACTOR*)actorx;
    int* haniwa_on_table_p = my_room->haniwa_on_table;
    int i;

    for (i = 0; i < aMR_HANIWA_ON_TABLE_NUM; i++) {
        *haniwa_on_table_p++ = -1;
    }
}

static int aMR_GetHaniwaSwitchVac(ACTOR* actorx) {
    MY_ROOM_ACTOR* my_room = (MY_ROOM_ACTOR*)actorx;
    int* haniwa_on_table_p = my_room->haniwa_on_table;
    int i;

    for (i = 0; i < aMR_HANIWA_ON_TABLE_NUM; i++) {
        if (*haniwa_on_table_p == -1) {
            return i;
        }
        haniwa_on_table_p++;
    }

    return -1;
}

static int aMR_TidyHaniwaOnTable(ACTOR* actorx, int idx) {
    MY_ROOM_ACTOR* my_room = (MY_ROOM_ACTOR*)actorx;
    int* haniwa_on_table_p = my_room->haniwa_on_table;
    int i;

    if (idx >= 0 && idx < aMR_HANIWA_ON_TABLE_NUM) {
        if (idx == (aMR_HANIWA_ON_TABLE_NUM - 1)) {
            haniwa_on_table_p[idx] = -1;
        } else {
            for (i = idx; i < (aMR_HANIWA_ON_TABLE_NUM - 1); i++) {
                haniwa_on_table_p[i] = haniwa_on_table_p[i + 1];
            }

            haniwa_on_table_p[aMR_HANIWA_ON_TABLE_NUM - 1] = -1;
        }

        return TRUE;
    }

    return FALSE;
}

static int aMR_HaniwaOffReport(ACTOR* actorx, int ftr_idx) {
    MY_ROOM_ACTOR* my_room = (MY_ROOM_ACTOR*)actorx;
    int* haniwa_on_table_p = my_room->haniwa_on_table;
    int i;

    for (i = 0; i < aMR_HANIWA_ON_TABLE_NUM; i++) {
        if (*haniwa_on_table_p == ftr_idx) {
            aMR_TidyHaniwaOnTable(actorx, i);
            return TRUE;
        }

        haniwa_on_table_p++;
    }

    return FALSE;
}

static int aMR_ClearHaniwaSwitch(ACTOR* actorx, FTR_ACTOR* ftr_actor) {
    ftr_actor->switch_changed_flag = FALSE;
    ftr_actor->switch_bit = FALSE;
    return aMR_HaniwaOffReport(actorx, ftr_actor->id);
}

static void aMR_HaniwaSwitchOn(ACTOR* actorx, FTR_ACTOR* ftr_actor) {
    MY_ROOM_ACTOR* my_room = (MY_ROOM_ACTOR*)actorx;
    int free_idx = aMR_GetHaniwaSwitchVac(actorx);
    int* haniwa_on_table_p = my_room->haniwa_on_table;

    if (free_idx == -1) {
        int ftr_id = haniwa_on_table_p[0];

        if (ftr_id >= 0) {
            FTR_ACTOR* target_ftr_actor = l_aMR_work.ftr_actor_list + ftr_id;

            /* Turn off the old active gyroid */
            target_ftr_actor->switch_changed_flag = TRUE;
            target_ftr_actor->switch_bit = FALSE;
            aMR_HaniwaOffReport(actorx, target_ftr_actor->id);

            /* Turn on new one */
            ftr_actor->switch_changed_flag = TRUE;
            ftr_actor->switch_bit = TRUE;
            haniwa_on_table_p[aMR_HANIWA_ON_TABLE_NUM - 1] = ftr_actor->id;
        }
    } else {
        ftr_actor->switch_changed_flag = TRUE;
        ftr_actor->switch_bit = TRUE;
        haniwa_on_table_p[free_idx] = ftr_actor->id;
    }
}

static void aMR_SetSwitchStepData(FTR_ACTOR* ftr_actor, s16 placed_flag, MY_ROOM_ACTOR* my_room) {
    aFTR_PROFILE* profile = aMR_GetFurnitureProfile(ftr_actor->name);

    if (aFTR_CHECK_INTERACTION(profile->interaction_type, aFTR_INTERACTION_TYPE_HANIWA)) {
        ACTOR* actorx = aMR_CLIP->my_room_actor_p;

        if (actorx != NULL) {
            MY_ROOM_ACTOR* l_my_room = (MY_ROOM_ACTOR*)actorx;

            if (placed_flag == TRUE) {
                aMR_HaniwaSwitchOn(actorx, ftr_actor);
                aMR_GetHaniwaStep(ftr_actor, placed_flag, &l_my_room->haniwa_step_idx, l_my_room);
                return;
            } else {
                aMR_GetSwitchBit(ftr_actor, placed_flag, l_my_room);
                if (ftr_actor->switch_bit) {
                    aMR_HaniwaSwitchOn(actorx, ftr_actor);
                }

                aMR_GetHaniwaStep(ftr_actor, placed_flag, &l_my_room->haniwa_step_idx, l_my_room);
                return;
            }
        }
    } else {
        if (placed_flag == TRUE) {
            ftr_actor->switch_bit = TRUE;
            ftr_actor->haniwa_step = -1;

            if (aFTR_CHECK_INTERACTION(profile->interaction_type, aFTR_INTERACTION_TYPE_TOGGLE) ||
                aFTR_CHECK_INTERACTION(profile->interaction_type, aFTR_INTERACTION_TYPE_START_DISABLED)) {
                ftr_actor->switch_bit = FALSE;
            }
        } else {
            aMR_GetSwitchBit(ftr_actor, placed_flag, my_room);
            ftr_actor->haniwa_step = -1;
        }

        return;
    }

    ftr_actor->switch_bit = FALSE;
    ftr_actor->haniwa_step = 0;
}

static int aMR_SystemAnimeCt_UniqueCt(FTR_ACTOR* ftr_actor, aFTR_PROFILE* profile) {
    if (profile->rig != NULL) {
        cKF_SkeletonInfo_R_c* keyframe = &ftr_actor->keyframe;
        cKF_Skeleton_R_c* skeleton = profile->rig->skeleton;
        cKF_Animation_R_c* animation = profile->rig->animation;

        cKF_SkeletonInfo_R_ct(keyframe, skeleton, animation, ftr_actor->joint, ftr_actor->morph);
        cKF_SkeletonInfo_R_init_standard_repeat(keyframe, animation, NULL);
        cKF_SkeletonInfo_R_play(keyframe);

        keyframe->frame_control.speed = 0.5f;
    }

    if (profile->vtable != NULL && profile->vtable->ct_proc != NULL) {
        profile->vtable->ct_proc(ftr_actor, aMR_FtrNo2BankAddress(ftr_actor->name));
    }

    return TRUE;
}

static int aMR_RegistMoveBg(FTR_ACTOR* ftr_actor, aFTR_PROFILE* profile) {
    ftr_actor->move_bg_idx = -1;

    if (aFTR_CHECK_INTERACTION(profile->interaction_type, aFTR_INTERACTION_TYPE_NO_COLLISION) == FALSE ||
        mFI_CheckShop() == TRUE || mFI_GetFieldId() == mFI_FIELD_ROOM_BROKER_SHOP) {
        f32 height;

        if (aMR_layer_set_info[ftr_actor->name] == aFTR_SET_TYPE_SURFACE) {
            height = profile->height;
        } else {
            height = 100.0f;
        }

        ftr_actor->move_bg_idx =
            mCoBG_RegistMoveBg(&ftr_actor->bg_register, &ftr_actor->position, &ftr_actor->last_position,
                               &ftr_actor->s_angle_y, height, NULL, &ftr_actor->collision_scale, &ftr_actor->bg_contact,
                               &ftr_actor->base_position, profile->move_bg_type, mCoBG_ATTRIBUTE_STONE, 81.0f);

        if (ftr_actor->move_bg_idx != -1) {
            return TRUE;
        } else {
            return FALSE;
        }
    } else {
        return TRUE;
    }
}

static void aMR_MiniDiskCommonCt(FTR_ACTOR* ftr_actor, s16 placed_flag) {
    aFTR_PROFILE* profile = aMR_GetFurnitureProfile(ftr_actor->name);
    int count;
    int i;

    if (aFTR_CHECK_INTERACTION(profile->interaction_type, aFTR_INTERACTION_TYPE_MUSIC_DISK)) {
        if (placed_flag == FALSE) {
            if (ftr_actor->switch_bit == TRUE) {
                ftr_actor->haniwa_state = 1;
                ftr_actor->switch_changed_flag = FALSE;
            }
        } else {
            int home_idx = (mFI_GetFieldId() - mFI_FIELD_PLAYER0_ROOM) & 3;

            count = 0;
            for (i = 0; i < MINIDISK_NUM; i++) {
                u32* music_box = Save_Get(scene_no) == SCENE_COTTAGE_MY ? Save_Get(island).cottage.music_box
                                                                        : Save_Get(homes[home_idx]).music_box;

                if (((music_box[(i / 32) & 1] >> (i & 31)) & 1) != 0) {
                    count++;
                }
            }

            /* Pick a random song to play when placed */
            if (count > 0) {
                int sel = RANDOM(count);

                count = 0;
                for (i = 0; i < MINIDISK_NUM; i++) {
                    u32* music_box = Save_Get(scene_no) == SCENE_COTTAGE_MY ? Save_Get(island).cottage.music_box
                                                                            : Save_Get(homes[home_idx]).music_box;

                    if (((music_box[(i / 32) & 1] >> (i & 31)) & 1) != 0) {
                        if (sel == count) {
                            ftr_actor->items[0] = ITM_MINIDISK_START + i;
                            // @BUG - missing break;
                        }

                        count++;
                    }
                }
            }

            ftr_actor->switch_changed_flag = FALSE;
            ftr_actor->switch_bit = FALSE;
        }
    }
}

static void aMR_MiniDiskCommonDt(FTR_ACTOR* ftr_actor, ACTOR* actorx) {
    aFTR_PROFILE* profile = aMR_GetFurnitureProfile(ftr_actor->name);
    MY_ROOM_ACTOR* my_room = (MY_ROOM_ACTOR*)actorx;

    if (aFTR_CHECK_INTERACTION(profile->interaction_type, aFTR_INTERACTION_TYPE_MUSIC_DISK) &&
        ftr_actor->switch_bit == TRUE) {
#ifdef TARGET_PC
        if (aMR_pc_md_far) {
            aMR_pc_md_far = FALSE; /* out of earshot: its song was already off (aMR_pc_md_range) */
        } else
#endif
        {
            mBGMPsComp_delete_ps_room(my_room->bgm_info.md_no, 0);

            /* Don't delete for aerobics radio music */
            if (my_room->bgm_info.md_no != BGM_SPORTSFAIR_AEROBICS) {
                mBGMPsComp_MDPlayerPos_delete();
            }
        }
        my_room->bgm_info.active_flag = FALSE;
        my_room->bgm_info.active_ftr_actor = NULL;
        my_room->bgm_info.last_md_no = -1;
    }
}

static void aMR_RadioCommonCt(FTR_ACTOR* ftr_actor, s16 placed_flag) {
    aFTR_PROFILE* profile = aMR_GetFurnitureProfile(ftr_actor->name);

    if (aFTR_CHECK_INTERACTION(profile->interaction_type, aFTR_INTERACTION_TYPE_RADIO_AEROBICS)) {
        ftr_actor->switch_changed_flag = FALSE;
        ftr_actor->switch_bit = FALSE;
    }
}

static void aMR_RadioCommonDt(FTR_ACTOR* ftr_actor, ACTOR* actorx) {
    aFTR_PROFILE* profile = aMR_GetFurnitureProfile(ftr_actor->name);

    if (aFTR_CHECK_INTERACTION(profile->interaction_type, aFTR_INTERACTION_TYPE_RADIO_AEROBICS) &&
        ftr_actor->switch_bit == TRUE) {
        aMR_ReserveDefaultBgm(actorx, ftr_actor);
        aMR_ChangeMDBgm(actorx, ftr_actor);
    }
}

#ifdef TARGET_PC
/* A stereo's song on or off; outdoors not while the player is out of earshot (aMR_pc_md_range), where the hourly
 * music must play on undisturbed */
static int aMR_pc_md_out_of_earshot(FTR_ACTOR* ftr_actor) {
    PLAYER_ACTOR* player = GET_PLAYER_ACTOR_NOW();

    return aMR_pc_field && ftr_actor != NULL && player != NULL &&
           aMR_GetDistXZ(&player->actor_class.world.position, &ftr_actor->position) > aMR_PC_MD_NEAR;
}

static void aMR_pc_md_on(int md_no, FTR_ACTOR* ftr_actor) {
    if (md_no != BGM_SPORTSFAIR_AEROBICS && aMR_pc_md_out_of_earshot(ftr_actor)) {
        aMR_pc_md_far = TRUE;
        return;
    }
    aMR_pc_md_far = FALSE;
    mBGMPsComp_make_ps_room(md_no, 0);
    if (md_no != BGM_SPORTSFAIR_AEROBICS) {
        mBGMPsComp_MDPlayerPos_make();
    }
}

static void aMR_pc_md_off(int md_no) {
    if (aMR_pc_md_far) {
        aMR_pc_md_far = FALSE;
        return;
    }
    mBGMPsComp_delete_ps_room(md_no, 0);
    if (md_no != BGM_SPORTSFAIR_AEROBICS) {
        mBGMPsComp_MDPlayerPos_delete();
    }
}
#define aMR_MD_ON(md_no, ftr_actor) aMR_pc_md_on(md_no, ftr_actor)
#define aMR_MD_OFF(md_no) aMR_pc_md_off(md_no)
#else
#define aMR_MD_ON(md_no, ftr_actor)                       \
    do {                                                  \
        mBGMPsComp_make_ps_room(md_no, 0);                \
        if ((md_no) != BGM_SPORTSFAIR_AEROBICS) {         \
            mBGMPsComp_MDPlayerPos_make();                \
        }                                                 \
    } while (0)
#define aMR_MD_OFF(md_no)                                 \
    do {                                                  \
        mBGMPsComp_delete_ps_room(md_no, 0);              \
        if ((md_no) != BGM_SPORTSFAIR_AEROBICS) {         \
            mBGMPsComp_MDPlayerPos_delete();              \
        }                                                 \
    } while (0)
#endif

static void aMR_ChangeMDBgm(ACTOR* actorx, FTR_ACTOR* ftr_actor) {
    MY_ROOM_ACTOR* my_room = (MY_ROOM_ACTOR*)actorx;


    if ((int)my_room->bgm_info.reserve_flag == TRUE) {
        if (my_room->bgm_info.active_flag == FALSE) {
            if (my_room->bgm_info.md_no != -1) {
                aMR_MD_ON(my_room->bgm_info.md_no, ftr_actor);

                my_room->bgm_info.active_flag = TRUE;
                my_room->bgm_info.active_ftr_actor = ftr_actor;
                my_room->bgm_info.last_md_no = my_room->bgm_info.md_no;
            }
        } else if (my_room->bgm_info.md_no == -1) {
            aMR_MD_OFF(my_room->bgm_info.last_md_no);
            my_room->bgm_info.active_flag = FALSE;
            my_room->bgm_info.active_ftr_actor = NULL;
            my_room->bgm_info.last_md_no = my_room->bgm_info.md_no;
        } else {
            aMR_MD_OFF(my_room->bgm_info.last_md_no);
            aMR_MD_ON(my_room->bgm_info.md_no, ftr_actor);

            my_room->bgm_info.active_flag = TRUE;
            my_room->bgm_info.active_ftr_actor = ftr_actor;
            my_room->bgm_info.last_md_no = my_room->bgm_info.md_no;
        }

        my_room->bgm_info.reserve_flag = FALSE;
        my_room->bgm_info.last_md_no = my_room->bgm_info.md_no;
    }
}

static void aMR_ReserveBgm(ACTOR* actorx, int bgm_no, FTR_ACTOR* ftr_actor, s16 timer) {
    MY_ROOM_ACTOR* my_room = (MY_ROOM_ACTOR*)actorx;

    my_room->bgm_info.reserve_flag = TRUE;
    my_room->bgm_info.timer = timer;
    my_room->bgm_info.md_no = bgm_no;
    my_room->bgm_info.reserved_ftr_actor = ftr_actor;
}

static void aMR_ReserveDefaultBgm(ACTOR* actorx, FTR_ACTOR* ftr_actor) {
    MY_ROOM_ACTOR* my_room = (MY_ROOM_ACTOR*)actorx;

    my_room->bgm_info.reserve_flag = TRUE;
    my_room->bgm_info.timer = 0;
    my_room->bgm_info.md_no = -1;
    my_room->bgm_info.reserved_ftr_actor = ftr_actor;
}

static void aMR_AllMDSwitchOff(void) {
    FTR_ACTOR* ftr_actor = l_aMR_work.ftr_actor_list;
    u8* used = l_aMR_work.used_list;
    int i;

    for (i = 0; i < l_aMR_work.list_size; i++) {
        if (*used) {
            aFTR_PROFILE* profile = aMR_GetFurnitureProfile(ftr_actor->name);

            if (aFTR_CHECK_INTERACTION(profile->interaction_type, aFTR_INTERACTION_TYPE_MUSIC_DISK) ||
                aFTR_CHECK_INTERACTION(profile->interaction_type, aFTR_INTERACTION_TYPE_RADIO_AEROBICS)) {
                ftr_actor->switch_bit = FALSE;
                ftr_actor->switch_changed_flag = TRUE;
            }
        }

        used++;
        ftr_actor++;
    }
}

static void aMR_OneMDSwitchOn_TheOtherSwitchOff(FTR_ACTOR* ftr_actor) {
    aMR_AllMDSwitchOff();
    ftr_actor->switch_bit = TRUE;
    ftr_actor->switch_changed_flag = TRUE;
}

// Part 2

static void aMR_Status2MoveBgCollisionScale(FTR_ACTOR* ftr_actor, s16 status) {
    if (status == aFTR_STATE_BIRTH_WAIT) {
        ftr_actor->collision_scale = 0.6f;
    } else {
        ftr_actor->collision_scale = 1.0f;
    }
}

static void aMR_FurnitureCt(FTR_ACTOR* ftr_actor, GAME* game, int ut_x, int ut_z, mActor_name_t item, int ftr_idx,
                            s16 status, s16 layer, s16 placed_flag) {
    int angle = FTR_GET_ROTATION(item);
    static xyz_t xyz0 = { 0.0f, 0.0f, 0.0f };
    aFTR_PROFILE* profile;
    xyz_t pos;

    bzero(ftr_actor, sizeof(FTR_ACTOR));
    ftr_actor->ctr_type = aFTR_CTR_TYPE_GAME_PLAY;
    ftr_actor->base_position = xyz0;
    ftr_actor->layer = layer;
    ftr_actor->angle_y = aMR_angle_table[angle];
    ftr_actor->angle_y_target = ftr_actor->angle_y;
    ftr_actor->s_angle_y = RAD2SHORT_ANGLE2(DEG2RAD(ftr_actor->angle_y));
    ftr_actor->name = mRmTp_FtrItemNo2FtrIdx(item);

    profile = aMR_GetFurnitureProfile(ftr_actor->name);
    ftr_actor->state = status;
    aMR_SetFurnitureType(ftr_actor, angle);
    aMR_SetFirstScale(ftr_actor);
    aMR_UnitNumber2Position(&pos, ftr_actor->shape_type, ut_x, ut_z);
    pos.y = mCoBG_GetBgY_AngleS_FromWpos(NULL, pos, 0.0f);
    ftr_actor->position = pos;
    ftr_actor->last_position = pos;
    ftr_actor->id = ftr_idx;
    aMR_SetInfoFurnitureTable(ftr_actor->shape_type, ut_x + ut_z * aMR_UT_X_NUM, ftr_idx, ftr_actor->layer);
    ftr_actor->collision_direction = 0;

    if (aMR_CLIP != NULL && aMR_CLIP->my_room_actor_p != NULL) {
        aMR_SetSwitchStepData(ftr_actor, placed_flag, (MY_ROOM_ACTOR*)aMR_CLIP->my_room_actor_p);
    }

    aMR_MiniDiskCommonCt(ftr_actor, placed_flag);
    aMR_RadioCommonCt(ftr_actor, placed_flag);
    aMR_Status2MoveBgCollisionScale(ftr_actor, status);
    if (aFTR_CHECK_INTERACTION(profile->interaction_type, aFTR_INTERACTION_TYPE_HANIWA)) {
        ftr_actor->dynamic_work_s[3] = placed_flag;
    }

    if (aMR_RegistMoveBg(ftr_actor, profile) && aMR_SystemAnimeCt_UniqueCt(ftr_actor, profile)) {
        aMR_SetFurniture2FG(ftr_actor, ftr_actor->position, TRUE);
    }
}

static int aMR_DmaFurniture_Common(u16 ftr_id, mActor_name_t item, u8* bank_addr, int bank_idx) {
    if (bank_addr != NULL) {
        aFTR_PROFILE* profile = aMR_GetFurnitureProfile(ftr_id);

        if (bank_idx != -1) {
            l_bank_index_table[ftr_id] = bank_idx;
        }

        if (profile->vtable != NULL && profile->vtable->dma_proc != NULL) {
            profile->vtable->dma_proc(item, aMR_FtrNo2BankAddress(ftr_id));
            return TRUE;
        } else {
            return TRUE;
        }
    }

    return FALSE;
}

static int aMR_GetFurnitureBank(u16 ftr_id, GAME_PLAY* play, mActor_name_t item) {
    if (aMR_SearchFurnitureBankVacancy()) {
        int bank_idx = aMR_SearchFreeFurnitureBankIdx();

        if (bank_idx != -1) {
            u8* bank_addr = aMR_BankNo2BankAddress(bank_idx);

            return aMR_DmaFurniture_Common(ftr_id, item, bank_addr, bank_idx);
        }
    }

    return FALSE;
}

static int aMR_GetFurnitureBank2(u16 ftr_id, GAME* game, mActor_name_t item) {
    GAME_PLAY* play = (GAME_PLAY*)game;

    if (aMR_CheckFurnitureBankExist(ftr_id)) {
        return TRUE;
    }

    if (aMR_GetFurnitureBank(ftr_id, play, item)) {
        return TRUE;
    }

    return FALSE;
}

static void aMR_MakeOneFurniture(ACTOR* actorx, mActor_name_t item, GAME* game, int ut_x, int ut_z, s16 layer) {
    if (ITEM_IS_FTR(item) && aMR_CountFurniture() < l_aMR_work.list_size) {
        u16 ftr_idx = mRmTp_FtrItemNo2FtrIdx((mActor_name_t)item);
        int id = aMR_SearchFreeFurnitureActorNumber();

        if (id != -1) {
            u8* used = l_aMR_work.used_list;
            FTR_ACTOR* ftr_actor = l_aMR_work.ftr_actor_list + id;

            if (aMR_GetFurnitureBank2(ftr_idx, game, item)) {
                used[id] = TRUE;
                aMR_FurnitureCt(ftr_actor, game, ut_x, ut_z, item, id, aFTR_STATE_STOP, layer, FALSE);
                aMR_PlussWeight(actorx, ftr_actor);
            }
        }
    }
}

static void aMR_MakeFurnitureActor(ACTOR* actorx, GAME_PLAY* play, s16 layer) {
    GAME* game = (GAME*)play;
    mActor_name_t* fg_p = aMR_GetLayerTopFg(layer);
    int ut_x;
    int ut_z;

#ifdef TARGET_PC
    if (aMR_pc_field) {
        return; /* only the acres around the player, as they walk (aMR_pc_field_stream) */
    }
#endif
    if (fg_p != NULL) {
        for (ut_z = 0; ut_z < aMR_UT_Z_NUM; ut_z++) {
            for (ut_x = 0; ut_x < aMR_UT_X_NUM; ut_x++) {
                aMR_MakeOneFurniture(actorx, *fg_p, game, ut_x, ut_z, layer);
                fg_p++;
            }
        }
    }
}

static void aMR_InitFurnitureActorExistTable(void) {
    int i;

    for (i = 0; i < l_aMR_work.list_size; i++) {
        l_aMR_work.used_list[i] = FALSE;
    }
}

static void aMR_InitFurnitureBankTable(void) {
    int i;

    for (i = 0; i < FTR_NUM; i++) {
        l_bank_index_table[i] = aMR_NO_BANK;
    }
}

extern cKF_Skeleton_R_c cKF_bs_r_furniture_stop;
extern cKF_Animation_R_c cKF_ba_r_furniture_stop;

static void aMR_InitDummyKeyAnime(GAME_PLAY* play, ACTOR* actorx) {
    MY_ROOM_ACTOR* my_room = (MY_ROOM_ACTOR*)actorx;

    cKF_SkeletonInfo_R_ct(&my_room->keyframe, &cKF_bs_r_furniture_stop, &cKF_ba_r_furniture_stop, my_room->joint,
                          my_room->morph);
}

static void aMR_GetBankSitu(int* unalloc_bank_num, int* alloc_bank_num, int* req_bank_num) {
    *unalloc_bank_num = 0;
    *alloc_bank_num = 0;

    if (*req_bank_num > aMR_FTR_BANK_NUM) {
        *req_bank_num = aMR_FTR_BANK_NUM;
    }

    *unalloc_bank_num = *req_bank_num <= aMR_FTR_BANK_NUM ? *req_bank_num : aMR_FTR_BANK_NUM;
    if (*req_bank_num > *unalloc_bank_num) {
        *alloc_bank_num = *req_bank_num - *unalloc_bank_num;
    }
}

static void aMR_SecureFurnitureBank(MY_ROOM_ACTOR* my_room, GAME* game) {
    u8* bank_p;
    size_t size;
    int i;

    aMR_GetBankSitu(&my_room->bank_count0, &my_room->bank_count1, &l_aMR_work.list_size);
    size = my_room->bank_count0 * aMR_FTR_BANK_SIZE;

    for (i = 0; i < aMR_FTR_BANK_NUM; i++) {
        l_bank_address_table[i] = NULL;
    }

    bank_p = (u8*)zelda_malloc_align(size, 32);
#ifdef TARGET_PC
    if (bank_p == NULL && size != 0) {
        OSReport("[My_Room] no room for %d furniture banks (%u bytes)\n", my_room->bank_count0, (unsigned)size);
        my_room->bank_count0 = 0;
    }
#endif
    my_room->bank0_p = bank_p;
    for (i = 0; i < my_room->bank_count0; i++) {
        int size = i * aMR_FTR_BANK_SIZE;
        l_bank_address_table[i] = bank_p + (int)size;
    }

    if (my_room->bank_count1 > 0) {
        for (i = 0; i < my_room->bank_count1; i++) {
            u8* bank = (u8*)zelda_malloc(aMR_FTR_BANK_SIZE);
            int idx = my_room->bank_count0 + i;

            if (bank != NULL) {
                l_bank_address_table[idx] = bank;
            }
        }
    }
}

static int aMR_GetSceneFurnitureMax(void) {
    static int scene_table[] = {
        SCENE_NPC_HOUSE,
        SCENE_FIELD_TOOL_INSIDE,
        SCENE_SHOP0,
        SCENE_CONVENI,
        SCENE_SUPER,
        SCENE_DEPART,
        SCENE_DEPART_2,
        SCENE_BROKER_SHOP,
        SCENE_MY_ROOM_S,
        SCENE_MY_ROOM_M,
        SCENE_MY_ROOM_L,
        SCENE_KAMAKURA,
        SCENE_MUSEUM_ROOM_PAINTING,
        SCENE_MUSEUM_ROOM_FOSSIL,
        SCENE_MY_ROOM_LL1,
        SCENE_MY_ROOM_LL2,
        SCENE_MY_ROOM_BASEMENT_S,
        SCENE_MY_ROOM_BASEMENT_M,
        SCENE_MY_ROOM_BASEMENT_L,
        SCENE_MY_ROOM_BASEMENT_LL1,
        SCENE_COTTAGE_MY,
        SCENE_COTTAGE_NPC,
        -1,
    };

    static int bank_count_table[] = {
        30, 30, 10, 10, 10, 10, 10, 10, 32, 48, 64, 10, 20, 25, 64, 48, 64, 64, 64, 64, 64, 30, 5,
    };

    int scene = Save_Get(scene_no);
    int i;

#ifdef TARGET_PC
    if (aMR_pc_field) {
        return pc_oftr_loaded_max();
    }
#endif
    for (i = 0; scene_table[i] != -1; i++) {
        if (scene == scene_table[i]) {
            return bank_count_table[i];
        }
    }

    return 3;
}

#include "../src/actor/ac_my_room_msg_ctrl.c_inc"

static void aMR_SecureFurnitureRam(ACTOR* actorx) {
    l_aMR_work.ftr_actor_list = (FTR_ACTOR*)zelda_malloc(l_aMR_work.list_size * sizeof(FTR_ACTOR));
    l_aMR_work.used_list = (u8*)zelda_malloc(l_aMR_work.list_size * sizeof(u8));

    if (l_aMR_work.ftr_actor_list == NULL || l_aMR_work.used_list == NULL) {
        l_aMR_work.list_size = 0;
    }
}

static void aMR_InitFurnitureTable(FTR_ACTOR* ftr_actor, int count) {
    if (ftr_actor != NULL) {
        int i;

        for (i = 0; i < count; i++) {
            bzero(&ftr_actor[i], sizeof(FTR_ACTOR));
        }
    }
}

static int aMR_JudgeBreedNewFurniture(GAME* game, u16 ftr_no, int* ut_x, int* ut_z, u16* rotation, int* square_offset,
                                      int* layer);
static mActor_name_t aMR_SearchPickupFurniture(GAME* game);
static void aMR_Furniture2ItemBag(GAME* game);
static int aMR_JudgePlayerAction(xyz_t* wpos0, xyz_t* wpos1, int ftr_actor_idx);
static void aMR_PlayerMoveFurniture(int ftr_actor_idx, const xyz_t* wpos);
static int aMR_ftrID2Wpos(xyz_t* wpos, int ftr_id);
static int aMR_UnitNum2FtrItemNoFtrID(mActor_name_t* ftr_item_no, int* ftr_id, int ut_x, int ut_z, int layer);
static void aMR_FtrID2ExtinguishFurniture(int ftr_id);
static void aMR_RedmaFtrBank(void);
static int aMR_ReserveFurniture(GAME* game, u16 ftr_no, int free_idx, int ut_x, int ut_z, u16 rotation,
                                int square_offset, int layer);
static int aMR_CountFriendFurniture(FTR_ACTOR* ftr_actor, u8 switch_on);
static int aMR_JudgePlace2ndLayer(int ut_x, int ut_z);
static void aMR_OpenCloseCommonMove(FTR_ACTOR* ftr_actor, ACTOR* actor, GAME* game, f32 start_frame, f32 end_frame);
static int aMR_GetBedAction(ACTOR* actorx, int bed_move_dir);
static void aMR_MiniDiskCommonMove(FTR_ACTOR* ftr_actor, ACTOR* my_room_actorx, GAME* game, f32 start_frame,
                                   f32 end_frame);
static void aMR_FamicomEmuCommonMove(FTR_ACTOR* ftr_actor, ACTOR* my_room_actor, GAME* game, int rom_no,
                                     int agb_rom_no);
static int aMR_SetLeaf(const xyz_t* pos, f32 scale);
static int aMR_Ftr2Leaf(void);
static void aMR_LeafStartPos(xyz_t* pos);
static int aMR_PickupFtrLayer(void);
static void aMR_LeafPickuped(void);
static u8* aMR_FtrNo2BankAddress(u16 ftr_no);
static void aMR_CallSitDownOngenPosSE(const xyz_t* pos);
static void aMR_SoundMelody(FTR_ACTOR* ftr_actor, ACTOR* my_room_actor, int idx);
static int aMR_CheckDannaKill(xyz_t* pos);

#ifdef TARGET_PC
static void aMR_pc_dresser_load_ids(void);
#endif

static void aMR_SetClip(ACTOR* actorx) {
    MY_ROOM_ACTOR* my_room = (MY_ROOM_ACTOR*)actorx;

    my_room->clip.my_room_actor_p = actorx;
    my_room->clip.judge_breed_new_ftr_proc = &aMR_JudgeBreedNewFurniture;
    my_room->clip.search_pickup_ftr_proc = &aMR_SearchPickupFurniture;
    my_room->clip.ftr2itemBag_proc = &aMR_Furniture2ItemBag;
    my_room->clip.judge_player_action_proc = &aMR_JudgePlayerAction;
    my_room->clip.player_move_ftr_proc = &aMR_PlayerMoveFurniture;
    my_room->clip.ftrId2wpos_proc = &aMR_ftrID2Wpos;
    my_room->clip.unitNum2ftrItemNoftrId_proc = &aMR_UnitNum2FtrItemNoFtrID;
    my_room->clip.ftrId2extinguishFtr_proc = &aMR_FtrID2ExtinguishFurniture;
    my_room->clip.redma_ftr_bank_proc = &aMR_RedmaFtrBank;
    my_room->clip.reserve_ftr_proc = &aMR_ReserveFurniture;
    my_room->clip.count_friend_ftr_proc = &aMR_CountFriendFurniture;
    my_room->clip.judge_place_2nd_layer_proc = &aMR_JudgePlace2ndLayer;
    my_room->clip.open_close_common_move_proc = &aMR_OpenCloseCommonMove;
    my_room->clip.get_bed_action_proc = &aMR_GetBedAction;
    my_room->clip.mini_disk_common_move_proc = &aMR_MiniDiskCommonMove;
    my_room->clip.famicom_emu_common_move_proc = &aMR_FamicomEmuCommonMove;
    my_room->clip.set_leaf_proc = &aMR_SetLeaf;
    my_room->clip.ftr2leaf_proc = &aMR_Ftr2Leaf;
    my_room->clip.leaf_start_pos_proc = &aMR_LeafStartPos;
    my_room->clip.pickup_ftr_layer_proc = &aMR_PickupFtrLayer;
    my_room->clip.leaf_pickuped_proc = &aMR_LeafPickuped;
    my_room->clip.ftrNo2bankAddress_proc = &aMR_FtrNo2BankAddress;
    my_room->clip.call_sit_down_ongen_pos_se_proc = &aMR_CallSitDownOngenPosSE;
    my_room->clip.clock_info_p = &my_room->clock_info;
    my_room->clip.sound_melody_proc = &aMR_SoundMelody;
    my_room->clip.check_danna_kill_proc = &aMR_CheckDannaKill;

    aMR_CLIP = &my_room->clip;
}

static void aMR_MakeItemDataInFurniture(void) {
    mActor_name_t* layer_top_table[mCoBG_LAYER_NUM];
    int idx;
    int j;
    int ut_x;
    int ut_z;
    int i;
    int k;

    for (k = 0; k < mCoBG_LAYER_NUM; k++) {
        layer_top_table[k] = aMR_GetLayerTopFg(k);
    }

    for (i = mCoBG_LAYER0; i < mCoBG_LAYER2; i++) {
        if (layer_top_table[i] != NULL) {
            idx = 0;

            for (j = i + 1; j < mCoBG_LAYER_NUM; j++) {
                mActor_name_t* fg_top_p = layer_top_table[j];

                if (fg_top_p != NULL) {
                    for (ut_z = 0; ut_z < aMR_UT_Z_NUM; ut_z++) {
                        for (ut_x = 0; ut_x < aMR_UT_X_NUM; ut_x++) {
                            if (*fg_top_p != EMPTY_NO && *fg_top_p != RSV_NO && *fg_top_p != RSV_WALL_NO) {
                                mActor_name_t item;
                                int ftr_id;

                                if (aMR_UnitNum2FtrItemNoFtrID(&item, &ftr_id, ut_x, ut_z, i)) {
                                    FTR_ACTOR* ftr_actor = l_aMR_work.ftr_actor_list + ftr_id;
                                    aFTR_PROFILE* profile = aMR_GetFurnitureProfile(ftr_actor->name);

                                    if (aFTR_IS_STORAGE(profile)) {
                                        ftr_actor->items[idx] = *fg_top_p;
                                        *fg_top_p = EMPTY_NO;
                                    }
                                }
                            }

                            fg_top_p++;
                        }
                    }
                }

                idx++;
            }
        }
    }
}

static void aMR_ClearSwitchSaveData(MY_ROOM_ACTOR* my_room) {
    u64* switch_bit_table[2];
    int i;

    for (i = mCoBG_LAYER0; i < mCoBG_LAYER2; i++) {
        switch_bit_table[i] = aMR_GetBitSwitchTable(i, my_room);

        if (switch_bit_table[i] != NULL) {
            *switch_bit_table[i] = 0;
        }
    }
}

static TempoBeat_c* aMR_NowSceneWaltzTempo(MY_ROOM_ACTOR* my_room) {
    mActor_name_t field_id = mFI_GetFieldId();

    if (my_room->scene == SCENE_COTTAGE_MY) {
        return &Save_Get(island).cottage.room.tempo_beat;
    } else if (mFI_GET_TYPE(field_id) == mFI_FIELD_PLAYER0_ROOM) {
        int idx = (mFI_GetFieldId() - mFI_FIELD_PLAYER0_ROOM) & 3;
        int floor_no = mFI_GetPlayerHouseFloorNo(my_room->scene);

        if (floor_no != -1) {
            return &mHm_FLOOR(idx, floor_no).tempo_beat;
        }
    }

    return NULL;
}

static void aMR_GetSavedWaltzTempo(MY_ROOM_ACTOR* my_room) {
    sAdo_SetRhythmInfo(aMR_NowSceneWaltzTempo(my_room));
}

static void aMR_SaveWaltzTempo(MY_ROOM_ACTOR* my_room) {
    TempoBeat_c* rhythm = aMR_NowSceneWaltzTempo(my_room);

    if (rhythm != NULL && Common_Get(rhythym_updated) == FALSE) {
        sAdo_GetRhythmInfo(rhythm);
        Common_Set(rhythym_updated, TRUE);
    }
}

extern void aMR_SaveWaltzTempo2(void) {
    if (aMR_CLIP != NULL) {
        ACTOR* actorx = aMR_CLIP->my_room_actor_p;

        if (actorx != NULL) {
            MY_ROOM_ACTOR* my_room = (MY_ROOM_ACTOR*)actorx;

            aMR_SaveWaltzTempo(my_room);
            aMR_SaveHaniwaStepData(my_room);
        }
    }
}

static int aMR_CheckRoomOwner(u32 player_no, MY_ROOM_ACTOR* my_room) {
    if (Common_Get(field_type) == mFI_FIELDTYPE2_PLAYER_ROOM || my_room->scene == SCENE_COTTAGE_MY) {
        return TRUE;
    }
#ifdef TARGET_PC
    if (aMR_pc_field) {
        return TRUE; /* the town's furniture is everyone's */
    }
#endif

    return FALSE;
}

static void aMR_MakeRoomInfo(MY_ROOM_ACTOR* my_room) {
    u32 player_no = Common_Get(player_no);

    my_room->room_info.shop_flag = FALSE;
    my_room->room_info.owner_flag = FALSE;

    if (mFI_CheckShop() == TRUE || mFI_GetFieldId() == mFI_FIELD_ROOM_BROKER_SHOP) {
        my_room->room_info.shop_flag = TRUE;
    }

    if (aMR_CheckRoomOwner(player_no, my_room)) {
        my_room->room_info.owner_flag = TRUE;
    }
}

static void aMR_OneMDFurnitureSwitchOn(void) {
    FTR_ACTOR* ftr_actor = l_aMR_work.ftr_actor_list;
    u8* used = l_aMR_work.used_list;
    int i;

    for (i = 0; i < l_aMR_work.list_size; i++) {
        if (*used) {
            aFTR_PROFILE* profile = aMR_GetFurnitureProfile(ftr_actor->name);

            if (profile != NULL &&
                aFTR_CHECK_INTERACTION(profile->interaction_type, aFTR_INTERACTION_TYPE_MUSIC_DISK) &&
                !(ftr_actor->items[0] >= ITM_MINIDISK_START && ftr_actor->items[0] < ITM_MINIDISK_END)) {
                ftr_actor->switch_bit = FALSE;
                ftr_actor->switch_changed_flag = FALSE;
                ftr_actor->haniwa_state = 0;
            }
        }

        ftr_actor++;
        used++;
    }
}

static void aMR_InitFurnitureWork(void) {
    int i;
    int j;

    for (i = mCoBG_LAYER0; i < mCoBG_LAYER2; i++) {
        aMR_place_t* place_p = aMR_place_table[i & 1];

        for (j = 0; j < aMR_UT_TOTAL_NUM; j++) {
            if (*place_p != aMR_NO_FTR_ID2 && *place_p != aMR_NO_FTR_ID) {
                *place_p = aMR_NO_FTR_ID;
            }

            place_p++;
        }
    }
}

static void aMR_DeleteMusicWhichMusicBoxDontHave(void) {
    FTR_ACTOR* ftr_actor = l_aMR_work.ftr_actor_list;
    u8* used = l_aMR_work.used_list;
    int i;

    if (mFI_GET_TYPE(mFI_GetFieldId()) == mFI_FIELD_PLAYER0_ROOM) {
        for (i = 0; i < l_aMR_work.list_size; i++) {
            if (*used == TRUE) {
                aFTR_PROFILE* profile = aMR_GetFurnitureProfile(ftr_actor->name);

                if (profile != NULL &&
                    aFTR_CHECK_INTERACTION(profile->interaction_type, aFTR_INTERACTION_TYPE_MUSIC_DISK) &&
                    (ftr_actor->items[0] >= ITM_MINIDISK_START && ftr_actor->items[0] < ITM_MINIDISK_END)) {
                    int idx = (mFI_GetFieldId() - mFI_FIELD_PLAYER0_ROOM) & 3;
                    int music_idx = (mActor_name_t)(ftr_actor->items[0] - ITM_MINIDISK_START);
                    u32* music_box = Save_Get(scene_no) == SCENE_COTTAGE_MY ? Save_Get(island).cottage.music_box
                                                                            : Save_Get(homes[idx]).music_box;

                    if (((music_box[(music_idx / 32) & 1] >> (music_idx & 31)) & 1) == 0) {
                        ftr_actor->items[0] = EMPTY_NO;

                        if (ftr_actor->switch_bit == TRUE) {
                            ftr_actor->switch_bit = FALSE;
                            ftr_actor->haniwa_state = 0;
                        }
                    }
                }
            }

            ftr_actor++;
            used++;
        }
    }
}

static void aMR_SetMDIslandNPC(void) {
    FTR_ACTOR* ftr_actor = l_aMR_work.ftr_actor_list;
    u8* used = l_aMR_work.used_list;
    int i;
    int md_set = FALSE;

    if (Save_Get(scene_no) == SCENE_COTTAGE_NPC) {
        for (i = 0; i < l_aMR_work.list_size; i++) {
            if (*used) {
                aFTR_PROFILE* profile = aMR_GetFurnitureProfile(ftr_actor->name);

                if (profile != NULL &&
                    aFTR_CHECK_INTERACTION(profile->interaction_type, aFTR_INTERACTION_TYPE_MUSIC_DISK)) {
                    if (md_set == FALSE) {
                        ftr_actor->items[0] = ITM_MINIDISK_START + mNpc_GetIslandMDIdx();
                        ftr_actor->switch_bit = TRUE;
                        ftr_actor->haniwa_state = 1;
                        md_set = TRUE;
                    } else {
                        ftr_actor->switch_bit = FALSE;
                        ftr_actor->haniwa_state = 0;
                    }
                }
            }

            ftr_actor++;
            used++;
        }
    }
}

static void My_Room_Actor_ct(ACTOR* actorx, GAME* game) {
    MY_ROOM_ACTOR* my_room = (MY_ROOM_ACTOR*)actorx;
    GAME_PLAY* play = (GAME_PLAY*)game;

    bzero(&l_aMR_work, sizeof(l_aMR_work));
    my_room->scene = Save_Get(scene_no);
#ifdef TARGET_PC
    aMR_pc_set_grid(Common_Get(field_type) == mFI_FIELDTYPE2_FG);
#endif
    aMR_SetClip(actorx);
    aMR_InitFurnitureWork();
    aMR_SetMelodyData(my_room->melody);
    my_room->goki_ct_proc = &aMR_GokiInfoCt;
    aMR_MakeRoomInfo(my_room);
    aMR_GetSavedWaltzTempo(my_room);
    l_aMR_work.list_size = aMR_GetSceneFurnitureMax();
    aMR_SecureFurnitureRam(actorx);
    aMR_InitFurnitureTable(l_aMR_work.ftr_actor_list, l_aMR_work.list_size);
    aMR_InitFurnitureActorExistTable();
    aMR_SecureFurnitureBank(my_room, game);
    aMR_InitHaniwaOnTable(actorx);
    my_room->state = 0;
    my_room->clock_info.phase_frame = (f32)(play->game_frame % 120);
    aMR_InitFurnitureBankTable();
    aMR_MakeFurnitureActor(actorx, play, mCoBG_LAYER0);
    aMR_MakeFurnitureActor(actorx, play, mCoBG_LAYER1);
#ifdef TARGET_PC
    if (aMR_pc_field) {
        aMR_pc_field_ct();
    }
#endif
    my_room->parent_ftr.ftrID = -1;
    Common_Set(make_npc2_actor, TRUE);
    aMR_MakeItemDataInFurniture();
#ifdef TARGET_PC
    aMR_pc_dresser_load_ids();
#endif
    aMR_DeleteMusicWhichMusicBoxDontHave();
    aMR_InitDummyKeyAnime(play, actorx);
    my_room->allow_rotation_flag = TRUE;
    my_room->sit_timer = 0;
    my_room->bed_timer = 0;
    my_room->msg_type = 0;
    my_room->requested_msg_type = my_room->msg_type;
    my_room->bgm_info.reserve_flag = FALSE;
    my_room->bgm_info.md_no = -1;
    my_room->bgm_info.last_md_no = -1;
    aMR_ClearSwitchSaveData(my_room);
    aMR_OneMDFurnitureSwitchOn();
    aMR_SetMDIslandNPC();
    my_room->emu_info.request_flag = FALSE;
    my_room->emu_info.explaination_given_flag = FALSE;
    mCkRh_InitCanLookGokiCount();
}

// Part 3

static void aMR_FreeMallocBank(ACTOR* actorx) {
    MY_ROOM_ACTOR* my_room = (MY_ROOM_ACTOR*)actorx;
    int i;

    if (my_room->bank_count1 != 0) {
        for (i = 0; i < my_room->bank_count1; i++) {
            zelda_free(l_bank_address_table[my_room->bank_count0 + i]);
        }
    }
}

static void aMR_FreeHeapArea(ACTOR* actorx) {
    MY_ROOM_ACTOR* my_room = (MY_ROOM_ACTOR*)actorx;

    if (l_aMR_work.ftr_actor_list != NULL) {
        zelda_free(l_aMR_work.ftr_actor_list);
    }

    if (l_aMR_work.used_list != NULL) {
        zelda_free(l_aMR_work.used_list);
    }

    aMR_FreeMallocBank(actorx);

    if (my_room->emu_info.famicom_names_p != NULL) {
        zelda_free(my_room->emu_info.famicom_names_p);
    }

    if (my_room->bank0_p != NULL) {
        zelda_free(my_room->bank0_p);
    }
}

#ifdef TARGET_PC
/* The room's place among the dressers' storages (pc_dresser.c): a resident's house floor or the cottage */
static int aMR_pc_dresser_room(int* house, int* floor) {
    MY_ROOM_ACTOR* my_room;
    mActor_name_t field_id = mFI_GetFieldId();

    if (aMR_CLIP == NULL || aMR_CLIP->my_room_actor_p == NULL || l_aMR_work.ftr_actor_list == NULL ||
        l_aMR_work.used_list == NULL) {
        return FALSE;
    }
    my_room = (MY_ROOM_ACTOR*)aMR_CLIP->my_room_actor_p;
    if (aMR_pc_field) {
        *house = PC_DRESSER_TOWN;
        *floor = 0;
        return pc_oftr_here() && aMR_pc_acre_x >= 0;
    }
    if (my_room->scene == SCENE_COTTAGE_MY) {
        *house = PC_DRESSER_HOUSES - 1;
    } else if (mFI_GET_TYPE(field_id) == mFI_FIELD_PLAYER0_ROOM) {
        *house = (field_id - mFI_FIELD_PLAYER0_ROOM) & 3;
    } else {
        return FALSE;
    }
    *floor = mFI_GetPlayerHouseFloorNo(my_room->scene);
    return *floor >= 0;
}

static int aMR_pc_dresser_unit(FTR_ACTOR* ftr_actor) {
    int ut_x;
    int ut_z;

    if (!aMR_Wpos2PlaceNumber(&ut_x, &ut_z, ftr_actor->position, ftr_actor->shape_type)) {
        return -1;
    }
    return ut_x + ut_z * aMR_UT_X_NUM;
}

/* The room just made its furniture: each piece of storage furniture gets its storage ID back */
static void aMR_pc_dresser_load_ids(void) {
    int house, floor, i;

    if (!aMR_pc_dresser_room(&house, &floor)) {
        return;
    }
    for (i = 0; i < l_aMR_work.list_size; i++) {
        FTR_ACTOR* ftr_actor = &l_aMR_work.ftr_actor_list[i];

        if (l_aMR_work.used_list[i] && aFTR_IS_STORAGE(aMR_GetFurnitureProfile(ftr_actor->name))) {
            int ut = aMR_pc_dresser_unit(ftr_actor);

            ftr_actor->pc_dresser_id = ut >= 0 ? pc_dresser_bound(house, floor, ftr_actor->layer, ut) : 0;
        }
    }
}

/* Dev (pc_dresser_dev_dump): the room's storage furniture */
void aMR_pc_dresser_list(void) {
    int house, floor, i;

    if (!aMR_pc_dresser_room(&house, &floor)) {
        return;
    }
    for (i = 0; i < l_aMR_work.list_size; i++) {
        FTR_ACTOR* ftr_actor = &l_aMR_work.ftr_actor_list[i];

        if (l_aMR_work.used_list[i] && aFTR_IS_STORAGE(aMR_GetFurnitureProfile(ftr_actor->name))) {
            int ut = aMR_pc_dresser_unit(ftr_actor);

            printf("[Dresser] in the room (house %d floor %d): furniture %03X at %.0f,%.0f (unit %d,%d layer %d), "
                   "storage %d\n", house, floor, ftr_actor->name, ftr_actor->position.x, ftr_actor->position.z,
                   ut % aMR_UT_X_NUM, ut / aMR_UT_X_NUM, ftr_actor->layer, ftr_actor->pc_dresser_id);
        }
    }
}

/* Where the room's storage furniture stands now, for the save (pc_dresser.c calls this before writing) */
void aMR_pc_dresser_flush(void) {
    int house, floor, i;

    if (!aMR_pc_dresser_room(&house, &floor)) {
        return;
    }
    if (aMR_pc_field) {
        /* only the loaded acres: the others keep what they had */
        pc_dresser_town_clear(aMR_UT_X_NUM, (aMR_pc_acre_x - 1) * UT_X_NUM, (aMR_pc_acre_z - 1) * UT_Z_NUM,
                              (aMR_pc_acre_x + 2) * UT_X_NUM, (aMR_pc_acre_z + 2) * UT_Z_NUM);
    } else {
        pc_dresser_room_clear(house, floor);
    }
    for (i = 0; i < l_aMR_work.list_size; i++) {
        FTR_ACTOR* ftr_actor = &l_aMR_work.ftr_actor_list[i];

        if (l_aMR_work.used_list[i] && ftr_actor->pc_dresser_id != 0) {
            int ut = aMR_pc_dresser_unit(ftr_actor);

            if (ut >= 0) {
                pc_dresser_room_bind(house, floor, ftr_actor->layer, ut, ftr_actor->pc_dresser_id);
            }
        }
    }
}
#endif

static void aMR_KeepItem2Fg(FTR_ACTOR* ftr_actor) {
    int idx = 0;
    int i;

    for (i = ftr_actor->layer + 1; i < mCoBG_LAYER_NUM; i++) {
        if (ftr_actor->items[idx] != EMPTY_NO) {
            mActor_name_t* fg_p = aMR_GetLayerTopFg(i);

            if (fg_p != NULL) {
                int ut_x;
                int ut_z;

                if (aMR_Wpos2PlaceNumber(&ut_x, &ut_z, ftr_actor->position, ftr_actor->shape_type)) {
                    fg_p[ut_x + ut_z * aMR_UT_X_NUM] = ftr_actor->items[idx];
                    ftr_actor->items[idx] = EMPTY_NO;
                }
            }
        }

        idx++;
    }
}

static void aMR_AllFurnitureDestruct(ACTOR* actorx, GAME* game) {
    FTR_ACTOR* ftr_actor = l_aMR_work.ftr_actor_list;
    u8* used = l_aMR_work.used_list;
    int i;

    sAdo_RhythmAllStop();
#ifdef TARGET_PC
    aMR_pc_dresser_flush(); /* the last word on where the storage furniture stands */
#endif
    if (ftr_actor != NULL && used != NULL) {
        for (i = 0; i < l_aMR_work.list_size; i++) {
            if (*used) {
                aFTR_PROFILE* profile = aMR_GetFurnitureProfile(ftr_actor->name);

                mCoBG_CrossOffMoveBg(ftr_actor->move_bg_idx);
                aMR_KeepItem2Fg(ftr_actor);
                aMR_MiniDiskCommonDt(ftr_actor, actorx);
                aMR_RadioCommonDt(ftr_actor, actorx);
                aMR_MinusWeight(actorx, ftr_actor);

                if (profile != NULL && profile->vtable != NULL && profile->vtable->dt_proc != NULL) {
                    profile->vtable->dt_proc(ftr_actor, aMR_FtrNo2BankAddress(ftr_actor->name));
                }

                *used = FALSE;
            }

            used++;
            ftr_actor++;
        }
    }
}

static void aMR_LeafPickuped(void) {
    if (aMR_CLIP != NULL) {
        ACTOR* actorx = aMR_CLIP->my_room_actor_p;

        if (actorx != NULL) {
            MY_ROOM_ACTOR* my_room = (MY_ROOM_ACTOR*)actorx;

            my_room->pickup_info.pickup_flag = FALSE;
        }
    }
}

static int aMR_PickupFtrLayer(void) {
    if (aMR_CLIP != NULL) {
        ACTOR* actorx = aMR_CLIP->my_room_actor_p;

        if (actorx != NULL) {
            MY_ROOM_ACTOR* my_room = (MY_ROOM_ACTOR*)actorx;

            return my_room->pickup_info.layer;
        }
    }

    return mCoBG_LAYER0;
}

#include "../src/actor/ac_my_room_goki.c_inc"

static void aMR_LeafStartPos(xyz_t* pos) {
    static xyz_t leaf_start0 = { 0.0f, 0.0f, 0.0f };

    *pos = leaf_start0;
    if (aMR_CLIP != NULL) {
        ACTOR* actorx = aMR_CLIP->my_room_actor_p;

        if (actorx != NULL) {
            MY_ROOM_ACTOR* my_room = (MY_ROOM_ACTOR*)actorx;

            *pos = my_room->pickup_info.leaf_pos;
        }
    }
}

static int aMR_Ftr2Leaf(void) {
    if (aMR_CLIP != NULL) {
        ACTOR* actorx = aMR_CLIP->my_room_actor_p;

        if (actorx != NULL) {
            MY_ROOM_ACTOR* my_room = (MY_ROOM_ACTOR*)actorx;

            return my_room->pickup_info.picking_up_flag;
        }
    }

    return FALSE;
}

static int aMR_SetLeaf(const xyz_t* pos, f32 scale) {
    if (aMR_CLIP != NULL) {
        ACTOR* actorx = aMR_CLIP->my_room_actor_p;

        if (actorx != NULL) {
            MY_ROOM_ACTOR* my_room = (MY_ROOM_ACTOR*)actorx;

            if (my_room->leaf_info.exist_flag == FALSE) {
                my_room->leaf_info.pos = *pos;
                my_room->leaf_info.scale = scale;
                my_room->leaf_info.exist_flag = TRUE;
                return TRUE;
            }

            return FALSE;
        }
    }

    return FALSE;
}

static void My_Room_Actor_dt(ACTOR* actorx, GAME* game) {
    MY_ROOM_ACTOR* my_room = (MY_ROOM_ACTOR*)actorx;

    aMR_SaveSwitchData(my_room);
    aMR_AllFurnitureDestruct(actorx, game);
#ifdef TARGET_PC
    if (aMR_pc_field) {
        aMR_pc_field_dt();
    }
#endif
    aMR_FreeHeapArea(actorx);
#ifdef TARGET_PC
    /* a save made after leaving (a shop has no furniture actor) mustn't read the freed list (aMR_pc_items_to_fg) */
    l_aMR_work.ftr_actor_list = NULL;
    l_aMR_work.used_list = NULL;
    aMR_pc_field = FALSE;
#endif
    aMR_GokiInfoDt();
    aMR_CLIP = NULL;
    mCkRh_SetGoingOutCottageTime(my_room->scene);

    /* TODO: enums for this field */
    if ((Common_Get(my_room_message_control_flags) >> 3) & 1) {
        Common_Set(my_room_message_control_flags, 0);
        Common_Get(my_room_message_control_flags) &= ~2;
        Common_Get(my_room_message_control_flags) &= ~1;
        Common_Get(my_room_message_control_flags) |= 8;
        sAdo_SubGameStart();
    } else if ((Common_Get(my_room_message_control_flags) & 0x10)) {
        Common_Set(my_room_message_control_flags, 0);
        Common_Get(my_room_message_control_flags) &= ~2;
        Common_Get(my_room_message_control_flags) &= ~1;
        Common_Get(my_room_message_control_flags) |= 0x10;
        sAdo_SubGameStart();
    } else {
        Common_Set(my_room_message_control_flags, 0);
        Common_Get(my_room_message_control_flags) &= ~2;
        Common_Get(my_room_message_control_flags) &= ~1;
    }
}

#include "../src/actor/ac_my_room_melody.c_inc"
#include "../src/actor/ac_my_room_move.c_inc"
#include "../src/actor/ac_my_room_draw.c_inc"

static void aMR_RedmaFtrBank(void) {
    int i;

    for (i = 0; i < FTR_NUM; i++) {
        if (l_bank_index_table[i] != aMR_NO_BANK) {
            u8* bank_addr = aMR_BankNo2BankAddress(l_bank_index_table[i]);
            mActor_name_t item = mRmTp_FtrIdx2FtrItemNo(i, mRmTp_DIRECT_SOUTH);

            aMR_DmaFurniture_Common(i, item, bank_addr, -1);
        }
    }
}

static void aMR_RequestStartEmu(MY_ROOM_ACTOR* my_room, FTR_ACTOR* ftr_actor, int game_idx, int agb_game_idx) {
    if (my_room->emu_info.request_flag == FALSE && mMsg_Check_MainHide(mMsg_Get_base_window_p())) {
        my_room->emu_info.request_flag = TRUE;
        my_room->emu_info.rom_no = game_idx;
        my_room->emu_info.agb_rom_no = agb_game_idx;
        my_room->emu_info.explaination_given_flag = FALSE;
        my_room->emu_info._10 = 0;
        my_room->requested_msg_type = aMR_MSG_STATE_QQQ_EMULATOR;
        my_room->emu_ftrID = ftr_actor->id;
    }
}

static void aMR_RequestStartEmu_MemoryC(MY_ROOM_ACTOR* my_room, FTR_ACTOR* ftr_actor, int game_idx) {
    if (my_room->emu_info.request_flag == FALSE && mMsg_Check_MainHide(mMsg_Get_base_window_p())) {
        int card_count = aMR_GetCardFamicomCount();

        my_room->emu_info.card_famicom_count = card_count;
        my_room->emu_info.memory_game_select = 0;

        if (card_count > 0) {
            size_t namebuf_size = card_count * mIN_ITEM_NAME_LEN;

            if (my_room->emu_info.famicom_names_p != NULL) {
                zelda_free(my_room->emu_info.famicom_names_p);
            }

            my_room->emu_info.famicom_names_p = (char*)zelda_malloc(namebuf_size);

            if (my_room->emu_info.famicom_names_p != NULL) {
                int n_games = 0;

                if (famicom_get_disksystem_titles(&n_games, my_room->emu_info.famicom_names_p, namebuf_size) == FALSE) {
                    my_room->requested_msg_type = aMR_MSG_STATE_NO_PACK_NO_DATA;
                    my_room->room_msg_flag = TRUE;
                    return;
                }
            }

            if (card_count == 1) {
                my_room->emu_info.request_flag = TRUE;
                my_room->emu_info.rom_no = game_idx;
                my_room->emu_info.explaination_given_flag = FALSE;
                my_room->requested_msg_type = aMR_MSG_STATE_QQQ_EMULATOR_MEMORY1;
                my_room->emu_ftrID = ftr_actor->id;
                my_room->emu_info._10 = 0;
            } else if (card_count == 2) {
                my_room->emu_info.request_flag = TRUE;
                my_room->emu_info.rom_no = game_idx;
                my_room->emu_info.explaination_given_flag = FALSE;
                my_room->requested_msg_type = aMR_MSG_STATE_QQQ_EMULATOR_MEMORY2;
                my_room->emu_ftrID = ftr_actor->id;
                my_room->emu_info._10 = 0;
            } else {
                my_room->emu_info.request_flag = TRUE;
                my_room->emu_info.rom_no = game_idx;
                my_room->emu_info.explaination_given_flag = FALSE;
                my_room->requested_msg_type = aMR_MSG_STATE_QQQ_EMULATOR_MEMORY_OVER3;
                my_room->emu_ftrID = ftr_actor->id;
                my_room->emu_info._10 = 0;
            }
        } else {
            my_room->requested_msg_type = aMR_MSG_STATE_NO_PACK_NO_DATA;
            my_room->room_msg_flag = TRUE;
        }
    }
}

static void aMR_FamicomEmuCommonMove(FTR_ACTOR* ftr_actor, ACTOR* actorx, GAME* game, int rom_no, int agb_rom_no) {
    MY_ROOM_ACTOR* my_room = (MY_ROOM_ACTOR*)actorx;

    if (ftr_actor->switch_changed_flag) {
        if (rom_no == 0) {
            aMR_RequestStartEmu_MemoryC(my_room, ftr_actor, 0);
        } else {
            aMR_RequestStartEmu(my_room, ftr_actor, rom_no, agb_rom_no);
        }
    }
}

static void aMR_CallSitDownOngenPosSE(const xyz_t* pos) {
    mActor_name_t item_no;
    int ftrID;
    xyz_t sit_pos = *pos;
    int ut_x = 0;
    int ut_z = 0;

    sit_pos.y = 0.0f;
    sit_pos.y = mCoBG_GetBgY_OnlyCenter_FromWpos(sit_pos, 0.0f);
    if (mFI_Wpos2UtNum(&ut_x, &ut_z, sit_pos) &&
        aMR_UnitNum2FtrItemNoFtrID(&item_no, &ftrID, ut_x, ut_z, mCoBG_LAYER0)) {
        FTR_ACTOR* ftr_actor = l_aMR_work.ftr_actor_list + ftrID;

        /* Check for massage chair */
        switch (ftr_actor->name) {
            case FTR_KON_MASAJI:
                sAdo_OngenPos((u32)ftr_actor, 39, &ftr_actor->position);
                break;
        }
    }
}

static int aMR_CheckDannaKill(xyz_t* pos) {
    mActor_name_t* fg_p = mFI_GetUnitFG(*pos);

    /* @BUG - this doesn't check for newer furniture in the 0x3XXX range */
    if (fg_p != NULL && ((*fg_p >= FTR0_START && *fg_p <= ITEM1_NO_START) || *fg_p == RSV_FE1F)) {
        int ut_x;
        int ut_z;

        if (mFI_Wpos2UtNum(&ut_x, &ut_z, *pos) && aMR_CLIP != NULL) {
            mActor_name_t item_no;
            int ftrID;

            if (aMR_UnitNum2FtrItemNoFtrID(&item_no, &ftrID, aMR_LOCAL_UT(ut_x), aMR_LOCAL_UT(ut_z), mCoBG_LAYER0)) {
                FTR_ACTOR* ftr_actor = l_aMR_work.ftr_actor_list + ftrID;
                aFTR_PROFILE* profile = aMR_GetFurnitureProfile(ftr_actor->name);

                if (profile != NULL &&
                    aFTR_CHECK_INTERACTION(profile->interaction_type, aFTR_INTERACTION_TYPE_NO_COLLISION) == FALSE &&
                    aMR_CheckFtrAndGoki2(ftr_actor->edge_collision, pos)) {
                    return TRUE;
                }
            }
        }
    }

    return FALSE;
}

extern int aMR_GetFurnitureUnit(mActor_name_t item) {
    if (ITEM_IS_FTR(item)) {
        int ftr_idx = mRmTp_FtrItemNo2FtrIdx(item);
        aFTR_PROFILE* profile = aMR_GetFurnitureProfile(ftr_idx);

        if (profile != NULL) {
            switch (profile->shape) {
                case aFTR_SHAPE_TYPEA:
                    return 0;
                case aFTR_SHAPE_TYPEC:
                    return 2;
                default:
                    return 1;
            }
        }
    }

    return -1;
}

extern int aMR_CorrespondFurniture(mActor_name_t ftr0, mActor_name_t ftr1) {
    if (ITEM_IS_FTR(ftr0) && ITEM_IS_FTR(ftr1)) {
        int ftr_idx0 = mRmTp_FtrItemNo2FtrIdx(ftr0);
        int ftr_idx1 = mRmTp_FtrItemNo2FtrIdx(ftr1);

        if (ftr_idx0 == ftr_idx1) {
            return TRUE;
        }
    }

    return FALSE;
}

extern mActor_name_t aMR_FurnitureFg_to_FurnitureFgWithDirect(mActor_name_t item, int direct) {
    if (ITEM_IS_FTR(item)) {
        return mRmTp_FtrIdx2FtrItemNo(mRmTp_FtrItemNo2FtrIdx(item), direct);
    }

    return item;
}

extern void aMR_RadioCommonMove(FTR_ACTOR* ftr_actor, ACTOR* actorx) {
    if (ftr_actor->haniwa_state == 1) {
        aMR_ReserveBgm(actorx, BGM_SPORTSFAIR_AEROBICS, ftr_actor, 0);
        ftr_actor->haniwa_state = 0;
    } else if (ftr_actor->switch_changed_flag) {
        if (ftr_actor->switch_bit == FALSE) {
            aMR_OneMDSwitchOn_TheOtherSwitchOff(ftr_actor);
            aMR_ReserveDefaultBgm(actorx, ftr_actor);
            aMR_ChangeMDBgm(actorx, ftr_actor);
            ftr_actor->switch_bit = FALSE;
        } else {
            aMR_OneMDSwitchOn_TheOtherSwitchOff(ftr_actor);
            aMR_ReserveBgm(actorx, BGM_SPORTSFAIR_AEROBICS, ftr_actor, 0);
            aMR_ChangeMDBgm(actorx, ftr_actor);
            ftr_actor->switch_bit = TRUE;
        }
    }
}

extern int aMR_RadioBgmNow(void) {
    if (mBGMPsComp_execute_bgm_num_get() == BGM_SPORTSFAIR_AEROBICS) {
        return TRUE;
    }

    return FALSE;
}

extern aMR_contact_info_c* aMR_GetContactInfoLayer1(void) {
    if (aMR_CLIP != NULL) {
        ACTOR* actorx = aMR_CLIP->my_room_actor_p;

        if (actorx != NULL) {
            MY_ROOM_ACTOR* my_room = (MY_ROOM_ACTOR*)actorx;

            return &my_room->contact0;
        }
    }

    return NULL;
}

extern FTR_ACTOR* aMR_GetParentFactor(FTR_ACTOR* ftr_actor, ACTOR* actorx) {
    if (actorx != NULL) {
        MY_ROOM_ACTOR* my_room = (MY_ROOM_ACTOR*)actorx;

        if (my_room->parent_ftr.ftrID != -1) {
            FTR_ACTOR* parent_ftr_actor = l_aMR_work.ftr_actor_list + my_room->parent_ftr.ftrID;

            if (my_room->parent_ftr.fit_ftr_table[0].ftr_ID == ftr_actor->id ||
                my_room->parent_ftr.fit_ftr_table[1].ftr_ID == ftr_actor->id ||
                my_room->parent_ftr.fit_ftr_table[2].ftr_ID == ftr_actor->id ||
                my_room->parent_ftr.fit_ftr_table[3].ftr_ID == ftr_actor->id) {
                return parent_ftr_actor;
            }
        }
    }

    return NULL;
}

extern s16 aMR_GetParentAngleOffset(FTR_ACTOR* ftr_actor, ACTOR* actorx) {
    if (actorx != NULL) {
        MY_ROOM_ACTOR* my_room = (MY_ROOM_ACTOR*)actorx;
        if (my_room->parent_ftr.ftrID != -1) {
            FTR_ACTOR* parent_ftr_actor = l_aMR_work.ftr_actor_list + my_room->parent_ftr.ftrID;

            if (my_room->parent_ftr.fit_ftr_table[0].ftr_ID == ftr_actor->id ||
                my_room->parent_ftr.fit_ftr_table[1].ftr_ID == ftr_actor->id ||
                my_room->parent_ftr.fit_ftr_table[2].ftr_ID == ftr_actor->id ||
                my_room->parent_ftr.fit_ftr_table[3].ftr_ID == ftr_actor->id) {
                return parent_ftr_actor->s_angle_y - my_room->parent_ftr.angle_y;
            }
        }
    }

    return 0;
}

extern u8 aMR_GetAlphaEdge(u16 ftr_name) {
    switch (ftr_name) {
        case FTR_SUM_CLASSICWARDROPE01:  // classic cabinet
        case FTR_SUM_VIOLA01: // violin
        case FTR_SUM_BASS01: // bass (instrument)
        case FTR_SUM_CELLO01: // cello
            return 11;
        case FTR_KON_AMECLOCK: // kitschy clock
        case FTR_KON_ATQCLOCK: // antique clock
            return 250;
        case FTR_NOG_BALLOON_COMMON0: // red balloon
        case FTR_NOG_BALLOON_COMMON1: // yellow balloon
        case FTR_NOG_BALLOON_COMMON2: // blue balloon
        case FTR_NOG_BALLOON_COMMON3: // green balloon
        case FTR_NOG_BALLOON_COMMON4: // purple balloon
        case FTR_NOG_BALLOON_COMMON5: // bunny p. balloon
        case FTR_NOG_BALLOON_COMMON6: // bunny b. balloon
        case FTR_NOG_BALLOON_COMMON7: // bunny o. balloon
            return 96;
        case FTR_IKE_K_TANABATA01:
            return 127; // tanabata palm
        case FTR_YOS_WHEEL:
            return 20; // wagon wheel
        default:
            return 127;
    }
}

extern int aMR_DrawDolphinMode(u16 ftr_name) {
    switch (ftr_name) {
        case FTR_TAK_MONEY: // stone coin
        case FTR_TAK_HAM1: // hamster cage
        case FTR_IKE_PRORES_LING01: // neutral corner
        case FTR_IKE_PRORES_LING02: // red corner
        case FTR_IKE_PRORES_LING03: // blue corner
            return TRUE;
        default:
            return FALSE;
    }
}

extern void aMR_ThrowItem_FurnitureLock(void) {
    if (aMR_CLIP != NULL) {
        ACTOR* actorx = aMR_CLIP->my_room_actor_p;

        if (actorx != NULL) {
            MY_ROOM_ACTOR* my_room = (MY_ROOM_ACTOR*)actorx;

            my_room->throw_item_lock_flag = TRUE;
        }
    }
}

extern void aMR_ThrowItem_FurnitureUnlock(void) {
    if (aMR_CLIP != NULL) {
        ACTOR* actorx = aMR_CLIP->my_room_actor_p;

        if (actorx != NULL) {
            MY_ROOM_ACTOR* my_room = (MY_ROOM_ACTOR*)actorx;

            my_room->throw_item_lock_flag = FALSE;
        }
    }
}

extern void aMR_SameFurnitureSwitchOFF(u16 ftr_name) {
    FTR_ACTOR* ftr_actor = l_aMR_work.ftr_actor_list;
    u8* used = l_aMR_work.used_list;
    int i;

    for (i = 0; i < l_aMR_work.list_size; i++) {
        if (*used && ftr_actor->name == ftr_name) {
            ftr_actor->switch_bit = FALSE;
            ftr_actor->switch_changed_flag = TRUE;
        }

        ftr_actor++;
        used++;
    }
}

#ifdef TARGET_PC
/* Rooms: one block of 16 x 16 units at the world's origin. Field mode (outdoors in the town): the whole town map,
 * whose units are the world's own (pc_outdoor_ftr.c). */
static void aMR_pc_set_grid(int field) {
    int w = field ? PC_OFTR_UT_X_NUM : UT_X_NUM;
    int i;

    aMR_pc_field = field;
    aMR_pc_home = field && pc_oftr_here();
    aMR_pc_md_far = FALSE;
    aMR_pc_acre_x = -1;
    aMR_pc_acre_z = -1;
    aMR_ut_x_num = w;
    aMR_ut_z_num = field ? PC_OFTR_UT_Z_NUM : UT_Z_NUM;
    aMR_poccess_typeB90.unit_ofs[1] = -w;
    aMR_poccess_typeB270.unit_ofs[1] = w;
    aMR_poccess_typeC.unit_ofs[2] = w;
    aMR_poccess_typeC.unit_ofs[3] = w + 1;
    square_offset_data[2] = -w;
    square_offset_data[3] = -(w + 1);
    if (field) {
        for (i = 0; i < 2; i++) {
            int ut;

            aMR_place_table[i] = pc_oftr_place_table(i);
            for (ut = 0; ut < PC_OFTR_UT_TOTAL_NUM; ut++) {
                aMR_place_table[i][ut] = aMR_NO_FTR_ID;
            }
        }
    } else {
        for (i = 0; i < UT_TOTAL_NUM; i++) {
            l_place_table[i] = l_place_table_gc[i] == 201 ? aMR_NO_FTR_ID2 : aMR_NO_FTR_ID;
            l_place_table2[i] = l_place_table2_gc[i] == 201 ? aMR_NO_FTR_ID2 : aMR_NO_FTR_ID;
        }
        aMR_place_table[0] = l_place_table;
        aMR_place_table[1] = l_place_table2;
    }
}

/* The town's markers under every piece, loaded or not */
/* Items lying on furniture outdoors: the town has no shop goods actor (a room's draws and drops them), so the
 * furniture actor stands in for it over its grid (layer 1 of pc_outdoor_ftr.c) */
extern void aSG_pc_single_draw(GAME* game, mActor_name_t item, xyz_t* pos, f32 scale, s16 angle_y, int layer);
extern int aSG_pc_field_drop(GAME* game, mActor_name_t item, int ut_x, int ut_z);
extern void aSG_pc_field_move(void);
extern void aSG_pc_field_draw(GAME* game);
extern void aSG_pc_field_flush(void);
static aSG_Clip_c aMR_pc_sg_clip;
static int aMR_pc_sg_owned;

static s16 aMR_pc_sg_get_angle_y(int ut_z, int ut_x, int layer) {
    return layer == mCoBG_LAYER1 ? pc_oftr_angle(ut_x + ut_z * aMR_UT_X_NUM) : 0;
}

static void aMR_pc_sg_set_angle_y(int ut_z, int ut_x, int layer, s16 angle_y) {
    if (layer == mCoBG_LAYER1) {
        pc_oftr_set_angle(ut_x + ut_z * aMR_UT_X_NUM, angle_y);
    }
}

/* Tossed onto the furniture like in a house (ac_shop_goods.c does the flight outdoors too) */
static int aMR_pc_sg_drop_entry(GAME* game, mActor_name_t item, int ut_x, int ut_z, int layer, int delay_timer) {
    if (layer != mCoBG_LAYER1 || ut_x < 0 || ut_z < 0 || ut_x >= aMR_UT_X_NUM || ut_z >= aMR_UT_Z_NUM) {
        return FALSE;
    }
    return aSG_pc_field_drop(game, item, ut_x, ut_z);
}

/* Draw what lies on furniture in the acres around the player */
static void aMR_pc_draw_items_on_top(GAME* game) {
    mActor_name_t* fg_p = aMR_GetLayerTopFg(mCoBG_LAYER1);
    int ut_x;
    int ut_z;

    if (!aMR_pc_field || aMR_pc_acre_x < 0 || fg_p == NULL) {
        return;
    }
    aSG_pc_field_draw(game); /* still flying onto furniture */
    for (ut_z = (aMR_pc_acre_z - 1) * UT_Z_NUM; ut_z < (aMR_pc_acre_z + 2) * UT_Z_NUM; ut_z++) {
        for (ut_x = (aMR_pc_acre_x - 1) * UT_X_NUM; ut_x < (aMR_pc_acre_x + 2) * UT_X_NUM; ut_x++) {
            mActor_name_t item;
            xyz_t pos;

            if (ut_x < 0 || ut_z < 0 || ut_x >= aMR_UT_X_NUM || ut_z >= aMR_UT_Z_NUM) {
                continue;
            }
            item = fg_p[ut_x + ut_z * aMR_UT_X_NUM];
            if (item == EMPTY_NO || item == RSV_NO || item == RSV_FE1F || ITEM_IS_FTR(item)) {
                continue;
            }
            mFI_UtNum2CenterWpos(&pos, ut_x, ut_z);
            pos.y = mCoBG_GetBgY_AngleS_FromWpos(NULL, pos, 0.0f); /* the furniture top */
            aSG_pc_single_draw(game, item, &pos, 0.01f, pc_oftr_angle(ut_x + ut_z * aMR_UT_X_NUM), mCoBG_LAYER1);
        }
    }
}

static void aMR_pc_field_dt(void) {
    aSG_pc_field_flush(); /* what is still flying lands */
    if (aMR_pc_sg_owned) {
        Common_Get(clip).shop_goods_clip = NULL;
        aMR_pc_sg_owned = FALSE;
    }
}

/* Entering the town: a piece whose place the town has since taken (a villager's house built next to it...) goes to
 * the police station's lost and found, with whatever it holds or has on top. One at a time: a full lost and found
 * lets its oldest go, as always */
static void aMR_pc_evict_misplaced(void) {
    static u8 rotate_angle_types[] = {
        aFTR_SHAPE_TYPEB_0,
        aFTR_SHAPE_TYPEB_90,
        aFTR_SHAPE_TYPEB_180,
        aFTR_SHAPE_TYPEB_270,
    };
    mActor_name_t* fg0 = pc_oftr_fg(mCoBG_LAYER0);
    int evicted = 0;
    int ut;

    if (fg0 == NULL || !pc_oftr_here()) {
        return;
    }
    for (ut = 0; ut < PC_OFTR_UT_TOTAL_NUM; ut++) {
        mActor_name_t item = fg0[ut];
        aFTR_PROFILE* profile;
        int ut_info[4];
        int count;
        int ok = TRUE;
        int layer;
        int i;
        u8 shape;

        if (!ITEM_IS_FTR(item) || (profile = aMR_GetFurnitureProfile(mRmTp_FtrItemNo2FtrIdx(item))) == NULL) {
            continue;
        }
        shape = profile->shape == aFTR_SHAPE_TYPEB_0 ? rotate_angle_types[item & 3] : profile->shape;
        count = aMR_GetFurniturePoccessUnitNo(ut_info, ut % aMR_UT_X_NUM, ut / aMR_UT_X_NUM, shape);
        for (i = 0; i < count; i++) {
            if (ut_info[i] < 0 || ut_info[i] >= PC_OFTR_UT_TOTAL_NUM || !pc_oftr_unit_still_ok(ut_info[i])) {
                ok = FALSE;
            }
        }
        if (ok) {
            continue;
        }
        /* what lies on it or in it first, then the piece itself */
        for (layer = mCoBG_LAYER_NUM - 1; layer >= mCoBG_LAYER0; layer--) {
            mActor_name_t* fg_p = pc_oftr_fg(layer);

            for (i = 0; i < count; i++) {
                int u = ut_info[i];

                if (u < 0 || u >= PC_OFTR_UT_TOTAL_NUM || fg_p == NULL) {
                    continue;
                }
                if (ITEM_IS_FTR(fg_p[u])) {
                    mPB_keep_item(mRmTp_FtrItemNo2Item1ItemNo(fg_p[u], TRUE));
                } else if (ITEM_IS_ITEM1(fg_p[u])) {
                    mPB_keep_item(fg_p[u]);
                }
                fg_p[u] = EMPTY_NO;
                pc_dresser_town_clear(aMR_UT_X_NUM, u % aMR_UT_X_NUM, u / aMR_UT_X_NUM, u % aMR_UT_X_NUM + 1,
                                      u / aMR_UT_X_NUM + 1);
                if (layer == mCoBG_LAYER0) {
                    pc_oftr_unit_released(u); /* the town's own map: our mark off the ground */
                }
            }
        }
        printf("[oftr] %04X at %d,%d no longer fits there: to the lost and found\n", item, ut % aMR_UT_X_NUM,
               ut / aMR_UT_X_NUM);
        evicted++;
    }
    /* Anything on top of furniture (or held by a piece on top) with no furniture under it any more */
    {
        mActor_name_t* fg1 = pc_oftr_fg(mCoBG_LAYER1);
        mActor_name_t* fg2 = pc_oftr_fg(mCoBG_LAYER2);

        for (ut = 0; ut < PC_OFTR_UT_TOTAL_NUM; ut++) {
            int under1 = ITEM_IS_FTR(fg0[ut]) || fg0[ut] == RSV_FE1F;
            int under2 = fg1 != NULL && (ITEM_IS_FTR(fg1[ut]) || fg1[ut] == RSV_FE1F);

            if (fg2 != NULL && fg2[ut] != EMPTY_NO && !(under1 && under2)) {
                if (ITEM_IS_ITEM1(fg2[ut])) {
                    mPB_keep_item(fg2[ut]);
                }
                printf("[oftr] %04X floating at %d,%d (layer 2): to the lost and found\n", fg2[ut],
                       ut % aMR_UT_X_NUM, ut / aMR_UT_X_NUM);
                fg2[ut] = EMPTY_NO;
                evicted++;
            }
            if (fg1 != NULL && fg1[ut] != EMPTY_NO && fg1[ut] != RSV_FE1F && !under1) {
                if (ITEM_IS_FTR(fg1[ut])) {
                    mPB_keep_item(mRmTp_FtrItemNo2Item1ItemNo(fg1[ut], TRUE));
                } else if (ITEM_IS_ITEM1(fg1[ut])) {
                    mPB_keep_item(fg1[ut]);
                }
                printf("[oftr] %04X floating at %d,%d: to the lost and found\n", fg1[ut], ut % aMR_UT_X_NUM,
                       ut / aMR_UT_X_NUM);
                fg1[ut] = EMPTY_NO;
                evicted++;
            }
        }
    }
    if (evicted > 0) {
        extern void pc_quick_save_show_message(const char* msg);

        pc_quick_save_show_message(evicted == 1 ? "A piece of furniture outside was taken to the lost and found."
                                                : "Some furniture outside was taken to the lost and found.");
    }
}

static void aMR_pc_field_ct(void) {
    if (Common_Get(clip).shop_goods_clip == NULL) {
        aMR_pc_sg_clip.single_draw_proc = &aSG_pc_single_draw; /* shop goods' own drawing */
        aMR_pc_sg_clip.player_drop_entry_proc = &aMR_pc_sg_drop_entry;
        aMR_pc_sg_clip.single_get_angle_y_proc = &aMR_pc_sg_get_angle_y;
        aMR_pc_sg_clip.single_set_angle_y_proc = &aMR_pc_sg_set_angle_y;
        Common_Get(clip).shop_goods_clip = &aMR_pc_sg_clip;
        aMR_pc_sg_owned = TRUE;
    }
    aMR_pc_evict_misplaced();
    pc_oftr_mark_town();
    OSReport("[My_Room] outdoors: up to %d pieces loaded, %u bytes free\n", l_aMR_work.list_size,
             (unsigned)zelda_GetTotalFreeSize());
}

static int aMR_pc_loaded_unit(int ut) {
    int bx = (ut % aMR_UT_X_NUM) / UT_X_NUM;
    int bz = (ut / aMR_UT_X_NUM) / UT_Z_NUM;

    return bx >= aMR_pc_acre_x - 1 && bx <= aMR_pc_acre_x + 1 && bz >= aMR_pc_acre_z - 1 && bz <= aMR_pc_acre_z + 1;
}

/* A piece pushed or pulled toward an acre that holds all it can: say so, rather than only not moving */
static void aMR_pc_full_acre_notice(int from_ut, int to_ut) {
    extern void pc_quick_save_show_message(const char* msg);
    int from_acre = (from_ut % aMR_UT_X_NUM) / UT_X_NUM + ((from_ut / aMR_UT_X_NUM) / UT_Z_NUM) * BLOCK_X_NUM;
    int to_acre = (to_ut % aMR_UT_X_NUM) / UT_X_NUM + ((to_ut / aMR_UT_X_NUM) / UT_Z_NUM) * BLOCK_X_NUM;

    if (aMR_pc_field && from_acre != to_acre && pc_oftr_acre_full(to_ut)) {
        pc_quick_save_show_message("That acre can't hold any more furniture.");
    }
}

/* A piece leaves (its acre is no longer around the player); it stays on the grid */
/* Outdoors a stereo or music box is heard around it, not over the whole town: its song (quieter with distance, as
 * in a room) gives way to the hourly music past about 12 units and comes back on the way back */

static void aMR_pc_md_range(MY_ROOM_ACTOR* my_room, FTR_ACTOR* md_ftr_actor, GAME* game) {
    PLAYER_ACTOR* player = GET_PLAYER_ACTOR((GAME_PLAY*)game);
    f32 dist;
    int md_no = my_room->bgm_info.last_md_no;

    if (player == NULL || md_no == -1 || md_no == BGM_SPORTSFAIR_AEROBICS) {
        return;
    }
    dist = aMR_GetDistXZ(&player->actor_class.world.position, &md_ftr_actor->position);
    if (!aMR_pc_md_far && dist > aMR_PC_MD_FAR) {
        mBGMPsComp_delete_ps_room(md_no, 0);
        mBGMPsComp_MDPlayerPos_delete();
        aMR_pc_md_far = TRUE;
    } else if (aMR_pc_md_far && dist < aMR_PC_MD_NEAR) {
        mBGMPsComp_make_ps_room(md_no, 0);
        mBGMPsComp_MDPlayerPos_make();
        aMR_pc_md_far = FALSE;
    }
}

/* What a piece holds (a stereo's disc...) waits in the layers above it while it isn't loaded (aMR_KeepItem2Fg, as
 * a room keeps it); take it back as it loads, as aMR_MakeItemDataInFurniture does when a room is entered */
static void aMR_pc_take_items(FTR_ACTOR* ftr_actor) {
    int idx = 0;
    int ut_x;
    int ut_z;
    int i;

    if (!aMR_Wpos2PlaceNumber(&ut_x, &ut_z, ftr_actor->position, ftr_actor->shape_type)) {
        return;
    }
    for (i = ftr_actor->layer + 1; i < mCoBG_LAYER_NUM; i++, idx++) {
        mActor_name_t* fg_p = aMR_GetLayerTopFg(i);
        mActor_name_t* item_p;

        if (fg_p == NULL) {
            continue;
        }
        item_p = &fg_p[ut_x + ut_z * aMR_UT_X_NUM];
        if (*item_p != EMPTY_NO && *item_p != RSV_NO && *item_p != RSV_WALL_NO && !ITEM_IS_FTR(*item_p) &&
            *item_p != RSV_FE1F) {
            ftr_actor->items[idx] = *item_p;
            *item_p = EMPTY_NO;
        }
    }
}

/* Saving (pc_oftr_write_chunk): what the loaded pieces hold goes into the grid for the write (on TRUE) and back out
 * after (FALSE), so the save has it while the pieces keep it */
/* Furniture mid-move (pushed, pulled, turned, appearing) or an item flying onto it outdoors: what's on top and
 * the piece itself are in between places, so a save now would lose them (pc_quick_save.c waits) */
int aMR_pc_busy(void) {
    extern int aSG_pc_field_dropping(void);
    MY_ROOM_ACTOR* my_room;
    int i;

    if (aMR_CLIP == NULL || (my_room = (MY_ROOM_ACTOR*)aMR_CLIP->my_room_actor_p) == NULL) {
        return FALSE;
    }
    if (my_room->parent_ftr.ftrID != -1 || (aMR_pc_field && aSG_pc_field_dropping())) {
        return TRUE;
    }
    for (i = 0; i < ARRAY_COUNT(my_room->rsv_ftr); i++) {
        if (my_room->rsv_ftr[i].exist_flag) {
            return TRUE;
        }
    }
    return FALSE;
}

void aMR_pc_items_to_fg(int on) {
    int i;

    if (!aMR_pc_field || aMR_CLIP == NULL || l_aMR_work.ftr_actor_list == NULL || l_aMR_work.used_list == NULL) {
        return;
    }
    for (i = 0; i < l_aMR_work.list_size; i++) {
        FTR_ACTOR* ftr_actor = &l_aMR_work.ftr_actor_list[i];
        int ut_x;
        int ut_z;
        int layer;
        int idx = 0;

        if (!l_aMR_work.used_list[i] ||
            !aMR_Wpos2PlaceNumber(&ut_x, &ut_z, ftr_actor->position, ftr_actor->shape_type)) {
            continue;
        }
        for (layer = ftr_actor->layer + 1; layer < mCoBG_LAYER_NUM; layer++, idx++) {
            mActor_name_t* fg_p = aMR_GetLayerTopFg(layer);

            if (fg_p != NULL && ftr_actor->items[idx] != EMPTY_NO) {
                fg_p[ut_x + ut_z * aMR_UT_X_NUM] = on ? ftr_actor->items[idx] : EMPTY_NO;
            }
        }
    }
}

static void aMR_pc_unload(ACTOR* actorx, FTR_ACTOR* ftr_actor) {
    aFTR_PROFILE* profile = aMR_GetFurnitureProfile(ftr_actor->name);
    int ut_x;
    int ut_z;

    mCoBG_CrossOffMoveBg(ftr_actor->move_bg_idx);
    if (aMR_Wpos2PlaceNumber(&ut_x, &ut_z, ftr_actor->position, ftr_actor->shape_type)) {
        aMR_SetInfoFurnitureTable(ftr_actor->shape_type, ut_x + ut_z * aMR_UT_X_NUM, aMR_NO_FTR_ID, ftr_actor->layer);
    }
    aMR_KeepItem2Fg(ftr_actor);
    aMR_MiniDiskCommonDt(ftr_actor, actorx);
    aMR_RadioCommonDt(ftr_actor, actorx);
    aMR_MinusWeight(actorx, ftr_actor);
    if (profile != NULL && profile->vtable != NULL && profile->vtable->dt_proc != NULL) {
        profile->vtable->dt_proc(ftr_actor, aMR_FtrNo2BankAddress(ftr_actor->name));
    }
    l_aMR_work.used_list[ftr_actor->id] = FALSE;
    aMR_DeleteFurnitureBank(ftr_actor->name);
    aMR_ClearBitSwitch(ftr_actor);
}

/* Field mode, every frame: the furniture of the player's acre and the eight around it is loaded, the rest not.
 * A piece still moving (pushed, rotated...) leaves a frame or more later. */
static void aMR_pc_field_stream(ACTOR* actorx, GAME* game) {
    static int pending = FALSE;
    PLAYER_ACTOR* player = GET_PLAYER_ACTOR((GAME_PLAY*)game);
    int bx;
    int bz;
    int i;
    int layer;
    int ut_x;
    int ut_z;

    if (player == NULL || l_aMR_work.ftr_actor_list == NULL || l_aMR_work.used_list == NULL) {
        return;
    }
    bx = (int)(player->actor_class.world.position.x / mFI_BK_WORLDSIZE_X_F);
    bz = (int)(player->actor_class.world.position.z / mFI_BK_WORLDSIZE_Z_F);
    if (bx != aMR_pc_acre_x || bz != aMR_pc_acre_z) {
        if (aMR_pc_acre_x >= 0) {
            aMR_pc_dresser_flush(); /* the storage IDs of what is about to leave */
        }
        aMR_pc_acre_x = bx;
        aMR_pc_acre_z = bz;
        pending = TRUE;
    }
    if (!pending) {
        return;
    }
    pending = FALSE;

    for (i = 0; i < l_aMR_work.list_size; i++) {
        FTR_ACTOR* ftr_actor = &l_aMR_work.ftr_actor_list[i];

        if (l_aMR_work.used_list[i] && aMR_Wpos2PlaceNumber(&ut_x, &ut_z, ftr_actor->position, ftr_actor->shape_type) &&
            !aMR_pc_loaded_unit(ut_x + ut_z * aMR_UT_X_NUM)) {
            if (ftr_actor->state == aFTR_STATE_STOP) {
                aMR_pc_unload(actorx, ftr_actor);
            } else {
                pending = TRUE;
            }
        }
    }

    for (layer = mCoBG_LAYER0; layer <= mCoBG_LAYER1; layer++) {
        mActor_name_t* fg_p = aMR_GetLayerTopFg(layer);
        aMR_place_t* place_p = aMR_GetLayerPlaceTable(layer);

        for (ut_z = (bz - 1) * UT_Z_NUM; ut_z < (bz + 2) * UT_Z_NUM; ut_z++) {
            for (ut_x = (bx - 1) * UT_X_NUM; ut_x < (bx + 2) * UT_X_NUM; ut_x++) {
                int ut = ut_x + ut_z * aMR_UT_X_NUM;

                if (ut_x < 0 || ut_z < 0 || ut_x >= aMR_UT_X_NUM || ut_z >= aMR_UT_Z_NUM || !ITEM_IS_FTR(fg_p[ut]) ||
                    place_p[ut] != aMR_NO_FTR_ID) {
                    continue;
                }
                aMR_MakeOneFurniture(actorx, fg_p[ut], game, ut_x, ut_z, layer);
                if (place_p[ut] < l_aMR_work.list_size) {
                    FTR_ACTOR* ftr_actor = &l_aMR_work.ftr_actor_list[place_p[ut]];

                    aMR_SetFurniture2FG(ftr_actor, ftr_actor->position, TRUE); /* its other units too */
                    if (aFTR_IS_STORAGE(aMR_GetFurnitureProfile(ftr_actor->name))) {
                        ftr_actor->pc_dresser_id = pc_dresser_bound(PC_DRESSER_TOWN, 0, layer, ut);
                        aMR_pc_take_items(ftr_actor); /* before layer 1 is looked at: a stereo's disc is there */
                    }
                }
            }
        }
    }
}
#endif

#ifdef TARGET_PC
/* Dev (PC_CTL oftr): furniture item ftr outdoors with its first unit on (ut_x, ut_z), every unit it takes checked */
int aMR_pc_dev_put(int ut_x, int ut_z, mActor_name_t ftr) {
    static u8 rotate_angle_types[] = {
        aFTR_SHAPE_TYPEB_0,
        aFTR_SHAPE_TYPEB_90,
        aFTR_SHAPE_TYPEB_180,
        aFTR_SHAPE_TYPEB_270,
    };
    aFTR_PROFILE* profile;
    int ut_info[4];
    int count;
    u8 shape;

    if (!aMR_pc_field || !ITEM_IS_FTR(ftr) || ut_x < 0 || ut_z < 0 ||
        (profile = aMR_GetFurnitureProfile(mRmTp_FtrItemNo2FtrIdx(ftr))) == NULL) {
        return FALSE;
    }
    shape = profile->shape;
    if (shape == aFTR_SHAPE_TYPEB_0) {
        shape = rotate_angle_types[ftr & 3];
    }
    count = aMR_GetFurniturePoccessUnitNo(ut_info, ut_x, ut_z, shape);
    return pc_oftr_put(ut_info, count, ftr);
}

/* Dev (PC_CTL oftrfill N): the player's acre and the eight around it get up to N one-unit pieces each, of as many
 * kinds as there are, then load again. For stress tests; it writes to the town save like any placing. */
void aMR_pc_dev_fill(int per_acre) {
    int kind = 0;
    int bx;
    int bz;
    int placed = 0;

    if (!aMR_pc_field || aMR_pc_acre_x < 0) {
        return;
    }
    for (bz = aMR_pc_acre_z - 1; bz <= aMR_pc_acre_z + 1; bz++) {
        for (bx = aMR_pc_acre_x - 1; bx <= aMR_pc_acre_x + 1; bx++) {
            int n = 0;
            int u;

            for (u = 0; u < UT_TOTAL_NUM && n < per_acre; u += 2) {
                int ut_x = bx * UT_X_NUM + (u % UT_X_NUM) + ((u / UT_X_NUM) & 1);
                int ut_z = bz * UT_Z_NUM + u / UT_X_NUM;
                aFTR_PROFILE* profile;
                int tries = 0;

                do {
                    kind = (kind + 37) % FTR_NUM;
                    profile = aMR_GetFurnitureProfile(kind);
                } while ((profile == NULL || profile->shape != aFTR_SHAPE_TYPEA) && ++tries < FTR_NUM);
                if (pc_oftr_acre_full(ut_x + ut_z * aMR_UT_X_NUM)) {
                    break;
                }
                if (aMR_pc_dev_put(ut_x, ut_z, mRmTp_FtrIdx2FtrItemNo(kind, mRmTp_DIRECT_SOUTH))) {
                    n++;
                }
            }
            placed += n;
        }
    }
    printf("[oftr] filled %d pieces around acre %d,%d\n", placed, aMR_pc_acre_x, aMR_pc_acre_z);
    aMR_pc_acre_x = -1; /* load again */
}
#endif
