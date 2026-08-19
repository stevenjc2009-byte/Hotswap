// Bottom-screen touch UI.
//
// Deliberately plain: solid rectangles and text, no textures. That keeps it
// identical on every model in the family (Old 3DS, New 3DS, 2DS, New 2DS XL)
// and keeps the CIA small. Stereoscopic 3D is never enabled, because the 2DS
// and 2DS XL have no slider and would just render the left eye anyway.
//
// The list scrolls with the D-pad rather than by dragging. A drag and a tap are
// the same gesture until the stylus lifts, so drag-scrolling would make every
// scroll a coin flip between moving the list and picking whatever is under the
// pen - and picking the wrong row here swaps a mod.

#include "ui.h"

#include <3ds.h>
#include <citro2d.h>
#include <stdio.h>
#include <string.h>

#define TOP_W 400
#define BOT_W 320
#define SCR_H 240

// List geometry on the bottom screen. Row height is per-screen and arrives in
// ui_state_t, so how many rows fit is derived rather than fixed: the game list
// trades rows for icon-sized ones, the mod list trades them for the button.
#define LIST_X     8
#define LIST_Y     32
#define LIST_W     (BOT_W - (2 * LIST_X))   // 304
#define ROW_GAP    2
#define STRIPE_W   6
#define BAR_W      4

// The launch button, and where the list has to stop when one is drawn.
#define BTN_Y        194
#define BTN_H        38
#define LIST_END     214
#define LIST_END_BTN (BTN_Y - 4)

static C3D_RenderTarget *g_top;
static C3D_RenderTarget *g_bot;
static C2D_TextBuf g_buf;

// Palette. The community mod gets its own colour so it reads as "the special
// one" when it is available, and drains to grey when it is not.
#define COL_BG_TOP    C2D_Color32(0x14, 0x16, 0x1C, 0xFF)
#define COL_BG_BOT    C2D_Color32(0x1B, 0x1E, 0x26, 0xFF)
#define COL_ROW       C2D_Color32(0x28, 0x2D, 0x39, 0xFF)
#define COL_TEXT      C2D_Color32(0xF2, 0xF4, 0xF8, 0xFF)
#define COL_DIM       C2D_Color32(0x8A, 0x90, 0x9E, 0xFF)
#define COL_STOCK     C2D_Color32(0x2E, 0x6D, 0xB8, 0xFF)
#define COL_COMMUNITY C2D_Color32(0xD8, 0x4A, 0x1E, 0xFF)
#define COL_CUSTOM    C2D_Color32(0x2E, 0x9E, 0x5B, 0xFF)
#define COL_NEUTRAL   C2D_Color32(0x57, 0x60, 0x7A, 0xFF)
#define COL_DISABLED  C2D_Color32(0x33, 0x37, 0x42, 0xFF)
#define COL_ACTIVE    C2D_Color32(0xFF, 0xD5, 0x4A, 0xFF)
#define COL_ERROR     C2D_Color32(0xE0, 0x4B, 0x4B, 0xFF)
#define COL_OK        C2D_Color32(0x5C, 0xD6, 0x8A, 0xFF)
#define COL_LAUNCH    C2D_Color32(0x25, 0x8A, 0x4C, 0xFF)

// C2D_Color32 is an inline function rather than a constant expression, so the
// palette cannot live in a static initialiser.
static u32 accent_colour(ui_accent_t a)
{
    switch (a) {
        case UI_ACCENT_STOCK:     return COL_STOCK;
        case UI_ACCENT_COMMUNITY: return COL_COMMUNITY;
        case UI_ACCENT_CUSTOM:    return COL_CUSTOM;
        case UI_ACCENT_NEUTRAL:   break;
    }
    return COL_NEUTRAL;
}

static bool g_was_touching = false;

void ui_init(void)
{
    gfxInitDefault();
    C3D_Init(C3D_DEFAULT_CMDBUF_SIZE);
    C2D_Init(C2D_DEFAULT_MAX_OBJECTS);
    C2D_Prepare();

    g_top = C2D_CreateScreenTarget(GFX_TOP, GFX_LEFT);
    g_bot = C2D_CreateScreenTarget(GFX_BOTTOM, GFX_LEFT);
    g_buf = C2D_TextBufNew(4096);
}

void ui_exit(void)
{
    C2D_TextBufDelete(g_buf);
    C2D_Fini();
    C3D_Fini();
    gfxExit();
}

static void draw_text(const char *s, float x, float y, float scale, u32 colour)
{
    if (!s || !*s) return;
    C2D_Text t;
    C2D_TextParse(&t, g_buf, s);
    C2D_TextOptimize(&t);
    C2D_DrawText(&t, C2D_WithColor, x, y, 0.5f, scale, scale, colour);
}

// Draws text that must not spill past `max_w`, trimming it and marking the cut
// with an ellipsis. Needed because a game row's text shares its width with the
// game's artwork, and a long region or mod name would otherwise run underneath
// the picture.
//
// The trim length is estimated from the measured width rather than found by
// trimming a character at a time: the font is proportional so the estimate is
// approximate, but it is one extra measurement instead of a loop that would
// re-parse into the shared text buffer dozens of times a frame.
static void draw_text_fit(const char *s, float x, float y, float scale,
                          u32 colour, float max_w)
{
    if (!s || !*s) return;

    C2D_Text t;
    C2D_TextParse(&t, g_buf, s);

    float w, h;
    C2D_TextGetDimensions(&t, scale, scale, &w, &h);

    if (w > max_w && w > 0.0f) {
        size_t len = strlen(s);
        size_t keep = (size_t)((float)len * (max_w / w));
        if (keep > 1) keep--;          // the estimate rounds long, so give it room
        if (keep > len) keep = len;

        char buf[UI_ROW_SUB + 4];
        if (keep >= sizeof(buf) - 4) keep = sizeof(buf) - 4;
        memcpy(buf, s, keep);
        memcpy(buf + keep, "...", 4);

        C2D_TextParse(&t, g_buf, buf);
    }

    C2D_TextOptimize(&t);
    C2D_DrawText(&t, C2D_WithColor, x, y, 0.5f, scale, scale, colour);
}

// Centres text horizontally inside a box of the given width.
static void draw_text_centred(const char *s, float box_x, float box_w, float y,
                              float scale, u32 colour)
{
    if (!s || !*s) return;
    C2D_Text t;
    C2D_TextParse(&t, g_buf, s);
    C2D_TextOptimize(&t);

    float w, h;
    C2D_TextGetDimensions(&t, scale, scale, &w, &h);
    C2D_DrawText(&t, C2D_WithColor, box_x + (box_w - w) / 2.0f, y, 0.5f, scale, scale, colour);
}

static int row_height(const ui_state_t *st)
{
    return st->row_h > 0 ? st->row_h : UI_ROW_H_MOD;
}

// How many rows fit, given this screen's row height and whether the launch
// button is eating the bottom strip. Derived rather than fixed so neither
// screen has to know the other's layout.
static int rows_shown(const ui_state_t *st)
{
    int end = st->launch_label ? LIST_END_BTN : LIST_END;
    int n = (end - LIST_Y + ROW_GAP) / (row_height(st) + ROW_GAP);
    return n < 1 ? 1 : n;
}

static float row_y(const ui_state_t *st, int visible_index)
{
    return (float)(LIST_Y + visible_index * (row_height(st) + ROW_GAP));
}

// Keeps the window over the list valid and, as far as possible, showing a full
// page. Called before drawing so the frame the user sees is already correct.
static void clamp_scroll(ui_state_t *st)
{
    int max_scroll = st->row_count - rows_shown(st);
    if (max_scroll < 0) max_scroll = 0;
    if (st->scroll > max_scroll) st->scroll = max_scroll;
    if (st->scroll < 0) st->scroll = 0;
}

static void draw_top(const ui_state_t *st)
{
    C2D_TargetClear(g_top, COL_BG_TOP);
    C2D_SceneBegin(g_top);

    draw_text(st->heading ? st->heading : "Hotswap", 12.0f, 12.0f, 0.62f, COL_TEXT);

    if (st->line1) draw_text(st->line1, 12.0f, 44.0f, 0.50f, COL_DIM);
    if (st->line2) draw_text(st->line2, 12.0f, 64.0f, 0.50f, COL_DIM);
    if (st->status) draw_text(st->status, 12.0f, 100.0f, 0.52f, COL_TEXT);

    // An error and a confirmation share the same line, because they answer the
    // same question - what happened when I touched that - and only one of them
    // can be the answer.
    if (st->error) {
        draw_text(st->error, 12.0f, 132.0f, 0.52f, COL_ERROR);
        draw_text(st->error_hint ? st->error_hint
                                 : "Nothing was changed. Your files are untouched.",
                  12.0f, 152.0f, 0.48f, COL_DIM);
    } else if (st->notice) {
        draw_text(st->notice, 12.0f, 132.0f, 0.52f, COL_OK);
        draw_text(st->notice_hint ? st->notice_hint
                                  : "Pick another, or press Launch when you are ready to play.",
                  12.0f, 152.0f, 0.48f, COL_DIM);
    }

    // Built rather than picked from a table: the three hints appear in different
    // combinations on different screens, and spelling every combination out is
    // how one of them ends up saying "B: back" on the screen you cannot go back
    // from.
    char hint[96];
    snprintf(hint, sizeof(hint), "Touch a row below.%s%s  START: exit.",
             st->show_back ? "  B: back." : "",
             st->show_options ? "  SELECT: options." : "");
    draw_text(hint, 12.0f, 208.0f, 0.48f, COL_DIM);
    C2D_Flush();
}

static void draw_row(const ui_state_t *st, const ui_row_t *r, int visible_index)
{
    float y = row_y(st, visible_index);
    float h = (float)row_height(st);

    C2D_DrawRectSolid(LIST_X, y, 0.0f, LIST_W, h, COL_ROW);
    C2D_DrawRectSolid(LIST_X, y, 0.0f, STRIPE_W, h,
                      r->enabled ? accent_colour(r->accent) : COL_DISABLED);

    // The game's own artwork, inset from the right edge and scaled to the row
    // rather than the row being sized to it, so a future taller or shorter row
    // still frames it correctly.
    float text_x = LIST_X + STRIPE_W + 10.0f;
    float text_w = LIST_X + LIST_W - 8.0f - text_x;

    if (r->icon.tex && r->icon.subtex && r->icon.subtex->width > 0) {
        float side = h - 8.0f;
        float scale = side / (float)r->icon.subtex->width;
        C2D_DrawImageAt(r->icon, LIST_X + LIST_W - side - 6.0f, y + 4.0f, 0.5f,
                        NULL, scale, scale);
        text_w -= side + 10.0f;
    }
    // The ACTIVE badge occupies the same right edge on mod rows, so a long mod
    // name has to stop short of it too.
    if (r->active) text_w -= 70.0f;

    u32 label_col = r->enabled ? COL_TEXT : COL_DIM;
    draw_text_fit(r->title, text_x, y + 5.0f, 0.56f, label_col, text_w);
    if (r->subtitle[0]) {
        draw_text_fit(r->subtitle, text_x, y + 25.0f, 0.42f, COL_DIM, text_w);
    }

    // A gold band down the right edge marks what is live right now. Only mod
    // rows are ever active, and those carry no artwork, so the two never meet.
    if (r->active) {
        C2D_DrawRectSolid(LIST_X + LIST_W - STRIPE_W, y, 0.0f, STRIPE_W, h, COL_ACTIVE);
        draw_text("ACTIVE", LIST_X + LIST_W - 68.0f, y + 10.0f, 0.42f, COL_ACTIVE);
    }
}

static void draw_scrollbar(const ui_state_t *st)
{
    int shown = rows_shown(st);
    if (st->row_count <= shown) return;

    float track_y = LIST_Y;
    float track_h = shown * (row_height(st) + ROW_GAP) - ROW_GAP;
    float x = BOT_W - BAR_W - 2.0f;

    C2D_DrawRectSolid(x, track_y, 0.0f, BAR_W, track_h, COL_DISABLED);

    float frac = (float)shown / (float)st->row_count;
    float thumb_h = track_h * frac;
    float offset = track_h * ((float)st->scroll / (float)st->row_count);
    C2D_DrawRectSolid(x, track_y + offset, 0.0f, BAR_W, thumb_h, COL_DIM);
}

// The button under the mod list. It is what ends the app now: a swap leaves the
// user here so they can change their mind, and only this starts the game.
static void draw_launch(const ui_state_t *st)
{
    bool live = !st->busy;
    C2D_DrawRectSolid(LIST_X, BTN_Y, 0.0f, LIST_W, BTN_H, live ? COL_LAUNCH : COL_DISABLED);
    draw_text_centred(live ? st->launch_label : "Swapping...",
                      LIST_X, LIST_W, BTN_Y + 9.0f, 0.56f, live ? COL_TEXT : COL_DIM);
}

static void draw_bottom(const ui_state_t *st)
{
    C2D_TargetClear(g_bot, COL_BG_BOT);
    C2D_SceneBegin(g_bot);

    draw_text_centred(st->list_title ? st->list_title : "", 0, BOT_W, 12.0f, 0.56f, COL_TEXT);

    if (st->row_count == 0) {
        draw_text_centred("Nothing to show.", 0, BOT_W, 110.0f, 0.52f, COL_DIM);
    }

    int shown = rows_shown(st);
    for (int v = 0; v < shown; v++) {
        int i = st->scroll + v;
        if (i >= st->row_count) break;
        draw_row(st, &st->rows[i], v);
    }

    draw_scrollbar(st);

    // The button owns the bottom strip when there is one, so the scroll hint
    // only appears on the screen that has the room for it.
    if (st->launch_label) {
        draw_launch(st);
    } else if (st->busy) {
        draw_text_centred("Swapping...", 0, BOT_W, 218.0f, 0.54f, COL_ACTIVE);
    } else if (st->row_count > shown) {
        draw_text_centred("Up / Down to scroll", 0, BOT_W, 218.0f, 0.42f, COL_DIM);
    }

    C2D_Flush();
}

// What a released touch landed on: a row index, UI_HIT_LAUNCH, or UI_HIT_NONE.
// Only the drawn page is considered, so a coordinate outside the list can never
// resolve to a row.
static int row_at(const ui_state_t *st, int x, int y)
{
    if (x < LIST_X || x > LIST_X + LIST_W) return UI_HIT_NONE;

    // Tested before the rows, because the button sits outside the list area and
    // a busy frame must not queue a launch behind the swap in progress.
    if (st->launch_label && !st->busy && y >= BTN_Y && y <= BTN_Y + BTN_H) {
        return UI_HIT_LAUNCH;
    }

    int shown = rows_shown(st);
    for (int v = 0; v < shown; v++) {
        int i = st->scroll + v;
        if (i >= st->row_count) break;

        float top = row_y(st, v);
        if (y >= top && y <= top + row_height(st)) {
            return st->rows[i].enabled ? i : UI_HIT_NONE;
        }
    }
    return UI_HIT_NONE;
}

int ui_frame(ui_state_t *st)
{
    clamp_scroll(st);

    C2D_TextBufClear(g_buf);

    C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
    draw_top(st);
    draw_bottom(st);
    C3D_FrameEnd(0);

    u32 down = hidKeysDown();
    if (down & KEY_DOWN) st->scroll++;
    if (down & KEY_UP)   st->scroll--;
    clamp_scroll(st);

    // Act on release rather than press, so a stray drag across the screen does
    // not swap a mod the user did not mean to choose.
    u32 held = hidKeysHeld();
    touchPosition tp;
    hidTouchRead(&tp);

    int hit = -1;

    // The touch coordinates read as (0,0) on the frame the stylus lifts, so the
    // last position seen while held is what the release has to be tested against.
    static int last_x = -1, last_y = -1;
    if (held & KEY_TOUCH) {
        last_x = tp.px;
        last_y = tp.py;
        g_was_touching = true;
    } else if (g_was_touching) {
        g_was_touching = false;
        hit = row_at(st, last_x, last_y);
        last_x = last_y = -1;
    }

    return hit;
}
