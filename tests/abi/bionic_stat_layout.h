/* bionic arm32 struct stat layout that libgame.so was compiled against (hardware boot-02 root cause).
 * Checked against the real NDK header by bionic_stat_ndk.c and against our stat64_bionic by
 * stat_layout_vita.c; both must compile. */
#define BIONIC_STAT_SIZE        104
#define BIONIC_STAT_MODE        16
#define BIONIC_STAT_NLINK       20
#define BIONIC_STAT_UID         24
#define BIONIC_STAT_GID         28
#define BIONIC_STAT_RDEV        32
#define BIONIC_STAT_SIZE_OFF    48
#define BIONIC_STAT_BLKSIZE     56
#define BIONIC_STAT_BLOCKS      64
#define BIONIC_STAT_ATIM        72
#define BIONIC_STAT_MTIM        80
#define BIONIC_STAT_CTIM        88
#define BIONIC_STAT_INO         96
