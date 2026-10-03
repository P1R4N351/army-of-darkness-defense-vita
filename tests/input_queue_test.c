/*
 * input_queue_test.c — C99 standalone assert-based tests for aod_input_queue
 * and aod_touch_map.  Compile with actual source files; never replicates impl.
 *
 * Compile example (from src/):
 *   cc -std=c99 -fno-fast-math -I source \
 *      source/aod/input_queue.c source/aod/touch_ids.c \
 *      tests/input_queue_test.c -lm -o run_tests && ./run_tests
 */

#include "aod/input_queue.h"
#include "aod/touch_ids.h"
#include <assert.h>
#include <stdint.h>
#include <math.h>
#include <stdio.h>

/* ---- helpers ------------------------------------------------------------ */

static aod_input_queue g_q;

static void testreset(void)
{
    aod_input_queue_init(&g_q);
}

/* Push a KEY event; returns push result. */
static bool push_key(int type, int id, int64_t t)
{
    aod_input_event ev;
    ev.kind = AOD_KIND_KEY;
    ev.type = type;
    ev.id   = id;
    ev.x    = 0.0f;
    ev.y    = 0.0f;
    ev.t    = t;
    return aod_input_queue_push(&g_q, &ev);
}

/* Push a WINDOW event; returns push result. */
static bool push_window(int type, int64_t t)
{
    aod_input_event ev;
    ev.kind = AOD_KIND_WINDOW;
    ev.type = type;
    ev.id   = 0;
    ev.x    = 0.0f;
    ev.y    = 0.0f;
    ev.t    = t;
    return aod_input_queue_push(&g_q, &ev);
}

/* ---- case 1: basic DOWN/MOVE/UP sequence, clear preserves map ----------- */

static void test_basic_touch_sequence(void)
{
    testreset();

    /* DOWN vita0 -> Android pointer id 0 */
    bool ok = aod_input_queue_touch(&g_q, 0, AOD_TOUCH_DOWN, 10.0f, 20.0f, 100LL);
    assert(ok);
    assert(aod_input_queue_count(&g_q) == 1U);

    const aod_input_event *ev = aod_input_queue_at(&g_q, 0);
    assert(ev != NULL);
    assert(ev->kind == AOD_KIND_TOUCH);
    assert(ev->type == AOD_TOUCH_DOWN);   /* 0 */
    assert(ev->id   == 0);
    assert(ev->x    == 10.0f);
    assert(ev->y    == 20.0f);
    assert(ev->t    == 100LL);

    /* MOVE on same vita0 */
    ok = aod_input_queue_touch(&g_q, 0, AOD_TOUCH_MOVE, 11.0f, 21.0f, 200LL);
    assert(ok);
    assert(aod_input_queue_count(&g_q) == 2U);

    ev = aod_input_queue_at(&g_q, 1);
    assert(ev != NULL);
    assert(ev->type == AOD_TOUCH_MOVE);   /* 1 */
    assert(ev->id   == 0);
    assert(ev->x    == 11.0f);
    assert(ev->t    == 200LL);

    /* UP vita0 */
    ok = aod_input_queue_touch(&g_q, 0, AOD_TOUCH_UP, 11.0f, 21.0f, 300LL);
    assert(ok);
    assert(aod_input_queue_count(&g_q) == 3U);

    ev = aod_input_queue_at(&g_q, 2);
    assert(ev != NULL);
    assert(ev->type == AOD_TOUCH_UP);     /* 2 */
    assert(ev->id   == 0);

    /* Event ordering and exact numeric action values */
    assert(AOD_TOUCH_DOWN == 0);
    assert(AOD_TOUCH_MOVE == 1);
    assert(AOD_TOUCH_UP   == 2);

    /* Out-of-range index returns NULL */
    assert(aod_input_queue_at(&g_q, 3) == NULL);
    assert(aod_input_queue_at(&g_q, 999) == NULL);

    /* clear preserves map ownership: vita0 slot was freed by UP above;
     * a new DOWN for vita0 should still get slot 0. */
    aod_input_queue_clear(&g_q);
    assert(aod_input_queue_count(&g_q) == 0U);

    /* map was NOT reset by clear; but slot 0 was freed by UP, so DOWN reuses it */
    ok = aod_input_queue_touch(&g_q, 0, AOD_TOUCH_DOWN, 5.0f, 5.0f, 400LL);
    assert(ok);
    ev = aod_input_queue_at(&g_q, 0);
    assert(ev != NULL);
    assert(ev->id == 0);

    /* init resets the map: a second vita id (vita1) DOWN after init gives slot 0 */
    testreset();
    ok = aod_input_queue_touch(&g_q, 1, AOD_TOUCH_DOWN, 1.0f, 1.0f, 0LL);
    assert(ok);
    ev = aod_input_queue_at(&g_q, 0);
    assert(ev != NULL);
    assert(ev->id == 0);   /* fresh map -> first free slot is 0 */
}

/* ---- case 2: full queue rejects DOWN without allocating map id ---------- */

static void test_full_queue_rejects_down(void)
{
    testreset();

    /* Fill the queue with KEY events (256 slots). */
    unsigned i;
    for (i = 0U; i < AOD_QUEUE_CAPACITY; i++) {
        bool ok = push_key(0, (int)i, (int64_t)i);
        assert(ok);
    }
    assert(aod_input_queue_count(&g_q) == AOD_QUEUE_CAPACITY);

    /* Full queue: DOWN must be rejected and MUST NOT allocate a map slot. */
    bool ok = aod_input_queue_touch(&g_q, 5, AOD_TOUCH_DOWN, 1.0f, 1.0f, 0LL);
    assert(!ok);
    assert(aod_input_queue_count(&g_q) == AOD_QUEUE_CAPACITY);

    /* After clear, a DOWN for vita5 should receive free index 0 (map untouched
     * by rejected push means slot was never claimed). */
    aod_input_queue_clear(&g_q);
    assert(aod_input_queue_count(&g_q) == 0U);

    ok = aod_input_queue_touch(&g_q, 5, AOD_TOUCH_DOWN, 2.0f, 3.0f, 1LL);
    assert(ok);
    const aod_input_event *ev = aod_input_queue_at(&g_q, 0);
    assert(ev != NULL);
    assert(ev->id == 0);   /* expected free index 0 */
}

/* ---- case 3: map owner persists across clear; slot reuse after UP ------- */

static void test_map_owner_persists(void)
{
    testreset();

    /* DOWN vita2 -> slot 0. */
    bool ok = aod_input_queue_touch(&g_q, 2, AOD_TOUCH_DOWN, 1.0f, 1.0f, 0LL);
    assert(ok);
    int owned_id;
    {
        const aod_input_event *ev = aod_input_queue_at(&g_q, 0);
        assert(ev != NULL);
        owned_id = ev->id;
        assert(owned_id == 0);
    }

    /* Fill remaining queue with KEY events. */
    unsigned i;
    for (i = 1U; i < AOD_QUEUE_CAPACITY; i++) {
        ok = push_key(0, 0, (int64_t)i);
        assert(ok);
    }

    /* MOVE and UP rejected (queue full), but ownership still held. */
    ok = aod_input_queue_touch(&g_q, 2, AOD_TOUCH_MOVE, 2.0f, 2.0f, 10LL);
    assert(!ok);
    ok = aod_input_queue_touch(&g_q, 2, AOD_TOUCH_UP, 2.0f, 2.0f, 20LL);
    assert(!ok);

    /* Clear — map ownership preserved (vita2 still owns slot 0). */
    aod_input_queue_clear(&g_q);

    /* MOVE for vita2 succeeds now (owner still mapped). */
    ok = aod_input_queue_touch(&g_q, 2, AOD_TOUCH_MOVE, 3.0f, 3.0f, 30LL);
    assert(ok);
    {
        const aod_input_event *ev = aod_input_queue_at(&g_q, 0);
        assert(ev != NULL);
        assert(ev->id   == owned_id);
        assert(ev->type == AOD_TOUCH_MOVE);
    }

    /* UP for vita2 frees the slot. */
    ok = aod_input_queue_touch(&g_q, 2, AOD_TOUCH_UP, 3.0f, 3.0f, 40LL);
    assert(ok);
    {
        const aod_input_event *ev = aod_input_queue_at(&g_q, 1);
        assert(ev != NULL);
        assert(ev->type == AOD_TOUCH_UP);
    }

    /* A different vita id (vita7) can now reuse slot 0. */
    aod_input_queue_clear(&g_q);
    ok = aod_input_queue_touch(&g_q, 7, AOD_TOUCH_DOWN, 4.0f, 4.0f, 50LL);
    assert(ok);
    {
        const aod_input_event *ev = aod_input_queue_at(&g_q, 0);
        assert(ev != NULL);
        assert(ev->id == 0);   /* reused slot */
    }
}

/* ---- case 4: map capacity 10, reserved IDs 512..514 -------------------- */

static void test_map_capacity(void)
{
    testreset();

    /* Occupy all 10 slots with vita IDs 0..9. */
    int slot[AOD_MAX_POINTERS];
    int i;
    for (i = 0; i < AOD_MAX_POINTERS; i++) {
        bool ok = aod_input_queue_touch(&g_q, i, AOD_TOUCH_DOWN,
                                        (float)i, 0.0f, (int64_t)i);
        assert(ok);
        const aod_input_event *ev = aod_input_queue_at(&g_q, (unsigned)i);
        assert(ev != NULL);
        slot[i] = ev->id;
    }

    /* 11th DOWN must be rejected (map full). */
    bool ok = aod_input_queue_touch(&g_q, 10, AOD_TOUCH_DOWN, 0.0f, 0.0f, 0LL);
    assert(!ok);
    assert(aod_input_queue_count(&g_q) == (unsigned)AOD_MAX_POINTERS);

    /* clear does NOT free contacts; MOVE still works for vita0. */
    aod_input_queue_clear(&g_q);
    ok = aod_input_queue_touch(&g_q, 0, AOD_TOUCH_MOVE, 1.0f, 1.0f, 0LL);
    assert(ok);
    {
        const aod_input_event *ev = aod_input_queue_at(&g_q, 0);
        assert(ev != NULL);
        assert(ev->id == slot[0]);
    }

    /* Release vita0 UP -> slot 0 now free. */
    aod_input_queue_clear(&g_q);
    ok = aod_input_queue_touch(&g_q, 0, AOD_TOUCH_UP, 1.0f, 1.0f, 0LL);
    assert(ok);

    /* vita0 slot freed; now vita10 DOWN should get a slot. */
    ok = aod_input_queue_touch(&g_q, 10, AOD_TOUCH_DOWN, 0.0f, 0.0f, 0LL);
    assert(ok);

    /* Reserved IDs 512, 513, 514 allowed and collision-free vs front IDs. */
    testreset();
    ok = aod_input_queue_touch(&g_q, 0,   AOD_TOUCH_DOWN, 0.f, 0.f, 0LL);
    assert(ok);
    ok = aod_input_queue_touch(&g_q, 512, AOD_TOUCH_DOWN, 1.f, 0.f, 1LL);
    assert(ok);
    ok = aod_input_queue_touch(&g_q, 513, AOD_TOUCH_DOWN, 2.f, 0.f, 2LL);
    assert(ok);
    ok = aod_input_queue_touch(&g_q, 514, AOD_TOUCH_DOWN, 3.f, 0.f, 3LL);
    assert(ok);

    /* All four must have distinct Android pointer IDs. */
    int ids[4];
    for (i = 0; i < 4; i++) {
        const aod_input_event *ev = aod_input_queue_at(&g_q, (unsigned)i);
        assert(ev != NULL);
        ids[i] = ev->id;
    }
    int a, b;
    for (a = 0; a < 4; a++) {
        for (b = a + 1; b < 4; b++) {
            assert(ids[a] != ids[b]);
        }
    }
}

/* ---- case 5: NULL / out-of-range / invalid action / NaN/Inf ------------ */

static void test_invalid_inputs(void)
{
    testreset();

    /* NULL queue */
    assert(aod_input_queue_count(NULL) == 0U);
    assert(aod_input_queue_at(NULL, 0) == NULL);
    assert(!aod_input_queue_touch(NULL, 0, AOD_TOUCH_DOWN, 0.f, 0.f, 0LL));

    aod_input_event dummy;
    dummy.kind = AOD_KIND_KEY;
    dummy.type = 0; dummy.id = 0;
    dummy.x = 0.f; dummy.y = 0.f; dummy.t = 0LL;
    assert(!aod_input_queue_push(NULL, &dummy));
    assert(!aod_input_queue_push(&g_q, NULL));

    /* Out-of-range vita_id: negative, 256, 511, 515 */
    assert(!aod_input_queue_touch(&g_q, -1,  AOD_TOUCH_DOWN, 0.f, 0.f, 0LL));
    assert(!aod_input_queue_touch(&g_q, 256, AOD_TOUCH_DOWN, 0.f, 0.f, 0LL));
    assert(!aod_input_queue_touch(&g_q, 511, AOD_TOUCH_DOWN, 0.f, 0.f, 0LL));
    assert(!aod_input_queue_touch(&g_q, 515, AOD_TOUCH_DOWN, 0.f, 0.f, 0LL));
    assert(aod_input_queue_count(&g_q) == 0U);

    /* Invalid action */
    assert(!aod_input_queue_touch(&g_q, 0, -1, 0.f, 0.f, 0LL));
    assert(!aod_input_queue_touch(&g_q, 0,  3, 0.f, 0.f, 0LL));
    assert(aod_input_queue_count(&g_q) == 0U);

    /* NaN and Inf produce false; compiled with -fno-fast-math so isfinite works */
    float nan_val = (float)(0.0 / 0.0);
    float inf_val = (float)(1.0 / 0.0);
    assert(!aod_input_queue_touch(&g_q, 0, AOD_TOUCH_DOWN, nan_val, 0.f, 0LL));
    assert(!aod_input_queue_touch(&g_q, 0, AOD_TOUCH_DOWN, 0.f, nan_val, 0LL));
    assert(!aod_input_queue_touch(&g_q, 0, AOD_TOUCH_DOWN, inf_val, 0.f, 0LL));
    assert(!aod_input_queue_touch(&g_q, 0, AOD_TOUCH_DOWN, 0.f, -inf_val, 0LL));
    assert(aod_input_queue_count(&g_q) == 0U);

    /* Confirm no map mutation occurred throughout. */
    bool ok = aod_input_queue_touch(&g_q, 0, AOD_TOUCH_DOWN, 1.f, 1.f, 0LL);
    assert(ok);
    const aod_input_event *ev = aod_input_queue_at(&g_q, 0);
    assert(ev != NULL);
    assert(ev->id == 0);   /* slot 0 still free — no prior mutation */
}

/* ---- case 6: push kind filter, timestamps preserved, at/count edge ----- */

static void test_push_kind_and_order(void)
{
    testreset();

    /* TOUCH kind rejected by push */
    aod_input_event touch_ev;
    touch_ev.kind = AOD_KIND_TOUCH;
    touch_ev.type = AOD_TOUCH_DOWN;
    touch_ev.id = 0; touch_ev.x = 0.f; touch_ev.y = 0.f; touch_ev.t = 0LL;
    assert(!aod_input_queue_push(&g_q, &touch_ev));
    assert(aod_input_queue_count(&g_q) == 0U);

    /* KEY and WINDOW accepted; timestamps and order preserved. */
    assert(push_key(42, 7, 1000LL));
    assert(push_window(3, 2000LL));
    assert(push_key(99, 1, 3000LL));

    assert(aod_input_queue_count(&g_q) == 3U);

    const aod_input_event *ev0 = aod_input_queue_at(&g_q, 0);
    assert(ev0 != NULL);
    assert(ev0->kind == AOD_KIND_KEY);
    assert(ev0->type == 42);
    assert(ev0->t    == 1000LL);

    const aod_input_event *ev1 = aod_input_queue_at(&g_q, 1);
    assert(ev1 != NULL);
    assert(ev1->kind == AOD_KIND_WINDOW);
    assert(ev1->t    == 2000LL);

    const aod_input_event *ev2 = aod_input_queue_at(&g_q, 2);
    assert(ev2 != NULL);
    assert(ev2->kind == AOD_KIND_KEY);
    assert(ev2->t    == 3000LL);

    /* at out-of-range -> NULL */
    assert(aod_input_queue_at(&g_q, 3)    == NULL);
    assert(aod_input_queue_at(&g_q, 0xFFFFFFFFU) == NULL);

    /* count on NULL -> 0 (already tested; confirm boundary) */
    assert(aod_input_queue_count(NULL) == 0U);
}

/* ---- case 7: physical max ID 255 accepted; slot safely reused ----------- */

static void test_physical_max_id(void)
{
    testreset();

    /* vita_id 255 is the maximum physical byte value; must be accepted. */
    bool ok = aod_input_queue_touch(&g_q, 255, AOD_TOUCH_DOWN, 5.0f, 6.0f, 10LL);
    assert(ok);
    assert(aod_input_queue_count(&g_q) == 1U);

    const aod_input_event *ev = aod_input_queue_at(&g_q, 0);
    assert(ev != NULL);
    assert(ev->kind == AOD_KIND_TOUCH);
    assert(ev->type == AOD_TOUCH_DOWN);
    int owned_slot = ev->id;

    /* MOVE for vita_id 255 uses same slot. */
    ok = aod_input_queue_touch(&g_q, 255, AOD_TOUCH_MOVE, 6.0f, 7.0f, 20LL);
    assert(ok);
    ev = aod_input_queue_at(&g_q, 1);
    assert(ev != NULL);
    assert(ev->type == AOD_TOUCH_MOVE);
    assert(ev->id   == owned_slot);

    /* UP for vita_id 255 frees the slot. */
    ok = aod_input_queue_touch(&g_q, 255, AOD_TOUCH_UP, 6.0f, 7.0f, 30LL);
    assert(ok);
    ev = aod_input_queue_at(&g_q, 2);
    assert(ev != NULL);
    assert(ev->type == AOD_TOUCH_UP);
    assert(ev->id   == owned_slot);

    /* Slot is now free; a new DOWN for vita_id 0 reuses it. */
    aod_input_queue_clear(&g_q);
    ok = aod_input_queue_touch(&g_q, 0, AOD_TOUCH_DOWN, 1.0f, 1.0f, 40LL);
    assert(ok);
    ev = aod_input_queue_at(&g_q, 0);
    assert(ev != NULL);
    assert(ev->id == owned_slot);   /* slot safely reused */
}

/* ---- main --------------------------------------------------------------- */

int main(void)
{
    test_basic_touch_sequence();
    test_full_queue_rejects_down();
    test_map_owner_persists();
    test_map_capacity();
    test_invalid_inputs();
    test_push_kind_and_order();
    test_physical_max_id();
    printf("All input_queue tests passed.\n");
    return 0;
}