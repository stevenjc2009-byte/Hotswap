#include "swap.h"

#include <stdio.h>
#include <string.h>

static const char *SLOT_SUBDIR[MK7_SLOT_COUNT] = { "layeredfs", "plugins" };
static const char *SLOT_LIVE_PARENT[MK7_SLOT_COUNT] = { "luma/titles", "luma/plugins" };
static const char *SLOT_KEY[MK7_SLOT_COUNT] = { "layeredfs=", "plugins=" };

#define MODS_DIR    "mk7mods"
#define STATE_FILE  "mk7mods/mk7swap.state"
#define JOURNAL_FILE "mk7mods/mk7swap.journal"
#define CTGP_DIR    "CTGP-7"

static void joinp(char *out, size_t cap, const char *root, const char *rel)
{
    snprintf(out, cap, "%s%s", root, rel);
}

static bool is_stock(const char *slug)
{
    return !slug || slug[0] == 0 || strcmp(slug, MK7_SLUG_STOCK) == 0;
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

void mk7_slot_live_path(const mk7_ctx_t *ctx, mk7_slot_t slot, char *out, size_t cap)
{
    snprintf(out, cap, "%s%s/%s", ctx->root, SLOT_LIVE_PARENT[slot], ctx->title_hex);
}

void mk7_slot_parked_path(const mk7_ctx_t *ctx, const char *slug, mk7_slot_t slot,
                          char *out, size_t cap)
{
    snprintf(out, cap, "%s%s/%s/%s", ctx->root, MODS_DIR, slug, SLOT_SUBDIR[slot]);
}

bool mk7_ctx_init(mk7_ctx_t *ctx, const char *root, const char *title_hex)
{
    if (!ctx || !root || !title_hex) return false;
    if (strlen(title_hex) != 16) return false;

    size_t rl = strlen(root);
    if (rl == 0 || rl + 2 >= MK7_MAX_ROOT) return false;

    snprintf(ctx->root, sizeof(ctx->root), "%s", root);
    // Every path builder assumes the root already ends in a separator.
    if (ctx->root[rl - 1] != '/') {
        ctx->root[rl] = '/';
        ctx->root[rl + 1] = 0;
    }
    snprintf(ctx->title_hex, sizeof(ctx->title_hex), "%s", title_hex);
    for (char *p = ctx->title_hex; *p; p++) {
        if (*p >= 'a' && *p <= 'z') *p -= 32;
    }
    return true;
}

mk7_result_t mk7_ensure_layout(const mk7_ctx_t *ctx)
{
    char p[PLAT_MAX_PATH];

    joinp(p, sizeof(p), ctx->root, MODS_DIR);
    if (!plat_mkdir_p(p)) return SWAP_ERR_MKDIR;

    for (int s = 0; s < MK7_SLOT_COUNT; s++) {
        joinp(p, sizeof(p), ctx->root, SLOT_LIVE_PARENT[s]);
        if (!plat_mkdir_p(p)) return SWAP_ERR_MKDIR;
    }
    return SWAP_OK;
}

// Records who owns each slot as well as which mod the UI highlights. The two
// only diverge on a console we adopted, where CTGP-7's plugin and someone
// else's LayeredFS mod were both already live.
static void write_state_slots(const mk7_ctx_t *ctx, const char *active,
                              const char *owner_fs, const char *owner_pl)
{
    char p[PLAT_MAX_PATH], line[(MK7_MAX_SLUG + 16) * 3];
    joinp(p, sizeof(p), ctx->root, STATE_FILE);
    snprintf(line, sizeof(line), "active=%s\nlayeredfs=%s\nplugins=%s\n",
             is_stock(active)   ? MK7_SLUG_STOCK : active,
             is_stock(owner_fs) ? MK7_SLUG_STOCK : owner_fs,
             is_stock(owner_pl) ? MK7_SLUG_STOCK : owner_pl);
    plat_write_file(p, line, strlen(line));
}

// After a swap one mod owns everything, so the common case writes all three
// keys the same.
static void write_state(const mk7_ctx_t *ctx, const char *slug)
{
    write_state_slots(ctx, slug, slug, slug);
}

static bool state_exists(const mk7_ctx_t *ctx)
{
    char p[PLAT_MAX_PATH];
    joinp(p, sizeof(p), ctx->root, STATE_FILE);
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

void mk7_read_active(const mk7_ctx_t *ctx, char *out_slug, size_t cap)
{
    char p[PLAT_MAX_PATH], buf[256];
    snprintf(out_slug, cap, "%s", MK7_SLUG_STOCK);

    joinp(p, sizeof(p), ctx->root, STATE_FILE);
    if (plat_read_file(p, buf, sizeof(buf)) < 0) return;

    read_kv(buf, "active=", out_slug, cap);
}

void mk7_read_slot_owner(const mk7_ctx_t *ctx, mk7_slot_t slot, char *out_slug, size_t cap)
{
    char p[PLAT_MAX_PATH], buf[256];

    // A state file with no per-slot keys meant "one mod owns both", so the
    // active slug is the right answer for it.
    mk7_read_active(ctx, out_slug, cap);

    joinp(p, sizeof(p), ctx->root, STATE_FILE);
    if (plat_read_file(p, buf, sizeof(buf)) < 0) return;

    read_kv(buf, SLOT_KEY[slot], out_slug, cap);
}

static void write_journal(const mk7_ctx_t *ctx, const char *from, const char *to)
{
    char p[PLAT_MAX_PATH], buf[256];
    joinp(p, sizeof(p), ctx->root, JOURNAL_FILE);
    snprintf(buf, sizeof(buf), "v1\nfrom=%s\nto=%s\n",
             is_stock(from) ? MK7_SLUG_STOCK : from,
             is_stock(to) ? MK7_SLUG_STOCK : to);
    plat_write_file(p, buf, strlen(buf));
}

static void clear_journal(const mk7_ctx_t *ctx)
{
    char p[PLAT_MAX_PATH];
    joinp(p, sizeof(p), ctx->root, JOURNAL_FILE);
    if (plat_exists(p)) plat_unlink(p);
}

static bool read_journal_target(const mk7_ctx_t *ctx, char *out, size_t cap)
{
    char p[PLAT_MAX_PATH], buf[256];
    joinp(p, sizeof(p), ctx->root, JOURNAL_FILE);
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
static mk7_result_t park_slot(const mk7_ctx_t *ctx, const char *owner, mk7_slot_t slot)
{
    char live[PLAT_MAX_PATH], parked[PLAT_MAX_PATH], parent[PLAT_MAX_PATH];

    mk7_slot_live_path(ctx, slot, live, sizeof(live));
    if (!plat_exists(live)) return SWAP_OK;

    if (!has_content(live)) {
        // Empty directory left behind by a previous swap - just drop it.
        plat_rmdir(live);
        return SWAP_OK;
    }

    // Content is live but the state file says stock. That means someone put it
    // there outside this app, so it belongs to the adopted "existing" entry
    // rather than to nobody.
    const char *own = is_stock(owner) ? MK7_SLUG_EXISTING : owner;

    mk7_slot_parked_path(ctx, own, slot, parked, sizeof(parked));
    if (has_content(parked)) return SWAP_ERR_CONFLICT;
    if (plat_exists(parked)) plat_rmdir(parked);

    snprintf(parent, sizeof(parent), "%s%s/%s", ctx->root, MODS_DIR, own);
    if (!plat_mkdir_p(parent)) return SWAP_ERR_MKDIR;

    if (!plat_rename(live, parked)) return SWAP_ERR_RENAME;
    return SWAP_OK;
}

static mk7_result_t activate_slot(const mk7_ctx_t *ctx, const char *slug, mk7_slot_t slot)
{
    char live[PLAT_MAX_PATH], parked[PLAT_MAX_PATH], parent[PLAT_MAX_PATH];

    if (is_stock(slug)) return SWAP_OK;

    mk7_slot_parked_path(ctx, slug, slot, parked, sizeof(parked));
    mk7_slot_live_path(ctx, slot, live, sizeof(live));

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

mk7_result_t mk7_swap_to(const mk7_ctx_t *ctx, const char *slug)
{
    if (!ctx || !slug) return SWAP_ERR_ARG;

    char cur[MK7_MAX_SLUG];
    mk7_read_active(ctx, cur, sizeof(cur));

    const char *target = is_stock(slug) ? MK7_SLUG_STOCK : slug;

    mk7_result_t rc = mk7_ensure_layout(ctx);
    if (rc != SWAP_OK) return rc;

    write_journal(ctx, cur, target);

    // Park first, so the live slots are empty before anything moves in. Skipped
    // when we are already on the target, otherwise a replay would pointlessly
    // move the target's own content out and straight back in.
    if (strcmp(cur, target) != 0) {
        for (int s = 0; s < MK7_SLOT_COUNT; s++) {
            // Each slot goes back to whoever actually owns it. Reading this
            // inside the loop is safe: no state is written until every slot is
            // parked, so a slot still names its true owner when its turn comes.
            char owner[MK7_MAX_SLUG];
            mk7_read_slot_owner(ctx, (mk7_slot_t)s, owner, sizeof(owner));
            rc = park_slot(ctx, owner, (mk7_slot_t)s);
            if (rc != SWAP_OK) return rc;
        }
        // Everything is parked: stock really is the live configuration now, so
        // record that before touching anything else. A crash here leaves the
        // console on stock rather than on a lie.
        write_state(ctx, MK7_SLUG_STOCK);
    }

    // Claim ownership before moving content in, so an interrupted activate is
    // resumed as "finish activating target" and never as "park target away".
    write_state(ctx, target);

    for (int s = 0; s < MK7_SLOT_COUNT; s++) {
        rc = activate_slot(ctx, target, (mk7_slot_t)s);
        if (rc != SWAP_OK) return rc;
    }

    clear_journal(ctx);
    return SWAP_OK;
}

mk7_result_t mk7_recover(const mk7_ctx_t *ctx)
{
    char target[MK7_MAX_SLUG];
    if (!read_journal_target(ctx, target, sizeof(target))) return SWAP_OK;
    return mk7_swap_to(ctx, target);
}

mk7_result_t mk7_adopt_if_needed(const mk7_ctx_t *ctx)
{
    if (state_exists(ctx)) return SWAP_OK;

    char live_fs[PLAT_MAX_PATH], live_pl[PLAT_MAX_PATH], ctgp[PLAT_MAX_PATH];
    mk7_slot_live_path(ctx, MK7_SLOT_LAYEREDFS, live_fs, sizeof(live_fs));
    mk7_slot_live_path(ctx, MK7_SLOT_PLUGINS, live_pl, sizeof(live_pl));
    joinp(ctgp, sizeof(ctgp), ctx->root, CTGP_DIR);

    bool any_fs = has_content(live_fs);
    bool any_pl = has_content(live_pl);

    // The two slots are adopted independently, because they can legitimately
    // belong to two different things at once: CTGP-7 lives in the plugin slot
    // and a LayeredFS mod lives in the other, and neither knows about the
    // other. Collapsing them into a single owner used to hide an installed
    // CTGP-7 behind a greyed-out button.
    const char *owner_fs = any_fs ? MK7_SLUG_EXISTING : MK7_SLUG_STOCK;
    const char *owner_pl = MK7_SLUG_STOCK;
    if (any_pl) {
        // A live plugin next to a CTGP-7 install is CTGP-7. Without that
        // folder it is somebody else's plugin and gets the generic name.
        owner_pl = plat_exists(ctgp) ? MK7_SLUG_CTGP : MK7_SLUG_EXISTING;
    }

    // Highlight the LayeredFS mod if there is one, since that is what the user
    // thinks of as "my mod"; otherwise highlight whatever owns the plugin.
    const char *active = !is_stock(owner_fs) ? owner_fs : owner_pl;

    const char *owners[2] = { owner_fs, owner_pl };
    for (int i = 0; i < 2; i++) {
        if (is_stock(owners[i])) continue;
        if (i == 1 && strcmp(owners[0], owners[1]) == 0) continue;
        char dir[PLAT_MAX_PATH];
        snprintf(dir, sizeof(dir), "%s%s/%s", ctx->root, MODS_DIR, owners[i]);
        if (!plat_mkdir_p(dir)) return SWAP_ERR_MKDIR;
    }

    write_state_slots(ctx, active, owner_fs, owner_pl);
    return SWAP_OK;
}

bool mk7_ctgp_available(const mk7_ctx_t *ctx)
{
    char ctgp[PLAT_MAX_PATH];
    joinp(ctgp, sizeof(ctgp), ctx->root, CTGP_DIR);
    if (!plat_exists(ctgp)) return false;

    // The folder alone is not enough - an uninstall can leave it behind. There
    // must also be a plugin somewhere, either live or parked under our slug.
    // This asks who owns the plugin slot, not which button is highlighted:
    // CTGP-7 can be live while a separate LayeredFS mod is the active one.
    char owner[MK7_MAX_SLUG];
    mk7_read_slot_owner(ctx, MK7_SLOT_PLUGINS, owner, sizeof(owner));

    char p[PLAT_MAX_PATH];
    if (strcmp(owner, MK7_SLUG_CTGP) == 0) {
        mk7_slot_live_path(ctx, MK7_SLOT_PLUGINS, p, sizeof(p));
        if (has_content(p)) return true;
    }
    mk7_slot_parked_path(ctx, MK7_SLUG_CTGP, MK7_SLOT_PLUGINS, p, sizeof(p));
    return has_content(p);
}

int mk7_list_mods(const mk7_ctx_t *ctx, mk7_mod_t *out, int max)
{
    char dir[PLAT_MAX_PATH];
    char names[PLAT_MAX_ENTRIES][PLAT_MAX_NAME];

    joinp(dir, sizeof(dir), ctx->root, MODS_DIR);
    int n = plat_list_dir(dir, names, PLAT_MAX_ENTRIES);
    if (n < 0) return 0;

    char active[MK7_MAX_SLUG];
    mk7_read_active(ctx, active, sizeof(active));

    int count = 0;
    for (int i = 0; i < n && count < max; i++) {
        // Truncating a name would produce a slug that no longer points at a
        // real folder, so the mod would list but never swap. Skip instead.
        size_t nlen = strlen(names[i]);
        if (nlen >= MK7_MAX_SLUG) continue;

        char sub[PLAT_MAX_PATH];
        if (snprintf(sub, sizeof(sub), "%s/%s", dir, names[i]) >= (int)sizeof(sub)) continue;
        if (!plat_is_dir(sub)) continue;  // skips the state and journal files

        mk7_mod_t *m = &out[count];
        memcpy(m->slug, names[i], nlen + 1);
        m->is_active = (strcmp(names[i], active) == 0);

        for (int s = 0; s < MK7_SLOT_COUNT; s++) {
            char p[PLAT_MAX_PATH], owner[MK7_MAX_SLUG];
            // Ask per slot rather than trusting is_active, or a mod that owns
            // only the plugin slot would look like it has no content at all.
            mk7_read_slot_owner(ctx, (mk7_slot_t)s, owner, sizeof(owner));

            bool live = false;
            if (strcmp(names[i], owner) == 0) {
                mk7_slot_live_path(ctx, (mk7_slot_t)s, p, sizeof(p));
                live = has_content(p);
            }
            mk7_slot_parked_path(ctx, names[i], (mk7_slot_t)s, p, sizeof(p));
            bool parked = has_content(p);

            if (s == MK7_SLOT_LAYEREDFS) m->has_layeredfs = live || parked;
            else                          m->has_plugins   = live || parked;
        }
        count++;
    }
    return count;
}

const char *mk7_result_str(mk7_result_t r)
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
