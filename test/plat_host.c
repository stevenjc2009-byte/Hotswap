// Host implementation of the platform layer, for the test harness only.
//
// Adds fault injection that the 3DS build does not have: plat_host_fail_after(n)
// makes the n-th subsequent rename fail. That lets the tests interrupt a real
// swap at every possible point and prove the recovery path actually recovers,
// rather than reasoning about it from the source.

#include "../source/core/plat.h"

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#ifdef _WIN32
#include <direct.h>
#include <io.h>
#define MKDIR(p) _mkdir(p)
#define RMDIR(p) _rmdir(p)
#define UNLINK(p) _unlink(p)
#else
#include <unistd.h>
#define MKDIR(p) mkdir((p), 0777)
#define RMDIR(p) rmdir(p)
#define UNLINK(p) unlink(p)
#endif

static int g_fail_after = -1;   // -1 disables injection
static int g_rename_count = 0;

void plat_host_fail_after(int n)
{
    g_fail_after = n;
    g_rename_count = 0;
}

void plat_host_no_faults(void)
{
    g_fail_after = -1;
    g_rename_count = 0;
}

int plat_host_renames(void)
{
    return g_rename_count;
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
    return (st.st_mode & S_IFDIR) != 0;
}

bool plat_mkdir_p(const char *path)
{
    char tmp[PLAT_MAX_PATH];
    snprintf(tmp, sizeof(tmp), "%s", path);

    size_t len = strlen(tmp);
    while (len > 1 && (tmp[len - 1] == '/' || tmp[len - 1] == '\\')) tmp[--len] = 0;

    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/' || *p == '\\') {
            char c = *p;
            *p = 0;
            if (!plat_exists(tmp)) MKDIR(tmp);
            *p = c;
        }
    }
    if (!plat_exists(tmp)) MKDIR(tmp);
    return plat_is_dir(tmp);
}

bool plat_rename(const char *from, const char *to)
{
    if (g_fail_after >= 0 && g_rename_count >= g_fail_after) {
        g_rename_count++;
        return false;   // simulated power cut
    }
    g_rename_count++;

    if (plat_exists(to)) return false;   // contract: never clobber
    return rename(from, to) == 0;
}

bool plat_rmdir(const char *path)
{
    return RMDIR(path) == 0;
}

bool plat_unlink(const char *path)
{
    return UNLINK(path) == 0;
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
