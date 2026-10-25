/* pc_nlrooms.c - New Leaf style extra house rooms. See pc_nlrooms.h. */
#include "pc_nlrooms.h"

#include "m_common_data.h"
#include "m_field_info.h"
#include "m_home.h"
#include "m_name_table.h"
#include "m_private.h"
#include "m_scene.h"
#include "m_scene_table.h"
#include "m_lib.h"
#include "graph.h"
#include "sys_matrix.h"

#include <dolphin/os.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PC_NLR_VERSION 2
#define PC_NLR_PRICE_NEW 398000 /* a new room, as the big main room cost */
#define PC_NLR_PRICE_GROW 798000 /* a room grown to 8x8, as the upstairs did */
#define PC_NLR_UNIT 40.0f
#define PC_NLR_MAIN_DOORS 2 /* the big main room's own: the step down and the stairs up */

/* m_home.c: an empty room with the walls and door of a room of that size */
extern void mHm_PC_InitNlRoom(mHm_flr_c* room, int size, const mHm_flr_c* main_floor);
extern void mHm_PC_GrowNlRoom(mHm_flr_c* room, int from, int to);

/* Nook's work on a house: ordered (built on the first load on a later day), then built (he bills it next visit) */
typedef struct {
    s8 room;  /* -1 none */
    u8 size;  /* what it'll be */
    u8 built; /* done, not yet announced and billed */
    u8 pad;
    lbRTC_ymd_c date; /* ordered on */
} pc_nlr_order_c;

typedef struct {
    u8 size[PC_NLR_ROOMS]; /* 0 none yet, 6 or 8 */
    pc_nlr_order_c order;
    mHm_flr_c room[PC_NLR_ROOMS];
} pc_nlr_house_c;

/* The chunk: { u32 version; s32 room the player is in; u32 houses } then for each house
 * { u8 house; u8 size[3]; PersonalID_c owner; Nook's order; the rooms that exist, left, right, back } */
typedef struct {
    u8 house;
    u8 size[PC_NLR_ROOMS];
    PersonalID_c owner;
    pc_nlr_order_c order;
} pc_nlr_chunk_house_c;

static pc_nlr_house_c l_nlr[PLAYER_NUM];
static int l_cur = -1;     /* the extra room the player is in */
static int l_pending = -1; /* the room the door being taken leads to, -2 when none was taken */
static int l_granted;

/* This scene's door table: the scene's own doors, then the ones to and from the extra rooms */
static Door_data_c l_doors[PC_NLR_MAIN_DOORS + PC_NLR_ROOMS];
static int l_door_target[PC_NLR_MAIN_DOORS + PC_NLR_ROOMS]; /* -1: the main room; -2: not ours */
static int l_door_first;                                 /* the first of ours */
static int l_door_num;
static int l_door_of_room[PC_NLR_ROOMS];                 /* in the main room: each room's door, -1 none */

static int pc_nlr_house(void) {
    int house = Common_Get(house_owner_name);

    return house >= 0 && house < PLAYER_NUM ? house : -1;
}

/* Big main room, upstairs and basement: the rooms can only hang off that */
static int pc_nlr_house_ready(int house) {
    return house >= 0 && house < PLAYER_NUM && Save_Get(homes[house]).size_info.size >= mHm_HOMESIZE_UPPER &&
           Save_Get(homes[house]).flags.has_basement;
}

mHm_flr_c* mHm_GetFloorP(int house, int floor_no) {
    house &= 3;
    if (floor_no >= PC_NLR_FLOOR_FIRST && floor_no < PC_NLR_FLOOR_FIRST + PC_NLR_ROOMS) {
        return &l_nlr[house].room[floor_no - PC_NLR_FLOOR_FIRST];
    }
    if (floor_no < 0 || floor_no >= mHm_ROOM_NUM) {
        floor_no = mHm_ROOM_MAIN;
    }
    return &Save_Get(homes[house]).floors[floor_no];
}

static int pc_nlr_scene_of(int size) {
    return size == 8 ? SCENE_MY_ROOM_L : SCENE_MY_ROOM_M;
}

int pc_nlr_floor_for_scene(int scene) {
    int house = pc_nlr_house();

    if (l_cur < 0 || house < 0 || l_nlr[house].size[l_cur] == 0 ||
        scene != pc_nlr_scene_of(l_nlr[house].size[l_cur])) {
        return -1;
    }
    return PC_NLR_FLOOR_FIRST + l_cur;
}

int pc_nlr_current(int* size) {
    int house = pc_nlr_house();

    if (pc_nlr_floor_for_scene(Save_Get(scene_no)) < 0) {
        return -1;
    }
    if (size != NULL) {
        *size = l_nlr[house].size[l_cur];
    }
    return l_cur;
}

int pc_nlr_room_size(int house, int room) {
    if (house < 0 || house >= PLAYER_NUM || room < 0 || room >= PC_NLR_ROOMS) {
        return 0;
    }
    return l_nlr[house].size[room];
}

int pc_nlr_unbilled(int house) {
    return house >= 0 && house < PLAYER_NUM && l_nlr[house].order.room >= 0 && l_nlr[house].order.built;
}

/* Keep furniture off the floor in front of a door, as the game does at its stairs. What's there already stays, for
 * the player to move (as in the later games); the floor's claimed again once it's clear (pc_nlr_door_data) */
static void pc_nlr_reserve(mHm_flr_c* floor, int ux, int uz) {
    mActor_name_t* item = &floor->layer_main.items[uz][ux];

    if (*item == EMPTY_NO) {
        *item = RSV_DOOR;
    }
}

/* The floor in front of the doors to and from `room` (n: its size) */
static void pc_nlr_reserve_doors(int house, int room, int n) {
    mHm_flr_c* main_floor = &Save_Get(homes[house]).floors[mHm_ROOM_MAIN];
    mHm_flr_c* extra = &l_nlr[house].room[room];

    if (room == PC_NLR_LEFT) {
        pc_nlr_reserve(main_floor, 1, 7);
        pc_nlr_reserve(main_floor, 1, 8);
        pc_nlr_reserve(extra, n, n - 1);
        pc_nlr_reserve(extra, n, n);
    } else if (room == PC_NLR_RIGHT) {
        pc_nlr_reserve(main_floor, 8, 7);
        pc_nlr_reserve(main_floor, 8, 8);
        pc_nlr_reserve(extra, 1, n - 1);
        pc_nlr_reserve(extra, 1, n);
    } else {
        pc_nlr_reserve(main_floor, 4, 1);
        pc_nlr_reserve(main_floor, 5, 1);
    }
}

void pc_nlr_set_room_size(int house, int room, int size) {
    pc_nlr_house_c* h;

    if (house < 0 || house >= PLAYER_NUM || room < 0 || room >= PC_NLR_ROOMS || (size != 6 && size != 8)) {
        return;
    }
    h = &l_nlr[house];
    if (h->size[room] == 0) {
        mHm_PC_InitNlRoom(&h->room[room], size, &Save_Get(homes[house]).floors[mHm_ROOM_MAIN]);
    } else if (h->size[room] != size) {
        int old = h->size[room], z;

        for (z = old - 1; z <= old && room != PC_NLR_BACK; z++) {
            mActor_name_t* was = &h->room[room].layer_main.items[z][room == PC_NLR_LEFT ? old : 1];

            if (*was == RSV_DOOR) {
                *was = EMPTY_NO; /* the door moves with the wall */
            }
        }
        mHm_PC_GrowNlRoom(&h->room[room], old, size);
    }
    h->size[room] = (u8)size;
    pc_nlr_reserve_doors(house, room, size);
    OSReport("[NLRooms] house %d: room %d is %dx%d\n", house, room, size, size);
}

/* dev: PC_NLR_GRANT=a,b,c */
static void pc_nlr_grant(void) {
    const char* env = getenv("PC_NLR_GRANT");
    int size[PC_NLR_ROOMS] = { 0 };
    int house, r;

    if (l_granted || env == NULL || !mSc_IS_SCENE_PLAYER_ROOM(Save_Get(scene_no))) {
        return; /* in a house, so the town is loaded */
    }
    l_granted = TRUE;
    sscanf(env, "%d,%d,%d", &size[0], &size[1], &size[2]);
    for (house = 0; house < PLAYER_NUM; house++) {
        if (!pc_nlr_house_ready(house)) {
            continue;
        }
        for (r = 0; r < PC_NLR_ROOMS; r++) {
            if (size[r] == 6 || size[r] == 8) {
                pc_nlr_set_room_size(house, r, size[r]);
            }
        }
    }
}

void pc_nlr_play_init(void) {
    pc_nlr_grant();
    if (l_pending != -2) {
        l_cur = l_pending;
        l_pending = -2;
    }
    if (l_cur >= 0 && pc_nlr_floor_for_scene(Save_Get(scene_no)) < 0) {
        l_cur = -1; /* somewhere else */
    }
    if (l_cur >= 0) {
        OSReport("[NLRooms] in room %d\n", l_cur);
    }
}

void pc_nlr_scene_left(Door_data_c* door) {
    int i;

    for (i = 0; i < l_door_num; i++) {
        if (door == &l_doors[i]) {
            l_pending = l_door_target[i] >= -1 ? l_door_target[i] : -1;
            return;
        }
    }
    l_pending = -1; /* any other door: the house's own floors */
}

/* Unit (x, z) to world, and a door entry from the first of the scene's own */
static void pc_nlr_add_door(const Door_data_c* base, int target, int scene, int ux, int uz, int facing) {
    Door_data_c* d = &l_doors[l_door_num];

    *d = *base;
    d->next_scene_id = scene;
    d->exit_orientation = (u8)facing;
    d->exit_position.x = (s16)(ux * PC_NLR_UNIT + PC_NLR_UNIT / 2);
    d->exit_position.y = 0;
    d->exit_position.z = (s16)(uz * PC_NLR_UNIT + PC_NLR_UNIT / 2);
    l_door_target[l_door_num++] = target;
}

/* Exit orientations (m_scene.c angle_table) */
#define PC_NLR_FACE_S 0
#define PC_NLR_FACE_E 2
#define PC_NLR_FACE_N 4
#define PC_NLR_FACE_W 6

Door_data_c* pc_nlr_door_data(int scene, Door_data_c* doors, int* num_doors) {
    int house = pc_nlr_house();
    int r, n;

    l_door_num = 0;
    for (r = 0; r < PC_NLR_ROOMS; r++) {
        l_door_of_room[r] = -1;
    }
    if (getenv("PC_NLR_LOG") != NULL) {
        printf("[NLRooms] doors for scene %d: house %d ready %d, %d doors, room %d\n", scene, house,
               pc_nlr_house_ready(house), *num_doors, l_cur);
        fflush(stdout);
    }
    if (!pc_nlr_house_ready(house) || doors == NULL || *num_doors < 1 || *num_doors > PC_NLR_MAIN_DOORS) {
        return doors;
    }
    for (r = 0; r < PC_NLR_ROOMS; r++) {
        if (l_nlr[house].size[r] != 0) {
            pc_nlr_reserve_doors(house, r, l_nlr[house].size[r]);
        }
    }
    memcpy(l_doors, doors, sizeof(Door_data_c) * *num_doors);
    for (r = 0; r < *num_doors; r++) {
        l_door_target[r] = -2;
    }
    l_door_num = l_door_first = *num_doors;

    if (l_cur < 0 && scene == SCENE_MY_ROOM_LL1) {
        /* the main room: into each room that's there, arriving at its door */
        for (r = 0; r < PC_NLR_ROOMS; r++) {
            n = l_nlr[house].size[r];
            if (n == 0) {
                continue;
            }
            l_door_of_room[r] = l_door_num;
            if (r == PC_NLR_LEFT) {
                pc_nlr_add_door(&doors[0], r, pc_nlr_scene_of(n), n, n, PC_NLR_FACE_W);
            } else if (r == PC_NLR_RIGHT) {
                pc_nlr_add_door(&doors[0], r, pc_nlr_scene_of(n), 1, n, PC_NLR_FACE_E);
            } else {
                /* in through its front door, as into a house */
                pc_nlr_add_door(&doors[0], r, pc_nlr_scene_of(n), n / 2, n + 1, PC_NLR_FACE_N);
                l_doors[l_door_num - 1].exit_position.x = (s16)(n / 2 * PC_NLR_UNIT + PC_NLR_UNIT);
            }
        }
    } else if (l_cur >= 0 && scene == pc_nlr_scene_of(l_nlr[house].size[l_cur])) {
        /* an extra room: back to the main room, by its door there */
        if (l_cur == PC_NLR_LEFT) {
            pc_nlr_add_door(&doors[0], -1, SCENE_MY_ROOM_LL1, 1, 8, PC_NLR_FACE_E);
        } else if (l_cur == PC_NLR_RIGHT) {
            pc_nlr_add_door(&doors[0], -1, SCENE_MY_ROOM_LL1, 8, 8, PC_NLR_FACE_W);
        } else {
            pc_nlr_add_door(&doors[0], -1, SCENE_MY_ROOM_LL1, 4, 1, PC_NLR_FACE_S);
            l_doors[l_door_num - 1].exit_position.x = (s16)(5 * PC_NLR_UNIT);
        }
    } else {
        l_door_num = 0;
        return doors;
    }
    *num_doors = l_door_num;
    return l_doors;
}

int pc_nlr_door_in_front(const xyz_t* probe, int item) {
    int house = pc_nlr_house();
    int ux, uz, n;

    if (l_door_num == 0 || house < 0 || probe->x < 0.0f || probe->z < 0.0f) {
        return item;
    }
    ux = (int)(probe->x / PC_NLR_UNIT);
    uz = (int)(probe->z / PC_NLR_UNIT);
    if (getenv("PC_NLR_LOG") != NULL) { /* dev: what the player is facing, in units */
        static int last = -1;

        if (last != ux * 100 + uz) {
            last = ux * 100 + uz;
            printf("[NLRooms] facing unit %d,%d item %04X room %d doors %d\n", ux, uz, item, l_cur, l_door_num);
            fflush(stdout);
        }
    }

    if (l_cur < 0) {
        /* the main room's walls: left and right near the front, the back in the middle */
        if (l_door_of_room[PC_NLR_LEFT] >= 0 && ux == 0 && (uz == 7 || uz == 8)) {
            return DOOR_START + l_door_of_room[PC_NLR_LEFT];
        }
        if (l_door_of_room[PC_NLR_RIGHT] >= 0 && ux == 9 && (uz == 7 || uz == 8)) {
            return DOOR_START + l_door_of_room[PC_NLR_RIGHT];
        }
        if (l_door_of_room[PC_NLR_BACK] >= 0 && uz == 0 && (ux == 4 || ux == 5)) {
            return DOOR_START + l_door_of_room[PC_NLR_BACK];
        }
        return item;
    }

    n = l_nlr[house].size[l_cur];
    if (item == EXIT_DOOR || item == EXIT_DOOR + 1) {
        /* the front door: the back room's way out, a sealed one for the side rooms */
        return l_cur == PC_NLR_BACK ? DOOR_START + l_door_first : RSV_WALL_NO;
    }
    if (((l_cur == PC_NLR_LEFT && ux == n + 1) || (l_cur == PC_NLR_RIGHT && ux == 0)) && (uz == n - 1 || uz == n)) {
        return DOOR_START + l_door_first;
    }
    return item;
}

int pc_nlr_is_door(int item) {
    int idx = item - DOOR_START;

    return l_door_num > 0 && idx >= l_door_first && idx < l_door_num;
}

/* --- Nook builds them --- */

static const char* l_room_side[PC_NLR_ROOMS] = { "left", "right", "back" };

/* The next room Nook can build or grow for a house, in order: new rooms first, then each grown to 8x8 */
static int pc_nlr_next(int house, int* size) {
    int r;

    for (r = 0; r < PC_NLR_ROOMS; r++) {
        if (l_nlr[house].size[r] == 0) {
            *size = 6;
            return r;
        }
    }
    for (r = 0; r < PC_NLR_ROOMS; r++) {
        if (l_nlr[house].size[r] == 6) {
            *size = 8;
            return r;
        }
    }
    return -1;
}

/* Nook's line being shown, in place of message l_line_msg: -1 none */
static int l_line = -1;
static int l_line_msg;
static int l_line_room;

enum {
    PC_NLR_LINE_NEW,      /* offering a new room */
    PC_NLR_LINE_GROW,     /* offering to grow one */
    PC_NLR_LINE_NEW_DONE, /* built: the bill */
    PC_NLR_LINE_GROW_DONE,
};

int pc_nlr_nook_greeting(int house, int msg_no) {
    pc_nlr_house_c* h;
    Private_c* priv = Now_Private;
    mHm_hs_c* home;
    int size, room;

    if (house < 0 || house >= PLAYER_NUM || priv == NULL) {
        return FALSE;
    }
    h = &l_nlr[house];
    home = Save_GetPointer(homes[house]);
    if (getenv("PC_NLR_LOG") != NULL) {
        printf("[NLRooms] Nook greets house %d: order %d built %d loan %u size %d next %d statue %d basement %d\n", house,
               h->order.room, h->order.built, priv->inventory.loan, home->size_info.size, home->size_info.next_size,
               home->size_info.statue_ordered, home->flags.has_basement);
        fflush(stdout);
    }
    if (h->order.room >= 0 && h->order.built) {
        /* built since: the bill, as for the GameCube's upgrades */
        l_line_room = h->order.room;
        l_line = h->order.size == 8 ? PC_NLR_LINE_GROW_DONE : PC_NLR_LINE_NEW_DONE;
        priv->inventory.loan = h->order.size == 8 ? PC_NLR_PRICE_GROW : PC_NLR_PRICE_NEW;
        h->order.room = -1;
        h->order.built = FALSE;
    } else if (h->order.room < 0 && priv->inventory.loan == 0 && pc_nlr_house_ready(house) &&
               !home->size_info.statue_ordered && (room = pc_nlr_next(house, &size)) >= 0) {
        /* paid off and no statue being made: the next room. They come before the statue (the user's call), which
         * he offers once they're all 8x8, as he would have; a house with its statue already up gets them too */
        const lbRTC_time_c* now = &Common_Get(time.rtc_time);

        h->order.room = (s8)room;
        h->order.size = (u8)size;
        h->order.built = FALSE;
        h->order.date.year = now->year;
        h->order.date.month = now->month;
        h->order.date.day = now->day;
        l_line_room = room;
        l_line = size == 8 ? PC_NLR_LINE_GROW : PC_NLR_LINE_NEW;
    } else {
        return FALSE;
    }
    l_line_msg = msg_no;
    OSReport("[NLRooms] Nook: line %d, room %d, loan %u\n", l_line, l_line_room, priv->inventory.loan);
    return TRUE;
}

void pc_nlr_check_orders(void) {
    const lbRTC_time_c* now = &Common_Get(time.rtc_time);
    int house;

    for (house = 0; house < PLAYER_NUM; house++) {
        pc_nlr_order_c* o = &l_nlr[house].order;

        if (o->room < 0 || o->built || !pc_nlr_house_ready(house)) {
            continue;
        }
        /* as mHm_CheckRehouseOrder: done on the first load on another day */
        if (o->date.day != now->day || o->date.month != now->month || o->date.year != now->year) {
            /* a new room's wing outside needs its ground: a tree or a rock there holds it up a day */
            if (l_nlr[house].size[o->room] == 0) {
                if (!pc_nlr_wing_ground(house, o->room, FALSE)) {
                    continue;
                }
                pc_nlr_wing_ground(house, o->room, TRUE);
            }
            pc_nlr_set_room_size(house, o->room, o->size);
            o->built = TRUE;
        }
    }
}

/* Nook's lines. \n a new line, \f the next page (after A), @ the player's name; ends the talk. */
static const char* l_lines[] = {
    /* PC_NLR_LINE_NEW */
    "Oh! @! Welcome, welcome!\f"
    "That house of yours has\ncome such a long way, hm?\nBut I can't help thinking\nit could use more room...\f"
    "So here's my idea: a whole\nnew room, right off the %s\nside of your main room!\nSpacious, cozy, splendid!\f"
    "My crew will start at once.\nIt'll be ready tomorrow,\nyes? Oh ho ho! You can\nthank me later!",
    /* PC_NLR_LINE_GROW */
    "Ah, @! Do come in!\f"
    "I was thinking about that\n%s room of yours. Lovely,\nbut a bit snug, wouldn't\nyou say?\f"
    "I'll have it made as big as\nyour main room! It'll be\nready tomorrow, hm?\nOh ho ho!",
    /* PC_NLR_LINE_NEW_DONE */
    "Welcome, @!\f"
    "Have you seen your new room\nyet? Right off the %s side\nof your main room! Grand,\nisn't it?\f"
    "Now then, the small matter\nof the cost... That'll be\n398,000 Bells!\f"
    "No need to pay it now, of\ncourse. Just pay it off\nthrough the post office,\nlike before, yes?",
    /* PC_NLR_LINE_GROW_DONE */
    "Welcome, @!\f"
    "Your %s room is all\ngrown up now! Plenty of\nspace for all sorts of\nfurniture, hm?\f"
    "And that comes to...\n798,000 Bells!\f"
    "Pay it off through the post\noffice whenever you can.\nOh ho ho! Do come again!",
};

int pc_nlr_msg(int index, u8* buf, int buf_size, int* len) {
    char text[512];
    const char* t;
    int i = 0;

    if (l_line < 0) {
        return FALSE;
    }
    if (index != l_line_msg) {
        l_line = -1; /* the talk has moved on: that message is the game's own again */
        return FALSE;
    }
    snprintf(text, sizeof(text), l_lines[l_line], l_room_side[l_line_room]);
    for (t = text; *t != '\0' && i < buf_size - 8; t++) {
        if (*t == '\n') {
            buf[i++] = 0xCD;
        } else if (*t == '\f') {
            buf[i++] = 0x7F, buf[i++] = 0x04; /* wait for A */
            buf[i++] = 0xCD;
            buf[i++] = 0x7F, buf[i++] = 0x02; /* clear */
        } else if (*t == '@') {
            buf[i++] = 0x7F, buf[i++] = 0x1A; /* the player's name */
        } else {
            buf[i++] = (u8)*t;
        }
    }
    buf[i++] = 0xCD;
    buf[i++] = 0x7F, buf[i++] = 0x00; /* LAST: the end of the talk */
    *len = i;
    return TRUE;
}

/* --- what's seen of the doors: openings cut into the wall, with a dark passage behind ---
 *
 * The walls' display lists are copied with the opening cut out of them: each wall triangle that overlaps it is
 * replaced by the pieces of it outside the opening, which keep its texture coordinates and its hand-painted shading,
 * so the wallpaper runs on around the opening as before. Behind the opening a short passage fades into the dark,
 * as the front door's alcove does. The wall models are in model units, 16 to the world's. */

#define PC_NLR_M 16.0f                      /* model units to the world's */
#define PC_NLR_FLOOR_Y (40.0f * PC_NLR_M)   /* the room's floor */
#define PC_NLR_OPEN_HIGH (72.0f * PC_NLR_M) /* how tall an opening is */
#define PC_NLR_OPEN_WIDE (72.0f * PC_NLR_M) /* how wide: two units, less the frame */
#define PC_NLR_PASSAGE (24.0f * PC_NLR_M)   /* how deep the passage behind it */
#define PC_NLR_FRAME (3.0f * PC_NLR_M)      /* the wooden frame around it, on the wall */
#define PC_NLR_FRAME_OUT (0.75f * PC_NLR_M) /* standing out from the wall */

typedef struct {
    int along_z; /* the wall is at x = wall, running along z; else at z = wall, along x */
    f32 wall;
    f32 s0, s1;  /* across along the wall */
    f32 inward;  /* +1/-1 towards the room */
} pc_nlr_opening_c;

/* The openings in this room's walls, in model units; *front_sealed is where the room ends at the front when its
 * front door leads nowhere (0 when it does) */
static int pc_nlr_openings(pc_nlr_opening_c* op, int* front_sealed) {
    int house = pc_nlr_house();
    int scene = Save_Get(scene_no);
    int n = 0, size;
    f32 u = PC_NLR_UNIT * PC_NLR_M, h = PC_NLR_OPEN_WIDE * 0.5f;

    *front_sealed = 0;
    if (l_door_num == 0 || house < 0 ||
        scene != (l_cur < 0 ? SCENE_MY_ROOM_LL1 : pc_nlr_scene_of(l_nlr[house].size[l_cur]))) {
        return 0;
    }
#define PC_NLR_OPEN(z, w, c, in) \
    (op[n].along_z = (z), op[n].wall = (w), op[n].s0 = (c) - h, op[n].s1 = (c) + h, op[n].inward = (in), n++)
    if (l_cur < 0) {
        if (l_door_of_room[PC_NLR_LEFT] >= 0) {
            PC_NLR_OPEN(TRUE, u, 8 * u, 1.0f);
        }
        if (l_door_of_room[PC_NLR_RIGHT] >= 0) {
            PC_NLR_OPEN(TRUE, 9 * u, 8 * u, -1.0f);
        }
        if (l_door_of_room[PC_NLR_BACK] >= 0) {
            PC_NLR_OPEN(FALSE, u, 5 * u, 1.0f);
        }
    } else {
        size = l_nlr[house].size[l_cur];
        if (l_cur == PC_NLR_LEFT) {
            PC_NLR_OPEN(TRUE, (size + 1) * u, size * u, -1.0f);
        } else if (l_cur == PC_NLR_RIGHT) {
            PC_NLR_OPEN(TRUE, u, size * u, 1.0f);
        }
        /* the back room's way out is its front door; the side rooms have none there */
        *front_sealed = l_cur != PC_NLR_BACK ? (int)((size + 1) * u) : 0;
    }
#undef PC_NLR_OPEN
    return n;
}

typedef struct {
    f32 p[3];  /* position */
    f32 t[2];  /* texture coordinates */
    f32 c[4];  /* colour */
} pc_nlr_pt_c;

static void pc_nlr_pt_from(pc_nlr_pt_c* pt, const Vtx* v) {
    int k;

    for (k = 0; k < 3; k++) {
        pt->p[k] = v->v.ob[k];
    }
    pt->t[0] = v->v.tc[0];
    pt->t[1] = v->v.tc[1];
    for (k = 0; k < 4; k++) {
        pt->c[k] = v->v.cn[k];
    }
}

static void pc_nlr_pt_lerp(pc_nlr_pt_c* out, const pc_nlr_pt_c* a, const pc_nlr_pt_c* b, f32 f) {
    int k;

    for (k = 0; k < 3; k++) {
        out->p[k] = a->p[k] + (b->p[k] - a->p[k]) * f;
    }
    for (k = 0; k < 2; k++) {
        out->t[k] = a->t[k] + (b->t[k] - a->t[k]) * f;
    }
    for (k = 0; k < 4; k++) {
        out->c[k] = a->c[k] + (b->c[k] - a->c[k]) * f;
    }
}

/* Where a point is across the wall (axis = the wall's run) and up it */
#define PC_NLR_ACROSS(pt, o) ((pt)->p[(o)->along_z ? 2 : 0])

/* Keep the part of polygon `in` (n points) where sign * (coordinate - limit) >= 0 */
static int pc_nlr_clip(pc_nlr_pt_c* out, const pc_nlr_pt_c* in, int n, int coord, f32 limit, f32 sign) {
    int i, m = 0;

    for (i = 0; i < n; i++) {
        const pc_nlr_pt_c* a = &in[i];
        const pc_nlr_pt_c* b = &in[(i + 1) % n];
        f32 da = sign * (a->p[coord] - limit), db = sign * (b->p[coord] - limit);

        if (da >= 0.0f) {
            out[m++] = *a;
        }
        if ((da >= 0.0f) != (db >= 0.0f)) {
            pc_nlr_pt_lerp(&out[m++], a, b, da / (da - db));
        }
    }
    return m;
}

#define PC_NLR_WALL_SLOTS 3
#define PC_NLR_WALL_GFX 1024
#define PC_NLR_WALL_VTX 768
#define PC_NLR_PIECE_PTS 8

typedef struct {
    Gfx* src;
    u32 key;
    Gfx gfx[PC_NLR_WALL_GFX];
    Vtx vtx[PC_NLR_WALL_VTX];
} pc_nlr_wall_c;

static pc_nlr_wall_c l_walls[PC_NLR_WALL_SLOTS];
static int l_wall_next;

typedef struct {
    pc_nlr_wall_c* w;
    int ng, nv;
    int batch_first;           /* the first vertex of the batch not yet drawn */
    u8 tri[32 * 3];
    int ntri;
} pc_nlr_build_c;

static void pc_nlr_emit_cmd(pc_nlr_build_c* b, const Gfx* g) {
    if (b->ng < PC_NLR_WALL_GFX - 1) {
        b->w->gfx[b->ng++] = *g;
    }
}

/* Draw the triangles batched so far, then load `restore` again for the wall's own triangles that follow */
static void pc_nlr_flush(pc_nlr_build_c* b, const Gfx* restore) {
    Gfx* g;
    int i, nv = b->nv - b->batch_first;

    if (nv > 0 && b->ntri > 0 && b->ng + 2 + b->ntri + 1 < PC_NLR_WALL_GFX) {
        g = &b->w->gfx[b->ng];
        gSPVertex(g++, &b->w->vtx[b->batch_first], nv, 0);
        for (i = 0; i < b->ntri; i++) {
            gSPNTrianglesInit_5b(g++, 1, b->tri[i * 3], b->tri[i * 3 + 1], b->tri[i * 3 + 2], 0, 0, 0, 0, 0, 0);
        }
        b->ng = (int)(g - b->w->gfx);
        if (restore != NULL) {
            pc_nlr_emit_cmd(b, restore);
        }
    }
    b->batch_first = b->nv;
    b->ntri = 0;
}

static int pc_nlr_add_vtx(pc_nlr_build_c* b, const pc_nlr_pt_c* pt) {
    Vtx* v;
    int k;

    if (b->nv >= PC_NLR_WALL_VTX) {
        return -1;
    }
    v = &b->w->vtx[b->nv];
    for (k = 0; k < 3; k++) {
        v->v.ob[k] = (s16)(pt->p[k] + (pt->p[k] < 0.0f ? -0.5f : 0.5f));
    }
    v->v.flag = 0;
    v->v.tc[0] = (s16)pt->t[0];
    v->v.tc[1] = (s16)pt->t[1];
    for (k = 0; k < 4; k++) {
        v->v.cn[k] = (u8)(pt->c[k] < 0.0f ? 0.0f : pt->c[k] > 255.0f ? 255.0f : pt->c[k]);
    }
    return b->nv++ - b->batch_first;
}

/* A convex polygon as a fan of triangles in the batch */
static void pc_nlr_add_poly(pc_nlr_build_c* b, const pc_nlr_pt_c* pts, int n, const Gfx* restore) {
    int i, first;

    if (n < 3) {
        return;
    }
    if (b->nv - b->batch_first + n > 32 || b->ntri + n - 2 > 32) {
        pc_nlr_flush(b, restore);
    }
    first = b->nv - b->batch_first;
    for (i = 0; i < n; i++) {
        if (pc_nlr_add_vtx(b, &pts[i]) < 0) {
            return;
        }
    }
    for (i = 1; i + 1 < n; i++) {
        b->tri[b->ntri * 3 + 0] = (u8)first;
        b->tri[b->ntri * 3 + 1] = (u8)(first + i);
        b->tri[b->ntri * 3 + 2] = (u8)(first + i + 1);
        b->ntri++;
    }
}

/* The opening triangle `v` of the wall is in, -1 when none */
static int pc_nlr_tri_opening(Vtx* const v[3], const pc_nlr_opening_c* op, int nop) {
    int i, k, axis;

    for (i = 0; i < nop; i++) {
        const pc_nlr_opening_c* o = &op[i];
        f32 lo = 1e9f, hi = -1e9f, ylo = 1e9f, yhi = -1e9f;
        int on_wall = TRUE;

        axis = o->along_z ? 0 : 2;
        for (k = 0; k < 3; k++) {
            f32 s = v[k]->v.ob[o->along_z ? 2 : 0], y = v[k]->v.ob[1];

            on_wall &= v[k]->v.ob[axis] == (s16)o->wall;
            lo = s < lo ? s : lo, hi = s > hi ? s : hi;
            ylo = y < ylo ? y : ylo, yhi = y > yhi ? y : yhi;
        }
        if (on_wall && hi > o->s0 && lo < o->s1 && yhi > PC_NLR_FLOOR_Y && ylo < PC_NLR_FLOOR_Y + PC_NLR_OPEN_HIGH) {
            return i;
        }
    }
    return -1;
}

/* Triangle `v` of the wall without opening `o` in it: the rest of it, beside, above and below the opening */
static void pc_nlr_cut_tri(pc_nlr_build_c* b, Vtx* const v[3], const pc_nlr_opening_c* o, const Gfx* restore) {
    pc_nlr_pt_c tri[3], rest[PC_NLR_PIECE_PTS], piece[PC_NLR_PIECE_PTS], tmp[PC_NLR_PIECE_PTS];
    int k, n, m, coord;

    for (k = 0; k < 3; k++) {
        pc_nlr_pt_from(&tri[k], v[k]);
    }
    coord = o->along_z ? 2 : 0;
    /* beside: before s0, and after s1 */
    n = pc_nlr_clip(piece, tri, 3, coord, o->s0, -1.0f);
    pc_nlr_add_poly(b, piece, n, restore);
    n = pc_nlr_clip(piece, tri, 3, coord, o->s1, 1.0f);
    pc_nlr_add_poly(b, piece, n, restore);
    /* between them: above the opening, and below (nothing, as it's on the floor) */
    n = pc_nlr_clip(tmp, tri, 3, coord, o->s0, 1.0f);
    n = pc_nlr_clip(rest, tmp, n, coord, o->s1, -1.0f);
    m = pc_nlr_clip(piece, rest, n, 1, PC_NLR_FLOOR_Y + PC_NLR_OPEN_HIGH, 1.0f);
    pc_nlr_add_poly(b, piece, m, restore);
    m = pc_nlr_clip(piece, rest, n, 1, PC_NLR_FLOOR_Y, -1.0f);
    pc_nlr_add_poly(b, piece, m, restore);
}

/* A quad from corners given across the wall, up it and out from it (towards the room), each with its colour, facing
 * `facing` (across, up, out: one of them +1 or -1); with `back`, its back too, in black */
static void pc_nlr_add_quad(pc_nlr_build_c* b, const pc_nlr_opening_c* o, const f32 corner[4][3], const f32* col[4],
                            const s8 facing[3], int back) {
    pc_nlr_pt_c q[4], t;
    f32 e1[3], e2[3], n[3], want[3];
    int k, c;

    for (k = 0; k < 4; k++) {
        q[k].p[o->along_z ? 2 : 0] = corner[k][0];
        q[k].p[1] = corner[k][1];
        q[k].p[o->along_z ? 0 : 2] = o->wall + o->inward * corner[k][2];
        q[k].t[0] = q[k].t[1] = 0.0f;
        for (c = 0; c < 4; c++) {
            q[k].c[c] = col[k][c];
        }
    }
    /* wound so it faces the way it should, the walls' way: counterclockwise seen from in front */
    want[o->along_z ? 2 : 0] = facing[0];
    want[1] = facing[1];
    want[o->along_z ? 0 : 2] = facing[2] * o->inward;
    for (c = 0; c < 3; c++) {
        e1[c] = q[1].p[c] - q[0].p[c];
        e2[c] = q[2].p[c] - q[0].p[c];
    }
    n[0] = e1[1] * e2[2] - e1[2] * e2[1];
    n[1] = e1[2] * e2[0] - e1[0] * e2[2];
    n[2] = e1[0] * e2[1] - e1[1] * e2[0];
    if (n[0] * want[0] + n[1] * want[1] + n[2] * want[2] < 0.0f) {
        t = q[1], q[1] = q[3], q[3] = t;
    }
    pc_nlr_add_poly(b, q, 4, NULL);
    if (back) {
        t = q[1], q[1] = q[3], q[3] = t;
        for (k = 0; k < 4; k++) {
            q[k].c[0] = q[k].c[1] = q[k].c[2] = 0.0f;
        }
        pc_nlr_add_poly(b, q, 4, NULL);
    }
}

/* The passage behind an opening, its sides, ceiling and floor fading into the dark, and the frame around it */
static void pc_nlr_add_passage(pc_nlr_build_c* b, const pc_nlr_opening_c* o) {
    static const f32 wood[4] = { 132.0f, 88.0f, 54.0f, 255.0f };
    static const f32 wood_lit[4] = { 168.0f, 116.0f, 72.0f, 255.0f };
    static const f32 wood_dark[4] = { 70.0f, 44.0f, 26.0f, 255.0f };
    static const f32 dark[4] = { 0.0f, 0.0f, 0.0f, 255.0f };
    f32 s0 = o->s0, s1 = o->s1, y0 = PC_NLR_FLOOR_Y, y1 = PC_NLR_FLOOR_Y + PC_NLR_OPEN_HIGH;
    f32 d = -PC_NLR_PASSAGE, f = PC_NLR_FRAME, fo = PC_NLR_FRAME_OUT;
    const f32* fade[4] = { wood_dark, wood_dark, dark, dark };
    const f32* black[4] = { dark, dark, dark, dark };
    const f32* face[4] = { wood_lit, wood_lit, wood, wood };
    const f32* side[4] = { wood, wood, wood_dark, wood_dark };

    /* the passage: sides, ceiling, floor (corners at the wall first), and its dark end */
    {
        const f32 q[5][4][3] = {
            { { s0, y0, 0 }, { s0, y1, 0 }, { s0, y1, d }, { s0, y0, d } },
            { { s1, y1, 0 }, { s1, y0, 0 }, { s1, y0, d }, { s1, y1, d } },
            { { s0, y1, 0 }, { s1, y1, 0 }, { s1, y1, d }, { s0, y1, d } },
            { { s1, y0, 0 }, { s0, y0, 0 }, { s0, y0, d }, { s1, y0, d } },
            { { s0, y0, d }, { s1, y0, d }, { s1, y1, d }, { s0, y1, d } },
        };
        static const s8 facing[5][3] = { { 1, 0, 0 }, { -1, 0, 0 }, { 0, -1, 0 }, { 0, 1, 0 }, { 0, 0, 1 } };
        int k;

        /* seen from outside (past the room's open front) they're black, as the dark around the room */
        for (k = 0; k < 5; k++) {
            pc_nlr_add_quad(b, o, q[k], k < 4 ? fade : black, facing[k], TRUE);
        }
    }
    /* the frame: its face on the wall, lit from above, and its edges into the opening and out to the wall */
    {
        const f32 q[][4][3] = {
            { { s0 - f, y1 + f, fo }, { s1 + f, y1 + f, fo }, { s1 + f, y1, fo }, { s0 - f, y1, fo } }, /* top */
            { { s0 - f, y1, fo }, { s0, y1, fo }, { s0, y0, fo }, { s0 - f, y0, fo } },                 /* sides */
            { { s1, y1, fo }, { s1 + f, y1, fo }, { s1 + f, y0, fo }, { s1, y0, fo } },
            { { s0, y1, fo }, { s0, y1, 0 }, { s0, y0, 0 }, { s0, y0, fo } }, /* the edges: into the opening */
            { { s1, y1, 0 }, { s1, y1, fo }, { s1, y0, fo }, { s1, y0, 0 } },
            { { s0, y1, fo }, { s1, y1, fo }, { s1, y1, 0 }, { s0, y1, 0 } },
            { { s0 - f, y1 + f, 0 }, { s1 + f, y1 + f, 0 }, { s1 + f, y1 + f, fo }, { s0 - f, y1 + f, fo } }, /* out */
            { { s0 - f, y1 + f, fo }, { s0 - f, y1 + f, 0 }, { s0 - f, y0, 0 }, { s0 - f, y0, fo } },
            { { s1 + f, y1 + f, 0 }, { s1 + f, y1 + f, fo }, { s1 + f, y0, fo }, { s1 + f, y0, 0 } },
        };
        static const s8 facing[][3] = {
            { 0, 0, 1 }, { 0, 0, 1 }, { 0, 0, 1 }, { 1, 0, 0 }, { -1, 0, 0 }, { 0, -1, 0 }, { 0, 1, 0 }, { -1, 0, 0 }, { 1, 0, 0 },
        };
        int k;

        for (k = 0; k < (int)(sizeof(q) / sizeof(q[0])); k++) {
            pc_nlr_add_quad(b, o, q[k], k < 3 ? face : side, facing[k], FALSE);
        }
    }
}

static u32 pc_nlr_wall_key(const pc_nlr_opening_c* op, int nop, int front_sealed) {
    u32 key = 2166136261u;
    int i;

    for (i = 0; i < nop; i++) {
        key = (key ^ (u32)op[i].wall) * 16777619u;
        key = (key ^ (u32)op[i].s0) * 16777619u;
        key = (key ^ (u32)(op[i].along_z * 2 + (op[i].inward > 0))) * 16777619u;
    }
    return (key ^ (u32)(nop * 2 + front_sealed)) * 16777619u;
}

/* Copy wall model `src` into `w` with the openings cut out, the passages behind them added, and, in a room
 * whose front door is sealed, without the floor of the door's alcove */
static void pc_nlr_build_wall(pc_nlr_wall_c* w, Gfx* src, const pc_nlr_opening_c* op, int nop, int front_sealed) {
    pc_nlr_build_c b;
    Vtx* cache[64];
    Vtx* cut[32][3];
    int cut_op[32];
    const Gfx* last_vtx = NULL;
    Gfx* g = src;
    int i, k, ncut;

    memset(&b, 0, sizeof(b));
    memset(cache, 0, sizeof(cache));
    b.w = w;

    for (; b.ng < PC_NLR_WALL_GFX - 64; g++) {
        u32 cmd = g->words.w0 >> 24;

        if (cmd == G_ENDDL) {
            break;
        }
        if (cmd == G_VTX) {
            int n = (g->words.w0 >> 12) & 0xFF;
            int v0 = ((g->words.w0 >> 1) & 0x7F) - n;
            Vtx* vp = (Vtx*)(uintptr_t)g->words.w1;

            for (k = 0; k < n && v0 + k < 64; k++) {
                cache[v0 + k] = &vp[k];
            }
            last_vtx = g;
            pc_nlr_emit_cmd(&b, g);
            continue;
        }
        if ((cmd == G_TRIN_INDEPEND || cmd == G_TRIN) && (g->words.w1 & 1) == 0) {
            /* 5 bit indices: three triangles in the first command, four in each after */
            int left = ((g->words.w0 >> 17) & 0x7F) + 1;
            int slot = 0, gone, o;

            ncut = 0;
            for (;;) {
                Gfx cur = *g;
                int per = slot == 0 ? 3 : 4;

                for (k = 0; k < per && left > 0; k++, left--) {
                    int idx[3];
                    Vtx* tv[3];

                    if (k == 0) {
                        idx[0] = POLY_GET_V0_5b(g), idx[1] = POLY_GET_V1_5b(g), idx[2] = POLY_GET_V2_5b(g);
                    } else if (k == 1) {
                        idx[0] = POLY_GET_V3_5b(g), idx[1] = POLY_GET_V4_5b(g), idx[2] = POLY_GET_V5_5b(g);
                    } else if (k == 2) {
                        idx[0] = POLY_GET_V6_5b(g), idx[1] = POLY_GET_V7_5b(g), idx[2] = POLY_GET_V8_5b(g);
                    } else {
                        idx[0] = POLY_GET_V9_5b(g), idx[1] = POLY_GET_V10_5b(g), idx[2] = POLY_GET_V11_5b(g);
                    }
                    tv[0] = cache[idx[0]], tv[1] = cache[idx[1]], tv[2] = cache[idx[2]];
                    if (tv[0] == NULL || tv[1] == NULL || tv[2] == NULL) {
                        continue;
                    }
                    /* the front door's alcove, sealed, beyond the front edge */
                    gone = front_sealed && tv[0]->v.ob[2] >= front_sealed && tv[1]->v.ob[2] >= front_sealed &&
                           tv[2]->v.ob[2] >= front_sealed;
                    o = gone ? -1 : pc_nlr_tri_opening(tv, op, nop);
                    if (o >= 0 && ncut < 32) {
                        cut[ncut][0] = tv[0], cut[ncut][1] = tv[1], cut[ncut][2] = tv[2];
                        cut_op[ncut++] = o;
                        gone = TRUE;
                    }
                    if (gone) {
                        /* the triangle goes: its indices made the same vertex three times */
                        if (k == 0) {
                            cur.words.w1 &= ~(0x7FFFu << 4);
                        } else if (k == 1) {
                            cur.words.w1 &= ~(0x1FFFu << 19);
                            cur.words.w0 &= ~3u;
                        } else if (k == 2) {
                            cur.words.w0 &= ~(0x7FFFu << 2);
                        } else {
                            cur.words.w0 &= ~(0x7FFFu << 17);
                        }
                    }
                }
                pc_nlr_emit_cmd(&b, &cur);
                slot++;
                if (left <= 0) {
                    break;
                }
                g++;
            }
            /* what's left of the cut triangles, after the wall's own */
            for (k = 0; k < ncut; k++) {
                pc_nlr_cut_tri(&b, cut[k], &op[cut_op[k]], last_vtx);
            }
            if (ncut > 0) {
                pc_nlr_flush(&b, last_vtx);
            }
            continue;
        }
        pc_nlr_emit_cmd(&b, g);
    }

    /* the passages and frames, shaded */
    if (nop > 0) {
        Gfx* s = &w->gfx[b.ng];

        gDPPipeSync(s++);
        gSPTexture(s++, 0, 0, 0, G_TX_RENDERTILE, G_OFF);
        /* lit by the room's light, as the walls are */
        gDPSetCombineLERP(s++, 0, 0, 0, SHADE, 0, 0, 0, 1, PRIMITIVE, 0, COMBINED, 0, 0, 0, 0, COMBINED);
        gDPSetRenderMode(s++, G_RM_FOG_SHADE_A, G_RM_AA_ZB_OPA_SURF2);
        gSPLoadGeometryMode(s++, G_ZBUFFER | G_SHADE | G_CULL_BACK | G_FOG | G_SHADING_SMOOTH);
        b.ng = (int)(s - w->gfx);
        for (i = 0; i < nop; i++) {
            pc_nlr_add_passage(&b, &op[i]);
        }
        pc_nlr_flush(&b, NULL);
    }
    gSPEndDisplayList(&w->gfx[b.ng]);
}

Gfx* pc_nlr_wall_model(Gfx* model) {
    pc_nlr_opening_c op[PC_NLR_ROOMS];
    int nop, front_sealed, i;
    u32 key;
    pc_nlr_wall_c* w;

    if (model == NULL || !mSc_IS_SCENE_PLAYER_ROOM(Save_Get(scene_no))) {
        return model;
    }
    nop = pc_nlr_openings(op, &front_sealed);
    if (nop == 0 && !front_sealed) {
        return model;
    }
    key = pc_nlr_wall_key(op, nop, front_sealed);
    for (i = 0; i < PC_NLR_WALL_SLOTS; i++) {
        if (l_walls[i].src == model && l_walls[i].key == key) {
            return l_walls[i].gfx;
        }
    }
    w = &l_walls[l_wall_next];
    l_wall_next = (l_wall_next + 1) % PC_NLR_WALL_SLOTS;
    pc_nlr_build_wall(w, model, op, nop, front_sealed);
    w->src = model;
    w->key = key;
    return w->gfx;
}

/* --- the companion save --- */

void pc_nlr_clear_house(int house) {
    if (house >= 0 && house < PLAYER_NUM) {
        memset(&l_nlr[house], 0, sizeof(l_nlr[house]));
        l_nlr[house].order.room = -1;
    }
}

void pc_nlr_reset(void) {
    int house;

    for (house = 0; house < PLAYER_NUM; house++) {
        pc_nlr_clear_house(house);
    }
    l_granted = FALSE; /* the dev grant goes to the town just loaded */
    l_cur = -1;
    l_pending = -2;
    l_door_num = 0;
}

int pc_nlr_read_chunk(const char tag[4], void* fp, unsigned int size) {
    FILE* f = (FILE*)fp;
    u32 version, houses, i;
    s32 cur;
    long end;

    if (memcmp(tag, "NLRM", 4) != 0) {
        return FALSE;
    }
    end = ftell(f) + (long)size;
    pc_nlr_reset();
    if (fread(&version, 4, 1, f) != 1 || version != PC_NLR_VERSION || fread(&cur, 4, 1, f) != 1 ||
        fread(&houses, 4, 1, f) != 1) {
        fseek(f, end, SEEK_SET);
        return TRUE;
    }
    for (i = 0; i < houses && i < PLAYER_NUM; i++) {
        pc_nlr_chunk_house_c h;
        int r;

        if (fread(&h, sizeof(h), 1, f) != 1 || h.house >= PLAYER_NUM) {
            break;
        }
        for (r = 0; r < PC_NLR_ROOMS; r++) {
            if (h.size[r] != 0 && fread(&l_nlr[h.house].room[r], sizeof(mHm_flr_c), 1, f) != 1) {
                break;
            }
        }
        /* only back to the house's owner (players can be deleted and remade) */
        if (mPr_CheckCmpPersonalID(&h.owner, &Save_Get(homes[h.house]).ownerID)) {
            memcpy(l_nlr[h.house].size, h.size, sizeof(h.size));
            l_nlr[h.house].order = h.order;
        } else {
            pc_nlr_clear_house(h.house);
        }
    }
    l_pending = cur; /* saved while in an extra room: back into it */
    fseek(f, end, SEEK_SET);
    return TRUE;
}

int pc_nlr_write_chunk(void* fp) {
    FILE* f = (FILE*)fp;
    u32 version = PC_NLR_VERSION, houses = 0, size;
    s32 cur = l_cur;
    int i, r, ok;

    size = 12;
    for (i = 0; i < PLAYER_NUM; i++) {
        int rooms = 0;

        for (r = 0; r < PC_NLR_ROOMS; r++) {
            rooms += l_nlr[i].size[r] != 0;
        }
        if (rooms != 0 || l_nlr[i].order.room >= 0) {
            houses++;
            size += sizeof(pc_nlr_chunk_house_c) + rooms * sizeof(mHm_flr_c);
        }
    }
    ok = fwrite("NLRM", 1, 4, f) == 4 && fwrite(&size, 4, 1, f) == 1 && fwrite(&version, 4, 1, f) == 1 &&
         fwrite(&cur, 4, 1, f) == 1 && fwrite(&houses, 4, 1, f) == 1;
    for (i = 0; ok && i < PLAYER_NUM; i++) {
        pc_nlr_chunk_house_c h;

        memset(&h, 0, sizeof(h));
        h.house = (u8)i;
        memcpy(h.size, l_nlr[i].size, sizeof(h.size));
        h.order = l_nlr[i].order;
        if (h.size[0] == 0 && h.size[1] == 0 && h.size[2] == 0 && h.order.room < 0) {
            continue;
        }
        h.owner = Save_Get(homes[i]).ownerID;
        ok = fwrite(&h, sizeof(h), 1, f) == 1;
        for (r = 0; ok && r < PC_NLR_ROOMS; r++) {
            if (h.size[r] != 0) {
                ok = fwrite(&l_nlr[i].room[r], sizeof(mHm_flr_c), 1, f) == 1;
            }
        }
    }
    return ok;
}
