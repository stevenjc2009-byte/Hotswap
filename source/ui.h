// Bottom-screen touch UI - a scrolling list of rows.
//
// One widget serves both screens. The game picker and the mod picker differ
// only in what fills `rows`, which keeps the drawing and the touch handling in
// one place rather than duplicated per screen.

#ifndef HOTSWAP_UI_H
#define HOTSWAP_UI_H

#include <stdbool.h>
#include "core/swap.h"
#include "detect.h"

#define UI_MAX_ROWS  40
#define UI_ROW_TITLE 40
#define UI_ROW_SUB   48

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
} ui_row_t;

typedef struct {
    // Top screen.
    const char *heading;     // large line
    const char *line1;
    const char *line2;
    const char *status;
    const char *error;       // NULL when fine

    // Bottom screen.
    const char *list_title;
    ui_row_t rows[UI_MAX_ROWS];
    int row_count;
    int scroll;              // first visible row; ui_frame clamps and updates it
    bool show_back;          // draws the "B: back" hint

    bool busy;
} ui_state_t;

void ui_init(void);
void ui_exit(void);

// Draws one frame and handles scrolling. Returns the index of a row the user
// just released a touch on, or -1. Only enabled rows can be returned.
int ui_frame(ui_state_t *st);

#endif
