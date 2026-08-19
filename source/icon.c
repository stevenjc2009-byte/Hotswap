#include "icon.h"

#include <3ds.h>
#include <string.h>

#include "detect.h"

// The SMDH is the metadata blob every title carries in its ExeFS: names,
// ratings, and the two icons the HOME menu draws. Only the tail of it matters
// here, but it has to be read as a whole because the icon sits at a fixed
// offset from the start. libctru has no public smdh struct - every project
// that needs one declares its own, and this is that declaration.
typedef struct {
    uint32_t magic;              // 'SMDH'
    uint16_t version;
    uint16_t reserved;
    uint16_t titles[16][0x100];  // short + long + publisher, per language
    uint8_t  settings[0x30];
    uint8_t  reserved2[0x8];
    uint8_t  small_icon[0x480];  // 24x24
    uint16_t big_icon[0x900];    // 48x48, RGB565, already GPU-tiled
} smdh_t;

// The icon's offset inside the blob is what makes the struct above load-bearing,
// so a miscounted field is caught at compile time rather than by drawing noise.
_Static_assert(sizeof(smdh_t) == 0x36C0, "SMDH must be 0x36C0 bytes");
_Static_assert(__builtin_offsetof(smdh_t, big_icon) == 0x24C0,
               "SMDH large icon must sit at 0x24C0");

#define ICON_SIDE 48
#define TEX_SIDE  64

// One texture per game the console has. Bounded by the registry, so this cannot
// grow with what is on the SD card.
#define MAX_ICONS HS_MAX_INSTALLS

static C3D_Tex g_tex[MAX_ICONS];
static int g_used = 0;

// All icons are the same size, so one sub-texture describes every one of them.
//
// The copy below starts 16 rows into the texture, and V runs the opposite way
// to the memory order, so those skipped rows land at the BOTTOM of the V range
// and the image occupies 0.0 to 48/64. Getting this the wrong way round draws
// a black band across the top quarter of every icon, which is exactly what the
// first emulator run showed.
static const Tex3DS_SubTexture ICON_SUBTEX = {
    .width  = ICON_SIDE,
    .height = ICON_SIDE,
    .left   = 0.0f,
    .top    = (float)ICON_SIDE / (float)TEX_SIDE,
    .right  = (float)ICON_SIDE / (float)TEX_SIDE,
    .bottom = 0.0f,
};

// Opens <title>:/exefs/icon through the archive that maps a title's own
// content. The two paths are binary rather than textual: the archive path is
// the title id split into two words plus its media type, and the file path is a
// fixed blob that means "the ExeFS section named icon".
static bool read_smdh(uint64_t title_id, bool on_gamecard, smdh_t *out)
{
    uint32_t archive_path[4] = {
        (uint32_t)(title_id & 0xFFFFFFFF),
        (uint32_t)(title_id >> 32),
        on_gamecard ? MEDIATYPE_GAME_CARD : MEDIATYPE_SD,
        0,
    };
    // { 0, 0, 2 = ExeFS, "icon" little-endian, 0 }
    static const uint32_t FILE_PATH[5] = { 0, 0, 2, 0x6E6F6369, 0 };

    FS_Path apath = { PATH_BINARY, sizeof(archive_path), archive_path };
    FS_Path fpath = { PATH_BINARY, sizeof(FILE_PATH), FILE_PATH };

    Handle fh = 0;
    if (R_FAILED(FSUSER_OpenFileDirectly(&fh, ARCHIVE_SAVEDATA_AND_CONTENT,
                                         apath, fpath, FS_OPEN_READ, 0))) {
        return false;
    }

    uint32_t got = 0;
    Result rc = FSFILE_Read(fh, &got, 0, out, sizeof(*out));
    FSFILE_Close(fh);

    // A short read would leave the icon half-filled with stack garbage, which
    // draws as noise rather than failing - so it is rejected outright.
    return R_SUCCEEDED(rc) && got == sizeof(*out) && out->magic == 0x48444D53;
}

bool icon_load(uint64_t title_id, bool on_gamecard, C2D_Image *out)
{
    if (!out) return false;
    out->tex = NULL;
    out->subtex = NULL;

    if (g_used >= MAX_ICONS) return false;

    smdh_t smdh;
    if (!read_smdh(title_id, on_gamecard, &smdh)) return false;

    C3D_Tex *tex = &g_tex[g_used];
    if (!C3D_TexInit(tex, TEX_SIDE, TEX_SIDE, GPU_RGB565)) return false;

    // The corner of the texture the icon never reaches would otherwise hold
    // whatever was in that VRAM before, and linear filtering drags it into the
    // edge pixels.
    memset(tex->data, 0, tex->size);

    // No untiling: the SMDH stores the icon in exactly the 8x8 tile order the
    // GPU wants, so this is a straight copy. It goes one 8-row band at a time
    // only because the source rows are 48 wide and the destination's are 64.
    uint16_t *dst = (uint16_t *)tex->data + (TEX_SIDE - ICON_SIDE) * TEX_SIDE;
    const uint16_t *src = smdh.big_icon;
    for (int row = 0; row < ICON_SIDE; row += 8) {
        memcpy(dst, src, ICON_SIDE * 8 * sizeof(uint16_t));
        src += ICON_SIDE * 8;
        dst += TEX_SIDE * 8;
    }

    // Drawn slightly smaller than native to fit the row, so it is filtered
    // rather than point-sampled.
    C3D_TexSetFilter(tex, GPU_LINEAR, GPU_LINEAR);
    C3D_TexSetWrap(tex, GPU_CLAMP_TO_EDGE, GPU_CLAMP_TO_EDGE);

    g_used++;
    out->tex = tex;
    out->subtex = &ICON_SUBTEX;
    return true;
}

void icon_free_all(void)
{
    for (int i = 0; i < g_used; i++) C3D_TexDelete(&g_tex[i]);
    g_used = 0;
}
