/*
 * Copyright (C) 2021      Andy Nguyen
 * Copyright (C) 2022      Rinnegatamante
 * Copyright (C) 2022-2024 Volodymyr Atamanenko
 *
 * This software may be modified and distributed under the terms
 * of the MIT license. See the LICENSE file for details.
 */

/**
 * @file  io.h
 * @brief Wrappers and implementations for some of the IO functions.
 */

#ifndef SOLOADER_IO_H
#define SOLOADER_IO_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdio.h>
#include <sys/dirent.h>
#include <sys/syslimits.h>
#include <sys/fcntl.h>

#ifndef PATH_MAX
#define PATH_MAX 1024
#endif

#ifndef DT_DIR
#define DT_UNKNOWN 0
#define DT_FIFO 1
#define DT_CHR 2
#define DT_DIR 4
#define DT_BLK 6
#define DT_REG 8
#define DT_LNK 10
#define DT_SOCK 12
#define DT_WHT 14
#endif

/*
 * bionic 32-bit ARM `struct stat` (== kernel stat64), as libgame.so expects it.
 * aod-vita fix (hardware boot-02 core dump): the previous __packed__ definition used newlib's
 * 16-bit nlink_t/uid_t/gid_t, putting st_size at offset 38 instead of 48 (sizeof 90, not 104),
 * so libgame read st_size == 0 for a 24-byte save file and parsed an empty buffer.
 * Offsets below are asserted against the Android NDK r27d bionic headers (tests/abi).
 */
#ifdef __cplusplus
#define AOD_STATIC_ASSERT(c, m) static_assert(c, m)
#else
#define AOD_STATIC_ASSERT(c, m) _Static_assert(c, m)
#endif

typedef struct bionic_timespec32 {
    int32_t tv_sec;
    int32_t tv_nsec;
} bionic_timespec32;

typedef struct stat64_bionic {
    uint64_t st_dev;          //  0
    uint8_t  __pad0[4];       //  8
    uint32_t __st_ino;        // 12
    uint32_t st_mode;         // 16
    uint32_t st_nlink;        // 20
    uint32_t st_uid;          // 24
    uint32_t st_gid;          // 28
    uint64_t st_rdev;         // 32
    uint8_t  __pad3[4];       // 40
    uint8_t  __pad3b[4];      // 44 (alignment of st_size)
    int64_t  st_size;         // 48
    uint32_t st_blksize;      // 56
    uint8_t  __pad4[4];       // 60 (alignment of st_blocks)
    uint64_t st_blocks;       // 64
    bionic_timespec32 st_atim; // 72
    bionic_timespec32 st_mtim; // 80
    bionic_timespec32 st_ctim; // 88
    uint64_t st_ino;          // 96
} stat64_bionic;              // 104

AOD_STATIC_ASSERT(sizeof(stat64_bionic) == 104, "bionic arm32 struct stat size");
AOD_STATIC_ASSERT(__builtin_offsetof(stat64_bionic, st_mode) == 16, "st_mode");
AOD_STATIC_ASSERT(__builtin_offsetof(stat64_bionic, st_nlink) == 20, "st_nlink");
AOD_STATIC_ASSERT(__builtin_offsetof(stat64_bionic, st_uid) == 24, "st_uid");
AOD_STATIC_ASSERT(__builtin_offsetof(stat64_bionic, st_gid) == 28, "st_gid");
AOD_STATIC_ASSERT(__builtin_offsetof(stat64_bionic, st_rdev) == 32, "st_rdev");
AOD_STATIC_ASSERT(__builtin_offsetof(stat64_bionic, st_size) == 48, "st_size");
AOD_STATIC_ASSERT(__builtin_offsetof(stat64_bionic, st_blksize) == 56, "st_blksize");
AOD_STATIC_ASSERT(__builtin_offsetof(stat64_bionic, st_blocks) == 64, "st_blocks");
AOD_STATIC_ASSERT(__builtin_offsetof(stat64_bionic, st_atim) == 72, "st_atim");
AOD_STATIC_ASSERT(__builtin_offsetof(stat64_bionic, st_mtim) == 80, "st_mtim");
AOD_STATIC_ASSERT(__builtin_offsetof(stat64_bionic, st_ctim) == 88, "st_ctim");
AOD_STATIC_ASSERT(__builtin_offsetof(stat64_bionic, st_ino) == 96, "st_ino");

typedef struct __attribute__((__packed__)) dirent64_bionic {
    int16_t d_ino; // 2 bytes // offset 0x0
    int64_t d_off; // 8 bytes // offset 0x2
    uint64_t d_reclen; // 8 bytes // 0xA
    unsigned char d_type; // 1 byte // offset 0x12
    char d_name[256]; // 256 bytes // offset 0x13
} dirent64_bionic;

int open_soloader(const char * path, int oflag, ...);

FILE * fopen_soloader(const char * filename, const char * mode);

DIR *opendir_soloader(char *name);

int stat_soloader(const char * path, stat64_bionic * buf);

int fstat_soloader(int fd, stat64_bionic * buf);

struct dirent64_bionic * readdir_soloader(DIR *dir);

int readdir_r_soloader(DIR * dirp, dirent64_bionic * entry,
                       dirent64_bionic ** result);

int close_soloader(int fd);

int fclose_soloader(FILE *f);

int closedir_soloader(DIR *dir);

int fcntl_soloader(int fd, int cmd, ...);

int ioctl_soloader(int fd, int request, ... /* arg */);

int fsync_soloader(int fd);

#ifdef __cplusplus
};
#endif

#endif // SOLOADER_IO_H
