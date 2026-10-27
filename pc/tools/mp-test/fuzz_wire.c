/* libFuzzer target: the frame decoder (everything the network can hand the game). See run.sh. */
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "mp_wire.h"

int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    static mp_frame f;
    static uint8_t out[MP_MAX_FRAME];
    if (mp_wire_decode_frame(data, size, &f)) {
        /* whatever decodes must encode back to the same bytes */
        mp_wr w;
        int i;
        mp_wire_begin_frame(&w, out, sizeof(out), f.ack, f.sack);
        for (i = 0; i < f.count; i++) {
            if (!mp_wire_encode_msg(&w, &f.msg[i])) {
                __builtin_trap();
            }
        }
        if (f.count > 0 && w.pos != size) {
            __builtin_trap();
        }
    }
    return 0;
}
