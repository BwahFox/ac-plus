/* mp_noise.c - see mp_noise.h.
 * Packet layouts (indices are random u32s each side picks for the session, so packets find their session):
 *   INIT   u8 1, u32 sender, "ACMP", u16 version, e[32], cookie[16] (zero the first time), zero padding to 128
 *   COOKIE u8 2, u32 receiver, cookie[16]
 *   RESP   u8 3, u32 receiver, u32 sender, e[32], enc(s)[32+16], enc()[16]
 *   FIN    u8 4, u32 receiver, enc(s)[32+16], enc()[16]
 *   DATA   u8 5, u32 receiver, u64 counter, ciphertext, tag[16]   (the 13 header bytes are authenticated too)
 * Hashing follows Noise: h and ck are BLAKE2b-512, HKDF uses keyed BLAKE2b in place of HMAC, and the
 * handshake's AEAD is XChaCha20-Poly1305 with the nonce counter in its first 8 bytes. */
#include "mp_noise.h"
#include "mp_wire.h"

#include <string.h>

#include "monocypher.h"

#ifdef _WIN32
#include <windows.h>
#include <bcrypt.h>
#else
#include <sys/random.h>
#endif

#define MP_PROTOCOL_NAME "ac-plus Noise_XX_25519_XChaChaPoly_BLAKE2b"
static const uint8_t mp_magic[4] = { 'A', 'C', 'M', 'P' };

enum { HS_NONE, HS_CLIENT_SENT_INIT, HS_HOST_SENT_RESP, HS_DONE };

void mp_wipe(void* p, size_t n) {
    crypto_wipe(p, n);
}

int mp_random(void* out, size_t n) {
#ifdef _WIN32
    return BCryptGenRandom(NULL, (PUCHAR)out, (ULONG)n, BCRYPT_USE_SYSTEM_PREFERRED_RNG) == 0;
#else
    uint8_t* p = (uint8_t*)out;
    while (n > 0) {
        ssize_t got = getrandom(p, n, 0);
        if (got <= 0) {
            return 0;
        }
        p += got;
        n -= (size_t)got;
    }
    return 1;
#endif
}

int mp_identity_new(mp_identity* id) {
    if (!mp_random(id->sec, MP_KEY_LEN)) {
        return 0;
    }
    crypto_x25519_public_key(id->pub, id->sec);
    return 1;
}

void mp_fingerprint(const uint8_t pub[MP_KEY_LEN], char* out) {
    static const char digits[] = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789"; /* no I, O, 0, 1 */
    uint8_t h[16];
    int i, o = 0;
    uint32_t acc = 0;
    int bits = 0, n = 0;

    crypto_blake2b(h, sizeof(h), pub, MP_KEY_LEN);
    for (i = 0; i < 16 && n < 16; i++) {
        acc = (acc << 8) | h[i];
        bits += 8;
        while (bits >= 5 && n < 16) {
            if (n > 0 && n % 4 == 0) {
                out[o++] = '-';
            }
            out[o++] = digits[(acc >> (bits - 5)) & 31];
            bits -= 5;
            n++;
        }
    }
    out[o] = '\0';
}

static void put32(uint8_t* p, uint32_t v) {
    p[0] = (uint8_t)v, p[1] = (uint8_t)(v >> 8), p[2] = (uint8_t)(v >> 16), p[3] = (uint8_t)(v >> 24);
}

static uint32_t get32(const uint8_t* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void put64(uint8_t* p, uint64_t v) {
    put32(p, (uint32_t)v);
    put32(p + 4, (uint32_t)(v >> 32));
}

static uint64_t get64(const uint8_t* p) {
    return (uint64_t)get32(p) | ((uint64_t)get32(p + 4) << 32);
}

uint32_t mp_pkt_receiver(const uint8_t* pkt, size_t len) {
    if (len < 5 || pkt[0] == MP_PKT_INIT) {
        return 0;
    }
    return get32(pkt + 1);
}

/* --- Noise symmetric state --- */

static void mix_hash(mp_hs* hs, const uint8_t* data, size_t len) {
    crypto_blake2b_ctx ctx;
    crypto_blake2b_init(&ctx, MP_HASH_LEN);
    crypto_blake2b_update(&ctx, hs->h, MP_HASH_LEN);
    crypto_blake2b_update(&ctx, data, len);
    crypto_blake2b_final(&ctx, hs->h);
}

static void hkdf2(const uint8_t ck[MP_HASH_LEN], const uint8_t* ikm, size_t ikm_len, uint8_t out1[MP_HASH_LEN],
                  uint8_t out2[MP_HASH_LEN]) {
    uint8_t temp[MP_HASH_LEN];
    uint8_t buf[MP_HASH_LEN + 1];
    uint8_t one = 1;

    crypto_blake2b_keyed(temp, MP_HASH_LEN, ck, MP_HASH_LEN, ikm, ikm_len);
    crypto_blake2b_keyed(out1, MP_HASH_LEN, temp, MP_HASH_LEN, &one, 1);
    memcpy(buf, out1, MP_HASH_LEN);
    buf[MP_HASH_LEN] = 2;
    crypto_blake2b_keyed(out2, MP_HASH_LEN, temp, MP_HASH_LEN, buf, sizeof(buf));
    crypto_wipe(temp, sizeof(temp));
    crypto_wipe(buf, sizeof(buf));
}

/* the low byte of has_k counts uses of k (the nonce); 0 means no key yet */
static int mix_key(mp_hs* hs, const uint8_t* dh) {
    static const uint8_t zero[MP_KEY_LEN] = { 0 };
    uint8_t out1[MP_HASH_LEN], out2[MP_HASH_LEN];

    if (crypto_verify32(dh, zero) == 0) {
        return 0; /* a low-order point: refuse */
    }
    hkdf2(hs->ck, dh, MP_KEY_LEN, out1, out2);
    memcpy(hs->ck, out1, MP_HASH_LEN);
    memcpy(hs->k, out2, MP_KEY_LEN);
    hs->has_k = 1;
    crypto_wipe(out1, sizeof(out1));
    crypto_wipe(out2, sizeof(out2));
    return 1;
}

static void hs_nonce(const mp_hs* hs, uint8_t nonce[24]) {
    memset(nonce, 0, 24);
    nonce[0] = (uint8_t)(hs->has_k - 1);
}

/* encrypt_and_hash: out = ciphertext[len] + tag */
static void encrypt_and_hash(mp_hs* hs, const uint8_t* plain, size_t len, uint8_t* out) {
    uint8_t nonce[24];
    hs_nonce(hs, nonce);
    crypto_aead_lock(out, out + len, hs->k, nonce, hs->h, MP_HASH_LEN, plain, len);
    hs->has_k++;
    mix_hash(hs, out, len + MP_TAG_LEN);
}

static int decrypt_and_hash(mp_hs* hs, const uint8_t* in, size_t len, uint8_t* plain) {
    uint8_t nonce[24];
    uint8_t h_before[MP_HASH_LEN];
    hs_nonce(hs, nonce);
    memcpy(h_before, hs->h, MP_HASH_LEN);
    if (crypto_aead_unlock(plain, in + len, hs->k, nonce, h_before, MP_HASH_LEN, in, len) != 0) {
        return 0;
    }
    hs->has_k++;
    mix_hash(hs, in, len + MP_TAG_LEN);
    return 1;
}

static void hs_begin(mp_hs* hs) {
    uint8_t prologue[6];
    memset(hs, 0, sizeof(*hs));
    crypto_blake2b(hs->h, MP_HASH_LEN, (const uint8_t*)MP_PROTOCOL_NAME, strlen(MP_PROTOCOL_NAME));
    memcpy(hs->ck, hs->h, MP_HASH_LEN);
    memcpy(prologue, mp_magic, 4);
    prologue[4] = (uint8_t)MP_PROTO_VERSION;
    prologue[5] = (uint8_t)(MP_PROTO_VERSION >> 8);
    mix_hash(hs, prologue, sizeof(prologue));
}

static int dh(uint8_t out[MP_KEY_LEN], const uint8_t sec[MP_KEY_LEN], const uint8_t pub[MP_KEY_LEN]) {
    crypto_x25519(out, sec, pub);
    return 1;
}

static void split(mp_hs* hs, mp_session* s, int initiator) {
    uint8_t k1[MP_HASH_LEN], k2[MP_HASH_LEN];
    hkdf2(hs->ck, NULL, 0, k1, k2);
    memset(s, 0, sizeof(*s));
    memcpy(initiator ? s->k_send : s->k_recv, k1, MP_KEY_LEN);
    memcpy(initiator ? s->k_recv : s->k_send, k2, MP_KEY_LEN);
    s->local_idx = hs->local_idx;
    s->remote_idx = hs->remote_idx;
    memcpy(s->remote_static, hs->rs, MP_KEY_LEN);
    s->live = 1;
    crypto_wipe(k1, sizeof(k1));
    crypto_wipe(k2, sizeof(k2));
    crypto_wipe(hs, sizeof(*hs));
}

static uint32_t random_idx(void) {
    uint32_t v = 0;
    while (v == 0) {
        if (!mp_random(&v, sizeof(v))) {
            return 0;
        }
    }
    return v;
}

/* --- visitor --- */

static void client_write_init(mp_hs* hs, uint8_t out[MP_INIT_SIZE]) {
    memset(out, 0, MP_INIT_SIZE);
    out[0] = MP_PKT_INIT;
    put32(out + 1, hs->local_idx);
    memcpy(out + 5, mp_magic, 4);
    out[9] = (uint8_t)MP_PROTO_VERSION;
    out[10] = (uint8_t)(MP_PROTO_VERSION >> 8);
    memcpy(out + 11, hs->e_pub, MP_KEY_LEN);
    if (hs->has_cookie) {
        memcpy(out + 11 + MP_KEY_LEN, hs->cookie, MP_COOKIE_LEN);
    }
}

int mp_hs_client_init(mp_hs* hs, uint8_t out[MP_INIT_SIZE]) {
    hs_begin(hs);
    hs->local_idx = random_idx();
    if (hs->local_idx == 0 || !mp_random(hs->e_sec, MP_KEY_LEN)) {
        return 0;
    }
    crypto_x25519_public_key(hs->e_pub, hs->e_sec);
    mix_hash(hs, hs->e_pub, MP_KEY_LEN);
    hs->state = HS_CLIENT_SENT_INIT;
    client_write_init(hs, out);
    return 1;
}

size_t mp_hs_client_on_cookie(mp_hs* hs, const uint8_t* pkt, size_t len, uint8_t out[MP_INIT_SIZE]) {
    if (hs->state != HS_CLIENT_SENT_INIT || len != MP_COOKIE_SIZE || pkt[0] != MP_PKT_COOKIE ||
        get32(pkt + 1) != hs->local_idx) {
        return 0;
    }
    memcpy(hs->cookie, pkt + 5, MP_COOKIE_LEN);
    hs->has_cookie = 1;
    client_write_init(hs, out);
    return MP_INIT_SIZE;
}

int mp_hs_client_on_resp(mp_hs* hs, const mp_identity* id, const uint8_t* pkt, size_t len, uint8_t out[MP_FIN_SIZE],
                         mp_session* sess) {
    mp_hs work;
    uint8_t shared[MP_KEY_LEN];
    uint8_t empty[1];
    const uint8_t* p;
    uint8_t* o;

    if (hs->state != HS_CLIENT_SENT_INIT || len != MP_RESP_SIZE || pkt[0] != MP_PKT_RESP ||
        get32(pkt + 1) != hs->local_idx) {
        return 0;
    }
    work = *hs; /* a forged RESP must not spoil the real one that may follow */
    work.remote_idx = get32(pkt + 5);
    p = pkt + 9;
    memcpy(work.re, p, MP_KEY_LEN);
    mix_hash(&work, work.re, MP_KEY_LEN);
    p += MP_KEY_LEN;
    dh(shared, work.e_sec, work.re); /* ee */
    if (!mix_key(&work, shared)) {
        goto fail;
    }
    if (!decrypt_and_hash(&work, p, MP_KEY_LEN, work.rs)) { /* s */
        goto fail;
    }
    p += MP_KEY_LEN + MP_TAG_LEN;
    dh(shared, work.e_sec, work.rs); /* es */
    if (!mix_key(&work, shared) || !decrypt_and_hash(&work, p, 0, empty)) {
        goto fail;
    }

    /* -> s, se */
    out[0] = MP_PKT_FIN;
    put32(out + 1, work.remote_idx);
    o = out + 5;
    encrypt_and_hash(&work, id->pub, MP_KEY_LEN, o);
    o += MP_KEY_LEN + MP_TAG_LEN;
    dh(shared, id->sec, work.re); /* se */
    if (!mix_key(&work, shared)) {
        goto fail;
    }
    encrypt_and_hash(&work, NULL, 0, o);
    crypto_wipe(shared, sizeof(shared));
    split(&work, sess, 1);
    crypto_wipe(hs, sizeof(*hs));
    return 1;

fail:
    crypto_wipe(shared, sizeof(shared));
    crypto_wipe(&work, sizeof(work));
    return 0;
}

/* --- host --- */

void mp_cookie_rotate(mp_cookie_jar* jar) {
    memcpy(jar->prev, jar->now, MP_KEY_LEN);
    if (!mp_random(jar->now, MP_KEY_LEN)) {
        memcpy(jar->now, jar->prev, MP_KEY_LEN); /* keep the old secret rather than a guessable one */
    }
}

static void make_cookie(const uint8_t key[MP_KEY_LEN], const uint8_t* addr, size_t addr_len, const uint8_t* e,
                        uint8_t out[MP_COOKIE_LEN]) {
    crypto_blake2b_ctx ctx;
    crypto_blake2b_keyed_init(&ctx, MP_COOKIE_LEN, key, MP_KEY_LEN);
    crypto_blake2b_update(&ctx, addr, addr_len);
    crypto_blake2b_update(&ctx, e, MP_KEY_LEN);
    crypto_blake2b_final(&ctx, out);
}

int mp_hs_host_on_init(mp_hs* hs, const mp_identity* id, const mp_cookie_jar* jar, const uint8_t* addr,
                       size_t addr_len, const uint8_t* pkt, size_t len, uint8_t* out, size_t* out_len) {
    uint8_t want[MP_COOKIE_LEN];
    uint8_t shared[MP_KEY_LEN];
    const uint8_t* e;
    const uint8_t* cookie;
    uint8_t* o;
    int ok;

    *out_len = 0;
    if (len != MP_INIT_SIZE || pkt[0] != MP_PKT_INIT || memcmp(pkt + 5, mp_magic, 4) != 0) {
        return 0;
    }
    if ((pkt[9] | (pkt[10] << 8)) != MP_PROTO_VERSION) {
        return 0; /* the visitor sees no answer; versions are also compared in JOIN with a clear message */
    }
    e = pkt + 11;
    cookie = e + MP_KEY_LEN;

    make_cookie(jar->now, addr, addr_len, e, want);
    ok = crypto_verify16(cookie, want) == 0;
    if (!ok) {
        make_cookie(jar->prev, addr, addr_len, e, want);
        ok = crypto_verify16(cookie, want) == 0;
    }
    if (!ok) {
        make_cookie(jar->now, addr, addr_len, e, want);
        out[0] = MP_PKT_COOKIE;
        memcpy(out + 1, pkt + 1, 4);
        memcpy(out + 5, want, MP_COOKIE_LEN);
        *out_len = MP_COOKIE_SIZE;
        return MP_PKT_COOKIE;
    }

    hs_begin(hs);
    hs->remote_idx = get32(pkt + 1);
    hs->local_idx = random_idx();
    if (hs->remote_idx == 0 || hs->local_idx == 0 || !mp_random(hs->e_sec, MP_KEY_LEN)) {
        crypto_wipe(hs, sizeof(*hs));
        return 0;
    }
    memcpy(hs->re, e, MP_KEY_LEN);
    mix_hash(hs, hs->re, MP_KEY_LEN);

    /* <- e, ee, s, es */
    crypto_x25519_public_key(hs->e_pub, hs->e_sec);
    out[0] = MP_PKT_RESP;
    put32(out + 1, hs->remote_idx);
    put32(out + 5, hs->local_idx);
    o = out + 9;
    memcpy(o, hs->e_pub, MP_KEY_LEN);
    mix_hash(hs, hs->e_pub, MP_KEY_LEN);
    o += MP_KEY_LEN;
    dh(shared, hs->e_sec, hs->re); /* ee */
    if (!mix_key(hs, shared)) {
        goto fail;
    }
    encrypt_and_hash(hs, id->pub, MP_KEY_LEN, o);
    o += MP_KEY_LEN + MP_TAG_LEN;
    dh(shared, id->sec, hs->re); /* es */
    if (!mix_key(hs, shared)) {
        goto fail;
    }
    encrypt_and_hash(hs, NULL, 0, o);
    crypto_wipe(shared, sizeof(shared));
    hs->state = HS_HOST_SENT_RESP;
    *out_len = MP_RESP_SIZE;
    return MP_PKT_RESP;

fail:
    crypto_wipe(shared, sizeof(shared));
    crypto_wipe(hs, sizeof(*hs));
    return 0;
}

int mp_hs_host_on_fin(mp_hs* hs, const uint8_t* pkt, size_t len, mp_session* sess) {
    mp_hs work;
    uint8_t shared[MP_KEY_LEN];
    uint8_t empty[1];
    const uint8_t* p;

    if (hs->state != HS_HOST_SENT_RESP || len != MP_FIN_SIZE || pkt[0] != MP_PKT_FIN ||
        get32(pkt + 1) != hs->local_idx) {
        return 0;
    }
    work = *hs;
    p = pkt + 5;
    if (!decrypt_and_hash(&work, p, MP_KEY_LEN, work.rs)) {
        goto fail;
    }
    p += MP_KEY_LEN + MP_TAG_LEN;
    dh(shared, work.e_sec, work.rs); /* se */
    if (!mix_key(&work, shared) || !decrypt_and_hash(&work, p, 0, empty)) {
        goto fail;
    }
    crypto_wipe(shared, sizeof(shared));
    split(&work, sess, 0);
    crypto_wipe(hs, sizeof(*hs));
    return 1;

fail:
    crypto_wipe(shared, sizeof(shared));
    crypto_wipe(&work, sizeof(work));
    return 0;
}

/* --- DATA --- */

static void data_nonce(uint64_t ctr, uint8_t nonce[24]) {
    memset(nonce, 0, 24);
    put64(nonce, ctr);
}

size_t mp_session_seal(mp_session* s, const uint8_t* plain, size_t plain_len, uint8_t* out, size_t out_cap) {
    uint8_t nonce[24];
    size_t total = plain_len + MP_DATA_OVERHEAD;

    if (!s->live || plain_len > MP_MAX_FRAME || total > out_cap || s->send_ctr == UINT64_MAX) {
        return 0;
    }
    out[0] = MP_PKT_DATA;
    put32(out + 1, s->remote_idx);
    put64(out + 5, s->send_ctr);
    data_nonce(s->send_ctr, nonce);
    crypto_aead_lock(out + 13, out + 13 + plain_len, s->k_send, nonce, out, 13, plain, plain_len);
    s->send_ctr++;
    return total;
}

#define MP_REPLAY_WINDOW 64

int mp_session_open(mp_session* s, const uint8_t* pkt, size_t len, uint8_t* plain, size_t plain_cap) {
    uint8_t nonce[24];
    uint64_t ctr;
    size_t plain_len;

    if (!s->live || len < MP_DATA_OVERHEAD || len > MP_MAX_PACKET || pkt[0] != MP_PKT_DATA ||
        get32(pkt + 1) != s->local_idx) {
        return -1;
    }
    plain_len = len - MP_DATA_OVERHEAD;
    if (plain_len > plain_cap) {
        return -1;
    }
    ctr = get64(pkt + 5);
    if (ctr == UINT64_MAX) {
        return -1;
    }
    if (ctr + 1 <= s->recv_top) { /* old: inside the window and unseen? */
        uint64_t back = s->recv_top - 1 - ctr;
        if (back >= MP_REPLAY_WINDOW || (s->recv_win >> back) & 1) {
            return -1;
        }
    }
    data_nonce(ctr, nonce);
    if (crypto_aead_unlock(plain, pkt + len - MP_TAG_LEN, s->k_recv, nonce, pkt, 13, pkt + 13, plain_len) != 0) {
        return -1;
    }
    /* only now, with the tag checked, move the window */
    if (ctr + 1 > s->recv_top) {
        uint64_t shift = ctr + 1 - s->recv_top;
        s->recv_win = shift >= MP_REPLAY_WINDOW ? 0 : s->recv_win << shift;
        s->recv_win |= 1;
        s->recv_top = ctr + 1;
    } else {
        s->recv_win |= (uint64_t)1 << (s->recv_top - 1 - ctr);
    }
    return (int)plain_len;
}
