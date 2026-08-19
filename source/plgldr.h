// Minimal client for Luma3DS's plugin loader service, "plg:ldr".
//
// Why this file exists: CTGP-7 is not installed the way an ordinary 3GX plugin
// is. An ordinary plugin sits in /luma/plugins/<TITLEID>/ and the loader picks
// it up on its own, which is why Hotswap can switch those by renaming a folder.
// CTGP-7 instead keeps its plugin at a fixed path of its own,
// /CTGP-7/resources/CTGP-7.3gx, and its own launcher application tells the
// plugin loader to use that file for the next title that boots. There is
// nothing under /luma/plugins/ to rename, so a rename-based swapper can never
// turn CTGP-7 on. Doing what its launcher does is the only way.
//
// libctru does not expose this service - it is a Luma3DS addition, not part of
// the system. The three commands below are written from the service's wire
// format (command ids 2, 3 and 4, and their buffer descriptors), which is
// documented in Luma3DS's plugin loader and in CTGP-7's own launcher. No
// Luma3DS or CTGP-7 source is copied here.
//
// "plg:ldr" is created with svcCreatePort rather than registered with srv:, so
// any process may connect to it. Nothing has to be declared in the exheader.

#ifndef HOTSWAP_PLGLDR_H
#define HOTSWAP_PLGLDR_H

#include <3ds.h>
#include <stdbool.h>
#include <stdint.h>

// Mirrors the service's PluginLoadParameters. The three bytes at the front are
// packed into one command word, so their order here is ours, not the wire's.
typedef struct {
    // Suppresses the brief screen flash the loader otherwise does to show a
    // plugin was loaded. CTGP-7's launcher sets this.
    bool no_flash;

    // 0 = swap (the loader's default), 1 = mode3, 2 = none. Leave at 0 unless
    // there is a measured reason not to.
    uint8_t memory_strategy;

    // 0 = consumed by the next matching title launch, which is what a "launch
    // once with this plugin" button wants. 1 would write the choice to the SD
    // card and keep applying it until something clears it.
    uint8_t persistent;

    // Low 32 bits of the target title id - 0x00030700 for Mario Kart 7 Europe.
    // 0 would match any title, which is never what we want here.
    uint32_t low_title_id;

    // SD-root-absolute, with a leading slash and no "sdmc:" prefix. The service
    // opens it against the SD archive itself.
    char path[256];

    // 128 bytes handed to the plugin verbatim. The plugin author decides what
    // it means; the loader only copies it.
    uint32_t config[32];
} hs_plg_params_t;

// Connects to the service. Fails when the running firmware has no plugin
// loader, which is the check that tells us CTGP-7 could not be started anyway.
Result hs_plgldr_init(void);
void   hs_plgldr_exit(void);

Result hs_plgldr_is_enabled(bool *out_enabled);
Result hs_plgldr_set_enabled(bool enabled);

// Arms the loader for the next title whose low title id matches. Does not
// launch anything by itself.
Result hs_plgldr_set_load_params(const hs_plg_params_t *p);

#endif
