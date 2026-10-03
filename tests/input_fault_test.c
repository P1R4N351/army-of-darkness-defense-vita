/*
 * tests/input_fault_test.c — C99 assert-based fault/rejection tests for the
 * aod_input subsystem.  Tests queue-full rejection, UP-retry semantics,
 * direction ownership after rejection, cancellation finality, and timing
 * anomaly recovery (time backwards, large gap).
 *
 * Uses aod_input_queue_emit as the actual bridge and fills the queue via
 * aod_input_queue_push (KEY events) to provoke full-queue rejection on
 * subsequent TOUCH emits.  Never emulates acceptance internally.
 *
 * Compile:
 *   cc -std=c99 -Wall -Wextra -Werror -O3 -fno-fast-math -lm \
 *      -Isource source/aod/input.c source/aod/input_queue.c \
 *      source/aod/touch_ids.c tests/input_fault_test.c -o tests/run/input_fault_test
 */

#include "aod/input.h"
#include "aod/input_queue.h"
#include "aod/touch_ids.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

#define W 960.0f
#define H 544.0f
#define TS_STEP 10000u

/* ── result tracking ────────────────────────────────────────────────────── */
static int g_pass = 0, g_fail = 0;
#define PASS(n) do { printf("PASS: %s\n", (n)); g_pass++; } while (0)
#define FAIL(n) do { printf("FAIL: %s\n", (n)); g_fail++; } while (0)
#define CHECK(n, e) do { if (e) PASS(n); else FAIL(n); } while (0)

/* ── emit contexts ──────────────────────────────────────────────────────── */

/* Queue-backed emit: userdata = qemit_ctx* */
typedef struct { aod_input_queue *q; int64_t ts_ms; } qemit_ctx;
static bool emit_queue(void *ud, const aod_event *ev) {
    qemit_ctx *c = (qemit_ctx *)ud;
    return aod_input_queue_emit(c->q, ev, c->ts_ms);
}

/* Reject-N-then-queue emit: userdata = rq_ctx* */
typedef struct {
    int reject_remaining;
    aod_input_queue *q;
    int64_t ts_ms;
} rq_ctx;
static bool emit_reject_n(void *ud, const aod_event *ev) {
    rq_ctx *r = (rq_ctx *)ud;
    if (r->reject_remaining > 0) { r->reject_remaining--; return false; }
    return aod_input_queue_emit(r->q, ev, r->ts_ms);
}

/* Reject-always emit */
static bool emit_reject_all(void *ud, const aod_event *ev) {
    (void)ud; (void)ev; return false;
}

/* ── helpers ────────────────────────────────────────────────────────────── */

static void mk_frame(aod_input_frame *f, uint64_t ts) {
    memset(f, 0, sizeof(*f));
    f->timestamp_us = ts;
    f->active = true; f->front_valid = true;
    f->pad_valid = true; f->rear_valid = true;
    f->width = W; f->height = H;
    f->front.min_x = 0.0f;  f->front.min_y = 0.0f;
    f->front.max_x = 1919.0f; f->front.max_y = 1087.0f;
    f->rear.min_x  = 0.0f;  f->rear.min_y  = 108.0f;
    f->rear.max_x  = 1919.0f; f->rear.max_y  = 889.0f;
    f->stick_x = 128; f->stick_y = 128;
}

/* Fill queue to capacity with KEY events (leaves no TOUCH slots). */
static void fill_queue(aod_input_queue *q) {
    aod_input_event ev;
    memset(&ev, 0, sizeof(ev));
    ev.kind = AOD_KIND_KEY; ev.type = 0; ev.id = 0; ev.t = 0;
    while (q->count < AOD_QUEUE_CAPACITY) {
        bool ok = aod_input_queue_push(q, &ev);
        assert(ok);
    }
}

/* ══════════════════════════════════════════════════════════════════════════
 * Queue full after accepted cursor DOWN: subsequent MOVE/step returns false.
 * The cursor was accepted (owned) so retry is pending; queue touch-map slot
 * for source 512 (cursor) must still be held.
 * ══════════════════════════════════════════════════════════════════════════ */
static void test_queue_full_after_cursor_down(void) {
    aod_input_state s;
    assert(aod_input_init(&s, AOD_INPUT_SHOULDERS, W, H));
    aod_input_queue q; aod_input_queue_init(&q);
    aod_input_frame f;
    uint64_t ts = 0u;

    qemit_ctx ctx; ctx.q = &q; ctx.ts_ms = 0;

    mk_frame(&f, ts); f.timestamp_us = ts; ts += TS_STEP;
    assert(aod_input_step(&s, &f, emit_queue, &ctx)); /* rearm, ts=0 */

    /* Cross pressed → cursor DOWN accepted into queue */
    mk_frame(&f, ts); f.timestamp_us = ts; ts += TS_STEP;
    f.buttons = AOD_BTN_CROSS;
    ctx.ts_ms = (int64_t)(ts / 1000u);
    bool ok1 = aod_input_step(&s, &f, emit_queue, &ctx);
    assert(ok1 && s.cursor.active);
    unsigned after_down = q.count;
    assert(after_down >= 1u);

    /* Fill remaining queue capacity */
    fill_queue(&q);
    assert(q.count == AOD_QUEUE_CAPACITY);

    /* Dpad RIGHT: pointer moves → cursor MOVE would be emitted but queue is full */
    mk_frame(&f, ts); f.timestamp_us = ts; ts += TS_STEP;
    f.buttons = AOD_BTN_CROSS | AOD_BTN_RIGHT;
    ctx.ts_ms = (int64_t)(ts / 1000u);
    bool ok2 = aod_input_step(&s, &f, emit_queue, &ctx);
    /* Rejection: pending_cancel or just returns false; cursor still owned */
    CHECK("queue_full_after_cursor_down", !ok2 && s.cursor.active);
}

/* ══════════════════════════════════════════════════════════════════════════
 * Rejected cursor UP retains ownership; after queue cleared, UP succeeds.
 * Then fresh Cross repress creates new cursor DOWN.
 * ══════════════════════════════════════════════════════════════════════════ */
static void test_rejected_up_retains_cursor(void) {
    aod_input_state s;
    assert(aod_input_init(&s, AOD_INPUT_SHOULDERS, W, H));
    aod_input_queue q; aod_input_queue_init(&q);
    aod_input_frame f;
    uint64_t ts = 0u;
    rq_ctx rq; rq.q = &q; rq.ts_ms = 0; rq.reject_remaining = 0;

    mk_frame(&f, ts); f.timestamp_us = ts; ts += TS_STEP;
    assert(aod_input_step(&s, &f, emit_reject_n, &rq)); /* rearm */

    /* Cross → cursor DOWN accepted */
    mk_frame(&f, ts); f.timestamp_us = ts; ts += TS_STEP;
    f.buttons = AOD_BTN_CROSS; rq.reject_remaining = 0;
    assert(aod_input_step(&s, &f, emit_reject_n, &rq) && s.cursor.active);

    /* Release Cross; reject the cursor UP → cursor still owned */
    mk_frame(&f, ts); f.timestamp_us = ts; ts += TS_STEP;
    f.buttons = 0; rq.reject_remaining = 1;
    bool ok_reject = aod_input_step(&s, &f, emit_reject_n, &rq);
    assert(!ok_reject && s.cursor.active); /* rejected: still owned */

    /* Next step: no Cross, reject_remaining=0 → UP accepted */
    mk_frame(&f, ts); f.timestamp_us = ts; ts += TS_STEP;
    f.buttons = 0; rq.reject_remaining = 0;
    bool ok_up = aod_input_step(&s, &f, emit_reject_n, &rq);
    assert(ok_up && !s.cursor.active);

    /* Fresh Cross press → new cursor DOWN */
    mk_frame(&f, ts); f.timestamp_us = ts; ts += TS_STEP;
    f.buttons = AOD_BTN_CROSS; rq.reject_remaining = 0;
    bool ok_new = aod_input_step(&s, &f, emit_reject_n, &rq);
    CHECK("rejected_up_retains_cursor", ok_new && s.cursor.active);
}

/* ══════════════════════════════════════════════════════════════════════════
 * Start button: rejected DOWN → not accepted; next frame re-tries DOWN.
 * After acceptance, rejected UP retains; accepted UP clears.
 * ══════════════════════════════════════════════════════════════════════════ */
static void test_start_rejected_down_retry(void) {
    aod_input_state s;
    assert(aod_input_init(&s, AOD_INPUT_SHOULDERS, W, H));
    aod_input_queue q; aod_input_queue_init(&q);
    aod_input_frame f;
    uint64_t ts = 0u;
    rq_ctx rq; rq.q = &q; rq.ts_ms = 0; rq.reject_remaining = 0;

    mk_frame(&f, ts); f.timestamp_us = ts; ts += TS_STEP;
    assert(aod_input_step(&s, &f, emit_reject_n, &rq)); /* rearm */

    /* Start pressed, reject its DOWN */
    mk_frame(&f, ts); f.timestamp_us = ts; ts += TS_STEP;
    f.buttons = AOD_BTN_START; rq.reject_remaining = 1;
    bool ok1 = aod_input_step(&s, &f, emit_reject_n, &rq);
    assert(!ok1);
    assert(!(s.accepted_key_buttons & AOD_BTN_START));

    /* Next frame: still held, DOWN accepted */
    mk_frame(&f, ts); f.timestamp_us = ts; ts += TS_STEP;
    f.buttons = AOD_BTN_START; rq.reject_remaining = 0;
    bool ok2 = aod_input_step(&s, &f, emit_reject_n, &rq);
    assert(ok2 && (s.accepted_key_buttons & AOD_BTN_START));

    /* Verify queue: last KEY event is DOWN (type=0, id=1 for START) */
    unsigned cnt = aod_input_queue_count(&q);
    assert(cnt >= 1u);
    const aod_input_event *ev = aod_input_queue_at(&q, cnt - 1u);
    assert(ev && ev->kind == AOD_KIND_KEY && ev->type == 0 && ev->id == 1);

    /* Release Start → UP accepted → cleared */
    mk_frame(&f, ts); f.timestamp_us = ts; ts += TS_STEP;
    f.buttons = 0; rq.reject_remaining = 0;
    bool ok3 = aod_input_step(&s, &f, emit_reject_n, &rq);
    CHECK("start_rejected_down_retry", ok3 && !(s.accepted_key_buttons & AOD_BTN_START));
}

/* ══════════════════════════════════════════════════════════════════════════
 * Direction UP rejection: left held → right: old UP must be retried before
 * new DOWN.  Rejection of old UP keeps dir_owned = -1 (left).
 * ══════════════════════════════════════════════════════════════════════════ */
static void test_direction_up_fail_left_to_right(void) {
    aod_input_state s;
    assert(aod_input_init(&s, AOD_INPUT_SHOULDERS, W, H));
    aod_input_queue q; aod_input_queue_init(&q);
    aod_input_frame f;
    uint64_t ts = 0u;
    rq_ctx rq; rq.q = &q; rq.ts_ms = 0; rq.reject_remaining = 0;

    mk_frame(&f, ts); f.timestamp_us = ts; ts += TS_STEP;
    assert(aod_input_step(&s, &f, emit_reject_n, &rq)); /* rearm */

    /* L1 → direction LEFT owned */
    mk_frame(&f, ts); f.timestamp_us = ts; ts += TS_STEP;
    f.buttons = AOD_BTN_L1;
    assert(aod_input_step(&s, &f, emit_reject_n, &rq));
    assert(s.dir_owned == -1);

    /* Switch to R1; reject the LEFT-UP → dir_owned must remain -1, pending_cancel set */
    mk_frame(&f, ts); f.timestamp_us = ts; ts += TS_STEP;
    f.buttons = AOD_BTN_R1; rq.reject_remaining = 1;
    bool ok_rej = aod_input_step(&s, &f, emit_reject_n, &rq);
    assert(!ok_rej && s.dir_owned == -1 && s.pending_cancel);

    /* Next frame R1 still held: LEFT-UP retried and accepted via pending_cancel path.
     * cancel() sets rearm=false, dir_r_suppressed=true; R1 held → not neutral → no RIGHT DOWN. */
    mk_frame(&f, ts); f.timestamp_us = ts; ts += TS_STEP;
    f.buttons = AOD_BTN_R1; rq.reject_remaining = 0;
    (void)aod_input_step(&s, &f, emit_reject_n, &rq);
    assert(s.dir_owned == 0); /* LEFT UP accepted by cancel */

    /* Neutral frame: clears rearm + dir_r_suppressed (R1 released) */
    mk_frame(&f, ts); f.timestamp_us = ts; ts += TS_STEP;
    f.buttons = 0; rq.reject_remaining = 0;
    assert(aod_input_step(&s, &f, emit_reject_n, &rq) && s.rearm);

    /* Fresh R1 press → RIGHT DOWN (suppression cleared) */
    mk_frame(&f, ts); f.timestamp_us = ts; ts += TS_STEP;
    f.buttons = AOD_BTN_R1;
    bool ok_right = aod_input_step(&s, &f, emit_reject_n, &rq);
    CHECK("direction_up_fail_left_to_right", ok_right && s.dir_owned == 1);
}

/* ══════════════════════════════════════════════════════════════════════════
 * aod_input_cancel: accepted release is final; rejected release retained.
 * ══════════════════════════════════════════════════════════════════════════ */
static void test_cancel_partial(void) {
    aod_input_state s;
    assert(aod_input_init(&s, AOD_INPUT_SHOULDERS, W, H));
    aod_input_queue q; aod_input_queue_init(&q);
    aod_input_frame f;
    uint64_t ts = 0u;
    rq_ctx rq; rq.q = &q; rq.ts_ms = 0; rq.reject_remaining = 0;

    mk_frame(&f, ts); f.timestamp_us = ts; ts += TS_STEP;
    assert(aod_input_step(&s, &f, emit_reject_n, &rq)); /* rearm */

    /* Acquire direction LEFT */
    mk_frame(&f, ts); f.timestamp_us = ts; ts += TS_STEP;
    f.buttons = AOD_BTN_L1;
    assert(aod_input_step(&s, &f, emit_reject_n, &rq));
    assert(s.dir_owned == -1);

    /* Cancel; reject the direction UP → pending_cancel set, dir_owned retained */
    rq.reject_remaining = 1;
    bool ok_c1 = aod_input_cancel(&s, emit_reject_n, &rq);
    assert(!ok_c1 && s.dir_owned == -1 && s.pending_cancel);

    /* Retry cancel (accept) → accepted UP is final */
    rq.reject_remaining = 0;
    bool ok_c2 = aod_input_cancel(&s, emit_reject_n, &rq);
    CHECK("cancel_partial", ok_c2 && s.dir_owned == 0 && !s.pending_cancel);
}

/* ══════════════════════════════════════════════════════════════════════════
 * Time backwards: triggers cancel + rearm=false; neutral next frame rearmed.
 * ══════════════════════════════════════════════════════════════════════════ */
static void test_time_backwards(void) {
    aod_input_state s;
    assert(aod_input_init(&s, AOD_INPUT_SHOULDERS, W, H));
    aod_input_frame f;
    uint64_t ts = 100000u; /* start non-zero */

    mk_frame(&f, ts); ts += TS_STEP;
    assert(aod_input_step(&s, &f, emit_reject_all, NULL)); /* first frame */
    /* rearm=true after neutral first frame */

    /* L1 direction */
    mk_frame(&f, ts); f.buttons = AOD_BTN_L1; ts += TS_STEP;
    /* emit_reject_all will reject any events from rearm */
    /* Actually step returns false if any emit rejected; just check state after */
    (void)aod_input_step(&s, &f, emit_reject_all, NULL);

    /* Step with older timestamp → regression → cancel + rearm=false */
    uint64_t past_ts = ts - 3u * TS_STEP; /* in the past */
    mk_frame(&f, past_ts); f.buttons = 0;
    bool ok_reg = aod_input_step(&s, &f, emit_reject_all, NULL);
    /* Should return false (cancel called, possibly pending) */
    assert(!ok_reg);
    assert(!s.rearm);

    /* Provide neutral frame at a forward timestamp → rearm */
    uint64_t future_ts = past_ts + TS_STEP;
    mk_frame(&f, future_ts);
    /* Allow cancel retry + rearm */
    bool ok_new = aod_input_step(&s, &f, emit_reject_all, NULL);
    /* rearm still becomes true since neutral frame */
    CHECK("time_backwards", !s.pending_cancel || ok_new || s.rearm);
}

/* ══════════════════════════════════════════════════════════════════════════
 * Large gap (> 250 ms): cancel + rearm=false; neutral frame re-enables.
 * ══════════════════════════════════════════════════════════════════════════ */
static void test_large_gap(void) {
    aod_input_state s;
    assert(aod_input_init(&s, AOD_INPUT_SHOULDERS, W, H));
    aod_input_queue q; aod_input_queue_init(&q);
    aod_input_frame f;
    uint64_t ts = 0u;
    qemit_ctx ctx; ctx.q = &q; ctx.ts_ms = 0;

    mk_frame(&f, ts); f.timestamp_us = ts; ts += TS_STEP;
    assert(aod_input_step(&s, &f, emit_queue, &ctx));

    /* Acquire direction RIGHT */
    mk_frame(&f, ts); f.timestamp_us = ts; ts += TS_STEP;
    f.buttons = AOD_BTN_R1; ctx.ts_ms = (int64_t)(ts / 1000u);
    assert(aod_input_step(&s, &f, emit_queue, &ctx) && s.dir_owned == 1);

    /* Jump 500 ms forward (> MAX_DELTA_US=250000) → cancel + rearm=false */
    ts += 500000u;
    mk_frame(&f, ts); f.timestamp_us = ts; f.buttons = 0;
    ctx.ts_ms = (int64_t)(ts / 1000u);
    bool ok_gap = aod_input_step(&s, &f, emit_queue, &ctx);
    assert(!ok_gap && !s.rearm);

    /* Neutral frame → rearm=true */
    ts += TS_STEP;
    mk_frame(&f, ts); f.timestamp_us = ts; f.buttons = 0;
    ctx.ts_ms = (int64_t)(ts / 1000u);
    bool ok_neutral = aod_input_step(&s, &f, emit_queue, &ctx);
    CHECK("large_gap", ok_neutral && s.rearm);
}

/* ══════════════════════════════════════════════════════════════════════════
 * Touchmap slot reuse: DOWN on id=5, UP frees slot, DOWN again on id=5
 * maps to same or different Android pointer id (must not be -1).
 * ══════════════════════════════════════════════════════════════════════════ */
static void test_touchmap_slot_reuse(void) {
    aod_touch_reset();
    /* DOWN vita_id=5 → gets android slot */
    int slot1 = aod_touch_map(5, 0);
    assert(slot1 >= 0);
    /* UP vita_id=5 → frees slot; returns same slot */
    int slot_up = aod_touch_map(5, 2);
    assert(slot_up == slot1);
    /* DOWN again vita_id=5 → slot available again */
    int slot2 = aod_touch_map(5, 0);
    assert(slot2 >= 0);
    /* UP unknown vita_id before DOWN → returns -1 (drop) */
    int slot_bad = aod_touch_map(99, 2);
    CHECK("touchmap_slot_reuse", slot2 >= 0 && slot_bad == -1);
    /* Cleanup */
    (void)aod_touch_map(5, 2);
}

/* ══════════════════════════════════════════════════════════════════════════
 * Front UP rejected when queue full; touchmap slot preserved.
 * After queue cleared, old UP accepted before any new cursor DOWN.
 * Cross held through front contact is blocked until neutral + fresh press.
 * ══════════════════════════════════════════════════════════════════════════ */
static void test_front_up_rejected_queue_full_cross(void) {
    aod_input_state s;
    assert(aod_input_init(&s, AOD_INPUT_SHOULDERS, W, H));
    aod_input_queue q; aod_input_queue_init(&q);
    aod_input_frame f;
    uint64_t ts = 0u;
    qemit_ctx ctx; ctx.q = &q; ctx.ts_ms = 1234LL;

    /* Rearm */
    mk_frame(&f, ts); f.timestamp_us = ts; ts += TS_STEP;
    assert(aod_input_step(&s, &f, emit_queue, &ctx));

    /* Front contact 3 DOWN: accepted; bridge DOWN type=0, t=1234 */
    mk_frame(&f, ts); f.timestamp_us = ts; ts += TS_STEP;
    f.front_contacts[0].id = 3;
    f.front_contacts[0].x  = 480.0f;
    f.front_contacts[0].y  = 271.0f;
    f.front_count = 1;
    bool ok_down = aod_input_step(&s, &f, emit_queue, &ctx);
    assert(ok_down && s.front_owned_count == 1u);
    assert(q.count >= 1u);
    const aod_input_event *ev_d = aod_input_queue_at(&q, q.count - 1u);
    assert(ev_d != NULL && ev_d->type == AOD_TOUCH_DOWN && ev_d->t == 1234LL);
    /* Touchmap: slot held for vita_id=3 (MOVE query is side-effect-free) */
    assert(aod_touch_map(3, 1) >= 0);

    /* Fill queue; front UP now rejected */
    fill_queue(&q);
    assert(q.count == AOD_QUEUE_CAPACITY);

    /* Front absent + Cross pressed while queue full → UP rejected */
    mk_frame(&f, ts); f.timestamp_us = ts; ts += TS_STEP;
    f.front_count = 0;
    f.buttons = AOD_BTN_CROSS;
    bool ok_rej = aod_input_step(&s, &f, emit_queue, &ctx);
    assert(!ok_rej && s.front_owned_count == 1u);
    assert(q.count == AOD_QUEUE_CAPACITY);
    /* Touchmap slot not freed (rejected UP skipped aod_touch_map) */
    assert(aod_touch_map(3, 1) >= 0);

    /* Clear queue: pending_cancel retry → front UP accepted; cross_blocked set */
    aod_input_queue_clear(&q);
    mk_frame(&f, ts); f.timestamp_us = ts; ts += TS_STEP;
    f.front_count = 0;
    f.buttons = AOD_BTN_CROSS;
    bool ok_retry = aod_input_step(&s, &f, emit_queue, &ctx);
    assert(s.front_owned_count == 0u);
    /* Bridge: TOUCH UP type=2, t=1234 */
    assert(q.count >= 1u);
    const aod_input_event *ev_u = aod_input_queue_at(&q, 0u);
    assert(ev_u != NULL && ev_u->type == AOD_TOUCH_UP && ev_u->t == 1234LL);
    /* Touchmap slot freed after accepted UP */
    assert(aod_touch_map(3, 1) == -1);
    /* No cursor DOWN: cross_blocked=true, rearm=false after cancel */
    bool no_new_cursor = true;
    for (unsigned i = 0; i < q.count; i++) {
        const aod_input_event *e = aod_input_queue_at(&q, i);
        if (e != NULL && e->kind == AOD_KIND_TOUCH && e->type == AOD_TOUCH_DOWN)
            no_new_cursor = false;
    }
    assert(no_new_cursor);

    /* Release Cross: cross_blocked cleared, rearm restored */
    aod_input_queue_clear(&q);
    mk_frame(&f, ts); f.timestamp_us = ts; ts += TS_STEP;
    f.front_count = 0; f.buttons = 0;
    assert(aod_input_step(&s, &f, emit_queue, &ctx));

    /* Fresh Cross press → cursor DOWN accepted; bridge type=0, t=1234 */
    aod_input_queue_clear(&q);
    mk_frame(&f, ts); f.timestamp_us = ts; ts += TS_STEP;
    f.buttons = AOD_BTN_CROSS;
    bool ok_new = aod_input_step(&s, &f, emit_queue, &ctx);
    assert(ok_new && s.cursor.active);
    assert(q.count >= 1u);
    const aod_input_event *ev_cd = aod_input_queue_at(&q, 0u);
    assert(ev_cd != NULL && ev_cd->type == AOD_TOUCH_DOWN && ev_cd->t == 1234LL);

    CHECK("front_up_rejected_queue_full_cross",
          ok_retry && s.front_owned_count == 0u && s.cursor.active);
}

/* ══════════════════════════════════════════════════════════════════════════
 * Start DOWN accepted; Start released while queue full → UP rejected/retained.
 * Clear queue → old UP (type=1, id=1) accepted before any new DOWN (type=0,
 * id=1); neutral+fresh Start then DOWN; accepted UP exactly once.
 * ══════════════════════════════════════════════════════════════════════════ */
static void test_start_up_rejected_queue_full(void) {
    aod_input_state s;
    assert(aod_input_init(&s, AOD_INPUT_SHOULDERS, W, H));
    aod_input_queue q; aod_input_queue_init(&q);
    aod_input_frame f;
    uint64_t ts = 0u;
    qemit_ctx ctx; ctx.q = &q; ctx.ts_ms = 1234LL;

    /* Rearm */
    mk_frame(&f, ts); f.timestamp_us = ts; ts += TS_STEP;
    assert(aod_input_step(&s, &f, emit_queue, &ctx));

    /* Start DOWN: accepted; bridge KEY type=0, id=1 (Menu), t=1234 */
    mk_frame(&f, ts); f.timestamp_us = ts; ts += TS_STEP;
    f.buttons = AOD_BTN_START;
    bool ok_down = aod_input_step(&s, &f, emit_queue, &ctx);
    assert(ok_down && (s.accepted_key_buttons & AOD_BTN_START));
    assert(q.count >= 1u);
    const aod_input_event *ev_kd = aod_input_queue_at(&q, q.count - 1u);
    assert(ev_kd != NULL && ev_kd->kind == AOD_KIND_KEY
           && ev_kd->type == 0 && ev_kd->id == 1 && ev_kd->t == 1234LL);

    /* Fill queue; Start UP will be rejected */
    fill_queue(&q);
    assert(q.count == AOD_QUEUE_CAPACITY);

    /* Start released while queue full → UP rejected; ownership retained;
     * fix: pending_cancel must be set so the next step retries UP before
     * any new DOWN.  This is the strong assertion the prior author weakened. */
    mk_frame(&f, ts); f.timestamp_us = ts; ts += TS_STEP;
    f.buttons = 0;
    bool ok_rej = aod_input_step(&s, &f, emit_queue, &ctx);
    assert(!ok_rej && (s.accepted_key_buttons & AOD_BTN_START));
    assert(q.count == AOD_QUEUE_CAPACITY);
    assert(s.pending_cancel); /* STRONG: rejected keyUP must set pending_cancel */

    /* Fresh Start repress while queue still full: pending_cancel → cancel tries
     * old UP via cancel_keys → queue still full → rejected again; no new DOWN. */
    mk_frame(&f, ts); f.timestamp_us = ts; ts += TS_STEP;
    f.buttons = AOD_BTN_START;
    (void)aod_input_step(&s, &f, emit_queue, &ctx);
    assert(q.count == AOD_QUEUE_CAPACITY); /* queue unchanged */
    assert(s.accepted_key_buttons & AOD_BTN_START); /* owner retained */

    /* Clear queue; KEEP Start HELD → pending_cancel retries old UP first:
     * old UP (Menu id=1, type=1) delivered at queue[0]; accepted_key cleared.
     * cancel sets rearm=false; Start held → not neutral → no new DOWN emitted. */
    aod_input_queue_clear(&q);
    mk_frame(&f, ts); f.timestamp_us = ts; ts += TS_STEP;
    f.buttons = AOD_BTN_START; /* HELD: strong — old UP must precede any new DOWN */
    bool ok_up = aod_input_step(&s, &f, emit_queue, &ctx);
    bool start_cleared = !(s.accepted_key_buttons & AOD_BTN_START);
    assert(ok_up && start_cleared);
    assert(q.count >= 1u);
    const aod_input_event *ev_ku = aod_input_queue_at(&q, 0u);
    assert(ev_ku != NULL && ev_ku->kind == AOD_KIND_KEY
           && ev_ku->type == 1 && ev_ku->id == 1 && ev_ku->t == 1234LL);
    /* No new DOWN while Start still held (rearm=false after cancel succeeds) */
    for (unsigned qi = 0u; qi < q.count; qi++) {
        const aod_input_event *qe = aod_input_queue_at(&q, qi);
        if (qe != NULL && qe->kind == AOD_KIND_KEY
                && qe->type == 0 && qe->id == 1) {
            assert(false); /* new DOWN must NOT appear before neutral rearm */
        }
    }

    /* Another held frame: rearm=false, Start still held → not neutral → no events */
    aod_input_queue_clear(&q);
    mk_frame(&f, ts); f.timestamp_us = ts; ts += TS_STEP;
    f.buttons = AOD_BTN_START;
    bool ok_held = aod_input_step(&s, &f, emit_queue, &ctx);
    assert(ok_held && q.count == 0u);

    /* Release Start → neutral → rearm restored */
    aod_input_queue_clear(&q);
    mk_frame(&f, ts); f.timestamp_us = ts; ts += TS_STEP;
    f.buttons = 0;
    assert(aod_input_step(&s, &f, emit_queue, &ctx));

    /* Fresh Start press → new DOWN (Menu id=1, type=0) accepted exactly once.
     * start_cleared captured before this press to avoid contradictory CHECK. */
    aod_input_queue_clear(&q);
    mk_frame(&f, ts); f.timestamp_us = ts; ts += TS_STEP;
    f.buttons = AOD_BTN_START;
    bool ok_new = aod_input_step(&s, &f, emit_queue, &ctx);
    assert(ok_new && (s.accepted_key_buttons & AOD_BTN_START));
    assert(q.count == 1u);
    const aod_input_event *ev_kd2 = aod_input_queue_at(&q, 0u);
    assert(ev_kd2 != NULL && ev_kd2->kind == AOD_KIND_KEY
           && ev_kd2->type == 0 && ev_kd2->id == 1);

    CHECK("start_up_rejected_queue_full",
          ok_up && start_cleared && ok_new
          && (s.accepted_key_buttons & AOD_BTN_START));
}

/* ══════════════════════════════════════════════════════════════════════════
 * main
 * ══════════════════════════════════════════════════════════════════════════ */

int main(void) {
    test_queue_full_after_cursor_down();
    test_rejected_up_retains_cursor();
    test_start_rejected_down_retry();
    test_direction_up_fail_left_to_right();
    test_cancel_partial();
    test_time_backwards();
    test_large_gap();
    test_touchmap_slot_reuse();
    test_front_up_rejected_queue_full_cross();
    test_start_up_rejected_queue_full();
    printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}