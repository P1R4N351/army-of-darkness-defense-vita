/*
 * tests/input_config_test.c
 * Unit tests for source/aod/input_config.c
 *
 * Build (example):
 *   cc -std=c11 -Wall -Wextra -Werror \
 *      -I source \
 *      source/aod/input_config.c \
 *      tests/input_config_test.c \
 *      -o input_config_test
 *   ./input_config_test /tmp/aod_test_XXXXXX
 *
 * Caller supplies a writable temporary directory as argv[1].
 * No real game paths are used.
 */

#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#include <unistd.h>

#include "aod/input_config.h"

/* ------------------------------------------------------------------ */
/* Assertion helpers                                                   */
/* ------------------------------------------------------------------ */

static int g_total  = 0;
static int g_failed = 0;

#define CHECK(label, expr)                                              \
    do {                                                                \
        g_total++;                                                      \
        if (!(expr)) {                                                  \
            fprintf(stderr, "FAIL [%s]: %s  (%s:%d)\n",               \
                    (label), #expr, __FILE__, __LINE__);               \
            g_failed++;                                                 \
        } else {                                                        \
            printf("PASS [%s]\n", (label));                            \
        }                                                               \
    } while (0)

/* ------------------------------------------------------------------ */
/* I/O helpers — all returns checked; fixture helpers exit(2) on fail */
/* ------------------------------------------------------------------ */

/*
 * FIXTURE_UNLINK / FIXTURE_RMDIR: cleanup that must succeed.
 * Callers that intentionally allow ENOENT handle it before this macro.
 */
#define FIXTURE_UNLINK(p) \
    do { \
        if (unlink(p) != 0) { \
            fprintf(stderr, "FATAL: unlink %s: %s\n", (p), strerror(errno)); \
            exit(2); \
        } \
    } while (0)

#define FIXTURE_RMDIR(p) \
    do { \
        if (rmdir(p) != 0) { \
            fprintf(stderr, "FATAL: rmdir %s: %s\n", (p), strerror(errno)); \
            exit(2); \
        } \
    } while (0)

/* Build "dir/name" into buf of bufsz; exits with 2 on overflow. */
static void helper_join(char *buf, size_t bufsz,
                         const char *dir, const char *name)
{
    int r = snprintf(buf, bufsz, "%s/%s", dir, name);
    if (r < 0 || (size_t)r >= bufsz) {
        fprintf(stderr, "FATAL: path overflow: dir=%s name=%s\n", dir, name);
        exit(2);
    }
}

/* Write bytes to path; exits with 2 on any I/O failure. */
static void helper_write_file(const char *path,
                               const char *data, size_t len)
{
    FILE *f = fopen(path, "wb");
    if (!f) {
        fprintf(stderr, "FATAL: fopen(wb) %s: %s\n", path, strerror(errno));
        exit(2);
    }
    if (fwrite(data, 1, len, f) != len || ferror(f)) {
        fprintf(stderr, "FATAL: fwrite %s\n", path);
        (void)fclose(f);
        exit(2);
    }
    if (fclose(f) != 0) {
        fprintf(stderr, "FATAL: fclose(wb) %s: %s\n", path, strerror(errno));
        exit(2);
    }
}

/* Read up to cap bytes from path into buf; returns bytes read or -1. */
static long helper_read_file(const char *path, char *buf, size_t cap)
{
    FILE *f = fopen(path, "rb");
    if (!f) { return -1; }
    size_t n  = fread(buf, 1, cap, f);
    int    err = ferror(f);
    if (fclose(f) != 0) { return -1; }
    if (err) { return -1; }
    return (long)n;
}

/* Return 1 if path exists, 0 if ENOENT; other stat errors exit(2). */
static int helper_exists(const char *path)
{
    struct stat st;
    if (stat(path, &st) == 0) { return 1; }
    if (errno == ENOENT) { return 0; }
    fprintf(stderr, "FATAL: stat %s: %s\n", path, strerror(errno));
    exit(2);
}

/* ------------------------------------------------------------------ */
/* Exact wire formats                                                  */
/* ------------------------------------------------------------------ */

static const char CONFIG_REAR[]      = "AOD_INPUT_CONFIG_V1\nmode=rear\n";
static const char CONFIG_SHOULDERS[] = "AOD_INPUT_CONFIG_V1\nmode=shoulders\n";
static const char CONFIG_BOTH[]      = "AOD_INPUT_CONFIG_V1\nmode=both\n";

/* ------------------------------------------------------------------ */
/* Parse tests (no I/O)                                               */
/* ------------------------------------------------------------------ */

static void test_parse_modes(void)
{
    aod_input_mode out;

    out = (aod_input_mode)-99;
    CHECK("parse_rear",
          aod_input_config_parse(CONFIG_REAR,
                                  sizeof(CONFIG_REAR) - 1, &out) == 0
          && out == AOD_INPUT_REAR);

    out = (aod_input_mode)-99;
    CHECK("parse_shoulders",
          aod_input_config_parse(CONFIG_SHOULDERS,
                                  sizeof(CONFIG_SHOULDERS) - 1, &out) == 0
          && out == AOD_INPUT_SHOULDERS);

    out = (aod_input_mode)-99;
    CHECK("parse_both",
          aod_input_config_parse(CONFIG_BOTH,
                                  sizeof(CONFIG_BOTH) - 1, &out) == 0
          && out == AOD_INPUT_BOTH);
}

static void test_parse_null_out(void)
{
    /* null out pointer must return -1, not crash */
    int r = aod_input_config_parse(CONFIG_REAR,
                                    sizeof(CONFIG_REAR) - 1, NULL);
    CHECK("parse_null_out_returns_minus1", r == -1);
}

static void test_parse_invalid_inputs(void)
{
    aod_input_mode out;

    /* null data */
    out = (aod_input_mode)-99;
    int r = aod_input_config_parse(NULL, 10, &out);
    CHECK("parse_null_data_returns_minus1",  r == -1);
    CHECK("parse_null_data_default_mode",    out == AOD_INPUT_SHOULDERS);

    /* empty / size 0 */
    out = (aod_input_mode)-99;
    r = aod_input_config_parse("", 0, &out);
    CHECK("parse_empty_returns_minus1",  r == -1);
    CHECK("parse_empty_default_mode",    out == AOD_INPUT_SHOULDERS);

    /* embedded NUL mid-stream */
    const char nul_buf[] = "AOD_INPUT_CONFIG_V1\nmode=\x00rear\n";
    out = (aod_input_mode)-99;
    r = aod_input_config_parse(nul_buf, sizeof(nul_buf) - 1, &out);
    CHECK("parse_embedded_nul_returns_minus1", r == -1);
    CHECK("parse_embedded_nul_default_mode",   out == AOD_INPUT_SHOULDERS);

    /* wrong version header */
    const char badver[] = "AOD_INPUT_CONFIG_V2\nmode=rear\n";
    out = (aod_input_mode)-99;
    r = aod_input_config_parse(badver, sizeof(badver) - 1, &out);
    CHECK("parse_bad_version_returns_minus1", r == -1);
    CHECK("parse_bad_version_default_mode",   out == AOD_INPUT_SHOULDERS);

    /* unknown mode value */
    const char unkmode[] = "AOD_INPUT_CONFIG_V1\nmode=trigger\n";
    out = (aod_input_mode)-99;
    r = aod_input_config_parse(unkmode, sizeof(unkmode) - 1, &out);
    CHECK("parse_unknown_mode_returns_minus1", r == -1);
    CHECK("parse_unknown_mode_default_mode",   out == AOD_INPUT_SHOULDERS);

    /* trailing bytes after valid record */
    const char trailing[] = "AOD_INPUT_CONFIG_V1\nmode=rear\nextra\n";
    out = (aod_input_mode)-99;
    r = aod_input_config_parse(trailing, sizeof(trailing) - 1, &out);
    CHECK("parse_trailing_returns_minus1", r == -1);
    CHECK("parse_trailing_default_mode",   out == AOD_INPUT_SHOULDERS);

    /* duplicate mode key */
    const char dup[] = "AOD_INPUT_CONFIG_V1\nmode=rear\nmode=both\n";
    out = (aod_input_mode)-99;
    r = aod_input_config_parse(dup, sizeof(dup) - 1, &out);
    CHECK("parse_duplicate_returns_minus1", r == -1);
    CHECK("parse_duplicate_default_mode",   out == AOD_INPUT_SHOULDERS);

    /* oversize (> MAX_BYTES == 64) */
    char big[128];
    memset(big, 'x', sizeof(big));
    out = (aod_input_mode)-99;
    r = aod_input_config_parse(big, sizeof(big), &out);
    CHECK("parse_oversize_returns_minus1", r == -1);
    CHECK("parse_oversize_default_mode",   out == AOD_INPUT_SHOULDERS);
}

/* CRLF, truncated newline, trailing NUL */
static void test_parse_format_edge_cases(void)
{
    aod_input_mode out;
    int r;

    /* CRLF line endings must be rejected */
    const char crlf[] = "AOD_INPUT_CONFIG_V1\r\nmode=rear\r\n";
    out = (aod_input_mode)-99;
    r = aod_input_config_parse(crlf, sizeof(crlf) - 1, &out);
    CHECK("parse_crlf_returns_minus1", r == -1);
    CHECK("parse_crlf_default_mode",   out == AOD_INPUT_SHOULDERS);

    /* Truncated: header present but no trailing newline / mode line */
    const char trunc[] = "AOD_INPUT_CONFIG_V1";
    out = (aod_input_mode)-99;
    r = aod_input_config_parse(trunc, sizeof(trunc) - 1, &out);
    CHECK("parse_truncated_no_newline_returns_minus1", r == -1);
    CHECK("parse_truncated_no_newline_default_mode",   out == AOD_INPUT_SHOULDERS);

    /* Trailing NUL byte appended after an otherwise valid record */
    char nul_trail[64];
    size_t base = sizeof(CONFIG_REAR) - 1;
    if (base + 1 <= sizeof(nul_trail)) {
        memcpy(nul_trail, CONFIG_REAR, base);
        nul_trail[base] = '\0';
        out = (aod_input_mode)-99;
        r = aod_input_config_parse(nul_trail, base + 1, &out);
        CHECK("parse_trailing_nul_returns_minus1", r == -1);
        CHECK("parse_trailing_nul_default_mode",   out == AOD_INPUT_SHOULDERS);
    }
}

/* ------------------------------------------------------------------ */
/* Load tests                                                          */
/* ------------------------------------------------------------------ */

static void test_load_valid_modes(const char *dir)
{
    char path[512];
    aod_input_mode out;
    int r;

    /* --- rear --- */
    helper_join(path, sizeof(path), dir, "load_rear.cfg");
    helper_write_file(path, CONFIG_REAR, sizeof(CONFIG_REAR) - 1);
    out = (aod_input_mode)-99;
    r = aod_input_config_load(path, &out);
    CHECK("load_rear_returns_0",    r == 0);
    CHECK("load_rear_mode_correct", out == AOD_INPUT_REAR);
    FIXTURE_UNLINK(path);

    /* --- shoulders --- */
    helper_join(path, sizeof(path), dir, "load_shoulders.cfg");
    helper_write_file(path, CONFIG_SHOULDERS, sizeof(CONFIG_SHOULDERS) - 1);
    out = (aod_input_mode)-99;
    r = aod_input_config_load(path, &out);
    CHECK("load_shoulders_returns_0",    r == 0);
    CHECK("load_shoulders_mode_correct", out == AOD_INPUT_SHOULDERS);
    FIXTURE_UNLINK(path);

    /* --- both --- */
    helper_join(path, sizeof(path), dir, "load_both.cfg");
    helper_write_file(path, CONFIG_BOTH, sizeof(CONFIG_BOTH) - 1);
    out = (aod_input_mode)-99;
    r = aod_input_config_load(path, &out);
    CHECK("load_both_returns_0",    r == 0);
    CHECK("load_both_mode_correct", out == AOD_INPUT_BOTH);
    FIXTURE_UNLINK(path);
}

static void test_load_missing_file(const char *dir)
{
    char path[512];
    helper_join(path, sizeof(path), dir, "nonexistent_file.cfg");
    /* Guarantee it does not exist; ENOENT is acceptable here */
    if (unlink(path) != 0 && errno != ENOENT) {
        fprintf(stderr, "FATAL: unlink %s: %s\n", path, strerror(errno));
        exit(2);
    }

    aod_input_mode out = (aod_input_mode)-99;
    int r = aod_input_config_load(path, &out);
    CHECK("load_missing_returns_1",        r == 1);
    CHECK("load_missing_default_mode",     out == AOD_INPUT_SHOULDERS);
}

static void test_load_corrupt_file(const char *dir)
{
    char path[512];
    helper_join(path, sizeof(path), dir, "load_corrupt.cfg");

    const char corrupt[] = "NOT_A_VALID_HEADER\ngarbage\n";
    helper_write_file(path, corrupt, sizeof(corrupt) - 1);

    aod_input_mode out = (aod_input_mode)-99;
    int r = aod_input_config_load(path, &out);
    CHECK("load_corrupt_returns_minus1",   r == -1);
    CHECK("load_corrupt_default_mode",     out == AOD_INPUT_SHOULDERS);
    FIXTURE_UNLINK(path);
}

static void test_load_oversize_file(const char *dir)
{
    char path[512];
    helper_join(path, sizeof(path), dir, "load_oversize.cfg");

    /* 128 bytes of junk, well above MAX_BYTES=64 */
    char big[128];
    memset(big, 'a', sizeof(big));
    helper_write_file(path, big, sizeof(big));

    aod_input_mode out = (aod_input_mode)-99;
    int r = aod_input_config_load(path, &out);
    CHECK("load_oversize_returns_minus1",  r == -1);
    CHECK("load_oversize_default_mode",    out == AOD_INPUT_SHOULDERS);
    FIXTURE_UNLINK(path);
}

/* load with null out pointer must return -1 */
static void test_load_null_out(const char *dir)
{
    char path[512];
    helper_join(path, sizeof(path), dir, "load_null_out.cfg");
    helper_write_file(path, CONFIG_REAR, sizeof(CONFIG_REAR) - 1);

    int r = aod_input_config_load(path, NULL);
    CHECK("load_null_out_returns_minus1", r == -1);

    FIXTURE_UNLINK(path);
}

/* Opening a directory as a file: helper_read_file must return -1 */
static void test_read_file_is_directory(const char *dir)
{
    char buf[64];
    long n = helper_read_file(dir, buf, sizeof(buf));
    CHECK("read_file_directory_returns_minus1", n == -1);
}

static void test_load_null_path(void)
{
    aod_input_mode out = (aod_input_mode)-99;
    int r = aod_input_config_load(NULL, &out);
    CHECK("load_null_path_returns_minus1", r == -1);
    CHECK("load_null_path_default_mode",   out == AOD_INPUT_SHOULDERS);
}

static void test_load_empty_path(void)
{
    aod_input_mode out = (aod_input_mode)-99;
    int r = aod_input_config_load("", &out);
    CHECK("load_empty_path_returns_minus1", r == -1);
    CHECK("load_empty_path_default_mode",   out == AOD_INPUT_SHOULDERS);
}

static void test_load_overlong_path(void)
{
    /* 600-character path, exceeds bounded limit of 512 */
    char long_path[600];
    memset(long_path, 'p', sizeof(long_path) - 1);
    long_path[sizeof(long_path) - 1] = '\0';

    aod_input_mode out = (aod_input_mode)-99;
    int r = aod_input_config_load(long_path, &out);
    CHECK("load_overlong_path_returns_minus1", r == -1);
    CHECK("load_overlong_path_default_mode",   out == AOD_INPUT_SHOULDERS);
}

/* ------------------------------------------------------------------ */
/* Save tests                                                          */
/* ------------------------------------------------------------------ */

static void test_save_roundtrip(const char *dir)
{
    char path[512];
    char buf[128];
    long n;
    int r;

    /* rear */
    helper_join(path, sizeof(path), dir, "save_rear.cfg");
    r = aod_input_config_save(path, AOD_INPUT_REAR);
    CHECK("save_rear_returns_0", r == 0);
    n = helper_read_file(path, buf, sizeof(buf));
    CHECK("save_rear_bytes_match",
          n == (long)(sizeof(CONFIG_REAR) - 1)
          && memcmp(buf, CONFIG_REAR, (size_t)n) == 0);
    FIXTURE_UNLINK(path);

    /* shoulders */
    helper_join(path, sizeof(path), dir, "save_shoulders.cfg");
    r = aod_input_config_save(path, AOD_INPUT_SHOULDERS);
    CHECK("save_shoulders_returns_0", r == 0);
    n = helper_read_file(path, buf, sizeof(buf));
    CHECK("save_shoulders_bytes_match",
          n == (long)(sizeof(CONFIG_SHOULDERS) - 1)
          && memcmp(buf, CONFIG_SHOULDERS, (size_t)n) == 0);
    FIXTURE_UNLINK(path);

    /* both */
    helper_join(path, sizeof(path), dir, "save_both.cfg");
    r = aod_input_config_save(path, AOD_INPUT_BOTH);
    CHECK("save_both_returns_0", r == 0);
    n = helper_read_file(path, buf, sizeof(buf));
    CHECK("save_both_bytes_match",
          n == (long)(sizeof(CONFIG_BOTH) - 1)
          && memcmp(buf, CONFIG_BOTH, (size_t)n) == 0);
    FIXTURE_UNLINK(path);
}

static void test_save_invalid_enum_preserves_old(const char *dir)
{
    char path[512];
    helper_join(path, sizeof(path), dir, "save_inv_enum.cfg");

    const char sentinel[] = "ORIGINAL_CONTENT";
    helper_write_file(path, sentinel, sizeof(sentinel) - 1);

    /* Cast an invalid enum value */
    int r = aod_input_config_save(path, (aod_input_mode)99);
    CHECK("save_invalid_enum_returns_minus1", r == -1);

    char buf[64];
    long n = helper_read_file(path, buf, sizeof(buf));
    CHECK("save_invalid_enum_preserves_old_file",
          n == (long)(sizeof(sentinel) - 1)
          && memcmp(buf, sentinel, (size_t)n) == 0);
    FIXTURE_UNLINK(path);
}

static void test_save_null_path(void)
{
    int r = aod_input_config_save(NULL, AOD_INPUT_REAR);
    CHECK("save_null_path_returns_minus1", r == -1);
}

static void test_save_empty_path(void)
{
    int r = aod_input_config_save("", AOD_INPUT_REAR);
    CHECK("save_empty_path_returns_minus1", r == -1);
}

static void test_save_overlong_path(void)
{
    char long_path[600];
    memset(long_path, 'q', sizeof(long_path) - 1);
    long_path[sizeof(long_path) - 1] = '\0';

    int r = aod_input_config_save(long_path, AOD_INPUT_REAR);
    CHECK("save_overlong_path_returns_minus1", r == -1);
}

static void test_save_missing_parent_preserves_nothing(const char *dir)
{
    /* Use a nested path whose parent directory does not exist */
    char path[512];
    helper_join(path, sizeof(path), dir, "no_such_subdir/cfg.cfg");

    int r = aod_input_config_save(path, AOD_INPUT_REAR);
    CHECK("save_missing_parent_returns_minus1", r == -1);
    /* No file should have been left behind */
    CHECK("save_missing_parent_no_file_created", !helper_exists(path));
}

/* ------------------------------------------------------------------ */
/* Stale-temp tests                                                    */
/* ------------------------------------------------------------------ */

static void test_save_stale_temps_do_not_block(const char *dir)
{
    char cfg_path[512];
    char fixed_tmp[512];   /* <cfg>.tmp  */
    char unique_tmp[512];  /* <cfg>.tmp.abcdef */

    helper_join(cfg_path,   sizeof(cfg_path),   dir, "stale.cfg");
    helper_join(fixed_tmp,  sizeof(fixed_tmp),  dir, "stale.cfg.tmp");
    helper_join(unique_tmp, sizeof(unique_tmp), dir, "stale.cfg.tmp.abcdef");

    /* Write known sentinel bytes into each stale temp */
    const char sentinel_fixed[]  = "SENTINEL_FIXED";
    const char sentinel_unique[] = "SENTINEL_UNIQUE";

    helper_write_file(fixed_tmp,  sentinel_fixed,  sizeof(sentinel_fixed)  - 1);
    helper_write_file(unique_tmp, sentinel_unique, sizeof(sentinel_unique) - 1);

    /* Save should succeed despite both stale files existing */
    int r = aod_input_config_save(cfg_path, AOD_INPUT_BOTH);
    CHECK("stale_temps_save_returns_0", r == 0);

    /* Verify config was written correctly */
    char buf[128];
    long n = helper_read_file(cfg_path, buf, sizeof(buf));
    CHECK("stale_temps_config_both_correct",
          n == (long)(sizeof(CONFIG_BOTH) - 1)
          && memcmp(buf, CONFIG_BOTH, (size_t)n) == 0);

    /* Verify fixed sentinel is unchanged */
    char chk[64];
    long cn = helper_read_file(fixed_tmp, chk, sizeof(chk));
    CHECK("stale_fixed_tmp_bytes_unchanged",
          cn == (long)(sizeof(sentinel_fixed) - 1)
          && memcmp(chk, sentinel_fixed, (size_t)cn) == 0);

    /* Verify unique sentinel is unchanged */
    cn = helper_read_file(unique_tmp, chk, sizeof(chk));
    CHECK("stale_unique_tmp_bytes_unchanged",
          cn == (long)(sizeof(sentinel_unique) - 1)
          && memcmp(chk, sentinel_unique, (size_t)cn) == 0);

    /* Clean up all fixtures; all must have been created */
    FIXTURE_UNLINK(cfg_path);
    FIXTURE_UNLINK(fixed_tmp);
    FIXTURE_UNLINK(unique_tmp);
}

/* ------------------------------------------------------------------ */
/* Generic relative path smoke tests                                   */
/* ------------------------------------------------------------------ */

static void test_load_save_plain_relative(const char *dir)
{
    /* Use a genuine relative path: chdir into dir, operate on "plain.cfg". */
    char saved_cwd[1024];
    if (getcwd(saved_cwd, sizeof(saved_cwd)) == NULL) {
        fprintf(stderr, "FATAL: getcwd: %s\n", strerror(errno));
        exit(2);
    }
    if (chdir(dir) != 0) {
        fprintf(stderr, "FATAL: chdir %s: %s\n", dir, strerror(errno));
        exit(2);
    }

    int r = aod_input_config_save("plain.cfg", AOD_INPUT_SHOULDERS);
    CHECK("plain_relative_save_returns_0", r == 0);

    aod_input_mode out = (aod_input_mode)-99;
    r = aod_input_config_load("plain.cfg", &out);
    CHECK("plain_relative_load_returns_0",      r == 0);
    CHECK("plain_relative_load_mode_shoulders", out == AOD_INPUT_SHOULDERS);

    if (unlink("plain.cfg") != 0) {
        fprintf(stderr, "FATAL: unlink plain.cfg: %s\n", strerror(errno));
        if (chdir(saved_cwd) != 0) {
            fprintf(stderr, "FATAL: chdir restore %s: %s\n",
                    saved_cwd, strerror(errno));
        }
        exit(2);
    }

    if (chdir(saved_cwd) != 0) {
        fprintf(stderr, "FATAL: chdir restore: %s\n", strerror(errno));
        exit(2);
    }
}

static void test_load_save_nested_relative(const char *dir)
{
    /* Create a sub-directory under dir, chdir into it, operate on "nested.cfg". */
    char subdir[512];
    helper_join(subdir, sizeof(subdir), dir, "nested");

    if (mkdir(subdir, 0755) != 0 && errno != EEXIST) {
        fprintf(stderr, "FATAL: mkdir %s: %s\n", subdir, strerror(errno));
        exit(2);
    }
    /* Verify it is actually a directory */
    {
        struct stat st;
        if (stat(subdir, &st) != 0 || !S_ISDIR(st.st_mode)) {
            fprintf(stderr, "FATAL: %s is not a directory\n", subdir);
            exit(2);
        }
    }

    char saved_cwd[1024];
    if (getcwd(saved_cwd, sizeof(saved_cwd)) == NULL) {
        fprintf(stderr, "FATAL: getcwd: %s\n", strerror(errno));
        exit(2);
    }
    if (chdir(subdir) != 0) {
        fprintf(stderr, "FATAL: chdir %s: %s\n", subdir, strerror(errno));
        exit(2);
    }

    int r = aod_input_config_save("nested.cfg", AOD_INPUT_REAR);
    CHECK("nested_relative_save_returns_0", r == 0);

    aod_input_mode out = (aod_input_mode)-99;
    r = aod_input_config_load("nested.cfg", &out);
    CHECK("nested_relative_load_returns_0",   r == 0);
    CHECK("nested_relative_load_mode_rear",   out == AOD_INPUT_REAR);

    if (unlink("nested.cfg") != 0) {
        fprintf(stderr, "FATAL: unlink nested.cfg: %s\n", strerror(errno));
        if (chdir(saved_cwd) != 0) {
            fprintf(stderr, "FATAL: chdir restore %s: %s\n",
                    saved_cwd, strerror(errno));
        }
        exit(2);
    }

    if (chdir(saved_cwd) != 0) {
        fprintf(stderr, "FATAL: chdir restore: %s\n", strerror(errno));
        exit(2);
    }

    FIXTURE_RMDIR(subdir);
}

/* ------------------------------------------------------------------ */
/* main                                                                */
/* ------------------------------------------------------------------ */

int main(int argc, char *argv[])
{
    if (argc < 2) {
        fprintf(stderr, "usage: %s <writable-tmpdir>\n", argv[0]);
        return 2;
    }
    const char *tmp_dir = argv[1];

    {
        struct stat st;
        if (stat(tmp_dir, &st) != 0) {
            fprintf(stderr, "error: stat %s: %s\n", tmp_dir, strerror(errno));
            return 2;
        }
        if (!S_ISDIR(st.st_mode)) {
            fprintf(stderr, "error: %s is not a directory\n", tmp_dir);
            return 2;
        }
    }
    if (access(tmp_dir, W_OK) != 0) {
        fprintf(stderr, "error: %s is not writable: %s\n",
                tmp_dir, strerror(errno));
        return 2;
    }

    printf("=== aod_input_config unit tests (tmpdir=%s) ===\n", tmp_dir);

    test_parse_modes();
    test_parse_null_out();
    test_parse_invalid_inputs();
    test_parse_format_edge_cases();

    test_load_valid_modes(tmp_dir);
    test_load_missing_file(tmp_dir);
    test_load_corrupt_file(tmp_dir);
    test_load_oversize_file(tmp_dir);
    test_load_null_out(tmp_dir);
    test_load_null_path();
    test_load_empty_path();
    test_load_overlong_path();
    test_read_file_is_directory(tmp_dir);

    test_save_roundtrip(tmp_dir);
    test_save_invalid_enum_preserves_old(tmp_dir);
    test_save_null_path();
    test_save_empty_path();
    test_save_overlong_path();
    test_save_missing_parent_preserves_nothing(tmp_dir);

    test_save_stale_temps_do_not_block(tmp_dir);

    test_load_save_plain_relative(tmp_dir);
    test_load_save_nested_relative(tmp_dir);

    printf("\n=== Results: %d/%d passed ===\n",
           g_total - g_failed, g_total);

    return (g_failed > 0) ? 1 : 0;
}