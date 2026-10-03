/*
 * tests/input_config_faults.c
 * GNU --wrap fault-injection harness for aod_input_config_save / _load.
 *
 * Build (example):
 *   cc -D_POSIX_C_SOURCE=200809L -std=c11 -Wall -Wextra -Werror \
 *      source/aod/input_config.c tests/input_config_faults.c \
 *      -I source \
 *      -Wl,--wrap=mkstemp,--wrap=fdopen,--wrap=fwrite,--wrap=fflush, \
 *          --wrap=fileno,--wrap=fsync,--wrap=fclose,--wrap=rename, \
 *          --wrap=fopen,--wrap=fread,--wrap=ferror \
 *      -o input_config_faults_test
 *   ./input_config_faults_test /tmp/mytest
 *
 * NOTE: this file relaxes P10 rule (bounded loops) only for opendir/readdir
 * which is bounded to MAX_DIR_SCAN entries; no other relaxation.
 */

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "aod/input_config.h"

/* ── real-symbol forward declarations ────────────────────────────────── */
extern int    __real_mkstemp(char *tmpl);
extern FILE  *__real_fdopen(int fd, const char *mode);
extern size_t __real_fwrite(const void *ptr, size_t sz, size_t n, FILE *fp);
extern int    __real_fflush(FILE *fp);
extern int    __real_fileno(FILE *fp);
extern int    __real_fsync(int fd);
extern int    __real_fclose(FILE *fp);
extern int    __real_rename(const char *old, const char *nw);
extern FILE  *__real_fopen(const char *path, const char *mode);
extern size_t __real_fread(void *ptr, size_t sz, size_t n, FILE *fp);
extern int    __real_ferror(FILE *fp);
/*
 * FORTIFY_SOURCE on Linux replaces fread() calls with __fread_chk() when the
 * buffer size is statically known.  nm -u on the ASAN object confirms the
 * compiler emits "U __fread_chk" instead of "U fread".  We must wrap the
 * fortified entry-point too; the real symbol is forwarded for non-fault paths.
 * Signature: size_t __fread_chk(void *ptr, size_t buflen,
 *                               size_t sz, size_t n, FILE *fp)
 */
extern size_t __real___fread_chk(void *ptr, size_t buflen,
                                 size_t sz, size_t n, FILE *fp);

/* ── fault injection globals ─────────────────────────────────────────── */
typedef enum {
    FAULT_NONE = 0,
    FAULT_MKSTEMP, FAULT_FDOPEN, FAULT_FWRITE_ZERO, FAULT_FWRITE_SHORT,
    FAULT_FFLUSH,  FAULT_FILENO, FAULT_FSYNC,       FAULT_FCLOSE,
    FAULT_RENAME,  FAULT_FOPEN_EACCES, FAULT_FOPEN_ENOENT,
    FAULT_FREAD_PARTIAL, FAULT_FREAD_FERROR, FAULT_FCLOSE_LOAD
} fault_id;

static fault_id g_fault      = FAULT_NONE;
static int      g_fault_fired = 0;  /* set to 1 when wrapper triggers */

static void set_fault(fault_id f) { g_fault = f; g_fault_fired = 0; }
static void clear_fault(void)     { g_fault = FAULT_NONE; g_fault_fired = 0; }

/* ── wrap implementations ────────────────────────────────────────────── */
int __wrap_mkstemp(char *tmpl)
{
    if (g_fault == FAULT_MKSTEMP) { g_fault_fired = 1; errno = EACCES; return -1; }
    return __real_mkstemp(tmpl);
}

FILE *__wrap_fdopen(int fd, const char *mode)
{
    if (g_fault == FAULT_FDOPEN) { g_fault_fired = 1; errno = ENOMEM; return NULL; }
    return __real_fdopen(fd, mode);
}

size_t __wrap_fwrite(const void *ptr, size_t sz, size_t n, FILE *fp)
{
    if (g_fault == FAULT_FWRITE_ZERO)  { g_fault_fired = 1; return 0; }
    if (g_fault == FAULT_FWRITE_SHORT) { g_fault_fired = 1; return (n > 1) ? n - 1 : 0; }
    return __real_fwrite(ptr, sz, n, fp);
}

int __wrap_fflush(FILE *fp)
{
    if (g_fault == FAULT_FFLUSH) { g_fault_fired = 1; errno = EIO; return EOF; }
    return __real_fflush(fp);
}

int __wrap_fileno(FILE *fp)
{
    if (g_fault == FAULT_FILENO) { g_fault_fired = 1; errno = EBADF; return -1; }
    return __real_fileno(fp);
}

int __wrap_fsync(int fd)
{
    if (g_fault == FAULT_FSYNC) { g_fault_fired = 1; errno = EIO; return -1; }
    return __real_fsync(fd);
}

/* fclose fault: call real first (no leak), check result, then force EOF */
int __wrap_fclose(FILE *fp)
{
    if (g_fault == FAULT_FCLOSE || g_fault == FAULT_FCLOSE_LOAD) {
        g_fault_fired = 1;
        int rr = __real_fclose(fp);
        if (rr != 0) {
            fprintf(stderr,
                    "NOTE: __real_fclose returned %d in fault wrapper\n", rr);
        }
        return EOF;
    }
    return __real_fclose(fp);
}

int __wrap_rename(const char *old, const char *nw)
{
    if (g_fault == FAULT_RENAME) { g_fault_fired = 1; errno = EACCES; return -1; }
    return __real_rename(old, nw);
}

FILE *__wrap_fopen(const char *path, const char *mode)
{
    if (g_fault == FAULT_FOPEN_EACCES) { g_fault_fired = 1; errno = EACCES; return NULL; }
    if (g_fault == FAULT_FOPEN_ENOENT) { g_fault_fired = 1; errno = ENOENT; return NULL; }
    return __real_fopen(path, mode);
}

static int g_fread_partial_triggered = 0;
size_t __wrap_fread(void *ptr, size_t sz, size_t n, FILE *fp)
{
    if (g_fault == FAULT_FREAD_PARTIAL && !g_fread_partial_triggered) {
        g_fault_fired = 1; g_fread_partial_triggered = 1;
        /* return 1 byte — causes parse to fail */
        return __real_fread(ptr, 1, 1, fp);
    }
    if (g_fault == FAULT_FREAD_FERROR) {
        /* read nothing; ferror wrapper will signal error */
        g_fault_fired = 1;
        return 0;
    }
    return __real_fread(ptr, sz, n, fp);
}

int __wrap_ferror(FILE *fp)
{
    if (g_fault == FAULT_FREAD_FERROR) { return 1; }
    return __real_ferror(fp);
}

/*
 * __wrap___fread_chk — intercepts the FORTIFY_SOURCE fread replacement.
 * Uses identical fault semantics as __wrap_fread:
 *   FAULT_FREAD_PARTIAL: read exactly 1 byte via __real_fread (safe only when
 *                        buflen >= 1; we guard before calling).
 *   FAULT_FREAD_FERROR:  return 0; __wrap_ferror will signal the error.
 * Normal path: delegate to __real___fread_chk (preserves FORTIFY checks).
 */
size_t __wrap___fread_chk(void *ptr, size_t buflen,
                          size_t sz, size_t n, FILE *fp)
{
    if (g_fault == FAULT_FREAD_PARTIAL && !g_fread_partial_triggered) {
        g_fault_fired = 1;
        g_fread_partial_triggered = 1;
        if (buflen >= 1) {
            return __real_fread(ptr, 1, 1, fp);
        }
        /* buflen == 0 would overflow; return 0 to signal short read */
        return 0;
    }
    if (g_fault == FAULT_FREAD_FERROR) {
        g_fault_fired = 1;
        return 0;
    }
    return __real___fread_chk(ptr, buflen, sz, n, fp);
}

/* ── assertion helpers ───────────────────────────────────────────────── */
static int g_assertions = 0;
static int g_failures   = 0;

#define CHECK(cond, msg) do { \
    g_assertions++; \
    if (!(cond)) { g_failures++; \
        fprintf(stderr, "FAIL [%s:%d] %s\n", __FILE__, __LINE__, (msg)); } \
} while (0)

/* ── directory temp-count helper ─────────────────────────────────────── */
#define MAX_DIR_SCAN 128
#define CFG_TMP_PREFIX ".tmp."

static int count_cfg_tmps(const char *dir, const char *base)
{
    DIR           *d;
    struct dirent *ent;
    int            count   = 0;
    int            scanned = 0;
    size_t         blen    = strlen(base);
    char           prefix[256];
    size_t         plen;

    /* prefix = "<base>.tmp." */
    if (blen + sizeof(CFG_TMP_PREFIX) >= sizeof(prefix)) { return -1; }
    memcpy(prefix, base, blen);
    memcpy(prefix + blen, CFG_TMP_PREFIX, sizeof(CFG_TMP_PREFIX));
    plen = blen + sizeof(CFG_TMP_PREFIX) - 1;

    errno = 0;
    d = opendir(dir);
    if (!d) { return -1; }

    errno = 0;
    while (scanned < MAX_DIR_SCAN && (ent = readdir(d)) != NULL) {
        scanned++;
        if (strncmp(ent->d_name, prefix, plen) == 0) { count++; }
        errno = 0;
    }
    /* check for readdir error (errno set on error, 0 on normal end) */
    if (errno != 0) { closedir(d); return -1; }
    /* hit scan cap without exhausting directory — refuse partial result */
    if (scanned >= MAX_DIR_SCAN) { closedir(d); return -1; }
    if (closedir(d) != 0) { return -1; }
    return count;
}

/* ── fixture helpers ─────────────────────────────────────────────────── */
static int write_file(const char *path, const char *data, size_t len)
{
    FILE *fp = __real_fopen(path, "wb");
    if (!fp) { return -1; }
    if (__real_fwrite(data, 1, len, fp) != len) { __real_fclose(fp); return -1; }
    return __real_fclose(fp);
}

static int file_byte_exact(const char *path, const char *data, size_t len)
{
    char   buf[65];
    FILE  *fp;
    size_t n;
    if (len > 64) { return 0; }
    fp = __real_fopen(path, "rb");
    if (!fp) { return 0; }
    n = __real_fread(buf, 1, sizeof(buf), fp);
    if (__real_ferror(fp)) { __real_fclose(fp); return 0; }
    if (__real_fclose(fp) != 0) { return 0; }
    return (n == len && memcmp(buf, data, len) == 0);
}

/* ── canonical rear literal (mirrored from production) ───────────────── */
static const char REAR_LITERAL[] = "AOD_INPUT_CONFIG_V1\nmode=rear\n";
#define REAR_LEN (sizeof(REAR_LITERAL) - 1U)

/* ── test body functions ─────────────────────────────────────────────── */

typedef struct {
    fault_id    fault;
    const char *name;
} save_fault_case;

static const save_fault_case SAVE_FAULTS[] = {
    { FAULT_MKSTEMP,      "mkstemp_fail"   },
    { FAULT_FDOPEN,       "fdopen_fail"    },
    { FAULT_FWRITE_ZERO,  "fwrite_zero"    },
    { FAULT_FWRITE_SHORT, "fwrite_short"   },
    { FAULT_FFLUSH,       "fflush_fail"    },
    { FAULT_FILENO,       "fileno_fail"    },
    { FAULT_FSYNC,        "fsync_fail"     },
    { FAULT_FCLOSE,       "fclose_fail"    },
    { FAULT_RENAME,       "rename_fail"    },
};
#define N_SAVE_FAULTS (sizeof(SAVE_FAULTS)/sizeof(SAVE_FAULTS[0]))

/*
 * One unowned sentinel: base.tmp.abcdef
 *   - matches the counted prefix  "<base>.tmp."  → count == 1
 *   - base.tmp would NOT match (no suffix after ".tmp.") — not created
 */
static void setup_fixtures(const char *cfg, const char *dir, const char *base,
                           char *fix, size_t fixsz)
{
    int n = snprintf(fix, fixsz, "%s/%s.tmp.abcdef", dir, base);
    if (n < 0 || (size_t)n >= fixsz) {
        fprintf(stderr, "FATAL: fixture path overflow\n"); exit(1);
    }
    if (write_file(fix, "x", 1) != 0) {
        fprintf(stderr, "FATAL: fixture creation failed\n"); exit(1);
    }
    /* seed valid REAR config via real production path (no fault) */
    clear_fault();
    int rc = aod_input_config_save(cfg, AOD_INPUT_REAR);
    if (rc != 0) { fprintf(stderr, "FATAL: baseline save failed\n"); exit(1); }
}

static void run_save_fault_cases(const char *cfg, const char *dir,
                                 const char *base, const char *fix)
{
    for (size_t i = 0; i < N_SAVE_FAULTS; i++) {
        g_fread_partial_triggered = 0;
        set_fault(SAVE_FAULTS[i].fault);

        int rc = aod_input_config_save(cfg, AOD_INPUT_BOTH);
        CHECK(rc == -1, SAVE_FAULTS[i].name);

        /* capture fired flag before clearing fault so __real helpers bypass */
        int fired = g_fault_fired;
        clear_fault();
        CHECK(fired == 1, SAVE_FAULTS[i].name);

        /* old config byte-exact REAR (fault cleared before reading) */
        CHECK(file_byte_exact(cfg, REAR_LITERAL, REAR_LEN), SAVE_FAULTS[i].name);

        /* unowned fixture unchanged */
        CHECK(file_byte_exact(fix, "x", 1), SAVE_FAULTS[i].name);

        /* production cleaned its own tmp → only our 1 sentinel remains */
        int tmps = count_cfg_tmps(dir, base);
        CHECK(tmps == 1, SAVE_FAULTS[i].name);
    }
}

typedef struct {
    fault_id       fault;
    const char    *name;
    int            expected_rc;
    aod_input_mode expected_mode;
} load_fault_case;

static const load_fault_case LOAD_FAULTS[] = {
    { FAULT_FOPEN_EACCES,  "fopen_eacces",  -1, AOD_INPUT_SHOULDERS },
    { FAULT_FOPEN_ENOENT,  "fopen_enoent",   1, AOD_INPUT_SHOULDERS },
    { FAULT_FREAD_PARTIAL, "fread_partial", -1, AOD_INPUT_SHOULDERS },
    { FAULT_FREAD_FERROR,  "fread_ferror",  -1, AOD_INPUT_SHOULDERS },
    { FAULT_FCLOSE_LOAD,   "fclose_load",   -1, AOD_INPUT_SHOULDERS },
};
#define N_LOAD_FAULTS (sizeof(LOAD_FAULTS)/sizeof(LOAD_FAULTS[0]))

static void run_load_fault_cases(const char *cfg)
{
    for (size_t i = 0; i < N_LOAD_FAULTS; i++) {
        g_fread_partial_triggered = 0;
        set_fault(LOAD_FAULTS[i].fault);

        aod_input_mode mode = AOD_INPUT_BOTH; /* sentinel != expected default */
        int rc = aod_input_config_load(cfg, &mode);

        /* capture before clearing so residual field does not confuse */
        int fired = g_fault_fired;
        clear_fault();

        CHECK(rc   == LOAD_FAULTS[i].expected_rc,   LOAD_FAULTS[i].name);
        CHECK(mode == LOAD_FAULTS[i].expected_mode,  LOAD_FAULTS[i].name);
        CHECK(fired == 1,                            LOAD_FAULTS[i].name);
    }
}

static void run_nullout_safe(const char *cfg)
{
    aod_input_mode m = AOD_INPUT_BOTH;
    int rc;
    rc = aod_input_config_load(NULL, &m);          CHECK(rc == -1, "load_null_path");
    rc = aod_input_config_load(cfg,  NULL);         CHECK(rc == -1, "load_null_out");
    rc = aod_input_config_save(NULL, AOD_INPUT_REAR); CHECK(rc == -1, "save_null_path");
    rc = aod_input_config_parse(NULL, 4, &m);       CHECK(rc == -1, "parse_null_data");
    rc = aod_input_config_parse("x",  0, &m);       CHECK(rc == -1, "parse_zero_size");
    rc = aod_input_config_parse("x",  4, NULL);     CHECK(rc == -1, "parse_null_out");
}

static void run_recovery(const char *cfg)
{
    clear_fault();
    int rc = aod_input_config_save(cfg, AOD_INPUT_BOTH);
    CHECK(rc == 0, "recovery_save");

    aod_input_mode mode = AOD_INPUT_REAR;
    rc = aod_input_config_load(cfg, &mode);
    CHECK(rc == 0,               "recovery_load_rc");
    CHECK(mode == AOD_INPUT_BOTH, "recovery_load_mode");
}

/* ── main ────────────────────────────────────────────────────────────── */
int main(int argc, char *argv[])
{
    if (argc != 2) {
        fprintf(stderr, "usage: %s <writable-tempdir>\n", argv[0]);
        return 1;
    }

    const char *dir  = argv[1];
    const char *base = "aod_input_cfg_test";
    char cfg[512], fix[512];

    int n = snprintf(cfg, sizeof(cfg), "%s/%s", dir, base);
    if (n < 0 || (size_t)n >= sizeof(cfg)) {
        fprintf(stderr, "FATAL: cfg path overflow\n"); return 1;
    }

    if (access(dir, W_OK) != 0) {
        fprintf(stderr, "FATAL: '%s' is not writable\n", dir); return 1;
    }

    setup_fixtures(cfg, dir, base, fix, sizeof(fix));

    run_save_fault_cases(cfg, dir, base, fix);
    run_load_fault_cases(cfg);
    run_nullout_safe(cfg);
    run_recovery(cfg);

    /* cleanup fixtures — failures propagate to suite result */
    if (unlink(fix) != 0) {
        fprintf(stderr, "WARN: unlink fixture '%s' failed: %s\n",
                fix, strerror(errno));
        g_failures++;
    }
    if (unlink(cfg) != 0 && errno != ENOENT) {
        fprintf(stderr, "WARN: unlink cfg '%s' failed: %s\n",
                cfg, strerror(errno));
        g_failures++;
    }

    printf("assertions=%d failures=%d\n", g_assertions, g_failures);
    return (g_failures > 0) ? 1 : 0;
}