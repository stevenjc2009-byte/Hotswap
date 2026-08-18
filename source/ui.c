// Bottom-screen touch UI.
//
// Deliberately plain: solid rectangles and text, no textures. That keeps it
// identical on every model in the family (Old 3DS, New 3DS, 2DS, New 2DS XL)
// and keeps the CIA small. Stereoscopic 3D is never enabled, because the 2DS
// and 2DS XL have no slider and would just render the left eye anyway.

#include "ui.h"

#include <3ds.h>
#include <citro2d.h>
#include <stdio.h>
#include <string.h>

#define TOP_W 400
#define BOT_W 320
#define SCR_H 240

// Button geometry on the bottom screen.
#define BTN_MARGIN 8
#define BTN_GAP    8
#define BTN_W      ((BOT_W - (2 * BTN_MARGIN) - (2 * BTN_GAP)) / 3)   // 96
#define BTN_Y      64
#define BTN_H      124

static C3D_RenderTarget *g_top;
static C3D_RenderTarget *g_bot;
static C2D_TextBuf g_buf;

// Palette. CTGP-7 gets its own colour so the middle button reads as "the
// colourful one" when it is available, and drains to grey when it is not.
#define COL_BG_TOP    C2D_Color32(0x14, 0x16, 0x1C, 0xFF)
#define COL_BG_BOT    C2D_Color32(0x1B, 0x1E, 0x26, 0xFF)
#define COL_TEXT      C2D_Color32(0xF2, 0xF4, 0xF8, 0xFF)
#define COL_DIM       C2D_Color32(0x8A, 0x90, 0x9E, 0xFF)
#define COL_STOCK     C2D_Color32(0x2E, 0x6D, 0xB8, 0xFF)
#define COL_CTGP      C2D_Color32(0xD8, 0x4A, 0x1E, 0xFF)
#define COL_CUSTOM    C2D_Color32(0x2E, 0x9E, 0x5B, 0xFF)
#define COL_DISABLED  C2D_Color32(0x33, 0x37, 0x42, 0xFF)
#define COL_ACTIVE    C2D_Color32(0xFF, 0xD5, 0x4A, 0xFF)
#define COL_ERROR     C2D_Color32(0xE0, 0x4B, 0x4B, 0xFF)

// C2D_Color32 is an inline function rather than a constant expression, so the
// palette cannot live in a static initialiser.
static u32 slot_colour(int slot)
{
    switch (slot) {
        case UI_SLOT_STOCK:  return COL_STOCK;
        case UI_SLOT_CTGP:   return COL_CTGP;
        case UI_SLOT_CUSTOM: return COL_CUSTOM;
    }
    return COL_DISABLED;
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

static int button_x(int i)
{
    return BTN_MARGIN + i * (BTN_W + BTN_GAP);
}

static void draw_top(const ui_state_t *st)
{
    C2D_TargetClear(g_top, COL_BG_TOP);
    C2D_SceneBegin(g_top);

    draw_text("Mario Kart 7  -  Mod Swapper", 12.0f, 12.0f, 0.62f, COL_TEXT);

    char line[128];
    if (st->install && st->install->found) {
        snprintf(line, sizeof(line), "Detected: Mario Kart 7 (%s)%s",
                 st->install->region,
                 st->install->on_gamecard ? "  [cartridge]" : "  [SD]");
        draw_text(line, 12.0f, 44.0f, 0.50f, COL_DIM);

        snprintf(line, sizeof(line), "Title ID: %s", st->install->hex);
        draw_text(line, 12.0f, 64.0f, 0.50f, COL_DIM);
    } else {
        draw_text("Mario Kart 7 was not found on this console.", 12.0f, 44.0f, 0.50f, COL_ERROR);
        draw_text("Install it, or insert the cartridge, then reopen.", 12.0f, 64.0f, 0.50f, COL_DIM);
    }

    if (st->status) draw_text(st->status, 12.0f, 100.0f, 0.52f, COL_TEXT);

    if (st->error) {
        draw_text(st->error, 12.0f, 132.0f, 0.52f, COL_ERROR);
        draw_text("Nothing was changed. Your files are untouched.",
                  12.0f, 152.0f, 0.48f, COL_DIM);
    }

    draw_text("Touch a mode below.  START to exit.", 12.0f, 208.0f, 0.48f, COL_DIM);
    C2D_Flush();
}

static void draw_bottom(const ui_state_t *st)
{
    C2D_TargetClear(g_bot, COL_BG_BOT);
    C2D_SceneBegin(g_bot);

    draw_text_centred("Choose what to play", 0, BOT_W, 16.0f, 0.56f, COL_TEXT);

    for (int i = 0; i < UI_SLOT_COUNT; i++) {
        const ui_button_t *b = &st->buttons[i];
        float x = (float)button_x(i);

        u32 fill = b->enabled ? slot_colour(i) : COL_DISABLED;
        C2D_DrawRectSolid(x, BTN_Y, 0.0f, BTN_W, BTN_H, fill);

        // A gold band along the top marks the mode that is currently live.
        if (b->active) C2D_DrawRectSolid(x, BTN_Y, 0.0f, BTN_W, 6.0f, COL_ACTIVE);

        u32 label_col = b->enabled ? COL_TEXT : COL_DIM;
        draw_text_centred(b->title, x, BTN_W, BTN_Y + 44.0f, 0.60f, label_col);

        if (b->subtitle) {
            draw_text_centred(b->subtitle, x, BTN_W, BTN_Y + 72.0f, 0.42f, label_col);
        }
        if (b->active) {
            draw_text_centred("ACTIVE", x, BTN_W, BTN_Y + 96.0f, 0.42f, COL_ACTIVE);
        }
    }

    if (st->busy) {
        draw_text_centred("Swapping...", 0, BOT_W, 206.0f, 0.54f, COL_ACTIVE);
    }

    C2D_Flush();
}

int ui_frame(const ui_state_t *st)
{
    C2D_TextBufClear(g_buf);

    C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
    draw_top(st);
    draw_bottom(st);
    C3D_FrameEnd(0);

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
        if (last_y >= BTN_Y && last_y <= BTN_Y + BTN_H) {
            for (int i = 0; i < UI_SLOT_COUNT; i++) {
                int x = button_x(i);
                if (last_x >= x && last_x <= x + BTN_W && st->buttons[i].enabled) {
                    hit = i;
                    break;
                }
            }
        }
        last_x = last_y = -1;
    }

    return hit;
}
