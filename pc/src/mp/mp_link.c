/* mp_link.c - see mp_link.h.
 * Reliability is selective repeat: reliable messages carry a sequence number; the receiver hands them to the game
 * in order, keeps ones that arrive early (up to the send window), and acks the highest in-order one plus a bitmap
 * of the early ones. The sender resends whatever is neither acked nor in the bitmap after the retransmit timeout.
 * BYE is taken whenever it arrives (it is authenticated like everything else).
 * Relay routes: a peer behind a relay has the address 0.0.0.0:route. What is sent there goes to the route's server
 * as RELAYED + the route's token + the packet, and what the server sends back with our token comes from that
 * address; the server sees only the encrypted packets. */
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
typedef SOCKET mp_sock_t;
#define MP_BAD_SOCK INVALID_SOCKET
#define mp_closesock closesocket
#else
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
typedef int mp_sock_t;
#define MP_BAD_SOCK (-1)
#define mp_closesock close
#endif

#include "mp_link.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MP_SLOTS (MP_LINK_PEERS + 1) /* one more than allowed, so the game can tell an extra visitor "full" */
#define MP_HS_RESEND_MS 500
#define MP_HS_GIVEUP_MS 10000
#define MP_TIMEOUT_MS 15000
#define MP_KEEPALIVE_MS 1000
#define MP_SEND_WINDOW 64
#define MP_PACKETS_PER_FLUSH 24
#define MP_INIT_PER_SEC 20
#define MP_RELAY_ROUTES 8
#define MP_RELAY_HEAD (1 + MP_RELAY_TOKEN)

enum { PS_FREE, PS_HS, PS_UNCONFIRMED, PS_LIVE };

typedef struct {
    int state;
    struct sockaddr_in addr;
    mp_hs hs;
    mp_session sess;
    unsigned long long started, last_recv, last_send, hs_sent;
    uint8_t hs_pkt[MP_INIT_SIZE];
    size_t hs_pkt_len;

    mp_msg q[MP_REL_QUEUE];
    unsigned long long q_sent[MP_REL_QUEUE];
    uint8_t q_sacked[MP_REL_QUEUE];
    mp_msg rq[MP_REL_QUEUE]; /* received early, by seq */
    uint8_t rq_has[MP_REL_QUEUE];
    uint32_t next_seq; /* next sequence number to give */
    uint32_t acked;    /* highest acked */
    uint32_t recv_seq; /* highest received in order */
    int ack_dirty;

    uint8_t expect_key[MP_KEY_LEN]; /* visitor: the host's key as the matchmaker gave it */
    int has_expect;
    mp_pstate state_out[MP_STATE_SLOTS]; /* the latest of each player, by slot */
    uint8_t has_state[MP_STATE_SLOTS];
    mp_npcs npcs_out;
    int has_npcs;
    int rtt; /* smoothed, ms; -1 unknown */
} mp_peer;

typedef struct {
    int used;
    struct sockaddr_in server;
    uint8_t token[MP_RELAY_TOKEN];
    unsigned long long at;
} mp_route;

struct mp_link {
    mp_sock_t sock;
    int host;
    mp_identity id;
    mp_cookie_jar jar;
    unsigned long long jar_time;
    unsigned long long init_window;
    int init_count;
    mp_peer peers[MP_SLOTS];
    int loss;
    unsigned short port;
    mp_raw_fn raw_fn;
    void* raw_user;
    mp_route routes[MP_RELAY_ROUTES];
    unsigned long long last_poll;
};

static int mp_sock_init(void) {
#ifdef _WIN32
    static int done = 0;
    WSADATA wsa;
    if (!done) {
        if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
            return 0;
        }
        done = 1;
    }
#endif
    return 1;
}

mp_link* mp_link_open(const mp_identity* id, int host, unsigned short port) {
    mp_link* l;
    struct sockaddr_in a;
    socklen_t alen = sizeof(a);

    if (!mp_sock_init()) {
        return NULL;
    }
    l = (mp_link*)calloc(1, sizeof(*l));
    if (l == NULL) {
        return NULL;
    }
    l->sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (l->sock == MP_BAD_SOCK) {
        free(l);
        return NULL;
    }
    memset(&a, 0, sizeof(a));
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_ANY);
    a.sin_port = htons(port);
    if (bind(l->sock, (struct sockaddr*)&a, sizeof(a)) != 0) {
        mp_closesock(l->sock);
        free(l);
        return NULL;
    }
#ifdef _WIN32
    {
        u_long nb = 1;
        ioctlsocket(l->sock, FIONBIO, &nb);
    }
#else
    fcntl(l->sock, F_SETFL, fcntl(l->sock, F_GETFL, 0) | O_NONBLOCK);
#endif
    if (getsockname(l->sock, (struct sockaddr*)&a, &alen) == 0) {
        l->port = ntohs(a.sin_port);
    }
    l->host = host;
    l->id = *id;
    mp_cookie_rotate(&l->jar);
    mp_cookie_rotate(&l->jar);
    return l;
}

void mp_link_close(mp_link* l) {
    int i;
    if (l == NULL) {
        return;
    }
    for (i = 0; i < MP_SLOTS; i++) {
        if (l->peers[i].state == PS_LIVE) {
            mp_link_kick(l, i, MP_BYE_QUIT);
        }
    }
    mp_closesock(l->sock);
    mp_wipe(l, sizeof(*l));
    free(l);
}

unsigned short mp_link_port(mp_link* l) {
    return l->port;
}

void mp_link_set_loss(mp_link* l, int percent) {
    l->loss = percent;
}

/* the route a virtual address stands for, or -1 */
static int route_of(const mp_link* l, const struct sockaddr_in* a) {
    int r = (int)ntohs(a->sin_port) - 1;
    if (a->sin_addr.s_addr != 0 || r < 0 || r >= MP_RELAY_ROUTES || !l->routes[r].used) {
        return -1;
    }
    return r;
}

/* time since t; 0 for a t later than now (times taken during a poll are later than the poll's own now: an unsigned
 * difference made them look ages old, and a handshake begun in the poll gave up at once) */
static unsigned long long since(unsigned long long now, unsigned long long t) {
    return now > t ? now - t : 0;
}

static void send_raw(mp_link* l, const struct sockaddr_in* to, const uint8_t* p, size_t len) {
    if (l->loss > 0 && rand() % 100 < l->loss) {
        return;
    }
    if (len > 0 && p[0] < 0x20 && p[0] != MP_PKT_DATA && getenv("PC_MP_DEBUG") != NULL) {
        printf("[mplink] %s sends handshake packet %d (%d bytes) to %08X:%u\n", l->host ? "host" : "visitor", p[0],
               (int)len, (unsigned)ntohl(to->sin_addr.s_addr), (unsigned)ntohs(to->sin_port));
    }
    if (to->sin_addr.s_addr == 0) {
        uint8_t wrapped[MP_RELAY_HEAD + MP_MAX_PACKET];
        int r = route_of(l, to);
        if (r < 0 || len > MP_MAX_PACKET) {
            return;
        }
        wrapped[0] = MP_PKT_RELAYED;
        memcpy(wrapped + 1, l->routes[r].token, MP_RELAY_TOKEN);
        memcpy(wrapped + MP_RELAY_HEAD, p, len);
        sendto(l->sock, (const char*)wrapped, (int)(MP_RELAY_HEAD + len), 0,
               (const struct sockaddr*)&l->routes[r].server, sizeof(l->routes[r].server));
        return;
    }
    sendto(l->sock, (const char*)p, (int)len, 0, (const struct sockaddr*)to, sizeof(*to));
}

static void peer_reset(mp_peer* p) {
    mp_wipe(p, sizeof(*p));
}

static void peer_start(mp_peer* p, const struct sockaddr_in* addr, unsigned long long now) {
    peer_reset(p);
    p->addr = *addr;
    p->started = now;
    p->last_recv = now;
    p->next_seq = 1;
    p->rtt = -1;
}

static int same_addr(const struct sockaddr_in* a, const struct sockaddr_in* b) {
    return a->sin_addr.s_addr == b->sin_addr.s_addr && a->sin_port == b->sin_port;
}

int mp_resolve(const char* address, unsigned short default_port, mp_addr* out) {
    char hostname[256];
    const char* colon;
    unsigned port = default_port;
    struct addrinfo hints, *res = NULL;
    struct sockaddr_in addr;
    size_t n;

    if (address == NULL || !mp_sock_init()) {
        return 0;
    }
    colon = strrchr(address, ':');
    n = colon != NULL ? (size_t)(colon - address) : strlen(address);
    if (n == 0 || n >= sizeof(hostname)) {
        return 0;
    }
    memcpy(hostname, address, n);
    hostname[n] = '\0';
    if (colon != NULL) {
        char* end;
        unsigned long v = strtoul(colon + 1, &end, 10);
        if (*end != '\0' || v == 0 || v > 65535) {
            return 0;
        }
        port = (unsigned)v;
    }
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    if (getaddrinfo(hostname, NULL, &hints, &res) != 0 || res == NULL) {
        return 0;
    }
    memcpy(&addr, res->ai_addr, sizeof(addr));
    freeaddrinfo(res);
    out->ip = addr.sin_addr.s_addr;
    out->port = htons((unsigned short)port);
    return out->ip != 0; /* 0.0.0.0 is how relay routes are addressed */
}

static void to_sockaddr(const mp_addr* a, struct sockaddr_in* s) {
    memset(s, 0, sizeof(*s));
    s->sin_family = AF_INET;
    s->sin_addr.s_addr = a->ip;
    s->sin_port = a->port;
}

int mp_link_connect(mp_link* l, const char* address, unsigned long long now_ms) {
    mp_addr a;
    return mp_resolve(address, MP_DEFAULT_PORT, &a) && mp_link_connect_addr(l, &a, NULL, now_ms);
}

int mp_link_connect_addr(mp_link* l, const mp_addr* a, const uint8_t* expect_key, unsigned long long now_ms) {
    struct sockaddr_in addr;
    mp_peer* p = &l->peers[0];

    if (l->host) {
        return 0;
    }
    to_sockaddr(a, &addr);
    peer_start(p, &addr, now_ms);
    if (expect_key != NULL) {
        memcpy(p->expect_key, expect_key, MP_KEY_LEN);
        p->has_expect = 1;
    }
    if (!mp_hs_client_init(&p->hs, p->hs_pkt)) {
        peer_reset(p);
        return 0;
    }
    p->hs_pkt_len = MP_INIT_SIZE;
    p->state = PS_HS;
    p->hs_sent = now_ms;
    send_raw(l, &p->addr, p->hs_pkt, p->hs_pkt_len);
    return 1;
}

void mp_link_send_to(mp_link* l, const mp_addr* a, const void* data, size_t len) {
    struct sockaddr_in addr;
    to_sockaddr(a, &addr);
    send_raw(l, &addr, (const uint8_t*)data, len);
}

int mp_link_add_relay(mp_link* l, const mp_addr* server, const uint8_t* token, unsigned long long now_ms,
                      mp_addr* out) {
    int r, i, best = -1;

    for (r = 0; r < MP_RELAY_ROUTES; r++) {
        int in_use = 0;
        if (l->routes[r].used) {
            for (i = 0; i < MP_SLOTS; i++) {
                struct sockaddr_in* a = &l->peers[i].addr;
                in_use |= l->peers[i].state != PS_FREE && a->sin_addr.s_addr == 0 && ntohs(a->sin_port) == r + 1;
            }
        }
        if (!in_use && (best < 0 || !l->routes[r].used || (l->routes[best].used && l->routes[r].at < l->routes[best].at))) {
            best = r; /* a free route, else the oldest one nobody is on */
        }
    }
    if (best < 0 || server->ip == 0) {
        return 0;
    }
    l->routes[best].used = 1;
    to_sockaddr(server, &l->routes[best].server);
    memcpy(l->routes[best].token, token, MP_RELAY_TOKEN);
    l->routes[best].at = now_ms;
    out->ip = 0;
    out->port = htons((unsigned short)(best + 1));
    return 1;
}

void mp_link_set_raw_handler(mp_link* l, mp_raw_fn fn, void* user) {
    l->raw_fn = fn;
    l->raw_user = user;
}

static void emit(mp_event_fn fn, void* user, int type, int peer, const uint8_t* key, const mp_msg* m, int reason) {
    mp_event ev;
    ev.type = type;
    ev.peer = peer;
    ev.remote_key = key;
    ev.msg = m;
    ev.reason = reason;
    if (fn != NULL) {
        fn(user, &ev);
    }
}

static void gone(mp_link* l, int i, int reason, mp_event_fn fn, void* user) {
    int was_live = l->peers[i].state == PS_LIVE;
    peer_reset(&l->peers[i]);
    if (was_live || !l->host) {
        emit(fn, user, MP_EV_GONE, i, NULL, NULL, reason);
    }
}

static int find_by_idx(mp_link* l, uint32_t idx) {
    int i;
    if (idx == 0) {
        return -1;
    }
    for (i = 0; i < MP_SLOTS; i++) {
        mp_peer* p = &l->peers[i];
        if (p->state == PS_HS && p->hs.local_idx == idx) {
            return i;
        }
        if ((p->state == PS_LIVE || p->state == PS_UNCONFIRMED) && p->sess.local_idx == idx) {
            return i;
        }
    }
    return -1;
}

static void send_frame_now(mp_link* l, int i, unsigned long long now);

static void host_on_init(mp_link* l, const struct sockaddr_in* from, const uint8_t* pkt, size_t len,
                         unsigned long long now) {
    uint8_t addr[6];
    uint8_t out[MP_RESP_SIZE];
    size_t out_len;
    mp_hs hs;
    int i, slot = -1, r;
    uint32_t sender;

    if (since(now, l->init_window) >= 1000) {
        l->init_window = now;
        l->init_count = 0;
    }
    if (++l->init_count > MP_INIT_PER_SEC) {
        return;
    }
    if (len != MP_INIT_SIZE) {
        return;
    }
    sender = (uint32_t)pkt[1] | ((uint32_t)pkt[2] << 8) | ((uint32_t)pkt[3] << 16) | ((uint32_t)pkt[4] << 24);
    for (i = 0; i < MP_SLOTS; i++) {
        mp_peer* p = &l->peers[i];
        if (p->state != PS_FREE && same_addr(&p->addr, from)) {
            uint32_t remote = p->state == PS_HS ? p->hs.remote_idx : p->sess.remote_idx;
            if (remote == sender) {
                if (p->state != PS_HS) {
                    return; /* a late copy of the INIT of a live session */
                }
                slot = i; /* the RESP was lost: answer again with a new one */
            }
        }
    }
    if (slot < 0) {
        for (i = 0; i < MP_SLOTS && slot < 0; i++) {
            if (l->peers[i].state == PS_FREE) {
                slot = i;
            }
        }
    }
    memcpy(addr, &from->sin_addr.s_addr, 4);
    memcpy(addr + 4, &from->sin_port, 2);
    r = mp_hs_host_on_init(&hs, &l->id, &l->jar, addr, sizeof(addr), pkt, len, out, &out_len);
    if (r == MP_PKT_COOKIE) {
        send_raw(l, from, out, out_len);
    } else if (r == MP_PKT_RESP && slot >= 0) {
        mp_peer* p = &l->peers[slot];
        peer_start(p, from, now);
        p->hs = hs;
        p->state = PS_HS;
        send_raw(l, from, out, out_len);
    }
    mp_wipe(&hs, sizeof(hs));
}

static void on_frame(mp_link* l, int i, const mp_frame* f, unsigned long long now, mp_event_fn fn, void* user) {
    mp_peer* p = &l->peers[i];
    int k;

    if (f->ack > p->acked && f->ack < p->next_seq) {
        unsigned long long sent = p->q_sent[f->ack % MP_REL_QUEUE];
        if (sent != 0 && now >= sent) {
            int sample = (int)since(now, sent);
            p->rtt = p->rtt < 0 ? sample : (p->rtt * 7 + sample) / 8;
        }
        while (p->acked < f->ack) {
            p->acked++;
            memset(&p->q[p->acked % MP_REL_QUEUE], 0, sizeof(mp_msg));
            p->q_sent[p->acked % MP_REL_QUEUE] = 0;
            p->q_sacked[p->acked % MP_REL_QUEUE] = 0;
        }
    }
    for (k = 0; k < 64; k++) {
        uint32_t s = f->ack + 2 + (uint32_t)k;
        if (((f->sack >> k) & 1) && s > p->acked && s < p->next_seq) {
            p->q_sacked[s % MP_REL_QUEUE] = 1;
        }
    }
    for (k = 0; k < f->count && l->peers[i].state == PS_LIVE; k++) {
        const mp_msg* m = &f->msg[k];
        if (m->type == MP_MSG_BYE) {
            gone(l, i, m->u.bye.reason, fn, user);
            return;
        }
        if (!m->reliable) {
            emit(fn, user, MP_EV_MESSAGE, i, p->sess.remote_static, m, 0);
            continue;
        }
        p->ack_dirty = 1;
        if (m->seq > p->recv_seq + 1 && m->seq <= p->recv_seq + MP_SEND_WINDOW) {
            int slot = m->seq % MP_REL_QUEUE;
            if (!p->rq_has[slot]) {
                p->rq[slot] = *m;
                p->rq_has[slot] = 1;
            }
            continue;
        }
        if (m->seq != p->recv_seq + 1) {
            continue;
        }
        p->recv_seq++;
        emit(fn, user, MP_EV_MESSAGE, i, p->sess.remote_static, m, 0);
        /* then whatever had arrived early and is next now */
        while (l->peers[i].state == PS_LIVE && p->rq_has[(p->recv_seq + 1) % MP_REL_QUEUE]) {
            int slot = (p->recv_seq + 1) % MP_REL_QUEUE;
            p->rq_has[slot] = 0;
            p->recv_seq++;
            emit(fn, user, MP_EV_MESSAGE, i, p->sess.remote_static, &p->rq[slot], 0);
        }
    }
}

static uint64_t sack_bits(const mp_peer* p) {
    uint64_t bits = 0;
    int k;
    for (k = 0; k < 64 && k + 2 <= MP_SEND_WINDOW; k++) {
        if (p->rq_has[(p->recv_seq + 2 + (uint32_t)k) % MP_REL_QUEUE]) {
            bits |= (uint64_t)1 << k;
        }
    }
    return bits;
}

void mp_link_poll(mp_link* l, unsigned long long now, mp_event_fn fn, void* user) {
    static uint8_t buf[MP_RELAY_HEAD + MP_MAX_PACKET + 1];
    static uint8_t plain[MP_MAX_PACKET];
    static mp_frame frame;
    int i;

    if (l->last_poll != 0 && since(now, l->last_poll) > 1000) {
        /* the game stood still (a long load): that time isn't the peers' silence or a slow handshake */
        unsigned long long gap = since(now, l->last_poll);
        for (i = 0; i < MP_SLOTS; i++) {
            l->peers[i].started += gap;
            l->peers[i].last_recv += gap;
        }
    }
    l->last_poll = now;
    if (since(now, l->jar_time) > 120000) {
        mp_cookie_rotate(&l->jar);
        l->jar_time = now;
    }

    for (;;) {
        struct sockaddr_in from;
        socklen_t flen = sizeof(from);
        int n = (int)recvfrom(l->sock, (char*)buf, sizeof(buf), 0, (struct sockaddr*)&from, &flen);

        if (n < 0) {
#ifdef _WIN32
            if (WSAGetLastError() == WSAECONNRESET) {
                continue; /* an ICMP "port unreachable" for an earlier send */
            }
#endif
            break;
        }
        if (n < 5 || flen != sizeof(from) || from.sin_family != AF_INET) {
            continue;
        }
        if (buf[0] == MP_PKT_RELAYED) {
            /* from a relay we set up, with our token: the packet inside is from that route's peer */
            int r;
            for (r = 0; r < MP_RELAY_ROUTES; r++) {
                if (l->routes[r].used && same_addr(&l->routes[r].server, &from) && n >= MP_RELAY_HEAD &&
                    memcmp(buf + 1, l->routes[r].token, MP_RELAY_TOKEN) == 0) {
                    break;
                }
            }
            if (r == MP_RELAY_ROUTES) {
                continue;
            }
            n -= MP_RELAY_HEAD;
            memmove(buf, buf + MP_RELAY_HEAD, (size_t)n);
            memset(&from, 0, sizeof(from));
            from.sin_family = AF_INET;
            from.sin_port = htons((unsigned short)(r + 1));
            if (n < 5 || buf[0] >= 0x20) {
                continue; /* only game packets come through a relay */
            }
        }
        if (n > MP_MAX_PACKET) {
            continue;
        }
        if (buf[0] < 0x20 && buf[0] != MP_PKT_DATA && getenv("PC_MP_DEBUG") != NULL) {
            printf("[mplink] %s got handshake packet %d (%d bytes) from %08X:%u\n", l->host ? "host" : "visitor",
                   buf[0], (int)n, (unsigned)ntohl(from.sin_addr.s_addr), (unsigned)ntohs(from.sin_port));
        }
        switch (buf[0]) {
            case MP_PKT_INIT:
                if (l->host) {
                    host_on_init(l, &from, buf, (size_t)n, now);
                }
                break;
            case MP_PKT_COOKIE: {
                mp_peer* p = &l->peers[0];
                if (!l->host && p->state == PS_HS && same_addr(&p->addr, &from)) {
                    size_t len = mp_hs_client_on_cookie(&p->hs, buf, (size_t)n, p->hs_pkt);
                    if (len > 0) {
                        p->hs_pkt_len = len;
                        p->hs_sent = now;
                        send_raw(l, &p->addr, p->hs_pkt, len);
                    }
                }
                break;
            }
            case MP_PKT_RESP: {
                mp_peer* p = &l->peers[0];
                if (!l->host && p->state == PS_HS && same_addr(&p->addr, &from) &&
                    mp_hs_client_on_resp(&p->hs, &l->id, buf, (size_t)n, p->hs_pkt, &p->sess)) {
                    if (p->has_expect && memcmp(p->sess.remote_static, p->expect_key, MP_KEY_LEN) != 0) {
                        /* not the town the matchmaker introduced: someone in between */
                        gone(l, 0, MP_BYE_REFUSED, fn, user);
                        break;
                    }
                    p->hs_pkt_len = MP_FIN_SIZE;
                    p->state = PS_UNCONFIRMED;
                    p->hs_sent = now;
                    send_raw(l, &p->addr, p->hs_pkt, p->hs_pkt_len);
                }
                break;
            }
            case MP_PKT_FIN: {
                int k = find_by_idx(l, mp_pkt_receiver(buf, (size_t)n));
                if (l->host && k >= 0 && l->peers[k].state == PS_HS && same_addr(&l->peers[k].addr, &from)) {
                    mp_peer* p = &l->peers[k];
                    if (mp_hs_host_on_fin(&p->hs, buf, (size_t)n, &p->sess)) {
                        p->state = PS_LIVE;
                        p->last_recv = now;
                        emit(fn, user, MP_EV_CONNECTED, k, p->sess.remote_static, NULL, 0);
                        if (p->state == PS_LIVE) {
                            send_frame_now(l, k, now); /* confirms the session to the visitor */
                        }
                    }
                }
                break;
            }
            case MP_PKT_DATA: {
                int k = find_by_idx(l, mp_pkt_receiver(buf, (size_t)n));
                mp_peer* p;
                int len;
                if (k < 0) {
                    break;
                }
                p = &l->peers[k];
                if (p->state != PS_LIVE && p->state != PS_UNCONFIRMED) {
                    break;
                }
                len = mp_session_open(&p->sess, buf, (size_t)n, plain, sizeof(plain));
                if (len < 0) {
                    break; /* not from our peer, or a replay: dropped unread */
                }
                p->addr = from; /* authenticated: follow the peer if its NAT moved it */
                p->last_recv = now;
                if (p->state == PS_UNCONFIRMED) {
                    p->state = PS_LIVE;
                    emit(fn, user, MP_EV_CONNECTED, k, p->sess.remote_static, NULL, 0);
                    if (p->state != PS_LIVE) {
                        break;
                    }
                }
                if (!mp_wire_decode_frame(plain, (size_t)len, &frame)) {
                    /* our authenticated peer sent something malformed: a bug or a hostile client */
                    int was_live = p->state == PS_LIVE;
                    mp_link_kick(l, k, MP_BYE_ERROR);
                    if (was_live) {
                        emit(fn, user, MP_EV_GONE, k, NULL, NULL, MP_BYE_ERROR);
                    }
                    break;
                }
                on_frame(l, k, &frame, now, fn, user);
                break;
            }
            default:
                if (buf[0] >= 0x20 && buf[0] <= 0x2F && l->raw_fn != NULL) {
                    mp_addr a;
                    a.ip = from.sin_addr.s_addr;
                    a.port = from.sin_port;
                    l->raw_fn(l->raw_user, &a, buf, (size_t)n); /* matchmaking (mp_mm.c checks it) */
                }
                break;
        }
    }

    for (i = 0; i < MP_SLOTS; i++) {
        mp_peer* p = &l->peers[i];
        if (p->state == PS_HS || p->state == PS_UNCONFIRMED) {
            if (since(now, p->started) > MP_HS_GIVEUP_MS) {
                gone(l, i, MP_BYE_ERROR, fn, user);
            } else if (!l->host && since(now, p->hs_sent) >= MP_HS_RESEND_MS) {
                p->hs_sent = now;
                send_raw(l, &p->addr, p->hs_pkt, p->hs_pkt_len);
            }
        } else if (p->state == PS_LIVE && since(now, p->last_recv) > MP_TIMEOUT_MS) {
            gone(l, i, MP_BYE_ERROR, fn, user);
        }
    }
}

static int rto(const mp_peer* p) {
    int t = p->rtt < 0 ? 300 : p->rtt * 2 + 50;
    return t < 100 ? 100 : t > 2000 ? 2000 : t;
}

/* builds and sends one frame; returns how many reliable messages went in */
static int send_one(mp_link* l, int i, unsigned long long now, int with_state, int force) {
    static uint8_t frame[MP_MAX_FRAME];
    static uint8_t pkt[MP_MAX_PACKET];
    mp_peer* p = &l->peers[i];
    mp_wr w;
    uint32_t s;
    int rel = 0;
    size_t len;

    mp_wire_begin_frame(&w, frame, sizeof(frame), p->recv_seq, sack_bits(p));
    for (s = p->acked + 1; s < p->next_seq && s <= p->acked + MP_SEND_WINDOW; s++) {
        int slot = s % MP_REL_QUEUE;
        if (p->q_sacked[slot]) {
            continue;
        }
        if (p->q_sent[slot] != 0 && since(now, p->q_sent[slot]) < (unsigned long long)rto(p)) {
            continue;
        }
        if (!mp_wire_encode_msg(&w, &p->q[slot])) {
            break;
        }
        p->q_sent[slot] = now;
        rel++;
    }
    for (s = 0; with_state && s < MP_STATE_SLOTS; s++) {
        mp_msg m;
        if (!p->has_state[s]) {
            continue;
        }
        memset(&m, 0, sizeof(m));
        m.type = MP_MSG_PSTATE;
        m.u.pstate = p->state_out[s];
        if (mp_wire_encode_msg(&w, &m)) {
            p->has_state[s] = 0;
            force = 1;
        }
    }
    if (with_state && p->has_npcs) {
        mp_msg m;
        memset(&m, 0, sizeof(m));
        m.type = MP_MSG_NPCS;
        m.u.npcs = p->npcs_out;
        if (mp_wire_encode_msg(&w, &m)) {
            p->has_npcs = 0;
            force = 1;
        }
    }
    if (rel == 0 && !force && !p->ack_dirty && since(now, p->last_send) < MP_KEEPALIVE_MS) {
        return 0;
    }
    len = mp_session_seal(&p->sess, frame, w.pos, pkt, sizeof(pkt));
    if (len > 0) {
        send_raw(l, &p->addr, pkt, len);
        p->last_send = now;
        p->ack_dirty = 0;
    }
    return rel;
}

static void send_frame_now(mp_link* l, int i, unsigned long long now) {
    send_one(l, i, now, 0, 1);
}

void mp_link_flush(mp_link* l, unsigned long long now) {
    int i, n;
    for (i = 0; i < MP_SLOTS; i++) {
        if (l->peers[i].state != PS_LIVE) {
            continue;
        }
        for (n = 0; n < MP_PACKETS_PER_FLUSH; n++) {
            if (send_one(l, i, now, n == 0, 0) == 0) {
                break;
            }
        }
    }
}

int mp_link_send(mp_link* l, int peer, const mp_msg* m) {
    mp_peer* p;
    int slot;
    if (peer < 0 || peer >= MP_SLOTS || l->peers[peer].state != PS_LIVE || m->type == MP_MSG_PSTATE) {
        return 0;
    }
    p = &l->peers[peer];
    if (p->next_seq - p->acked >= MP_REL_QUEUE) {
        return 0;
    }
    slot = p->next_seq % MP_REL_QUEUE;
    p->q[slot] = *m;
    p->q[slot].reliable = 1;
    p->q[slot].seq = p->next_seq;
    p->q_sent[slot] = 0;
    p->q_sacked[slot] = 0;
    p->next_seq++;
    return 1;
}

void mp_link_set_state(mp_link* l, int peer, const mp_pstate* s) {
    if (peer >= 0 && peer < MP_SLOTS && l->peers[peer].state == PS_LIVE && s->player < MP_STATE_SLOTS) {
        l->peers[peer].state_out[s->player] = *s;
        l->peers[peer].has_state[s->player] = 1;
    }
}

void mp_link_set_npcs(mp_link* l, int peer, const mp_npcs* n) {
    if (peer >= 0 && peer < MP_SLOTS && l->peers[peer].state == PS_LIVE) {
        l->peers[peer].npcs_out = *n;
        l->peers[peer].has_npcs = 1;
    }
}

int mp_link_queue_free(mp_link* l, int peer) {
    mp_peer* p;
    if (peer < 0 || peer >= MP_SLOTS || l->peers[peer].state != PS_LIVE) {
        return 0;
    }
    p = &l->peers[peer];
    return MP_REL_QUEUE - 1 - (int)(p->next_seq - p->acked - 1);
}

int mp_link_peer_live(mp_link* l, int peer) {
    return peer >= 0 && peer < MP_SLOTS && l->peers[peer].state == PS_LIVE;
}

int mp_link_rtt(mp_link* l, int peer) {
    return mp_link_peer_live(l, peer) ? l->peers[peer].rtt : -1;
}

void mp_link_kick(mp_link* l, int peer, int reason) {
    static uint8_t frame[64];
    static uint8_t pkt[128];
    mp_peer* p;
    mp_wr w;
    mp_msg m;
    size_t len;
    int k;

    if (peer < 0 || peer >= MP_SLOTS) {
        return;
    }
    p = &l->peers[peer];
    if (p->state == PS_LIVE) {
        memset(&m, 0, sizeof(m));
        m.type = MP_MSG_BYE;
        m.reliable = 1;
        m.seq = p->next_seq;
        m.u.bye.reason = (uint8_t)reason;
        mp_wire_begin_frame(&w, frame, sizeof(frame), p->recv_seq, 0);
        mp_wire_encode_msg(&w, &m);
        for (k = 0; k < 3; k++) { /* no acks for a goodbye: send it a few times */
            len = mp_session_seal(&p->sess, frame, w.pos, pkt, sizeof(pkt));
            if (len > 0) {
                send_raw(l, &p->addr, pkt, len);
            }
        }
    }
    peer_reset(p);
}

int mp_local_ip(char* out, int n) {
    /* the address this computer would use to reach the internet; connecting a UDP socket sends nothing */
    struct sockaddr_in a;
    socklen_t alen = sizeof(a);
    mp_sock_t s;
    int ok = 0;

    out[0] = '\0';
    if (!mp_sock_init()) {
        return 0;
    }
    s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s == MP_BAD_SOCK) {
        return 0;
    }
    memset(&a, 0, sizeof(a));
    a.sin_family = AF_INET;
    a.sin_port = htons(53);
    a.sin_addr.s_addr = htonl(0x08080808); /* any public address will do */
    if (connect(s, (struct sockaddr*)&a, sizeof(a)) == 0 && getsockname(s, (struct sockaddr*)&a, &alen) == 0) {
        unsigned long ip = ntohl(a.sin_addr.s_addr);
        snprintf(out, (size_t)n, "%lu.%lu.%lu.%lu", (ip >> 24) & 255, (ip >> 16) & 255, (ip >> 8) & 255, ip & 255);
        ok = ip != 0;
    }
    mp_closesock(s);
    return ok;
}
