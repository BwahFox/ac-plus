/* pc_mp.c - online multiplayer, the game's side: runs the session (pc/src/mp), sends this player's state and look,
 * and keeps the other players' as checked values for their puppets (ac_mp_puppet.c).
 *
 * Every value from the network was already checked for its format by mp_wire.c; here it is checked again
 * against the game's own tables (animations, scenes, faces) before anything uses it. A visitor's slot always
 * comes from the host's own records, never from what the visitor says.
 *
 * Prototype controls (no menus yet): PC_MP_HOST=port (or 1 for the default port) hosts this town,
 * PC_MP_JOIN=address[:port] visits one. */
#include "pc_mp.h"
#include "pc_cf_anim.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "m_actor.h"
#include "m_common_data.h"
#include "m_land.h"
#include "m_name_table.h"
#include "m_demo.h"
#include "m_msg.h"
#include "ac_effectbg.h"
#include "ac_uki.h"
#include "m_play.h"
#include "m_player.h"
#include "m_player_lib.h"
#include "m_private.h"
#include "m_scene_table.h"
#include "pc_skin.h"
#include "pc_settings.h"
#include "pc_mp_town.h"
#include "pc_mp_world.h"
#include "pc_mp_fish.h"
#include "pc_outdoor_ftr.h"
#include "pc_mp_npc.h"
#include "m_scene.h"
#include "m_field_info.h"
#include "m_font.h"
#include "pc_menu_util.h"
#include "m_event.h"
#include "lb_rtc.h"

#include "mp/mp_link.h"
#include "mp/mp_mm.h"

extern unsigned int SDL_GetTicks(void);
extern void mPlib_pc_load_face(int sex, int face_type, int tone, int sunburn_rank, u8* face_tex, u16* face_pal);
extern u8* mPlib_get_player_tex_p(GAME* game);
extern u16* mPlib_get_player_pallet_p(GAME* game);
extern int pc_mcd_online_depart(GAME_PLAY* play, const Save_t* town);
extern int pc_mcd_online_return(void);
extern int pc_mcd_online_visiting(void);
extern void pc_os_mp_set_clock(int on, int year, int month, int day, int hour, int min, int sec);

#define MP_ID_FILE "save/multiplayer_id.bin"

static struct {
    int started;
    int env_done;
    int env_frames;
    int has_id;
    int close_link;
    int host;
    mp_link* link;
    mp_identity id;
    int my_slot;
    int peer_slot[MP_LINK_PEERS + 1]; /* host: which slot each link peer is, 0 = none yet */
    unsigned long long peer_since[MP_LINK_PEERS + 1]; /* host: when each peer connected (JOIN within 30 s) */
    int welcomed;                     /* visitor: the host accepted us */
    int my_perm;                      /* visitor: MP_PERM_* the host gave us */
    u32 tick;
    int look_sent;
    pc_mp_remote remote[PC_MP_SLOTS];
    mp_look looks[PC_MP_SLOTS]; /* host: each visitor's look as they sent it, to pass on */
    /* host: the town being sent to each peer, as it was when they joined */
    u8* town_out[MP_LINK_PEERS + 1];
    int town_out_len[MP_LINK_PEERS + 1];
    int town_sent[MP_LINK_PEERS + 1]; /* bytes queued so far, -1 = not sending */
    /* visitor: the town arriving */
    u8* town_in;
    u32 town_in_len, town_in_got;
    int town_ready; /* arrived whole: go there */
    int travelling;  /* 1 = on the way there, 2 = there, 3 = on the way home */
    int go_home;     /* the session ended while away */
    /* keeping the town the same (pc_mp_world.c) */
    int trusted[PC_MP_SLOTS]; /* host: may this visitor dig, chop, ... */
    f32 act_tokens[PC_MP_SLOTS];          /* host: each visitor's changes, rate-limited */
    unsigned long long act_at[PC_MP_SLOTS];
    u32 next_req;
    struct {
        u32 req;
        int kind;
        u16 a, b, old_value, new_value;
    } pending[64]; /* visitor: changes sent, not answered yet */
    mp_action early[512]; /* visitor: changes that came before we arrived */
    int early_n;
    /* matchmaking (mp_mm.c, the server's side in server/ac-plus-server.py) */
    int mm_on;            /* a server is in use for this session */
    mp_addr mm_server;
    char mm_code[8];      /* host: our invite code; visitor: the code we're looking up */
    unsigned long long mm_sent, mm_started;
    u32 mm_nonce;
    int mm_found;
    unsigned long long mm_found_at;
    u8 mm_key[32];        /* visitor: the host's key, as the server gave it */
    int mm_has_relay;     /* visitor: the server gave us a relay token for this visit */
    u8 mm_relay[MP_RELAY_TOKEN];
    int mm_relayed;       /* visitor: calling through the relay */
    int mm_find;          /* visitor: looking the town up by its host's key (a friend's town), not a code */
    u8 mm_find_key[32];
    u8 host_key[32];      /* visitor: the key of the host we're connected to */
    /* the town's time and weather: the host's */
    mp_clock clock;
    int has_clock;
    unsigned long long clock_sent;
    int oftr_frames; /* host: frames since the outdoor furniture was last looked at */
} l_mp;

pc_mp_remote* pc_mp_get_remote(int slot) {
    if (slot < 0 || slot >= PC_MP_SLOTS || slot == l_mp.my_slot || !l_mp.remote[slot].present) {
        return NULL;
    }
    return &l_mp.remote[slot];
}

int pc_mp_my_slot(void) {
    return l_mp.my_slot;
}

int pc_mp_active(void) {
    return l_mp.link != NULL;
}

static unsigned long long mp_now(void) {
    return SDL_GetTicks();
}

/* ms from t to now; 0 if t is later (set during this frame's poll, after the frame read `now`) */
static unsigned long long mp_since(unsigned long long now, unsigned long long t) {
    return now > t ? now - t : 0;
}

static int mp_load_identity(void) {
    FILE* f = fopen(MP_ID_FILE, "rb");
    if (f != NULL) {
        int ok = fread(&l_mp.id, 1, sizeof(l_mp.id), f) == sizeof(l_mp.id);
        fclose(f);
        if (ok) {
            return 1;
        }
    }
    if (!mp_identity_new(&l_mp.id)) {
        return 0;
    }
    f = fopen(MP_ID_FILE, "wb");
    if (f != NULL) {
        fwrite(&l_mp.id, 1, sizeof(l_mp.id), f);
        fclose(f);
    }
    return 1;
}

static void mp_drop_slot(int slot) {
    pc_mp_remote* r = &l_mp.remote[slot];
    if (r->actor != NULL) {
        Actor_delete(r->actor);
    }
    memset(r, 0, sizeof(*r));
}

/* --- matchmaking (invite codes) --- */

static void mp_status(const char* fmt, ...);
static void mp_visitor_quit(const char* why);
static void mp_world_effect(const mp_action* a);
static void mp_switch_apply(const mp_action* a);

/* testing (PC_MP_RELAY=1): a visitor goes through the relay without trying the host directly */
static int mp_force_relay(void) {
    const char* e = getenv("PC_MP_RELAY");
    return e != NULL && atoi(e) != 0;
}

static void mp_mm_raw(void* user, const mp_addr* from, const uint8_t* data, size_t len) {
    mp_mm_msg m;
    uint8_t out[MP_MM_REQ_SIZE];

    if (!l_mp.mm_on || from->ip != l_mp.mm_server.ip || from->port != l_mp.mm_server.port ||
        !mp_mm_parse(data, len, &m)) {
        return; /* only from our server, and only well-formed */
    }
    switch (m.type) {
        case MP_MM_CODE:
            if (l_mp.host && strcmp(l_mp.mm_code, m.code) != 0) {
                memcpy(l_mp.mm_code, m.code, sizeof(m.code));
                mp_status("Invite code: %s", l_mp.mm_code);
            }
            break;
        case MP_MM_ASK: {
            /* someone has our code: yes while there's room */
            int room = l_mp.host && pc_mp_visitors() < PC_MP_SLOTS - 1;
            if (l_mp.host) {
                mp_link_send_to(l_mp.link, &l_mp.mm_server, out, mp_mm_answer(out, m.nonce, room ? 1 : 2));
            }
            break;
        }
        case MP_MM_INTRO:
            if (l_mp.host) {
                /* a few packets to the visitor open our router for their hello */
                mp_addr v;
                uint8_t punch[8] = { MP_MM_PUNCH };
                int i;
                v.ip = m.ip, v.port = m.port;
                for (i = 0; i < 3; i++) {
                    mp_link_send_to(l_mp.link, &v, punch, sizeof(punch));
                }
            }
            break;
        case MP_MM_RELAY:
            if (l_mp.host) {
                /* in case the visitor can't reach us directly: they'll come from this route */
                mp_addr v;
                if (!mp_link_add_relay(l_mp.link, &l_mp.mm_server, m.token, mp_now(), &v)) {
                    printf("[mp] no free relay route\n");
                }
            } else if (!l_mp.mm_relayed && m.nonce == l_mp.mm_nonce) {
                memcpy(l_mp.mm_relay, m.token, sizeof(l_mp.mm_relay));
                l_mp.mm_has_relay = 1;
            }
            break;
        case MP_MM_FOUND:
            if (!l_mp.host && !l_mp.mm_found && m.nonce == l_mp.mm_nonce) {
                mp_addr h;
                if (l_mp.mm_find && memcmp(m.key, l_mp.mm_find_key, sizeof(l_mp.mm_find_key)) != 0) {
                    mp_status("The server sent another town: not going.");
                    l_mp.close_link = 1; /* we asked for this key; the handshake will only take this key anyway */
                    break;
                }
                h.ip = m.ip, h.port = m.port;
                l_mp.mm_found = 1;
                l_mp.mm_found_at = mp_now();
                memcpy(l_mp.mm_key, m.key, sizeof(l_mp.mm_key));
                mp_status("Found %.8s, calling...", (const char*)m.town);
                if (mp_force_relay()) {
                    break; /* testing: the relay only (mp_mm_tick) */
                }
                if (!mp_link_connect_addr(l_mp.link, &h, m.key, mp_now())) {
                    mp_status("Couldn't call that town.");
                }
            }
            break;
        case MP_MM_NONE:
            if (!l_mp.host && !l_mp.mm_found && m.nonce == l_mp.mm_nonce) {
                static const char* why[] = { "No town has that code.", "The host said no.", "Too many tries; wait a minute.",
                                             "That town is full." };
                mp_status("%s", m.why == 0 && l_mp.mm_find ? "That town isn't open right now." : m.why < 4 ? why[m.why] : why[0]);
                l_mp.close_link = 1;
            }
            break;
    }
}

/* every frame: keep the host's code alive, keep asking for a code until the server answers; a visitor that can't
 * get through to the host in a few seconds calls again through the server's relay, if it has one */
static void mp_mm_tick(unsigned long long now) {
    uint8_t out[MP_MM_REQ_SIZE];

    if (!l_mp.mm_on || l_mp.link == NULL) {
        return;
    }
    if (!l_mp.host && l_mp.mm_found && l_mp.mm_has_relay && !l_mp.mm_relayed && !mp_link_peer_live(l_mp.link, 0) &&
        (mp_force_relay() || mp_since(now, l_mp.mm_found_at) > 3000)) {
        mp_addr via;
        l_mp.mm_relayed = 1;
        if (mp_link_add_relay(l_mp.link, &l_mp.mm_server, l_mp.mm_relay, now, &via) &&
            mp_link_connect_addr(l_mp.link, &via, l_mp.mm_key, now)) {
            mp_status("Calling through %.40s...", g_pc_settings.mp_server);
        }
        return;
    }
    if (l_mp.host && (l_mp.mm_sent == 0 || now - l_mp.mm_sent >= (l_mp.mm_code[0] ? 15000u : 2000u))) {
        Private_c* priv = Now_Private;
        uint8_t name[8], town[8];
        memset(name, ' ', 8);
        memcpy(town, mLd_GetLandName(), 8);
        if (priv != NULL) {
            memcpy(name, priv->player_ID.player_name, 8);
        }
        l_mp.mm_sent = now;
        mp_link_send_to(l_mp.link, &l_mp.mm_server, out,
                        mp_mm_host(out, ++l_mp.mm_nonce, l_mp.id.pub, g_pc_settings.mp_public, town, name));
    } else if (!l_mp.host && !l_mp.mm_found && now - l_mp.mm_sent >= 1000) {
        uint8_t name[8];
        if (mp_since(now, l_mp.mm_started) > 12000) {
            mp_status("The server didn't answer.");
            l_mp.close_link = 1;
            return;
        }
        memset(name, ' ', 8);
        if (Now_Private != NULL) {
            memcpy(name, Now_Private->player_ID.player_name, 8);
        }
        l_mp.mm_sent = now;
        if (l_mp.mm_find) {
            mp_link_send_to(l_mp.link, &l_mp.mm_server, out,
                            mp_mm_find(out, l_mp.mm_nonce, l_mp.mm_find_key, l_mp.id.pub, name));
        } else {
            mp_link_send_to(l_mp.link, &l_mp.mm_server, out,
                            mp_mm_lookup(out, l_mp.mm_nonce, l_mp.mm_code, l_mp.id.pub, name));
        }
    }
}

static int mp_mm_begin(void) {
    l_mp.mm_on = 0;
    l_mp.mm_code[0] = '\0';
    l_mp.mm_sent = 0;
    l_mp.mm_found = 0;
    l_mp.mm_has_relay = 0;
    l_mp.mm_relayed = 0;
    l_mp.mm_find = 0;
    if (g_pc_settings.mp_server[0] == '\0') {
        return FALSE;
    }
    if (!mp_resolve(g_pc_settings.mp_server, MP_MM_DEFAULT_PORT, &l_mp.mm_server)) {
        mp_status("Couldn't find the server %.40s.", g_pc_settings.mp_server);
        return FALSE;
    }
    l_mp.mm_on = 1;
    mp_link_set_raw_handler(l_mp.link, mp_mm_raw, NULL);
    return TRUE;
}

/* --- the server's list of public towns (Online Play > Browse), on a socket of its own --- */

static struct {
    mp_link* link;
    mp_addr server;
    u32 nonce;
    unsigned long long sent, started;
    int got, count;
    struct {
        char code[MP_MM_CODE_LEN + 1];
        char town[MP_NAME_LEN + 1], name[MP_NAME_LEN + 1];
    } towns[MP_MM_PAGE];
} l_browse;

static void mp_name_text(const u8* in, char* out);

static void mp_browse_raw(void* user, const mp_addr* from, const uint8_t* data, size_t len) {
    mp_mm_msg m;
    int i;
    if (from->ip != l_browse.server.ip || from->port != l_browse.server.port || !mp_mm_parse(data, len, &m) ||
        m.type != MP_MM_TOWNS || m.nonce != l_browse.nonce) {
        return;
    }
    l_browse.count = m.count;
    for (i = 0; i < m.count; i++) {
        memcpy(l_browse.towns[i].code, m.towns[i].code, sizeof(l_browse.towns[i].code));
        mp_name_text(m.towns[i].town, l_browse.towns[i].town);
        mp_name_text(m.towns[i].name, l_browse.towns[i].name);
    }
    l_browse.got = 1;
}

void pc_mp_browse_stop(void) {
    if (l_browse.link != NULL) {
        mp_link_close(l_browse.link);
    }
    memset(&l_browse, 0, sizeof(l_browse));
}

int pc_mp_browse_start(void) {
    pc_mp_browse_stop();
    if (g_pc_settings.mp_server[0] == '\0') {
        mp_status("Browsing needs a server (mp_server in settings.ini).");
        return FALSE;
    }
    if (!l_mp.has_id && !(l_mp.has_id = mp_load_identity())) {
        return FALSE;
    }
    if (!mp_resolve(g_pc_settings.mp_server, MP_MM_DEFAULT_PORT, &l_browse.server)) {
        mp_status("Couldn't find the server %.40s.", g_pc_settings.mp_server);
        return FALSE;
    }
    l_browse.link = mp_link_open(&l_mp.id, 0, 0);
    if (l_browse.link == NULL) {
        return FALSE;
    }
    mp_link_set_raw_handler(l_browse.link, mp_browse_raw, NULL);
    l_browse.started = mp_now();
    l_browse.nonce = (u32)l_browse.started | 1;
    return TRUE;
}

/* -1 still asking, -2 no answer, else how many towns */
int pc_mp_browse_count(void) {
    if (l_browse.got) {
        return l_browse.count;
    }
    return l_browse.link != NULL ? -1 : -2;
}

int pc_mp_browse_town(int i, char* code, char* town, char* name) {
    if (!l_browse.got || i < 0 || i >= l_browse.count) {
        return FALSE;
    }
    memcpy(code, l_browse.towns[i].code, MP_MM_CODE_LEN + 1);
    memcpy(town, l_browse.towns[i].town, MP_NAME_LEN + 1);
    memcpy(name, l_browse.towns[i].name, MP_NAME_LEN + 1);
    return TRUE;
}

static void mp_browse_tick(unsigned long long now) {
    uint8_t out[MP_MM_LIST_SIZE];
    if (l_browse.link == NULL || l_browse.got) {
        return;
    }
    mp_link_poll(l_browse.link, now, NULL, NULL);
    if (l_browse.got) {
        mp_link_close(l_browse.link); /* the list is in */
        l_browse.link = NULL;
        return;
    }
    if (now - l_browse.started > 6000) {
        mp_link_close(l_browse.link); /* the server didn't answer: pc_mp_browse_count says so */
        l_browse.link = NULL;
        return;
    }
    if (l_browse.sent == 0 || now - l_browse.sent >= 1000) {
        l_browse.sent = now;
        mp_link_send_to(l_browse.link, &l_browse.server, out, mp_mm_list(out, l_browse.nonce, 0));
    }
}

const char* pc_mp_invite_code(void) {
    return l_mp.host && l_mp.mm_on ? l_mp.mm_code : "";
}

/* --- starting and stopping (pause menu > Online Play, pc_mp_menu.c) --- */

static char l_status[96];

static void mp_status(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(l_status, sizeof(l_status), fmt, ap);
    va_end(ap);
    printf("[mp] %s\n", l_status);
}

const char* pc_mp_status(void) {
    return l_status;
}

void pc_mp_fingerprint(char* out) {
    if (!l_mp.has_id) {
        l_mp.has_id = mp_load_identity();
    }
    if (l_mp.has_id) {
        mp_fingerprint(l_mp.id.pub, out);
    } else {
        out[0] = '\0';
    }
}

int pc_mp_hosting(void) {
    return l_mp.link != NULL && l_mp.host;
}

int pc_mp_visiting(void) {
    return !l_mp.host && (l_mp.link != NULL || l_mp.travelling != 0);
}

int pc_mp_visitors(void) {
    int i, n = 0;
    for (i = 1; i < PC_MP_SLOTS; i++) {
        n += l_mp.remote[i].present;
    }
    return l_mp.host ? n : 0;
}

/* forget the session; the visitor's trip home (travelling, go_home) is left as it is */
static void mp_forget(void) {
    int i;
    for (i = 0; i < PC_MP_SLOTS; i++) {
        mp_drop_slot(i);
    }
    memset(l_mp.peer_slot, 0, sizeof(l_mp.peer_slot));
    memset(l_mp.town_sent, 0xFF, sizeof(l_mp.town_sent));
    for (i = 0; i <= MP_LINK_PEERS; i++) {
        free(l_mp.town_out[i]);
        l_mp.town_out[i] = NULL;
    }
    pc_mp_npc_reset();
    pc_mp_fish_reset();
    pc_mp_bugs_reset();
    pc_mp_memory_reset();
    free(l_mp.town_in);
    l_mp.town_in = NULL;
    l_mp.town_ready = 0;
    l_mp.welcomed = 0;
    l_mp.has_clock = 0;
    l_mp.early_n = 0;
    l_mp.my_slot = 0;
    l_mp.my_perm = 0;
}

static int mp_at_home(void) {
    return Common_Get(player_no) < mPr_PLAYER_NUM && !pc_mcd_online_visiting() && mEv_IsNotTitleDemo();
}

int pc_mp_host_start(void) {
    char fp[24];
    int port = g_pc_settings.mp_port;

    if (l_mp.link != NULL || l_mp.travelling != 0) {
        return FALSE;
    }
    if (!mp_at_home()) {
        mp_status("You can only host your own town.");
        return FALSE;
    }
    pc_oftr_changes_reset();
    if (!l_mp.has_id && !(l_mp.has_id = mp_load_identity())) {
        mp_status("No random numbers from the system.");
        return FALSE;
    }
    l_mp.link = mp_link_open(&l_mp.id, 1, (unsigned short)port);
    if (l_mp.link == NULL) {
        mp_status("Couldn't open port %d (in use?).", port);
        return FALSE;
    }
    mp_forget();
    l_mp.host = 1;
    mp_fingerprint(l_mp.id.pub, fp);
    mp_status("Hosting on port %d.", port);
    if (mp_mm_begin()) {
        mp_status("Asking %.40s for an invite code...", g_pc_settings.mp_server);
    }
    printf("[mp] hosting on port %d, key %s\n", port, fp);
    return TRUE;
}

/* --- friends: towns visited before, found again through the server by their host's key --- */

#define MP_FRIENDS_FILE "save/multiplayer_friends.txt"
#define MP_FRIENDS_MAX 16

static struct {
    int loaded, count;
    struct {
        u8 key[32];
        char town[MP_NAME_LEN + 1], name[MP_NAME_LEN + 1];
    } f[MP_FRIENDS_MAX];
} l_friends;

static int hex_val(char c) {
    return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;
}

/* our own file, but read as carefully as anything else: "64 hex digits town|name", one town a line */
static void mp_friends_load(void) {
    FILE* f;
    char line[160];

    if (l_friends.loaded) {
        return;
    }
    l_friends.loaded = 1;
    l_friends.count = 0;
    f = fopen(MP_FRIENDS_FILE, "rb");
    if (f == NULL) {
        return;
    }
    while (l_friends.count < MP_FRIENDS_MAX && fgets(line, sizeof(line), f) != NULL) {
        int i, ok = 1;
        char* bar;
        char* names = line + 65;
        if (strlen(line) < 66 || line[64] != ' ') {
            continue;
        }
        for (i = 0; i < 32 && ok; i++) {
            int hi = hex_val(line[i * 2]), lo = hex_val(line[i * 2 + 1]);
            ok = hi >= 0 && lo >= 0;
            l_friends.f[l_friends.count].key[i] = (u8)(hi * 16 + lo);
        }
        names[strcspn(names, "\r\n")] = '\0';
        bar = strchr(names, '|');
        if (!ok || bar == NULL) {
            continue;
        }
        *bar = '\0';
        snprintf(l_friends.f[l_friends.count].town, sizeof(l_friends.f[0].town), "%.8s", names);
        snprintf(l_friends.f[l_friends.count].name, sizeof(l_friends.f[0].name), "%.8s", bar + 1);
        l_friends.count++;
    }
    fclose(f);
}

static void mp_friends_save(void) {
    FILE* f = fopen(MP_FRIENDS_FILE, "wb");
    int i, k;
    if (f == NULL) {
        return;
    }
    for (i = 0; i < l_friends.count; i++) {
        for (k = 0; k < 32; k++) {
            fprintf(f, "%02x", l_friends.f[i].key[k]);
        }
        fprintf(f, " %s|%s\n", l_friends.f[i].town, l_friends.f[i].name);
    }
    fclose(f);
}

/* after arriving: this town goes to the top of the list (names as text, '|' can't be in them) */
static void mp_friends_remember(const u8* key, const u8* town, const u8* name) {
    int i, at = -1;
    char t[MP_NAME_LEN + 1], n[MP_NAME_LEN + 1];

    mp_friends_load();
    mp_name_text(town, t);
    mp_name_text(name, n);
    for (i = 0; t[i]; i++) {
        t[i] = t[i] == '|' ? '?' : t[i];
    }
    for (i = 0; n[i]; i++) {
        n[i] = n[i] == '|' ? '?' : n[i];
    }
    for (i = 0; i < l_friends.count; i++) {
        if (memcmp(l_friends.f[i].key, key, 32) == 0) {
            at = i;
        }
    }
    if (at < 0) {
        at = l_friends.count < MP_FRIENDS_MAX ? l_friends.count++ : MP_FRIENDS_MAX - 1; /* the oldest goes */
    }
    memmove(&l_friends.f[1], &l_friends.f[0], sizeof(l_friends.f[0]) * at);
    memcpy(l_friends.f[0].key, key, 32);
    snprintf(l_friends.f[0].town, sizeof(l_friends.f[0].town), "%s", t);
    snprintf(l_friends.f[0].name, sizeof(l_friends.f[0].name), "%s", n);
    mp_friends_save();
}

int pc_mp_friend(int i, char* town, char* name) {
    mp_friends_load();
    if (i < 0 || i >= l_friends.count) {
        return FALSE;
    }
    memcpy(town, l_friends.f[i].town, MP_NAME_LEN + 1);
    memcpy(name, l_friends.f[i].name, MP_NAME_LEN + 1);
    return TRUE;
}

void pc_mp_forget_friend(int i) {
    mp_friends_load();
    if (i >= 0 && i < l_friends.count) {
        memmove(&l_friends.f[i], &l_friends.f[i + 1], sizeof(l_friends.f[0]) * (l_friends.count - i - 1));
        l_friends.count--;
        mp_friends_save();
    }
}

/* look friend i's town up on the server by its host's key, and visit it if it's open */
int pc_mp_visit_friend(int i) {
    mp_friends_load();
    if (i < 0 || i >= l_friends.count || l_mp.link != NULL || l_mp.travelling != 0) {
        return FALSE;
    }
    if (!mp_at_home()) {
        mp_status("Visit from your own town.");
        return FALSE;
    }
    if (!l_mp.has_id && !(l_mp.has_id = mp_load_identity())) {
        return FALSE;
    }
    l_mp.link = mp_link_open(&l_mp.id, 0, 0);
    if (l_mp.link == NULL || !mp_mm_begin()) {
        if (g_pc_settings.mp_server[0] == '\0') {
            mp_status("Friends' towns need a server (mp_server in settings.ini).");
        }
        if (l_mp.link != NULL) {
            mp_link_close(l_mp.link);
            l_mp.link = NULL;
        }
        return FALSE;
    }
    mp_forget();
    l_mp.mm_find = 1;
    memcpy(l_mp.mm_find_key, l_friends.f[i].key, 32);
    l_mp.host = 0;
    l_mp.my_slot = -1;
    l_mp.mm_started = mp_now();
    l_mp.mm_nonce++;
    mp_status("Looking for %s...", l_friends.f[i].town);
    return TRUE;
}

int pc_mp_visit_start(const char* address) {
    if (l_mp.link != NULL || l_mp.travelling != 0) {
        return FALSE;
    }
    if (!mp_at_home()) {
        mp_status("Visit from your own town.");
        return FALSE;
    }
    if (!l_mp.has_id && !(l_mp.has_id = mp_load_identity())) {
        mp_status("No random numbers from the system.");
        return FALSE;
    }
    l_mp.link = mp_link_open(&l_mp.id, 0, 0);
    if (l_mp.link != NULL && mp_mm_code_clean(address, l_mp.mm_code) && strchr(address, '.') == NULL) {
        char code[8];
        memcpy(code, l_mp.mm_code, sizeof(code));
        if (!mp_mm_begin()) {
            if (g_pc_settings.mp_server[0] == '\0') {
                mp_status("Invite codes need a server (mp_server in settings.ini).");
            }
            mp_link_close(l_mp.link);
            l_mp.link = NULL;
            return FALSE;
        }
        mp_forget();
        memcpy(l_mp.mm_code, code, sizeof(code));
        l_mp.host = 0;
        l_mp.my_slot = -1;
        l_mp.mm_started = mp_now();
        l_mp.mm_nonce++;
        if (strcmp(g_pc_settings.mp_last_visit, address) != 0) {
            snprintf(g_pc_settings.mp_last_visit, sizeof(g_pc_settings.mp_last_visit), "%s", address);
            pc_settings_save();
        }
        mp_status("Looking up %s...", code);
        return TRUE;
    }
    if (l_mp.link == NULL || !mp_link_connect(l_mp.link, address, mp_now())) {
        mp_status("Couldn't find %.40s.", address);
        if (l_mp.link != NULL) {
            mp_link_close(l_mp.link);
            l_mp.link = NULL;
        }
        return FALSE;
    }
    mp_forget();
    l_mp.host = 0;
    l_mp.my_slot = -1; /* until WELCOME */
    if (strcmp(g_pc_settings.mp_last_visit, address) != 0) {
        snprintf(g_pc_settings.mp_last_visit, sizeof(g_pc_settings.mp_last_visit), "%s", address);
        pc_settings_save();
    }
    mp_status("Calling %.40s...", address);
    return TRUE;
}

void pc_mp_stop(void) {
    if (l_mp.link != NULL && l_mp.host && l_mp.mm_on) {
        uint8_t out[MP_MM_REQ_SIZE];
        mp_link_send_to(l_mp.link, &l_mp.mm_server, out, mp_mm_close(out, ++l_mp.mm_nonce, l_mp.id.pub));
    }
    l_mp.mm_on = 0;
    if (l_mp.link != NULL) {
        mp_link_close(l_mp.link); /* says goodbye */
        l_mp.link = NULL;
    }
    if (!l_mp.host && l_mp.travelling == 2) {
        l_mp.go_home = 1;
        mp_status("Going home...");
    } else if (!l_mp.host && l_mp.travelling == 1) {
        l_mp.go_home = 1; /* as soon as we've arrived */
    } else {
        mp_status(l_mp.host ? "Stopped hosting." : "Left.");
    }
    mp_forget();
    l_mp.host = 0;
}

static void mp_start(void) {
    l_mp.started = 1;
    memset(l_mp.town_sent, 0xFF, sizeof(l_mp.town_sent));
    atexit(pc_mp_shutdown);
}

/* tests: PC_MP_HOST=port hosts, PC_MP_JOIN=address visits, once the player is out in their town (not the title
 * demo: its loading stalls the game long enough for a handshake to give up) */
static void mp_env_start(GAME* game) {
    const char* host = getenv("PC_MP_HOST");
    const char* join = getenv("PC_MP_JOIN");

    if (l_mp.env_done || ((GAME_PLAY*)game)->scene_id != SCENE_FG || !Common_Get(player_actor_exists) ||
        !mp_at_home()) {
        l_mp.env_frames = 0;
        return;
    }
    if (++l_mp.env_frames < 300) {
        return; /* and a few seconds of it running smoothly: the first moments in town still load things */
    }
    l_mp.env_done = 1;
    if (host != NULL && *host != '\0') {
        if (atoi(host) > 1 && atoi(host) <= 65535) {
            g_pc_settings.mp_port = atoi(host);
        }
        pc_mp_host_start();
    } else if (join != NULL && *join != '\0') {
        pc_mp_visit_start(join);
    }
}

void pc_mp_shutdown(void) {
    extern void pc_mcd_passport_write(void);
    pc_mcd_passport_write(); /* the window closed mid-visit: home from here next time */
    if (l_mp.link != NULL) {
        mp_link_close(l_mp.link);
        l_mp.link = NULL;
    }
}

/* --- looks --- */

static void mp_my_look(GAME* game, mp_look* l) {
    Private_c* priv = Now_Private;
    u8* tex = mPlib_get_player_tex_p(game);
    u16* pal = mPlib_get_player_pallet_p(game);

    memset(l, 0, sizeof(*l));
    l->slot = (uint8_t)(l_mp.my_slot < 0 ? 0 : l_mp.my_slot);
    memcpy(l->name, priv->player_ID.player_name, MP_NAME_LEN);
    memcpy(l->town, priv->player_ID.land_name, MP_NAME_LEN);
    l->gender = priv->gender == mPr_SEX_FEMALE;
    l->face = priv->face & 7;
    l->skin = (uint8_t)pc_skin_tone();
    l->tan = (uint8_t)pc_skin_sunburn_rank();
    l->cloth = priv->cloth.item;
    if (tex != NULL && pal != NULL) {
        memcpy(l->cloth_tex, tex, MP_DESIGN_BYTES);
        memcpy(l->cloth_pal, pal, sizeof(l->cloth_pal));
    }
}

static void mp_look_of(int slot, mp_look* l) {
    *l = l_mp.looks[slot];
    l->slot = (uint8_t)slot;
}

static void mp_apply_look(int slot, const mp_look* l) {
    pc_mp_remote* r = &l_mp.remote[slot];
    int tone = l->skin < PC_SKIN_TONE_NUM ? l->skin : PC_SKIN_TONE_DEFAULT;
    int tan = l->tan <= mPr_SUNBURN_RANK_MAX ? l->tan : 0;

    l_mp.looks[slot] = *l;
    r->gender = l->gender ? mPr_SEX_FEMALE : mPr_SEX_MALE;
    memcpy(r->name, l->name, sizeof(r->name));
    mPlib_pc_load_face(r->gender, l->face, tone, tan, r->face_tex, r->face_pal);
    memcpy(r->cloth_tex, l->cloth_tex, sizeof(r->cloth_tex));
    memcpy(r->cloth_pal, l->cloth_pal, sizeof(r->cloth_pal));
    r->has_look = 1;
}

static mp_look l_look_sent; /* our look as the others last had it */

static void mp_send_look(GAME* game, int peer) {
    mp_msg m;
    memset(&m, 0, sizeof(m));
    m.type = MP_MSG_LOOK;
    mp_my_look(game, &m.u.look);
    l_look_sent = m.u.look;
    mp_link_send(l_mp.link, peer, &m);
}

/* a change of clothes (or anything else in the look) goes to the others: a visitor's to the host, who passes it on */
static void mp_look_changes(GAME* game) {
    static int frames;
    mp_msg m;
    int k;

    if (++frames < 30) {
        return;
    }
    frames = 0;
    memset(&m, 0, sizeof(m));
    m.type = MP_MSG_LOOK;
    mp_my_look(game, &m.u.look);
    if (memcmp(&m.u.look, &l_look_sent, sizeof(mp_look)) == 0) {
        return;
    }
    l_look_sent = m.u.look;
    if (!l_mp.host) {
        mp_link_send(l_mp.link, 0, &m);
        return;
    }
    for (k = 0; k <= MP_LINK_PEERS; k++) {
        if (l_mp.peer_slot[k] != 0) {
            mp_link_send(l_mp.link, k, &m);
        }
    }
}

/* --- states --- */

/* which tool an item skeleton or animation belongs to (m_player.h's item data, in groups), -1 a plain model */
static int mp_item_group(int idx) {
    if (idx >= mPlayer_ITEM_DATA_NET && idx <= mPlayer_ITEM_DATA_YATTA_M) {
        return 0;
    }
    if (idx >= mPlayer_ITEM_DATA_ROD && idx <= mPlayer_ITEM_DATA_ROD_NOT_SWING) {
        return 1;
    }
    if (idx >= mPlayer_ITEM_DATA_BALLOON1 && idx <= mPlayer_ITEM_DATA_BALLOON_GYAZA) {
        return 2;
    }
    if (idx >= mPlayer_ITEM_DATA_PINWHEEL1 && idx <= mPlayer_ITEM_DATA_PINWHEEL_WAIT) {
        return 3;
    }
    return -1;
}

/* a player animation the game has: its own, or ac-plus's City Folk ones (Brewster's coffee; pc_cf_anim.h) */
static int mp_anim_ok(int idx) {
    return (idx >= 0 && idx < mPlayer_ANIM_NUM) || (idx >= PC_CF_PANIM_FIRST && idx < PC_CF_PANIM_FIRST + PC_CF_PANIM_NUM);
}

static void mp_apply_state(int slot, const mp_pstate* s) {
    pc_mp_remote* r = &l_mp.remote[slot];

    if (s->scene >= SCENE_NUM || !mp_anim_ok(s->anim0) || !mp_anim_ok(s->anim1) ||
        s->part_table >= mPlayer_PART_TABLE_NUM || s->eye >= mPlayer_EYE_TEX_NUM || s->mouth >= mPlayer_MOUTH_TEX_NUM) {
        return;
    }
    /* a tool that isn't one the player can hold is just not shown */
    r->item_main = s->item_main < mPlayer_ITEM_MAIN_NUM ? s->item_main : mPlayer_ITEM_MAIN_NONE;
    r->item_shape = -1;
    r->item_anim = -1;
    if (s->item_shape < mPlayer_ITEM_DATA_NUM) {
        int t = mPlib_Get_Item_DataPointerType(s->item_shape);
        if (t == mPlayer_ITEM_DATA_TYPE_GFX || t == mPlayer_ITEM_DATA_TYPE_SKELETON) {
            r->item_shape = s->item_shape;
        }
    }
    if (s->item_anim < mPlayer_ITEM_DATA_NUM && r->item_shape >= 0 && mp_item_group(s->item_anim) >= 0 &&
        mp_item_group(s->item_anim) == mp_item_group(r->item_shape)) {
        int t = mPlib_Get_Item_DataPointerType(s->item_anim);
        if (t != mPlayer_ITEM_DATA_TYPE_GFX && t != mPlayer_ITEM_DATA_TYPE_SKELETON) {
            r->item_anim = s->item_anim; /* only one of its own skeleton's animations: their joints match */
        }
    }
    r->umbrella = s->umbrella >= 1 && s->umbrella <= mPlayer_ITEM_KIND_ORG_UMBRELLA07 - mPlayer_ITEM_KIND_UMBRELLA00 + 1
                      ? s->umbrella - 1
                      : -1;
    r->item_frame = s->item_frame;
    if (getenv("PC_MP_DEBUG") != NULL && s->catch_item != (uint16_t)r->catch_item) {
        printf("[mpdbg] slot %d holds up %04X\n", slot, s->catch_item);
    }
    r->catch_item = s->catch_item >= ITM_FISH_START && s->catch_item < ITM_FISH_END ? s->catch_item : 0;
    if (r->has_state && (s32)(s->tick - r->tick) <= 0) {
        return; /* older than what we have */
    }
    r->scene = s->scene;
    r->field = s->field;
    r->visible = (s->flags & MP_PS_VISIBLE) != 0;
    r->x = s->x, r->y = s->y, r->z = s->z;
    r->angle_y = s->angle_y;
    r->anim0 = s->anim0, r->anim1 = s->anim1;
    r->frame0 = s->frame0, r->frame1 = s->frame1;
    r->speed = s->speed;
    r->part_table = s->part_table;
    r->eye = s->eye, r->mouth = s->mouth;
    r->tick = s->tick;
    r->has_state = 1;
}

/* the tool in the player's hand, as Player_actor_Item_draw shows it */
static void mp_my_item(PLAYER_ACTOR* p, mp_pstate* s) {
    int bank = p->item_bank_idx & 1;
    s->item_main = mPlayer_ITEM_MAIN_NONE;
    s->item_shape = s->item_anim = 0xFF;
    s->umbrella = 0;
    switch (p->now_main_index) {
        case mPlayer_INDEX_HOLD:
        case mPlayer_INDEX_PUSH:
        case mPlayer_INDEX_PULL:
        case mPlayer_INDEX_ROTATE_FURNITURE:
        case mPlayer_INDEX_OPEN_FURNITURE:
        case mPlayer_INDEX_CLOSE_FURNITURE:
        case mPlayer_INDEX_PICKUP_FURNITURE:
            return; /* put away while handling furniture */
    }
    if (!mPlayer_ITEM_MAIN_VALID(p->now_item_main_index)) {
        return;
    }
    s->item_main = (uint8_t)p->now_item_main_index;
    if (p->now_item_main_index == mPlayer_ITEM_MAIN_UMBRELLA_NORMAL) {
        if (mPlayer_ITEM_IS_UMBRELLA(p->item_kind) && p->umbrella_actor != NULL) {
            s->umbrella = (uint8_t)(p->item_kind - mPlayer_ITEM_KIND_UMBRELLA00 + 1);
        }
        return;
    }
    if (mPlayer_ITEM_DATA_VALID(p->item_shape_type[bank])) {
        s->item_shape = (uint8_t)p->item_shape_type[bank];
    }
    if (mPlayer_ITEM_DATA_VALID(p->item_animation_idx[bank])) {
        s->item_anim = (uint8_t)p->item_animation_idx[bank];
        s->item_frame = p->item_keyframe.frame_control.current_frame;
        if (!(s->item_frame >= 0.0f && s->item_frame <= 10000.0f)) {
            s->item_frame = 0.0f;
        }
    }
}

int pc_mp_test_catch, pc_mp_test_catch_frames; /* ctl showfish */

static int mp_my_state(GAME* game, mp_pstate* s) {
    GAME_PLAY* play = (GAME_PLAY*)game;
    PLAYER_ACTOR* p;

    memset(s, 0, sizeof(*s));
    s->player = (uint8_t)(l_mp.my_slot < 0 ? 0 : l_mp.my_slot);
    s->scene = (uint16_t)play->scene_id;
    s->field = (uint16_t)mFI_GetFieldId();
    s->tick = ++l_mp.tick;
    if (!Common_Get(player_actor_exists) || (!l_mp.host && l_mp.travelling != 2)) {
        return 1; /* not visible: a visitor is only there once they've arrived */
    }
    p = GET_PLAYER_ACTOR_GAME(game);
    if (!mp_anim_ok(p->animation0_idx) || p->part_table_idx < 0) {
        return 1;
    }
    s->flags = MP_PS_VISIBLE;
    s->x = p->actor_class.world.position.x;
    s->y = p->actor_class.world.position.y;
    s->z = p->actor_class.world.position.z;
    s->angle_y = p->actor_class.shape_info.rotation.y;
    s->anim0 = (uint16_t)p->animation0_idx;
    s->anim1 = (uint16_t)(mp_anim_ok(p->animation1_idx) ? p->animation1_idx : p->animation0_idx);
    s->frame0 = p->keyframe0.frame_control.current_frame;
    s->frame1 = p->keyframe1.frame_control.current_frame;
    s->speed = p->keyframe0.frame_control.speed;
    s->part_table = (uint8_t)p->part_table_idx;
    mp_my_item(p, s);
    if (pc_mp_test_catch_frames > 0) {
        pc_mp_test_catch_frames--;
        s->catch_item = (uint16_t)pc_mp_test_catch;
    } else if (p->now_main_index == mPlayer_INDEX_NOTICE_ROD && p->fishing_rod_actor_p != NULL) {
        /* holding up a fish they caught: the others see it in their hands */
        UKI_ACTOR* uki = (UKI_ACTOR*)p->fishing_rod_actor_p;
        mActor_name_t fish = uki->get_fish_type_proc != NULL ? uki->get_fish_type_proc(&uki->actor_class) : EMPTY_NO;
        s->catch_item = fish >= ITM_FISH_START && fish < ITM_FISH_END ? fish : 0;
    }
    s->eye = (uint8_t)(p->eye_tex_idx >= 0 ? p->eye_tex_idx : 0);
    s->mouth = (uint8_t)(p->mouth_tex_idx >= 0 ? p->mouth_tex_idx : 0);
    if (s->speed < -4.0f || s->speed > 4.0f) {
        s->speed = 0.0f;
    }
    return 1;
}

/* --- the town kept the same (pc_mp_world.c) --- */

static void mp_send_world(int peer, const mp_action* a) {
    mp_msg m;
    memset(&m, 0, sizeof(m));
    m.type = MP_MSG_WORLD;
    m.u.action = *a;
    mp_link_send(l_mp.link, peer, &m);
}

static void mp_world_to_all(const mp_action* a, int except_peer) {
    int i;
    for (i = 0; i <= MP_LINK_PEERS; i++) {
        if (l_mp.peer_slot[i] != 0 && i != except_peer) {
            mp_send_world(i, a);
        }
    }
}

/* host: a visitor's change, checked before it happens here */
#define ACT_PER_SEC 10.0f /* a visitor's changes: ten a second, bursts of twenty */
#define ACT_BURST 20.0f
#define ACT_REACH 200.0f /* how far from where they stand a change can be (five units: reach, a tree's fruit) */

/* could the visitor in slot have made this change, from where they are and this fast */
static int mp_action_plausible(int slot, const mp_action* in) {
    pc_mp_remote* r = &l_mp.remote[slot];
    unsigned long long now = mp_now();
    f32 t;

    t = l_mp.act_tokens[slot] + (now - l_mp.act_at[slot]) * (ACT_PER_SEC / 1000.0f);
    l_mp.act_tokens[slot] = t > ACT_BURST ? ACT_BURST : t;
    l_mp.act_at[slot] = now;
    if (l_mp.act_tokens[slot] < 1.0f) {
        return FALSE; /* too fast */
    }
    l_mp.act_tokens[slot] -= 1.0f;
    if (!r->has_state) {
        return FALSE;
    }
    if (in->kind == MP_WORLD_SHOP) {
        /* buying: in a shop */
        return r->scene == SCENE_SHOP0 || r->scene == SCENE_CONVENI || r->scene == SCENE_SUPER ||
               r->scene == SCENE_DEPART || r->scene == SCENE_DEPART_2;
    }
    if (in->kind == MP_WORLD_SET || in->kind == MP_WORLD_DEPOSIT) {
        /* out in town, next to the unit (a buried flag's row: the acre's units in it) */
        f32 ux, uz, dx, dz;
        if (r->scene != SCENE_FG) {
            return FALSE;
        }
        if (in->kind == MP_WORLD_SET) {
            ux = in->ux * 40.0f + 20.0f, uz = in->uz * 40.0f + 20.0f;
        } else {
            int bx = in->ux % FG_BLOCK_X_NUM + 1, bz = in->ux / FG_BLOCK_X_NUM + 1;
            int lo = 16, hi = -1, bit;
            for (bit = 0; bit < 16; bit++) {
                if (((in->item ^ in->new_item) >> bit) & 1) {
                    lo = bit < lo ? bit : lo, hi = bit;
                }
            }
            if (hi < 0) {
                return FALSE;
            }
            ux = (bx * 16 + (lo + hi) / 2) * 40.0f + 20.0f, uz = (bz * 16 + in->uz) * 40.0f + 20.0f;
            if (hi - lo > 2) {
                return FALSE; /* one dig, one unit */
            }
        }
        dx = ux - r->x, dz = uz - r->z;
        return dx * dx + dz * dz <= ACT_REACH * ACT_REACH;
    }
    return TRUE; /* the rest is refused for visitors anyway (museum, houses) */
}

static void mp_host_on_action(int peer, int slot, const mp_action* in) {
    mp_action out = *in;
    u16 here;
    int ok;

    out.player = (uint8_t)slot;
    if (in->kind == MP_WORLD_SWITCH) {
        if (getenv("PC_MP_DEBUG") != NULL) {
            printf("[mpdbg] switch from slot %d: %d where %04X (their scene %d)\n", slot, in->item, in->new_item,
                   l_mp.remote[slot].scene);
        }
        /* a furniture switch: harmless, so anyone may; only checked for sense and rate, then passed on */
        if (in->item < FTR_NUM && (in->new_item >> 8) == (l_mp.remote[slot].scene & 255) &&
            mp_action_plausible(slot, in)) {
            out.ok = 1;
            mp_switch_apply(&out);
            mp_world_to_all(&out, peer);
        }
        return;
    }
    ok = pc_mp_world_get(in->kind, in->ux, in->uz, &here) && here == in->item && mp_action_plausible(slot, in) &&
         (l_mp.trusted[slot] ? pc_mp_world_trusted_may(in->kind, in->ux, in->uz, in->item, in->new_item)
                             : pc_mp_world_visitor_may(in->kind, in->ux, in->uz, in->item, in->new_item)) &&
         pc_mp_world_apply(in->kind, in->ux, in->uz, in->new_item);
    if (ok) {
        mp_world_effect(&out); /* (out.item: what was there) */
        pc_mp_world_get(in->kind, in->ux, in->uz, &here); /* now the new value, also in our copy */
        out.ok = 1;
        mp_send_world(peer, &out);
        mp_world_to_all(&out, peer);
    } else {
        out.ok = 0;
        out.new_item = here; /* what is really there: the visitor puts it back */
        if (!pc_mp_world_get(in->kind, in->ux, in->uz, &out.new_item)) {
            return;
        }
        mp_send_world(peer, &out);
        printf("[mp] slot %d may not change %d,%d from %04X to %04X\n", slot, in->ux, in->uz, in->item, in->new_item);
    }
}

static void mp_host_world_change(void* user, int kind, int a, int b, u16 old_value, u16 new_value) {
    mp_action w;
    memset(&w, 0, sizeof(w));
    w.kind = (uint8_t)kind;
    w.ux = (uint16_t)a, w.uz = (uint16_t)b;
    w.item = old_value, w.new_item = new_value;
    w.ok = 1;
    mp_world_to_all(&w, -1);
}

static void mp_visitor_world_change(void* user, int kind, int a, int b, u16 old_value, u16 new_value) {
    mp_msg m;
    int i;
    if (kind == MP_WORLD_SET && old_value >= HOLE_START && old_value <= HOLE_END && new_value == EMPTY_NO &&
        gamePT != NULL && GET_PLAYER_ACTOR((GAME_PLAY*)gamePT) != NULL) {
        /* the game tidying away a hole far from us (it does when an area loads): the host would refuse it, as we
         * aren't there, so it goes straight back */
        ACTOR* me = &GET_PLAYER_ACTOR((GAME_PLAY*)gamePT)->actor_class;
        f32 dx = a * 40.0f + 20.0f - me->world.position.x, dz = b * 40.0f + 20.0f - me->world.position.z;
        if (dx * dx + dz * dz > ACT_REACH * ACT_REACH) {
            pc_mp_world_apply(kind, a, b, old_value);
            return;
        }
    }
    memset(&m, 0, sizeof(m));
    m.type = MP_MSG_ACTION;
    m.u.action.kind = (uint8_t)kind;
    m.u.action.player = (uint8_t)l_mp.my_slot;
    m.u.action.ux = (uint16_t)a, m.u.action.uz = (uint16_t)b;
    m.u.action.item = old_value, m.u.action.new_item = new_value;
    m.u.action.req = ++l_mp.next_req;
    i = (int)(l_mp.next_req % 64);
    l_mp.pending[i].req = l_mp.next_req;
    l_mp.pending[i].kind = kind;
    l_mp.pending[i].a = (u16)a, l_mp.pending[i].b = (u16)b;
    l_mp.pending[i].old_value = old_value, l_mp.pending[i].new_value = new_value;
    mp_link_send(l_mp.link, 0, &m);
}

/* --- fish and insects (pc_mp_fish.c, pc_mp_bugs.c): the host's go to its visitors five times a second; a visitor
 * says which of them it's after, and what it caught --- */
static void mp_fish_send(void) {
    static int frames;
    mp_msg m;
    int k;
    if (++frames < 12) {
        return;
    }
    frames = 0;
    memset(&m, 0, sizeof(m));
    m.type = MP_MSG_FISH;
    if (l_mp.host) {
        mp_msg b;
        memset(&b, 0, sizeof(b));
        b.type = MP_MSG_BUGS;
        pc_mp_fish_collect_host(&m.u.fish);
        pc_mp_bugs_collect_host(&b.u.fish);
        for (k = 0; k <= MP_LINK_PEERS; k++) {
            if (l_mp.peer_slot[k] != 0) {
                mp_link_send(l_mp.link, k, &m);
                mp_link_send(l_mp.link, k, &b);
            }
        }
    } else {
        mp_msg mm;
        memset(&mm, 0, sizeof(mm));
        mm.type = MP_MSG_MEMORY;
        if (pc_mp_memory_visitor_tick(&mm.u.memory)) {
            mp_link_send(l_mp.link, 0, &mm); /* a villager's memory of us changed: for the host's save */
        }
        if (pc_mp_fish_collect_visitor(&m.u.fish)) {
            mp_link_send(l_mp.link, 0, &m);
        }
        memset(&m, 0, sizeof(m));
        m.type = MP_MSG_BUGS;
        if (pc_mp_bugs_collect_visitor(&m.u.fish)) {
            mp_link_send(l_mp.link, 0, &m);
        }
    }
}

/* --- the town's outdoor furniture: the host's changes go to its visitors (they don't place things in its town) --- */
static void mp_oftr_change(void* user, int ut, int layer, mActor_name_t old_item, mActor_name_t new_item) {
    mp_action w;
    (void)user;
    memset(&w, 0, sizeof(w));
    w.kind = (uint8_t)(MP_WORLD_OFTR0 + layer);
    w.ux = (uint16_t)(ut % PC_OFTR_UT_X_NUM), w.uz = (uint16_t)(ut / PC_OFTR_UT_X_NUM);
    w.item = old_item, w.new_item = new_item;
    w.ok = 1;
    mp_world_to_all(&w, -1);
}

static void mp_oftr_apply(const mp_action* a) {
    extern void pc_aMR_field_live(int ut, int layer, mActor_name_t old_item, mActor_name_t new_item);
    int ut = a->uz * PC_OFTR_UT_X_NUM + a->ux, layer = a->kind - MP_WORLD_OFTR0;
    if (getenv("PC_MP_DEBUG") != NULL) {
        printf("[mpdbg] outdoor %d,%d layer %d: %04X -> %04X\n", a->ux, a->uz, layer, a->item, a->new_item);
    }
    if (pc_oftr_visit_set(ut, layer, a->new_item)) {
        pc_aMR_field_live(ut, layer, a->item, a->new_item);
    }
}

/* --- furniture switches (TVs, lamps, gyroids...): not part of the town's data, only of the room someone is in.
 * Each game watches the switches of the room it's in; a change goes to the others, the host passes a visitor's on
 * (anyone may switch things: it changes nothing that matters), and a game in that same room sets it too. --- */
#define SWITCH_MAX 256

extern void pc_aMR_switch_each(void (*fn)(void* user, int idx, u16 name, int ut_x, int ut_z, int layer, int on),
                               void* user);
extern int pc_aMR_switch_set(u16 name, int ut_x, int ut_z, int layer, int on);

static struct {
    int scene, field, ready;
    s16 name[SWITCH_MAX]; /* -1: nothing seen there */
    u8 on[SWITCH_MAX];
} l_sw;

static u16 mp_switch_where(int on, int layer) {
    GAME_PLAY* play = (GAME_PLAY*)gamePT;
    return (u16)((on ? 1 : 0) | (layer & 1) << 1 | (mFI_GetFieldId() & 63) << 2 | (play->scene_id & 255) << 8);
}

static int mp_switch_here(u16 where) {
    GAME_PLAY* play = (GAME_PLAY*)gamePT;
    return play != NULL && (where >> 8) == (play->scene_id & 255) && ((where >> 2) & 63) == (mFI_GetFieldId() & 63);
}

static void mp_switch_send(int ut_x, int ut_z, u16 name, u16 where) {
    mp_action w;
    if (l_mp.host) {
        memset(&w, 0, sizeof(w));
        w.kind = MP_WORLD_SWITCH;
        w.ux = (uint16_t)ut_x, w.uz = (uint16_t)ut_z;
        w.item = name, w.new_item = where;
        w.ok = 1;
        mp_world_to_all(&w, -1);
    } else {
        mp_visitor_world_change(NULL, MP_WORLD_SWITCH, ut_x, ut_z, name, where);
    }
}

static void mp_switch_seen(void* user, int idx, u16 name, int ut_x, int ut_z, int layer, int on) {
    (void)user;
    if (idx >= SWITCH_MAX) {
        return;
    }
    if (l_sw.ready && l_sw.name[idx] == (s16)name && l_sw.on[idx] != on) {
        if (getenv("PC_MP_DEBUG") != NULL) {
            printf("[mpdbg] switch %d at %d,%d -> %d sent\n", name, ut_x, ut_z, on);
        }
        mp_switch_send(ut_x, ut_z, name, mp_switch_where(on, layer));
    }
    l_sw.name[idx] = (s16)name;
    l_sw.on[idx] = (u8)on;
}

/* every frame in the town: our switches that changed go to the others (a new room is only looked at first) */
static void mp_switch_watch(GAME_PLAY* play) {
    int field = mFI_GetFieldId();
    if (play->scene_id != l_sw.scene || field != l_sw.field) {
        l_sw.scene = play->scene_id, l_sw.field = field, l_sw.ready = 0;
        memset(l_sw.name, 0xFF, sizeof(l_sw.name));
    }
    pc_aMR_switch_each(mp_switch_seen, NULL);
    l_sw.ready = 1;
}

/* another game's switch: set it if we're in that room, and remember it so it isn't sent back */
static void mp_switch_apply(const mp_action* a) {
    int idx;
    if (!mp_switch_here(a->new_item)) {
        return;
    }
    idx = pc_aMR_switch_set(a->item, a->ux, a->uz, (a->new_item >> 1) & 1, a->new_item & 1);
    printf("[mp] furniture %d at %d,%d switched %s by slot %d%s\n", a->item, a->ux, a->uz, a->new_item & 1 ? "on" : "off",
           a->player, idx < 0 ? " (not here)" : "");
    if (idx >= 0 && idx < SWITCH_MAX) {
        l_sw.name[idx] = (s16)a->item;
        l_sw.on[idx] = (u8)(a->new_item & 1);
    }
}

/* someone else's change to the town, as it looks when they make it: a tree falling or shaking (the effect only
 * draws; the ground is already changed). Digging and filling holes aren't shown, as their effects change the
 * ground themselves. */
static void mp_world_effect(const mp_action* a) {
    extern void pc_mp_effect_bg_tree(GAME* game, mActor_name_t item, s16 bg_ef_type, int ut_x, int ut_z);
    GAME_PLAY* play = (GAME_PLAY*)gamePT;

    if (a->kind != MP_WORLD_SET || a->player == l_mp.my_slot || play == NULL || play->scene_id != SCENE_FG ||
        (l_mp.host ? 0 : l_mp.travelling != 2)) {
        return;
    }
    if (IS_ITEM_TREE_STUMP(a->new_item) && pc_mp_fg_natural_ok(a->item) && !IS_ITEM_TREE_STUMP(a->item) &&
        IS_ITEM_ALIVE_TREE(a->item)) {
        pc_mp_effect_bg_tree(gamePT, a->item, EffectBG_EFFECT_CUT_RIGHT, a->ux, a->uz);
    } else if (pc_mp_guest_may_shake(a->item) && a->new_item != a->item && IS_ITEM_ALIVE_TREE(a->new_item)) {
        pc_mp_effect_bg_tree(gamePT, a->new_item, EffectBG_EFFECT_SHAKE, a->ux, a->uz);
    }
}

static void mp_visitor_on_world(const mp_action* a) {
    if (a->kind >= MP_WORLD_OFTR0 && a->kind <= MP_WORLD_OFTR0 + 2) {
        mp_oftr_apply(a);
        return;
    }
    if (a->kind == MP_WORLD_SWITCH) {
        if (a->player != l_mp.my_slot) {
            mp_switch_apply(a);
        }
        return;
    }
    pc_mp_world_apply(a->kind, a->ux, a->uz, a->new_item);
    if (a->ok) {
        mp_world_effect(a);
    }
    if (!a->ok && a->req != 0) {
        int i = (int)(a->req % 64);
        printf("[mp] the host put %d,%d back to %04X\n", a->ux, a->uz, a->new_item);
        /* our pickup of something that wasn't ours to take: give it back */
        if (l_mp.pending[i].req == a->req && l_mp.pending[i].kind == MP_WORLD_SET &&
            l_mp.pending[i].new_value == EMPTY_NO && pc_mp_item_ok(l_mp.pending[i].old_value)) {
            pc_mp_world_undo_pickup(l_mp.pending[i].old_value);
        }
        /* our purchase of something someone else had just bought: item and bells back */
        if (l_mp.pending[i].req == a->req && l_mp.pending[i].kind == MP_WORLD_SHOP &&
            pc_mp_shop_sold(l_mp.pending[i].new_value)) {
            pc_mp_world_undo_purchase(l_mp.pending[i].old_value);
        }
        if (l_mp.pending[i].req == a->req) {
            l_mp.pending[i].req = 0; /* answered: a second "no" for it takes nothing more */
        }
    }
}

/* --- time and weather: the host's --- */

static void mp_host_clock(mp_clock* c) {
    lbRTC_time_c t;
    u8 w = Save_Get(weather);
    lbRTC_GetTime(&t);
    memset(c, 0, sizeof(*c));
    c->year = t.year, c->month = t.month, c->day = t.day;
    c->hour = t.hour, c->min = t.min, c->sec = t.sec, c->weekday = t.weekday;
    c->weather = w & 7;
    c->weather_intensity = (w >> 4) & 7;
}

static void mp_host_send_clock(int peer) {
    mp_msg m;
    memset(&m, 0, sizeof(m));
    m.type = MP_MSG_CLOCK;
    mp_host_clock(&m.u.clock);
    mp_link_send(l_mp.link, peer, &m);
}

/* visitor: in the host's town, its time and weather are ours */
static void mp_visitor_use_clock(void) {
    const mp_clock* c = &l_mp.clock;
    if (!l_mp.has_clock || (l_mp.travelling != 1 && l_mp.travelling != 2)) {
        return;
    }
    pc_os_mp_set_clock(1, c->year, c->month, c->day, c->hour, c->min, c->sec);
    if (l_mp.travelling == 2) {
        Save_Set(weather, (u8)((c->weather_intensity << 4) | c->weather));
    }
}

/* --- visitors joining --- */

static void mp_host_town_done(int peer) {
    free(l_mp.town_out[peer]);
    l_mp.town_out[peer] = NULL;
    l_mp.town_out_len[peer] = 0;
    l_mp.town_sent[peer] = -1;
}

/* the town as it is now, for peer to download; its size */
static int mp_host_town_pack(GAME_PLAY* play, int peer) {
    u8* buf = (u8*)malloc(MP_BULK_MAX);
    int len = buf != NULL ? pc_mp_town_pack(play, buf, MP_BULK_MAX) : 0;

    mp_host_town_done(peer);
    if (len > 0) {
        l_mp.town_out[peer] = (u8*)realloc(buf, (size_t)len);
        if (l_mp.town_out[peer] == NULL) {
            l_mp.town_out[peer] = buf;
        }
        l_mp.town_out_len[peer] = len;
        l_mp.town_sent[peer] = 0;
    } else {
        free(buf);
    }
    return len;
}

/* a new visitor sees the ones already here, and they see the new one */
static void mp_host_swap_looks(int peer, int slot) {
    int k;
    for (k = 0; k <= MP_LINK_PEERS; k++) {
        int other = l_mp.peer_slot[k];
        mp_msg m;
        if (k == peer || other == 0 || !l_mp.remote[other].has_look) {
            continue;
        }
        memset(&m, 0, sizeof(m));
        m.type = MP_MSG_LOOK;
        mp_look_of(other, &m.u.look);
        mp_link_send(l_mp.link, peer, &m);
        mp_look_of(slot, &m.u.look);
        mp_link_send(l_mp.link, k, &m);
    }
}

/* a name in the game's charset as text for the menu: letters, digits and most punctuation are the same */
static void mp_name_text(const u8* in, char* out) {
    int i, n = 0;
    for (i = 0; i < MP_NAME_LEN; i++) {
        out[i] = in[i] >= 0x20 && in[i] < 0x7F ? (char)in[i] : '?';
        if (in[i] != ' ') {
            n = i + 1;
        }
    }
    out[n] = '\0';
}

/* host: a visitor's connection ended (they left, timed out, or were sent home); the others hear about it */
static void mp_host_peer_gone(int peer, int reason) {
    int slot = l_mp.peer_slot[peer];
    mp_host_town_done(peer);
    printf("[mp] slot %d left (reason %d)\n", slot, reason);
    l_mp.peer_slot[peer] = 0;
    if (slot > 0) {
        mp_msg left;
        char name[MP_NAME_LEN + 1];
        int k;
        mp_name_text(l_mp.remote[slot].name, name);
        mp_status("%s went home.", name);
        mp_drop_slot(slot);
        pc_mp_npc_forget(slot);
        l_mp.trusted[slot] = 0;
        memset(&left, 0, sizeof(left));
        left.type = MP_MSG_LEFT;
        left.u.left.slot = (uint8_t)slot;
        for (k = 0; k <= MP_LINK_PEERS; k++) {
            if (l_mp.peer_slot[k] != 0) {
                mp_link_send(l_mp.link, k, &left);
            }
        }
    }
}

static int mp_peer_of(int slot) {
    int k;
    for (k = 0; slot > 0 && k <= MP_LINK_PEERS; k++) {
        if (l_mp.peer_slot[k] == slot) {
            return k;
        }
    }
    return -1;
}

int pc_mp_visitor(int slot, char* name, int* trusted) {
    if (!l_mp.host || l_mp.link == NULL || mp_peer_of(slot) < 0 || !l_mp.remote[slot].present) {
        return FALSE;
    }
    mp_name_text(l_mp.remote[slot].name, name);
    *trusted = l_mp.trusted[slot];
    return TRUE;
}

void pc_mp_set_trusted(int slot, int on) {
    int peer = mp_peer_of(slot);
    mp_msg w;
    if (!l_mp.host || l_mp.link == NULL || peer < 0) {
        return;
    }
    l_mp.trusted[slot] = on != 0;
    /* a WELCOME again tells them what they may do now */
    memset(&w, 0, sizeof(w));
    w.type = MP_MSG_WELCOME;
    w.u.welcome.slot = (uint8_t)slot;
    w.u.welcome.permissions = l_mp.trusted[slot] ? MP_PERM_TRUSTED : 0;
    memcpy(w.u.welcome.town, mLd_GetLandName(), MP_NAME_LEN);
    w.u.welcome.town_bytes = 0;
    mp_link_send(l_mp.link, peer, &w);
}

void pc_mp_send_home(int slot) {
    int peer = mp_peer_of(slot);
    if (!l_mp.host || l_mp.link == NULL || peer < 0) {
        return;
    }
    mp_link_kick(l_mp.link, peer, MP_BYE_KICKED);
    mp_host_peer_gone(peer, MP_BYE_KICKED);
}

/* --- Porter: his "travel?" question offers online visits. At home (no online session) the choices become "yes"
 * (the GameCube's card trip), "Online" and "no"; in a town visited online, "Go home" and "no" (the card trip
 * isn't for a visitor). What was chosen happens once the talk has ended. --- */
static int l_porter_mode;   /* 0: the GameCube's choices, 1: with "Online", 2: "Go home" */
int pc_porter_talk_act = -1; /* Porter's talk step (ac_station_clip.c_inc) */
static int l_porter_action; /* 1: open Online Play, 2: go home */

static void mp_choice_text(u8* out, const char* text) {
    int i;
    for (i = 0; i < 16; i++) {
        out[i] = *text != '\0' ? (u8)*text++ : ' ';
    }
}

int pc_mp_porter_choices(ACTOR* actor, int msg_no, u8 (*str)[16], int count) {
    (void)msg_no;
    l_porter_mode = 0;
    if (getenv("PC_MP_DEBUG") != NULL && actor != NULL) {
        printf("[mpdbg] choices: actor profile %d, step %d, %d choices\n", actor->id, pc_porter_talk_act, count);
    }
    if (actor == NULL || actor->id != mAc_PROFILE_NPC_STATION_MASTER || pc_porter_talk_act != 0 /* "travel?" */ ||
        count != 2) {
        return count;
    }
    if (l_mp.link != NULL && !l_mp.host && l_mp.travelling == 2) {
        mp_choice_text(str[0], "Go home");
        l_porter_mode = 2;
        return 2;
    }
    if (l_mp.link != NULL || l_mp.travelling != 0) {
        return count; /* hosting, or on the way: as on the GameCube */
    }
    memcpy(str[2], str[1], 16);
    mp_choice_text(str[1], "Online");
    l_porter_mode = 1;
    return 3;
}

/* ac_station_clip.c_inc: the choice made; -1 when it's the GameCube's to handle */
int pc_mp_porter_chosen(int chosen) {
    int mode = l_porter_mode;
    if (getenv("PC_MP_DEBUG") != NULL) {
        printf("[mpdbg] porter choice %d (mode %d)\n", chosen, mode);
    }
    l_porter_mode = 0;
    if (mode == 1) {
        if (chosen == 0) {
            return -1; /* the card trip */
        }
        l_porter_action = chosen == 1 ? 1 : 0;
        return chosen;
    }
    if (mode == 2) {
        l_porter_action = chosen == 0 ? 2 : 0;
        return chosen;
    }
    return -1;
}

/* every frame in play: Porter's choice, once his talk is over */
static void mp_porter_tick(GAME* game) {
    extern void pc_pause_menu_open_online(void);
    static int hidden_frames;
    (void)game;
    if (l_porter_action == 0) {
        return;
    }
    /* the talk is over once the window has been closed for a moment */
    hidden_frames = mMsg_CHECK_MAINHIDE() && mDemo_Get_talk_actor() == NULL ? hidden_frames + 1 : 0;
    if (hidden_frames < 10) {
        return;
    }
    if (l_porter_action == 1) {
        pc_pause_menu_open_online();
    } else if (l_porter_action == 2) {
        pc_mp_stop();
    }
    l_porter_action = 0;
    hidden_frames = 0;
}

/* m_player_lib.c: a seat another player is sitting on can't be sat on. The request gives where the player sits down
 * from: the sitting animation backs them about 35 onto the seat, so someone seated there is about 35 from it, and
 * someone on the next stool or sofa seat (40 along) about 53. */
int pc_mp_seat_taken(GAME* game, const xyz_t* pos) {
    GAME_PLAY* play = (GAME_PLAY*)game;
    int k;
    if (l_mp.link == NULL || pos == NULL) {
        return FALSE;
    }
    for (k = 0; k < PC_MP_SLOTS; k++) {
        pc_mp_remote* r = &l_mp.remote[k];
        f32 dx, dz;
        if (k == l_mp.my_slot || !r->present || !r->visible || r->scene != play->scene_id ||
            r->field != mFI_GetFieldId()) {
            continue;
        }
        if (r->anim0 != mPlayer_ANIM_SITDOWN1 && r->anim0 != mPlayer_ANIM_SITDOWN_WAIT1 && r->anim0 < PC_CF_PANIM_FIRST) {
            continue; /* not sitting */
        }
        dx = r->x - pos->x, dz = r->z - pos->z;
        if (dx * dx + dz * dz < 44.0f * 44.0f) {
            return TRUE;
        }
    }
    return FALSE;
}

/* m_demo.c: a villager someone else in the town is talking to can't be talked to (nor talk to us) */
int pc_mp_villager_busy(ACTOR* a) {
    int in_town = l_mp.link != NULL && (l_mp.host ? pc_mp_visitors() > 0 : l_mp.travelling == 2);
    return in_town && pc_mp_npc_busy(a, l_mp.host, mp_now());
}

int pc_mp_trusted(void) {
    int guest = l_mp.link != NULL && !l_mp.host && l_mp.travelling == 2; /* in someone else's town */
    return !guest || (l_mp.my_perm & MP_PERM_TRUSTED);
}

/* --- chat: a line said shows at the top of every player's screen for a while --- */

#define CHAT_LINES 4
#define CHAT_SHOW_MS 8000
#define CHAT_GAP_MS 500 /* a visitor's lines, at most two a second */

static struct {
    char text[MP_NAME_LEN + 2 + MP_CHAT_LEN + 1];
    unsigned long long at;
} l_chat[CHAT_LINES];
static unsigned long long l_chat_last[PC_MP_SLOTS];

static void mp_chat_show(const u8* name, const mp_chat* c) {
    char who[MP_NAME_LEN + 1];
    char text[MP_CHAT_LEN + 1];
    int i;

    mp_name_text(name, who);
    for (i = 0; i < c->len && i < MP_CHAT_LEN; i++) {
        text[i] = c->text[i] >= 0x20 && c->text[i] < 0x7F ? (char)c->text[i] : '?';
    }
    text[i] = '\0';
    memmove(&l_chat[1], &l_chat[0], sizeof(l_chat[0]) * (CHAT_LINES - 1));
    snprintf(l_chat[0].text, sizeof(l_chat[0].text), "%s: %s", who, text);
    l_chat[0].at = mp_now();
    printf("[mp] chat %s\n", l_chat[0].text);
}

static void mp_chat_to_all(const mp_msg* m, int except_peer) {
    int k;
    for (k = 0; k <= MP_LINK_PEERS; k++) {
        if (k != except_peer && l_mp.peer_slot[k] != 0) {
            mp_link_send(l_mp.link, k, m);
        }
    }
}

int pc_mp_say(const char* text) {
    mp_msg m;
    int i;

    if (l_mp.link == NULL || (!l_mp.host && !l_mp.welcomed) || Now_Private == NULL) {
        return FALSE;
    }
    memset(&m, 0, sizeof(m));
    m.type = MP_MSG_CHAT;
    m.u.chat.player = (uint8_t)(l_mp.host ? 0 : l_mp.my_slot);
    for (i = 0; text[i] != '\0' && i < MP_CHAT_LEN; i++) {
        m.u.chat.text[i] = (uint8_t)(text[i] >= 0x20 && text[i] < 0x7F ? text[i] : '?');
    }
    m.u.chat.len = (uint8_t)i;
    if (i == 0 || mp_now() - l_chat_last[m.u.chat.player] < CHAT_GAP_MS) {
        return FALSE; /* nothing to say, or faster than the host passes on */
    }
    l_chat_last[m.u.chat.player] = mp_now();
    if (l_mp.host) {
        mp_chat_to_all(&m, -1);
    } else {
        mp_link_send(l_mp.link, 0, &m);
    }
    mp_chat_show(Now_Private->player_ID.player_name, &m.u.chat);
    return TRUE;
}

/* graph.c, every frame: the lines said lately, newest at the top, fading out */
void pc_mp_draw(GAME* game) {
    unsigned long long now = mp_now();
    int i, any = 0;
    f32 y = 34.0f;

    if (game == NULL || game->graph == NULL) {
        return;
    }
    for (i = 0; i < CHAT_LINES; i++) {
        any |= l_chat[i].at != 0 && now - l_chat[i].at < CHAT_SHOW_MS;
    }
    if (!any) {
        return;
    }
    mFont_SetMatrix(game->graph, mFont_MODE_FONT);
    for (i = 0; i < CHAT_LINES; i++) {
        unsigned long long age = now - l_chat[i].at;
        int alpha;
        if (l_chat[i].at == 0 || age >= CHAT_SHOW_MS) {
            continue;
        }
        alpha = age < CHAT_SHOW_MS - 1000 ? 255 : (int)((CHAT_SHOW_MS - age) * 255 / 1000);
        pc_menu_draw_centered(game, l_chat[i].text, y, 255, 255, 220, alpha, 0.9f);
        y += 14.0f;
    }
    mFont_UnSetMatrix(game->graph, mFont_MODE_FONT);
}

/* --- events --- */

static GAME* l_event_game;

static void mp_on_event(void* user, const mp_event* ev) {
    GAME* game = l_event_game;
    int slot;

    if (l_mp.host) {
        slot = ev->peer >= 0 && ev->peer <= MP_LINK_PEERS ? l_mp.peer_slot[ev->peer] : 0;
        switch (ev->type) {
            case MP_EV_CONNECTED: {
                char fp[24];
                mp_fingerprint(ev->remote_key, fp);
                printf("[mp] visitor connected (peer %d, key %s)\n", ev->peer, fp);
                l_mp.peer_slot[ev->peer] = 0;
                l_mp.peer_since[ev->peer] = mp_now();
                break;
            }
            case MP_EV_GONE:
                mp_host_peer_gone(ev->peer, ev->reason);
                break;
            case MP_EV_MESSAGE: {
                const mp_msg* m = ev->msg;
                if (slot == 0) {
                    /* only JOIN until the visitor has a slot */
                    int s, free_slot = 0;
                    mp_msg w;
                    if (m->type != MP_MSG_JOIN) {
                        break;
                    }
                    if (m->u.join.version != MP_PROTO_VERSION) {
                        mp_link_kick(l_mp.link, ev->peer, MP_BYE_VERSION);
                        break;
                    }
                    for (s = 1; s < PC_MP_SLOTS && free_slot == 0; s++) {
                        if (!l_mp.remote[s].present) {
                            free_slot = s;
                        }
                    }
                    if (free_slot == 0) {
                        mp_link_kick(l_mp.link, ev->peer, MP_BYE_FULL);
                        break;
                    }
                    if (pc_mp_visitors() > 0) {
                        /* the others hear about what changed up to now; the town packed below has it too */
                        pc_mp_world_scan(mp_host_world_change, NULL);
                    } else {
                        pc_mp_world_reset(); /* changes from now on are sent as they happen */
                    }
                    l_mp.peer_slot[ev->peer] = free_slot;
                    memset(&l_mp.remote[free_slot], 0, sizeof(pc_mp_remote));
                    l_mp.remote[free_slot].present = 1;
                    mp_apply_look(free_slot, &m->u.join.look);
                    memset(&w, 0, sizeof(w));
                    w.type = MP_MSG_WELCOME;
                    w.u.welcome.slot = (uint8_t)free_slot;
                    memcpy(w.u.welcome.town, mLd_GetLandName(), MP_NAME_LEN);
                    w.u.welcome.town_bytes = (uint32_t)mp_host_town_pack((GAME_PLAY*)game, ev->peer);
                    l_mp.trusted[free_slot] = getenv("PC_MP_TRUST") != NULL && atoi(getenv("PC_MP_TRUST")) != 0;
                    l_mp.act_tokens[free_slot] = ACT_BURST;
                    l_mp.act_at[free_slot] = mp_now();
                    w.u.welcome.permissions = l_mp.trusted[free_slot] ? MP_PERM_TRUSTED : 0;
                    mp_link_send(l_mp.link, ev->peer, &w);
                    mp_send_look(game, ev->peer);
                    mp_host_send_clock(ev->peer);
                    mp_host_swap_looks(ev->peer, free_slot);
                    {
                        /* the villagers remember them (if they've been here before) */
                        mp_msg mm;
                        int v;
                        memset(&mm, 0, sizeof(mm));
                        mm.type = MP_MSG_MEMORY;
                        for (v = 0; v < MP_NPC_MAX; v++) {
                            if (pc_mp_memory_for(v, m->u.join.look.name, m->u.join.look.town, &mm.u.memory)) {
                                mp_link_send(l_mp.link, ev->peer, &mm);
                            }
                        }
                    }
                    printf("[mp] visitor joined as slot %d\n", free_slot);
                    {
                        char name[MP_NAME_LEN + 1];
                        mp_name_text(m->u.join.look.name, name);
                        mp_status("%s is visiting.", name);
                    }
                    break;
                }
                if (m->type == MP_MSG_PSTATE) {
                    mp_pstate s = m->u.pstate;
                    int k;
                    s.player = (uint8_t)slot; /* the host decides who this is */
                    mp_apply_state(slot, &s);
                    for (k = 0; k <= MP_LINK_PEERS; k++) {
                        if (k != ev->peer && l_mp.peer_slot[k] != 0) {
                            mp_link_set_state(l_mp.link, k, &s); /* and the other visitors see them */
                        }
                    }
                } else if (m->type == MP_MSG_LOOK) {
                    mp_msg l = *m;
                    int k;
                    l.u.look.slot = (uint8_t)slot;
                    mp_apply_look(slot, &l.u.look);
                    for (k = 0; k <= MP_LINK_PEERS; k++) {
                        if (k != ev->peer && l_mp.peer_slot[k] != 0) {
                            mp_link_send(l_mp.link, k, &l);
                        }
                    }
                } else if (m->type == MP_MSG_ACTION) {
                    mp_host_on_action(ev->peer, slot, &m->u.action);
                } else if (m->type == MP_MSG_NPCS) {
                    pc_mp_npc_received(slot, &m->u.npcs, mp_now());
                } else if (m->type == MP_MSG_FISH) {
                    pc_mp_fish_host_received(slot, &m->u.fish, mp_now());
                } else if (m->type == MP_MSG_BUGS) {
                    pc_mp_bugs_host_received(slot, &m->u.fish, mp_now());
                } else if (m->type == MP_MSG_MEMORY) {
                    pc_mp_memory_host_apply(&m->u.memory, l_mp.looks[slot].name, l_mp.looks[slot].town);
                } else if (m->type == MP_MSG_CHAT) {
                    mp_msg c = *m;
                    unsigned long long now = mp_now();
                    if (now - l_chat_last[slot] < CHAT_GAP_MS) {
                        break; /* too fast: dropped */
                    }
                    l_chat_last[slot] = now;
                    c.u.chat.player = (uint8_t)slot; /* the host decides who said it */
                    mp_chat_show(l_mp.remote[slot].name, &c.u.chat);
                    mp_chat_to_all(&c, ev->peer);
                }
                break;
            }
        }
        return;
    }

    /* visitor */
    switch (ev->type) {
        case MP_EV_CONNECTED: {
            char fp[24];
            mp_msg m;
            mp_fingerprint(ev->remote_key, fp);
            memcpy(l_mp.host_key, ev->remote_key, sizeof(l_mp.host_key));
            printf("[mp] connected to the host (key %s)%s\n", fp, l_mp.mm_relayed ? " through the relay" : "");
            mp_status("Connected%s (host key %s).", l_mp.mm_relayed ? " via the server" : "", fp);
            memset(&m, 0, sizeof(m));
            m.type = MP_MSG_JOIN;
            m.u.join.version = MP_PROTO_VERSION;
            mp_my_look(game, &m.u.join.look);
            l_look_sent = m.u.join.look;
            mp_link_send(l_mp.link, 0, &m);
            break;
        }
        case MP_EV_GONE: {
            static const char* why[MP_BYE_COUNT] = {
                "The host closed the town.", "The town is full.", "The host has another ac-plus version.",
                "The host said no.", "The host sent you home.", "Lost the connection.",
            };
            mp_status("%s", ev->reason >= 0 && ev->reason < MP_BYE_COUNT ? why[ev->reason] : why[MP_BYE_ERROR]);
            mp_forget();
            l_mp.mm_on = 0; /* the session is over: nothing more to look up or relay */
            if (l_mp.travelling != 0) {
                l_mp.go_home = 1;
            }
            l_mp.close_link = 1; /* after the poll that told us */
            break;
        }
        case MP_EV_MESSAGE: {
            const mp_msg* m = ev->msg;
            if (m->type == MP_MSG_WELCOME) {
                if (l_mp.welcomed && m->u.welcome.slot == l_mp.my_slot) {
                    l_mp.my_perm = m->u.welcome.permissions; /* the host changed what we may do */
                    mp_status(l_mp.my_perm & MP_PERM_TRUSTED ? "The host trusts you: dig, chop and pick flowers."
                                                             : "You are a guest again: no digging or chopping.");
                } else if (!l_mp.welcomed) {
                    l_mp.welcomed = 1;
                    l_mp.my_slot = m->u.welcome.slot;
                    l_mp.my_perm = m->u.welcome.permissions;
                    l_mp.remote[0].present = 1;
                    l_mp.town_in_len = m->u.welcome.town_bytes;
                    l_mp.town_in_got = 0;
                    free(l_mp.town_in);
                    l_mp.town_in = l_mp.town_in_len > 0 ? (u8*)malloc(l_mp.town_in_len) : NULL;
                    printf("[mp] welcomed as slot %d, town %u bytes\n", l_mp.my_slot, l_mp.town_in_len);
                    mp_status("Getting the town...");
                }
            } else if (!l_mp.welcomed) {
                break;
            } else if (m->type == MP_MSG_BULK) {
                const mp_bulk* b = &m->u.bulk;
                /* in order (reliable), each piece right after the last, all inside the size the host gave */
                if (l_mp.town_in == NULL || b->total != l_mp.town_in_len || b->offset != l_mp.town_in_got ||
                    b->len > l_mp.town_in_len - l_mp.town_in_got) {
                    printf("[mp] town data out of place: leaving\n");
                    mp_visitor_quit("The host's town didn't arrive right.");
                    break;
                }
                memcpy(l_mp.town_in + b->offset, b->data, b->len);
                l_mp.town_in_got += b->len;
                if (l_mp.town_in_got == l_mp.town_in_len) {
                    l_mp.town_ready = 1;
                    printf("[mp] town arrived\n");
                }
            } else if (m->type == MP_MSG_NPCS) {
                if (l_mp.travelling == 2) {
                    pc_mp_npc_received(0, &m->u.npcs, mp_now());
                }
            } else if (m->type == MP_MSG_FISH) {
                if (l_mp.travelling == 2) {
                    pc_mp_fish_visitor_received(&m->u.fish, mp_now());
                }
            } else if (m->type == MP_MSG_BUGS) {
                if (l_mp.travelling == 2) {
                    pc_mp_bugs_visitor_received(&m->u.fish, mp_now());
                }
            } else if (m->type == MP_MSG_MEMORY) {
                pc_mp_memory_visitor_received(&m->u.memory);
            } else if (m->type == MP_MSG_CHAT) {
                int from = m->u.chat.player;
                if (from != l_mp.my_slot && from < PC_MP_SLOTS && l_mp.remote[from].has_look) {
                    mp_chat_show(l_mp.remote[from].name, &m->u.chat);
                }
            } else if (m->type == MP_MSG_LEFT) {
                if (m->u.left.slot != l_mp.my_slot) {
                    mp_drop_slot(m->u.left.slot);
                }
            } else if (m->type == MP_MSG_CLOCK) {
                l_mp.clock = m->u.clock;
                l_mp.has_clock = 1;
                mp_visitor_use_clock();
            } else if (m->type == MP_MSG_WORLD) {
                if (l_mp.travelling == 2) {
                    mp_visitor_on_world(&m->u.action);
                } else if (l_mp.early_n < (int)(sizeof(l_mp.early) / sizeof(l_mp.early[0]))) {
                    l_mp.early[l_mp.early_n++] = m->u.action;
                }
            } else if (m->type == MP_MSG_LOOK) {
                if (m->u.look.slot != l_mp.my_slot) {
                    l_mp.remote[m->u.look.slot].present = 1;
                    mp_apply_look(m->u.look.slot, &m->u.look);
                }
            } else if (m->type == MP_MSG_PSTATE) {
                if (m->u.pstate.player != l_mp.my_slot) {
                    l_mp.remote[m->u.pstate.player].present = 1;
                    mp_apply_state(m->u.pstate.player, &m->u.pstate);
                }
            }
            break;
        }
    }
}

/* visitor: we end it (the link says no GONE for our own kick) */
static void mp_visitor_quit(const char* why) {
    mp_link_kick(l_mp.link, 0, MP_BYE_ERROR);
    mp_status("%s", why);
    mp_forget();
    l_mp.mm_on = 0;
    if (l_mp.travelling != 0) {
        l_mp.go_home = 1;
    }
    l_mp.close_link = 1;
}

/* --- the town --- */

static void mp_host_send_towns(void) {
    int i;
    for (i = 0; i <= MP_LINK_PEERS; i++) {
        while (l_mp.town_sent[i] >= 0 && l_mp.town_sent[i] < l_mp.town_out_len[i] && mp_link_queue_free(l_mp.link, i) > 8) {
            mp_msg m;
            int n = l_mp.town_out_len[i] - l_mp.town_sent[i];
            if (n > MP_BULK_CHUNK) {
                n = MP_BULK_CHUNK;
            }
            memset(&m, 0, sizeof(m));
            m.type = MP_MSG_BULK;
            m.u.bulk.stream = 1;
            m.u.bulk.total = (uint32_t)l_mp.town_out_len[i];
            m.u.bulk.offset = (uint32_t)l_mp.town_sent[i];
            m.u.bulk.len = (uint16_t)n;
            memcpy(m.u.bulk.data, l_mp.town_out[i] + l_mp.town_sent[i], (size_t)n);
            if (!mp_link_send(l_mp.link, i, &m)) {
                break;
            }
            l_mp.town_sent[i] += n;
        }
        if (l_mp.town_sent[i] >= 0 && l_mp.town_sent[i] == l_mp.town_out_len[i]) {
            mp_host_town_done(i); /* all queued: the link has its own copy */
        }
    }
}

/* the station of the town, where a train would have left the player (ac_npc_p_sel2_talk.c_inc) */
static void mp_goto_station(GAME_PLAY* play) {
    static Door_data_c station = {
        SCENE_FG, mSc_DIRECT_SOUTH, FALSE, 0, { 1979, 0, 840 }, EMPTY_NO, 0, { 0, 0, 0 },
    };
    goto_other_scene(play, &station, TRUE);
}

/* only from the town outdoors, with the player free to walk */
static int mp_can_travel(GAME_PLAY* play) {
    return play->scene_id == SCENE_FG && Common_Get(player_actor_exists) && play->fb_fade_type == 0 &&
           play->submenu.process_status == mSM_PROCESS_WAIT &&
           mPlib_get_player_actor_main_index((GAME*)play) <= mPlayer_INDEX_RUN;
}

static void mp_visitor_travel(GAME_PLAY* play) {
    if (l_mp.go_home && l_mp.travelling == 2 && mp_can_travel(play)) {
        l_mp.go_home = 0;
        printf("[mp] going home\n");
        fflush(stdout);
        pc_os_mp_set_clock(0, 0, 0, 0, 0, 0, 0);
        if (pc_mcd_online_return()) {
            printf("[mp] home loaded, to the station\n");
            fflush(stdout);
            l_mp.travelling = 3;
            mp_goto_station(play);
        }
        return;
    }
    if (l_mp.travelling == 1 && pc_mcd_online_visiting()) {
        int i;
        l_mp.travelling = 2;
        pc_mp_world_reset();
        pc_mp_npc_reset();
        for (i = 0; i < l_mp.early_n; i++) {
            mp_visitor_on_world(&l_mp.early[i]);
        }
        l_mp.early_n = 0;
        mp_status("Visiting %.8s.", (const char*)mLd_GetLandName());
        mp_friends_remember(l_mp.host_key, mLd_GetLandName(), l_mp.remote[0].name);
    } else if (l_mp.travelling == 3 && !pc_mcd_online_visiting() && Common_Get(player_no) < mPr_PLAYER_NUM) {
        l_mp.travelling = 0;
        mp_status("Back home.");
    }
    if (l_mp.town_ready && l_mp.travelling == 0 && mp_can_travel(play)) {
        Save_t* town = (Save_t*)malloc(sizeof(Save_t));
        l_mp.town_ready = 0;
        if (town == NULL) {
            return;
        }
        memcpy(town, Common_GetPointer(save.save), sizeof(Save_t));
        if (!pc_mp_town_unpack(l_mp.town_in, (int)l_mp.town_in_len, town)) {
            printf("[mp] the host's town didn't check out: leaving\n");
            mp_visitor_quit("The host's town didn't check out.");
        } else if (pc_mcd_online_depart(play, town)) {
            l_mp.travelling = 1;
            mp_visitor_use_clock();
            mp_goto_station(play);
        }
        free(town);
    }
}

/* --- per frame --- */

/* m_play.c, right after the actors moved */
/* Outside the town's play (an NES game, the title screen's demo...) the game doesn't run pc_mp_play_tick, but the
 * connection has to go on or the other side gives up on us. Then the link is polled anyway, and what arrives is held
 * here and handled once the town is back: changes to the town, chat and so on, in order. Positions, fish, insects
 * and the clock are left out (fresh ones follow). Too much to hold ends a visit, as the town would no longer be the host's. */
#define HELD_MAX 1024

typedef struct {
    int type, peer, reason;
    uint8_t key[32];
    mp_msg msg;
} mp_held;

static mp_held* l_held;
static int l_held_n;
static int l_held_overflow;
static int l_play_ticked;

static void mp_hold_event(void* user, const mp_event* ev) {
    mp_held* h;
    (void)user;
    if (ev->type == MP_EV_MESSAGE && (ev->msg->type == MP_MSG_PSTATE || ev->msg->type == MP_MSG_NPCS ||
                                      ev->msg->type == MP_MSG_FISH || ev->msg->type == MP_MSG_BUGS ||
                                      ev->msg->type == MP_MSG_CLOCK)) {
        return; /* where things are now: fresh ones follow (the clock every five seconds) */
    }
    if (l_held == NULL) {
        l_held = (mp_held*)malloc(sizeof(mp_held) * HELD_MAX);
    }
    if (l_held == NULL || l_held_n >= HELD_MAX) {
        l_held_overflow = 1;
        return;
    }
    h = &l_held[l_held_n++];
    memset(h, 0, sizeof(*h));
    h->type = ev->type;
    h->peer = ev->peer;
    h->reason = ev->reason;
    if (ev->remote_key != NULL) {
        memcpy(h->key, ev->remote_key, sizeof(h->key));
    }
    if (ev->msg != NULL) {
        h->msg = *ev->msg;
    }
}

/* what arrived outside the town, now handled (pc_mp_play_tick) */
static void mp_handle_held(void) {
    int i;
    for (i = 0; i < l_held_n; i++) {
        mp_event ev;
        ev.type = l_held[i].type;
        ev.peer = l_held[i].peer;
        ev.reason = l_held[i].reason;
        ev.remote_key = l_held[i].key;
        ev.msg = &l_held[i].msg;
        mp_on_event(NULL, &ev);
        if (l_mp.link == NULL) {
            break;
        }
    }
    l_held_n = 0;
}

/* graph.c, after every frame's game: keep the connection going when the town's play didn't run this frame */
void pc_mp_frame_end(GAME* game) {
    (void)game;
    if (!l_play_ticked && l_mp.link != NULL) {
        unsigned long long now = mp_now();
        mp_link_poll(l_mp.link, now, mp_hold_event, NULL);
        if (l_mp.link != NULL) {
            mp_link_flush(l_mp.link, now); /* acks, and our last position again: we're still here */
        }
    }
    l_play_ticked = 0;
}

void pc_mp_after_actors(GAME* game) {
    if (l_mp.link == NULL) {
        return;
    }
    if ((l_mp.host && pc_mp_visitors() > 0) || (!l_mp.host && l_mp.travelling == 2)) {
        pc_mp_remote* h = &l_mp.remote[0];
        pc_mp_npc_apply((GAME_PLAY*)game, l_mp.host, mp_now());
        int host_out = !l_mp.host && h->present && h->visible && h->scene == SCENE_FG;
        pc_mp_fish_apply((GAME_PLAY*)game, l_mp.host, host_out, h->x, h->z, mp_now());
        pc_mp_bugs_apply((GAME_PLAY*)game, l_mp.host, host_out, h->x, h->z, mp_now());
    }
}

static void mp_spawn_puppets(GAME_PLAY* play) {
    int slot;
    if (!l_mp.host && l_mp.travelling != 2) {
        return; /* not in the host's town (yet) */
    }
    for (slot = 0; slot < PC_MP_SLOTS; slot++) {
        pc_mp_remote* r = pc_mp_get_remote(slot);
        if (r == NULL || r->actor != NULL || !r->has_state || !r->has_look || !r->visible ||
            r->scene != play->scene_id || r->field != mFI_GetFieldId()) {
            continue;
        }
        r->actor = Actor_info_make_actor(&play->actor_info, (GAME*)play, mAc_PROFILE_MP_PUPPET, r->x, r->y, r->z, 0,
                                         r->angle_y, 0, -1, -1, -1, EMPTY_NO, (s16)slot, -1, -1);
        if (r->actor != NULL) {
            printf("[mp] puppet for slot %d in scene %d\n", slot, play->scene_id);
        }
    }
}

void pc_mp_play_tick(GAME* game) {
    unsigned long long now;
    mp_pstate s;
    int i;

    mp_porter_tick(game);
    if (!l_mp.started) {
        mp_start();
    }
    mp_env_start(game);
    mp_browse_tick(mp_now());
    if (l_mp.link == NULL) {
        if (l_mp.travelling != 0) {
            mp_visitor_travel((GAME_PLAY*)game); /* on the way home */
        }
        return;
    }
    now = mp_now();
    l_play_ticked = 1;
    l_event_game = game;
    if (l_held_overflow) {
        l_held_overflow = 0;
        l_held_n = 0;
        if (!l_mp.host) {
            mp_visitor_quit("Too much happened in the town while you were away from it.");
            return;
        }
        {
            /* host: visitors' requests were lost, so their towns may no longer match ours */
            int k;
            for (k = 1; k < PC_MP_SLOTS; k++) {
                pc_mp_send_home(k);
            }
        }
    }
    mp_handle_held();
    if (l_mp.link == NULL) {
        return;
    }
    if (!l_mp.host && l_mp.travelling == 2) {
        /* the player as they are now, for the next start if this visit ends without the trip home (pc_m_card.c) */
        static unsigned long long passport_at;
        extern void pc_mcd_passport_write(void);
        if (mp_since(now, passport_at) > 5000) {
            passport_at = now;
            pc_mcd_passport_write();
        }
    }
    l_event_game = game;
    mp_link_poll(l_mp.link, now, mp_on_event, NULL);
    mp_mm_tick(now);
    if (l_mp.close_link) {
        l_mp.close_link = 0;
        mp_link_close(l_mp.link);
        l_mp.link = NULL;
        if (l_mp.travelling != 0) {
            mp_visitor_travel((GAME_PLAY*)game);
        }
        return;
    }

    if (mp_my_state(game, &s)) {
        mp_npcs npcs;
        int in_town = l_mp.host || l_mp.travelling == 2;
        if (l_mp.host && ++l_mp.oftr_frames >= 15) {
            /* (kept up from the start: a visitor's copy is the town as it was when they joined) */
            l_mp.oftr_frames = 0;
            pc_oftr_changes(mp_oftr_change, NULL);
        }
        if (in_town && (!l_mp.host || pc_mp_visitors() > 0)) {
            mp_look_changes(game);
            mp_switch_watch((GAME_PLAY*)game);
            mp_fish_send();
        }
        if (in_town) {
            static unsigned long long last_log;
            pc_mp_npc_collect((GAME_PLAY*)game, &npcs);
            if (getenv("PC_MP_DEBUG") != NULL && now - last_log > 3000) {
                int k;
                last_log = now;
                for (k = 0; k < PC_MP_SLOTS; k++) {
                    pc_mp_remote* r = pc_mp_get_remote(k);
                    if (r != NULL) {
                        printf("[mp] slot %d: scene %d field %d vis %d at %d,%d actor %p (here scene %d field %d)\n", k,
                               r->scene, r->field, r->visible, (int)r->x, (int)r->z, (void*)r->actor,
                               ((GAME_PLAY*)game)->scene_id, mFI_GetFieldId());
                    }
                }
                printf("[mp] villagers here:");
                for (k = 0; k < npcs.count; k++) {
                    printf(" %d@%d,%d/a%d%s", npcs.e[k].idx, (int)npcs.e[k].x, (int)npcs.e[k].z, npcs.e[k].anim,
                           (npcs.e[k].flags & MP_NPC_TALKING) ? "(talking)" : "");
                }
                printf("\n");
            }
        }
        for (i = 0; i <= MP_LINK_PEERS; i++) {
            if (l_mp.host ? l_mp.peer_slot[i] != 0 : (i == 0 && l_mp.welcomed)) {
                mp_link_set_state(l_mp.link, i, &s);
                if (l_mp.host) {
                    pc_mp_npc_collect_for((GAME_PLAY*)game, l_mp.peer_slot[i], now, &npcs);
                }
                if (in_town) {
                    mp_link_set_npcs(l_mp.link, i, &npcs);
                }
            }
        }
    }
    if (l_mp.host) {
        int any = 0;
        for (i = 0; i <= MP_LINK_PEERS; i++) {
            any |= l_mp.peer_slot[i] != 0;
        }
        mp_host_send_towns();
        for (i = 0; i <= MP_LINK_PEERS; i++) {
            /* connected but never said who they are: the slot goes to someone else */
            if (l_mp.peer_slot[i] == 0 && mp_link_peer_live(l_mp.link, i) && mp_since(now, l_mp.peer_since[i]) > 30000) {
                printf("[mp] peer %d never joined: dropped\n", i);
                mp_link_kick(l_mp.link, i, MP_BYE_ERROR);
            }
        }
        if (any && now - l_mp.clock_sent >= 5000) {
            l_mp.clock_sent = now;
            for (i = 0; i <= MP_LINK_PEERS; i++) {
                if (l_mp.peer_slot[i] != 0) {
                    mp_host_send_clock(i);
                }
            }
        }
        if (any) {
            pc_mp_world_scan(mp_host_world_change, NULL);
        }
    } else {
        mp_visitor_travel((GAME_PLAY*)game);
        if (l_mp.travelling == 2 && l_mp.welcomed) {
            pc_mp_world_scan(mp_visitor_world_change, NULL);
        }
    }
    mp_spawn_puppets((GAME_PLAY*)game);
    mp_link_flush(l_mp.link, now);
}
