// The swap engine.
//
// Layout on the SD card, one subtree per game:
//   /hotswap/<game>/<slug>/layeredfs/  parked, becomes /luma/titles/<TID>/
//   /hotswap/<game>/<slug>/plugins/    parked, becomes /luma/plugins/<TID>/
//   /hotswap/<game>/state              active= plus a per-slot owner line
//   /hotswap/<game>/journal            present only while a swap is in flight
//
// Games are fully independent: each has its own state, its own journal and its
// own mods, so swapping Mario Kart cannot disturb anything else.
//
// Nothing is ever copied. Activating a mod renames its parked directory into
// the luma slot; deactivating renames it back out. A directory rename inside
// one FAT32 volume is O(1) and atomic, so a 2 GB mod swaps as fast as a 2 KB
// one and a power cut can never leave a half-written directory.
//
// hs_swap_to() is idempotent. That is the whole crash-recovery design: after
// an interrupted swap, re-running it against the journal's target finishes the
// job from wherever it stopped. There is no separate repair path to get wrong.
//
// The engine refuses to clobber. If both the parked copy and the live slot
// hold content, it reports SWAP_ERR_CONFLICT rather than picking a winner -
// on a stranger's SD card, guessing means destroying someone's mod setup.

#ifndef HOTSWAP_SWAP_H
#define HOTSWAP_SWAP_H

#include <stdbool.h>
#include "plat.h"
#include "games.h"

#define HS_MAX_GAME   32

#define HS_MAX_SLUG   48
#define HS_MAX_MODS   32

// The SD root prefix is "sdmc:/" on hardware and a short temp path in tests.
// Bounding it well below PLAT_MAX_PATH guarantees the composed paths below can
// never be silently truncated into the wrong directory.
#define HS_MAX_ROOT   64

// Reserved slugs.
#define HS_SLUG_STOCK    "-"        // nothing active
#define HS_SLUG_CTGP     "ctgp7"    // CTGP-7's plugin, parked by us
#define HS_SLUG_EXISTING "existing" // whatever we found on first run

typedef enum {
    HS_SLOT_LAYEREDFS = 0,
    HS_SLOT_PLUGINS   = 1,
    HS_SLOT_COUNT     = 2
} hs_slot_t;

typedef enum {
    SWAP_OK = 0,
    SWAP_ERR_RENAME,    // the filesystem refused a move
    SWAP_ERR_CONFLICT,  // parked and live copies both exist - refused
    SWAP_ERR_MKDIR,
    SWAP_ERR_ARG
} hs_result_t;

typedef struct {
    char root[HS_MAX_ROOT];    // "sdmc:/" on hardware, a temp tree in tests
    char game[HS_MAX_GAME];    // registry slug, e.g. "mk7" - names the subtree
    char title_hex[17];        // resolved title id for this game, uppercase

    // Registry entry for `game`, or NULL when the slug is not one Hotswap
    // ships with. Unknown games still swap - they just have no display name
    // and no known community mod.
    const hs_game_t *def;
} hs_ctx_t;

typedef struct {
    char slug[HS_MAX_SLUG];
    bool has_layeredfs;  // has content for the LayeredFS slot (parked or live)
    bool has_plugins;    // has content for the plugin slot (parked or live)
    bool is_active;
} hs_mod_t;

bool hs_ctx_init(hs_ctx_t *ctx, const char *root, const char *game, const char *title_hex);

// One-time move of the v1 single-game layout into the per-game one:
//   /mk7mods/  ->  /hotswap/mk7/
//   mk7swap.state / mk7swap.journal  ->  state / journal
// Three renames, each atomic. Does nothing when there is no legacy tree, or
// when /hotswap/mk7/ already exists - so it is safe to call every boot.
hs_result_t hs_migrate_legacy(const char *root);

// Creates /hotswap/<game>/ and the luma parent dirs. Safe to call every boot.
hs_result_t hs_ensure_layout(const hs_ctx_t *ctx);

// First-run only: if there is no state file, record what is already live
// without moving a single file. Pre-existing mods keep working untouched.
hs_result_t hs_adopt_if_needed(const hs_ctx_t *ctx);

// The mod the UI highlights. Usually owns both slots, but not always - see
// hs_read_slot_owner.
void hs_read_active(const hs_ctx_t *ctx, char *out_slug, size_t cap);

// Which mod owns one slot. Normally the same as the active slug, because a
// swap gives one mod both slots. They differ on a console that already had
// CTGP-7 and a separate LayeredFS mod live at once, since those are two
// independent owners occupying two different folders. Falls back to the active
// slug when the state file predates per-slot keys or is truncated.
void hs_read_slot_owner(const hs_ctx_t *ctx, hs_slot_t slot, char *out_slug, size_t cap);

// Lists parked mods plus the live one. Stock is not included - it is implicit.
int hs_list_mods(const hs_ctx_t *ctx, hs_mod_t *out, int max);

// True when this game's well-known community mod looks installed: its SD
// folder exists and we can see a plugin for it, either live or parked. Always
// false for a game that has no community mod, or one not in the registry.
bool hs_community_available(const hs_ctx_t *ctx);

// The only mutating entry point. Idempotent.
hs_result_t hs_swap_to(const hs_ctx_t *ctx, const char *slug);

// If a journal is present, finish the swap it describes. Returns SWAP_OK and
// does nothing when there is no journal.
hs_result_t hs_recover(const hs_ctx_t *ctx);

const char *hs_result_str(hs_result_t r);

// Exposed for tests.
void hs_slot_live_path(const hs_ctx_t *ctx, hs_slot_t slot, char *out, size_t cap);
void hs_slot_parked_path(const hs_ctx_t *ctx, const char *slug, hs_slot_t slot, char *out, size_t cap);

#endif
