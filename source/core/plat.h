// Platform layer for the swap core.
//
// The core is portable C with no libctru dependency, so the whole swap engine
// can be unit-tested on the host against a simulated SD tree. Two impls exist:
//   plat_3ds.c  - libctru / FSUSER, used on hardware
//   plat_host.c - stdio, used by the test harness
//
// Only directory-granular operations are needed. plat_rename MUST be atomic
// for directories (FAT32 rename within one volume is), because the crash
// recovery logic treats the filesystem as the source of truth.

#ifndef MK7SWAP_PLAT_H
#define MK7SWAP_PLAT_H

#include <stdbool.h>
#include <stddef.h>

#define PLAT_MAX_PATH 512
#define PLAT_MAX_NAME 128
#define PLAT_MAX_ENTRIES 64

bool plat_exists(const char *path);
bool plat_is_dir(const char *path);

// Creates every missing component of path. Returns true if it exists after.
bool plat_mkdir_p(const char *path);

// Atomic within one volume. Fails if dst already exists.
bool plat_rename(const char *from, const char *to);

bool plat_rmdir(const char *path);
bool plat_unlink(const char *path);

// Returns count written, or -1 on error. Skips "." and "..".
int plat_list_dir(const char *path, char names[][PLAT_MAX_NAME], int max);

bool plat_write_file(const char *path, const void *data, size_t len);

// Returns bytes read, or -1 if missing/unreadable. Always NUL-terminates.
int plat_read_file(const char *path, char *buf, size_t cap);

#endif
