/*
 * tests/input_test.c — C99 assert-based tests for the aod_input subsystem.
 * Covers: init validation, frame validation, cursor/cross/dpad flow,
 * coordinate mapping, front priority over cursor, cross-block suppression,
 * and duplicate/overflow front-contact rejection.
 *
 * Compile:
 *   cc -std=c99 -Wall -Wextra -Werror -O3 -fno-fast-math -lm \
 *      -Isource source/aod/input.c source/aod/input_queue.c \
 *      source/aod/touch_ids.c tests/input_test.c -o tests/run/input_test
 */

/* NOTE: this file relaxes P10 rule 4 (functions ≤60 non-blank lines) only for
 * test_cross_block_through_front which requires a 7-step interaction trace
 * that cannot be decomposed without exposing opaque internal state. */

#include "aod/input.h"
#include "aod/input_queue.h"
#include <assert.h>
#include <float.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#define W  960.0f
#define H  544.0f
#define TS_STEP 10000u   /* 10 ms in microseconds */

/* ── emit capture ───────────────────────────────────────────────────────── */
#define MAX_CAP 32
typedef struct { aod_event ev[MAX_CAP]; unsigned n; aod_input_queue q; } cap_t;
static void cap_init(cap_t *c) { aod_input_queue_init(&c->q); c->n = 0u; }
static bool emit_cap(void *ud, const aod_event *e) {
    cap_t *c = (cap_t *)ud;
    if (c->n >= MAX_CAP) return false;
    if (!aod_input_queue_emit(&c->q, e, 1234LL)) return false;
    c->ev[c->n++] = *e;
    return true;
}

/* ── frame builder ──────────────────────────────────────────────────────── */
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

/* Neutral step: clears cap and queue count, increments ts, calls step. */
static bool do_step(aod_input_state *s, aod_input_frame *f,
                    cap_t *c, uint64_t *ts) {
    f->timestamp_us = *ts;
    *ts += TS_STEP;
    c->n = 0;
    aod_input_queue_clear(&c->q);
    return aod_input_step(s, f, emit_cap, c);
}

/* ── result tracking ────────────────────────────────────────────────────── */
static int g_pass = 0, g_fail = 0;
#define PASS(name) do { printf("PASS: %s\n", (name)); g_pass++; } while (0)
#define FAIL(name) do { printf("FAIL: %s\n", (name)); g_fail++; } while (0)
#define CHECK(name, expr) do { if (expr) PASS(name); else FAIL(name); } while (0)

/* ══════════════════════════════════════════════════════════════════════════
 * Init validation tests
 * ══════════════════════════════════════════════════════════════════════════ */

static void test_init_valid(void) {
    aod_input_state s;
    bool ok = aod_input_init(&s, AOD_INPUT_SHOULDERS, W, H);
    CHECK("init_valid", ok && s.width == W && s.height == H
          && s.mode == AOD_INPUT_SHOULDERS);
}

static void test_init_null(void) {
    bool ok = aod_input_init(NULL, AOD_INPUT_SHOULDERS, W, H);
    CHECK("init_null", !ok);
}

static void test_init_mode99(void) {
    aod_input_state s;
    bool ok = aod_input_init(&s, (aod_input_mode)99, W, H);
    CHECK("init_mode99", !ok);
}

static void test_init_nan_width(void) {
    aod_input_state s;
    bool ok = aod_input_init(&s, AOD_INPUT_SHOULDERS, (float)NAN, H);
    CHECK("init_nan_width", !ok);
}

static void test_init_inf_height(void) {
    aod_input_state s;
    bool ok = aod_input_init(&s, AOD_INPUT_SHOULDERS, W, (float)INFINITY);
    CHECK("init_inf_height", !ok);
}

static void test_init_width_small(void) {
    aod_input_state s;
    /* width 0.5 is < 1: must reject */
    bool ok = aod_input_init(&s, AOD_INPUT_SHOULDERS, 0.5f, H);
    CHECK("init_width_0_5", !ok);
}

/* ══════════════════════════════════════════════════════════════════════════
 * Frame validation: span overflow (FLT_MAX - (-FLT_MAX) = +inf)
 * ══════════════════════════════════════════════════════════════════════════ */

static void test_frame_span_overflow(void) {
    aod_input_state s;
    assert(aod_input_init(&s, AOD_INPUT_SHOULDERS, W, H));
    aod_input_frame f; cap_t c; cap_init(&c);
    mk_frame(&f, 0);
    f.front.min_x = -FLT_MAX; f.front.max_x = FLT_MAX; /* span = +inf */
    bool ok = aod_input_step(&s, &f, emit_cap, &c);
    CHECK("frame_span_overflow", !ok && c.n == 0);
}

/* ══════════════════════════════════════════════════════════════════════════
 * Cursor: Cross DOWN + release UP + pointer visible.
 * Bridge assertions: TOUCH DOWN→type=0, UP→type=2; timestamp=1234.
 * ══════════════════════════════════════════════════════════════════════════ */

static void test_cursor_cross_dpad_move(void) {
    aod_input_state s;
    assert(aod_input_init(&s, AOD_INPUT_SHOULDERS, W, H));
    aod_input_frame f; cap_t c; cap_init(&c);
    uint64_t ts = 0u;

    mk_frame(&f, 0);
    /* Step 0: neutral → rearm */
    assert(do_step(&s, &f, &c, &ts));
    assert(c.n == 0);

    /* Step 1: Cross + RIGHT held → pointer moves 6 px right; cursor DOWN */
    f.buttons = AOD_BTN_CROSS | AOD_BTN_RIGHT;
    bool ok1 = do_step(&s, &f, &c, &ts);
    assert(ok1 && c.n == 1);
    assert(c.ev[0].kind == AOD_EVENT_TOUCH);
    assert(c.ev[0].source_id == AOD_INPUT_SOURCE_CURSOR);
    assert(c.ev[0].action == AOD_ACTION_DOWN);
    /* pointer_x started at 479.5, moved 6 px → 485.5 */
    assert(fabsf(c.ev[0].x - 485.5f) < 0.01f);
    /* Bridge: TOUCH DOWN → queue type=AOD_TOUCH_DOWN(0), t=1234 */
    const aod_input_event *qe_d = aod_input_queue_at(&c.q, 0u);
    assert(qe_d != NULL && qe_d->kind == AOD_KIND_TOUCH
           && qe_d->type == AOD_TOUCH_DOWN && qe_d->t == 1234LL);

    /* Step 2: Cross released → cursor UP */
    f.buttons = 0;
    bool ok2 = do_step(&s, &f, &c, &ts);
    assert(ok2 && c.n == 1);
    assert(c.ev[0].action == AOD_ACTION_UP);
    assert(c.ev[0].source_id == AOD_INPUT_SOURCE_CURSOR);
    /* Bridge: TOUCH UP → queue type=AOD_TOUCH_UP(2), t=1234 */
    const aod_input_event *qe_u = aod_input_queue_at(&c.q, 0u);
    assert(qe_u != NULL && qe_u->kind == AOD_KIND_TOUCH
           && qe_u->type == AOD_TOUCH_UP && qe_u->t == 1234LL);

    /* Pointer must be visible (was moved) */
    float px, py;
    bool vis = aod_input_pointer(&s, &px, &py);
    CHECK("cursor_cross_dpad_move", vis && fabsf(px - 485.5f) < 0.01f);
}

/* ══════════════════════════════════════════════════════════════════════════
 * Pointer clamping: dpad right at right edge stays ≤ W-1
 * ══════════════════════════════════════════════════════════════════════════ */

static void test_pointer_clamp(void) {
    aod_input_state s;
    assert(aod_input_init(&s, AOD_INPUT_SHOULDERS, W, H));
    aod_input_frame f; cap_t c; cap_init(&c);
    uint64_t ts = 0u;

    mk_frame(&f, 0);
    assert(do_step(&s, &f, &c, &ts)); /* rearm */

    /* Hold RIGHT for many steps to push pointer to edge */
    f.buttons = AOD_BTN_RIGHT;
    for (int i = 0; i < 200; i++) (void)do_step(&s, &f, &c, &ts);

    float px, py;
    (void)aod_input_pointer(&s, &px, &py);
    CHECK("pointer_clamp_right", px <= W - 1.0f && px >= 0.0f);
}

/* ══════════════════════════════════════════════════════════════════════════
 * Front coordinate mapping: contact at panel max → screen max.
 * Bridge: TOUCH DOWN type=0 and MOVE type=1; t=1234.
 * ══════════════════════════════════════════════════════════════════════════ */

static void test_front_coord_map_max(void) {
    aod_input_state s;
    assert(aod_input_init(&s, AOD_INPUT_SHOULDERS, W, H));
    aod_input_frame f; cap_t c; cap_init(&c);
    uint64_t ts = 0u;

    mk_frame(&f, 0);
    assert(do_step(&s, &f, &c, &ts)); /* rearm */

    /* Front contact id=255 at panel coordinates (1919,1087) */
    f.front_contacts[0].id = 255;
    f.front_contacts[0].x  = 1919.0f;
    f.front_contacts[0].y  = 1087.0f;
    f.front_count = 1;
    bool ok = do_step(&s, &f, &c, &ts);
    assert(ok && c.n >= 1);
    /* First event: front DOWN, id=255, screen (959,543) */
    bool found = false;
    for (unsigned i = 0; i < c.n; i++) {
        if (c.ev[i].source_id == 255 && c.ev[i].action == AOD_ACTION_DOWN) {
            assert(fabsf(c.ev[i].x - 959.0f) < 0.01f);
            assert(fabsf(c.ev[i].y - 543.0f) < 0.01f);
            /* Bridge: DOWN → queue type=0, t=1234 */
            const aod_input_event *qe = aod_input_queue_at(&c.q, i);
            assert(qe != NULL && qe->type == AOD_TOUCH_DOWN && qe->t == 1234LL);
            found = true;
        }
    }
    CHECK("front_coord_map_max", found);
    /* Bridge MOVE assertion: move contact, verify MOVE type=1, t=1234 */
    f.front_contacts[0].x = 1900.0f; f.front_contacts[0].y = 1080.0f;
    assert(do_step(&s, &f, &c, &ts) && c.n >= 1);
    bool move_found = false;
    for (unsigned i = 0; i < c.n; i++) {
        if (c.ev[i].source_id == 255 && c.ev[i].action == AOD_ACTION_MOVE) {
            const aod_input_event *qm = aod_input_queue_at(&c.q, i);
            assert(qm != NULL && qm->type == AOD_TOUCH_MOVE && qm->t == 1234LL);
            move_found = true;
        }
    }
    assert(move_found);
}

static void test_front_coord_map_min(void) {
    aod_input_state s;
    assert(aod_input_init(&s, AOD_INPUT_SHOULDERS, W, H));
    aod_input_frame f; cap_t c; cap_init(&c);
    uint64_t ts = 0u;

    mk_frame(&f, 0);
    assert(do_step(&s, &f, &c, &ts));

    f.front_contacts[0].id = 0;
    f.front_contacts[0].x  = 0.0f;
    f.front_contacts[0].y  = 0.0f;
    f.front_count = 1;
    bool ok = do_step(&s, &f, &c, &ts);
    assert(ok);
    bool found = false;
    for (unsigned i = 0; i < c.n; i++) {
        if (c.ev[i].source_id == 0 && c.ev[i].action == AOD_ACTION_DOWN) {
            assert(fabsf(c.ev[i].x) < 0.01f);
            assert(fabsf(c.ev[i].y) < 0.01f);
            found = true;
        }
    }
    CHECK("front_coord_map_min", found);
}

/* ══════════════════════════════════════════════════════════════════════════
 * Front priority: cursor UP emitted before front DOWN.
 * Bridge: UP type=2 at queue[0], DOWN type=0 at queue[1]; t=1234.
 * ══════════════════════════════════════════════════════════════════════════ */

static void test_front_priority_cursor(void) {
    aod_input_state s;
    assert(aod_input_init(&s, AOD_INPUT_SHOULDERS, W, H));
    aod_input_frame f; cap_t c; cap_init(&c);
    uint64_t ts = 0u;

    mk_frame(&f, 0);
    assert(do_step(&s, &f, &c, &ts)); /* rearm */

    /* Step 1: Cross pressed → cursor DOWN */
    f.buttons = AOD_BTN_CROSS;
    assert(do_step(&s, &f, &c, &ts));
    assert(c.n == 1 && c.ev[0].source_id == AOD_INPUT_SOURCE_CURSOR
           && c.ev[0].action == AOD_ACTION_DOWN);

    /* Step 2: Cross still held + front contact arrives */
    f.buttons = AOD_BTN_CROSS;
    f.front_contacts[0].id = 10;
    f.front_contacts[0].x  = 960.0f;
    f.front_contacts[0].y  = 543.0f;
    f.front_count = 1;
    bool ok = do_step(&s, &f, &c, &ts);
    /* Must emit cursor UP THEN front DOWN (in that order) */
    assert(ok && c.n >= 2);
    bool cursor_up_first = (c.ev[0].source_id == AOD_INPUT_SOURCE_CURSOR
                            && c.ev[0].action == AOD_ACTION_UP);
    bool front_down_second = false;
    for (unsigned i = 1; i < c.n; i++) {
        if (c.ev[i].source_id == 10 && c.ev[i].action == AOD_ACTION_DOWN)
            front_down_second = true;
    }
    /* Bridge ordering: queue[0]=TOUCH UP(2), queue[1]=TOUCH DOWN(0), t=1234 */
    const aod_input_event *q0 = aod_input_queue_at(&c.q, 0u);
    const aod_input_event *q1 = aod_input_queue_at(&c.q, 1u);
    assert(q0 != NULL && q0->type == AOD_TOUCH_UP   && q0->t == 1234LL);
    assert(q1 != NULL && q1->type == AOD_TOUCH_DOWN && q1->t == 1234LL);
    CHECK("front_priority_cursor", cursor_up_first && front_down_second);
}

/* ══════════════════════════════════════════════════════════════════════════
 * Cross-block: Cross held through front contact not replayed until
 * raw release and fresh press.
 * Bridge: TOUCH MOVE type=1 verified between cursor DOWN and cursor UP steps.
 * ══════════════════════════════════════════════════════════════════════════ */

static void test_cross_block_through_front(void) {
    aod_input_state s;
    assert(aod_input_init(&s, AOD_INPUT_SHOULDERS, W, H));
    aod_input_frame f; cap_t c; cap_init(&c);
    uint64_t ts = 0u;
    bool ok;

    mk_frame(&f, 0);
    assert(do_step(&s, &f, &c, &ts)); /* rearm */

    /* Step 1: Cross → cursor DOWN */
    f.buttons = AOD_BTN_CROSS;
    ok = do_step(&s, &f, &c, &ts);
    assert(ok && c.n == 1 && c.ev[0].action == AOD_ACTION_DOWN);

    /* Step 1b: Cross + RIGHT → cursor MOVE; bridge: type=1, t=1234 */
    f.buttons = AOD_BTN_CROSS | AOD_BTN_RIGHT;
    ok = do_step(&s, &f, &c, &ts);
    assert(ok && c.n == 1 && c.ev[0].action == AOD_ACTION_MOVE);
    const aod_input_event *qe_mv = aod_input_queue_at(&c.q, 0u);
    assert(qe_mv != NULL && qe_mv->type == AOD_TOUCH_MOVE && qe_mv->t == 1234LL);

    /* Step 2: Cross + front → cursor UP (cross_blocked set), front DOWN */
    f.buttons = AOD_BTN_CROSS;
    f.front_contacts[0].id = 5; f.front_contacts[0].x = 480.0f;
    f.front_contacts[0].y = 271.0f; f.front_count = 1;
    ok = do_step(&s, &f, &c, &ts);
    assert(ok);
    bool saw_cursor_up = false;
    for (unsigned i = 0; i < c.n; i++)
        if (c.ev[i].source_id == AOD_INPUT_SOURCE_CURSOR
            && c.ev[i].action == AOD_ACTION_UP) saw_cursor_up = true;
    assert(saw_cursor_up);

    /* Step 3: Cross held, front released → front UP, but NO cursor DOWN (blocked) */
    f.front_count = 0; f.buttons = AOD_BTN_CROSS;
    ok = do_step(&s, &f, &c, &ts);
    bool got_new_cursor_down = false;
    for (unsigned i = 0; i < c.n; i++)
        if (c.ev[i].source_id == AOD_INPUT_SOURCE_CURSOR
            && c.ev[i].action == AOD_ACTION_DOWN) got_new_cursor_down = true;
    assert(!got_new_cursor_down);

    /* Step 4: Cross released */
    f.buttons = 0;
    ok = do_step(&s, &f, &c, &ts);
    assert(ok);

    /* Step 5: Cross re-pressed → cursor DOWN again (fresh press) */
    f.buttons = AOD_BTN_CROSS;
    ok = do_step(&s, &f, &c, &ts);
    assert(ok);
    bool got_cursor_down = false;
    for (unsigned i = 0; i < c.n; i++)
        if (c.ev[i].source_id == AOD_INPUT_SOURCE_CURSOR
            && c.ev[i].action == AOD_ACTION_DOWN) got_cursor_down = true;
    CHECK("cross_block_through_front", got_cursor_down);
}

/* ══════════════════════════════════════════════════════════════════════════
 * Duplicate front-contact IDs → frame rejected (returns false)
 * ══════════════════════════════════════════════════════════════════════════ */

static void test_duplicate_front_ids(void) {
    aod_input_state s;
    assert(aod_input_init(&s, AOD_INPUT_SHOULDERS, W, H));
    aod_input_frame f; cap_t c; cap_init(&c);
    uint64_t ts = 0u;

    mk_frame(&f, 0);
    assert(do_step(&s, &f, &c, &ts)); /* rearm */

    /* Two contacts with the same id: invalid */
    f.front_contacts[0].id = 3; f.front_contacts[0].x = 480.0f; f.front_contacts[0].y = 271.0f;
    f.front_contacts[1].id = 3; f.front_contacts[1].x = 500.0f; f.front_contacts[1].y = 271.0f;
    f.front_count = 2;
    bool ok = do_step(&s, &f, &c, &ts);
    CHECK("duplicate_front_ids", !ok);
}

/* ══════════════════════════════════════════════════════════════════════════
 * front_count > AOD_INPUT_FRONT_CAP (7 > 6) → frame rejected
 * ══════════════════════════════════════════════════════════════════════════ */

static void test_front_count_overflow(void) {
    aod_input_state s;
    assert(aod_input_init(&s, AOD_INPUT_SHOULDERS, W, H));
    aod_input_frame f; cap_t c; cap_init(&c);
    uint64_t ts = 0u;

    mk_frame(&f, 0);
    assert(do_step(&s, &f, &c, &ts)); /* rearm */

    f.front_count = AOD_INPUT_FRONT_CAP + 1u; /* 7 > capacity 6 */
    bool ok = do_step(&s, &f, &c, &ts);
    CHECK("front_count_overflow", !ok);
}

/* ══════════════════════════════════════════════════════════════════════════
 * aod_input_pointer: fails on NULL args; returns false until visible
 * ══════════════════════════════════════════════════════════════════════════ */

static void test_pointer_null_args(void) {
    aod_input_state s;
    assert(aod_input_init(&s, AOD_INPUT_SHOULDERS, W, H));
    float px, py;
    bool a = aod_input_pointer(NULL, &px, &py);
    bool b = aod_input_pointer(&s, NULL, &py);
    bool c2 = aod_input_pointer(&s, &px, NULL);
    bool d = aod_input_pointer(&s, &px, &py); /* not visible yet */
    CHECK("pointer_null_args", !a && !b && !c2 && !d);
}

/* ══════════════════════════════════════════════════════════════════════════
 * main
 * ══════════════════════════════════════════════════════════════════════════ */

int main(void) {
    test_init_valid();
    test_init_null();
    test_init_mode99();
    test_init_nan_width();
    test_init_inf_height();
    test_init_width_small();
    test_frame_span_overflow();
    test_cursor_cross_dpad_move();
    test_pointer_clamp();
    test_front_coord_map_max();
    test_front_coord_map_min();
    test_front_priority_cursor();
    test_cross_block_through_front();
    test_duplicate_front_ids();
    test_front_count_overflow();
    test_pointer_null_args();
    printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}