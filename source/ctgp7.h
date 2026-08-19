// Turning CTGP-7 on for the next launch of Mario Kart 7.
//
// This is the one mod Hotswap cannot switch by renaming a folder. Everything
// else it manages lives under /luma/titles/<TITLEID>/ or /luma/plugins/<TITLEID>/,
// where Luma finds it on its own. CTGP-7 keeps its plugin at
// /CTGP-7/resources/CTGP-7.3gx and relies on being told about it, once, just
// before the game boots. Its own launcher application is what normally does
// that telling; hs_ctgp7_arm() does the same thing so the user does not have to
// leave Hotswap.
//
// Turning it OFF needs no work at all: the instruction is consumed by the next
// matching title launch, so simply not giving one leaves Mario Kart 7 stock.

#ifndef HOTSWAP_CTGP7_H
#define HOTSWAP_CTGP7_H

#include <stdbool.h>
#include <stdint.h>

#include "detect.h"

typedef enum {
    CTGP7_OK = 0,
    CTGP7_ERR_NO_LOADER,   // this firmware has no plugin loader at all
    CTGP7_ERR_SERVICE,     // the loader is there but refused the request
    CTGP7_ERR_ARG          // no plugin path in the registry for this game
} hs_ctgp7_t;

// Points the plugin loader at CTGP-7's plugin for `inst`'s title id. Does not
// launch anything - the caller still has to arm the jump and exit normally.
//
// An unrecognised region or game version is not an error here: it is passed on
// as zero, which is what CTGP-7's own launcher does, leaving the decision about
// what it can and cannot patch to the plugin itself.
hs_ctgp7_t hs_ctgp7_arm(const hs_install_t *inst);

const char *hs_ctgp7_err_str(hs_ctgp7_t e);

#endif
