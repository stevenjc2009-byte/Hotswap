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
#include "ctgp7.h"
#include "detect.h"
#include "icon.h"
#include "ui.h"
#include "update.h"
#include "version.h"

#ifdef HOTSWAP_DEV
#include "screenshot.h"
#include "selftest.h"
#endif

#define SD_ROOT "sdmc:/"

bool plat_3ds_init(void);
void plat_3ds_exit(void);

typedef enum {
    SCREEN_GAMES = 0,
    SCREEN_MODS = 1,
    SCREEN_OPTIONS = 2
} screen_t;

// Slug of the one row on the options screen. Compared rather than switched on
// an index, so inserting another option above it cannot silently repoint it.
#define OPT_SLUG_UPDATE "update"

// What the startup scan found out about one installed game. Everything the
// game list needs is worked out once, on open, rather than re-read from the SD
// card on every frame - the card is slow, and nothing outside this app is
// moving those folders while it runs.
typedef struct {
    hs_ctx_t ctx;
    bool ready;                  // its folders opened and any crash was repaired
    C2D_Image icon;              // its own HOME-menu artwork; tex NULL if none
    char summary[UI_ROW_SUB];    // "Europe  -  SD  -  CTGP-7 + 2 mods"
} game_scan_t;

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

// One line saying where this game lives and what is waiting for it, so the
// question "have I got mods for that one?" is answered before the user has to
// open it and look.
static void describe_game(const hs_ctx_t *ctx, const hs_install_t *inst,
                          char *out, size_t cap)
{
    hs_mod_t mods[HS_MAX_MODS];
    int n = hs_list_mods(ctx, mods, HS_MAX_MODS);

    int own = 0;
    for (int i = 0; i < n; i++) {
        if (is_reserved(ctx, mods[i].slug)) continue;
        if (mods[i].has_layeredfs || mods[i].has_plugins) own++;
    }

    char found[32];
    const char *community = (ctx->def && hs_community_available(ctx))
                              ? ctx->def->community : NULL;

    if (community && own > 0) {
        snprintf(found, sizeof(found), "%s + %d mod%s", community, own,
                 own == 1 ? "" : "s");
    } else if (community) {
        snprintf(found, sizeof(found), "%s", community);
    } else if (own > 0) {
        snprintf(found, sizeof(found), "%d mod%s", own, own == 1 ? "" : "s");
    } else {
        snprintf(found, sizeof(found), "no mods found");
    }

    snprintf(out, cap, "%s  -  %s  -  %s", inst->region,
             inst->on_gamecard ? "cartridge" : "SD", found);
}

// Opens one game's folders, finishes any swap a power cut interrupted, then
// reads back what it has. Run for every detected game as the app starts, which
// is what makes a mod installed since last time show up without the user having
// to go looking for it.
static void scan_game(const hs_install_t *inst, game_scan_t *out)
{
    memset(out, 0, sizeof(*out));

    // Artwork is independent of the SD layout, so a game whose folders cannot
    // be opened still gets its icon and still looks like itself in the list.
    icon_load(inst->title_id, inst->on_gamecard, &out->icon);

    if (!hs_ctx_init(&out->ctx, SD_ROOT, inst->game->slug, inst->hex) ||
        hs_ensure_layout(&out->ctx) != SWAP_OK) {
        snprintf(out->summary, sizeof(out->summary), "%s  -  folders unavailable",
                 inst->region);
        return;
    }

    hs_adopt_if_needed(&out->ctx);
    hs_recover(&out->ctx);

    out->ready = true;
    describe_game(&out->ctx, inst, out->summary, sizeof(out->summary));
}

static void build_game_rows(ui_state_t *st, const hs_install_t *installs,
                            const game_scan_t *scans, int n)
{
    st->row_count = 0;
    for (int i = 0; i < n && st->row_count < UI_MAX_ROWS; i++) {
        ui_row_t *r = &st->rows[st->row_count++];
        set_row(r, installs[i].game->name, scans[i].summary,
                UI_ACCENT_NEUTRAL, scans[i].ready, false, installs[i].game->slug);
        r->icon = scans[i].icon;
    }
}

// ------------------------------------------------------------- mod screen

static void build_mod_rows(ui_state_t *st, const hs_ctx_t *ctx, const char *active)
{
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

// ---------------------------------------------------------- options screen

static void build_option_rows(ui_state_t *st)
{
    st->row_count = 0;
    set_row(&st->rows[st->row_count++], "Check for updates",
            "Installed: version " HOTSWAP_VERSION,
            UI_ACCENT_NEUTRAL, true, false, OPT_SLUG_UPDATE);
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

    // Laid out the way the real mod lays itself out: its plugin at its own
    // fixed path, and nothing parked under Hotswap at all. Parking a fake
    // plugin instead would exercise the folder-rename path, which is precisely
    // the path CTGP-7 does not use - the emulator would then pass while the
    // real card failed.
    if (!ctx->def->community_plugin) return;
    snprintf(p, sizeof(p), "%s%s", ctx->root, ctx->def->community_plugin);
    char *last = strrchr(p, '/');
    if (!last) return;
    *last = 0;
    plat_mkdir_p(p);
    *last = '/';
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

// An emulator has no real supported game, so the fixture's own icon read finds
// nothing and the artwork path would never run. Borrow the icon of whatever
// title IS installed, purely so the SMDH read, the tile copy and the texture
// upload are exercised by a real run and can be looked at in a screenshot
// rather than argued about from the source.
static void dev_borrow_any_icon(C2D_Image *out)
{
    u32 count = 0;
    if (R_FAILED(AM_GetTitleCount(MEDIATYPE_SD, &count)) || count == 0) return;
    if (count > 64) count = 64;

    u64 ids[64];
    u32 read = 0;
    if (R_FAILED(AM_GetTitleList(&read, MEDIATYPE_SD, count, ids))) return;

    for (u32 i = 0; i < read && !out->tex; i++) icon_load(ids[i], false, out);
}

// Lays the emulator fixtures down and runs the on-device self test. Called
// during the startup scan rather than when a game is opened, because the scan
// is now what decides what the list says - a fixture created afterwards would
// not appear until the next boot.
static void dev_prepare(const hs_ctx_t *ctx, int *failures)
{
    // Creating sdmc:/mk7swap/no-ctgp suppresses the fixture, so the greyed-out
    // "not installed" path can be seen for real instead of being taken on
    // trust. Only lay a fixture down when that slot has nothing at all, live or
    // parked: a second copy would trip the engine's own anti-clobber check on
    // the next swap.
    bool suppress = plat_exists("sdmc:/mk7swap/no-ctgp");
    if (!suppress && !hs_community_available(ctx)) dev_make_community_placeholder(ctx);
    if (!dev_has_own_mod(ctx)) dev_make_custom_placeholder(ctx);
    if (!suppress) *failures = selftest_run(ctx);
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

    // The scan every game gets on the way in. Anything installed since the last
    // run is picked up here, so the list is never stale.
    game_scan_t scans[HS_MAX_INSTALLS];
    for (int i = 0; i < install_count; i++) {
        scan_game(&installs[i], &scans[i]);
#ifdef HOTSWAP_DEV
        if (!scans[i].icon.tex) dev_borrow_any_icon(&scans[i].icon);
        if (scans[i].ready) {
            dev_prepare(&scans[i].ctx, &dev_failures);
            // The fixtures changed what is on the card, so the summary this
            // scan just produced is already out of date.
            describe_game(&scans[i].ctx, &installs[i], scans[i].summary,
                          sizeof(scans[i].summary));
        }
#endif
    }

    ui_state_t st;
    memset(&st, 0, sizeof(st));
    st.heading = "Hotswap";

    screen_t screen = SCREEN_GAMES;
    screen_t options_from = SCREEN_GAMES;   // where SELECT was pressed
    int open_game = -1;          // index into installs, or -1 on the game screen
    char game_line[96];
    char status_line[128];
    char launch_label[64];
    char notice_line[96];

    // Rows are built from the last scan, not re-read from the SD card every
    // frame. Set whenever something happens that could change what they say.
    bool rows_stale = true;

    if (!sd_ok) {
        st.error = "Could not open the SD card.";
    } else if (install_count == 0) {
        st.error = "No supported games found on this console.";
    }

    while (aptMainLoop()) {
        hidScanInput();
        u32 down = hidKeysDown();
        if (down & KEY_START) break;

        // SELECT is a toggle into the options screen from wherever the user
        // happens to be, and B puts them back exactly there - so opening
        // options never costs them the game they had open.
        if ((down & KEY_SELECT) && screen != SCREEN_OPTIONS) {
            options_from = screen;
            screen = SCREEN_OPTIONS;
            st.scroll = 0;
            st.error = NULL;
            st.notice = NULL;
            rows_stale = true;
        } else if (screen == SCREEN_OPTIONS && (down & KEY_B)) {
            screen = options_from;
            st.scroll = 0;
            st.error = NULL;
            st.notice = NULL;
            rows_stale = true;
        } else if (screen == SCREEN_MODS && (down & KEY_B)) {
            // A swap may have changed what this game's row says about itself,
            // so its summary is rebuilt on the way out rather than left to
            // describe the card as it was when the app started.
            game_scan_t *was = &scans[open_game];
            if (was->ready) {
                describe_game(&was->ctx, &installs[open_game], was->summary,
                              sizeof(was->summary));
            }

            screen = SCREEN_GAMES;
            open_game = -1;
            st.scroll = 0;
            st.error = NULL;
            st.notice = NULL;
            rows_stale = true;
        }

        st.show_options = true;
        st.error_hint = NULL;
        st.notice_hint = NULL;

        if (screen == SCREEN_GAMES) {
            st.list_title = "Choose a game";
            st.line1 = install_count > 0
                         ? "These are the supported games on this console."
                         : "Install a supported game, or insert its cartridge.";
            st.line2 = NULL;
            st.status = NULL;
            st.show_back = false;
            st.row_h = UI_ROW_H_GAME;
            st.launch_label = NULL;
            if (rows_stale) build_game_rows(&st, installs, scans, install_count);
        } else if (screen == SCREEN_OPTIONS) {
            st.list_title = "Options";
            st.line1 = "Hotswap version " HOTSWAP_VERSION ".";
            st.line2 = NULL;
            st.status = NULL;
            st.show_back = true;
            st.show_options = false;   // already here
            st.row_h = UI_ROW_H_MOD;
            st.launch_label = NULL;
            st.error_hint = "Nothing was installed. Hotswap is unchanged.";
            st.notice_hint = "B goes back to what you were doing.";
            if (rows_stale) build_option_rows(&st);
        } else {
            const hs_install_t *inst = &installs[open_game];
            const hs_ctx_t *ctx = &scans[open_game].ctx;

            st.list_title = inst->game->short_name;
            snprintf(game_line, sizeof(game_line), "%s  (%s)  -  %s",
                     inst->game->name, inst->region, inst->hex);
            st.line1 = game_line;
            st.line2 = NULL;
            st.show_back = true;
            st.row_h = UI_ROW_H_MOD;
            st.launch_label = launch_label;

            if (rows_stale) {
                char active[HS_MAX_SLUG];
                hs_read_active(ctx, active, sizeof(active));
#ifdef HOTSWAP_DEV
                snprintf(status_line, sizeof(status_line),
                         "Active: %s   |   self test: %d failed",
                         strcmp(active, HS_SLUG_STOCK) == 0 ? "Stock" : active,
                         dev_failures);
#else
                snprintf(status_line, sizeof(status_line), "Currently active: %s",
                         strcmp(active, HS_SLUG_STOCK) == 0 ? "Stock" : active);
#endif
                build_mod_rows(&st, ctx, active);
            }
            st.status = status_line;
        }
        rows_stale = false;

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
            const char *tag = screen == SCREEN_GAMES ? "games"
                            : screen == SCREEN_MODS  ? "mods"
                                                     : "options";
            char name[32];
            snprintf(name, sizeof(name), "%s_top", tag);
            screenshot_capture(GFX_TOP, name);
            snprintf(name, sizeof(name), "%s_bot", tag);
            screenshot_capture(GFX_BOTTOM, name);
        }
#endif

        if (hit == UI_HIT_NONE) continue;

        if (screen == SCREEN_GAMES) {
            // Everything this game needs was worked out by the startup scan;
            // opening it is now just a change of screen.
            if (!scans[hit].ready) {
                st.error = "Could not open that game's folder on the SD card.";
                continue;
            }

            open_game = hit;
            screen = SCREEN_MODS;
            st.scroll = 0;
            st.error = NULL;
            st.notice = NULL;
            snprintf(launch_label, sizeof(launch_label), "Launch %s",
                     installs[hit].game->short_name);
            rows_stale = true;
            continue;
        }

        if (screen == SCREEN_OPTIONS) {
            if (strcmp(st.rows[hit].slug, OPT_SLUG_UPDATE) != 0) continue;

            // Downloading and installing both block for seconds at a time, so
            // the busy frame is pushed out before either starts - otherwise the
            // console looks hung for the whole of it.
            st.busy = true;
            st.error = NULL;
            st.notice = NULL;
            ui_frame(&st);

            char tag[32] = {0};
            hs_update_t urc = hs_update_run(tag, sizeof(tag));
            st.busy = false;

            if (urc == HS_UPDATE_INSTALLED) {
                // The new copy is on the card, but the old one is still the one
                // executing. Restarting is the same manoeuvre as launching a
                // game: arm the jump and let main's normal shutdown perform it.
                snprintf(notice_line, sizeof(notice_line),
                         "Version %s installed. Restarting.", tag);
                st.notice = notice_line;
                ui_frame(&st);
                if (hs_update_relaunch()) break;

                snprintf(notice_line, sizeof(notice_line),
                         "Version %s installed. Close and reopen Hotswap.", tag);
                st.notice = notice_line;
                continue;
            }
            if (urc == HS_UPDATE_CURRENT) {
                snprintf(notice_line, sizeof(notice_line),
                         "Version %s is already the newest release.",
                         HOTSWAP_VERSION);
                st.notice = notice_line;
                continue;
            }

            st.error = hs_update_str(urc);
            continue;
        }

        // The launch button is the only thing that ends the app now. Nothing is
        // torn down here on purpose: the single shutdown below is the only exit
        // path, and libctru performs the jump inside the aptExit() at the end
        // of it. Tearing the screen and the SD archive down early and jumping
        // by hand is what black-screened the console with HOME dead.
        if (hit == UI_HIT_LAUNCH) {
            // CTGP-7 is not a set of files sitting in a folder waiting to be
            // found - it is an instruction to the plugin loader that lasts for
            // exactly one launch. So it is armed here, at the moment of
            // launching, rather than when the user picked the row. Refusing
            // here is better than launching anyway: a game that boots stock
            // when the user asked for CTGP-7 looks like the mod is broken.
            const hs_ctx_t *ctx = &scans[open_game].ctx;
            char active[HS_MAX_SLUG];
            hs_read_active(ctx, active, sizeof(active));

            if (ctx->def && ctx->def->community_slug &&
                strcmp(active, ctx->def->community_slug) == 0) {
                hs_ctgp7_t crc = hs_ctgp7_arm(&installs[open_game]);
                if (crc != CTGP7_OK) {
                    st.error = hs_ctgp7_err_str(crc);
                    st.notice = NULL;
                    continue;
                }
            }

            if (!hs_launch(&installs[open_game])) {
                st.error = "Could not start the game.";
                st.notice = NULL;
                continue;
            }
            break;
        }

        // A mod row: swap to it and stay here. Choosing a mod and playing are
        // two separate decisions, so picking one no longer commits the user to
        // the other - they can change their mind, or swap and walk away.
        st.busy = true;
        st.error = NULL;
        st.notice = NULL;
        ui_frame(&st);

        hs_result_t rc = hs_swap_to(&scans[open_game].ctx, st.rows[hit].slug);
        st.busy = false;
        rows_stale = true;   // whatever it says now, the card has moved on

        if (rc != SWAP_OK) {
            st.error = hs_result_str(rc);
            continue;
        }
        // A launch-armed mod like CTGP-7 moved no files, so calling it a
        // completed swap would be describing something that did not happen -
        // and it would hide the one thing the user still has to do, which is
        // press Launch. Nothing outside this app can turn it on.
        const hs_game_t *def = scans[open_game].ctx.def;
        bool armed_at_launch = def && def->community_plugin && def->community_slug &&
                               strcmp(st.rows[hit].slug, def->community_slug) == 0;
        if (armed_at_launch) {
            snprintf(notice_line, sizeof(notice_line), "%s selected.", def->community);
            st.notice = notice_line;
        } else {
            st.notice = "Swap complete.";
        }
    }

    icon_free_all();
    ui_exit();
    plat_3ds_exit();
    amExit();
    return 0;
}
