/* pc_msg_text.c - our own lines for the game's message windows, from text (pc_msg_text.h) */

#include "pc_msg_text.h"
#include "m_font.h"

static int hexval(char c) {
    return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
}

int pc_msg_text(const char* t, u8* buf, int buf_size) {
    int i = 0;

    while (*t != '\0' && i < buf_size) {
        if (t[0] == '{' && t[1] != '\0' && t[2] != '\0' && t[3] == '}' && hexval(t[1]) >= 0 && hexval(t[2]) >= 0) {
            buf[i++] = (u8)(hexval(t[1]) << 4 | hexval(t[2]));
            t += 4;
        } else if (*t == '|') {
            buf[i++] = CHAR_NEW_LINE;
            t++;
        } else {
            buf[i++] = (u8)*t++;
        }
    }
    return i;
}
