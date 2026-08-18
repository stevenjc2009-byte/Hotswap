#include "detect.h"

#include <3ds.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_TITLES 1024

// Records one hit, unless this game was already found on media that outranks
// the current one. The game card outranks SD: a card in the slot is the copy
// the user is about to play, so that is the one the launch jump must target.
static void record(hs_install_t *out, int *count, int max,
                   const hs_game_t *game, const hs_title_t *title, bool gamecard)
{
    hs_install_t *slot = NULL;
    for (int i = 0; i < *count; i++) {
        if (out[i].game == game) { slot = &out[i]; break; }
    }

    // Already listed, and nothing to gain: either it is already the card copy,
    // or this hit is just another region of a game we have.
    if (slot && (slot->on_gamecard || !gamecard)) return;

    if (!slot) {
        if (*count >= max) return;
        slot = &out[(*count)++];
    }

    memset(slot, 0, sizeof(*slot));
    slot->found = true;
    slot->game = game;
    slot->title_id = title->title_id;
    slot->region = title->region;
    slot->on_gamecard = gamecard;
    snprintf(slot->hex, sizeof(slot->hex), "%s", title->hex);
}

static void scan_media(FS_MediaType media, hs_install_t *out, int *count, int max)
{
    u32 total = 0;
    if (R_FAILED(AM_GetTitleCount(media, &total)) || total == 0) return;
    if (total > MAX_TITLES) total = MAX_TITLES;

    u64 *ids = (u64 *)malloc(sizeof(u64) * total);
    if (!ids) return;

    u32 read = 0;
    if (R_FAILED(AM_GetTitleList(&read, media, total, ids))) {
        free(ids);
        return;
    }

    bool gamecard = (media == MEDIATYPE_GAME_CARD);
    for (u32 i = 0; i < read; i++) {
        const hs_title_t *t = NULL;
        const hs_game_t *g = hs_game_by_title(ids[i], &t);
        if (g) record(out, count, max, g, t, gamecard);
    }

    free(ids);
}

int hs_detect_installs(hs_install_t *out, int max)
{
    int count = 0;
    if (!out || max <= 0) return 0;

    // SD first, then the card, so the card's higher rank is applied as an
    // upgrade rather than depending on scan order.
    scan_media(MEDIATYPE_SD, out, &count, max);
    scan_media(MEDIATYPE_GAME_CARD, out, &count, max);
    return count;
}

// Arms the jump; it happens later, during aptExit(), on the way out of main.
//
// This deliberately does NOT call APT_PrepareToDoApplicationJump and
// APT_DoApplicationJump itself. libctru issues those two commands (APT 0x31 and
// 0x32) from inside aptExit, gated on the state aptSetChainloader writes -
// firing them by hand mid-run leaves a jump pending that libctru knows nothing
// about, and it then runs its normal shutdown handshake on top of it. The
// console ends up on a black screen with the HOME button dead and only a hard
// reset gets out. Handing the target to libctru instead keeps one exit path.
bool hs_launch(const hs_install_t *inst)
{
    if (!inst || !inst->found) return false;

    u8 media = inst->on_gamecard ? MEDIATYPE_GAME_CARD : MEDIATYPE_SD;
    aptSetChainloader(inst->title_id, media);
    return true;
}
