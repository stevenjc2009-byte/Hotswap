// mk7swap - Mario Kart 7 mod swapper for the Nintendo 3DS family.
//
// Boot order matters. Recovery runs before anything is drawn, so a console
// that lost power mid-swap is already consistent by the time the user sees a
// button. Adoption runs before that, so a card that has never seen this app
// records what is already installed instead of assuming it owns the folder.

#include <3ds.h>
#include <stdio.h>
#include <string.h>

#include "core/swap.h"
#include "detect.h"
#include "ui.h"

#ifdef MK7SWAP_DEV
#include "screenshot.h"
#include "selftest.h"
#endif

#define SD_ROOT "sdmc:/"

bool plat_3ds_init(void);
void plat_3ds_exit(void);

// Slugs the UI treats specially; anything else is a user mod.
static bool is_reserved(const char *slug)
{
    return strcmp(slug, MK7_SLUG_CTGP) == 0 || strcmp(slug, MK7_SLUG_STOCK) == 0;
}

// Copies a slug only if it fits whole. A truncated slug names no real folder,
// so returning false is honest where a short copy would look like success.
static bool copy_slug(char *out, size_t cap, const char *slug)
{
    size_t len = strlen(slug);
    if (len >= cap) return false;
    memcpy(out, slug, len + 1);
    return true;
}

// Picks which mod the right-hand button represents. Prefers a slug literally
// called "custom", otherwise the first non-reserved mod on the card.
static bool pick_custom(const mk7_ctx_t *ctx, char *out, size_t cap)
{
    mk7_mod_t mods[MK7_MAX_MODS];
    int n = mk7_list_mods(ctx, mods, MK7_MAX_MODS);

    for (int i = 0; i < n; i++) {
        if (strcmp(mods[i].slug, "custom") == 0) {
            return copy_slug(out, cap, mods[i].slug);
        }
    }
    for (int i = 0; i < n; i++) {
        if (is_reserved(mods[i].slug)) continue;
        if (!mods[i].has_layeredfs && !mods[i].has_plugins) continue;
        return copy_slug(out, cap, mods[i].slug);
    }
    return false;
}

#ifdef MK7SWAP_DEV
// Development only. Writes a fake CTGP-7 install so the greyed-out/enabled
// logic and the full swap cycle can be exercised in an emulator, where the
// real mod cannot be installed. Compiled out of release builds entirely, so a
// shipped CIA physically cannot create this on a user's card.
static void dev_make_ctgp_placeholder(const mk7_ctx_t *ctx)
{
    char p[PLAT_MAX_PATH];

    snprintf(p, sizeof(p), "%sCTGP-7", ctx->root);
    plat_mkdir_p(p);
    snprintf(p, sizeof(p), "%sCTGP-7/PLACEHOLDER.txt", ctx->root);
    plat_write_file(p, "placeholder CTGP-7 install for testing\n", 39);

    // Park it as a mod rather than dropping it live, so the app's own swap
    // logic is what puts it in place - that is the thing under test.
    mk7_slot_parked_path(ctx, MK7_SLUG_CTGP, MK7_SLOT_PLUGINS, p, sizeof(p));
    plat_mkdir_p(p);
    strncat(p, "/CTGP-7.3gx", sizeof(p) - strlen(p) - 1);
    plat_write_file(p, "PLACEHOLDER-PLUGIN", 18);
}

static void dev_make_custom_placeholder(const mk7_ctx_t *ctx)
{
    char p[PLAT_MAX_PATH];
    mk7_slot_parked_path(ctx, "custom", MK7_SLOT_LAYEREDFS, p, sizeof(p));
    strncat(p, "/romfs", sizeof(p) - strlen(p) - 1);
    plat_mkdir_p(p);
    strncat(p, "/PLACEHOLDER.txt", sizeof(p) - strlen(p) - 1);
    plat_write_file(p, "placeholder custom mod for testing\n", 35);
}
#endif

int main(int argc, char **argv)
{
    (void)argc; (void)argv;

    amInit();
    ui_init();

    mk7_install_t install;
    mk7_detect_install(&install);

#ifdef MK7SWAP_DEV
    // An emulator has no Mario Kart 7 installed, so detection correctly finds
    // nothing and the UI would never be reachable. Stand in a fake Americas
    // install so the buttons, greying and swap cycle can all be exercised.
    // Release builds have no such fallback: no game, no swapping.
    if (!install.found) {
        install.found = true;
        install.title_id = 0x0004000000030800ULL;
        install.region = "Americas [DEV FIXTURE]";
        install.on_gamecard = false;
        snprintf(install.hex, sizeof(install.hex), "0004000000030800");
    }
#endif

    ui_state_t st;
    memset(&st, 0, sizeof(st));
    st.install = &install;

    mk7_ctx_t ctx;
    bool ready = false;

    if (install.found && plat_3ds_init() && mk7_ctx_init(&ctx, SD_ROOT, install.hex)) {
        if (mk7_ensure_layout(&ctx) == SWAP_OK) {
            mk7_adopt_if_needed(&ctx);

            mk7_result_t rec = mk7_recover(&ctx);
            if (rec != SWAP_OK) st.error = mk7_result_str(rec);
            ready = true;
        } else {
            st.error = "Could not create folders on the SD card.";
        }
    } else if (!install.found) {
        st.error = "Mario Kart 7 not found.";
    } else {
        st.error = "Could not open the SD card.";
    }

#ifdef MK7SWAP_DEV
    int dev_failures = -1;

    if (ready) {
        // Only lay a fixture down when that slot has nothing at all, live or
        // parked. Writing a second copy would trip the engine's own
        // anti-clobber check on the next swap.
        // Creating sdmc:/mk7swap/no-ctgp suppresses the fixture, so the
        // greyed-out "not installed" path can be seen for real instead of
        // being taken on trust.
        bool suppress_ctgp = plat_exists("sdmc:/mk7swap/no-ctgp");

        if (!suppress_ctgp && !mk7_ctgp_available(&ctx)) dev_make_ctgp_placeholder(&ctx);

        char probe[MK7_MAX_SLUG];
        if (!pick_custom(&ctx, probe, sizeof(probe))) dev_make_custom_placeholder(&ctx);

        if (!suppress_ctgp) dev_failures = selftest_run(&ctx);
    }
#endif

    char custom_slug[MK7_MAX_SLUG];
    char status_line[128];
    bool have_custom = false;

    while (aptMainLoop()) {
        hidScanInput();
        if (hidKeysDown() & KEY_START) break;

        if (ready) {
            char active[MK7_MAX_SLUG];
            mk7_read_active(&ctx, active, sizeof(active));
            have_custom = pick_custom(&ctx, custom_slug, sizeof(custom_slug));
            bool ctgp_ok = mk7_ctgp_available(&ctx);

            st.buttons[UI_SLOT_STOCK].title = "Stock";
            st.buttons[UI_SLOT_STOCK].subtitle = "Unmodified";
            st.buttons[UI_SLOT_STOCK].enabled = true;
            st.buttons[UI_SLOT_STOCK].active = (strcmp(active, MK7_SLUG_STOCK) == 0);
            snprintf(st.buttons[UI_SLOT_STOCK].slug,
                     sizeof(st.buttons[UI_SLOT_STOCK].slug), "%s", MK7_SLUG_STOCK);

            st.buttons[UI_SLOT_CTGP].title = "CTGP-7";
            st.buttons[UI_SLOT_CTGP].subtitle = ctgp_ok ? "Custom Track GP" : "Not installed";
            st.buttons[UI_SLOT_CTGP].enabled = ctgp_ok;
            st.buttons[UI_SLOT_CTGP].active = (strcmp(active, MK7_SLUG_CTGP) == 0);
            snprintf(st.buttons[UI_SLOT_CTGP].slug,
                     sizeof(st.buttons[UI_SLOT_CTGP].slug), "%s", MK7_SLUG_CTGP);

            st.buttons[UI_SLOT_CUSTOM].title = "Custom";
            st.buttons[UI_SLOT_CUSTOM].subtitle = have_custom ? custom_slug : "No mod installed";
            st.buttons[UI_SLOT_CUSTOM].enabled = have_custom;
            st.buttons[UI_SLOT_CUSTOM].active =
                have_custom && (strcmp(active, custom_slug) == 0);
            snprintf(st.buttons[UI_SLOT_CUSTOM].slug,
                     sizeof(st.buttons[UI_SLOT_CUSTOM].slug), "%s",
                     have_custom ? custom_slug : "");

#ifdef MK7SWAP_DEV
            snprintf(status_line, sizeof(status_line),
                     "Active: %s   |   self test: %d failed",
                     strcmp(active, MK7_SLUG_STOCK) == 0 ? "Stock" : active,
                     dev_failures);
#else
            snprintf(status_line, sizeof(status_line), "Currently active: %s",
                     strcmp(active, MK7_SLUG_STOCK) == 0 ? "Stock" : active);
#endif
            st.status = status_line;
        }

        int hit = ui_frame(&st);

#ifdef MK7SWAP_DEV
        // Capture once the UI has settled, so the emulator run leaves proof of
        // what was actually on screen rather than a description of it.
        static int frames = 0;
        if (++frames == 8) {
            screenshot_capture(GFX_TOP, "top");
            screenshot_capture(GFX_BOTTOM, "bottom");
        }
#endif

        if (hit < 0 || !ready) continue;

        st.busy = true;
        st.error = NULL;
        ui_frame(&st);

        mk7_result_t rc = mk7_swap_to(&ctx, st.buttons[hit].slug);
        st.busy = false;

        if (rc != SWAP_OK) {
            st.error = mk7_result_str(rc);
            continue;
        }

        // Swap succeeded - hand straight over to the game.
        ui_exit();
        plat_3ds_exit();
        if (!mk7_launch(&install)) {
            // The jump failed, so come back up and say so rather than sitting
            // on a black screen.
            ui_init();
            plat_3ds_init();
            st.error = "Swapped, but could not start Mario Kart 7.";
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
