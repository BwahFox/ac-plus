static void aMFU_ct(FTR_ACTOR* ftr_actor, u8* data) {
    // nothing
}

extern Gfx obj_shop_umbmy_model[];

static void aMFU_dw(FTR_ACTOR* ftr_actor, ACTOR* my_room_actor, GAME* game, u8* data) {
    OPEN_DISP(game->graph);

    gSPSegment(NEXT_POLY_OPA_DISP, G_MWO_SEGMENT_9, data);                       // seg 9 has tex
    gSPSegment(NEXT_POLY_OPA_DISP, G_MWO_SEGMENT_8, data + mNW_DESIGN_TEX_SIZE); // seg 8 has pal
    gSPDisplayList(NEXT_POLY_OPA_DISP, obj_shop_umbmy_model);

    CLOSE_DISP(game->graph);
}

static void aMFU_mv(FTR_ACTOR* ftr_actor, ACTOR* my_room_actor, GAME* game, u8* data) {
    // nothing
}

static void aMFU_dt(FTR_ACTOR* ftr_actor, u8* data) {
    // nothing
}

static void aMFU_dma(mActor_name_t ftr_name, u8* data) {
    int house_no = (mFI_GetFieldId() - mFI_FIELD_PLAYER0_ROOM) & 3;
    int player_no = mHS_get_pl_no(house_no);
    u32 manekin_no = ((ftr_name - FTR_MYUMBRELLA_START) >> 2);

    mNW_original_design_c* design = &Save_Get(private_data[player_no]).my_org[manekin_no & 7];
#ifdef TARGET_PC
    const mNW_original_design_c* kept = pc_pattern_use(PC_PAT_USE_UMBRELLA_STAND, house_no, -1, manekin_no & 7);
    if (kept != NULL) {
        design = (mNW_original_design_c*)kept; /* the pattern it was made from */
    }
#endif

    mNW_CopyOriginalTexture(data, design);
    mNW_CopyOriginalPalette(data + mNW_DESIGN_TEX_SIZE, design);
}

static aFTR_vtable_c aMFU_func = {
    &aMFU_ct, &aMFU_mv, &aMFU_dw, &aMFU_dt, &aMFU_dma,
};

aFTR_PROFILE iam_myfumbrella = {
    // clang-format off
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    30.0f,
    0.01f,
    aFTR_SHAPE_TYPEA,
    mCoBG_FTR_TYPEA,
    0,
    0,
    0,
    aFTR_INTERACTION_UMBRELLA,
    &aMFU_func,
    // clang-format on
};
