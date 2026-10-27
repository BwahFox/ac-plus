/* libFuzzer target: handshake and DATA packets from a stranger (no keys), as the host and as the visitor. */
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "mp_mm.h"
#include "mp_noise.h"

int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    static int ready = 0;
    static mp_identity id;
    static mp_cookie_jar jar;
    static mp_hs client, host_pending;
    static mp_session sess;
    static uint8_t init[MP_INIT_SIZE];
    uint8_t out[512], plain[1300];
    size_t out_len;
    uint8_t addr[6] = { 1, 2, 3, 4, 5, 6 };
    mp_hs h;
    mp_session s;

    if (!ready) {
        mp_identity_new(&id);
        mp_cookie_rotate(&jar);
        mp_hs_client_init(&client, init);
        memset(&sess, 0, sizeof(sess));
        sess.live = 1;
        sess.local_idx = 0x01020304;
        ready = 1;
    }
    mp_hs_host_on_init(&h, &id, &jar, addr, 6, data, size, out, &out_len);
    {
        mp_hs c = client;
        mp_hs_client_on_cookie(&c, data, size, out);
        c = client;
        mp_hs_client_on_resp(&c, &id, data, size, out, &s);
    }
    {
        mp_hs p = host_pending;
        p.state = 2; /* HS_HOST_SENT_RESP */
        mp_hs_host_on_fin(&p, data, size, &s);
    }
    {
        mp_session t = sess;
        mp_session_open(&t, data, size, plain, sizeof(plain));
    }
    mp_pkt_receiver(data, size);
    {
        static mp_mm_msg m;
        mp_mm_parse(data, size, &m);
    }
    return 0;
}
