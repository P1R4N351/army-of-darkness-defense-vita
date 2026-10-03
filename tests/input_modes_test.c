/*
 * tests/input_modes_test.c — C99 assert-based mode-specific tests for the
 * aod_input subsystem.  Covers shoulder (SHOULDERS mode), rear-panel
 * (REAR mode), BOTH mode, runtime mode switching, and malformed-frame
 * failsafe cancellation.
 *
 * Compile:
 *   cc -std=c99 -Wall -Wextra -Werror -O3 -fno-fast-math -lm \
 *      -Isource source/aod/input.c source/aod/input_queue.c \
 *      source/aod/touch_ids.c tests/input_modes_test.c -o tests/run/input_modes_test
 */

#include "aod/input.h"
#include "aod/input_queue.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#define W 960.0f
#define H 544.0f
#define TS_STEP 10000u

/* Expected direction positions */
#define DIR_LEFT_X  ((W - 1.0f) * 0.25f)   /* 239.75 */
#define DIR_RIGHT_X ((W - 1.0f) * 0.75f)   /* 719.25 */
#define DIR_Y       ((H - 1.0f) * 0.5f)    /* 271.5  */

/* ── result tracking ────────────────────────────────────────────────────── */
static int g_pass = 0, g_fail = 0;
#define PASS(n) do { printf("PASS: %s\n", (n)); g_pass++; } while (0)
#define FAIL(n) do { printf("FAIL: %s\n", (n)); g_fail++; } while (0)
#define CHECK(n, e) do { if (e) PASS(n); else FAIL(n); } while (0)

/* ── emit capture ───────────────────────────────────────────────────────── */
#define MAX_CAP 32
typedef struct { aod_event ev[MAX_CAP]; unsigned n; aod_input_queue q; } cap_t;
static void cap_init(cap_t *c) { aod_input_queue_init(&c->q); c->n = 0u; }
static void cap_clear(cap_t *c) { aod_input_queue_clear(&c->q); c->n = 0u; }
static bool emit_cap(void *ud, const aod_event *e) {
    cap_t *c = (cap_t *)ud;
    if (c->n >= MAX_CAP) return false;
    if (!aod_input_queue_emit(&c->q, e, 1234LL)) return false;
    c->ev[c->n++] = *e;
    return true;
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

static bool do_step(aod_input_state *s, aod_input_frame *f,
                    cap_t *c, uint64_t *ts) {
    f->timestamp_us = *ts;
    *ts += TS_STEP;
    c->n = 0;
    aod_input_queue_clear(&c->q);
    return aod_input_step(s, f, emit_cap, c);
}

/* Count events of given source and action in cap */
static unsigned count_ev(const cap_t *c, int32_t src, int action) {
    unsigned n = 0;
    for (unsigned i = 0; i < c->n; i++)
        if (c->ev[i].source_id == src && c->ev[i].action == action) n++;
    return n;
}

/* ══════════════════════════════════════════════════════════════════════════
 * Shoulder left → both neutral → right → none:
 * Tests single-contact ownership: old UP before new DOWN; both neutral = UP.
 * Bridge: L1 DOWN → queue type=AOD_TOUCH_DOWN(0), t=1234.
 * ══════════════════════════════════════════════════════════════════════════ */

static void test_shoulder_left_to_right(void) {
    aod_input_state s;
    assert(aod_input_init(&s, AOD_INPUT_SHOULDERS, W, H));
    aod_input_frame f; cap_t c; cap_init(&c); uint64_t ts = 0u;

    mk_frame(&f, 0);
    assert(do_step(&s, &f, &c, &ts)); /* rearm */

    /* L1 → direction LEFT DOWN, source_id=513 */
    f.buttons = AOD_BTN_L1;
    assert(do_step(&s, &f, &c, &ts));
    assert(count_ev(&c, AOD_INPUT_SOURCE_LEFT, AOD_ACTION_DOWN) == 1u);
    assert(fabsf(c.ev[0].x - DIR_LEFT_X) < 0.01f);
    assert(s.dir_owned == -1);
    /* Bridge: TOUCH DOWN → queue type=AOD_TOUCH_DOWN(0), t=1234 */
    const aod_input_event *qe_shld = aod_input_queue_at(&c.q, 0u);
    assert(qe_shld != NULL && qe_shld->kind == AOD_KIND_TOUCH
           && qe_shld->type == AOD_TOUCH_DOWN && qe_shld->t == 1234LL);

    /* Both L1+R1: desired=0 (both cancel) → UP for LEFT */
    f.buttons = AOD_BTN_L1 | AOD_BTN_R1;
    assert(do_step(&s, &f, &c, &ts));
    assert(count_ev(&c, AOD_INPUT_SOURCE_LEFT, AOD_ACTION_UP) == 1u);
    assert(s.dir_owned == 0);

    /* R1 only → RIGHT DOWN */
    f.buttons = AOD_BTN_R1;
    assert(do_step(&s, &f, &c, &ts));
    assert(count_ev(&c, AOD_INPUT_SOURCE_RIGHT, AOD_ACTION_DOWN) == 1u);
    assert(fabsf(c.ev[0].x - DIR_RIGHT_X) < 0.01f);
    assert(s.dir_owned == 1);

    /* Neither → RIGHT UP */
    f.buttons = 0;
    assert(do_step(&s, &f, &c, &ts));
    assert(count_ev(&c, AOD_INPUT_SOURCE_RIGHT, AOD_ACTION_UP) == 1u);
    CHECK("shoulder_left_to_right", s.dir_owned == 0);
}

/* ══════════════════════════════════════════════════════════════════════════
 * Rear fresh central contact (REAR mode): contact in left/right zone
 * emits direction; contact outside zone (outer grip) is ignored.
 * ══════════════════════════════════════════════════════════════════════════ */

static void test_rear_fresh_central(void) {
    aod_input_state s;
    assert(aod_input_init(&s, AOD_INPUT_REAR, W, H));
    aod_input_frame f; cap_t c; cap_init(&c); uint64_t ts = 0u;

    mk_frame(&f, 0);
    assert(do_step(&s, &f, &c, &ts)); /* rearm */

    /* Rear contact in RIGHT zone: nx = 1200/1919≈0.625 ∈ [0.55,0.75],
     * ny = (498-108)/781≈0.499 ∈ [0.25,0.75] → side=+1 */
    f.rear_contacts[0].id = 10;
    f.rear_contacts[0].x  = 1200.0f; /* nx≈0.625 */
    f.rear_contacts[0].y  = 498.0f;  /* ny≈0.499 */
    f.rear_count = 1;
    assert(do_step(&s, &f, &c, &ts));
    /* On first appearance the contact is added to tracker; direction emitted */
    assert(count_ev(&c, AOD_INPUT_SOURCE_RIGHT, AOD_ACTION_DOWN) == 1u);
    assert(s.dir_owned == 1);

    /* Contact released → RIGHT UP */
    f.rear_count = 0;
    assert(do_step(&s, &f, &c, &ts));
    assert(count_ev(&c, AOD_INPUT_SOURCE_RIGHT, AOD_ACTION_UP) == 1u);
    CHECK("rear_fresh_central", s.dir_owned == 0);
}

/* ══════════════════════════════════════════════════════════════════════════
 * Rear contact held at startup (rearm=false): blocked until absent + fresh.
 * Front and pointer still work while rear is blocked.
 * ══════════════════════════════════════════════════════════════════════════ */

static void test_rear_held_at_startup(void) {
    aod_input_state s;
    assert(aod_input_init(&s, AOD_INPUT_REAR, W, H));
    aod_input_frame f; cap_t c; cap_init(&c); uint64_t ts = 0u;

    mk_frame(&f, 0);
    /* Rear contact in right zone present from frame 0 (before rearm).
     * nx=1200/1919≈0.625 ∈ [0.55,0.75], ny≈0.499 ∈ [0.25,0.75] → right zone */
    f.rear_contacts[0].id = 20;
    f.rear_contacts[0].x  = 1200.0f;
    f.rear_contacts[0].y  = 498.0f;
    f.rear_count = 1;
    /* First frame: neutral except for rear contact → rearm becomes true,
     * rear contact is added to tracker as blocked */
    assert(do_step(&s, &f, &c, &ts));
    assert(count_ev(&c, AOD_INPUT_SOURCE_RIGHT, AOD_ACTION_DOWN) == 0u);

    /* Still held: should not emit direction (blocked) */
    assert(do_step(&s, &f, &c, &ts));
    assert(count_ev(&c, AOD_INPUT_SOURCE_RIGHT, AOD_ACTION_DOWN) == 0u);

    /* Release rear contact */
    f.rear_count = 0;
    assert(do_step(&s, &f, &c, &ts));

    /* Fresh contact in right zone → should emit RIGHT DOWN */
    f.rear_contacts[0].id = 21; /* new id, fresh press */
    f.rear_contacts[0].x  = 1200.0f; /* nx≈0.625 ∈ [0.55,0.75] */
    f.rear_contacts[0].y  = 498.0f;
    f.rear_count = 1;
    assert(do_step(&s, &f, &c, &ts));
    CHECK("rear_held_at_startup", count_ev(&c, AOD_INPUT_SOURCE_RIGHT, AOD_ACTION_DOWN) == 1u);
}

/* ══════════════════════════════════════════════════════════════════════════
 * BOTH mode, same-side OR: shoulder L1 + rear left: release L1 while rear
 * still held → no UP (still active via rear); release rear → LEFT UP.
 * ══════════════════════════════════════════════════════════════════════════ */

static void test_both_same_side_or(void) {
    aod_input_state s;
    assert(aod_input_init(&s, AOD_INPUT_BOTH, W, H));
    aod_input_frame f; cap_t c; cap_init(&c); uint64_t ts = 0u;

    mk_frame(&f, 0);
    assert(do_step(&s, &f, &c, &ts)); /* rearm */

    /* L1 + rear left contact active */
    /* Rear left zone: nx in [0.25,0.45], ny in [0.25,0.75]
     * x = 0.35*1919 ≈ 671.65, y = 0.5*(889-108)+108 = 498.5 */
    f.buttons = AOD_BTN_L1;
    f.rear_contacts[0].id = 30;
    f.rear_contacts[0].x  = 672.0f;  /* nx ≈ 0.35 */
    f.rear_contacts[0].y  = 498.0f;  /* ny ≈ 0.5  */
    f.rear_count = 1;
    assert(do_step(&s, &f, &c, &ts));
    assert(s.dir_owned == -1); /* LEFT owned */

    /* Release L1, keep rear left contact → no UP (rear still holds left) */
    f.buttons = 0;
    assert(do_step(&s, &f, &c, &ts));
    assert(count_ev(&c, AOD_INPUT_SOURCE_LEFT, AOD_ACTION_UP) == 0u);
    assert(s.dir_owned == -1);

    /* Release rear contact → LEFT UP */
    f.rear_count = 0;
    assert(do_step(&s, &f, &c, &ts));
    CHECK("both_same_side_or", s.dir_owned == 0
          && count_ev(&c, AOD_INPUT_SOURCE_LEFT, AOD_ACTION_UP) == 1u);
}

/* ══════════════════════════════════════════════════════════════════════════
 * Runtime mode switch: set_mode cancels pending direction UP; inputs held
 * through mode switch must re-arm before accepting.
 * ══════════════════════════════════════════════════════════════════════════ */

static void test_mode_switch_cancels_direction(void) {
    aod_input_state s;
    assert(aod_input_init(&s, AOD_INPUT_SHOULDERS, W, H));
    aod_input_frame f; cap_t c; cap_init(&c); uint64_t ts = 0u;

    mk_frame(&f, 0);
    assert(do_step(&s, &f, &c, &ts));

    /* L1 → LEFT owned */
    f.buttons = AOD_BTN_L1;
    assert(do_step(&s, &f, &c, &ts) && s.dir_owned == -1);

    /* Switch to REAR mode: must cancel direction (emit LEFT UP) */
    cap_clear(&c);
    bool ok = aod_input_set_mode(&s, AOD_INPUT_REAR, emit_cap, &c);
    assert(ok);
    assert(count_ev(&c, AOD_INPUT_SOURCE_LEFT, AOD_ACTION_UP) == 1u);
    assert(s.dir_owned == 0);
    assert(s.mode == AOD_INPUT_REAR);
    /* rearm=false after mode switch */
    assert(!s.rearm);

    /* Neutral frame → rearm; now in REAR mode L1 should have no effect */
    f.buttons = 0;
    assert(do_step(&s, &f, &c, &ts) && s.rearm);
    f.buttons = AOD_BTN_L1; /* L1 in REAR mode: no shoulder processing */
    assert(do_step(&s, &f, &c, &ts));
    CHECK("mode_switch_cancels_direction", s.dir_owned == 0);
}

/* ══════════════════════════════════════════════════════════════════════════
 * SHOULDERS mode ignores rear invalid data (rear_valid=false).
 * Front panel and pointer still work.
 * ══════════════════════════════════════════════════════════════════════════ */

static void test_shoulders_ignores_rear_invalid(void) {
    aod_input_state s;
    assert(aod_input_init(&s, AOD_INPUT_SHOULDERS, W, H));
    aod_input_frame f; cap_t c; cap_init(&c); uint64_t ts = 0u;

    mk_frame(&f, 0);
    assert(do_step(&s, &f, &c, &ts));

    /* rear_valid=false; shoulder L1 should still work */
    f.rear_valid = false;
    f.rear_count = 1;
    f.rear_contacts[0].id = 50;
    f.rear_contacts[0].x  = 1200.0f;
    f.rear_contacts[0].y  = 498.0f;
    f.buttons = AOD_BTN_L1;
    bool ok = do_step(&s, &f, &c, &ts);
    CHECK("shoulders_ignores_rear_invalid",
          ok && s.dir_owned == -1
          && count_ev(&c, AOD_INPUT_SOURCE_LEFT, AOD_ACTION_DOWN) == 1u);
}

/* ══════════════════════════════════════════════════════════════════════════
 * REAR mode: malformed SDK frame (rear duplicate ID) clears tracker.
 * ══════════════════════════════════════════════════════════════════════════ */

static void test_rear_malformed_duplicate_id(void) {
    aod_input_state s;
    assert(aod_input_init(&s, AOD_INPUT_REAR, W, H));
    aod_input_frame f; cap_t c; cap_init(&c); uint64_t ts = 0u;

    mk_frame(&f, 0);
    assert(do_step(&s, &f, &c, &ts));

    /* First: valid rear contact in right zone → direction acquired */
    f.rear_contacts[0].id = 60;
    f.rear_contacts[0].x  = 1200.0f; /* nx≈0.625 ∈ [0.55,0.75] */
    f.rear_contacts[0].y  = 498.0f;
    f.rear_count = 1;
    assert(do_step(&s, &f, &c, &ts) && s.dir_owned == 1);

    /* Now inject malformed frame: two rear contacts with same id */
    f.rear_contacts[0].id = 61; f.rear_contacts[0].x = 1200.0f; f.rear_contacts[0].y = 498.0f;
    f.rear_contacts[1].id = 61; f.rear_contacts[1].x = 480.0f;  f.rear_contacts[1].y = 300.0f;
    f.rear_count = 2;
    /* update_rear_tracker clears rear_seen_count on duplicate; direction may stay */
    do_step(&s, &f, &c, &ts);
    /* tracker cleared: rear_seen_count should be 0 */
    CHECK("rear_malformed_duplicate_id", s.rear_seen_count == 0u);
}

/* ══════════════════════════════════════════════════════════════════════════
 * Rear contact sliding out of zone becomes blocked; no direction change on
 * re-entry without id absence.
 * ══════════════════════════════════════════════════════════════════════════ */

static void test_rear_slide_out_blocked(void) {
    aod_input_state s;
    assert(aod_input_init(&s, AOD_INPUT_REAR, W, H));
    aod_input_frame f; cap_t c; cap_init(&c); uint64_t ts = 0u;

    mk_frame(&f, 0);
    assert(do_step(&s, &f, &c, &ts));

    /* Contact in right zone: nx=1200/1919≈0.625 ∈ [0.55,0.75] */
    f.rear_contacts[0].id = 70;
    f.rear_contacts[0].x  = 1200.0f;
    f.rear_contacts[0].y  = 498.0f;
    f.rear_count = 1;
    assert(do_step(&s, &f, &c, &ts) && s.dir_owned == 1);

    /* Slide to deadband (nx=0.5, outside both zones) → blocked */
    f.rear_contacts[0].x = 960.0f; /* nx=0.5 */
    assert(do_step(&s, &f, &c, &ts));

    /* Now no rear contacts → direction UP */
    f.rear_count = 0;
    assert(do_step(&s, &f, &c, &ts));

    /* Fresh contact in right zone again → RIGHT DOWN again */
    f.rear_contacts[0].id = 71; /* new id */
    f.rear_contacts[0].x  = 1200.0f; /* nx≈0.625 ∈ [0.55,0.75] */
    f.rear_contacts[0].y  = 498.0f;
    f.rear_count = 1;
    assert(do_step(&s, &f, &c, &ts));
    CHECK("rear_slide_out_blocked", s.dir_owned == 1
          && count_ev(&c, AOD_INPUT_SOURCE_RIGHT, AOD_ACTION_DOWN) == 1u);
}

/* ══════════════════════════════════════════════════════════════════════════
 * M6: Rear outer grip (outside zone on first touch) is blocked even when the
 * same contact later slides into the valid zone.  A fresh press that starts
 * inside the zone is the only path to a direction event.
 * Kills the left-zone x0.25→0.0 mutation: a contact starting at nx≈0.05
 * must NOT emit LEFT DOWN; only after id-absence + fresh re-entry at 0.35.
 * ══════════════════════════════════════════════════════════════════════════ */

static void test_rear_outer_grip_requires_fresh_central(void) {
    aod_input_state s;
    assert(aod_input_init(&s, AOD_INPUT_REAR, W, H));
    aod_input_frame f; cap_t c; cap_init(&c); uint64_t ts = 0u;

    mk_frame(&f, 0);
    assert(do_step(&s, &f, &c, &ts)); /* rearm */

    /* Fresh contact ID=255 at 5% span x: nx≈0.05, outside left zone [0.25,0.45].
     * blocked_arm=false but rear_zone_side returns 0 → added as blocked; no DOWN. */
    f.rear_contacts[0].id = 255;
    f.rear_contacts[0].x  = 0.05f * 1919.0f; /* nx≈0.050, outside [0.25,0.45] */
    f.rear_contacts[0].y  = 498.0f;           /* ny≈0.499, inside  [0.25,0.75] */
    f.rear_count = 1;
    assert(do_step(&s, &f, &c, &ts));
    assert(count_ev(&c, AOD_INPUT_SOURCE_LEFT, AOD_ACTION_DOWN) == 0u);
    assert(s.dir_owned == 0);

    /* Same ID slides to 35% span x (inside left zone): blocked=true → no DOWN. */
    f.rear_contacts[0].x = 0.35f * 1919.0f; /* nx≈0.350 inside [0.25,0.45] */
    assert(do_step(&s, &f, &c, &ts));
    assert(count_ev(&c, AOD_INPUT_SOURCE_LEFT, AOD_ACTION_DOWN) == 0u);
    assert(s.dir_owned == 0);

    /* Contact absent → ID dropped from tracker. */
    f.rear_count = 0;
    assert(do_step(&s, &f, &c, &ts));

    /* Fresh contact ID=255 at 35% span x: z=-1 (left zone), armed → LEFT DOWN.
     * Bridge: queue type=AOD_TOUCH_DOWN(0), valid mapped id, t=1234. */
    f.rear_contacts[0].id = 255;
    f.rear_contacts[0].x  = 0.35f * 1919.0f; /* nx≈0.350 ∈ [0.25,0.45] */
    f.rear_contacts[0].y  = 498.0f;
    f.rear_count = 1;
    assert(do_step(&s, &f, &c, &ts));
    assert(count_ev(&c, AOD_INPUT_SOURCE_LEFT, AOD_ACTION_DOWN) == 1u);
    assert(s.dir_owned == -1);
    const aod_input_event *qe = aod_input_queue_at(&c.q, 0u);
    assert(qe != NULL && qe->kind == AOD_KIND_TOUCH
           && qe->type == AOD_TOUCH_DOWN && qe->id >= 0 && qe->t == 1234LL);

    CHECK("rear_outer_grip_requires_fresh_central", s.dir_owned == -1);
}

/* ══════════════════════════════════════════════════════════════════════════
 * aod_input_queue_emit: verify action conversion and key id mapping.
 * AOD_ACTION_DOWN→type=0, UP→type=1 for keys; START→key_id=1(Menu),
 * CIRCLE→key_id=0(Back).
 * ══════════════════════════════════════════════════════════════════════════ */

static void test_queue_emit_key_mapping(void) {
    aod_input_queue q; aod_input_queue_init(&q);
    aod_event ev;

    /* START DOWN → kind=KEY, type=0 (native DOWN), id=1 (Menu) */
    ev.kind = AOD_EVENT_KEY; ev.source_id = AOD_INPUT_SOURCE_START;
    ev.action = AOD_ACTION_DOWN; ev.x = 0.0f; ev.y = 0.0f;
    bool ok1 = aod_input_queue_emit(&q, &ev, 100LL);
    assert(ok1 && q.count == 1u);
    const aod_input_event *k = aod_input_queue_at(&q, 0u);
    assert(k && k->kind == AOD_KIND_KEY && k->type == 0 && k->id == 1);
    assert(k->t == 100LL);

    /* CIRCLE UP → kind=KEY, type=1 (native UP), id=0 (Back) */
    ev.source_id = AOD_INPUT_SOURCE_CIRCLE; ev.action = AOD_ACTION_UP;
    bool ok2 = aod_input_queue_emit(&q, &ev, 200LL);
    assert(ok2 && q.count == 2u);
    const aod_input_event *k2 = aod_input_queue_at(&q, 1u);
    assert(k2 && k2->kind == AOD_KIND_KEY && k2->type == 1 && k2->id == 0);

    /* KEY MOVE → rejected */
    ev.source_id = AOD_INPUT_SOURCE_START; ev.action = AOD_ACTION_MOVE;
    bool ok3 = aod_input_queue_emit(&q, &ev, 300LL);

    CHECK("queue_emit_key_mapping", !ok3 && q.count == 2u);
}

/* ══════════════════════════════════════════════════════════════════════════
 * aod_input_queue_emit: TOUCH action conversion.
 * AOD_ACTION_DOWN→AOD_TOUCH_DOWN(0), MOVE→1, UP→2 in queue event.
 * ══════════════════════════════════════════════════════════════════════════ */

static void test_queue_emit_touch_action_conversion(void) {
    aod_input_queue q; aod_input_queue_init(&q);
    aod_event ev;
    ev.kind = AOD_EVENT_TOUCH; ev.source_id = 5;
    ev.x = 100.0f; ev.y = 200.0f;

    ev.action = AOD_ACTION_DOWN;
    assert(aod_input_queue_emit(&q, &ev, 1LL));
    const aod_input_event *e0 = aod_input_queue_at(&q, 0u);
    assert(e0 && e0->kind == AOD_KIND_TOUCH && e0->type == AOD_TOUCH_DOWN);

    ev.action = AOD_ACTION_MOVE;
    assert(aod_input_queue_emit(&q, &ev, 2LL));
    const aod_input_event *e1 = aod_input_queue_at(&q, 1u);
    assert(e1 && e1->type == AOD_TOUCH_MOVE);

    ev.action = AOD_ACTION_UP;
    assert(aod_input_queue_emit(&q, &ev, 3LL));
    const aod_input_event *e2 = aod_input_queue_at(&q, 2u);
    assert(e2 && e2->type == AOD_TOUCH_UP);

    CHECK("queue_emit_touch_action_conversion",
          e0->type == 0 && e1->type == 1 && e2->type == 2);
}

/* ══════════════════════════════════════════════════════════════════════════
 * main
 * ══════════════════════════════════════════════════════════════════════════ */

int main(void) {
    test_shoulder_left_to_right();
    test_rear_fresh_central();
    test_rear_held_at_startup();
    test_both_same_side_or();
    test_mode_switch_cancels_direction();
    test_shoulders_ignores_rear_invalid();
    test_rear_malformed_duplicate_id();
    test_rear_slide_out_blocked();
    test_rear_outer_grip_requires_fresh_central();
    test_queue_emit_key_mapping();
    test_queue_emit_touch_action_conversion();
    printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}