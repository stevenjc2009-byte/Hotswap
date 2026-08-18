// Hardware implementation of the platform layer.
//
// Directory renames go through FSUSER_RenameDirectory rather than newlib's
// rename(), because the sdmc devoptab's handling of directory renames is not
// something worth betting the user's mod folder on. Everything else (stat,
// opendir, fopen) goes through the devoptab, which is well-trodden.

#include "core/plat.h"

#include <3ds.h>
#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static FS_Archive g_sdmc;
static bool g_have_archive = false;

bool plat_3ds_init(void)
{
    if (g_have_archive) return true;
    Result rc = FSUSER_OpenArchive(&g_sdmc, ARCHIVE_SDMC, fsMakePath(PATH_EMPTY, ""));
    g_have_archive = R_SUCCEEDED(rc);
    return g_have_archive;
}

void plat_3ds_exit(void)
{
    if (g_have_archive) {
        FSUSER_CloseArchive(g_sdmc);
        g_have_archive = false;
    }
}

// "sdmc:/luma/titles/x" -> "/luma/titles/x", which is what FSUSER wants.
static const char *strip_drive(const char *path)
{
    const char *colon = strchr(path, ':');
    return colon ? colon + 1 : path;
}

bool plat_exists(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0;
}

bool plat_is_dir(const char *path)
{
    struct stat st;
    if (stat(path, &st) != 0) return false;
    return S_ISDIR(st.st_mode);
}

bool plat_mkdir_p(const char *path)
{
    char tmp[PLAT_MAX_PATH];
    snprintf(tmp, sizeof(tmp), "%s", path);

    size_t len = strlen(tmp);
    while (len > 1 && tmp[len - 1] == '/') tmp[--len] = 0;

    // Skip past "sdmc:/" so we never try to mkdir the drive itself.
    char *start = tmp;
    const char *colon = strchr(tmp, ':');
    if (colon) start = (char *)colon + 2;

    for (char *p = start; *p; p++) {
        if (*p == '/') {
            *p = 0;
            if (!plat_exists(tmp)) mkdir(tmp, 0777);
            *p = '/';
        }
    }
    if (!plat_exists(tmp)) mkdir(tmp, 0777);
    return plat_is_dir(tmp);
}

bool plat_rename(const char *from, const char *to)
{
    if (!g_have_archive) return false;
    if (plat_exists(to)) return false;   // contract: never clobber

    bool dir = plat_is_dir(from);

    FS_Path fp = fsMakePath(PATH_ASCII, strip_drive(from));
    FS_Path tp = fsMakePath(PATH_ASCII, strip_drive(to));

    Result rc = dir ? FSUSER_RenameDirectory(g_sdmc, fp, g_sdmc, tp)
                    : FSUSER_RenameFile(g_sdmc, fp, g_sdmc, tp);
    return R_SUCCEEDED(rc);
}

bool plat_rmdir(const char *path)
{
    return rmdir(path) == 0;
}

bool plat_unlink(const char *path)
{
    return remove(path) == 0;
}

int plat_list_dir(const char *path, char names[][PLAT_MAX_NAME], int max)
{
    DIR *d = opendir(path);
    if (!d) return -1;

    int n = 0;
    struct dirent *e;
    while ((e = readdir(d)) != NULL && n < max) {
        if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0) continue;

        // A name too long to hold would come back truncated and no longer
        // open, so report it as absent rather than as something it is not.
        size_t len = strlen(e->d_name);
        if (len >= PLAT_MAX_NAME) continue;

        memcpy(names[n], e->d_name, len + 1);
        n++;
    }
    closedir(d);
    return n;
}

bool plat_write_file(const char *path, const void *data, size_t len)
{
    FILE *f = fopen(path, "wb");
    if (!f) return false;
    size_t w = fwrite(data, 1, len, f);
    fclose(f);
    return w == len;
}

int plat_read_file(const char *path, char *buf, size_t cap)
{
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    size_t r = fread(buf, 1, cap - 1, f);
    fclose(f);
    buf[r] = 0;
    return (int)r;
}
