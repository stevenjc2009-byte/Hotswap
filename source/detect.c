#include "detect.h"
#include "core/mk7titles.h"

#include <3ds.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_TITLES 1024

static bool scan_media(FS_MediaType media, mk7_install_t *out)
{
    u32 count = 0;
    if (R_FAILED(AM_GetTitleCount(media, &count)) || count == 0) return false;
    if (count > MAX_TITLES) count = MAX_TITLES;

    u64 *ids = (u64 *)malloc(sizeof(u64) * count);
    if (!ids) return false;

    u32 read = 0;
    if (R_FAILED(AM_GetTitleList(&read, media, count, ids))) {
        free(ids);
        return false;
    }

    bool hit = false;
    for (u32 i = 0; i < read && !hit; i++) {
        const mk7_title_t *t = mk7_lookup(ids[i]);
        if (!t) continue;

        out->found = true;
        out->title_id = t->title_id;
        out->region = t->region;
        out->on_gamecard = (media == MEDIATYPE_GAME_CARD);
        snprintf(out->hex, sizeof(out->hex), "%s", t->hex);
        hit = true;
    }

    free(ids);
    return hit;
}

void mk7_detect_install(mk7_install_t *out)
{
    memset(out, 0, sizeof(*out));
    out->region = "unknown";

    // A cartridge in the slot takes priority: if the user has the card in,
    // that is the copy they are about to play.
    if (scan_media(MEDIATYPE_GAME_CARD, out)) return;
    scan_media(MEDIATYPE_SD, out);
}

bool mk7_launch(const mk7_install_t *inst)
{
    if (!inst->found) return false;

    u8 media = inst->on_gamecard ? MEDIATYPE_GAME_CARD : MEDIATYPE_SD;

    if (R_FAILED(APT_PrepareToDoApplicationJump(0, inst->title_id, media))) return false;

    u8 param[0x300] = {0};
    u8 hmac[0x20] = {0};
    if (R_FAILED(APT_DoApplicationJump(param, sizeof(param), hmac))) return false;

    return true;   // not reached in practice - the system takes over
}
