/* Compile with the VitaSDK softfp toolchain exactly as the port does: our stat64_bionic must match. */
#include <stddef.h>
#include <sys/types.h>
#include "reimpl/io.h"
#include "bionic_stat_layout.h"
#define CHK(f, v) _Static_assert(offsetof(stat64_bionic, f) == v, #f)
_Static_assert(sizeof(stat64_bionic) == BIONIC_STAT_SIZE, "size");
CHK(st_mode, BIONIC_STAT_MODE); CHK(st_nlink, BIONIC_STAT_NLINK); CHK(st_uid, BIONIC_STAT_UID);
CHK(st_gid, BIONIC_STAT_GID); CHK(st_rdev, BIONIC_STAT_RDEV); CHK(st_size, BIONIC_STAT_SIZE_OFF);
CHK(st_blksize, BIONIC_STAT_BLKSIZE); CHK(st_blocks, BIONIC_STAT_BLOCKS); CHK(st_atim, BIONIC_STAT_ATIM);
CHK(st_mtim, BIONIC_STAT_MTIM); CHK(st_ctim, BIONIC_STAT_CTIM); CHK(st_ino, BIONIC_STAT_INO);
_Static_assert(sizeof(((stat64_bionic *)0)->st_size) == 8, "st_size width");
_Static_assert(sizeof(((stat64_bionic *)0)->st_mode) == 4, "st_mode width");
_Static_assert(sizeof(((stat64_bionic *)0)->st_atim) == 8, "timespec width");
int stat_layout_vita_ok;
