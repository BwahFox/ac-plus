/* mp_mm.h - matchmaking packets (the server's side: server/ac-plus-server.py, which documents them).
 * Builders write fixed-size requests; the parser takes only answers of exactly the right size, and the caller
 * only takes them from the server it asked. Text (town and player names) is cleaned like mp_wire's. */
#ifndef MP_MM_H
#define MP_MM_H

#include <stddef.h>
#include <stdint.h>

#define MP_MM_VERSION 1
#define MP_MM_DEFAULT_PORT 41331
#define MP_MM_CODE_LEN 6
#define MP_MM_REQ_SIZE 96
#define MP_MM_LIST_SIZE 400
#define MP_MM_PAGE 16

enum {
    MP_MM_HOST = 0x20,
    MP_MM_CODE = 0x21,
    MP_MM_LOOKUP = 0x22,
    MP_MM_ASK = 0x23,
    MP_MM_ANSWER = 0x24,
    MP_MM_FOUND = 0x25,
    MP_MM_LIST = 0x26,
    MP_MM_NONE = 0x27,
    MP_MM_INTRO = 0x28,
    MP_MM_TOWNS = 0x29,
    MP_MM_CLOSE = 0x2A,
    MP_MM_PUNCH = 0x2B, /* host -> visitor, opens the host's router; ignored on arrival */
    MP_MM_RELAY = 0x2C, /* a relay token for this visit (servers with --relay); 0x2D is relayed data (mp_link) */
    MP_MM_FIND = 0x2E,  /* visitor -> server: a town by its host's key (one visited before); answered like LOOKUP */
};

typedef struct {
    int type;
    uint32_t nonce; /* CODE, FOUND, NONE, TOWNS: the request's; ASK, INTRO: the server's request number */
    char code[MP_MM_CODE_LEN + 1];
    uint32_t ip;    /* network order */
    uint16_t port;  /* network order */
    uint8_t key[32];
    uint8_t town[8], name[8];
    uint8_t why;
    uint8_t token[8]; /* RELAY */
    int count;
    struct {
        char code[MP_MM_CODE_LEN + 1];
        uint8_t town[8], name[8];
    } towns[MP_MM_PAGE];
} mp_mm_msg;

size_t mp_mm_host(uint8_t* out, uint32_t nonce, const uint8_t key[32], int is_public, const uint8_t town[8],
                  const uint8_t name[8]);
size_t mp_mm_lookup(uint8_t* out, uint32_t nonce, const char* code, const uint8_t key[32], const uint8_t name[8]);
size_t mp_mm_find(uint8_t* out, uint32_t nonce, const uint8_t host_key[32], const uint8_t key[32],
                  const uint8_t name[8]);
size_t mp_mm_answer(uint8_t* out, uint32_t request, int yes);
size_t mp_mm_list(uint8_t* out, uint32_t nonce, int page);
size_t mp_mm_close(uint8_t* out, uint32_t nonce, const uint8_t key[32]);

/* 1 and *m filled if data is a well-formed answer from a server, else 0 */
int mp_mm_parse(const uint8_t* data, size_t len, mp_mm_msg* m);
/* a code as typed: 6 of the server's letters and digits, upper-cased into out (7 bytes); 0 if not */
int mp_mm_code_clean(const char* in, char* out);

#endif
