/* mp_test.c - native tests for the multiplayer network layer (pc/src/mp). Build and run with ./run.sh.
 * Monocypher is checked against libsodium (when built with -DMP_TEST_SODIUM), then the handshake, the session
 * crypto, the message format and a real UDP link over loopback with packet loss. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "monocypher.h"
#include "mp_link.h"
#include "mp_mm.h"
#include "mp_noise.h"
#include "mp_wire.h"

#ifdef MP_TEST_SODIUM
#include <sodium.h>
#endif

static int failures = 0;
#define CHECK(c)                                                                                                       \
    do {                                                                                                               \
        if (!(c)) {                                                                                                    \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c);                                                        \
            failures++;                                                                                                \
        }                                                                                                              \
    } while (0)

static unsigned long long now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (unsigned long long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static void fill(uint8_t* p, size_t n, unsigned seed) {
    size_t i;
    for (i = 0; i < n; i++) {
        seed = seed * 1103515245u + 12345u;
        p[i] = (uint8_t)(seed >> 16);
    }
}

static void test_crypto_vs_sodium(void) {
#ifdef MP_TEST_SODIUM
    uint8_t a_sec[32], b_sec[32], a_pub[32], b_pub[32], s1[32], s2[32];
    uint8_t key[32], nonce[24], msg[100], ad[13], c1[116], c2[116], h1[64], h2[64];
    unsigned long long clen;
    int t;

    CHECK(sodium_init() >= 0);
    for (t = 0; t < 50; t++) {
        fill(a_sec, 32, t * 7 + 1);
        fill(b_sec, 32, t * 7 + 2);
        crypto_x25519_public_key(a_pub, a_sec);
        crypto_x25519_public_key(b_pub, b_sec);
        CHECK(crypto_scalarmult_base(s1, a_sec) == 0 && memcmp(s1, a_pub, 32) == 0);
        crypto_x25519(s1, a_sec, b_pub);
        CHECK(crypto_scalarmult(s2, a_sec, b_pub) == 0 && memcmp(s1, s2, 32) == 0);

        fill(key, 32, t * 7 + 3);
        fill(nonce, 24, t * 7 + 4);
        fill(msg, sizeof(msg), t * 7 + 5);
        fill(ad, sizeof(ad), t * 7 + 6);
        crypto_aead_lock(c1, c1 + 100, key, nonce, ad, sizeof(ad), msg, 100);
        crypto_aead_xchacha20poly1305_ietf_encrypt(c2, &clen, msg, 100, ad, sizeof(ad), NULL, nonce, key);
        CHECK(clen == 116 && memcmp(c1, c2, 116) == 0);

        crypto_blake2b(h1, 64, msg, 100);
        crypto_generichash(h2, 64, msg, 100, NULL, 0);
        CHECK(memcmp(h1, h2, 64) == 0);
        crypto_blake2b_keyed(h1, 64, key, 32, msg, 100);
        crypto_generichash(h2, 64, msg, 100, key, 32);
        CHECK(memcmp(h1, h2, 64) == 0);
    }
    printf("crypto matches libsodium\n");
#else
    printf("crypto vs libsodium: skipped (no -DMP_TEST_SODIUM)\n");
#endif
}

static void handshake(mp_identity* host_id, mp_identity* vis_id, mp_session* hs_sess, mp_session* vs_sess) {
    mp_hs vhs, hhs;
    mp_cookie_jar jar;
    uint8_t init[MP_INIT_SIZE], out[256], fin[MP_FIN_SIZE];
    uint8_t addr[6] = { 127, 0, 0, 1, 0x12, 0x34 };
    size_t out_len;
    int r;

    memset(&jar, 0, sizeof(jar));
    mp_cookie_rotate(&jar);
    CHECK(mp_hs_client_init(&vhs, init));
    r = mp_hs_host_on_init(&hhs, host_id, &jar, addr, 6, init, MP_INIT_SIZE, out, &out_len);
    CHECK(r == MP_PKT_COOKIE && out_len == MP_COOKIE_SIZE && out_len < MP_INIT_SIZE);
    CHECK(mp_hs_client_on_cookie(&vhs, out, out_len, init) == MP_INIT_SIZE);
    /* the cookie is bound to the address */
    addr[5] ^= 1;
    CHECK(mp_hs_host_on_init(&hhs, host_id, &jar, addr, 6, init, MP_INIT_SIZE, out, &out_len) == MP_PKT_COOKIE);
    addr[5] ^= 1;
    r = mp_hs_host_on_init(&hhs, host_id, &jar, addr, 6, init, MP_INIT_SIZE, out, &out_len);
    CHECK(r == MP_PKT_RESP && out_len == MP_RESP_SIZE);
    CHECK(mp_hs_client_on_resp(&vhs, vis_id, out, out_len, fin, vs_sess));
    CHECK(mp_hs_host_on_fin(&hhs, fin, MP_FIN_SIZE, hs_sess));
}

static void test_handshake(void) {
    mp_identity host_id, vis_id, evil;
    mp_session hs, vs;
    uint8_t plain[200], pkt[300], back[300];
    size_t n;
    int i, len;
    char fp[24];

    CHECK(mp_identity_new(&host_id) && mp_identity_new(&vis_id) && mp_identity_new(&evil));
    handshake(&host_id, &vis_id, &hs, &vs);
    CHECK(memcmp(hs.remote_static, vis_id.pub, 32) == 0);
    CHECK(memcmp(vs.remote_static, host_id.pub, 32) == 0);
    CHECK(memcmp(hs.k_send, vs.k_recv, 32) == 0 && memcmp(hs.k_recv, vs.k_send, 32) == 0);
    CHECK(memcmp(hs.k_send, hs.k_recv, 32) != 0);

    fill(plain, sizeof(plain), 99);
    n = mp_session_seal(&vs, plain, sizeof(plain), pkt, sizeof(pkt));
    CHECK(n == sizeof(plain) + MP_DATA_OVERHEAD);
    len = mp_session_open(&hs, pkt, n, back, sizeof(back));
    CHECK(len == (int)sizeof(plain) && memcmp(back, plain, sizeof(plain)) == 0);
    CHECK(mp_session_open(&hs, pkt, n, back, sizeof(back)) == -1); /* replay */
    for (i = 0; i < (int)n; i++) {                                 /* any flipped bit is dropped */
        uint8_t p2[300];
        mp_session s2 = hs;
        memcpy(p2, pkt, n);
        p2[i] ^= 0x10;
        CHECK(mp_session_open(&s2, p2, n, back, sizeof(back)) == -1);
    }
    /* out of order inside the window is fine, each once */
    {
        uint8_t pk[5][300];
        size_t pn[5];
        int order[5] = { 4, 0, 2, 1, 3 };
        for (i = 0; i < 5; i++) {
            pn[i] = mp_session_seal(&vs, plain, 10, pk[i], 300);
        }
        for (i = 0; i < 5; i++) {
            CHECK(mp_session_open(&hs, pk[order[i]], pn[order[i]], back, sizeof(back)) == 10);
        }
        for (i = 0; i < 5; i++) {
            CHECK(mp_session_open(&hs, pk[i], pn[i], back, sizeof(back)) == -1);
        }
    }

    /* tampered RESP or FIN fail, and a forged RESP doesn't spoil the handshake */
    {
        mp_hs vhs, hhs;
        mp_cookie_jar jar;
        uint8_t init[MP_INIT_SIZE], out[256], bad[256], fin[MP_FIN_SIZE];
        uint8_t addr[6] = { 10, 0, 0, 2, 1, 2 };
        size_t out_len;
        mp_session s1, s2;
        memset(&jar, 0, sizeof(jar));
        mp_cookie_rotate(&jar);
        mp_hs_client_init(&vhs, init);
        mp_hs_host_on_init(&hhs, &host_id, &jar, addr, 6, init, MP_INIT_SIZE, out, &out_len);
        mp_hs_client_on_cookie(&vhs, out, out_len, init);
        CHECK(mp_hs_host_on_init(&hhs, &host_id, &jar, addr, 6, init, MP_INIT_SIZE, out, &out_len) == MP_PKT_RESP);
        for (i = 9; i < (int)out_len; i++) {
            memcpy(bad, out, out_len);
            bad[i] ^= 1;
            CHECK(!mp_hs_client_on_resp(&vhs, &vis_id, bad, out_len, fin, &s1));
        }
        CHECK(mp_hs_client_on_resp(&vhs, &vis_id, out, out_len, fin, &s1));
        for (i = 5; i < MP_FIN_SIZE; i++) {
            mp_hs h2 = hhs;
            memcpy(bad, fin, MP_FIN_SIZE);
            bad[i] ^= 1;
            CHECK(!mp_hs_host_on_fin(&h2, bad, MP_FIN_SIZE, &s2));
        }
        CHECK(mp_hs_host_on_fin(&hhs, fin, MP_FIN_SIZE, &s2));
    }

    /* a host with a different key: the visitor learns the other key, so a pinned key catches it */
    {
        mp_session a, b;
        handshake(&evil, &vis_id, &a, &b);
        CHECK(memcmp(b.remote_static, host_id.pub, 32) != 0);
    }
    mp_fingerprint(host_id.pub, fp);
    CHECK(strlen(fp) == 19 && fp[4] == '-');
    printf("handshake ok (host fingerprint %s)\n", fp);
}

static void test_wire(void) {
    uint8_t buf[MP_MAX_FRAME];
    mp_wr w;
    mp_msg m;
    mp_frame f;
    int i;

    mp_wire_begin_frame(&w, buf, sizeof(buf), 7, 0);
    memset(&m, 0, sizeof(m));
    m.type = MP_MSG_PSTATE;
    m.u.pstate.player = 1;
    m.u.pstate.x = 1234.5f;
    m.u.pstate.anim0 = 3;
    m.u.pstate.frame0 = 12.0f;
    m.u.pstate.speed = 1.0f;
    m.u.pstate.tick = 77;
    CHECK(mp_wire_encode_msg(&w, &m));
    memset(&m, 0, sizeof(m));
    m.type = MP_MSG_CHAT;
    m.reliable = 1;
    m.seq = 5;
    m.u.chat.len = 5;
    memcpy(m.u.chat.text, "hi\x7F!\xCD", 5);
    CHECK(mp_wire_encode_msg(&w, &m));
    CHECK(mp_wire_decode_frame(buf, w.pos, &f));
    CHECK(f.ack == 7 && f.count == 2);
    CHECK(f.msg[0].type == MP_MSG_PSTATE && f.msg[0].u.pstate.x == 1234.5f && f.msg[0].u.pstate.tick == 77);
    CHECK(f.msg[1].type == MP_MSG_CHAT && f.msg[1].seq == 5 && memcmp(f.msg[1].u.chat.text, "hi ! ", 5) == 0);

    /* LEFT: slots 1-3 only */
    for (i = 0; i < 5; i++) {
        mp_wr w2;
        uint8_t b2[64];
        mp_frame f2;
        mp_wire_begin_frame(&w2, b2, sizeof(b2), 0, 0);
        memset(&m, 0, sizeof(m));
        m.type = MP_MSG_LEFT;
        m.reliable = 1;
        m.seq = 1;
        m.u.left.slot = (uint8_t)i;
        CHECK(mp_wire_encode_msg(&w2, &m));
        CHECK(mp_wire_decode_frame(b2, w2.pos, &f2) == (i >= 1 && i <= 3));
    }

    /* every truncation fails, and one trailing byte fails */
    for (i = 0; i < (int)w.pos; i++) {
        CHECK(!mp_wire_decode_frame(buf, i, &f));
    }
    buf[w.pos] = 0;
    CHECK(!mp_wire_decode_frame(buf, w.pos + 1, &f));

    /* out-of-range values fail */
    {
        float bads[] = { 1e9f, -1e9f, 0.0f };
        uint32_t nan_bits = 0x7FC00000;
        memcpy(&bads[2], &nan_bits, 4);
        for (i = 0; i < 3; i++) {
            mp_wire_begin_frame(&w, buf, sizeof(buf), 0, 0);
            memset(&m, 0, sizeof(m));
            m.type = MP_MSG_PSTATE;
            m.u.pstate.y = bads[i];
            mp_wire_encode_msg(&w, &m);
            CHECK(!mp_wire_decode_frame(buf, w.pos, &f));
        }
        mp_wire_begin_frame(&w, buf, sizeof(buf), 0, 0);
        memset(&m, 0, sizeof(m));
        m.type = MP_MSG_BULK;
        m.reliable = 1;
        m.seq = 1;
        m.u.bulk.stream = 1;
        m.u.bulk.total = 100;
        m.u.bulk.offset = 90;
        m.u.bulk.len = 20; /* past the end */
        mp_wire_encode_msg(&w, &m);
        CHECK(!mp_wire_decode_frame(buf, w.pos, &f));
    }
    /* frames fill up and refuse more */
    mp_wire_begin_frame(&w, buf, sizeof(buf), 0, 0);
    memset(&m, 0, sizeof(m));
    m.type = MP_MSG_LOOK;
    m.reliable = 1;
    m.seq = 1;
    for (i = 0; mp_wire_encode_msg(&w, &m); i++) {
    }
    CHECK(i >= 1 && w.pos <= MP_MAX_FRAME);
    CHECK(mp_wire_decode_frame(buf, w.pos, &f) && f.count == i);

    /* random bytes and random mutations of a valid frame never get past the checks badly (run under ASan) */
    {
        uint8_t good[MP_MAX_FRAME];
        size_t good_len;
        int accepted = 0;
        mp_wire_begin_frame(&w, good, sizeof(good), 3, 0);
        memset(&m, 0, sizeof(m));
        m.type = MP_MSG_ACTION;
        m.reliable = 1;
        m.seq = 2;
        m.u.action.kind = MP_ACT_SET;
        mp_wire_encode_msg(&w, &m);
        good_len = w.pos;
        srand(1);
        for (i = 0; i < 200000; i++) {
            size_t len = good_len;
            int k;
            memcpy(buf, good, good_len);
            for (k = 0; k < 1 + rand() % 4; k++) {
                buf[rand() % good_len] = (uint8_t)rand();
            }
            if (rand() % 8 == 0) {
                len = rand() % (good_len + 1);
            }
            accepted += mp_wire_decode_frame(buf, len, &f);
        }
        printf("wire ok (%d of 200000 mutated frames still valid)\n", accepted);
    }
}

typedef struct {
    int connected, gone, gone_reason;
    uint8_t key[32];
    uint8_t* bulk;
    uint32_t bulk_got;
    uint32_t next_offset;
    int pstates;
    int order_errors;
    int messages;
} side;

static void on_event(void* user, const mp_event* ev) {
    side* s = (side*)user;
    if (ev->type == MP_EV_CONNECTED) {
        s->connected++;
        memcpy(s->key, ev->remote_key, 32);
    } else if (ev->type == MP_EV_GONE) {
        s->gone++;
        s->gone_reason = ev->reason;
    } else if (ev->type == MP_EV_MESSAGE) {
        s->messages++;
        if (ev->msg->type == MP_MSG_BULK) {
            const mp_bulk* b = &ev->msg->u.bulk;
            if (b->offset != s->next_offset) {
                s->order_errors++;
            }
            memcpy(s->bulk + b->offset, b->data, b->len);
            s->next_offset = b->offset + b->len;
            s->bulk_got += b->len;
        } else if (ev->msg->type == MP_MSG_PSTATE) {
            s->pstates++;
        }
    }
}

static void test_link(int loss) {
    mp_identity hid, vid;
    mp_link *h, *v;
    side hs, vs;
    char addr[32];
    static uint8_t data[300000];
    uint32_t sent = 0;
    unsigned long long start = now_ms(), t;
    int frames = 0;

    memset(&hs, 0, sizeof(hs));
    memset(&vs, 0, sizeof(vs));
    vs.bulk = (uint8_t*)calloc(1, sizeof(data));
    fill(data, sizeof(data), 5);
    mp_identity_new(&hid);
    mp_identity_new(&vid);
    h = mp_link_open(&hid, 1, 0);
    v = mp_link_open(&vid, 0, 0);
    CHECK(h != NULL && v != NULL);
    mp_link_set_loss(h, loss);
    mp_link_set_loss(v, loss);
    snprintf(addr, sizeof(addr), "127.0.0.1:%u", mp_link_port(h));
    CHECK(mp_link_connect(v, addr, now_ms()));

    while ((t = now_ms()) - start < 30000) {
        mp_link_poll(h, t, on_event, &hs);
        mp_link_poll(v, t, on_event, &vs);
        if (hs.connected && mp_link_peer_live(h, 0)) {
            mp_pstate ps;
            memset(&ps, 0, sizeof(ps));
            ps.tick = frames;
            mp_link_set_state(h, 0, &ps);
            while (sent < sizeof(data) && mp_link_queue_free(h, 0) > 0) {
                mp_msg m;
                uint32_t n = sizeof(data) - sent > MP_BULK_CHUNK ? MP_BULK_CHUNK : sizeof(data) - sent;
                memset(&m, 0, sizeof(m));
                m.type = MP_MSG_BULK;
                m.u.bulk.stream = 1;
                m.u.bulk.total = sizeof(data);
                m.u.bulk.offset = sent;
                m.u.bulk.len = (uint16_t)n;
                memcpy(m.u.bulk.data, data + sent, n);
                CHECK(mp_link_send(h, 0, &m));
                sent += n;
            }
        }
        mp_link_flush(h, t);
        mp_link_flush(v, t);
        frames++;
        if (vs.bulk_got == sizeof(data) && vs.pstates > 10) {
            break;
        }
        {
            struct timespec ts = { 0, 2000000 };
            nanosleep(&ts, NULL);
        }
    }
    CHECK(hs.connected == 1 && vs.connected == 1);
    CHECK(memcmp(hs.key, vid.pub, 32) == 0 && memcmp(vs.key, hid.pub, 32) == 0);
    CHECK(vs.bulk_got == sizeof(data) && memcmp(vs.bulk, data, sizeof(data)) == 0 && vs.order_errors == 0);
    printf("link ok at %d%% loss: %u bytes in order in %llu ms, %d states, rtt %d ms\n", loss, vs.bulk_got,
           now_ms() - start, vs.pstates, mp_link_rtt(v, 0));

    /* the visitor leaves: the host hears about it */
    mp_link_set_loss(v, 0);
    mp_link_close(v);
    start = now_ms();
    while (!hs.gone && now_ms() - start < 2000) {
        mp_link_poll(h, now_ms(), on_event, &hs);
    }
    CHECK(hs.gone == 1 && (loss > 0 || hs.gone_reason == MP_BYE_QUIT));
    mp_link_close(h);
    free(vs.bulk);
}

/* --- matchmaking against the real server (run.sh starts it and sets MP_TEST_MM_PORT) --- */

typedef struct {
    mp_link* link;
    mp_addr server;
    int is_host;
    char code[8];
    int asked, answered, introduced, found, none;
    uint32_t request;
    mp_addr found_at;
    uint8_t found_key[32];
    int relayed;
    uint8_t relay_token[8];
} mm_side;

static void mm_raw(void* user, const mp_addr* from, const uint8_t* data, size_t len) {
    mm_side* s = (mm_side*)user;
    mp_mm_msg m;
    uint8_t out[MP_MM_REQ_SIZE];
    if (from->ip != s->server.ip || from->port != s->server.port || !mp_mm_parse(data, len, &m)) {
        return; /* only the server's, and only well-formed */
    }
    switch (m.type) {
        case MP_MM_CODE:
            memcpy(s->code, m.code, 7);
            break;
        case MP_MM_ASK:
            s->asked++;
            mp_link_send_to(s->link, &s->server, out, mp_mm_answer(out, m.nonce, 1));
            break;
        case MP_MM_INTRO: {
            mp_addr v;
            uint8_t punch[8] = { MP_MM_PUNCH };
            s->introduced++;
            v.ip = m.ip, v.port = m.port;
            mp_link_send_to(s->link, &v, punch, sizeof(punch));
            break;
        }
        case MP_MM_FOUND:
            s->found++;
            s->found_at.ip = m.ip, s->found_at.port = m.port;
            memcpy(s->found_key, m.key, 32);
            break;
        case MP_MM_NONE:
            s->none++;
            break;
        case MP_MM_RELAY:
            s->relayed++;
            memcpy(s->relay_token, m.token, 8);
            if (s->is_host) {
                mp_addr v;
                CHECK(mp_link_add_relay(s->link, &s->server, m.token, now_ms(), &v));
            }
            break;
    }
}

/* mode 0: direct, 1: the host has another key, 2: through the relay only, 3: found by the host's key */
static void test_mm(int mode) {
    int wrong_key = mode == 1;
    const char* port_s = getenv("MP_TEST_MM_PORT");
    mp_identity hid, vid;
    mm_side hs, vs;
    side hev, vev;
    uint8_t pkt[MP_MM_REQ_SIZE];
    uint8_t name[8] = "Tester  ", town[8] = "Testtown";
    unsigned long long start = now_ms(), t;
    char addr[32];
    int connecting = 0;
    unsigned long long last_lookup = 0;

    if (port_s == NULL) {
        printf("matchmaking: skipped (no MP_TEST_MM_PORT)\n");
        return;
    }
    memset(&hs, 0, sizeof(hs));
    memset(&vs, 0, sizeof(vs));
    memset(&hev, 0, sizeof(hev));
    memset(&vev, 0, sizeof(vev));
    mp_identity_new(&hid);
    mp_identity_new(&vid);
    snprintf(addr, sizeof(addr), "127.0.0.1:%s", port_s);
    CHECK(mp_resolve(addr, MP_MM_DEFAULT_PORT, &hs.server));
    vs.server = hs.server;
    hs.is_host = 1;
    hs.link = mp_link_open(&hid, 1, 0);
    vs.link = mp_link_open(&vid, 0, 0);
    mp_link_set_raw_handler(hs.link, mm_raw, &hs);
    mp_link_set_raw_handler(vs.link, mm_raw, &vs);
    mp_link_send_to(hs.link, &hs.server, pkt, mp_mm_host(pkt, 1, hid.pub, 0, town, name));

    while ((t = now_ms()) - start < 8000 && !(hev.connected && vev.connected) && !vev.gone) {
        mp_link_poll(hs.link, t, on_event, &hev);
        mp_link_poll(vs.link, t, on_event, &vev);
        if (hs.code[0] != '\0' && !vs.found && !vs.none && t - last_lookup >= 500) {
            last_lookup = t;
            if (mode == 3) {
                mp_link_send_to(vs.link, &vs.server, pkt, mp_mm_find(pkt, 2, hid.pub, vid.pub, name));
            } else {
                mp_link_send_to(vs.link, &vs.server, pkt, mp_mm_lookup(pkt, 2, hs.code, vid.pub, name));
            }
        }
        if (vs.found && !connecting && (mode != 2 || vs.relayed)) {
            uint8_t key[32];
            mp_addr to = vs.found_at;
            memcpy(key, vs.found_key, 32);
            key[0] ^= wrong_key ? 1 : 0;
            if (mode == 2) {
                CHECK(mp_link_add_relay(vs.link, &vs.server, vs.relay_token, t, &to) && to.ip == 0);
            }
            CHECK(mp_link_connect_addr(vs.link, &to, key, t));
            connecting = 1;
        }
        mp_link_flush(hs.link, t);
        mp_link_flush(vs.link, t);
        {
            struct timespec ts = { 0, 2000000 };
            nanosleep(&ts, NULL);
        }
    }
    if (wrong_key) {
        CHECK(vev.gone == 1 && vev.gone_reason == MP_BYE_REFUSED && !vev.connected);
        printf("matchmaking: a host with another key is refused\n");
    } else {
        CHECK(strlen(hs.code) == 6 && hs.asked == 1 && hs.introduced == 1 && vs.found == 1);
        CHECK(memcmp(vs.found_key, hid.pub, 32) == 0);
        CHECK(hev.connected == 1 && vev.connected == 1);
        printf("matchmaking ok: code %s, introduced, connected %sin %llu ms\n", hs.code,
               mode == 2 ? "through the relay " : mode == 3 ? "(found by key) " : "", now_ms() - start);
    }
    if (mode == 2) {
        /* a reliable message each way through the relay */
        mp_msg m;
        unsigned long long t0 = now_ms();
        memset(&m, 0, sizeof(m));
        m.type = MP_MSG_CHAT;
        m.u.chat.len = 2;
        memcpy(m.u.chat.text, "hi", 2);
        CHECK(mp_link_send(hs.link, 0, &m) && mp_link_send(vs.link, 0, &m));
        while (now_ms() - t0 < 3000 && (hev.messages == 0 || vev.messages == 0)) {
            t = now_ms();
            mp_link_poll(hs.link, t, on_event, &hev);
            mp_link_poll(vs.link, t, on_event, &vev);
            mp_link_flush(hs.link, t);
            mp_link_flush(vs.link, t);
        }
        CHECK(hev.messages > 0 && vev.messages > 0);
        printf("relay carries messages both ways\n");
    }
    mp_link_close(hs.link);
    mp_link_close(vs.link);
}

static void test_mm_parse(void) {
    uint8_t buf[512];
    mp_mm_msg m;
    char code[8];
    int i, ok = 0;
    CHECK(mp_mm_code_clean("ABCD-EF", code) && strcmp(code, "ABCDEF") == 0);
    CHECK(!mp_mm_code_clean("ABCDE", code) && !mp_mm_code_clean("ABCDEFG", code) && !mp_mm_code_clean("ABCDE0", code));
    srand(3);
    for (i = 0; i < 200000; i++) {
        int k, len = rand() % 400;
        for (k = 0; k < len; k++) {
            buf[k] = (uint8_t)rand();
        }
        if (len > 0) {
            buf[0] = (uint8_t)(0x20 + rand() % 16);
        }
        ok += mp_mm_parse(buf, (size_t)len, &m);
    }
    printf("matchmaking parser ok (%d of 200000 random packets well-formed)\n", ok);
}

int main(void) {
    test_crypto_vs_sodium();
    test_handshake();
    test_wire();
    test_link(0);
    test_link(25);
    test_mm_parse();
    test_mm(0);
    test_mm(1);
    test_mm(2);
    test_mm(3);
    if (failures) {
        printf("%d FAILED\n", failures);
        return 1;
    }
    printf("all passed\n");
    return 0;
}
