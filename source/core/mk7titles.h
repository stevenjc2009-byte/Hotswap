// Mario Kart 7 title IDs, by region.
//
// Verified against multiple independent sources (kartdlphax, MK7 CTRPF plugin,
// MK7 PID Grabber, hShop, Bsquo/mk7-patch-collection). Do NOT add entries that
// have not been confirmed the same way - a wrong ID silently points the swapper
// at a folder the game never reads, which looks exactly like "the mod did
// nothing" to the user.
//
// The title ID alone does not identify the game revision (e.g. EUR v1.0 and
// EUR v1.2 share 0004000000030700). That is fine: LayeredFS and the plugin
// loader key off the title ID only.

#ifndef MK7SWAP_TITLES_H
#define MK7SWAP_TITLES_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint64_t title_id;
    const char *hex;     // uppercase, 16 chars - the luma folder name
    const char *region;  // human readable, shown in the UI
} mk7_title_t;

#define MK7_TITLE_COUNT 4
extern const mk7_title_t MK7_TITLES[MK7_TITLE_COUNT];

// Returns NULL if the id is not a known MK7 title.
const mk7_title_t *mk7_lookup(uint64_t title_id);

// Returns NULL if hex is not a known MK7 title. Case-insensitive.
const mk7_title_t *mk7_lookup_hex(const char *hex);

#endif
