#ifndef MK7SWAP_SCREENSHOT_H
#define MK7SWAP_SCREENSHOT_H

#include <3ds.h>
#include <stdbool.h>

// Writes sdmc:/mk7swap/<name>.bmp from the given screen's framebuffer.
bool screenshot_capture(gfxScreen_t screen, const char *name);

#endif
