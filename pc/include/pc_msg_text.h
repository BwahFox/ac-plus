#ifndef PC_MSG_TEXT_H
#define PC_MSG_TEXT_H

/* Our own lines for the game's message windows, written as text: {xx} is a byte in hex (control codes: {7f}..),
 * | a new line, anything else the character itself. Returns the length written (pc_msg_text.c) */

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

int pc_msg_text(const char* t, u8* buf, int buf_size);

#ifdef __cplusplus
}
#endif

#endif /* PC_MSG_TEXT_H */
