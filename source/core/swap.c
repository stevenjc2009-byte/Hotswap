#include "swap.h"

#include <stdio.h>
#include <string.h>

static const char *SLOT_SUBDIR[HS_SLOT_COUNT] = { "layeredfs", "plugins" };
static const char *SLOT_LIVE_PARENT[HS_SLOT_COUNT] = { "luma/titles", "luma/plugins" };
static const char *SLOT_KEY[HS_SLOT_COUNT] = { "layeredfs=", "plugins=" };

// Everything Hotswap owns lives under one folder, one subtree per game.
#define ROOT_DIR "hotswap"

// The v1 layout, migrated away from on first boot. Kept only so the migration
// knows what to look for.
#define LEGACY_MODS_DIR "mk7mods"
#define LEGACY_GAME     "mk7"

static void joinp(char *out, size_t cap, const char *root, const char *rel)
{
    snprintf(out, cap, "%s%s", root, rel);
}

// /<root>/hotswap/<game> - this game's whole world: its mods, its state, its
// journal. Nothing outside it is touched by a swap.
static void game_dir(const hs_ctx_t *ctx, char *out, size_t cap)
{
    snprintf(out, cap, "%s%s/%s", ctx->root, ROOT_DIR, ctx->game);
}

static void state_path(const hs_ctx_t *ctx, char *out, size_t cap)
{
    snprintf(out, cap, "%s%s/%s/state", ctx->root, ROOT_DIR, ctx->game);
}

static void journal_path(const hs_ctx_t *ctx, char *out, size_t cap)
{
    snprintf(out, cap, "%s%s/%s/journal", ctx->root, ROOT_DIR, ctx->game);
}

static void mod_dir(const hs_ctx_t *ctx, const char *slug, char *out, size_t cap)
{
    snprintf(out, cap, "%s%s/%s/%s", ctx->root, ROOT_DIR, ctx->game, slug);
}

// True when this game has a known community mod and that mod's own install
// folder is on the card. Only tells us it was installed, not that its plugin is
// still there - hs_community_available checks that part.
static bool community_installed(const hs_ctx_t *ctx)
{
    if (!ctx->def || !ctx->def->community_dir) return false;
    char p[PLAT_MAX_PATH];
    joinp(p, sizeof(p), ctx->root, ctx->def->community_dir);
    return plat_exists(p);
}

static bool is_stock(const char *slug)
{
    return !slug || slug[0] == 0 || strcmp(slug, HS_SLUG_STOCK) == 0;
}

// A directory counts as holding content only if it exists AND is non-empty.
// An empty leftover dir must not block a swap or it would strand the user.
static bool has_content(const char *path)
{
    if (!plat_exists(path) || !plat_is_dir(path)) return false;
    char names[PLAT_MAX_ENTRIES][PLAT_MAX_NAME];
    int n = plat_list_dir(path, names, PLAT_MAX_ENTRIES);
    return n > 0;
}

void hs_slot_live_path(const hs_ctx_t *ctx, hs_slot_t slot, char *out, size_t cap)
{
    snprintf(out, cap, "%s%s/%s", ctx->root, SLOT_LIVE_PARENT[slot], ctx->title_hex);
}

void hs_slot_parked_path(const hs_ctx_t *ctx, const char *slug, hs_slot_t slot,
                          char *out, size_t cap)
{
    snprintf(out, cap, "%s%s/%s/%s/%s", ctx->root, ROOT_DIR, ctx->game, slug,
             SLOT_SUBDIR[slot]);
}

bool hs_ctx_init(hs_ctx_t *ctx, const char *root, const char *game, const char *title_hex)
{
    if (!ctx || !root || !game || !title_hex) return false;
    if (strlen(title_hex) != 16) return false;

    size_t gl = strlen(game);
    if (gl == 0 || gl >= HS_MAX_GAME) return false;

    size_t rl = strlen(root);
    if (rl == 0 || rl + 2 >= HS_MAX_ROOT) return false;

    snprintf(ctx->root, sizeof(ctx->root), "%s", root);
    // Every path builder assumes the root already ends in a separator.
    if (ctx->root[rl - 1] != '/') {
        ctx->root[rl] = '/';
        ctx->root[rl + 1] = 0;
    }
    memcpy(ctx->game, game, gl + 1);

    snprintf(ctx->title_hex, sizeof(ctx->title_hex), "%s", title_hex);
    for (char *p = ctx->title_hex; *p; p++) {
        if (*p >= 'a' && *p <= 'z') *p -= 32;
    }

    // NULL is fine and means "a game we do not ship knowledge of". It still
    // swaps; it just has no display name and no known community mod.
    ctx->def = hs_game_by_slug(ctx->game);
    return true;
}

hs_result_t hs_migrate_legacy(const char *root)
{
    if (!root) return SWAP_ERR_ARG;

    char sep[HS_MAX_ROOT];
    size_t rl = strlen(root);
    if (rl == 0 || rl + 2 >= HS_MAX_ROOT) return SWAP_ERR_ARG;
    snprintf(sep, sizeof(sep), "%s", root);
    if (sep[rl - 1] != '/') { sep[rl] = '/'; sep[rl + 1] = 0; }

    // dest is deliberately narrower than PLAT_MAX_PATH: it is only ever the
    // bounded root plus two fixed names, and the tighter bound is what proves
    // the two filenames appended to it below cannot overflow.
    char legacy[PLAT_MAX_PATH], parent[PLAT_MAX_PATH];
    char dest[HS_MAX_ROOT + 32];
    joinp(legacy, sizeof(legacy), sep, LEGACY_MODS_DIR);
    snprintf(parent, sizeof(parent), "%s%s", sep, ROOT_DIR);
    snprintf(dest, sizeof(dest), "%s%s/%s", sep, ROOT_DIR, LEGACY_GAME);

    // Nothing to migrate, or it already happened. Either way this is a no-op,
    // which is what makes it safe to call on every boot.
    if (!plat_is_dir(legacy)) return SWAP_OK;
    if (plat_exists(dest)) return SWAP_OK;

    if (!plat_mkdir_p(parent)) return SWAP_ERR_MKDIR;

    // The whole mod tree moves in one atomic rename, so a power cut lands on
    // either the old layout or the new one and never on half of each.
    if (!plat_rename(legacy, dest)) return SWAP_ERR_RENAME;

    // Then the two bookkeeping files, which lost their mk7swap prefix now that
    // the folder they sit in already names the game. Their absence is normal -
    // the journal only exists mid-swap, and the state file only after a swap.
    char from[PLAT_MAX_PATH], to[PLAT_MAX_PATH];
    const char *old_names[2] = { "mk7swap.state", "mk7swap.journal" };
    const char *new_names[2] = { "state", "journal" };
    for (int i = 0; i < 2; i++) {
        snprintf(from, sizeof(from), "%s/%s", dest, old_names[i]);
        snprintf(to, sizeof(to), "%s/%s", dest, new_names[i]);
        if (!plat_exists(from)) continue;
        if (!plat_rename(from, to)) return SWAP_ERR_RENAME;
    }
    return SWAP_OK;
}

hs_result_t hs_ensure_layout(const hs_ctx_t *ctx)
{
    char p[PLAT_MAX_PATH];

    game_dir(ctx, p, sizeof(p));
    if (!plat_mkdir_p(p)) return SWAP_ERR_MKDIR;

    for (int s = 0; s < HS_SLOT_COUNT; s++) {
        joinp(p, sizeof(p), ctx->root, SLOT_LIVE_PARENT[s]);
        if (!plat_mkdir_p(p)) return SWAP_ERR_MKDIR;
    }
    return SWAP_OK;
}

// Records who owns each slot as well as which mod the UI highlights. The two
// only diverge on a console we adopted, where CTGP-7's plugin and someone
// else's LayeredFS mod were both already live.
static void write_state_slots(const hs_ctx_t *ctx, const char *active,
                              const char *owner_fs, const char *owner_pl)
{
    char p[PLAT_MAX_PATH], line[(HS_MAX_SLUG + 16) * 3];
    state_path(ctx, p, sizeof(p));
    snprintf(line, sizeof(line), "active=%s\nlayeredfs=%s\nplugins=%s\n",
             is_stock(active)   ? HS_SLUG_STOCK : active,
             is_stock(owner_fs) ? HS_SLUG_STOCK : owner_fs,
             is_stock(owner_pl) ? HS_SLUG_STOCK : owner_pl);
    plat_write_file(p, line, strlen(line));
}

// After a swap one mod owns everything, so the common case writes all three
// keys the same.
static void write_state(const hs_ctx_t *ctx, const char *slug)
{
    write_state_slots(ctx, slug, slug, slug);
}

static bool state_exists(const hs_ctx_t *ctx)
{
    char p[PLAT_MAX_PATH];
    state_path(ctx, p, sizeof(p));
    return plat_exists(p);
}

// Reads one `key=value` line out of an already-loaded buffer. Leaves `out`
// untouched and returns false when the key is missing or its value is empty,
// so callers can pre-seed a fallback.
static bool read_kv(const char *buf, const char *key, char *out, size_t cap)
{
    const char *v = strstr(buf, key);
    if (!v) return false;
    v += strlen(key);

    size_t n = 0;
    while (v[n] && v[n] != '\n' && v[n] != '\r' && n + 1 < cap) n++;
    if (n == 0) return false;

    memcpy(out, v, n);
    out[n] = 0;
    return true;
}

void hs_read_active(const hs_ctx_t *ctx, char *out_slug, size_t cap)
{
    char p[PLAT_MAX_PATH], buf[256];
    snprintf(out_slug, cap, "%s", HS_SLUG_STOCK);

    state_path(ctx, p, sizeof(p));
    if (plat_read_file(p, buf, sizeof(buf)) < 0) return;

    read_kv(buf, "active=", out_slug, cap);
}

void hs_read_slot_owner(const hs_ctx_t *ctx, hs_slot_t slot, char *out_slug, size_t cap)
{
    char p[PLAT_MAX_PATH], buf[256];

    // A state file with no per-slot keys meant "one mod owns both", so the
    // active slug is the right answer for it.
    hs_read_active(ctx, out_slug, cap);

    state_path(ctx, p, sizeof(p));
    if (plat_read_file(p, buf, sizeof(buf)) < 0) return;

    read_kv(buf, SLOT_KEY[slot], out_slug, cap);
}

static void write_journal(const hs_ctx_t *ctx, const char *from, const char *to)
{
    char p[PLAT_MAX_PATH], buf[256];
    journal_path(ctx, p, sizeof(p));
    snprintf(buf, sizeof(buf), "v1\nfrom=%s\nto=%s\n",
             is_stock(from) ? HS_SLUG_STOCK : from,
             is_stock(to) ? HS_SLUG_STOCK : to);
    plat_write_file(p, buf, strlen(buf));
}

static void clear_journal(const hs_ctx_t *ctx)
{
    char p[PLAT_MAX_PATH];
    journal_path(ctx, p, sizeof(p));
    if (plat_exists(p)) plat_unlink(p);
}

static bool read_journal_target(const hs_ctx_t *ctx, char *out, size_t cap)
{
    char p[PLAT_MAX_PATH], buf[256];
    journal_path(ctx, p, sizeof(p));
    if (plat_read_file(p, buf, sizeof(buf)) < 0) return false;

    const char *v = strstr(buf, "to=");
    if (!v) return false;
    v += 3;

    size_t n = 0;
    while (v[n] && v[n] != '\n' && v[n] != '\r' && n + 1 < cap) n++;
    if (n == 0) return false;

    memcpy(out, v, n);
    out[n] = 0;
    return true;
}

// Move whatever is live in this slot back under its owner's parked directory.
// No-op when the slot is already empty, which is what makes replay safe.
static hs_result_t park_slot(const hs_ctx_t *ctx, const char *owner, hs_slot_t slot)
{
    char live[PLAT_MAX_PATH], parked[PLAT_MAX_PATH], parent[PLAT_MAX_PATH];

    hs_slot_live_path(ctx, slot, live, sizeof(live));
    if (!plat_exists(live)) return SWAP_OK;

    if (!has_content(live)) {
        // Empty directory left behind by a previous swap - just drop it.
        plat_rmdir(live);
        return SWAP_OK;
    }

    // Content is live but the state file says stock. That means someone put it
    // there outside this app, so it belongs to the adopted "existing" entry
    // rather than to nobody.
    const char *own = is_stock(owner) ? HS_SLUG_EXISTING : owner;

    hs_slot_parked_path(ctx, own, slot, parked, sizeof(parked));
    if (has_content(parked)) return SWAP_ERR_CONFLICT;
    if (plat_exists(parked)) plat_rmdir(parked);

    mod_dir(ctx, own, parent, sizeof(parent));
    if (!plat_mkdir_p(parent)) return SWAP_ERR_MKDIR;

    if (!plat_rename(live, parked)) return SWAP_ERR_RENAME;
    return SWAP_OK;
}

static hs_result_t activate_slot(const hs_ctx_t *ctx, const char *slug, hs_slot_t slot)
{
    char live[PLAT_MAX_PATH], parked[PLAT_MAX_PATH], parent[PLAT_MAX_PATH];

    if (is_stock(slug)) return SWAP_OK;

    hs_slot_parked_path(ctx, slug, slot, parked, sizeof(parked));
    hs_slot_live_path(ctx, slot, live, sizeof(live));

    bool src = has_content(parked);
    bool dst = has_content(live);

    if (!src && dst) return SWAP_OK;  // replay: already activated
    if (!src) return SWAP_OK;         // this mod simply has nothing for this slot
    if (dst) return SWAP_ERR_CONFLICT;

    if (plat_exists(live)) plat_rmdir(live);

    snprintf(parent, sizeof(parent), "%s%s", ctx->root, SLOT_LIVE_PARENT[slot]);
    if (!plat_mkdir_p(parent)) return SWAP_ERR_MKDIR;

    if (!plat_rename(parked, live)) return SWAP_ERR_RENAME;
    return SWAP_OK;
}

// True when `slug` actually holds something for this slot - parked under its
// own name, or already live and recorded as its own. A mod that has nothing for
// a slot has no business being written down as that slot's owner.
static bool owns_content(const hs_ctx_t *ctx, const char *slug, hs_slot_t slot)
{
    char p[PLAT_MAX_PATH];

    hs_slot_parked_path(ctx, slug, slot, p, sizeof(p));
    if (has_content(p)) return true;

    char owner[HS_MAX_SLUG];
    hs_read_slot_owner(ctx, slot, owner, sizeof(owner));
    if (strcmp(owner, slug) != 0) return false;

    hs_slot_live_path(ctx, slot, p, sizeof(p));
    return has_content(p);
}

hs_result_t hs_swap_to(const hs_ctx_t *ctx, const char *slug)
{
    if (!ctx || !slug) return SWAP_ERR_ARG;

    char cur[HS_MAX_SLUG];
    hs_read_active(ctx, cur, sizeof(cur));

    const char *target = is_stock(slug) ? HS_SLUG_STOCK : slug;

    hs_result_t rc = hs_ensure_layout(ctx);
    if (rc != SWAP_OK) return rc;

    write_journal(ctx, cur, target);

    // Park first, so the live slots are empty before anything moves in. Skipped
    // when we are already on the target, otherwise a replay would pointlessly
    // move the target's own content out and straight back in.
    if (strcmp(cur, target) != 0) {
        for (int s = 0; s < HS_SLOT_COUNT; s++) {
            // Each slot goes back to whoever actually owns it. Reading this
            // inside the loop is safe: no state is written until every slot is
            // parked, so a slot still names its true owner when its turn comes.
            char owner[HS_MAX_SLUG];
            hs_read_slot_owner(ctx, (hs_slot_t)s, owner, sizeof(owner));
            rc = park_slot(ctx, owner, (hs_slot_t)s);
            if (rc != SWAP_OK) return rc;
        }
        // Everything is parked: stock really is the live configuration now, so
        // record that before touching anything else. A crash here leaves the
        // console on stock rather than on a lie.
        write_state(ctx, HS_SLUG_STOCK);
    }

    // Claim ownership before moving content in, so an interrupted activate is
    // resumed as "finish activating target" and never as "park target away".
    //
    // Only the slots the target actually has content for change hands. When the
    // user picks the mod that is already active the park phase above was
    // skipped, so something else may still legitimately own the other slot -
    // an adopted card where CTGP-7 holds the plugin slot and the user's own mod
    // holds LayeredFS is exactly that case, and handing both to the active mod
    // would file CTGP-7's plugin away under someone else's name.
    char owner_fs[HS_MAX_SLUG], owner_pl[HS_MAX_SLUG];
    hs_read_slot_owner(ctx, HS_SLOT_LAYEREDFS, owner_fs, sizeof(owner_fs));
    hs_read_slot_owner(ctx, HS_SLOT_PLUGINS, owner_pl, sizeof(owner_pl));

    if (owns_content(ctx, target, HS_SLOT_LAYEREDFS))
        snprintf(owner_fs, sizeof(owner_fs), "%s", target);
    if (owns_content(ctx, target, HS_SLOT_PLUGINS))
        snprintf(owner_pl, sizeof(owner_pl), "%s", target);

    write_state_slots(ctx, target, owner_fs, owner_pl);

    for (int s = 0; s < HS_SLOT_COUNT; s++) {
        rc = activate_slot(ctx, target, (hs_slot_t)s);
        if (rc != SWAP_OK) return rc;
    }

    clear_journal(ctx);
    return SWAP_OK;
}

hs_result_t hs_recover(const hs_ctx_t *ctx)
{
    char target[HS_MAX_SLUG];
    if (!read_journal_target(ctx, target, sizeof(target))) return SWAP_OK;
    return hs_swap_to(ctx, target);
}

hs_result_t hs_adopt_if_needed(const hs_ctx_t *ctx)
{
    if (state_exists(ctx)) return SWAP_OK;

    char live_fs[PLAT_MAX_PATH], live_pl[PLAT_MAX_PATH];
    hs_slot_live_path(ctx, HS_SLOT_LAYEREDFS, live_fs, sizeof(live_fs));
    hs_slot_live_path(ctx, HS_SLOT_PLUGINS, live_pl, sizeof(live_pl));

    bool any_fs = has_content(live_fs);
    bool any_pl = has_content(live_pl);

    // The two slots are adopted independently, because they can legitimately
    // belong to two different things at once: CTGP-7 lives in the plugin slot
    // and a LayeredFS mod lives in the other, and neither knows about the
    // other. Collapsing them into a single owner used to hide an installed
    // CTGP-7 behind a greyed-out button.
    const char *owner_fs = any_fs ? HS_SLUG_EXISTING : HS_SLUG_STOCK;
    const char *owner_pl = HS_SLUG_STOCK;
    if (any_pl) {
        // A live plugin next to the community mod's own install folder is that
        // mod. Without the folder it is somebody else's plugin and gets the
        // generic name.
        owner_pl = community_installed(ctx) ? ctx->def->community_slug
                                            : HS_SLUG_EXISTING;
    }

    // Highlight the LayeredFS mod if there is one, since that is what the user
    // thinks of as "my mod"; otherwise highlight whatever owns the plugin.
    const char *active = !is_stock(owner_fs) ? owner_fs : owner_pl;

    const char *owners[2] = { owner_fs, owner_pl };
    for (int i = 0; i < 2; i++) {
        if (is_stock(owners[i])) continue;
        if (i == 1 && strcmp(owners[0], owners[1]) == 0) continue;
        char dir[PLAT_MAX_PATH];
        mod_dir(ctx, owners[i], dir, sizeof(dir));
        if (!plat_mkdir_p(dir)) return SWAP_ERR_MKDIR;
    }

    write_state_slots(ctx, active, owner_fs, owner_pl);
    return SWAP_OK;
}

bool hs_community_available(const hs_ctx_t *ctx)
{
    if (!community_installed(ctx)) return false;

    // A mod that keeps its plugin at its own fixed path never puts anything
    // under /luma/plugins/, so that file is the whole test. This is the case
    // that made CTGP-7 read "Not installed" on a console that plainly had it:
    // the checks below looked only where Hotswap parks things, and CTGP-7 does
    // not park anything.
    if (ctx->def->community_plugin) {
        char cp[PLAT_MAX_PATH];
        joinp(cp, sizeof(cp), ctx->root, ctx->def->community_plugin);
        if (plat_exists(cp)) return true;
    }

    // The folder alone is not enough - an uninstall can leave it behind. There
    // must also be a plugin somewhere, either live or parked under our slug.
    // This asks who owns the plugin slot, not which button is highlighted:
    // CTGP-7 can be live while a separate LayeredFS mod is the active one.
    const char *slug = ctx->def->community_slug;
    char owner[HS_MAX_SLUG];
    hs_read_slot_owner(ctx, HS_SLOT_PLUGINS, owner, sizeof(owner));

    char p[PLAT_MAX_PATH];
    if (strcmp(owner, slug) == 0) {
        hs_slot_live_path(ctx, HS_SLOT_PLUGINS, p, sizeof(p));
        if (has_content(p)) return true;
    }
    hs_slot_parked_path(ctx, slug, HS_SLOT_PLUGINS, p, sizeof(p));
    return has_content(p);
}

int hs_list_mods(const hs_ctx_t *ctx, hs_mod_t *out, int max)
{
    char dir[PLAT_MAX_PATH];
    char names[PLAT_MAX_ENTRIES][PLAT_MAX_NAME];

    game_dir(ctx, dir, sizeof(dir));
    int n = plat_list_dir(dir, names, PLAT_MAX_ENTRIES);
    if (n < 0) return 0;

    char active[HS_MAX_SLUG];
    hs_read_active(ctx, active, sizeof(active));

    int count = 0;
    for (int i = 0; i < n && count < max; i++) {
        // Truncating a name would produce a slug that no longer points at a
        // real folder, so the mod would list but never swap. Skip instead.
        size_t nlen = strlen(names[i]);
        if (nlen >= HS_MAX_SLUG) continue;

        char sub[PLAT_MAX_PATH];
        if (snprintf(sub, sizeof(sub), "%s/%s", dir, names[i]) >= (int)sizeof(sub)) continue;
        if (!plat_is_dir(sub)) continue;  // skips the state and journal files

        hs_mod_t *m = &out[count];
        memcpy(m->slug, names[i], nlen + 1);
        m->is_active = (strcmp(names[i], active) == 0);

        for (int s = 0; s < HS_SLOT_COUNT; s++) {
            char p[PLAT_MAX_PATH], owner[HS_MAX_SLUG];
            // Ask per slot rather than trusting is_active, or a mod that owns
            // only the plugin slot would look like it has no content at all.
            hs_read_slot_owner(ctx, (hs_slot_t)s, owner, sizeof(owner));

            bool live = false;
            if (strcmp(names[i], owner) == 0) {
                hs_slot_live_path(ctx, (hs_slot_t)s, p, sizeof(p));
                live = has_content(p);
            }
            hs_slot_parked_path(ctx, names[i], (hs_slot_t)s, p, sizeof(p));
            bool parked = has_content(p);

            if (s == HS_SLOT_LAYEREDFS) m->has_layeredfs = live || parked;
            else                          m->has_plugins   = live || parked;
        }
        count++;
    }
    return count;
}

const char *hs_result_str(hs_result_t r)
{
    switch (r) {
        case SWAP_OK:            return "ok";
        case SWAP_ERR_RENAME:    return "could not move a folder";
        case SWAP_ERR_CONFLICT:  return "two copies found - refusing to overwrite";
        case SWAP_ERR_MKDIR:     return "could not create a folder";
        case SWAP_ERR_ARG:       return "bad argument";
    }
    return "unknown";
}
