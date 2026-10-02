/* Local homebrew achievements bridge. MIT; no game assets. */
#ifndef AOD_TROPHIES_H
#define AOD_TROPHIES_H
#include <stdint.h>
#define AOD_TROPHY_COUNT 52
#define AOD_TROPHY_MASK ((UINT64_C(1) << AOD_TROPHY_COUNT) - 1)
#define AOD_TROPHY_PREFIX "AOD_LOCAL_TROPHY_V1:"
/* Exact key + finite, exactly 100 percent; no counters/backfill. */
int aod_trophy_id(const char *key, double progress);
int aod_trophy_message(const char *message);
const char *aod_trophy_key(unsigned id);
/* Worker-owned state. Store must preserve a previous validated durable record.
 * unlock returns 0 for success/already unlocked, negative for retry. */
typedef struct {
    uint64_t earned, delivered, saved_earned, saved_delivered;
    unsigned next_id;
} aod_trophy_core;
typedef int (*aod_trophy_store)(void *, uint64_t, uint64_t);
typedef int (*aod_trophy_unlock)(void *, unsigned);
int aod_trophy_step(aod_trophy_core *, uint64_t incoming, int available,
                    aod_trophy_store, aod_trophy_unlock, void *);
typedef struct {
    uint64_t storage_retry_at, platform_retry_at;
    unsigned storage_delay, platform_delay;
} aod_trophy_schedule;
/* Worker-only clocked poll. Incoming is retained even during disk backoff.
 * stop permits one final persistence attempt, never another platform call. */
int aod_trophy_poll(aod_trophy_core *, aod_trophy_schedule *, uint64_t incoming,
                   int available, int stop, uint64_t now_us,
                   aod_trophy_store, aod_trophy_unlock, void *);
/* Runtime: init before nativeStart, stop before process exit. */
void aod_trophies_init(void);
void aod_trophies_stop(void);
/* Consumes only reserved messages; no blocking disk/trophy calls. */
int aod_trophies_receive(const char *message);
#endif
