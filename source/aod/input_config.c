/*
 * source/aod/input_config.c
 * AOD input-config persistence: exact-byte parse, bounded load, atomic save.
 *
 * Author: Sat Anthropic Claude Sonnet4.6
 *
 * DIRECTORY DURABILITY LIMITATION: this module does not fsync the containing
 * directory after a successful rename(2).  The file content is durable (file
 * fsync precedes rename), but the directory entry may not survive a power
 * loss immediately after rename on filesystems that require an explicit
 * directory fsync.  No independently proved VitaSDK directory-fsync path
 * exists; callers requiring that guarantee must perform it externally.
 * A successful save returns 0 only after rename succeeds; the old valid
 * config is preserved on any earlier failure.
 */

#include "aod/input_config.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* ── compile-time constants ───────────────────────────────────────────── */
#define PATH_MAX_LOCAL  512
#define TMP_SUFFIX      ".tmp.XXXXXX"
#define TMP_SUFFIX_LEN  (sizeof(TMP_SUFFIX) - 1U)  /* 11, no strlen */

/* one byte beyond MAX so an oversized file is detectable in one fread */
#define READ_BUF_LEN    (AOD_INPUT_CONFIG_MAX_BYTES + 1)

/* canonical byte strings — trailing NUL excluded from LEN macros */
static const char LITERAL_REAR[]      = "AOD_INPUT_CONFIG_V1\nmode=rear\n";
static const char LITERAL_SHOULDERS[] = "AOD_INPUT_CONFIG_V1\nmode=shoulders\n";
static const char LITERAL_BOTH[]      = "AOD_INPUT_CONFIG_V1\nmode=both\n";

#define LEN_REAR      (sizeof(LITERAL_REAR)      - 1U)
#define LEN_SHOULDERS (sizeof(LITERAL_SHOULDERS) - 1U)
#define LEN_BOTH      (sizeof(LITERAL_BOTH)      - 1U)

/* ── private helpers ──────────────────────────────────────────────────── */

/*
 * Bounded path-length measurement: walks at most PATH_MAX_LOCAL characters.
 * Returns 0 on success and stores the length in *len_out.
 * Returns -1 if path is NULL, empty, or has no NUL within PATH_MAX_LOCAL.
 * Never calls strlen on unvalidated input.
 */
static int bounded_path_len(const char *path, size_t *len_out)
{
    size_t i;
    if (path == NULL) { return -1; }
    for (i = 0; i < (size_t)PATH_MAX_LOCAL; i++) {
        if (path[i] == '\0') {
            if (i == 0) { return -1; }  /* empty string */
            *len_out = i;
            return 0;
        }
    }
    return -1;  /* no NUL within PATH_MAX_LOCAL: too long */
}

/*
 * Map a valid enum value to its canonical literal and byte length.
 * Returns 0 on success, -1 if mode is out of range.
 */
static int mode_to_literal(aod_input_mode  mode,
                            const char    **lit_out,
                            size_t         *len_out)
{
    if (mode == AOD_INPUT_REAR) {
        *lit_out = LITERAL_REAR;      *len_out = LEN_REAR;      return 0;
    }
    if (mode == AOD_INPUT_SHOULDERS) {
        *lit_out = LITERAL_SHOULDERS; *len_out = LEN_SHOULDERS; return 0;
    }
    if (mode == AOD_INPUT_BOTH) {
        *lit_out = LITERAL_BOTH;      *len_out = LEN_BOTH;      return 0;
    }
    return -1;
}

/*
 * Build the mkstemp template "<path>.tmp.XXXXXX" into dst[PATH_MAX_LOCAL].
 * Returns 0 on success; -1 if path is invalid or the result would overflow.
 */
static int build_tmp_template(const char *path, char *dst)
{
    size_t plen;
    if (bounded_path_len(path, &plen) != 0)             { return -1; }
    /* plen + TMP_SUFFIX_LEN + 1 (NUL) must fit in PATH_MAX_LOCAL */
    if (plen + TMP_SUFFIX_LEN >= (size_t)PATH_MAX_LOCAL) { return -1; }
    memcpy(dst, path, plen);
    memcpy(dst + plen, TMP_SUFFIX, TMP_SUFFIX_LEN);
    dst[plen + TMP_SUFFIX_LEN] = '\0';
    return 0;
}

/*
 * Write exactly lit_len bytes to fp, then fflush and fsync.
 * Does NOT fclose — caller retains ownership.  Returns 0 on success.
 * fileno() result is checked before fsync to guard against stream errors.
 */
static int write_and_sync(FILE *fp, const char *lit, size_t lit_len)
{
    int fd;
    if (fwrite(lit, 1, lit_len, fp) != lit_len) { return -1; }
    if (fflush(fp) != 0)                        { return -1; }
    fd = fileno(fp);
    if (fd < 0)                                  { return -1; }
    if (fsync(fd) != 0)                          { return -1; }
    return 0;
}

/* ── public API ───────────────────────────────────────────────────────── */

int aod_input_config_parse(const char *data, size_t size,
                           aod_input_mode *out)
{
    size_t i;

    /* Always default *out before any early return so callers get a safe value. */
    if (out != NULL) { *out = AOD_INPUT_CONFIG_DEFAULT; }

    if (out == NULL || data == NULL || size == 0 ||
            size > AOD_INPUT_CONFIG_MAX_BYTES) {
        return -1;
    }

    /* Reject embedded NUL bytes (bounded by size <= MAX_BYTES). */
    for (i = 0; i < size; i++) {
        if (data[i] == '\0') { return -1; }
    }

    /* Exact-length, exact-content match against each canonical literal. */
    if (size == LEN_REAR && memcmp(data, LITERAL_REAR, LEN_REAR) == 0) {
        *out = AOD_INPUT_REAR;      return 0;
    }
    if (size == LEN_SHOULDERS &&
            memcmp(data, LITERAL_SHOULDERS, LEN_SHOULDERS) == 0) {
        *out = AOD_INPUT_SHOULDERS; return 0;
    }
    if (size == LEN_BOTH && memcmp(data, LITERAL_BOTH, LEN_BOTH) == 0) {
        *out = AOD_INPUT_BOTH;      return 0;
    }
    return -1;
}

int aod_input_config_load(const char *path, aod_input_mode *out)
{
    char           buf[READ_BUF_LEN];
    FILE          *fp;
    size_t         n, plen;
    aod_input_mode parsed;
    int            ferr_rc, fclose_rc;

    if (out != NULL) { *out = AOD_INPUT_CONFIG_DEFAULT; }

    if (out == NULL || path == NULL) { return -1; }

    /* Bounded path validation — never strlen on unvalidated input. */
    if (bounded_path_len(path, &plen) != 0) { return -1; }
    (void)plen;  /* used only for the bounds check above */

    fp = fopen(path, "rb");
    if (fp == NULL) {
        /* ENOENT → missing file: return 1 with default already set. */
        return (errno == ENOENT) ? 1 : -1;
    }

    /* Read up to MAX+1 bytes; >MAX means oversized file. */
    n = fread(buf, 1, READ_BUF_LEN, fp);

    /*
     * Always check ferror AND always call fclose, even if ferror fired.
     * Both results are captured; either non-zero drives the return code.
     * No short-circuit: fclose must be reached on every code path.
     */
    ferr_rc   = (ferror(fp) == 0) ? 0 : -1;
    fclose_rc = (fclose(fp) == 0) ? 0 : -1;

    if (ferr_rc != 0 || fclose_rc != 0) { return -1; }
    if (n == 0 || n > AOD_INPUT_CONFIG_MAX_BYTES) { return -1; }

    parsed = AOD_INPUT_CONFIG_DEFAULT;
    if (aod_input_config_parse(buf, n, &parsed) != 0) { return -1; }
    *out = parsed;
    return 0;
}

/*
 * Write lit_len bytes to the already-opened temp file, then commit via
 * rename.  tmp[] holds the unique path returned by mkstemp (modified
 * in-place).  On any pre-commit failure, unlinks only the mkstemp-owned
 * temp and returns -1, leaving the prior config file intact.
 * On successful rename, returns 0 immediately — no directory fsync
 * (see file-level DIRECTORY DURABILITY LIMITATION comment).
 */
static int commit_tmp(FILE *fp, int fd,
                      char *tmp, const char *path,
                      const char *lit, size_t lit_len)
{
    int write_ok, unlink_rc;

    write_ok = write_and_sync(fp, lit, lit_len);
    /* fclose must run regardless; capture its result. */
    if (fclose(fp) != 0) { write_ok = -1; }

    if (write_ok != 0) {
        unlink_rc = unlink(tmp);
        (void)unlink_rc;  /* pre-commit cleanup; overall result is -1 */
        (void)fd;         /* fd consumed by fdopen; not used after fclose */
        return -1;
    }

    if (rename(tmp, path) != 0) {
        unlink_rc = unlink(tmp);
        (void)unlink_rc;  /* pre-commit cleanup; overall result is -1 */
        return -1;
    }

    /* rename succeeded — the new config is in place. */
    return 0;
}

int aod_input_config_save(const char *path, aod_input_mode mode)
{
    char        tmp[PATH_MAX_LOCAL];
    const char *lit     = NULL;
    size_t      lit_len = 0;
    size_t      plen;
    int         fd, unlink_rc, close_rc;
    FILE       *fp;

    if (path == NULL) { return -1; }

    /* Bounded path validation — never strlen on unvalidated input. */
    if (bounded_path_len(path, &plen) != 0) { return -1; }

    if (mode_to_literal(mode, &lit, &lit_len) != 0) { return -1; }

    /* Build mkstemp template "<path>.tmp.XXXXXX" — overflow checked inside. */
    if (build_tmp_template(path, tmp) != 0) { return -1; }

    /*
     * mkstemp atomically creates a unique 0600 file and returns its fd.
     * No pre-existing fixed temp can block this call; stale temps from
     * prior crashes have different unique suffixes and are never touched.
     */
    fd = mkstemp(tmp);
    if (fd < 0) { return -1; }

    fp = fdopen(fd, "wb");
    if (fp == NULL) {
        /*
         * fdopen failed; fd is not consumed.  Close and unlink our new
         * unique temp.  Return values are swallowed — overall result is -1
         * and no false-success is possible.
         */
        close_rc  = close(fd);
        unlink_rc = unlink(tmp);
        (void)close_rc;   /* pre-commit cleanup; overall result is -1 */
        (void)unlink_rc;  /* pre-commit cleanup; overall result is -1 */
        return -1;
    }

    /* fd is now owned by fp; commit_tmp drives fclose, rename, cleanup. */
    return commit_tmp(fp, fd, tmp, path, lit, lit_len);
}