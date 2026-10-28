/* mp_wire.h - multiplayer messages: the only code that reads bytes from the network.
 *
 * Everything that arrives (after decryption, see mp_noise.c) goes through mp_wire_decode_frame(), which turns it
 * into the fixed-size structs below or rejects the whole frame. Game code only ever sees these structs. Rules:
 *  - every read is bounds-checked against the frame (mp_rd), lengths are checked before use and can't overflow;
 *  - every message has a fixed size, or a fixed maximum checked before copying;
 *  - floats must be finite and inside the world's range; counts and indices are checked against fixed limits here,
 *    and game-specific ids (items, animations, scenes) are checked again by pc_mp.c against the game's own tables;
 *  - text is the game's own 8-bit charset, fixed length, with control codes removed, and is never a format string.
 * No game headers here, so the file builds natively for the tests and the fuzzer (pc/tools/mp-test). */
#ifndef MP_WIRE_H
#define MP_WIRE_H

#include <stddef.h>
#include <stdint.h>

#define MP_PROTO_VERSION 8
/* JOIN's version: 9 from Alpha 9.1, whose JOIN also names its release; games of other JOIN versions refuse each
 * other (MP_BYE_VERSION, a message older games already show) */
#define MP_JOIN_VERSION 9
#define MP_RELEASE_LEN 16
#define MP_MAX_PACKET 1200     /* UDP payload, fits any path MTU */
#define MP_MAX_FRAME 1160      /* plaintext inside one packet (+ 29 bytes of DATA header and tag) */
#define MP_MAX_RECORDS 16      /* messages in one frame */
#define MP_NAME_LEN 8          /* PLAYER_NAME_LEN / LAND_NAME_SIZE */
#define MP_BULK_CHUNK 960      /* bytes of a bulk stream per message */
#define MP_BULK_MAX (512 * 1024) /* largest bulk stream (town data) */
#define MP_POS_LIMIT 100000.0f /* |x|,|y|,|z| in world units */
#define MP_CHAT_LEN 32
#define MP_DESIGN_BYTES 0x200  /* a 32x32 4bpp pattern */

/* message types */
enum {
    MP_MSG_NONE = 0,
    MP_MSG_PSTATE,   /* unreliable: where a player is and what they're doing */
    MP_MSG_JOIN,     /* reliable, visitor -> host: who I am */
    MP_MSG_WELCOME,  /* reliable, host -> visitor: accepted, your slot, the town */
    MP_MSG_LOOK,     /* reliable: appearance (clothes, face) */
    MP_MSG_BULK,     /* reliable: a piece of a bulk stream (town data) */
    MP_MSG_CLOCK,    /* reliable, host -> visitor: the town's date and time */
    MP_MSG_BYE,      /* reliable: leaving, with a reason */
    MP_MSG_CHAT,     /* reliable: a short line of text */
    MP_MSG_ACTION,   /* reliable, visitor -> host: a request to change the town (host checks it) */
    MP_MSG_WORLD,    /* reliable, host -> visitor: a change to the town */
    MP_MSG_NPCS,     /* unreliable: where the villagers this game has loaded are */
    MP_MSG_LEFT,     /* reliable, host -> visitor: another visitor went home */
    MP_MSG_FISH,     /* reliable, a few a second: the fish where the host is (host -> visitor), the ones a visitor
                      * is fishing for or caught (visitor -> host) */
    MP_MSG_BUGS,     /* the same for insects: the host's, and the ones a visitor is after or caught */
    MP_MSG_MEMORY,   /* reliable: a villager's memory of a visitor (host -> visitor on joining: theirs only;
                      * visitor -> host when it changes, for the host's save) */
    MP_MSG_COUNT
};

typedef struct {
    uint8_t player;    /* slot 0..3 */
    uint16_t scene;    /* game scene number */
    uint16_t field;    /* field id (tells apart houses that share a scene) */
    uint8_t flags;     /* MP_PS_* */
    float x, y, z;
    int16_t angle_y;
    uint16_t anim0, anim1;
    float frame0, frame1;
    float speed;
    uint8_t part_table;
    uint8_t eye, mouth;
    /* the tool in their hand, as the player actor shows it (checked against the game's tables in pc_mp.c) */
    uint8_t item_main;  /* mPlayer_ITEM_MAIN_*, 0 = none */
    uint8_t item_shape; /* item model/skeleton, 0xFF = none */
    uint8_t item_anim;  /* its animation, 0xFF = none */
    uint8_t umbrella;   /* umbrella kind + 1, 0 = none */
    float item_frame;
    uint16_t catch_item; /* a fish they hold up having caught it (its item), 0 = none */
    uint32_t tick;      /* sender's frame counter, for ordering */
} mp_pstate;

#define MP_PS_VISIBLE 0x01
#define MP_PS_FLAGS_MASK 0x01

typedef struct {
    uint8_t slot;     /* whose look (the host sets it; a visitor's own JOIN says 0) */
    uint8_t name[MP_NAME_LEN];
    uint8_t town[MP_NAME_LEN];
    uint8_t gender;   /* 0 male, 1 female */
    uint8_t face;     /* 0..7 */
    uint8_t skin;     /* skin tone, checked by the game */
    uint8_t tan;      /* sunburn rank, checked by the game */
    uint16_t cloth;   /* what they wear, for show only: the picture is below */
    uint8_t cloth_tex[MP_DESIGN_BYTES]; /* the shirt texture as worn (32x32, 4 bits per pixel) */
    uint16_t cloth_pal[16];             /* and its palette (RGB5A3) */
} mp_look;

typedef struct {
    uint16_t version;
    uint8_t want_slot; /* 0 = any */
    mp_look look;
    uint8_t release[MP_RELEASE_LEN]; /* the release, e.g. "alpha-9.1" (its VERSION file); empty: a dev build, or a
                                      * JOIN from before MP_JOIN_VERSION 9 */
} mp_join;

typedef struct {
    uint8_t slot;        /* the visitor's player slot */
    uint8_t permissions; /* MP_PERM_* */
    uint8_t town[MP_NAME_LEN];
    uint32_t town_bytes; /* size of the bulk stream that follows */
} mp_welcome;

#define MP_PERM_TRUSTED 0x01
#define MP_PERM_MASK 0x01

typedef struct {
    uint8_t stream; /* 1 = town */
    uint32_t total;
    uint32_t offset;
    uint16_t len;
    uint8_t data[MP_BULK_CHUNK];
} mp_bulk;

typedef struct {
    uint16_t year;
    uint8_t month, day, hour, min, sec, weekday;
    uint8_t weather, weather_intensity;
} mp_clock;

typedef struct {
    uint8_t reason; /* MP_BYE_* */
} mp_bye;

typedef struct {
    uint8_t slot; /* 1..3 */
} mp_left;

enum { MP_BYE_QUIT, MP_BYE_FULL, MP_BYE_VERSION, MP_BYE_REFUSED, MP_BYE_KICKED, MP_BYE_ERROR, MP_BYE_COUNT };

typedef struct {
    uint8_t player;
    uint8_t len;
    uint8_t text[MP_CHAT_LEN];
} mp_chat;

/* changes to the town; the visitor asks with ACTION, the host checks and answers everyone with WORLD */
enum {
    MP_ACT_SET = 1, /* a unit of the town: ux, uz; item = what was there, new_item = what is there now */
    MP_ACT_DEPOSIT, /* buried-item flags: ux = acre (0-29), uz = row (0-15); item = old bits, new_item = new bits */
    MP_ACT_SHOP,    /* the shop's goods: ux = slot (39 = the rare item), uz = 0; item = what it was, new_item = now */
    MP_ACT_MUSEUM,  /* the museum (host -> visitors only): ux = byte of the exhibits (0-62), uz = 0; item/new_item */
    MP_ACT_ROOM,    /* a house (host -> visitors only): ux = ((home * floors + floor) * 2 + layer) * 2 + unit / 128,
                     * uz = unit % 128 */
    MP_ACT_SWITCH,  /* furniture switched on or off (TV, lamp...): ux, uz = its unit, item = which furniture,
                     * new_item = on | layer << 1 | (field & 63) << 2 | (scene & 255) << 8 (in that room only) */
    MP_ACT_OFTR0,   /* the town's outdoor furniture (host -> visitors only), layer 0, 1, 2: ux, uz = the unit (whole */
    MP_ACT_OFTR1,   /* town's), item/new_item = what was / is there */
    MP_ACT_OFTR2,
    MP_ACT_COUNT
};

typedef struct {
    uint8_t kind;
    uint8_t player;
    uint16_t ux, uz;
    uint16_t item;
    uint16_t new_item;
    uint32_t req;       /* request number, echoed */
    uint8_t ok;         /* WORLD: accepted */
} mp_action;

#define MP_NPC_MAX 15 /* ANIMAL_NUM_MAX */
#define MP_NPC_LOADED 0x01
#define MP_NPC_TALKING 0x02
#define MP_NPC_HOME 0x04 /* host -> visitor: in their house (the position is the house's) */
#define MP_NPC_AWAY 0x08 /* host -> visitor: out, where the host's town has them, nobody near (not loaded) */
#define MP_NPC_FLAGS_MASK 0x0F

typedef struct {
    uint8_t villager;   /* 0-14 */
    uint8_t name[8], town[8];
    uint16_t player_id, land_id;
    uint8_t time[6];    /* last talk: sec, min, hour, day, weekday, month */
    uint16_t year;
    uint8_t prev_town[8];
    uint64_t town_tune;
    int8_t friendship;
} mp_memory;

#define MP_BUG_MAX 8  /* aINS_ACTOR_NUM - 1: the insect setter's slots (the last is for bees and the like) */
#define MP_FISH_MAX 2 /* aGYO_MAX_GYOEI: a game has two fish at a time, near its player */
#define MP_FISH_HOOKED 0x01 /* visitor: going after (or on) their bobber: they move it */
#define MP_FISH_CAUGHT 0x02 /* visitor: they caught it */
#define MP_FISH_FLAGS_MASK 0x03

typedef struct {
    uint8_t count;
    struct {
        uint8_t slot;  /* the host's fish slot (0-1) */
        uint8_t type;  /* aGYO_TYPE_*, checked by the game */
        uint8_t flags; /* MP_FISH_* */
        float x, y, z;
        int16_t angle_y;
    } e[MP_BUG_MAX]; /* fish: up to MP_FISH_MAX; insects (MP_MSG_BUGS, the same layout): up to MP_BUG_MAX */
} mp_fish;

typedef struct {
    uint8_t count;
    struct {
        uint8_t idx; /* the town's villager 0-14 */
        uint8_t flags;
        float x, y, z;
        int16_t angle_y;
        uint8_t anim; /* animation (aNPC_ANIM_*), checked by the game */
    } e[MP_NPC_MAX];
} mp_npcs;

typedef struct {
    uint8_t type;
    uint8_t reliable;
    uint32_t seq; /* reliable only */
    union {
        mp_pstate pstate;
        mp_join join;
        mp_welcome welcome;
        mp_look look;
        mp_bulk bulk;
        mp_clock clock;
        mp_bye bye;
        mp_left left;
        mp_chat chat;
        mp_action action;
        mp_npcs npcs;
        mp_fish fish;
        mp_memory memory;
    } u;
} mp_msg;

typedef struct {
    uint32_t ack;   /* highest reliable seq received in order (0 = none yet) */
    uint64_t sack;  /* bit i: seq ack + 2 + i was received too (selective ack) */
    int count;
    mp_msg msg[MP_MAX_RECORDS];
} mp_frame;

/* bounded reader */
typedef struct {
    const uint8_t* p;
    size_t len, pos;
    int bad;
} mp_rd;

/* bounded writer */
typedef struct {
    uint8_t* p;
    size_t cap, pos;
    int bad;
} mp_wr;

void mp_rd_init(mp_rd* r, const void* p, size_t len);
uint8_t mp_rd_u8(mp_rd* r);
uint16_t mp_rd_u16(mp_rd* r);
uint32_t mp_rd_u32(mp_rd* r);
uint64_t mp_rd_u64(mp_rd* r);
float mp_rd_f32(mp_rd* r);
void mp_rd_bytes(mp_rd* r, void* out, size_t n);

void mp_wr_init(mp_wr* w, void* p, size_t cap);
void mp_wr_u8(mp_wr* w, uint8_t v);
void mp_wr_u16(mp_wr* w, uint16_t v);
void mp_wr_u32(mp_wr* w, uint32_t v);
void mp_wr_u64(mp_wr* w, uint64_t v);
void mp_wr_f32(mp_wr* w, float v);
void mp_wr_bytes(mp_wr* w, const void* p, size_t n);

/* Text: keeps only printable characters of the game's charset; anything else becomes a space. */
void mp_text_clean(uint8_t* text, size_t n);

/* Frames. decode returns 1 and fills *f, or 0 (and *f is unusable) if anything is wrong. */
int mp_wire_decode_frame(const uint8_t* data, size_t len, mp_frame* f);
/* encode_msg appends one message to a frame being built; returns 0 if it doesn't fit (w is then unchanged). */
void mp_wire_begin_frame(mp_wr* w, void* buf, size_t cap, uint32_t ack, uint64_t sack);
int mp_wire_encode_msg(mp_wr* w, const mp_msg* m);
/* size of a message on the wire */
size_t mp_wire_msg_size(const mp_msg* m);

#endif
