#ifndef MK7SWAP_UI_H
#define MK7SWAP_UI_H

#include <stdbool.h>
#include "core/swap.h"
#include "detect.h"

#define UI_SLOT_COUNT 3

typedef enum {
    UI_SLOT_STOCK  = 0,
    UI_SLOT_CTGP   = 1,
    UI_SLOT_CUSTOM = 2
} ui_slot_t;

typedef struct {
    const char *title;
    const char *subtitle;
    bool enabled;      // false renders greyed and ignores touches
    bool active;       // currently the live configuration
    char slug[MK7_MAX_SLUG];
} ui_button_t;

typedef struct {
    ui_button_t buttons[UI_SLOT_COUNT];
    const mk7_install_t *install;
    const char *status;     // one line on the top screen
    const char *error;      // NULL when fine
    bool busy;
} ui_state_t;

void ui_init(void);
void ui_exit(void);

// Draws one frame. Returns the index of a button the user just released a
// touch on, or -1. Only enabled buttons can be returned.
int ui_frame(const ui_state_t *st);

#endif
