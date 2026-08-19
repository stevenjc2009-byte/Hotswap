#include "ctgp7.h"

#include <stdio.h>
#include <string.h>

#include "plgldr.h"

// The numbers CTGP-7's plugin expects to find in its config block. They are its
// own private encoding, not anything the system defines, so they are written
// out here rather than derived from the title id. Anything outside these tables
// - Korea, or a build CTGP-7 has never heard of - is passed on as zero, exactly
// as CTGP-7's own launcher does; it is the plugin's business to decide what it
// can patch, not ours to second-guess by refusing to launch.
#define CTGP7_REGION_EUROPE  1
#define CTGP7_REGION_AMERICA 2
#define CTGP7_REGION_JAPAN   3

#define CTGP7_REV_0_11 1
#define CTGP7_REV_1    2

// The plugin reads its settings out of the first words of the config block, and
// checks a marker further in. Both the layout and the marker belong to CTGP-7.
#define CFG_REGION 0
#define CFG_REVISION 1
#define CFG_WAS_ENABLED 2
#define CFG_MARKER 7
#define CFG_MARKER_VALUE 0x77777777u

static uint32_t region_code(uint64_t title_id)
{
    switch ((uint32_t)title_id) {
        case 0x00030600: return CTGP7_REGION_JAPAN;
        case 0x00030700: return CTGP7_REGION_EUROPE;
        case 0x00030800: return CTGP7_REGION_AMERICA;
        default:         return 0;
    }
}

static uint32_t revision_code(uint16_t version)
{
    switch (version) {
        case 1040: return CTGP7_REV_0_11;
        case 2048:
        case 2112: return CTGP7_REV_1;
        default:   return 0;
    }
}

hs_ctgp7_t hs_ctgp7_arm(const hs_install_t *inst)
{
    if (!inst || !inst->found || !inst->game) return CTGP7_ERR_ARG;
    if (!inst->game->community_plugin) return CTGP7_ERR_ARG;

    uint32_t region = region_code(inst->title_id);
    uint32_t revision = revision_code(inst->version);

    if (R_FAILED(hs_plgldr_init())) return CTGP7_ERR_NO_LOADER;

    // Whether the loader was already on gets passed to the plugin, so it has to
    // be read before we turn it on ourselves. A failure here is not fatal - the
    // plugin only uses it to decide whether to leave the setting as it found it.
    bool was_enabled = false;
    hs_plgldr_is_enabled(&was_enabled);

    hs_ctgp7_t out = CTGP7_OK;
    if (R_FAILED(hs_plgldr_set_enabled(true))) {
        out = CTGP7_ERR_SERVICE;
    } else {
        // Zeroed in full first. Everything outside the few words below is
        // CTGP-7's to interpret, and handing it uninitialised stack would mean
        // a different boot every time.
        hs_plg_params_t p;
        memset(&p, 0, sizeof(p));

        p.no_flash = true;
        p.memory_strategy = 0;   // the loader's default
        p.persistent = 0;        // consumed by this one launch, then forgotten
        p.low_title_id = (uint32_t)inst->title_id;
        // The service opens this against the SD archive itself, so it is
        // rooted at the card and takes no "sdmc:" prefix.
        snprintf(p.path, sizeof(p.path), "/%s", inst->game->community_plugin);

        p.config[CFG_REGION] = region;
        p.config[CFG_REVISION] = revision;
        p.config[CFG_WAS_ENABLED] = was_enabled ? 1u : 0u;
        p.config[CFG_MARKER] = CFG_MARKER_VALUE;

        if (R_FAILED(hs_plgldr_set_load_params(&p))) out = CTGP7_ERR_SERVICE;
    }

    hs_plgldr_exit();
    return out;
}

const char *hs_ctgp7_err_str(hs_ctgp7_t e)
{
    switch (e) {
        case CTGP7_OK:            return "CTGP-7 armed.";
        case CTGP7_ERR_NO_LOADER: return "This firmware has no Luma plugin loader.";
        case CTGP7_ERR_SERVICE:   return "The plugin loader refused the request.";
        case CTGP7_ERR_ARG:       return "This game has no community plugin.";
    }
    return "CTGP-7 could not be started.";
}
