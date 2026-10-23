/* ac-plus: the museum's two City Folk rooms, upstairs (the observatory) and downstairs
 * (the café), reached by the stairs in the entrance hall's side walls. Their layout and
 * look come from the player's City Folk disc (pc/src/pc_museum_rooms.c). PC only. */
#ifdef TARGET_PC
#include "m_scene.h"

static Door_data_c MUSEUM_OBSERVATORY_door_data[] = {
    {
        SCENE_MUSEUM_ENTRANCE, // back down the stairs on the right
        mSc_DIRECT_WEST,       // exit orientation
        0,                     // exit type
        0,                     // arg
        { 340, 0, 400 },       // exit position
        0x4000,                // exit door item name
        2,                     // wipe type
        { 0, 0, 0 },           // padding
    },
};

static Door_data_c MUSEUM_CAFE_door_data[] = {
    {
        SCENE_MUSEUM_ENTRANCE, // back up the stairs on the left
        mSc_DIRECT_EAST,       // exit orientation
        0,                     // exit type
        0,                     // arg
        { 140, 0, 400 },       // exit position
        0x4000,                // exit door item name
        2,                     // wipe type
        { 0, 0, 0 },           // padding
    },
};

static Actor_data MUSEUM_PC_ROOM_player_data[] = {
    {
        mAc_PROFILE_PLAYER, // profile
        { 260, 0, 440 },    // position (the door's exit position wins)
        { 0, 0, 0 },        // rotation
        0                   // ct arg
    },
};

static s16 MUSEUM_PC_ROOM_ctrl_actor_data[] = {
    mAc_PROFILE_BIRTH_CONTROL,  // 0
    mAc_PROFILE_NPC2,           // 1
    mAc_PROFILE_INSECT,         // 2
    mAc_PROFILE_TOOLS,          // 3
    mAc_PROFILE_HANDOVERITEM,   // 4
    mAc_PROFILE_EFFECT_CONTROL, // 5
    mAc_PROFILE_SHOP_LEVEL,     // 6
    mAc_PROFILE_QUEST_MANAGER,  // 7
    mAc_PROFILE_EVENT_MANAGER,  // 8
    mAc_PROFILE_WEATHER,        // 9
    mAc_PROFILE_MY_ROOM,        // 10
};

static s16 MUSEUM_PC_ROOM_object_bank[] = {
    ACTOR_OBJ_BANK_EF_MUSEUM, // 0
};

extern Scene_Word_u museum_observatory_info[] = {
    mSc_DATA_SOUND(0, 1),
    mSc_DATA_DOOR_DATA(1, MUSEUM_OBSERVATORY_door_data),
    mSc_DATA_PLAYER(MUSEUM_PC_ROOM_player_data),
    mSc_DATA_ARRANGE_FTR(1),
    mSc_DATA_CTRL_ACTORS(11, MUSEUM_PC_ROOM_ctrl_actor_data),
    mSc_DATA_OBJ_BANK(1, MUSEUM_PC_ROOM_object_bank),
    mSc_DATA_FIELDCT(mSc_ITEM_TYPE_DUMMY, 1, 0xA000, mSc_ROOM_TYPE_MY_ROOM, FIELD_DRAW_TYPE_INDOORS),
    mSc_DATA_END(),
};

extern Scene_Word_u museum_cafe_info[] = {
    mSc_DATA_SOUND(0, 1),
    mSc_DATA_DOOR_DATA(1, MUSEUM_CAFE_door_data),
    mSc_DATA_PLAYER(MUSEUM_PC_ROOM_player_data),
    mSc_DATA_ARRANGE_FTR(1),
    mSc_DATA_CTRL_ACTORS(11, MUSEUM_PC_ROOM_ctrl_actor_data),
    mSc_DATA_OBJ_BANK(1, MUSEUM_PC_ROOM_object_bank),
    mSc_DATA_FIELDCT(mSc_ITEM_TYPE_DUMMY, 1, 0xA000, mSc_ROOM_TYPE_MY_ROOM, FIELD_DRAW_TYPE_INDOORS),
    mSc_DATA_END(),
};
#endif
