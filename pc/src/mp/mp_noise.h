/* mp_noise.h - multiplayer handshake and packet encryption (Monocypher).
 *
 * The handshake is Noise XX (-> e; <- e, ee, s, es; -> s, se) over X25519, BLAKE2b and XChaCha20-Poly1305:
 * both sides end up with fresh session keys and learn each other's long-term public key (the player's identity,
 * used for the friends list and the host's trusted list). Before the host keeps any state for a new visitor, the
 * visitor must echo a cookie bound to its address (stateless, like WireGuard's), so spoofed packets can't make
 * the host do work or send anything bigger than they were.
 *
 * Every packet after the handshake is DATA: receiver index, 64-bit counter, ciphertext, 16-byte tag. A DATA
 * packet whose tag doesn't check, or whose counter was seen before (replay window), is dropped before any of it
 * is parsed. Nothing here touches sockets or the game; see mp_link.c for that. */
#ifndef MP_NOISE_H
#define MP_NOISE_H

#include <stddef.h>
#include <stdint.h>

#define MP_KEY_LEN 32
#define MP_TAG_LEN 16
#define MP_HASH_LEN 64
#define MP_COOKIE_LEN 16

enum {
    MP_PKT_INIT = 1,   /* visitor -> host: e (padded so the answer is never bigger) */
    MP_PKT_COOKIE = 2, /* host -> visitor: try again with this cookie */
    MP_PKT_RESP = 3,   /* host -> visitor: e, s */
    MP_PKT_FIN = 4,    /* visitor -> host: s */
    MP_PKT_DATA = 5,
};

#define MP_INIT_SIZE 128
#define MP_COOKIE_SIZE (1 + 4 + MP_COOKIE_LEN)
#define MP_RESP_SIZE (1 + 4 + 4 + MP_KEY_LEN + MP_KEY_LEN + MP_TAG_LEN + MP_TAG_LEN)
#define MP_FIN_SIZE (1 + 4 + MP_KEY_LEN + MP_TAG_LEN + MP_TAG_LEN)
#define MP_DATA_OVERHEAD (1 + 4 + 8 + MP_TAG_LEN)

typedef struct {
    uint8_t pub[MP_KEY_LEN];
    uint8_t sec[MP_KEY_LEN];
} mp_identity;

typedef struct {
    uint8_t ck[MP_HASH_LEN];
    uint8_t h[MP_HASH_LEN];
    uint8_t k[MP_KEY_LEN];
    int has_k;
    uint8_t e_pub[MP_KEY_LEN], e_sec[MP_KEY_LEN];
    uint8_t re[MP_KEY_LEN]; /* remote ephemeral */
    uint8_t rs[MP_KEY_LEN]; /* remote static */
    uint32_t local_idx, remote_idx;
    uint8_t cookie[MP_COOKIE_LEN];
    int has_cookie;
    int state;
} mp_hs;

typedef struct {
    uint8_t k_send[MP_KEY_LEN], k_recv[MP_KEY_LEN];
    uint64_t send_ctr;
    uint64_t recv_top;  /* highest counter seen + 1 */
    uint64_t recv_win;  /* bit i = counter recv_top-1-i seen */
    uint32_t local_idx, remote_idx;
    uint8_t remote_static[MP_KEY_LEN];
    int live;
} mp_session;

/* the host's cookie secrets; rotate every couple of minutes */
typedef struct {
    uint8_t now[MP_KEY_LEN], prev[MP_KEY_LEN];
} mp_cookie_jar;

/* random bytes from the OS (BCryptGenRandom / getrandom); returns 0 on failure */
int mp_random(void* out, size_t n);
int mp_identity_new(mp_identity* id);
/* short text fingerprint of a public key, 4 groups of 4 letters/digits, e.g. "K3PA-7QXM-..." (out >= 20 bytes) */
void mp_fingerprint(const uint8_t pub[MP_KEY_LEN], char* out);

/* visitor side */
int mp_hs_client_init(mp_hs* hs, uint8_t out[MP_INIT_SIZE]);
/* COOKIE from the host: rebuilds INIT with the cookie; returns its size or 0 */
size_t mp_hs_client_on_cookie(mp_hs* hs, const uint8_t* pkt, size_t len, uint8_t out[MP_INIT_SIZE]);
/* RESP from the host: on success writes FIN to out, fills *sess (remote_static = the host's key) and returns 1 */
int mp_hs_client_on_resp(mp_hs* hs, const mp_identity* id, const uint8_t* pkt, size_t len, uint8_t out[MP_FIN_SIZE],
                         mp_session* sess);

/* host side. addr/addr_len identify the sender (IP + port bytes) for the cookie.
 * on_init returns MP_PKT_COOKIE (out holds a COOKIE), MP_PKT_RESP (out holds a RESP, *hs is the pending
 * handshake to keep) or 0 (drop). */
void mp_cookie_rotate(mp_cookie_jar* jar);
int mp_hs_host_on_init(mp_hs* hs, const mp_identity* id, const mp_cookie_jar* jar, const uint8_t* addr,
                       size_t addr_len, const uint8_t* pkt, size_t len, uint8_t* out, size_t* out_len);
int mp_hs_host_on_fin(mp_hs* hs, const uint8_t* pkt, size_t len, mp_session* sess);

/* receiver index of any packet (for finding its session); 0 if the packet is too short */
uint32_t mp_pkt_receiver(const uint8_t* pkt, size_t len);

/* DATA packets. seal returns the packet size (plain_len + MP_DATA_OVERHEAD) or 0.
 * open returns the plaintext size, or -1 if the packet must be dropped. */
size_t mp_session_seal(mp_session* s, const uint8_t* plain, size_t plain_len, uint8_t* out, size_t out_cap);
int mp_session_open(mp_session* s, const uint8_t* pkt, size_t len, uint8_t* plain, size_t plain_cap);

void mp_wipe(void* p, size_t n);

#endif
