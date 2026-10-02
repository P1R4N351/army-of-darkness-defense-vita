/* MIT. Portable, bounded completion set: one slot per original achievement. */
#include "aod/trophies.h"
#include <string.h>
static const char *const keys[AOD_TROPHY_COUNT] = {
#include "trophy_keys.inc"
};
const char *aod_trophy_key(unsigned id) { return id < AOD_TROPHY_COUNT ? keys[id] : 0; }
int aod_trophy_id(const char *key, double progress) {
    /* Equality rejects NaN/Inf without isfinite assumptions under -ffast-math.
     * This file is additionally compiled with -fno-fast-math. */
    if (!key || progress != 100.0) return -1;
    for (unsigned i = 0; i < AOD_TROPHY_COUNT; ++i)
        if (!strcmp(key, keys[i])) return (int)i;
    return -1;
}
int aod_trophy_message(const char *m) {
    const size_t n = sizeof(AOD_TROPHY_PREFIX) - 1;
    if (!m || strncmp(m, AOD_TROPHY_PREFIX, n)) return -1;
    /* Protocol contains no percentage: the Lua dispatch sends ONLY exact 100.
     * Reject suffixes, numeric IDs, partials and unknown keys. */
    return aod_trophy_id(m + n, 100.0);
}
static int persist(aod_trophy_core *c, aod_trophy_store store, void *ctx) {
    if (c->earned == c->saved_earned && c->delivered == c->saved_delivered) return 0;
    if (store(ctx, c->earned, c->delivered) < 0) return -1;
    c->saved_earned = c->earned;
    c->saved_delivered = c->delivered;
    return 0;
}
int aod_trophy_step(aod_trophy_core *c, uint64_t incoming, int available,
                    aod_trophy_store store, aod_trophy_unlock unlock, void *ctx) {
    c->earned |= incoming & AOD_TROPHY_MASK;
    /* Never call the platform before earned evidence is durable. */
    if (persist(c, store, ctx) < 0) return -1;
    if (!available) return 0;
    uint64_t pending = c->earned & ~c->delivered;
    for (unsigned offset = 0; offset < AOD_TROPHY_COUNT; ++offset) {
        unsigned i = (c->next_id + offset) % AOD_TROPHY_COUNT;
        uint64_t bit = UINT64_C(1) << i;
        if (!(pending & bit)) continue;
        c->next_id = (i + 1) % AOD_TROPHY_COUNT;
        if (unlock(ctx, i) < 0) return -2;
        c->delivered |= bit;
        return persist(c, store, ctx) < 0 ? -1 : 1;
    }
    return 0;
}
static void defer(uint64_t *retry_at, unsigned *delay, uint64_t now) {
    if (!*delay) *delay = 1;
    *retry_at = now + (uint64_t)*delay * 1000000;
    *delay = *delay < 30 ? *delay * 2 : 60;
}
int aod_trophy_poll(aod_trophy_core *core, aod_trophy_schedule *schedule,
                   uint64_t incoming, int available, int stop, uint64_t now,
                   aod_trophy_store store, aod_trophy_unlock unlock, void *ctx) {
    core->earned |= incoming & AOD_TROPHY_MASK;
    if (!stop && now < schedule->storage_retry_at) return 0;
    int rc = aod_trophy_step(core, 0, available && !stop &&
                            now >= schedule->platform_retry_at, store, unlock, ctx);
    if (rc == -1) {
        defer(&schedule->storage_retry_at, &schedule->storage_delay, now);
    } else {
        schedule->storage_retry_at = 0;
        schedule->storage_delay = 1;
    }
    if (rc == -2) defer(&schedule->platform_retry_at, &schedule->platform_delay, now);
    else if (rc > 0) {
        schedule->platform_retry_at = 0;
        schedule->platform_delay = 1;
    }
    return rc;
}
