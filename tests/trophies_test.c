/* MIT. Host boundary, journal and queue failure tests; no Vita dependencies. */
#include "aod/trophies.h"
#include "aod/trophy_storage.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

typedef struct { uint64_t e, d, unlocked; int store_fail, unlock_fail, calls, writes; } Fake;
static int save(void *p, uint64_t e, uint64_t d) {
    Fake *f = p; ++f->writes;
    if (f->store_fail) return -1;
    f->e = e; f->d = d; return 0;
}
static int unlock(void *p, unsigned id) {
    Fake *f = p; ++f->calls;
    assert(f->e & (UINT64_C(1) << id)); /* completion must be durable FIRST */
    if (f->unlock_fail == 1 || (f->unlock_fail == 2 && id == 0)) return -1;
    f->unlocked |= UINT64_C(1) << id; return 0;
}
int main(int argc, char **argv) {
    assert(argc == 2);
    for (unsigned i = 0; i < AOD_TROPHY_COUNT; ++i) {
        char msg[160];
        const char *k = aod_trophy_key(i);
        assert(aod_trophy_id(k, 100) == (int)i);
        assert(aod_trophy_id(k, 99.999) == -1);
        assert(aod_trophy_id(k, NAN) == -1);
        assert(aod_trophy_id(k, INFINITY) == -1);
        assert(aod_trophy_id(k, -INFINITY) == -1);
        assert(aod_trophy_id(k, 101) == -1);
        snprintf(msg, sizeof(msg), AOD_TROPHY_PREFIX "%s", k);
        assert(aod_trophy_message(msg) == (int)i);
        strcat(msg, ":100"); assert(aod_trophy_message(msg) == -1);
    }
    assert(!aod_trophy_key(52));
    assert(aod_trophy_id(NULL, 100) == -1);
    assert(aod_trophy_id("ach_invented",100) == -1);
    assert(aod_trophy_message("HOME") == -1);
    assert(aod_trophy_message(AOD_TROPHY_PREFIX "0") == -1);
    assert(aod_trophy_message(NULL) == -1);
    aod_trophy_core c = {0}; Fake f = {0};
    f.store_fail = 1;
    assert(aod_trophy_step(&c, 1, 1, save, unlock, &f) == -1);
    assert(c.earned == 1 && !c.delivered && f.calls == 0);
    f.store_fail = 0; f.unlock_fail = 1;
    assert(aod_trophy_step(&c, 0, 1, save, unlock, &f) == -2);
    assert(f.e == 1 && !c.delivered);
    f.unlock_fail = 0;
    assert(aod_trophy_step(&c, 1, 1, save, unlock, &f) == 1);
    int calls = f.calls;
    for (int i = 0; i < 1000; ++i) assert(aod_trophy_step(&c, 1, 1, save, unlock, &f) == 0);
    assert(f.calls == calls);
    /* Capacity: all 52 plus repeated saturation, invalid high bits cannot create ID52+. */
    assert(aod_trophy_step(&c, UINT64_MAX, 0, save, unlock, &f) == 0);
    assert(c.earned == AOD_TROPHY_MASK && f.calls == calls);
    for (int i = 0; i < 51; ++i) assert(aod_trophy_step(&c, UINT64_MAX, 1, save, unlock, &f) == 1);
    assert(c.delivered == AOD_TROPHY_MASK && f.unlocked == AOD_TROPHY_MASK);
    assert(aod_trophy_step(&c, UINT64_MAX, 1, save, unlock, &f) == 0);
    /* Post-unlock store failure: no extra platform call while in-memory done; retry store. */
    c = (aod_trophy_core){.earned=1,.saved_earned=1};
    f = (Fake){.e=1,.store_fail=1};
    assert(aod_trophy_step(&c, 0, 1, save, unlock, &f) == -1);
    assert(c.delivered == 1 && f.calls == 1 && f.d == 0);
    f.store_fail = 0;
    assert(aod_trophy_step(&c, 0, 1, save, unlock, &f) == 0);
    assert(f.calls == 1 && f.d == 1);
    /* A permanently failing first ID cannot starve subsequent earned trophies. */
    c = (aod_trophy_core){0}; f = (Fake){.unlock_fail=2};
    assert(aod_trophy_step(&c, 3, 1, save, unlock, &f) == -2);
    assert(aod_trophy_step(&c, 0, 1, save, unlock, &f) == 1);
    assert(c.delivered == 2 && f.unlocked == 2 && f.calls == 2);
    assert(aod_trophy_step(&c, 0, 1, save, unlock, &f) == -2);
    /* Failed storage is not retried on every 250ms game-worker tick. New events
     * remain in memory, and a final shutdown flush never calls the platform. */
    c = (aod_trophy_core){0}; f = (Fake){.store_fail=1};
    aod_trophy_schedule schedule = {0};
    assert(aod_trophy_poll(&c,&schedule,1,1,0,0,save,unlock,&f)==-1);
    assert(f.writes==1 && !f.calls && schedule.storage_retry_at==1000000);
    assert(aod_trophy_poll(&c,&schedule,2,1,0,250000,save,unlock,&f)==0);
    assert(c.earned==3 && f.writes==1);
    assert(aod_trophy_poll(&c,&schedule,4,1,0,999999,save,unlock,&f)==0);
    assert(c.earned==7 && f.writes==1);
    assert(aod_trophy_poll(&c,&schedule,0,1,0,1000000,save,unlock,&f)==-1);
    assert(f.writes==2 && schedule.storage_retry_at==3000000);
    f.store_fail=0;
    assert(aod_trophy_poll(&c,&schedule,8,1,1,1500000,save,unlock,&f)==0);
    assert(f.e==15 && !f.calls && !schedule.storage_retry_at);
    /* Missing platform still persists, and platform delay does not delay disk. */
    c=(aod_trophy_core){0}; f=(Fake){.unlock_fail=1}; schedule=(aod_trophy_schedule){0};
    assert(aod_trophy_poll(&c,&schedule,1,1,0,0,save,unlock,&f)==-2);
    assert(schedule.platform_retry_at==1000000);
    assert(aod_trophy_poll(&c,&schedule,2,1,0,250000,save,unlock,&f)==0);
    assert(f.e==3 && f.calls==1);
    f.unlock_fail=0;
    assert(aod_trophy_poll(&c,&schedule,0,1,0,1000000,save,unlock,&f)==1);
    assert(f.unlocked==2 && !schedule.platform_retry_at);
    /* Basic actual storage roundtrip; interrupted-write tests are separate. */
    char path[512]; snprintf(path,sizeof(path),"%s/journal",argv[1]);
    uint64_t e = 123, d = 456;
    assert(aod_trophy_load_file(path,&e,&d) == 1);
    assert(e == 123 && d == 456);
    assert(aod_trophy_save_file(path, AOD_TROPHY_MASK, 7) == 0);
    assert(aod_trophy_load_file(path,&e,&d) == 0 && e==AOD_TROPHY_MASK && d==7);
    assert(aod_trophy_save_file(path,0,1) == -1);
    assert(aod_trophy_save_file(path,UINT64_MAX,0) == -1);
    assert(aod_trophy_load_file(path,&e,&d) == 0 && d==7);
    assert(aod_trophy_save_file(path,4,0)==-1); /* no earned evidence regression */
    snprintf(path,sizeof(path),"%s/missing/journal",argv[1]);
    assert(aod_trophy_save_file(path,1,0)==-1);
    puts("PASS: 52 mappings, nonfinite rejection, dedup/capacity, fair retries, separate storage/platform backoff, optional fallback and journal roundtrip");
    return 0;
}
