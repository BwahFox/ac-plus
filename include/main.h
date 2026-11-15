#ifndef MAIN_H
#define MAIN_H

#include "types.h"
#include "dolphin/os/OSThread.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SCREEN_WIDTH 320
#define SCREEN_HEIGHT 240

#define SCREEN_WIDTH_F ((f32)SCREEN_WIDTH)
#define SCREEN_HEIGHT_F ((f32)SCREEN_HEIGHT)

extern int ScreenWidth;
extern int ScreenHeight;

extern OSThread graphThread;
extern u8 SegmentBaseAddress[0x40];

#if !(defined(_MSC_VER) && defined(__clang__))
void main(void); /* (clang-cl refuses a main that isn't int: ac-plus's Windows cross-build; main.c has its own) */
#endif
extern void mainproc(void* val);

#ifdef __cplusplus
}
#endif

#endif
