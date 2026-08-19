#include "games.h"

#include <stddef.h>

// Verified against kartdlphax, the MK7 CTRPF plugin thread, MK7 PID Grabber,
// hShop and Bsquo/mk7-patch-collection - five independent sources.
static const hs_title_t MK7_TITLES[] = {
    { 0x0004000000030600ULL, "0004000000030600", "Japan"    },
    { 0x0004000000030700ULL, "0004000000030700", "Europe"   },
    { 0x0004000000030800ULL, "0004000000030800", "Americas" },
    { 0x0004000000030A00ULL, "0004000000030A00", "Korea"    },
};

const hs_game_t HS_GAMES[HS_GAME_COUNT] = {
    {
        .slug = "mk7",
        .name = "Mario Kart 7",
        .short_name = "Mario Kart 7",
        .community = "CTGP-7",
        .community_slug = "ctgp7",
        .community_dir = "CTGP-7",
        .community_plugin = "CTGP-7/resources/CTGP-7.3gx",
        .titles = MK7_TITLES,
        .title_count = (int)(sizeof(MK7_TITLES) / sizeof(MK7_TITLES[0])),
    },
};

static int ci_equal(const char *a, const char *b)
{
    while (*a && *b) {
        char ca = *a, cb = *b;
        if (ca >= 'a' && ca <= 'z') ca -= 32;
        if (cb >= 'a' && cb <= 'z') cb -= 32;
        if (ca != cb) return 0;
        a++; b++;
    }
    return *a == 0 && *b == 0;
}

static int str_equal(const char *a, const char *b)
{
    while (*a && *b) {
        if (*a != *b) return 0;
        a++; b++;
    }
    return *a == 0 && *b == 0;
}

const hs_game_t *hs_game_by_slug(const char *slug)
{
    if (!slug) return NULL;
    for (int g = 0; g < HS_GAME_COUNT; g++) {
        if (str_equal(HS_GAMES[g].slug, slug)) return &HS_GAMES[g];
    }
    return NULL;
}

const hs_game_t *hs_game_by_title(uint64_t title_id, const hs_title_t **out_title)
{
    for (int g = 0; g < HS_GAME_COUNT; g++) {
        const hs_game_t *game = &HS_GAMES[g];
        for (int t = 0; t < game->title_count; t++) {
            if (game->titles[t].title_id == title_id) {
                if (out_title) *out_title = &game->titles[t];
                return game;
            }
        }
    }
    return NULL;
}

const hs_game_t *hs_game_by_hex(const char *hex, const hs_title_t **out_title)
{
    if (!hex) return NULL;
    for (int g = 0; g < HS_GAME_COUNT; g++) {
        const hs_game_t *game = &HS_GAMES[g];
        for (int t = 0; t < game->title_count; t++) {
            if (ci_equal(game->titles[t].hex, hex)) {
                if (out_title) *out_title = &game->titles[t];
                return game;
            }
        }
    }
    return NULL;
}
