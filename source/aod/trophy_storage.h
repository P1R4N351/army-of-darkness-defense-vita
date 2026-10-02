#ifndef AOD_TROPHY_STORAGE_H
#define AOD_TROPHY_STORAGE_H
#include <stdint.h>
/* 0 loaded, 1 absent, -1 IO/corruption; corrupt records never become awards. */
int aod_trophy_load_file(const char *, uint64_t *, uint64_t *);
int aod_trophy_save_file(const char *, uint64_t, uint64_t);
/* initially_absent is allowed ONLY after load returned 1 in this live worker.
 * Clear it after the first successful save; never use it for startup corruption. */
int aod_trophy_save_pending_file(const char *, uint64_t, uint64_t, int initially_absent);
#endif
