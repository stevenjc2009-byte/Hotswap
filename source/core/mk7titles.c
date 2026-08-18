#include "mk7titles.h"

#include <stdlib.h>
#include <string.h>

const mk7_title_t MK7_TITLES[MK7_TITLE_COUNT] = {
    { 0x0004000000030600ULL, "0004000000030600", "Japan"    },
    { 0x0004000000030700ULL, "0004000000030700", "Europe"   },
    { 0x0004000000030800ULL, "0004000000030800", "Americas" },
    { 0x0004000000030A00ULL, "0004000000030A00", "Korea"    },
};

const mk7_title_t *mk7_lookup(uint64_t title_id)
{
    for (int i = 0; i < MK7_TITLE_COUNT; i++) {
        if (MK7_TITLES[i].title_id == title_id) return &MK7_TITLES[i];
    }
    return NULL;
}

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

const mk7_title_t *mk7_lookup_hex(const char *hex)
{
    if (!hex) return NULL;
    for (int i = 0; i < MK7_TITLE_COUNT; i++) {
        if (ci_equal(MK7_TITLES[i].hex, hex)) return &MK7_TITLES[i];
    }
    return NULL;
}
