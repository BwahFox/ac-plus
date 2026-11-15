/* pc_acplus_shot.c - pictures of the game's frame, for the dev harness (pc_devtest.c) and the jank button (pc_jank.c).
 *
 * Aurora draws with a GPU API that can't be read back on the spot, so a picture is asked for at the end of a frame
 * and written a few frames later, when Aurora has it (AuroraReadbackCaptureColor). Only the game's own picture: not
 * Nookery's menus drawn over it. */
#include "pc_platform.h"

#include <dolphin/gx.h>
#include <dolphin/gx/GXAurora.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int stbi_write_png(char const* filename, int w, int h, int comp, const void* data, int stride_in_bytes);

#define SHOT_KEY 0xAC5107ull
#define SHOT_WAIT_FRAMES 30 /* given up after this many frames */

static AuroraReadbackStream s_stream;
static char s_path[512];
static int s_pending;  /* asked for: captured at the next frame end */
static int s_waiting;  /* captured: frames left to find it */
static u32 s_w, s_h;
static u64 s_key = SHOT_KEY; /* a new one per picture: the stream still holds the last picture under the old one */

/* path ending in .png: a PNG, else a PPM */
void pc_shot_request(const char* path) {
    snprintf(s_path, sizeof(s_path), "%s", path);
    s_pending = 1;
}

static void shot_write(const u8* rgba) {
    size_t n = strlen(s_path);
    FILE* f;
    u32 i;

    if (n > 4 && strcmp(s_path + n - 4, ".png") == 0) {
        stbi_write_png(s_path, (int)s_w, (int)s_h, 4, rgba, (int)s_w * 4);
    } else if ((f = fopen(s_path, "wb")) != NULL) {
        fprintf(f, "P6\n%u %u\n255\n", s_w, s_h);
        for (i = 0; i < s_w * s_h; i++) {
            fwrite(rgba + i * 4, 1, 3, f);
        }
        fclose(f);
    }
    printf("[devtest] wrote %s\n", s_path);
    fflush(stdout);
}

/* At the end of every frame (pc_devtest_frame_end) */
void pc_shot_frame_end(void) {
    if (s_stream == 0 && (s_pending || s_waiting)) {
        s_stream = AuroraReadbackCreateStream();
    }
    if (s_waiting > 0) {
        u8* rgba = (u8*)malloc((size_t)s_w * s_h * 4);
        AuroraReadbackDest dest;

        dest.key = s_key;
        dest.dest = rgba;
        dest.size = s_w * s_h * 4;
        dest.found = GX_FALSE;
        if (rgba != NULL && AuroraReadbackGet(s_stream, &dest, 1) && dest.found) {
            shot_write(rgba);
            s_waiting = 0;
        } else if (--s_waiting == 0) {
            printf("[devtest] no picture for %s\n", s_path);
            fflush(stdout);
        }
        free(rgba);
    }
    if (s_pending && s_waiting == 0) {
        s_pending = 0;
        s_key++;
        if (AuroraReadbackCaptureColor(s_stream, s_key, &s_w, &s_h) && s_w > 0 && s_h > 0) {
            s_waiting = SHOT_WAIT_FRAMES;
        } else {
            printf("[devtest] the picture couldn't be taken\n");
        }
    }
}
