// Hotswap - mod swapper for the Nintendo 3DS family.
//
// Two screens. The first lists the supported games this console actually has;
// the second lists what you can put in that game, with Stock always at the top.
//
// Boot order matters. The v1 migration runs first, then adoption, then crash
// recovery - so by the time a row is drawn, a card that lost power mid-swap is
// already consistent and a card that has never seen this app has recorded what
// was already installed instead of assuming it owns the folder.

#include <3ds.h>
#include <stdio.h>
#include <string.h>

#include "core/swap.h"
#include "detect.h"
#include "ui.h"

#ifdef HOTSWAP_DEV
#include "screenshot.h"
#include "selftest.h"
#endif

#define SD_ROOT "sdmc:/"

bool plat_3ds_init(void);
void plat_3ds_exit(void);

typedef enum { SCREEN_GAMES = 0, SCREEN_MODS = 1 } screen_t;

// Slugs the mod list places itself; anything else is one of the user's own.
static bool is_reserved(const hs_ctx_t *ctx, const char *slug)
{
    if (strcmp(slug, HS_SLUG_STOCK) == 0) return true;
    if (ctx->def && ctx->def->community_slug &&
        strcmp(slug, ctx->def->community_slug) == 0) return true;
    return false;
}

static void set_row(ui_row_t *r, const char *title, const char *subtitle,
                    ui_accent_t accent, bool enabled, bool active, const char *slug)
{
    memset(r, 0, sizeof(*r));
    snprintf(r->title, sizeof(r->title), "%s", title);
    if (subtitle) snprintf(r->subtitle, sizeof(r->subtitle), "%s", subtitle);
    r->accent = accent;
    r->enabled = enabled;
    r->active = active;
    if (slug) snprintf(r->slug, sizeof(r->slug), "%s", slug);
}

// ------------------------------------------------------------ game screen

static void build_game_rows(ui_state_t *st, const hs_install_t *installs, int n)
{
    st->row_count = 0;
    for (int i = 0; i < n && st->row_count < UI_MAX_ROWS; i++) {
        char sub[UI_ROW_SUB];
        snprintf(sub, sizeof(sub), "%s  -  %s", installs[i].region,
                 installs[i].on_gamecard ? "cartridge" : "SD");
        set_row(&st->rows[st->row_count++], installs[i].game->name, sub,
                UI_ACCENT_NEUTRAL, true, false, installs[i].game->slug);
    }
}

// ------------------------------------------------------------- mod screen

static void build_mod_rows(ui_state_t *st, const hs_ctx_t *ctx)
{
    char active[HS_MAX_SLUG];
    hs_read_active(ctx, active, sizeof(active));

    st->row_count = 0;

    // Stock is always first and always available - it is the way back.
    set_row(&st->rows[st->row_count++], "Stock", "The unmodified game",
            UI_ACCENT_STOCK, true, strcmp(active, HS_SLUG_STOCK) == 0, HS_SLUG_STOCK);

    // The community mod keeps its own row even when it is missing, because
    // "CTGP-7 - not installed" answers the question a blank list would raise.
    if (ctx->def && ctx->def->community) {
        bool ok = hs_community_available(ctx);
        set_row(&st->rows[st->row_count++], ctx->def->community,
                ok ? "Community mod" : "Not installed",
                UI_ACCENT_COMMUNITY, ok,
                ok && strcmp(active, ctx->def->community_slug) == 0,
                ctx->def->community_slug);
    }

    hs_mod_t mods[HS_MAX_MODS];
    int n = hs_list_mods(ctx, mods, HS_MAX_MODS);
    for (int i = 0; i < n && st->row_count < UI_MAX_ROWS; i++) {
        if (is_reserved(ctx, mods[i].slug)) continue;
        // A folder with nothing in either slot would swap to nothing at all.
        if (!mods[i].has_layeredfs && !mods[i].has_plugins) continue;

        const char *what = mods[i].has_layeredfs && mods[i].has_plugins
                             ? "Files and plugin"
                             : (mods[i].has_layeredfs ? "Game files" : "Plugin");
        set_row(&st->rows[st->row_count++], mods[i].slug, what,
                UI_ACCENT_CUSTOM, true, mods[i].is_active, mods[i].slug);
    }
}

#ifdef HOTSWAP_DEV
// Development only. Writes a fake community install so the greyed-out/enabled
// logic and the full swap cycle can be exercised in an emulator, where the real
// mod cannot be installed. Compiled out of release builds entirely, so a
// shipped CIA physically cannot create this on a user's card.
static void dev_make_community_placeholder(const hs_ctx_t *ctx)
{
    char p[PLAT_MAX_PATH];
    if (!ctx->def || !ctx->def->community_dir) return;

    snprintf(p, sizeof(p), "%s%s", ctx->root, ctx->def->community_dir);
    plat_mkdir_p(p);
    snprintf(p, sizeof(p), "%s%s/PLACEHOLDER.txt", ctx->root, ctx->def->community_dir);
    plat_write_file(p, "placeholder community install for testing\n", 42);

    // Park it as a mod rather than dropping it live, so the app's own swap
    // logic is what puts it in place - that is the thing under test.
    hs_slot_parked_path(ctx, ctx->def->community_slug, HS_SLOT_PLUGINS, p, sizeof(p));
    plat_mkdir_p(p);
    strncat(p, "/CTGP-7.3gx", sizeof(p) - strlen(p) - 1);
    plat_write_file(p, "PLACEHOLDER-PLUGIN", 18);
}

static void dev_make_custom_placeholder(const hs_ctx_t *ctx)
{
    char p[PLAT_MAX_PATH];
    hs_slot_parked_path(ctx, "custom", HS_SLOT_LAYEREDFS, p, sizeof(p));
    strncat(p, "/romfs", sizeof(p) - strlen(p) - 1);
    plat_mkdir_p(p);
    strncat(p, "/PLACEHOLDER.txt", sizeof(p) - strlen(p) - 1);
    plat_write_file(p, "placeholder custom mod for testing\n", 35);
}

// True when this game already has a user mod of its own.
static bool dev_has_own_mod(const hs_ctx_t *ctx)
{
    hs_mod_t mods[HS_MAX_MODS];
    int n = hs_list_mods(ctx, mods, HS_MAX_MODS);
    for (int i = 0; i < n; i++) {
        if (is_reserved(ctx, mods[i].slug)) continue;
        if (mods[i].has_layeredfs || mods[i].has_plugins) return true;
    }
    return false;
}
#endif

int main(int argc, char **argv)
{
    (void)argc; (void)argv;

    amInit();
    ui_init();

    hs_install_t installs[HS_MAX_INSTALLS];
    int install_count = 0;
    bool sd_ok = plat_3ds_init();

    if (sd_ok) {
        // Before anything reads the layout: move a v1 card onto the per-game
        // one. A no-op on every boot after the first.
        hs_migrate_legacy(SD_ROOT);
        install_count = hs_detect_installs(installs, HS_MAX_INSTALLS);
    }

#ifdef HOTSWAP_DEV
    // An emulator has no supported game installed, so detection correctly finds
    // nothing and the UI would never be reachable. Stand in a fake Americas
    // Mario Kart 7 so the list, greying and swap cycle can all be exercised.
    // Release builds have no such fallback: no game, no swapping.
    if (sd_ok && install_count == 0) {
        const hs_title_t *t = NULL;
        const hs_game_t *g = hs_game_by_hex("0004000000030800", &t);
        if (g) {
            memset(&installs[0], 0, sizeof(installs[0]));
            installs[0].found = true;
            installs[0].game = g;
            installs[0].title_id = t->title_id;
            installs[0].region = "Americas [DEV FIXTURE]";
            installs[0].on_gamecard = false;
            snprintf(installs[0].hex, sizeof(installs[0].hex), "%s", t->hex);
            install_count = 1;
        }
    }
    int dev_failures = -1;
#endif

    ui_state_t st;
    memset(&st, 0, sizeof(st));
    st.heading = "Hotswap";

    screen_t screen = SCREEN_GAMES;
    hs_ctx_t ctx;
    int open_game = -1;          // index into installs, or -1 on the game screen
    char game_line[96];
    char status_line[128];

    if (!sd_ok) {
        st.error = "Could not open the SD card.";
    } else if (install_count == 0) {
        st.error = "No supported games found on this console.";
    }

    while (aptMainLoop()) {
        hidScanInput();
        u32 down = hidKeysDown();
        if (down & KEY_START) break;

        if (screen == SCREEN_MODS && (down & KEY_B)) {
            screen = SCREEN_GAMES;
            open_game = -1;
            st.scroll = 0;
            st.error = NULL;
        }

        if (screen == SCREEN_GAMES) {
            st.list_title = "Choose a game";
            st.line1 = install_count > 0
                         ? "These are the supported games on this console."
                         : "Install a supported game, or insert its cartridge.";
            st.line2 = NULL;
            st.status = NULL;
            st.show_back = false;
            build_game_rows(&st, installs, install_count);
        } else {
            const hs_install_t *inst = &installs[open_game];

            st.list_title = inst->game->short_name;
            snprintf(game_line, sizeof(game_line), "%s  (%s)  -  %s",
                     inst->game->name, inst->region, inst->hex);
            st.line1 = game_line;
            st.line2 = NULL;
            st.show_back = true;

            char active[HS_MAX_SLUG];
            hs_read_active(&ctx, active, sizeof(active));
#ifdef HOTSWAP_DEV
            snprintf(status_line, sizeof(status_line),
                     "Active: %s   |   self test: %d failed",
                     strcmp(active, HS_SLUG_STOCK) == 0 ? "Stock" : active,
                     dev_failures);
#else
            snprintf(status_line, sizeof(status_line), "Currently active: %s",
                     strcmp(active, HS_SLUG_STOCK) == 0 ? "Stock" : active);
#endif
            st.status = status_line;
            build_mod_rows(&st, &ctx);
        }

        int hit = ui_frame(&st);

#ifdef HOTSWAP_DEV
        // Capture once each screen has settled, so the emulator run leaves proof
        // of what was actually drawn rather than a description of it. Both
        // screens get their own pair of files, or the second would overwrite
        // the evidence for the first.
        static int shot_frames = -1;
        static screen_t shot_screen = (screen_t)-1;
        if (shot_screen != screen) { shot_screen = screen; shot_frames = 0; }
        if (shot_frames >= 0 && ++shot_frames == 8) {
            shot_frames = -1;
            screenshot_capture(GFX_TOP, screen == SCREEN_GAMES ? "games_top" : "mods_top");
            screenshot_capture(GFX_BOTTOM, screen == SCREEN_GAMES ? "games_bot" : "mods_bot");
        }
#endif

        if (hit < 0) continue;

        if (screen == SCREEN_GAMES) {
            const hs_install_t *inst = &installs[hit];
            if (!hs_ctx_init(&ctx, SD_ROOT, inst->game->slug, inst->hex)) {
                st.error = "Could not open that game's folder.";
                continue;
            }
            if (hs_ensure_layout(&ctx) != SWAP_OK) {
                st.error = "Could not create folders on the SD card.";
                continue;
            }
            hs_adopt_if_needed(&ctx);

            hs_result_t rec = hs_recover(&ctx);
            st.error = (rec != SWAP_OK) ? hs_result_str(rec) : NULL;

#ifdef HOTSWAP_DEV
            // Creating sdmc:/mk7swap/no-ctgp suppresses the fixture, so the
            // greyed-out "not installed" path can be seen for real instead of
            // being taken on trust. Only lay a fixture down when that slot has
            // nothing at all, live or parked: a second copy would trip the
            // engine's own anti-clobber check on the next swap.
            bool suppress = plat_exists("sdmc:/mk7swap/no-ctgp");
            if (!suppress && !hs_community_available(&ctx)) {
                dev_make_community_placeholder(&ctx);
            }
            if (!dev_has_own_mod(&ctx)) dev_make_custom_placeholder(&ctx);
            if (!suppress) dev_failures = selftest_run(&ctx);
#endif

            open_game = hit;
            screen = SCREEN_MODS;
            st.scroll = 0;
            continue;
        }

        // Mod screen: the row's slug is what to swap to.
        st.busy = true;
        st.error = NULL;
        ui_frame(&st);

        hs_result_t rc = hs_swap_to(&ctx, st.rows[hit].slug);
        st.busy = false;

        if (rc != SWAP_OK) {
            st.error = hs_result_str(rc);
            continue;
        }

        // Swap succeeded - hand straight over to the game.
        ui_exit();
        plat_3ds_exit();
        if (!hs_launch(&installs[open_game])) {
            // The jump failed, so come back up and say so rather than sitting
            // on a black screen.
            ui_init();
            plat_3ds_init();
            st.error = "Swapped, but could not start the game.";
            continue;
        }
        amExit();
        return 0;
    }

    ui_exit();
    plat_3ds_exit();
    amExit();
    return 0;
}
