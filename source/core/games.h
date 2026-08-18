// The registry of games Hotswap knows how to mod.
//
// Adding a game is one entry here plus its verified region title IDs. Nothing
// else in the engine is game-specific.
//
// Do NOT add a title ID that has not been confirmed against several independent
// sources. A wrong ID silently points the swapper at a folder the game never
// reads, which looks exactly like "the mod did nothing" to the user - the worst
// kind of bug, because it looks like the mod's fault.
//
// A title ID does not identify the game revision (EUR v1.0 and EUR v1.2 share
// 0004000000030700). That is fine: LayeredFS and the plugin loader key off the
// title ID only.

#ifndef HOTSWAP_GAMES_H
#define HOTSWAP_GAMES_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint64_t title_id;
    const char *hex;     // uppercase, 16 chars - the luma folder name
    const char *region;  // human readable, shown in the UI
} hs_title_t;

typedef struct {
    const char *slug;     // folder name under /hotswap/ - lowercase, no spaces
    const char *name;     // shown in the game list
    const char *short_name;  // for tight spaces; never NULL

    // The well-known community mod for this game, if it has one. NULL when it
    // does not - most games will not.
    const char *community;      // display name, e.g. "CTGP-7"
    const char *community_slug; // mod folder name, e.g. "ctgp7"
    const char *community_dir;  // SD folder whose presence proves it is installed

    const hs_title_t *titles;
    int title_count;
} hs_game_t;

#define HS_GAME_COUNT 1
extern const hs_game_t HS_GAMES[HS_GAME_COUNT];

// All NULL-returning on no match.
const hs_game_t *hs_game_by_slug(const char *slug);
const hs_game_t *hs_game_by_title(uint64_t title_id, const hs_title_t **out_title);
const hs_game_t *hs_game_by_hex(const char *hex, const hs_title_t **out_title);

#endif
