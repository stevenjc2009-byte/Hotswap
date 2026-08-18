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

// List geometry on the bottom screen.
#define LIST_X     8
#define LIST_Y     38
#define LIST_W     (BOT_W - (2 * LIST_X))   // 304
#define ROW_H      42
#define ROW_GAP    2
#define ROWS_SHOWN 4
#define STRIPE_W   6
#define BAR_W      4

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

static float row_y(int visible_index)
{
    return (float)(LIST_Y + visible_index * (ROW_H + ROW_GAP));
}

// Keeps the window over the list valid and, as far as possible, showing a full
// page. Called before drawing so the frame the user sees is already correct.
static void clamp_scroll(ui_state_t *st)
{
    int max_scroll = st->row_count - ROWS_SHOWN;
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

    if (st->error) {
        draw_text(st->error, 12.0f, 132.0f, 0.52f, COL_ERROR);
        draw_text("Nothing was changed. Your files are untouched.",
                  12.0f, 152.0f, 0.48f, COL_DIM);
    }

    draw_text(st->show_back ? "Touch a row below.  B: back.  START: exit."
                            : "Touch a row below.  START: exit.",
              12.0f, 208.0f, 0.48f, COL_DIM);
    C2D_Flush();
}

static void draw_row(const ui_row_t *r, int visible_index)
{
    float y = row_y(visible_index);

    C2D_DrawRectSolid(LIST_X, y, 0.0f, LIST_W, ROW_H, COL_ROW);
    C2D_DrawRectSolid(LIST_X, y, 0.0f, STRIPE_W, ROW_H,
                      r->enabled ? accent_colour(r->accent) : COL_DISABLED);

    u32 label_col = r->enabled ? COL_TEXT : COL_DIM;
    draw_text(r->title, LIST_X + STRIPE_W + 10.0f, y + 5.0f, 0.56f, label_col);
    if (r->subtitle[0]) {
        draw_text(r->subtitle, LIST_X + STRIPE_W + 10.0f, y + 25.0f, 0.42f, COL_DIM);
    }

    // A gold band down the right edge marks what is live right now.
    if (r->active) {
        C2D_DrawRectSolid(LIST_X + LIST_W - STRIPE_W, y, 0.0f, STRIPE_W, ROW_H, COL_ACTIVE);
        draw_text("ACTIVE", LIST_X + LIST_W - 68.0f, y + 12.0f, 0.42f, COL_ACTIVE);
    }
}

static void draw_scrollbar(const ui_state_t *st)
{
    if (st->row_count <= ROWS_SHOWN) return;

    float track_y = LIST_Y;
    float track_h = ROWS_SHOWN * (ROW_H + ROW_GAP) - ROW_GAP;
    float x = BOT_W - BAR_W - 2.0f;

    C2D_DrawRectSolid(x, track_y, 0.0f, BAR_W, track_h, COL_DISABLED);

    float frac = (float)ROWS_SHOWN / (float)st->row_count;
    float thumb_h = track_h * frac;
    float offset = track_h * ((float)st->scroll / (float)st->row_count);
    C2D_DrawRectSolid(x, track_y + offset, 0.0f, BAR_W, thumb_h, COL_DIM);
}

static void draw_bottom(const ui_state_t *st)
{
    C2D_TargetClear(g_bot, COL_BG_BOT);
    C2D_SceneBegin(g_bot);

    draw_text_centred(st->list_title ? st->list_title : "", 0, BOT_W, 12.0f, 0.56f, COL_TEXT);

    if (st->row_count == 0) {
        draw_text_centred("Nothing to show.", 0, BOT_W, 110.0f, 0.52f, COL_DIM);
    }

    for (int v = 0; v < ROWS_SHOWN; v++) {
        int i = st->scroll + v;
        if (i >= st->row_count) break;
        draw_row(&st->rows[i], v);
    }

    draw_scrollbar(st);

    if (st->busy) {
        draw_text_centred("Swapping...", 0, BOT_W, 218.0f, 0.54f, COL_ACTIVE);
    } else if (st->row_count > ROWS_SHOWN) {
        draw_text_centred("Up / Down to scroll", 0, BOT_W, 218.0f, 0.42f, COL_DIM);
    }

    C2D_Flush();
}

// Which row index a released touch landed on, or -1. Only the drawn page is
// considered, so a coordinate outside the list can never resolve to a row.
static int row_at(const ui_state_t *st, int x, int y)
{
    if (x < LIST_X || x > LIST_X + LIST_W) return -1;

    for (int v = 0; v < ROWS_SHOWN; v++) {
        int i = st->scroll + v;
        if (i >= st->row_count) break;

        float top = row_y(v);
        if (y >= top && y <= top + ROW_H) {
            return st->rows[i].enabled ? i : -1;
        }
    }
    return -1;
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
