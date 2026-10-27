/* mp_link.h - multiplayer connections over UDP: handshakes (mp_noise), encrypted frames (mp_wire), reliable
 * messages in order with resends, one latest-only unreliable state per peer, keepalives and timeouts.
 * A host accepts up to MP_LINK_PEERS visitors; a visitor has one peer, the host.
 * Single-threaded: call mp_link_poll() and mp_link_flush() once per game frame. */
#ifndef MP_LINK_H
#define MP_LINK_H

#include "mp_noise.h"
#include "mp_wire.h"

#define MP_LINK_PEERS 3
#define MP_REL_QUEUE 128  /* reliable messages waiting for an ack, per peer */
#define MP_DEFAULT_PORT 41330

typedef struct mp_link mp_link;

/* an IPv4 address and port, both in network byte order */
typedef struct {
    uint32_t ip;
    uint16_t port;
} mp_addr;

/* packets 0x20-0x2F (matchmaking) arrive here, from anyone: the handler checks the sender */
typedef void (*mp_raw_fn)(void* user, const mp_addr* from, const uint8_t* data, size_t len);

enum {
    MP_EV_CONNECTED = 1, /* peer is live; remote_key is their identity */
    MP_EV_MESSAGE,       /* msg is a checked message from peer */
    MP_EV_GONE,          /* peer timed out or said BYE (reason) */
};

typedef struct {
    int type;
    int peer;
    const uint8_t* remote_key;
    const mp_msg* msg;
    int reason;
} mp_event;

typedef void (*mp_event_fn)(void* user, const mp_event* ev);

/* port 0 = any (visitors). Returns NULL if the socket can't be opened. */
mp_link* mp_link_open(const mp_identity* id, int host, unsigned short port);
void mp_link_close(mp_link* l);
/* visitor: start connecting to "host[:port]"; returns 0 if the address doesn't resolve */
int mp_link_connect(mp_link* l, const char* address, unsigned long long now_ms);
/* ... or to an address; with expect_key, a host with another key is refused (GONE, MP_BYE_REFUSED) */
int mp_link_connect_addr(mp_link* l, const mp_addr* a, const uint8_t* expect_key, unsigned long long now_ms);
/* "host[:port]" -> address (looks the name up) */
int mp_resolve(const char* address, unsigned short default_port, mp_addr* out);
/* matchmaking: send from the game's socket (so routers see the same port), hear back through the handler */
void mp_link_send_to(mp_link* l, const mp_addr* a, const void* data, size_t len);
void mp_link_set_raw_handler(mp_link* l, mp_raw_fn fn, void* user);
/* a route through the matchmaking server's relay (server/ac-plus-server.py --relay), with the token it gave us.
 * *out is the address to connect to, or that the peer will come from (0.0.0.0:n); 0 if every route is busy. */
#define MP_RELAY_TOKEN 8
#define MP_PKT_RELAYED 0x2D
int mp_link_add_relay(mp_link* l, const mp_addr* server, const uint8_t* token, unsigned long long now_ms,
                      mp_addr* out);

void mp_link_poll(mp_link* l, unsigned long long now_ms, mp_event_fn fn, void* user);
void mp_link_flush(mp_link* l, unsigned long long now_ms);

/* reliable: queued (copied) until acked; returns 0 if the peer's queue is full */
int mp_link_send(mp_link* l, int peer, const mp_msg* m);
/* unreliable: replaces the peer's pending state of player s->player, sent with the next flush (a host passes on
 * each visitor's state to the others) */
#define MP_STATE_SLOTS 4
void mp_link_set_state(mp_link* l, int peer, const mp_pstate* s);
/* unreliable, the same way: the villagers this game has loaded */
void mp_link_set_npcs(mp_link* l, int peer, const mp_npcs* n);
int mp_link_queue_free(mp_link* l, int peer);
int mp_link_peer_live(mp_link* l, int peer);
/* drop a peer, telling it why */
void mp_link_kick(mp_link* l, int peer, int reason);
/* the port actually bound (for tests that open port 0) */
unsigned short mp_link_port(mp_link* l);
/* round-trip time in ms of a live peer (smoothed), -1 if unknown */
int mp_link_rtt(mp_link* l, int peer);

/* this computer's address on its network (for "tell your visitors"), "a.b.c.d"; 0 if unknown */
int mp_local_ip(char* out, int n);

/* testing: drop this percentage of packets sent */
void mp_link_set_loss(mp_link* l, int percent);

#endif
