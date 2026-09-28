/* Compile with the Android NDK (armv7a, API 21): proves the constants are bionic's real layout. */
#include <stddef.h>
#include <sys/stat.h>
#include "bionic_stat_layout.h"
#define CHK(f, v) _Static_assert(offsetof(struct stat, f) == v, #f)
_Static_assert(sizeof(struct stat) == BIONIC_STAT_SIZE, "size");
CHK(st_mode, BIONIC_STAT_MODE); CHK(st_nlink, BIONIC_STAT_NLINK); CHK(st_uid, BIONIC_STAT_UID);
CHK(st_gid, BIONIC_STAT_GID); CHK(st_rdev, BIONIC_STAT_RDEV); CHK(st_size, BIONIC_STAT_SIZE_OFF);
CHK(st_blksize, BIONIC_STAT_BLKSIZE); CHK(st_blocks, BIONIC_STAT_BLOCKS); CHK(st_atim, BIONIC_STAT_ATIM);
CHK(st_mtim, BIONIC_STAT_MTIM); CHK(st_ctim, BIONIC_STAT_CTIM); CHK(st_ino, BIONIC_STAT_INO);
_Static_assert(sizeof(((struct stat *)0)->st_size) == 8, "st_size width");
_Static_assert(sizeof(((struct stat *)0)->st_mode) == 4, "st_mode width");
_Static_assert(sizeof(((struct stat *)0)->st_atim) == 8, "timespec width");
int bionic_stat_ndk_ok;
