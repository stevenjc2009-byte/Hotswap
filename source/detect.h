// Runtime detection of which supported games this console actually has.
//
// Nothing here is hardcoded to one console or one region. The title list is
// queried from the system and matched against the registry in core/games.h, so
// a European cartridge, a Japanese eShop copy and an American SD install all
// resolve correctly on the same build.
//
// Only games that are really installed come back. That is deliberate: the game
// list must never show something the user cannot pick.

#ifndef HOTSWAP_DETECT_H
#define HOTSWAP_DETECT_H

#include <stdbool.h>
#include <stdint.h>

#include "core/games.h"

#define HS_MAX_INSTALLS HS_GAME_COUNT

typedef struct {
    bool found;
    const hs_game_t *game;   // registry entry - never NULL when found
    uint64_t title_id;
    char hex[17];
    const char *region;      // "Europe", "Americas", ...
    bool on_gamecard;        // affects which media the launch jump targets

    // Highest of the game's own version and any installed update's. A plugin
    // has to know which build of the game it is patching, so this is passed on
    // when one is armed. 0 means neither could be read.
    uint16_t version;
} hs_install_t;

// Fills `out` with one entry per supported game found on this console, and
// returns how many. A game is listed once even if several of its regions are
// present; the game card wins over an SD install, because a card in the slot is
// the copy the user is about to play.
int hs_detect_installs(hs_install_t *out, int max);

// Arms the jump into one detected game. Returns immediately: the jump itself is
// performed by libctru during aptExit(), so the caller must leave the main loop
// and let main's normal shutdown run. Returns false only if `inst` is unusable,
// in which case nothing has been armed and the app can carry on.
bool hs_launch(const hs_install_t *inst);

#endif
