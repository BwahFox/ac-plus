/* mp_wire.c - see mp_wire.h. Frame layout (all little-endian):
 *   u32 ack, u64 selective ack, u8 count, then count records of
 *   u8 type, u8 flags (bit 0 = reliable), [u32 seq if reliable], u16 body length, body.
 * A body's length must be exactly its type's size (BULK and CHAT: their header plus their own length). */
#include "mp_wire.h"

#include <math.h>
#include <string.h>

void mp_rd_init(mp_rd* r, const void* p, size_t len) {
    r->p = (const uint8_t*)p;
    r->len = len;
    r->pos = 0;
    r->bad = 0;
}

static int mp_rd_need(mp_rd* r, size_t n) {
    if (r->bad || n > r->len - r->pos) {
        r->bad = 1;
        return 0;
    }
    return 1;
}

uint8_t mp_rd_u8(mp_rd* r) {
    if (!mp_rd_need(r, 1)) {
        return 0;
    }
    return r->p[r->pos++];
}

uint16_t mp_rd_u16(mp_rd* r) {
    uint16_t v;
    if (!mp_rd_need(r, 2)) {
        return 0;
    }
    v = (uint16_t)(r->p[r->pos] | (r->p[r->pos + 1] << 8));
    r->pos += 2;
    return v;
}

uint32_t mp_rd_u32(mp_rd* r) {
    uint32_t v;
    if (!mp_rd_need(r, 4)) {
        return 0;
    }
    v = (uint32_t)r->p[r->pos] | ((uint32_t)r->p[r->pos + 1] << 8) | ((uint32_t)r->p[r->pos + 2] << 16) |
        ((uint32_t)r->p[r->pos + 3] << 24);
    r->pos += 4;
    return v;
}

uint64_t mp_rd_u64(mp_rd* r) {
    uint64_t lo = mp_rd_u32(r);
    uint64_t hi = mp_rd_u32(r);
    return lo | (hi << 32);
}

float mp_rd_f32(mp_rd* r) {
    uint32_t u = mp_rd_u32(r);
    float f;
    memcpy(&f, &u, 4);
    return f;
}

void mp_rd_bytes(mp_rd* r, void* out, size_t n) {
    if (!mp_rd_need(r, n)) {
        memset(out, 0, n);
        return;
    }
    memcpy(out, r->p + r->pos, n);
    r->pos += n;
}

void mp_wr_init(mp_wr* w, void* p, size_t cap) {
    w->p = (uint8_t*)p;
    w->cap = cap;
    w->pos = 0;
    w->bad = 0;
}

static int mp_wr_need(mp_wr* w, size_t n) {
    if (w->bad || n > w->cap - w->pos) {
        w->bad = 1;
        return 0;
    }
    return 1;
}

void mp_wr_u8(mp_wr* w, uint8_t v) {
    if (mp_wr_need(w, 1)) {
        w->p[w->pos++] = v;
    }
}

void mp_wr_u16(mp_wr* w, uint16_t v) {
    if (mp_wr_need(w, 2)) {
        w->p[w->pos++] = (uint8_t)v;
        w->p[w->pos++] = (uint8_t)(v >> 8);
    }
}

void mp_wr_u32(mp_wr* w, uint32_t v) {
    if (mp_wr_need(w, 4)) {
        int i;
        for (i = 0; i < 4; i++) {
            w->p[w->pos++] = (uint8_t)(v >> (8 * i));
        }
    }
}

void mp_wr_u64(mp_wr* w, uint64_t v) {
    mp_wr_u32(w, (uint32_t)v);
    mp_wr_u32(w, (uint32_t)(v >> 32));
}

void mp_wr_f32(mp_wr* w, float v) {
    uint32_t u;
    memcpy(&u, &v, 4);
    mp_wr_u32(w, u);
}

void mp_wr_bytes(mp_wr* w, const void* p, size_t n) {
    if (mp_wr_need(w, n)) {
        memcpy(w->p + w->pos, p, n);
        w->pos += n;
    }
}

/* the font's control code (m_font.h CHAR_CONTROL_CODE) and line break (CHAR_NEW_LINE); the codes below 32 are
 * letters in this charset (accented ones), not control characters */
void mp_text_clean(uint8_t* text, size_t n) {
    size_t i;
    for (i = 0; i < n; i++) {
        if (text[i] == 127 || text[i] == 205) {
            text[i] = ' ';
        }
    }
}

static int mp_pos_ok(float v) {
    return isfinite(v) && v > -MP_POS_LIMIT && v < MP_POS_LIMIT;
}

static int mp_frame_ok(float v) {
    return isfinite(v) && v >= -1.0f && v <= 10000.0f;
}

/* fixed body sizes; 0 = variable (checked by the decoder) */
#define MP_PSTATE_SIZE (1 + 2 + 2 + 1 + 12 + 2 + 4 + 8 + 4 + 3 + 4 + 4 + 2 + 4)
#define MP_LOOK_SIZE (1 + MP_NAME_LEN * 2 + 4 + 2 + MP_DESIGN_BYTES + 32)
#define MP_JOIN_SIZE (2 + 1 + MP_LOOK_SIZE)
#define MP_WELCOME_SIZE (1 + 1 + MP_NAME_LEN + 4)
#define MP_BULK_HEAD (1 + 4 + 4 + 2)
#define MP_CLOCK_SIZE (2 + 6 + 2)
#define MP_BYE_SIZE 1
#define MP_LEFT_SIZE 1
#define MP_CHAT_HEAD 2
#define MP_ACTION_SIZE (1 + 1 + 2 + 2 + 2 + 2 + 4 + 1)
#define MP_NPC_ENTRY (1 + 1 + 12 + 2 + 1)
#define MP_FISH_ENTRY (1 + 1 + 1 + 12 + 2)
#define MP_MEMORY_SIZE (1 + 8 + 8 + 2 + 2 + 6 + 2 + 8 + 8 + 1)

static void mp_put_look(mp_wr* w, const mp_look* l) {
    int i;
    mp_wr_u8(w, l->slot);
    mp_wr_bytes(w, l->name, MP_NAME_LEN);
    mp_wr_bytes(w, l->town, MP_NAME_LEN);
    mp_wr_u8(w, l->gender);
    mp_wr_u8(w, l->face);
    mp_wr_u8(w, l->skin);
    mp_wr_u8(w, l->tan);
    mp_wr_u16(w, l->cloth);
    mp_wr_bytes(w, l->cloth_tex, MP_DESIGN_BYTES);
    for (i = 0; i < 16; i++) {
        mp_wr_u16(w, l->cloth_pal[i]);
    }
}

static int mp_get_look(mp_rd* r, mp_look* l) {
    int i;
    l->slot = mp_rd_u8(r);
    mp_rd_bytes(r, l->name, MP_NAME_LEN);
    mp_rd_bytes(r, l->town, MP_NAME_LEN);
    mp_text_clean(l->name, MP_NAME_LEN);
    mp_text_clean(l->town, MP_NAME_LEN);
    l->gender = mp_rd_u8(r);
    l->face = mp_rd_u8(r);
    l->skin = mp_rd_u8(r);
    l->tan = mp_rd_u8(r);
    l->cloth = mp_rd_u16(r);
    mp_rd_bytes(r, l->cloth_tex, MP_DESIGN_BYTES);
    for (i = 0; i < 16; i++) {
        l->cloth_pal[i] = mp_rd_u16(r);
    }
    return l->slot < 4 && l->gender <= 1 && l->face < 8 && l->skin < 16 && l->tan < 16;
}

size_t mp_wire_msg_size(const mp_msg* m) {
    size_t head = 1 + 1 + (m->reliable ? 4 : 0) + 2;
    switch (m->type) {
        case MP_MSG_PSTATE:
            return head + MP_PSTATE_SIZE;
        case MP_MSG_JOIN:
            return head + MP_JOIN_SIZE;
        case MP_MSG_WELCOME:
            return head + MP_WELCOME_SIZE;
        case MP_MSG_LOOK:
            return head + MP_LOOK_SIZE;
        case MP_MSG_BULK:
            return head + MP_BULK_HEAD + (m->u.bulk.len <= MP_BULK_CHUNK ? m->u.bulk.len : MP_BULK_CHUNK);
        case MP_MSG_CLOCK:
            return head + MP_CLOCK_SIZE;
        case MP_MSG_BYE:
            return head + MP_BYE_SIZE;
        case MP_MSG_LEFT:
            return head + MP_LEFT_SIZE;
        case MP_MSG_CHAT:
            return head + MP_CHAT_HEAD + (m->u.chat.len <= MP_CHAT_LEN ? m->u.chat.len : MP_CHAT_LEN);
        case MP_MSG_ACTION:
        case MP_MSG_WORLD:
            return head + MP_ACTION_SIZE;
        case MP_MSG_NPCS:
            return head + 1 + MP_NPC_ENTRY * (m->u.npcs.count <= MP_NPC_MAX ? m->u.npcs.count : MP_NPC_MAX);
        case MP_MSG_FISH:
            return head + 1 + MP_FISH_ENTRY * (m->u.fish.count <= MP_FISH_MAX ? m->u.fish.count : MP_FISH_MAX);
        case MP_MSG_BUGS:
            return head + 1 + MP_FISH_ENTRY * (m->u.fish.count <= MP_BUG_MAX ? m->u.fish.count : MP_BUG_MAX);
        case MP_MSG_MEMORY:
            return head + MP_MEMORY_SIZE;
        default:
            return 0;
    }
}

#define MP_FRAME_HEAD 13 /* the count is its last byte */

void mp_wire_begin_frame(mp_wr* w, void* buf, size_t cap, uint32_t ack, uint64_t sack) {
    mp_wr_init(w, buf, cap);
    mp_wr_u32(w, ack);
    mp_wr_u64(w, sack);
    mp_wr_u8(w, 0); /* count, patched by encode_msg */
}

int mp_wire_encode_msg(mp_wr* w, const mp_msg* m) {
    size_t size = mp_wire_msg_size(m);
    size_t start = w->pos;
    size_t body_len;

    if (size == 0 || w->bad || w->pos < MP_FRAME_HEAD || size > w->cap - w->pos ||
        w->p[MP_FRAME_HEAD - 1] >= MP_MAX_RECORDS) {
        return 0;
    }
    body_len = size - (1 + 1 + (m->reliable ? 4 : 0) + 2);
    mp_wr_u8(w, m->type);
    mp_wr_u8(w, m->reliable ? 1 : 0);
    if (m->reliable) {
        mp_wr_u32(w, m->seq);
    }
    mp_wr_u16(w, (uint16_t)body_len);

    switch (m->type) {
        case MP_MSG_PSTATE: {
            const mp_pstate* s = &m->u.pstate;
            mp_wr_u8(w, s->player);
            mp_wr_u16(w, s->scene);
            mp_wr_u16(w, s->field);
            mp_wr_u8(w, s->flags);
            mp_wr_f32(w, s->x);
            mp_wr_f32(w, s->y);
            mp_wr_f32(w, s->z);
            mp_wr_u16(w, (uint16_t)s->angle_y);
            mp_wr_u16(w, s->anim0);
            mp_wr_u16(w, s->anim1);
            mp_wr_f32(w, s->frame0);
            mp_wr_f32(w, s->frame1);
            mp_wr_f32(w, s->speed);
            mp_wr_u8(w, s->part_table);
            mp_wr_u8(w, s->eye);
            mp_wr_u8(w, s->mouth);
            mp_wr_u8(w, s->item_main);
            mp_wr_u8(w, s->item_shape);
            mp_wr_u8(w, s->item_anim);
            mp_wr_u8(w, s->umbrella);
            mp_wr_f32(w, s->item_frame);
            mp_wr_u16(w, s->catch_item);
            mp_wr_u32(w, s->tick);
            break;
        }
        case MP_MSG_JOIN:
            mp_wr_u16(w, m->u.join.version);
            mp_wr_u8(w, m->u.join.want_slot);
            mp_put_look(w, &m->u.join.look);
            break;
        case MP_MSG_WELCOME:
            mp_wr_u8(w, m->u.welcome.slot);
            mp_wr_u8(w, m->u.welcome.permissions);
            mp_wr_bytes(w, m->u.welcome.town, MP_NAME_LEN);
            mp_wr_u32(w, m->u.welcome.town_bytes);
            break;
        case MP_MSG_LOOK:
            mp_put_look(w, &m->u.look);
            break;
        case MP_MSG_BULK:
            mp_wr_u8(w, m->u.bulk.stream);
            mp_wr_u32(w, m->u.bulk.total);
            mp_wr_u32(w, m->u.bulk.offset);
            mp_wr_u16(w, (uint16_t)(body_len - MP_BULK_HEAD));
            mp_wr_bytes(w, m->u.bulk.data, body_len - MP_BULK_HEAD);
            break;
        case MP_MSG_CLOCK: {
            const mp_clock* c = &m->u.clock;
            mp_wr_u16(w, c->year);
            mp_wr_u8(w, c->month);
            mp_wr_u8(w, c->day);
            mp_wr_u8(w, c->hour);
            mp_wr_u8(w, c->min);
            mp_wr_u8(w, c->sec);
            mp_wr_u8(w, c->weekday);
            mp_wr_u8(w, c->weather);
            mp_wr_u8(w, c->weather_intensity);
            break;
        }
        case MP_MSG_BYE:
            mp_wr_u8(w, m->u.bye.reason);
            break;
        case MP_MSG_LEFT:
            mp_wr_u8(w, m->u.left.slot);
            break;
        case MP_MSG_CHAT:
            mp_wr_u8(w, m->u.chat.player);
            mp_wr_u8(w, (uint8_t)(body_len - MP_CHAT_HEAD));
            mp_wr_bytes(w, m->u.chat.text, body_len - MP_CHAT_HEAD);
            break;
        case MP_MSG_ACTION:
        case MP_MSG_WORLD: {
            const mp_action* a = &m->u.action;
            mp_wr_u8(w, a->kind);
            mp_wr_u8(w, a->player);
            mp_wr_u16(w, a->ux);
            mp_wr_u16(w, a->uz);
            mp_wr_u16(w, a->item);
            mp_wr_u16(w, a->new_item);
            mp_wr_u32(w, a->req);
            mp_wr_u8(w, a->ok);
            break;
        }
        case MP_MSG_MEMORY: {
            const mp_memory* v = &m->u.memory;
            mp_wr_u8(w, v->villager);
            mp_wr_bytes(w, v->name, 8);
            mp_wr_bytes(w, v->town, 8);
            mp_wr_u16(w, v->player_id);
            mp_wr_u16(w, v->land_id);
            mp_wr_bytes(w, v->time, 6);
            mp_wr_u16(w, v->year);
            mp_wr_bytes(w, v->prev_town, 8);
            mp_wr_u64(w, v->town_tune);
            mp_wr_u8(w, (uint8_t)v->friendship);
            break;
        }
        case MP_MSG_FISH:
        case MP_MSG_BUGS: {
            int max = m->type == MP_MSG_FISH ? MP_FISH_MAX : MP_BUG_MAX;
            int i, n = m->u.fish.count <= max ? m->u.fish.count : max;
            mp_wr_u8(w, (uint8_t)n);
            for (i = 0; i < n; i++) {
                mp_wr_u8(w, m->u.fish.e[i].slot);
                mp_wr_u8(w, m->u.fish.e[i].type);
                mp_wr_u8(w, m->u.fish.e[i].flags);
                mp_wr_f32(w, m->u.fish.e[i].x);
                mp_wr_f32(w, m->u.fish.e[i].y);
                mp_wr_f32(w, m->u.fish.e[i].z);
                mp_wr_u16(w, (uint16_t)m->u.fish.e[i].angle_y);
            }
            break;
        }
        case MP_MSG_NPCS: {
            int i, n = m->u.npcs.count <= MP_NPC_MAX ? m->u.npcs.count : MP_NPC_MAX;
            mp_wr_u8(w, (uint8_t)n);
            for (i = 0; i < n; i++) {
                mp_wr_u8(w, m->u.npcs.e[i].idx);
                mp_wr_u8(w, m->u.npcs.e[i].flags);
                mp_wr_f32(w, m->u.npcs.e[i].x);
                mp_wr_f32(w, m->u.npcs.e[i].y);
                mp_wr_f32(w, m->u.npcs.e[i].z);
                mp_wr_u16(w, (uint16_t)m->u.npcs.e[i].angle_y);
                mp_wr_u8(w, m->u.npcs.e[i].anim);
            }
            break;
        }
    }

    if (w->bad || w->pos - start != size) {
        w->pos = start;
        w->bad = 0;
        return 0;
    }
    w->p[MP_FRAME_HEAD - 1]++;
    return 1;
}

static int mp_decode_body(mp_rd* r, mp_msg* m, size_t body_len) {
    switch (m->type) {
        case MP_MSG_PSTATE: {
            mp_pstate* s = &m->u.pstate;
            if (body_len != MP_PSTATE_SIZE) {
                return 0;
            }
            s->player = mp_rd_u8(r);
            s->scene = mp_rd_u16(r);
            s->field = mp_rd_u16(r);
            s->flags = mp_rd_u8(r);
            s->x = mp_rd_f32(r);
            s->y = mp_rd_f32(r);
            s->z = mp_rd_f32(r);
            s->angle_y = (int16_t)mp_rd_u16(r);
            s->anim0 = mp_rd_u16(r);
            s->anim1 = mp_rd_u16(r);
            s->frame0 = mp_rd_f32(r);
            s->frame1 = mp_rd_f32(r);
            s->speed = mp_rd_f32(r);
            s->part_table = mp_rd_u8(r);
            s->eye = mp_rd_u8(r);
            s->mouth = mp_rd_u8(r);
            s->item_main = mp_rd_u8(r);
            s->item_shape = mp_rd_u8(r);
            s->item_anim = mp_rd_u8(r);
            s->umbrella = mp_rd_u8(r);
            s->item_frame = mp_rd_f32(r);
            s->catch_item = mp_rd_u16(r); /* (pc_mp.c checks it's a fish) */
            s->tick = mp_rd_u32(r);
            return s->player < 4 && (s->flags & ~MP_PS_FLAGS_MASK) == 0 && mp_pos_ok(s->x) && mp_pos_ok(s->y) &&
                   mp_pos_ok(s->z) && mp_frame_ok(s->frame0) && mp_frame_ok(s->frame1) && mp_frame_ok(s->item_frame) && isfinite(s->speed) &&
                   s->speed >= -4.0f && s->speed <= 4.0f;
        }
        case MP_MSG_JOIN:
            if (body_len != MP_JOIN_SIZE) {
                return 0;
            }
            m->u.join.version = mp_rd_u16(r);
            m->u.join.want_slot = mp_rd_u8(r);
            return mp_get_look(r, &m->u.join.look) && m->u.join.want_slot < 4;
        case MP_MSG_WELCOME:
            if (body_len != MP_WELCOME_SIZE) {
                return 0;
            }
            m->u.welcome.slot = mp_rd_u8(r);
            m->u.welcome.permissions = mp_rd_u8(r);
            mp_rd_bytes(r, m->u.welcome.town, MP_NAME_LEN);
            mp_text_clean(m->u.welcome.town, MP_NAME_LEN);
            m->u.welcome.town_bytes = mp_rd_u32(r);
            return m->u.welcome.slot >= 1 && m->u.welcome.slot < 4 &&
                   (m->u.welcome.permissions & ~MP_PERM_MASK) == 0 && m->u.welcome.town_bytes <= MP_BULK_MAX;
        case MP_MSG_LOOK:
            if (body_len != MP_LOOK_SIZE) {
                return 0;
            }
            return mp_get_look(r, &m->u.look);
        case MP_MSG_BULK: {
            mp_bulk* b = &m->u.bulk;
            if (body_len < MP_BULK_HEAD || body_len > MP_BULK_HEAD + MP_BULK_CHUNK) {
                return 0;
            }
            b->stream = mp_rd_u8(r);
            b->total = mp_rd_u32(r);
            b->offset = mp_rd_u32(r);
            b->len = mp_rd_u16(r);
            if (b->len != body_len - MP_BULK_HEAD || b->total > MP_BULK_MAX || b->offset > b->total ||
                b->len > b->total - b->offset || b->stream != 1) {
                return 0;
            }
            mp_rd_bytes(r, b->data, b->len);
            return 1;
        }
        case MP_MSG_CLOCK: {
            mp_clock* c = &m->u.clock;
            if (body_len != MP_CLOCK_SIZE) {
                return 0;
            }
            c->year = mp_rd_u16(r);
            c->month = mp_rd_u8(r);
            c->day = mp_rd_u8(r);
            c->hour = mp_rd_u8(r);
            c->min = mp_rd_u8(r);
            c->sec = mp_rd_u8(r);
            c->weekday = mp_rd_u8(r);
            c->weather = mp_rd_u8(r);
            c->weather_intensity = mp_rd_u8(r);
            return c->year >= 2000 && c->year <= 2099 && c->month >= 1 && c->month <= 12 && c->day >= 1 &&
                   c->day <= 31 && c->hour < 24 && c->min < 60 && c->sec < 60 && c->weekday < 7 && c->weather < 8 &&
                   c->weather_intensity < 8;
        }
        case MP_MSG_BYE:
            if (body_len != MP_BYE_SIZE) {
                return 0;
            }
            m->u.bye.reason = mp_rd_u8(r);
            if (m->u.bye.reason >= MP_BYE_COUNT) {
                m->u.bye.reason = MP_BYE_ERROR;
            }
            return 1;
        case MP_MSG_LEFT:
            if (body_len != MP_LEFT_SIZE) {
                return 0;
            }
            m->u.left.slot = mp_rd_u8(r);
            return m->u.left.slot >= 1 && m->u.left.slot < 4;
        case MP_MSG_CHAT: {
            mp_chat* c = &m->u.chat;
            if (body_len < MP_CHAT_HEAD || body_len > MP_CHAT_HEAD + MP_CHAT_LEN) {
                return 0;
            }
            c->player = mp_rd_u8(r);
            c->len = mp_rd_u8(r);
            if (c->len != body_len - MP_CHAT_HEAD || c->player >= 4) {
                return 0;
            }
            memset(c->text, ' ', MP_CHAT_LEN);
            mp_rd_bytes(r, c->text, c->len);
            mp_text_clean(c->text, MP_CHAT_LEN);
            return 1;
        }
        case MP_MSG_ACTION:
        case MP_MSG_WORLD: {
            mp_action* a = &m->u.action;
            if (body_len != MP_ACTION_SIZE) {
                return 0;
            }
            a->kind = mp_rd_u8(r);
            a->player = mp_rd_u8(r);
            a->ux = mp_rd_u16(r);
            a->uz = mp_rd_u16(r);
            a->item = mp_rd_u16(r);
            a->new_item = mp_rd_u16(r);
            a->req = mp_rd_u32(r);
            a->ok = mp_rd_u8(r);
            /* the town is at most 7 x 10 acres of 16 x 16 units (pc_mp.c checks the real size) */
            return a->kind >= 1 && a->kind < MP_ACT_COUNT && a->player < 4 && a->ux < 7 * 16 && a->uz < 10 * 16 &&
                   a->ok <= 1;
        }
        case MP_MSG_MEMORY: {
            mp_memory* v = &m->u.memory;
            if (body_len != MP_MEMORY_SIZE) {
                return 0;
            }
            v->villager = mp_rd_u8(r);
            mp_rd_bytes(r, v->name, 8);
            mp_rd_bytes(r, v->town, 8);
            v->player_id = mp_rd_u16(r);
            v->land_id = mp_rd_u16(r);
            mp_rd_bytes(r, v->time, 6);
            v->year = mp_rd_u16(r);
            mp_rd_bytes(r, v->prev_town, 8);
            v->town_tune = mp_rd_u64(r);
            v->friendship = (int8_t)mp_rd_u8(r);
            /* (the game checks the date and names) */
            return v->villager < MP_NPC_MAX;
        }
        case MP_MSG_FISH:
        case MP_MSG_BUGS: {
            mp_fish* f = &m->u.fish;
            int max = m->type == MP_MSG_FISH ? MP_FISH_MAX : MP_BUG_MAX;
            int i;
            if (body_len < 1) {
                return 0;
            }
            f->count = mp_rd_u8(r);
            if (f->count > max || body_len != 1 + (size_t)f->count * MP_FISH_ENTRY) {
                return 0;
            }
            for (i = 0; i < f->count; i++) {
                f->e[i].slot = mp_rd_u8(r);
                f->e[i].type = mp_rd_u8(r);
                f->e[i].flags = mp_rd_u8(r);
                f->e[i].x = mp_rd_f32(r);
                f->e[i].y = mp_rd_f32(r);
                f->e[i].z = mp_rd_f32(r);
                f->e[i].angle_y = (int16_t)mp_rd_u16(r);
                if (f->e[i].slot >= max || (f->e[i].flags & ~MP_FISH_FLAGS_MASK) || !mp_pos_ok(f->e[i].x) ||
                    !mp_pos_ok(f->e[i].y) || !mp_pos_ok(f->e[i].z)) {
                    return 0;
                }
            }
            return 1;
        }
        case MP_MSG_NPCS: {
            mp_npcs* n = &m->u.npcs;
            int i;
            uint16_t seen = 0;
            if (body_len < 1) {
                return 0;
            }
            n->count = mp_rd_u8(r);
            if (n->count > MP_NPC_MAX || body_len != 1 + (size_t)n->count * MP_NPC_ENTRY) {
                return 0;
            }
            for (i = 0; i < n->count; i++) {
                n->e[i].idx = mp_rd_u8(r);
                n->e[i].flags = mp_rd_u8(r);
                n->e[i].x = mp_rd_f32(r);
                n->e[i].y = mp_rd_f32(r);
                n->e[i].z = mp_rd_f32(r);
                n->e[i].angle_y = (int16_t)mp_rd_u16(r);
                n->e[i].anim = mp_rd_u8(r);
                if (n->e[i].idx >= MP_NPC_MAX || (seen >> n->e[i].idx) & 1 || (n->e[i].flags & ~MP_NPC_FLAGS_MASK) ||
                    !mp_pos_ok(n->e[i].x) || !mp_pos_ok(n->e[i].y) || !mp_pos_ok(n->e[i].z)) {
                    return 0;
                }
                seen |= (uint16_t)(1 << n->e[i].idx);
            }
            return 1;
        }
        default:
            return 0;
    }
}

int mp_wire_decode_frame(const uint8_t* data, size_t len, mp_frame* f) {
    mp_rd r;
    int count, i;

    if (len > MP_MAX_FRAME) {
        return 0;
    }
    mp_rd_init(&r, data, len);
    f->ack = mp_rd_u32(&r);
    f->sack = mp_rd_u64(&r);
    count = mp_rd_u8(&r);
    f->count = 0;
    if (r.bad || count > MP_MAX_RECORDS) {
        return 0;
    }
    for (i = 0; i < count; i++) {
        mp_msg* m = &f->msg[i];
        uint8_t flags;
        size_t body_len, body_start;

        memset(m, 0, sizeof(*m));
        m->type = mp_rd_u8(&r);
        flags = mp_rd_u8(&r);
        if (r.bad || (flags & ~1) != 0 || m->type == MP_MSG_NONE || m->type >= MP_MSG_COUNT) {
            return 0;
        }
        m->reliable = flags & 1;
        /* positions are the only unreliable messages, everything else must be reliable */
        if ((m->type == MP_MSG_PSTATE || m->type == MP_MSG_NPCS) == (m->reliable != 0)) {
            return 0;
        }
        if (m->reliable) {
            m->seq = mp_rd_u32(&r);
            if (m->seq == 0) {
                return 0;
            }
        }
        body_len = mp_rd_u16(&r);
        if (r.bad || body_len > r.len - r.pos) {
            return 0;
        }
        body_start = r.pos;
        if (!mp_decode_body(&r, m, body_len) || r.bad || r.pos - body_start != body_len) {
            return 0;
        }
    }
    if (r.pos != r.len) {
        return 0; /* trailing bytes */
    }
    f->count = count;
    return 1;
}
