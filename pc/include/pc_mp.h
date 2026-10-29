#ifndef PC_MP_H
#define PC_MP_H

/* Online multiplayer, the game's side (pc_mp.c). The network layer is pc/src/mp; nothing here sees raw bytes.
 * Slot 0 is the host (the town's owner), 1-3 are visitors. */

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PC_MP_SLOTS 4

struct actor_s;
struct game_s;

/* Another player as this game shows them: checked values only */
typedef struct {
    int present;   /* connected */
    int has_state; /* a position has arrived */
    int has_look;
    /* checked state */
    int scene;
    int field;
    int visible;
    int train_flags; /* MP_PS_TRAIN_IN / _OUT as last sent */
    int train_seen;  /* host: which of those its own train has already answered */
    f32 x, y, z;
    s16 angle_y;
    int anim0, anim1, part_table, eye, mouth;
    f32 frame0, frame1, speed;
    u32 tick;
    /* the tool in their hand: item main index, model (-1 none), animation (-1 none), umbrella kind (-1 none) */
    int item_main, item_shape, item_anim, umbrella;
    f32 item_frame;
    int catch_item; /* a fish they hold up (its item), 0 = none */
    int root_flags; /* their body's root held still (sitting, in bed), at root_rot / root_trans */
    s16 root_rot[3];
    s16 tilt_x, tilt_z; /* their body tipped over (lying down) */
    f32 root_trans[3];
    /* look, ready to draw */
    int gender;
    u8 name[8];
    u8 face_tex[0xE00] ATTRIBUTE_ALIGN(32);
    u16 face_pal[16] ATTRIBUTE_ALIGN(32);
    u8 cloth_tex[0x200] ATTRIBUTE_ALIGN(32);
    u16 cloth_pal[16] ATTRIBUTE_ALIGN(32);
    struct actor_s* actor; /* the puppet in this scene, if any (ac_mp_puppet.c) */
} pc_mp_remote;

/* NULL unless slot is someone else who is connected */
pc_mp_remote* pc_mp_get_remote(int slot);
/* this game's own slot (0 when hosting or offline) */
int pc_mp_my_slot(void);
int pc_mp_active(void);

/* starting and stopping (pause menu > Online Play, pc_mp_menu.c) */
int pc_mp_host_start(void);
int pc_mp_visit_start(const char* address);
void pc_mp_stop(void);
int pc_mp_hosting(void);
int pc_mp_visiting(void);
int pc_mp_visitors(void);
/* hosting with a matchmaking server: the invite code to give visitors ("" until the server answers) */
const char* pc_mp_invite_code(void);
/* host: the visitor in slot (1-3), if any - name as text (9 bytes) and whether they're trusted */
int pc_mp_visitor(int slot, char* name, int* trusted);
/* host: a trusted visitor may dig, chop, pick flowers, ... like the owner */
void pc_mp_set_trusted(int slot, int on);
void pc_mp_send_home(int slot);
/* may this game change anything in the town (not visiting, or a visitor the host trusts) */
int pc_mp_trusted(void);
/* may a guest shake this tree (pc_mp_world.c) */
int pc_mp_guest_may_shake(u16 tree);
/* the server's public towns: start asking, then count (-1 asking, -2 no answer) and each town's code and names
 * (text, 7 and 9 bytes) */
int pc_mp_browse_start(void);
void pc_mp_browse_stop(void);
int pc_mp_browse_count(void);
int pc_mp_browse_town(int i, char* code, char* town, char* name);
/* towns visited before (newest first): town and host name as text (9 bytes each); visit one, forget one */
int pc_mp_friend(int i, char* town, char* name);
int pc_mp_visit_friend(int i);
void pc_mp_forget_friend(int i);
/* say a line (plain text, up to 32 characters) to everyone in the session */
int pc_mp_say(const char* text);
/* the last thing that happened, for the menu */
const char* pc_mp_status(void);
/* this game's key, as a fingerprint people can compare ("ABCD-EFGH-...", 20 bytes) */
void pc_mp_fingerprint(char* out);

/* every frame in a play scene (m_play.c) */
void pc_mp_play_tick(struct game_s* game);
/* at exit */
void pc_mp_shutdown(void);

#ifdef __cplusplus
}
#endif

#endif
