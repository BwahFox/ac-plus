/* mp_mm.c - see mp_mm.h */
#include "mp_mm.h"
#include "mp_wire.h"

#include <string.h>

static const char mm_code_chars[] = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";

static void head(mp_wr* w, int type) {
    mp_wr_u8(w, (uint8_t)type);
    mp_wr_bytes(w, "ACMM", 4);
    mp_wr_u16(w, MP_MM_VERSION);
}

static size_t finish(mp_wr* w, size_t size) {
    while (!w->bad && w->pos < size) {
        mp_wr_u8(w, 0); /* padding: no answer is bigger than its question */
    }
    return w->bad ? 0 : w->pos;
}

size_t mp_mm_host(uint8_t* out, uint32_t nonce, const uint8_t key[32], int is_public, const uint8_t town[8],
                  const uint8_t name[8]) {
    mp_wr w;
    mp_wr_init(&w, out, MP_MM_REQ_SIZE);
    head(&w, MP_MM_HOST);
    mp_wr_u32(&w, nonce);
    mp_wr_bytes(&w, key, 32);
    mp_wr_u8(&w, is_public ? 1 : 0);
    mp_wr_bytes(&w, town, 8);
    mp_wr_bytes(&w, name, 8);
    return finish(&w, MP_MM_REQ_SIZE);
}

size_t mp_mm_lookup(uint8_t* out, uint32_t nonce, const char* code, const uint8_t key[32], const uint8_t name[8]) {
    mp_wr w;
    mp_wr_init(&w, out, MP_MM_REQ_SIZE);
    head(&w, MP_MM_LOOKUP);
    mp_wr_u32(&w, nonce);
    mp_wr_bytes(&w, code, MP_MM_CODE_LEN);
    mp_wr_bytes(&w, key, 32);
    mp_wr_bytes(&w, name, 8);
    return finish(&w, MP_MM_REQ_SIZE);
}

size_t mp_mm_find(uint8_t* out, uint32_t nonce, const uint8_t host_key[32], const uint8_t key[32],
                  const uint8_t name[8]) {
    mp_wr w;
    mp_wr_init(&w, out, MP_MM_REQ_SIZE);
    head(&w, MP_MM_FIND);
    mp_wr_u32(&w, nonce);
    mp_wr_bytes(&w, host_key, 32);
    mp_wr_bytes(&w, key, 32);
    mp_wr_bytes(&w, name, 8);
    return finish(&w, MP_MM_REQ_SIZE);
}

size_t mp_mm_answer(uint8_t* out, uint32_t request, int yes) {
    mp_wr w;
    mp_wr_init(&w, out, MP_MM_REQ_SIZE);
    head(&w, MP_MM_ANSWER);
    mp_wr_u32(&w, request);
    mp_wr_u8(&w, (uint8_t)yes);
    return finish(&w, MP_MM_REQ_SIZE);
}

size_t mp_mm_list(uint8_t* out, uint32_t nonce, int page) {
    mp_wr w;
    mp_wr_init(&w, out, MP_MM_LIST_SIZE);
    head(&w, MP_MM_LIST);
    mp_wr_u32(&w, nonce);
    mp_wr_u8(&w, (uint8_t)page);
    return finish(&w, MP_MM_LIST_SIZE);
}

size_t mp_mm_close(uint8_t* out, uint32_t nonce, const uint8_t key[32]) {
    mp_wr w;
    mp_wr_init(&w, out, MP_MM_REQ_SIZE);
    head(&w, MP_MM_CLOSE);
    mp_wr_u32(&w, nonce);
    mp_wr_bytes(&w, key, 32);
    return finish(&w, MP_MM_REQ_SIZE);
}

static int code_char(char c) {
    int i;
    for (i = 0; mm_code_chars[i] != '\0'; i++) {
        if (mm_code_chars[i] == c) {
            return 1;
        }
    }
    return 0;
}

int mp_mm_code_clean(const char* in, char* out) {
    int n = 0;
    for (; *in != '\0'; in++) {
        char c = *in >= 'a' && *in <= 'z' ? (char)(*in - 32) : *in;
        if (c == ' ' || c == '-') {
            continue;
        }
        if (!code_char(c) || n == MP_MM_CODE_LEN) {
            return 0;
        }
        out[n++] = c;
    }
    out[n] = '\0';
    return n == MP_MM_CODE_LEN;
}

/* ip[4] as on the wire (network order), u16 port little-endian -> mp_addr's network order */
static void read_addr(mp_rd* r, mp_mm_msg* m) {
    uint16_t port;
    uint8_t* b = (uint8_t*)&m->port;
    mp_rd_bytes(r, &m->ip, 4);
    port = mp_rd_u16(r);
    b[0] = (uint8_t)(port >> 8);
    b[1] = (uint8_t)port;
}

static int read_code(mp_rd* r, char* out) {
    char raw[MP_MM_CODE_LEN + 1];
    mp_rd_bytes(r, raw, MP_MM_CODE_LEN);
    raw[MP_MM_CODE_LEN] = '\0';
    return !r->bad && strlen(raw) == MP_MM_CODE_LEN && mp_mm_code_clean(raw, out);
}

int mp_mm_parse(const uint8_t* data, size_t len, mp_mm_msg* m) {
    mp_rd r;
    int i;

    memset(m, 0, sizeof(*m));
    if (len < 5) {
        return 0;
    }
    mp_rd_init(&r, data, len);
    m->type = mp_rd_u8(&r);
    m->nonce = mp_rd_u32(&r);
    switch (m->type) {
        case MP_MM_CODE:
            if (len != 1 + 4 + MP_MM_CODE_LEN || !read_code(&r, m->code)) {
                return 0;
            }
            break;
        case MP_MM_ASK:
            if (len != 1 + 4 + 32 + 8) {
                return 0;
            }
            mp_rd_bytes(&r, m->key, 32);
            mp_rd_bytes(&r, m->name, 8);
            break;
        case MP_MM_FOUND:
            if (len != 1 + 4 + 6 + 32 + 16) {
                return 0;
            }
            read_addr(&r, m);
            mp_rd_bytes(&r, m->key, 32);
            mp_rd_bytes(&r, m->town, 8);
            mp_rd_bytes(&r, m->name, 8);
            break;
        case MP_MM_NONE:
            if (len != 1 + 4 + 1) {
                return 0;
            }
            m->why = mp_rd_u8(&r);
            break;
        case MP_MM_INTRO:
            if (len != 1 + 4 + 6 + 32) {
                return 0;
            }
            read_addr(&r, m);
            mp_rd_bytes(&r, m->key, 32);
            break;
        case MP_MM_RELAY:
            if (len != 1 + 4 + 8) {
                return 0;
            }
            mp_rd_bytes(&r, m->token, 8);
            break;
        case MP_MM_TOWNS:
            if (len < 1 + 4 + 1) {
                return 0;
            }
            m->count = mp_rd_u8(&r);
            if (m->count > MP_MM_PAGE || len != 1 + 4 + 1 + (size_t)m->count * 22) {
                return 0;
            }
            for (i = 0; i < m->count; i++) {
                if (!read_code(&r, m->towns[i].code)) {
                    return 0;
                }
                mp_rd_bytes(&r, m->towns[i].town, 8);
                mp_rd_bytes(&r, m->towns[i].name, 8);
                mp_text_clean(m->towns[i].town, 8);
                mp_text_clean(m->towns[i].name, 8);
            }
            break;
        default:
            return 0;
    }
    mp_text_clean(m->town, 8);
    mp_text_clean(m->name, 8);
    return !r.bad && r.pos == len;
}
