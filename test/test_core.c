// Host test harness for the swap core.
//
// Runs the real engine against a real (temporary) directory tree, so renames,
// listings and the journal are all genuinely exercised. The fault-injection
// sweeps at the end interrupt live swaps at every rename boundary and assert
// that recovery finishes the job without losing a single file.

#include "../source/core/swap.h"
#include "../source/core/games.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>

#ifdef _WIN32
#include <direct.h>
#include <io.h>
#define RMDIR(p) _rmdir(p)
#define UNLINK(p) _unlink(p)
#else
#include <unistd.h>
#define RMDIR(p) rmdir(p)
#define UNLINK(p) unlink(p)
#endif

void plat_host_fail_after(int n);
void plat_host_no_faults(void);

#define ROOT "./_sdtest/"
#define TID  "0004000000030800"

static int g_checks = 0;
static int g_fails = 0;

static void check(int cond, const char *what)
{
    g_checks++;
    if (!cond) {
        g_fails++;
        printf("  FAIL: %s\n", what);
    }
}

static void check_eq_str(const char *got, const char *want, const char *what)
{
    g_checks++;
    if (strcmp(got, want) != 0) {
        g_fails++;
        printf("  FAIL: %s (got \"%s\", want \"%s\")\n", what, got, want);
    }
}

// ---------------------------------------------------------------- helpers

static void rm_rf(const char *path)
{
    struct stat st;
    if (stat(path, &st) != 0) return;

    if (st.st_mode & S_IFDIR) {
        DIR *d = opendir(path);
        if (d) {
            struct dirent *e;
            while ((e = readdir(d)) != NULL) {
                if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
                char sub[PLAT_MAX_PATH];
                snprintf(sub, sizeof(sub), "%s/%s", path, e->d_name);
                rm_rf(sub);
            }
            closedir(d);
        }
        RMDIR(path);
    } else {
        UNLINK(path);
    }
}

static void sd_reset(void)
{
    plat_host_no_faults();
    rm_rf("./_sdtest");
    plat_mkdir_p(ROOT);
}

// Creates a file (and its parents) at a path relative to the SD root.
static void mkfile(const char *rel, const char *content)
{
    char full[PLAT_MAX_PATH];
    snprintf(full, sizeof(full), "%s%s", ROOT, rel);

    char dir[PLAT_MAX_PATH];
    snprintf(dir, sizeof(dir), "%s", full);
    char *slash = strrchr(dir, '/');
    if (slash) { *slash = 0; plat_mkdir_p(dir); }

    plat_write_file(full, content, strlen(content));
}

static bool rel_exists(const char *rel)
{
    char full[PLAT_MAX_PATH];
    snprintf(full, sizeof(full), "%s%s", ROOT, rel);
    return plat_exists(full);
}

static bool rel_has(const char *rel, const char *want)
{
    char full[PLAT_MAX_PATH], buf[512];
    snprintf(full, sizeof(full), "%s%s", ROOT, rel);
    if (plat_read_file(full, buf, sizeof(buf)) < 0) return false;
    return strcmp(buf, want) == 0;
}

// Recursively counts files (not dirs) whose contents equal `want`.
static int count_content(const char *path, const char *want)
{
    struct stat st;
    if (stat(path, &st) != 0) return 0;

    if (!(st.st_mode & S_IFDIR)) {
        char buf[512];
        if (plat_read_file(path, buf, sizeof(buf)) < 0) return 0;
        return strcmp(buf, want) == 0 ? 1 : 0;
    }

    int total = 0;
    DIR *d = opendir(path);
    if (!d) return 0;
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
        char sub[PLAT_MAX_PATH];
        snprintf(sub, sizeof(sub), "%s/%s", path, e->d_name);
        total += count_content(sub, want);
    }
    closedir(d);
    return total;
}

static hs_ctx_t CTX;

static void ctx_setup(void)
{
    hs_ctx_init(&CTX, ROOT, "mk7", TID);
    hs_ensure_layout(&CTX);
}

// A tree with CTGP-7 installed and active, plus a parked custom mod.
static void fixture_ctgp_active_custom_parked(void)
{
    sd_reset();
    mkfile("CTGP-7/config.bin", "ctgp-install");
    mkfile("luma/plugins/" TID "/CTGP-7.3gx", "CTGP-PLUGIN");
    mkfile("hotswap/mk7/custom/layeredfs/romfs/course/mirror.szs", "CUSTOM-TRACK");
    ctx_setup();
    hs_adopt_if_needed(&CTX);
}

// ---------------------------------------------------------------- tests

static void t_titles(void)
{
    printf("titles\n");
    const hs_title_t *t = NULL;
    check(hs_game_by_title(0x0004000000030800ULL, &t) != NULL, "USA id known");
    check_eq_str(t->region, "Americas", "USA id names the right region");
    check(hs_game_by_title(0x0004000000030700ULL, NULL) != NULL, "EUR id known");
    check(hs_game_by_title(0x0004000000030600ULL, NULL) != NULL, "JPN id known");
    check(hs_game_by_title(0x0004000000030A00ULL, NULL) != NULL, "KOR id known");
    check(hs_game_by_title(0x0004000000123456ULL, NULL) == NULL, "unknown id rejected");
    check(hs_game_by_hex("0004000000030800", NULL) != NULL, "hex lookup");
    check(hs_game_by_hex("0004000000030a00", NULL) != NULL, "hex lookup is case-insensitive");
    check(hs_game_by_hex("nonsense", NULL) == NULL, "bad hex rejected");
}

static void t_registry(void)
{
    printf("game registry\n");
    const hs_game_t *g = hs_game_by_slug("mk7");
    check(g != NULL, "mk7 is in the registry");
    check_eq_str(g->name, "Mario Kart 7", "mk7 display name");
    check_eq_str(g->community_slug, HS_SLUG_CTGP, "mk7 community slug is ctgp7");
    check(hs_game_by_slug("nosuchgame") == NULL, "unknown slug rejected");
    check(hs_game_by_slug(NULL) == NULL, "NULL slug rejected");

    // Every shipped game must be usable as a folder name and must carry at
    // least one title id, or it would list in the UI and then swap nothing.
    for (int i = 0; i < HS_GAME_COUNT; i++) {
        const hs_game_t *e = &HS_GAMES[i];
        check(e->slug && e->slug[0] && strlen(e->slug) < HS_MAX_GAME,
              "registry slug fits a folder name");
        check(e->title_count > 0, "registry entry has at least one title id");
        check(e->name != NULL && e->short_name != NULL, "registry entry is named");
    }
}

static void t_migrate_legacy(void)
{
    printf("migration: v1 layout moves under /hotswap/mk7\n");
    sd_reset();
    mkfile("mk7mods/custom/layeredfs/romfs/course/mirror.szs", "CUSTOM-TRACK");
    mkfile("mk7mods/ctgp7/plugins/CTGP-7.3gx", "CTGP-PLUGIN");
    mkfile("mk7mods/mk7swap.state", "active=custom\n");

    check(hs_migrate_legacy(ROOT) == SWAP_OK, "migration reports success");
    check(!rel_exists("mk7mods"), "old folder is gone");
    check(rel_has("hotswap/mk7/custom/layeredfs/romfs/course/mirror.szs", "CUSTOM-TRACK"),
          "custom mod moved intact");
    check(rel_has("hotswap/mk7/ctgp7/plugins/CTGP-7.3gx", "CTGP-PLUGIN"),
          "parked CTGP plugin moved intact");
    check(rel_has("hotswap/mk7/state", "active=custom\n"), "state file renamed, contents kept");
    check(!rel_exists("hotswap/mk7/mk7swap.state"), "old state filename gone");

    // The engine must see the migrated card exactly as it saw the old one.
    ctx_setup();
    char active[HS_MAX_SLUG];
    hs_read_active(&CTX, active, sizeof(active));
    check_eq_str(active, "custom", "migrated state still names the active mod");

    printf("migration: idempotent and harmless\n");
    check(hs_migrate_legacy(ROOT) == SWAP_OK, "second run is a no-op");
    check(rel_has("hotswap/mk7/state", "active=custom\n"), "second run changed nothing");

    // A card that already has both layouts must keep the new one. Migrating on
    // top of it would either fail or bury live mods under a stale tree.
    mkfile("mk7mods/stale/layeredfs/old.szs", "STALE");
    check(hs_migrate_legacy(ROOT) == SWAP_OK, "legacy tree next to a new one is skipped");
    check(rel_has("hotswap/mk7/state", "active=custom\n"), "new layout untouched");
    check(rel_exists("mk7mods/stale"), "stale legacy tree left alone, not deleted");

    printf("migration: nothing to migrate\n");
    sd_reset();
    check(hs_migrate_legacy(ROOT) == SWAP_OK, "clean card migrates fine");
    check(!rel_exists("hotswap/mk7"), "no folder invented on a clean card");

    printf("migration: mid-swap journal survives\n");
    sd_reset();
    // Exactly what the engine writes, so recovery really has something to read.
    mkfile("mk7mods/mk7swap.journal", "v1\nfrom=-\nto=custom\n");
    mkfile("mk7mods/custom/layeredfs/a.szs", "CUSTOM-TRACK");
    check(hs_migrate_legacy(ROOT) == SWAP_OK, "migration with a journal present");
    check(rel_has("hotswap/mk7/journal", "v1\nfrom=-\nto=custom\n"),
          "journal renamed, target kept");
    ctx_setup();
    check(hs_recover(&CTX) == SWAP_OK, "recovery finishes the interrupted swap");
    check(rel_has("luma/titles/" TID "/a.szs", "CUSTOM-TRACK"), "interrupted swap completed");
    check(!rel_exists("hotswap/mk7/journal"), "journal cleared once the swap finished");
}

static void t_games_are_isolated(void)
{
    printf("multi-game: one game's swap cannot disturb another\n");
    sd_reset();

    // A second game that is not in the registry, to prove the engine is driven
    // by the slug rather than by anything it ships knowledge of.
    hs_ctx_t a, b;
    check(hs_ctx_init(&a, ROOT, "mk7", TID), "game A init");
    check(hs_ctx_init(&b, ROOT, "othergame", "000400000BADF00D"), "game B init");
    check(b.def == NULL, "unknown game has no registry entry");
    check(a.def != NULL, "known game resolves its registry entry");
    hs_ensure_layout(&a);
    hs_ensure_layout(&b);

    mkfile("hotswap/mk7/custom/layeredfs/a.szs", "A-MOD");
    mkfile("hotswap/othergame/custom/layeredfs/b.szs", "B-MOD");
    hs_adopt_if_needed(&a);
    hs_adopt_if_needed(&b);

    check(hs_swap_to(&a, "custom") == SWAP_OK, "game A swaps");
    check(rel_has("luma/titles/" TID "/a.szs", "A-MOD"), "game A mod went live");
    check(rel_has("hotswap/othergame/custom/layeredfs/b.szs", "B-MOD"),
          "game B mod still parked and untouched");

    check(hs_swap_to(&b, "custom") == SWAP_OK, "game B swaps");
    check(rel_has("luma/titles/000400000BADF00D/b.szs", "B-MOD"), "game B mod went live");
    check(rel_has("luma/titles/" TID "/a.szs", "A-MOD"), "game A mod still live after B swapped");

    // Separate state files, so "active" for one game says nothing about the other.
    check(hs_swap_to(&b, HS_SLUG_STOCK) == SWAP_OK, "game B back to stock");
    char active[HS_MAX_SLUG];
    hs_read_active(&a, active, sizeof(active));
    check_eq_str(active, "custom", "game A still active after B reverted");
    check(rel_has("luma/titles/" TID "/a.szs", "A-MOD"), "game A content untouched by B revert");

    printf("multi-game: a game with no community mod\n");
    check(!hs_community_available(&b), "unknown game reports no community mod");

    printf("multi-game: bad game slugs rejected\n");
    hs_ctx_t c;
    char longgame[HS_MAX_GAME + 8];
    memset(longgame, 'g', sizeof(longgame) - 1);
    longgame[sizeof(longgame) - 1] = 0;
    check(!hs_ctx_init(&c, ROOT, "", TID), "empty game slug rejected");
    check(!hs_ctx_init(&c, ROOT, longgame, TID), "over-long game slug rejected, not truncated");
    check(!hs_ctx_init(&c, ROOT, NULL, TID), "NULL game slug rejected");
}

static void t_ctx_init(void)
{
    printf("ctx init\n");
    hs_ctx_t c;
    check(hs_ctx_init(&c, "./x", "mk7", TID), "init accepts valid args");
    check_eq_str(c.root, "./x/", "root gains a trailing slash");

    check(hs_ctx_init(&c, "./x/", "mk7", "0004000000030a00"), "init accepts lowercase hex");
    check_eq_str(c.title_hex, "0004000000030A00", "hex is uppercased");
    check_eq_str(c.root, "./x/", "existing trailing slash not doubled");

    check(!hs_ctx_init(&c, "./x", "mk7", "TOOSHORT"), "short title id rejected");
    check(!hs_ctx_init(&c, "", "mk7", TID), "empty root rejected");

    char longroot[HS_MAX_ROOT + 8];
    memset(longroot, 'a', sizeof(longroot) - 1);
    longroot[sizeof(longroot) - 1] = 0;
    check(!hs_ctx_init(&c, longroot, "mk7", TID), "over-long root rejected, not truncated");
}

static void t_long_slug_skipped(void)
{
    printf("listing: pathological folder names\n");
    sd_reset();

    char longslug[HS_MAX_SLUG + 10];
    memset(longslug, 'z', sizeof(longslug) - 1);
    longslug[sizeof(longslug) - 1] = 0;

    char rel[PLAT_MAX_PATH];
    snprintf(rel, sizeof(rel), "hotswap/mk7/%s/layeredfs/a.szs", longslug);
    mkfile(rel, "TOO-LONG");
    mkfile("hotswap/mk7/custom/layeredfs/a.szs", "CUSTOM-TRACK");

    ctx_setup();
    hs_adopt_if_needed(&CTX);

    hs_mod_t mods[HS_MAX_MODS];
    int n = hs_list_mods(&CTX, mods, HS_MAX_MODS);
    check(n == 1, "over-long slug skipped rather than truncated");
    if (n == 1) check_eq_str(mods[0].slug, "custom", "the usable mod still lists");

    // Skipping must not delete anything.
    check(count_content(ROOT, "TOO-LONG") == 1, "skipped folder left untouched");
}

static void t_layout(void)
{
    printf("layout\n");
    sd_reset();
    ctx_setup();
    check(rel_exists("hotswap/mk7"), "per-game folder created");
    check(rel_exists("luma/titles"), "luma/titles created");
    check(rel_exists("luma/plugins"), "luma/plugins created");

    check(hs_ensure_layout(&CTX) == SWAP_OK, "ensure_layout is repeatable");
}

static void t_adopt_empty(void)
{
    printf("adopt: clean card\n");
    sd_reset();
    ctx_setup();
    check(hs_adopt_if_needed(&CTX) == SWAP_OK, "adopt succeeds");

    char active[HS_MAX_SLUG];
    hs_read_active(&CTX, active, sizeof(active));
    check_eq_str(active, HS_SLUG_STOCK, "clean card adopts as stock");
}

static void t_adopt_existing_mod(void)
{
    printf("adopt: someone else's mod already installed\n");
    sd_reset();
    mkfile("luma/titles/" TID "/romfs/ui/thing.szs", "SOMEONE-ELSES-MOD");
    ctx_setup();
    check(hs_adopt_if_needed(&CTX) == SWAP_OK, "adopt succeeds");

    char active[HS_MAX_SLUG];
    hs_read_active(&CTX, active, sizeof(active));
    check_eq_str(active, HS_SLUG_EXISTING, "pre-existing mod adopted as 'existing'");

    // The whole point: adoption must not move a byte.
    check(rel_has("luma/titles/" TID "/romfs/ui/thing.szs", "SOMEONE-ELSES-MOD"),
          "pre-existing mod left exactly where it was");
}

static void t_adopt_ctgp(void)
{
    printf("adopt: CTGP-7 already running\n");
    sd_reset();
    mkfile("CTGP-7/config.bin", "ctgp-install");
    mkfile("luma/plugins/" TID "/CTGP-7.3gx", "CTGP-PLUGIN");
    ctx_setup();
    hs_adopt_if_needed(&CTX);

    char active[HS_MAX_SLUG];
    hs_read_active(&CTX, active, sizeof(active));
    check_eq_str(active, HS_SLUG_CTGP, "live plugin + CTGP folder adopts as ctgp7");
    check(rel_has("luma/plugins/" TID "/CTGP-7.3gx", "CTGP-PLUGIN"), "plugin not moved");
}

static void t_adopt_is_once_only(void)
{
    printf("adopt: does not re-run\n");
    sd_reset();
    ctx_setup();
    hs_adopt_if_needed(&CTX);
    hs_swap_to(&CTX, HS_SLUG_STOCK);

    mkfile("luma/titles/" TID "/romfs/late.szs", "APPEARED-LATER");
    hs_adopt_if_needed(&CTX);

    char active[HS_MAX_SLUG];
    hs_read_active(&CTX, active, sizeof(active));
    check_eq_str(active, HS_SLUG_STOCK, "adopt is a no-op once state exists");
}

static void t_swap_basic(void)
{
    printf("swap: stock -> custom -> stock\n");
    sd_reset();
    mkfile("hotswap/mk7/custom/layeredfs/romfs/course/mirror.szs", "CUSTOM-TRACK");
    ctx_setup();
    hs_adopt_if_needed(&CTX);

    check(hs_swap_to(&CTX, "custom") == SWAP_OK, "swap to custom");
    check(rel_has("luma/titles/" TID "/romfs/course/mirror.szs", "CUSTOM-TRACK"),
          "custom track is live");
    check(!rel_exists("hotswap/mk7/custom/layeredfs"), "parked copy moved out");

    char active[HS_MAX_SLUG];
    hs_read_active(&CTX, active, sizeof(active));
    check_eq_str(active, "custom", "state records custom");

    check(hs_swap_to(&CTX, HS_SLUG_STOCK) == SWAP_OK, "swap to stock");
    check(rel_has("hotswap/mk7/custom/layeredfs/romfs/course/mirror.szs", "CUSTOM-TRACK"),
          "custom track parked again, intact");
    check(count_content(ROOT "luma", "CUSTOM-TRACK") == 0, "nothing custom left live");

    hs_read_active(&CTX, active, sizeof(active));
    check_eq_str(active, HS_SLUG_STOCK, "state records stock");
}

static void t_swap_idempotent(void)
{
    printf("swap: repeating the same choice\n");
    sd_reset();
    mkfile("hotswap/mk7/custom/layeredfs/romfs/a.szs", "CUSTOM-TRACK");
    ctx_setup();
    hs_adopt_if_needed(&CTX);

    check(hs_swap_to(&CTX, "custom") == SWAP_OK, "first swap");
    check(hs_swap_to(&CTX, "custom") == SWAP_OK, "second swap to the same mod");
    check(hs_swap_to(&CTX, "custom") == SWAP_OK, "third swap to the same mod");
    check(rel_has("luma/titles/" TID "/romfs/a.szs", "CUSTOM-TRACK"), "still live and intact");
    check(count_content(ROOT, "CUSTOM-TRACK") == 1, "exactly one copy exists");
}

static void t_swap_ctgp_cycle(void)
{
    printf("swap: CTGP-7 -> custom -> stock -> CTGP-7\n");
    fixture_ctgp_active_custom_parked();

    check(rel_has("luma/plugins/" TID "/CTGP-7.3gx", "CTGP-PLUGIN"), "CTGP starts live");

    check(hs_swap_to(&CTX, "custom") == SWAP_OK, "CTGP -> custom");
    check(count_content(ROOT "luma/plugins", "CTGP-PLUGIN") == 0,
          "CTGP plugin is completely gone from the live slot");
    check(rel_has("hotswap/mk7/ctgp7/plugins/CTGP-7.3gx", "CTGP-PLUGIN"), "CTGP plugin parked intact");
    check(rel_has("luma/titles/" TID "/romfs/course/mirror.szs", "CUSTOM-TRACK"), "custom live");

    check(hs_swap_to(&CTX, HS_SLUG_STOCK) == SWAP_OK, "custom -> stock");
    check(count_content(ROOT "luma", "CUSTOM-TRACK") == 0, "no custom live on stock");
    check(count_content(ROOT "luma", "CTGP-PLUGIN") == 0, "no CTGP live on stock");

    check(hs_swap_to(&CTX, HS_SLUG_CTGP) == SWAP_OK, "stock -> CTGP");
    check(rel_has("luma/plugins/" TID "/CTGP-7.3gx", "CTGP-PLUGIN"), "CTGP live again");
    check(rel_has("hotswap/mk7/custom/layeredfs/romfs/course/mirror.szs", "CUSTOM-TRACK"),
          "custom parked intact after the full cycle");

    check(count_content(ROOT, "CTGP-PLUGIN") == 1, "exactly one CTGP plugin on the card");
    check(count_content(ROOT, "CUSTOM-TRACK") == 1, "exactly one custom track on the card");
}

static void t_conflict_refused(void)
{
    printf("swap: refuses to clobber\n");
    sd_reset();
    mkfile("hotswap/mk7/custom/layeredfs/a.szs", "PARKED-COPY");
    mkfile("luma/titles/" TID "/a.szs", "LIVE-COPY");
    ctx_setup();
    mkfile("hotswap/mk7/state", "active=custom\n");

    hs_result_t rc = hs_swap_to(&CTX, HS_SLUG_STOCK);
    check(rc == SWAP_ERR_CONFLICT, "conflict detected");
    check(rel_has("hotswap/mk7/custom/layeredfs/a.szs", "PARKED-COPY"), "parked copy survived");
    check(rel_has("luma/titles/" TID "/a.szs", "LIVE-COPY"), "live copy survived");
}

static void t_empty_dir_not_blocking(void)
{
    printf("swap: stale empty folders do not block\n");
    sd_reset();
    mkfile("hotswap/mk7/custom/layeredfs/a.szs", "CUSTOM-TRACK");
    ctx_setup();
    plat_mkdir_p(ROOT "luma/titles/" TID);   // empty leftover from a manual edit
    hs_adopt_if_needed(&CTX);

    check(hs_swap_to(&CTX, "custom") == SWAP_OK, "swap succeeds past the empty folder");
    check(rel_has("luma/titles/" TID "/a.szs", "CUSTOM-TRACK"), "custom is live");
}

static void t_list_and_detect(void)
{
    printf("listing and CTGP detection\n");
    fixture_ctgp_active_custom_parked();

    hs_mod_t mods[HS_MAX_MODS];
    int n = hs_list_mods(&CTX, mods, HS_MAX_MODS);
    check(n == 2, "two mods listed (ctgp7 + custom)");

    bool saw_ctgp = false, saw_custom = false;
    for (int i = 0; i < n; i++) {
        if (!strcmp(mods[i].slug, HS_SLUG_CTGP)) {
            saw_ctgp = true;
            check(mods[i].is_active, "ctgp7 marked active");
            check(mods[i].has_plugins, "ctgp7 has a plugin");
        }
        if (!strcmp(mods[i].slug, "custom")) {
            saw_custom = true;
            check(!mods[i].is_active, "custom not active");
            check(mods[i].has_layeredfs, "custom has layeredfs content");
        }
    }
    check(saw_ctgp && saw_custom, "both slugs present");

    check(hs_community_available(&CTX), "CTGP detected when folder + plugin present");

    // Parked rather than live must still count as installed.
    hs_swap_to(&CTX, "custom");
    check(hs_community_available(&CTX), "CTGP still detected while parked");

    // Folder removed (uninstalled) -> must grey out.
    rm_rf(ROOT "CTGP-7");
    check(!hs_community_available(&CTX), "CTGP not detected after its folder is removed");
}

static void t_detect_no_ctgp(void)
{
    printf("CTGP detection: never installed\n");
    sd_reset();
    mkfile("hotswap/mk7/custom/layeredfs/a.szs", "CUSTOM-TRACK");
    ctx_setup();
    hs_adopt_if_needed(&CTX);
    check(!hs_community_available(&CTX), "CTGP absent on a card that never had it");

    // Leftover folder with no plugin must not count as installed.
    mkfile("CTGP-7/readme.txt", "leftover");
    check(!hs_community_available(&CTX), "empty CTGP leftover folder does not count");
}

// The important one: interrupt a real swap at every rename and recover.
static void t_crash_recovery_sweep(void)
{
    printf("crash recovery: interrupting at every rename\n");

    for (int fail_at = 0; fail_at < 6; fail_at++) {
        fixture_ctgp_active_custom_parked();

        plat_host_fail_after(fail_at);
        hs_swap_to(&CTX, "custom");   // may fail part-way, that is the point
        plat_host_no_faults();

        hs_result_t rc = hs_recover(&CTX);

        char label[96];
        snprintf(label, sizeof(label), "recovery after failure #%d returns ok", fail_at);
        check(rc == SWAP_OK, label);

        snprintf(label, sizeof(label), "recovery #%d: custom ends up live", fail_at);
        check(rel_has("luma/titles/" TID "/romfs/course/mirror.szs", "CUSTOM-TRACK"), label);

        snprintf(label, sizeof(label), "recovery #%d: CTGP plugin fully off", fail_at);
        check(count_content(ROOT "luma/plugins", "CTGP-PLUGIN") == 0, label);

        snprintf(label, sizeof(label), "recovery #%d: no CTGP data lost", fail_at);
        check(count_content(ROOT, "CTGP-PLUGIN") == 1, label);

        snprintf(label, sizeof(label), "recovery #%d: no custom data lost", fail_at);
        check(count_content(ROOT, "CUSTOM-TRACK") == 1, label);

        snprintf(label, sizeof(label), "recovery #%d: journal cleared", fail_at);
        check(!rel_exists("hotswap/mk7/journal"), label);
    }
}

static void t_crash_recovery_reverse(void)
{
    printf("crash recovery: interrupting the swap back to CTGP-7\n");

    for (int fail_at = 0; fail_at < 6; fail_at++) {
        fixture_ctgp_active_custom_parked();
        hs_swap_to(&CTX, "custom");   // clean, so we start from custom-active

        plat_host_fail_after(fail_at);
        hs_swap_to(&CTX, HS_SLUG_CTGP);
        plat_host_no_faults();

        hs_result_t rc = hs_recover(&CTX);

        char label[96];
        snprintf(label, sizeof(label), "reverse #%d returns ok", fail_at);
        check(rc == SWAP_OK, label);

        snprintf(label, sizeof(label), "reverse #%d: CTGP live again", fail_at);
        check(rel_has("luma/plugins/" TID "/CTGP-7.3gx", "CTGP-PLUGIN"), label);

        snprintf(label, sizeof(label), "reverse #%d: custom fully parked", fail_at);
        check(count_content(ROOT "luma/titles", "CUSTOM-TRACK") == 0, label);

        snprintf(label, sizeof(label), "reverse #%d: no data lost", fail_at);
        check(count_content(ROOT, "CUSTOM-TRACK") == 1 &&
              count_content(ROOT, "CTGP-PLUGIN") == 1, label);
    }
}

static void t_recover_noop(void)
{
    printf("recovery: nothing to do\n");
    fixture_ctgp_active_custom_parked();
    check(hs_recover(&CTX) == SWAP_OK, "recover with no journal is a no-op");
    check(rel_has("luma/plugins/" TID "/CTGP-7.3gx", "CTGP-PLUGIN"), "state untouched");
}

// ------------------------------------------------- adversarial / edge cases

static void t_mod_with_both_slots(void)
{
    printf("adversarial: one mod owning both slots\n");
    sd_reset();
    mkfile("hotswap/mk7/big/layeredfs/romfs/a.szs", "BIG-FS");
    mkfile("hotswap/mk7/big/plugins/big.3gx", "BIG-PLUGIN");
    ctx_setup();
    hs_adopt_if_needed(&CTX);

    check(hs_swap_to(&CTX, "big") == SWAP_OK, "swap to a two-slot mod");
    check(rel_has("luma/titles/" TID "/romfs/a.szs", "BIG-FS"), "layeredfs live");
    check(rel_has("luma/plugins/" TID "/big.3gx", "BIG-PLUGIN"), "plugin live");

    check(hs_swap_to(&CTX, HS_SLUG_STOCK) == SWAP_OK, "back to stock");
    check(count_content(ROOT "luma", "BIG-FS") == 0, "layeredfs fully off");
    check(count_content(ROOT "luma", "BIG-PLUGIN") == 0, "plugin fully off");
    check(count_content(ROOT, "BIG-FS") == 1 && count_content(ROOT, "BIG-PLUGIN") == 1,
          "both halves survived intact");
}

static void t_journal_points_at_missing_mod(void)
{
    printf("adversarial: journal names a mod that no longer exists\n");
    fixture_ctgp_active_custom_parked();

    // Simulate: a swap to "ghost" was interrupted, and the user then deleted
    // the ghost folder from a PC before booting again.
    mkfile("hotswap/mk7/journal", "v1\nfrom=ctgp7\nto=ghost\n");

    hs_result_t rc = hs_recover(&CTX);
    check(rc == SWAP_OK, "recovery does not error on a missing target");
    check(count_content(ROOT, "CTGP-PLUGIN") == 1, "CTGP data not lost");
    check(count_content(ROOT, "CUSTOM-TRACK") == 1, "custom data not lost");
    check(!rel_exists("hotswap/mk7/journal"), "journal cleared");

    // Whatever it decided, the app must still be able to get back to a real mod.
    check(hs_swap_to(&CTX, HS_SLUG_CTGP) == SWAP_OK, "can still swap to CTGP after");
    check(rel_has("luma/plugins/" TID "/CTGP-7.3gx", "CTGP-PLUGIN"), "CTGP live again");
}

static void t_target_deleted_between_boots(void)
{
    printf("adversarial: active mod's folder deleted from a PC\n");
    sd_reset();
    mkfile("hotswap/mk7/custom/layeredfs/a.szs", "CUSTOM-TRACK");
    ctx_setup();
    hs_adopt_if_needed(&CTX);
    hs_swap_to(&CTX, "custom");

    // User pulls the card and deletes the live folder by hand.
    rm_rf(ROOT "luma/titles/" TID);

    check(hs_swap_to(&CTX, HS_SLUG_STOCK) == SWAP_OK, "swap to stock survives it");
    char active[HS_MAX_SLUG];
    hs_read_active(&CTX, active, sizeof(active));
    check_eq_str(active, HS_SLUG_STOCK, "state is stock");
    check(hs_swap_to(&CTX, "custom") == SWAP_OK, "swapping back does not error");
}

static void t_repeated_swaps_no_recover(void)
{
    printf("adversarial: many swaps in a row\n");
    fixture_ctgp_active_custom_parked();

    for (int i = 0; i < 12; i++) {
        const char *t = (i % 3 == 0) ? HS_SLUG_CTGP
                      : (i % 3 == 1) ? "custom"
                                     : HS_SLUG_STOCK;
        if (hs_swap_to(&CTX, t) != SWAP_OK) {
            check(false, "a swap in the long run failed");
            break;
        }
    }
    check(count_content(ROOT, "CTGP-PLUGIN") == 1, "exactly one CTGP copy after 12 swaps");
    check(count_content(ROOT, "CUSTOM-TRACK") == 1, "exactly one custom copy after 12 swaps");
    check(!rel_exists("hotswap/mk7/journal"), "no journal left behind");
}

static void t_conflict_then_recovers_after_fix(void)
{
    printf("adversarial: conflict blocks, then clears once resolved\n");
    sd_reset();
    mkfile("hotswap/mk7/custom/layeredfs/a.szs", "PARKED-COPY");
    mkfile("luma/titles/" TID "/a.szs", "LIVE-COPY");
    ctx_setup();
    mkfile("hotswap/mk7/state", "active=custom\n");

    check(hs_swap_to(&CTX, HS_SLUG_STOCK) == SWAP_ERR_CONFLICT, "blocked while ambiguous");

    // The user resolves it by deleting the duplicate parked copy.
    rm_rf(ROOT "hotswap/mk7/custom/layeredfs");

    check(hs_swap_to(&CTX, HS_SLUG_STOCK) == SWAP_OK, "works once resolved");
    check(rel_has("hotswap/mk7/custom/layeredfs/a.szs", "LIVE-COPY"), "the live copy was parked");
    check(!rel_exists("hotswap/mk7/journal"), "journal cleared after success");
}

static void t_existing_then_cycle(void)
{
    printf("adversarial: adopted 'existing' mod swaps away and back\n");
    sd_reset();
    mkfile("luma/titles/" TID "/romfs/theirs.szs", "SOMEONE-ELSES-MOD");
    mkfile("hotswap/mk7/custom/layeredfs/romfs/mine.szs", "CUSTOM-TRACK");
    ctx_setup();
    hs_adopt_if_needed(&CTX);

    check(hs_swap_to(&CTX, "custom") == SWAP_OK, "swap away from the adopted mod");
    check(rel_has("hotswap/mk7/existing/layeredfs/romfs/theirs.szs", "SOMEONE-ELSES-MOD"),
          "their mod parked intact, not destroyed");

    check(hs_swap_to(&CTX, HS_SLUG_EXISTING) == SWAP_OK, "swap back to theirs");
    check(rel_has("luma/titles/" TID "/romfs/theirs.szs", "SOMEONE-ELSES-MOD"),
          "their mod is live again, byte-identical");
    check(count_content(ROOT, "SOMEONE-ELSES-MOD") == 1, "still exactly one copy");
}

static void t_state_file_corrupt(void)
{
    printf("adversarial: corrupt state file\n");
    fixture_ctgp_active_custom_parked();
    mkfile("hotswap/mk7/state", "\x01\x02 garbage no equals sign\n");

    // Must not crash, must not lose data, must still be usable.
    hs_result_t rc = hs_swap_to(&CTX, "custom");
    check(rc == SWAP_OK || rc == SWAP_ERR_CONFLICT, "corrupt state does not crash");
    check(count_content(ROOT, "CTGP-PLUGIN") == 1, "CTGP data survived corrupt state");
    check(count_content(ROOT, "CUSTOM-TRACK") == 1, "custom data survived corrupt state");
}

// A stranger's SD card can legitimately have CTGP-7 and their own LayeredFS
// mod live at the same time - the two occupy different folders and do not
// conflict. Both slots must be adopted under their true owners, or the CTGP-7
// button greys out on a console where CTGP-7 is plainly installed.
static void t_adopt_ctgp_alongside_own_mod(void)
{
    printf("adopt: CTGP-7 live alongside a separate LayeredFS mod\n");
    sd_reset();

    mkfile("CTGP-7/config.bin", "ctgp-install");
    mkfile("luma/plugins/" TID "/CTGP-7.3gx", "CTGP-PLUGIN");
    mkfile("luma/titles/" TID "/romfs/theirs.szs", "THEIR-MOD");

    hs_ctx_t c;
    hs_ctx_init(&c, ROOT, "mk7", TID);
    hs_ensure_layout(&c);
    hs_adopt_if_needed(&c);

    char owner[HS_MAX_SLUG];
    hs_read_slot_owner(&c, HS_SLOT_PLUGINS, owner, sizeof(owner));
    check_eq_str(owner, HS_SLUG_CTGP, "plugin slot adopted as ctgp7");
    hs_read_slot_owner(&c, HS_SLOT_LAYEREDFS, owner, sizeof(owner));
    check_eq_str(owner, HS_SLUG_EXISTING, "layeredfs slot adopted as existing");

    check(hs_community_available(&c), "CTGP-7 button lit when CTGP-7 is installed");

    // Nothing may have been moved on first run.
    check(rel_exists("luma/plugins/" TID "/CTGP-7.3gx"), "plugin left in place");
    check(rel_exists("luma/titles/" TID "/romfs/theirs.szs"), "their mod left in place");

    // Switching to CTGP-7 must park their mod under its own name, not lose it.
    check(hs_swap_to(&c, HS_SLUG_CTGP) == SWAP_OK, "swap to ctgp7 succeeds");
    check(rel_exists("luma/plugins/" TID "/CTGP-7.3gx"), "CTGP plugin still live");
    check(!rel_exists("luma/titles/" TID "/romfs/theirs.szs"), "their mod moved out");
    check(rel_has("hotswap/mk7/existing/layeredfs/romfs/theirs.szs", "THEIR-MOD"),
          "their mod parked under existing");
    check(count_content(ROOT, "THEIR-MOD") == 1, "exactly one copy of their mod");

    // And back again.
    check(hs_swap_to(&c, HS_SLUG_EXISTING) == SWAP_OK, "swap back to existing");
    check(rel_has("luma/titles/" TID "/romfs/theirs.szs", "THEIR-MOD"), "their mod live again");
    check(rel_has("hotswap/mk7/ctgp7/plugins/CTGP-7.3gx", "CTGP-PLUGIN"), "CTGP plugin parked");
    check(count_content(ROOT, "CTGP-PLUGIN") == 1, "exactly one copy of the plugin");
    check(hs_community_available(&c), "CTGP-7 still offered after switching away");
}

// Picking the mod that is already active must not quietly hand it the slot it
// does not own. On an adopted card CTGP-7 owns the plugin slot while the user's
// own mod owns LayeredFS, and tapping the row that is already highlighted is a
// completely ordinary thing to do.
static void t_reselect_active_keeps_slot_owner(void)
{
    printf("adopt: re-picking the active mod leaves the other slot's owner alone\n");
    sd_reset();

    mkfile("CTGP-7/config.bin", "ctgp-install");
    mkfile("luma/plugins/" TID "/CTGP-7.3gx", "CTGP-PLUGIN");
    mkfile("luma/titles/" TID "/romfs/theirs.szs", "THEIR-MOD");

    hs_ctx_t c;
    hs_ctx_init(&c, ROOT, "mk7", TID);
    hs_ensure_layout(&c);
    hs_adopt_if_needed(&c);

    // Adoption leaves "existing" active with the plugin slot owned by ctgp7.
    check(hs_swap_to(&c, HS_SLUG_EXISTING) == SWAP_OK,
          "re-picking the already-active mod succeeds");

    char owner[HS_MAX_SLUG];
    hs_read_slot_owner(&c, HS_SLOT_PLUGINS, owner, sizeof(owner));
    check_eq_str(owner, HS_SLUG_CTGP, "plugin slot still owned by ctgp7");
    check(hs_community_available(&c),
          "CTGP-7 still offered after re-picking the active mod");

    check(rel_exists("luma/plugins/" TID "/CTGP-7.3gx"), "CTGP plugin still live");
    check(rel_exists("luma/titles/" TID "/romfs/theirs.szs"), "their mod still live");

    // The real damage shows on the next swap: the plugin must come home to
    // ctgp7 rather than being filed inside the user's own mod.
    check(hs_swap_to(&c, HS_SLUG_STOCK) == SWAP_OK, "swap to stock succeeds");
    check(rel_has("hotswap/mk7/ctgp7/plugins/CTGP-7.3gx", "CTGP-PLUGIN"),
          "plugin parked under ctgp7");
    check(!rel_exists("hotswap/mk7/existing/plugins/CTGP-7.3gx"),
          "plugin not buried inside the user's own mod folder");
    check(count_content(ROOT, "CTGP-PLUGIN") == 1, "exactly one copy of the plugin");
}

// The real CTGP-7. It never puts anything under /luma/plugins/ - it keeps its
// plugin at its own fixed path and is pointed at it just before the game boots.
// This is the exact card steve reported: CTGP-7 plainly installed, and the app
// calling it "Not installed" because it was only ever looking where it parks
// things itself.
//
// Selecting it must also be a no-op on disk. There is nothing to rename, so the
// only thing that may change is which mod the state file calls active - and any
// other mod that was live has to be parked out of the way, or its files would
// still be in place underneath CTGP-7.
static void t_ctgp_plugin_at_own_path(void)
{
    printf("CTGP-7 installed at its own path, nothing under luma/plugins\n");
    sd_reset();

    mkfile("CTGP-7/resources/CTGP-7.3gx", "REAL-CTGP-PLUGIN");
    mkfile("luma/titles/" TID "/romfs/theirs.szs", "THEIR-MOD");

    hs_ctx_t c;
    hs_ctx_init(&c, ROOT, "mk7", TID);
    hs_ensure_layout(&c);
    hs_adopt_if_needed(&c);

    check(hs_community_available(&c),
          "CTGP-7 offered when only its own plugin file is present");

    check(hs_swap_to(&c, HS_SLUG_CTGP) == SWAP_OK, "swap to ctgp7 succeeds");

    char active[HS_MAX_SLUG];
    hs_read_active(&c, active, sizeof(active));
    check_eq_str(active, HS_SLUG_CTGP, "ctgp7 recorded as active");

    // Its plugin is not ours to move, and moving it would break the launcher
    // the user may still want to use.
    check(rel_has("CTGP-7/resources/CTGP-7.3gx", "REAL-CTGP-PLUGIN"),
          "CTGP-7's own plugin left exactly where it was");
    check(count_content(ROOT, "REAL-CTGP-PLUGIN") == 1,
          "exactly one copy of CTGP-7's plugin");

    // The other mod cannot be left live underneath it.
    check(!rel_exists("luma/titles/" TID "/romfs/theirs.szs"), "their mod moved out");
    check(rel_has("hotswap/mk7/existing/layeredfs/romfs/theirs.szs", "THEIR-MOD"),
          "their mod parked under existing");

    // And back to stock, which for CTGP-7 means simply not arming it.
    check(hs_swap_to(&c, HS_SLUG_STOCK) == SWAP_OK, "swap to stock succeeds");
    hs_read_active(&c, active, sizeof(active));
    check_eq_str(active, HS_SLUG_STOCK, "stock recorded as active");
    check(rel_has("CTGP-7/resources/CTGP-7.3gx", "REAL-CTGP-PLUGIN"),
          "CTGP-7's plugin still untouched after going back to stock");
    check(hs_community_available(&c), "CTGP-7 still offered from stock");
}

// Guard against a check that cannot fail: deliberately break the tree and
// confirm the same assertions go red.
static void t_harness_can_fail(void)
{
    printf("harness self-check\n");
    fixture_ctgp_active_custom_parked();
    rm_rf(ROOT "luma/plugins");

    int before = g_fails;
    check(rel_has("luma/plugins/" TID "/CTGP-7.3gx", "CTGP-PLUGIN"),
          "(expected failure - proves the check works)");
    int after = g_fails;

    // Undo the deliberate failure we just counted.
    g_fails = before;
    g_checks++;
    if (after != before + 1) {
        g_fails++;
        printf("  FAIL: assertions do not go red when the tree is broken\n");
    }
}

int main(void)
{
    printf("mk7swap core tests\n\n");

    t_titles();
    t_registry();
    t_migrate_legacy();
    t_games_are_isolated();
    t_ctx_init();
    t_layout();
    t_adopt_empty();
    t_adopt_existing_mod();
    t_adopt_ctgp();
    t_adopt_is_once_only();
    t_swap_basic();
    t_swap_idempotent();
    t_swap_ctgp_cycle();
    t_conflict_refused();
    t_empty_dir_not_blocking();
    t_list_and_detect();
    t_long_slug_skipped();
    t_detect_no_ctgp();
    t_crash_recovery_sweep();
    t_crash_recovery_reverse();
    t_recover_noop();

    t_mod_with_both_slots();
    t_journal_points_at_missing_mod();
    t_target_deleted_between_boots();
    t_repeated_swaps_no_recover();
    t_conflict_then_recovers_after_fix();
    t_existing_then_cycle();
    t_state_file_corrupt();
    t_adopt_ctgp_alongside_own_mod();
    t_ctgp_plugin_at_own_path();
    t_reselect_active_keeps_slot_owner();

    t_harness_can_fail();

    rm_rf("./_sdtest");

    printf("\n%d checks, %d failed\n", g_checks, g_fails);
    return g_fails == 0 ? 0 : 1;
}
