// The swap engine.
//
// Layout on the SD card:
//   /mk7mods/<slug>/layeredfs/   parked, becomes /luma/titles/<TID>/
//   /mk7mods/<slug>/plugins/     parked, becomes /luma/plugins/<TID>/
//   /mk7mods/mk7swap.state       one line: active=<slug>
//   /mk7mods/mk7swap.journal     present only while a swap is in flight
//
// Nothing is ever copied. Activating a mod renames its parked directory into
// the luma slot; deactivating renames it back out. A directory rename inside
// one FAT32 volume is O(1) and atomic, so a 2 GB mod swaps as fast as a 2 KB
// one and a power cut can never leave a half-written directory.
//
// mk7_swap_to() is idempotent. That is the whole crash-recovery design: after
// an interrupted swap, re-running it against the journal's target finishes the
// job from wherever it stopped. There is no separate repair path to get wrong.
//
// The engine refuses to clobber. If both the parked copy and the live slot
// hold content, it reports SWAP_ERR_CONFLICT rather than picking a winner -
// on a stranger's SD card, guessing means destroying someone's mod setup.

#ifndef MK7SWAP_SWAP_H
#define MK7SWAP_SWAP_H

#include <stdbool.h>
#include "plat.h"

#define MK7_MAX_SLUG   48
#define MK7_MAX_MODS   32

// The SD root prefix is "sdmc:/" on hardware and a short temp path in tests.
// Bounding it well below PLAT_MAX_PATH guarantees the composed paths below can
// never be silently truncated into the wrong directory.
#define MK7_MAX_ROOT   64

// Reserved slugs.
#define MK7_SLUG_STOCK    "-"        // nothing active
#define MK7_SLUG_CTGP     "ctgp7"    // CTGP-7's plugin, parked by us
#define MK7_SLUG_EXISTING "existing" // whatever we found on first run

typedef enum {
    MK7_SLOT_LAYEREDFS = 0,
    MK7_SLOT_PLUGINS   = 1,
    MK7_SLOT_COUNT     = 2
} mk7_slot_t;

typedef enum {
    SWAP_OK = 0,
    SWAP_ERR_RENAME,    // the filesystem refused a move
    SWAP_ERR_CONFLICT,  // parked and live copies both exist - refused
    SWAP_ERR_MKDIR,
    SWAP_ERR_ARG
} mk7_result_t;

typedef struct {
    char root[MK7_MAX_ROOT];   // "sdmc:/" on hardware, a temp tree in tests
    char title_hex[17];        // resolved MK7 title id, uppercase
} mk7_ctx_t;

typedef struct {
    char slug[MK7_MAX_SLUG];
    bool has_layeredfs;  // has content for the LayeredFS slot (parked or live)
    bool has_plugins;    // has content for the plugin slot (parked or live)
    bool is_active;
} mk7_mod_t;

bool mk7_ctx_init(mk7_ctx_t *ctx, const char *root, const char *title_hex);

// Creates /mk7mods and the luma parent dirs. Safe to call every boot.
mk7_result_t mk7_ensure_layout(const mk7_ctx_t *ctx);

// First-run only: if there is no state file, record what is already live
// without moving a single file. Pre-existing mods keep working untouched.
mk7_result_t mk7_adopt_if_needed(const mk7_ctx_t *ctx);

// The mod the UI highlights. Usually owns both slots, but not always - see
// mk7_read_slot_owner.
void mk7_read_active(const mk7_ctx_t *ctx, char *out_slug, size_t cap);

// Which mod owns one slot. Normally the same as the active slug, because a
// swap gives one mod both slots. They differ on a console that already had
// CTGP-7 and a separate LayeredFS mod live at once, since those are two
// independent owners occupying two different folders. Falls back to the active
// slug when the state file predates per-slot keys or is truncated.
void mk7_read_slot_owner(const mk7_ctx_t *ctx, mk7_slot_t slot, char *out_slug, size_t cap);

// Lists parked mods plus the live one. Stock is not included - it is implicit.
int mk7_list_mods(const mk7_ctx_t *ctx, mk7_mod_t *out, int max);

// True when CTGP-7 looks installed: its SD folder exists and we can see a
// plugin for it, either live or parked.
bool mk7_ctgp_available(const mk7_ctx_t *ctx);

// The only mutating entry point. Idempotent.
mk7_result_t mk7_swap_to(const mk7_ctx_t *ctx, const char *slug);

// If a journal is present, finish the swap it describes. Returns SWAP_OK and
// does nothing when there is no journal.
mk7_result_t mk7_recover(const mk7_ctx_t *ctx);

const char *mk7_result_str(mk7_result_t r);

// Exposed for tests.
void mk7_slot_live_path(const mk7_ctx_t *ctx, mk7_slot_t slot, char *out, size_t cap);
void mk7_slot_parked_path(const mk7_ctx_t *ctx, const char *slug, mk7_slot_t slot, char *out, size_t cap);

#endif
