// Runtime detection of the installed copy of Mario Kart 7.
//
// Nothing here is hardcoded to one console. The title list is queried from the
// system, so a European cartridge, a Japanese eShop copy and an American SD
// install all resolve correctly on the same build.

#ifndef MK7SWAP_DETECT_H
#define MK7SWAP_DETECT_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    bool found;
    uint64_t title_id;
    char hex[17];
    const char *region;      // "Europe", "Americas", ...
    bool on_gamecard;        // affects which media the launch jump targets
} mk7_install_t;

// Scans the game card first, then SD/NAND titles. First match wins.
void mk7_detect_install(mk7_install_t *out);

// Boots the detected copy of Mario Kart 7. Does not return on success.
// Returns false if the jump could not be prepared.
bool mk7_launch(const mk7_install_t *inst);

#endif
