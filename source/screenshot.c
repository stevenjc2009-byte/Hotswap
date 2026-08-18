// Dev-only framebuffer capture.
//
// Adapted from the Model Kit project's screenshot.c, where the row/column
// mapping below was confirmed against a real captured frame rather than
// assumed. Both screens are left at libctru's default GSP_BGR8_OES: 3 bytes
// per pixel stored B,G,R, which is already the byte order a 24-bit BMP wants,
// so framebuffer bytes go straight into the file with no channel swap.
//
// This exists so the UI can be verified from an emulator run instead of taken
// on trust. It is compiled out of release builds.

#include "screenshot.h"

#include <3ds.h>
#include <dirent.h>
#include <stdio.h>
#include <sys/stat.h>

#define SHOT_BPP 3
#define SHOT_H   240
#define SHOT_DIR "sdmc:/mk7swap"

static void put_u32(FILE *f, u32 v)
{
    u8 b[4] = { (u8)v, (u8)(v >> 8), (u8)(v >> 16), (u8)(v >> 24) };
    fwrite(b, 1, 4, f);
}

static void put_u16(FILE *f, u16 v)
{
    u8 b[2] = { (u8)v, (u8)(v >> 8) };
    fwrite(b, 1, 2, f);
}

bool screenshot_capture(gfxScreen_t screen, const char *name)
{
    mkdir(SHOT_DIR, 0777);   // already-exists is not a failure worth checking

    u16 fb_w = 0, fb_h = 0;
    u8 *fb = gfxGetFramebuffer(screen, GFX_LEFT, &fb_w, &fb_h);
    if (!fb) return false;

    // gfxGetFramebuffer reports width/height swapped relative to the screen,
    // because the panel is physically rotated.
    const u32 w = fb_h ? fb_h : (screen == GFX_TOP ? 400u : 320u);
    const u32 h = SHOT_H;

    char path[96];
    snprintf(path, sizeof(path), SHOT_DIR "/%s.bmp", name);

    FILE *f = fopen(path, "wb");
    if (!f) return false;

    const u32 row_bytes = w * SHOT_BPP;          // 1200 or 960, both /4
    const u32 pixel_bytes = row_bytes * h;

    fwrite("BM", 1, 2, f);
    put_u32(f, 54 + pixel_bytes);
    put_u32(f, 0);
    put_u32(f, 54);

    put_u32(f, 40);
    put_u32(f, w);
    put_u32(f, h);
    put_u16(f, 1);
    put_u16(f, 24);
    put_u32(f, 0);
    put_u32(f, pixel_bytes);
    put_u32(f, 2835);
    put_u32(f, 2835);
    put_u32(f, 0);
    put_u32(f, 0);

    // BMP rows run bottom-up. The 3DS stores physical column x as h
    // consecutive pixels running from the screen's bottom edge upward, so BMP
    // row r at column x is exactly offset (x*h + r).
    for (u32 r = 0; r < h; r++) {
        for (u32 x = 0; x < w; x++) {
            u32 off = (x * h + r) * SHOT_BPP;
            fwrite(fb + off, 1, SHOT_BPP, f);
        }
    }

    fclose(f);
    return true;
}
