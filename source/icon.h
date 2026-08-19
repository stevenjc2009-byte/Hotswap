// The HOME-menu artwork of an installed title, ready for citro2d to draw.
//
// Every installed title carries its own icon in its SMDH, inside its ExeFS.
// Reading it back is what lets the game list show Mario Kart's own box art
// rather than a generic row, so a console with several supported games is
// readable at a glance instead of by reading four similar lines of text.
//
// Failure is normal and silent: a title whose SMDH cannot be opened simply has
// no artwork, and the row draws without it. Nothing here can fail in a way that
// stops the app listing or swapping that game.

#ifndef HOTSWAP_ICON_H
#define HOTSWAP_ICON_H

#include <stdbool.h>
#include <stdint.h>

#include <citro2d.h>

// Loads one title's 48x48 icon into `out`. Returns false and leaves out->tex
// NULL when the title has no readable SMDH.
//
// The texture is owned by this module and lives until icon_free_all(). Callers
// hold the C2D_Image by value and never free it themselves.
bool icon_load(uint64_t title_id, bool on_gamecard, C2D_Image *out);

// Releases every texture handed out by icon_load. Call once, at shutdown.
void icon_free_all(void);

#endif
