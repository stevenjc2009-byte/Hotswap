// Dev-only on-device self test.
//
// The host tests cover the swap logic thoroughly, but they run against stdio
// rename() on a PC. This runs the same swap cycle through the real thing -
// FSUSER_RenameDirectory against the actual SD archive - which is the one part
// the host harness physically cannot exercise.
//
// It writes sdmc:/mk7swap/selftest.log and touches nothing outside /mk7mods,
// /luma/titles/<TID> and /luma/plugins/<TID>. Compiled out of release builds.

#include "selftest.h"

#include "core/swap.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#define LOG_PATH "sdmc:/mk7swap/selftest.log"

static FILE *g_log;
static int g_pass, g_fail;

static void logf_(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    if (g_log) vfprintf(g_log, fmt, ap);
    va_end(ap);
}

static void expect(bool cond, const char *what)
{
    if (cond) { g_pass++; logf_("  PASS  %s\n", what); }
    else      { g_fail++; logf_("  FAIL  %s\n", what); }
}

static bool file_has(const mk7_ctx_t *ctx, const char *rel, const char *want)
{
    char path[PLAT_MAX_PATH], buf[256];
    snprintf(path, sizeof(path), "%s%s", ctx->root, rel);
    if (plat_read_file(path, buf, sizeof(buf)) < 0) return false;
    return strcmp(buf, want) == 0;
}

static bool slot_live_has(const mk7_ctx_t *ctx, mk7_slot_t slot, const char *leaf,
                          const char *want)
{
    char path[PLAT_MAX_PATH], buf[256];
    mk7_slot_live_path(ctx, slot, path, sizeof(path));
    strncat(path, leaf, sizeof(path) - strlen(path) - 1);
    if (plat_read_file(path, buf, sizeof(buf)) < 0) return false;
    return strcmp(buf, want) == 0;
}

static bool slot_live_empty(const mk7_ctx_t *ctx, mk7_slot_t slot)
{
    char path[PLAT_MAX_PATH];
    char names[PLAT_MAX_ENTRIES][PLAT_MAX_NAME];
    mk7_slot_live_path(ctx, slot, path, sizeof(path));
    if (!plat_exists(path)) return true;
    return plat_list_dir(path, names, PLAT_MAX_ENTRIES) <= 0;
}

int selftest_run(const mk7_ctx_t *ctx)
{
    mkdir("sdmc:/mk7swap", 0777);
    g_log = fopen(LOG_PATH, "w");
    g_pass = g_fail = 0;

    logf_("mk7swap on-device self test\n");
    logf_("root=%s title=%s\n\n", ctx->root, ctx->title_hex);

    // Start from stock so the cycle below is deterministic regardless of what
    // the card looked like when the app opened.
    mk7_result_t rc = mk7_swap_to(ctx, MK7_SLUG_STOCK);
    logf_("reset to stock: %s\n", mk7_result_str(rc));

    logf_("\n[1] stock -> CTGP-7\n");
    rc = mk7_swap_to(ctx, MK7_SLUG_CTGP);
    expect(rc == SWAP_OK, "swap returned ok");
    expect(slot_live_has(ctx, MK7_SLOT_PLUGINS, "/CTGP-7.3gx", "PLACEHOLDER-PLUGIN"),
           "CTGP plugin is live");
    expect(slot_live_empty(ctx, MK7_SLOT_LAYEREDFS), "layeredfs slot empty");

    logf_("\n[2] CTGP-7 -> custom\n");
    rc = mk7_swap_to(ctx, "custom");
    expect(rc == SWAP_OK, "swap returned ok");
    expect(slot_live_empty(ctx, MK7_SLOT_PLUGINS), "CTGP plugin completely off");
    expect(file_has(ctx, "mk7mods/ctgp7/plugins/CTGP-7.3gx", "PLACEHOLDER-PLUGIN"),
           "CTGP plugin parked intact");
    expect(!slot_live_empty(ctx, MK7_SLOT_LAYEREDFS), "custom mod is live");

    logf_("\n[3] custom -> stock\n");
    rc = mk7_swap_to(ctx, MK7_SLUG_STOCK);
    expect(rc == SWAP_OK, "swap returned ok");
    expect(slot_live_empty(ctx, MK7_SLOT_PLUGINS), "plugin slot empty on stock");
    expect(slot_live_empty(ctx, MK7_SLOT_LAYEREDFS), "layeredfs slot empty on stock");
    expect(file_has(ctx, "mk7mods/custom/layeredfs/romfs/PLACEHOLDER.txt",
                    "placeholder custom mod for testing\n"),
           "custom mod parked intact");

    logf_("\n[4] stock -> CTGP-7 again (round trip)\n");
    rc = mk7_swap_to(ctx, MK7_SLUG_CTGP);
    expect(rc == SWAP_OK, "swap returned ok");
    expect(slot_live_has(ctx, MK7_SLOT_PLUGINS, "/CTGP-7.3gx", "PLACEHOLDER-PLUGIN"),
           "CTGP live again after full cycle");

    logf_("\n[5] repeat the same choice (idempotence)\n");
    rc = mk7_swap_to(ctx, MK7_SLUG_CTGP);
    expect(rc == SWAP_OK, "repeat swap returned ok");
    expect(slot_live_has(ctx, MK7_SLOT_PLUGINS, "/CTGP-7.3gx", "PLACEHOLDER-PLUGIN"),
           "CTGP still live and intact");

    logf_("\n[6] back to custom, then leave the card on stock\n");
    rc = mk7_swap_to(ctx, "custom");
    expect(rc == SWAP_OK, "swap to custom ok");
    expect(file_has(ctx, "mk7mods/ctgp7/plugins/CTGP-7.3gx", "PLACEHOLDER-PLUGIN"),
           "CTGP parked intact on the second cycle");
    rc = mk7_swap_to(ctx, MK7_SLUG_STOCK);
    expect(rc == SWAP_OK, "swap to stock ok");

    logf_("\n%d passed, %d failed\n", g_pass, g_fail);

    if (g_log) { fclose(g_log); g_log = NULL; }
    return g_fail;
}
