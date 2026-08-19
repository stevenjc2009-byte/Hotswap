// Bottom-screen touch UI - a scrolling list of rows.
//
// One widget serves both screens. The game picker and the mod picker differ
// only in what fills `rows`, which keeps the drawing and the touch handling in
// one place rather than duplicated per screen.

#ifndef HOTSWAP_UI_H
#define HOTSWAP_UI_H

#include <stdbool.h>
#include <citro2d.h>

#include "core/swap.h"
#include "detect.h"

#define UI_MAX_ROWS  40
#define UI_ROW_TITLE 40
// Wide enough for a game row's full "region - media - what is installed" line
// without the compiler having to assume it gets cut short.
#define UI_ROW_SUB   72

// ui_frame's answer when the user did not pick a row.
#define UI_HIT_NONE   (-1)
#define UI_HIT_LAUNCH (-2)

// Row heights, chosen per screen. Game rows carry the game's own icon on the
// right and so need to be at least as tall as it; mod rows give that space back
// to the launch button underneath the list.
#define UI_ROW_H_GAME 52
#define UI_ROW_H_MOD  38

// Picks the stripe colour down the left edge of a row. Purely cosmetic, but it
// is what makes stock, the community mod and your own mods readable at a glance.
typedef enum {
    UI_ACCENT_NEUTRAL = 0,
    UI_ACCENT_STOCK,
    UI_ACCENT_COMMUNITY,
    UI_ACCENT_CUSTOM
} ui_accent_t;

typedef struct {
    char title[UI_ROW_TITLE];
    char subtitle[UI_ROW_SUB];
    ui_accent_t accent;
    bool enabled;      // false renders greyed and ignores touches
    bool active;       // currently the live configuration
    char slug[HS_MAX_SLUG];

    // The game's own HOME-menu artwork, drawn at the right edge. `tex` is NULL
    // when there is none - a row without artwork just draws without it rather
    // than falling back to a placeholder.
    C2D_Image icon;
} ui_row_t;

typedef struct {
    // Top screen.
    const char *heading;     // large line
    const char *line1;
    const char *line2;
    const char *status;
    const char *error;       // NULL when fine
    const char *notice;      // confirmation, e.g. "Swap complete". NULL when none

    // Second line under an error or a notice. NULL keeps the mod screen's
    // wording, which is what most of them want; the options screen sets its own
    // because "press Launch when you are ready to play" is nonsense there.
    const char *error_hint;
    const char *notice_hint;

    // Bottom screen.
    const char *list_title;
    ui_row_t rows[UI_MAX_ROWS];
    int row_count;
    int scroll;              // first visible row; ui_frame clamps and updates it
    bool show_back;          // draws the "B: back" hint
    bool show_options;       // draws the "SELECT: options" hint

    // Row height for this screen - one of UI_ROW_H_GAME / UI_ROW_H_MOD. How
    // many rows fit is derived from it, so the two screens can differ without
    // either one hardcoding a count.
    int row_h;

    // Label for the button under the list, or NULL for no button. Touching it
    // makes ui_frame return UI_HIT_LAUNCH.
    const char *launch_label;

    bool busy;
} ui_state_t;

void ui_init(void);
void ui_exit(void);

// Draws one frame and handles scrolling. Returns the index of a row the user
// just released a touch on, UI_HIT_LAUNCH for the button under the list, or
// UI_HIT_NONE. Only enabled rows can be returned.
int ui_frame(ui_state_t *st);

#endif
