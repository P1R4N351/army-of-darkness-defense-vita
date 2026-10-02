/* MIT. Two checksummed generation slots; never rename or remove a valid slot.
 * Vita newlib rename removes its destination before moving the source. Writing
 * only the inactive slot keeps the previous durable record through write errors.
 * Checksums detect torn records, not malicious edits. Actual power-loss behavior
 * still requires device testing. Generation order uses 64-bit serial arithmetic. */
#include "aod/trophy_storage.h"
#include "aod/trophies.h"
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>

typedef struct { uint64_t generation, earned, done; } Record;
static uint32_t checksum(const unsigned char *p, unsigned n) {
    uint32_t h = 2166136261u;
    for (unsigned i = 0; i < n; ++i) h = (h ^ p[i]) * 16777619u;
    return h;
}
static void put64(unsigned char *p, uint64_t v) {
    for (unsigned i = 0; i < 8; ++i) p[i] = (unsigned char)(v >> (8 * i));
}
static uint64_t get64(const unsigned char *p) {
    uint64_t v = 0;
    for (unsigned i = 0; i < 8; ++i) v |= (uint64_t)p[i] << (8 * i);
    return v;
}
static int slot_path(char *out, size_t size, const char *base, int slot) {
    if (!base) return -1;
    int n = snprintf(out, size, "%s.%d", base, slot);
    return n < 0 || (size_t)n >= size ? -1 : 0;
}
/* 1 missing, -1 corrupt/unreadable; outputs change only on a valid record. */
static int read_record(const char *path, int legacy, Record *record) {
    unsigned char b[40];
    unsigned size = legacy ? 32 : 40;
    FILE *f = fopen(path, "rb");
    if (!f) return errno == ENOENT ? 1 : -1;
    size_t n = fread(b, 1, size, f);
    int extra = fgetc(f), bad = ferror(f);
    if (fclose(f)) bad = 1;
    const char *magic = legacy ? "AODTRP01" : "AODTRP02";
    if (bad || n != size || extra != EOF || memcmp(b, magic, 8)) return -1;
    unsigned offset = legacy ? 8 : 16;
    Record r = {legacy ? 0 : get64(b + 8), get64(b + offset), get64(b + offset + 8)};
    if ((r.earned & ~AOD_TROPHY_MASK) || (r.done & ~r.earned) ||
        get64(b + size - 8) != checksum(b, size - 8)) return -1;
    *record = r;
    return 0;
}
/* Equal/conflicting or exactly half-range generations are ambiguous: fail closed.
 * Consecutive saves differ by one, including UINT64_MAX -> 0. */
static int newest(const Record *a, const Record *b) {
    uint64_t difference = a->generation - b->generation;
    if (!difference) return a->earned == b->earned && a->done == b->done ? 0 : -1;
    if (difference == (UINT64_C(1) << 63)) return -1;
    return difference < (UINT64_C(1) << 63) ? 0 : 1;
}
static int load_current(const char *path, Record *record, int *active) {
    Record slots[2]; char names[2][512]; int status[2];
    for (int i = 0; i < 2; ++i) {
        if (slot_path(names[i], sizeof(names[i]), path, i)) return -1;
        status[i] = read_record(names[i], 0, &slots[i]);
    }
    *active = -1;
    if (!status[0] && !status[1]) {
        *active = newest(&slots[0], &slots[1]);
        if (*active < 0) return -1;
    } else if (!status[0]) *active = 0;
    else if (!status[1]) *active = 1;
    if (*active >= 0) { *record = slots[*active]; return 0; }
    /* A failed first migration cannot destroy the untouched original v1 file. */
    int legacy = read_record(path, 1, record);
    if (!legacy) return 0;
    return legacy == 1 && status[0] == 1 && status[1] == 1 ? 1 : -1;
}
int aod_trophy_load_file(const char *path, uint64_t *earned, uint64_t *done) {
    Record record; int active;
    if (!path || !earned || !done) return -1;
    int status = load_current(path, &record, &active);
    if (!status) { *earned = record.earned; *done = record.done; }
    return status;
}
/* A fresh live session can retry its own torn first record. Startup corruption
 * never grants this permission, and no valid slot or legacy record is replaced. */
static int only_initial_torn_slots(const char *path) {
    Record record; char name[512];
    for (int i = 0; i < 2; ++i) {
        if (slot_path(name, sizeof(name), path, i) || !read_record(name, 0, &record)) return 0;
    }
    return read_record(path, 1, &record) == 1;
}
static int sync_active(const char *path, int active) {
    if (active < 0) return 0; /* untouched legacy file or brand-new journal */
    char name[512];
    if (slot_path(name, sizeof(name), path, active)) return -1;
    FILE *f = fopen(name, "rb");
    if (!f) return -1;
    int bad = fsync(fileno(f));
    if (fclose(f)) bad = -1;
    return bad ? -1 : 0;
}
int aod_trophy_save_pending_file(const char *path, uint64_t earned, uint64_t done,
                               int initially_absent) {
    if (!path || (earned & ~AOD_TROPHY_MASK) || (done & ~earned)) return -1;
    Record current = {0}; int active;
    int status = load_current(path, &current, &active);
    if (status < 0) {
        if (!initially_absent || !only_initial_torn_slots(path)) return -1;
        current = (Record){0}; active = -1;
    }
    /* Never erase previously earned evidence, even if an invalid caller asks. */
    if (!status && (current.earned & ~earned)) return -1;
    /* A previous failed fsync may have left a structurally valid newer slot.
     * Make that selected record durable before truncating its older peer. */
    if (sync_active(path, active)) return -1;
    char target[512]; unsigned char b[40];
    if (slot_path(target, sizeof(target), path, active == 0 ? 1 : 0)) return -1;
    memcpy(b, "AODTRP02", 8); put64(b + 8, current.generation + 1);
    put64(b + 16, earned); put64(b + 24, done); put64(b + 32, checksum(b, 32));
    FILE *f = fopen(target, "wb");
    if (!f) return -1;
    int bad = fwrite(b, 1, sizeof(b), f) != sizeof(b);
    if (fflush(f)) bad = 1;
    if (fsync(fileno(f))) bad = 1;
    if (fclose(f)) bad = 1;
    /* The target may be partially written. Keep the previous slot and legacy
     * file untouched; a valid newly written record is harmless on recovery. */
    Record verified;
    if (bad || read_record(target, 0, &verified)) return -1;
    return verified.generation == current.generation + 1 &&
           verified.earned == earned && verified.done == done ? 0 : -1;
}
int aod_trophy_save_file(const char *path, uint64_t earned, uint64_t done) {
    return aod_trophy_save_pending_file(path, earned, done, 0);
}
