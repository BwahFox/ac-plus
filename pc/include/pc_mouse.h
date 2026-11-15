/* pc_mouse.h - the mouse for ac-plus's own screens, answered from Nookery's game pointer (pc_acplus_host.cpp) */
#ifndef PC_MOUSE_H
#define PC_MOUSE_H

#include "pc_platform.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PC_MOUSE_BUTTON_LEFT   (1 << 0)
#define PC_MOUSE_BUTTON_MIDDLE (1 << 1)
#define PC_MOUSE_BUTTON_RIGHT  (1 << 2)

extern s32 g_mouse_wheel_delta;

u32 pc_mouse_button_held(void);
u32 pc_mouse_button_pressed(void);
u32 pc_mouse_button_released(void);
s32 pc_mouse_scroll_wheel(void);
void pc_mouse_get_position(s32* x, s32* y);        /* window pixels */
void pc_mouse_get_native_position(s32* x, s32* y); /* the game's 320x240 screen */
int pc_mouse_moved(void);
int pc_mouse_active(void);

#ifdef __cplusplus
}
#endif

#endif /* PC_MOUSE_H */
