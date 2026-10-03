/*
 * tests/input_adapter_test.c
 * AOD input adapter host regression tests.
 * Single TU: stub implementations + composite scenario tests.
 */

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>

/* Always-active CHECK; never NDEBUG-gated; exits suite on failure. */
#define CHECK(cond) \
    do { \
        if (!(cond)) { \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            exit(1); \
        } \
    } while (0)

/* ── SDK stub state ─────────────────────────────────────────────────────── */

#include "psp2/touch.h"
#include "psp2/ctrl.h"
#include "psp2/kernel/processmgr.h"

static SceTouchData  g_front_td;
static SceTouchData  g_rear_td;
static SceCtrlData   g_pad;
static int           g_front_rc = 1;
static int           g_rear_rc  = -1;
static int           g_pad_rc   = 1;
static uint64_t      g_time_us  = 0u;

/* ── SDK stub implementations (CHECKs enforce exact allowed arguments) ── */

int sceTouchSetSamplingState(SceUInt32 port, SceTouchSamplingState state)
{
    CHECK(port == SCE_TOUCH_PORT_FRONT || port == (SceUInt32)SCE_TOUCH_PORT_BACK);
    CHECK(state == SCE_TOUCH_SAMPLING_STATE_STOP ||
          state == SCE_TOUCH_SAMPLING_STATE_START);
    return 0;
}

int sceTouchGetPanelInfo(SceUInt32 port, SceTouchPanelInfo *pi)
{
    CHECK(port == SCE_TOUCH_PORT_FRONT || port == (SceUInt32)SCE_TOUCH_PORT_BACK);
    CHECK(pi != NULL);
    memset(pi, 0, sizeof(*pi));
    pi->minAaX =     0; pi->minAaY =    0;
    pi->maxAaX =  1919; pi->maxAaY = 1087;
    return 0;
}

int sceTouchPeek(SceUInt32 port, SceTouchData *pData, SceUInt32 nBufs)
{
    CHECK(port == SCE_TOUCH_PORT_FRONT || port == (SceUInt32)SCE_TOUCH_PORT_BACK);
    CHECK(pData != NULL);
    CHECK(nBufs == 1u);
    if (port == SCE_TOUCH_PORT_FRONT) {
        *pData = g_front_td;
        return g_front_rc;
    }
    *pData = g_rear_td;
    return g_rear_rc;
}

int sceCtrlSetSamplingModeExt(SceCtrlPadInputMode mode)
{
    CHECK(mode == SCE_CTRL_MODE_ANALOG_WIDE);
    return 0;
}

int sceCtrlPeekBufferPositiveExt2(int port, SceCtrlData *pad_data, int count)
{
    CHECK(port == 0);
    CHECK(pad_data != NULL);
    CHECK(count == 1);
    *pad_data = g_pad;
    return g_pad_rc;
}

SceUInt64 sceKernelGetProcessTimeWide(void)
{
    g_time_us += 10000u;
    return g_time_us;
}

/* ── Queue + emit callback ──────────────────────────────────────────────── */

#include "aod/input_queue.h"
#include "aod/input_vita.h"

static aod_input_queue g_queue;

static bool s_emit(void *userdata, const aod_event *event)
{
    aod_input_queue *q = (aod_input_queue *)userdata;
    int64_t ts_ms = (int64_t)(g_time_us / 1000u);
    CHECK(ts_ms >= 0);
    return aod_input_queue_emit(q, event, ts_ms);
}

/* ── Helpers ────────────────────────────────────────────────────────────── */

static void set_buttons(unsigned int btns)
{
    memset(&g_pad, 0, sizeof(g_pad));
    g_pad.buttons = btns;
    g_pad.lx = 128u;
    g_pad.ly = 128u;
}

static void clear_queue_preserve_map(void)
{
    aod_input_queue_clear(&g_queue);
}

static void reset_fixtures(void)
{
    memset(&g_front_td, 0, sizeof(g_front_td));
    memset(&g_rear_td,  0, sizeof(g_rear_td));
    set_buttons(0);
    g_front_rc = 1;
    g_rear_rc  = -1;
    g_pad_rc   = 1;
    g_time_us  = 0u;
}

/*
 * expect_touch: assert exactly one event in the queue with kind TOUCH,
 * the given type (AOD_TOUCH_DOWN/MOVE/UP), and the given pointer id.
 */
static void expect_touch(int type, int id)
{
    CHECK(aod_input_queue_count(&g_queue) == 1u);
    const aod_input_event *ev = aod_input_queue_at(&g_queue, 0u);
    CHECK(ev != NULL);
    CHECK(ev->kind == AOD_KIND_TOUCH);
    CHECK(ev->type == type);
    CHECK(ev->id   == id);
}

/* ── Test 1: original composite scenario ───────────────────────────────── */

static void test_adapter_scenario(void)
{
    aod_input_queue_init(&g_queue);
    reset_fixtures();

    aod_input_vita v;
    memset(&v, 0, sizeof(v));
    bool ok = aod_input_vita_init(&v, AOD_INPUT_SHOULDERS, 960.0f, 544.0f,
                                  s_emit, &g_queue);
    CHECK(ok);
    CHECK(v.initialized);
    CHECK(v.front_ready);
    CHECK(v.pad_ready);
    /* rear_ready: stub sampling/panelinfo succeed; rear_rc=-1 only affects poll. */
    CHECK(v.rear_ready);

    /* Neutral → queue empty. */
    set_buttons(0);
    CHECK(aod_input_vita_poll(&v, true));
    CHECK(aod_input_queue_count(&g_queue) == 0u);

    /* CROSS press → one AndroidDOWN id=0. */
    set_buttons(SCE_CTRL_CROSS);
    CHECK(aod_input_vita_poll(&v, true));
    expect_touch(AOD_TOUCH_DOWN, 0);
    CHECK(v.state.cursor.active);

    /* pad_rc=2 (malformed) → poll false; cursor UP emitted; ownership gone. */
    clear_queue_preserve_map();
    g_pad_rc = 2;
    CHECK(!aod_input_vita_poll(&v, true));
    CHECK(!v.state.cursor.active);
    expect_touch(AOD_TOUCH_UP, 0);

    /* Restore pad_rc=1; CROSS still held → no new DOWN until released. */
    clear_queue_preserve_map();
    g_pad_rc = 1;
    CHECK(aod_input_vita_poll(&v, true));
    CHECK(aod_input_queue_count(&g_queue) == 0u);

    /* Release CROSS → rearms. */
    set_buttons(0);
    CHECK(aod_input_vita_poll(&v, true));
    CHECK(aod_input_queue_count(&g_queue) == 0u);

    /* Fresh CROSS press → new DOWN id=0. */
    set_buttons(SCE_CTRL_CROSS);
    CHECK(aod_input_vita_poll(&v, true));
    expect_touch(AOD_TOUCH_DOWN, 0);

    /* Cancel → UP emitted, no ownership leak. */
    clear_queue_preserve_map();
    CHECK(aod_input_vita_cancel(&v));
    expect_touch(AOD_TOUCH_UP, 0);
    CHECK(!v.state.cursor.active);
}

/* ── Test 2: front cap-boundary composite ───────────────────────────────── */

static void test_front_cap_boundary(void)
{
    aod_input_queue_init(&g_queue);
    reset_fixtures();
    aod_input_vita v;
    memset(&v, 0, sizeof(v));
    CHECK(aod_input_vita_init(&v, AOD_INPUT_SHOULDERS, 960.0f, 544.0f,
                              s_emit, &g_queue));
    /* Arm via neutral poll. */
    set_buttons(0);
    CHECK(aod_input_vita_poll(&v, true));
    CHECK(aod_input_queue_count(&g_queue) == 0u);
    /* 6 distinct SDK contacts IDs 1..6 in front panel rect 0..1919x0..1087. */
    g_front_td.reportNum = 6u;
    for (unsigned i = 0u; i < 6u; i++) {
        g_front_td.report[i].id = (SceUInt8)(i + 1u);
        g_front_td.report[i].x  = (SceInt16)(100 + (int)(i * 200));
        g_front_td.report[i].y  = (SceInt16)300;
    }
    /* rc=1, count=6 within cap → 6 AndroidDOWN id0..5, ownership=6. */
    CHECK(aod_input_vita_poll(&v, true));
    CHECK(aod_input_queue_count(&g_queue) == 6u);
    for (unsigned k = 0u; k < 6u; k++) {
        const aod_input_event *ev = aod_input_queue_at(&g_queue, k);
        CHECK(ev != NULL);
        CHECK(ev->kind == AOD_KIND_TOUCH);
        CHECK(ev->type == AOD_TOUCH_DOWN);
        CHECK(ev->id   == (int)k);
    }
    CHECK(v.state.front_owned_count == 6u);
    /* reportNum=7 (> cap=6, fits array[8]) → s_copy_front rejects →
     * front_valid=false → validate_frame fails → cancel → 6 UPs. */
    clear_queue_preserve_map();
    g_front_td.reportNum = 7u;
    CHECK(!aod_input_vita_poll(&v, true));
    CHECK(aod_input_queue_count(&g_queue) == 6u);
    for (unsigned k = 0u; k < 6u; k++) {
        const aod_input_event *ev = aod_input_queue_at(&g_queue, k);
        CHECK(ev != NULL);
        CHECK(ev->kind == AOD_KIND_TOUCH);
        CHECK(ev->type == AOD_TOUCH_UP);
        CHECK(ev->id   == (int)k);
    }
    CHECK(v.state.front_owned_count == 0u);
    /* Neutral clean. */
    clear_queue_preserve_map();
    g_front_td.reportNum = 0u;
    CHECK(aod_input_vita_cancel(&v));
}

/* ── Test 3: rear active BOTH/REAR direction ────────────────────────────── */

static void test_rear_direction(void)
{
    aod_input_queue_init(&g_queue);
    reset_fixtures();
    g_rear_rc = 1;
    g_rear_td.reportNum = 0u;
    aod_input_vita v;
    memset(&v, 0, sizeof(v));
    CHECK(aod_input_vita_init(&v, AOD_INPUT_BOTH, 960.0f, 544.0f,
                              s_emit, &g_queue));
    CHECK(v.rear_ready);
    /* Neutral arm: no front contacts, no buttons, stick centre. */
    set_buttons(0);
    CHECK(aod_input_vita_poll(&v, true));
    CHECK(aod_input_queue_count(&g_queue) == 0u);
    /* 4 rear contacts IDs 20..23 in left zone:
     * nx=600/1919≈0.313 ∈[0.25,0.45], ny=500/1087≈0.460 ∈[0.25,0.75]. */
    g_rear_td.reportNum = 4u;
    for (unsigned i = 0u; i < 4u; i++) {
        g_rear_td.report[i].id = (SceUInt8)(20u + i);
        g_rear_td.report[i].x  = (SceInt16)600;
        g_rear_td.report[i].y  = (SceInt16)500;
    }
    /* All 4 armed in left zone → aggregate LEFT DOWN (source 513 → id 0). */
    CHECK(aod_input_vita_poll(&v, true));
    expect_touch(AOD_TOUCH_DOWN, 0);
    CHECK(v.state.dir_owned == -1);
    /* reportNum=5 (> rear cap=4): s_copy_rear rejects → rear_valid=false →
     * rear_seen cleared → no rear sources → direction releases UP.
     * Front still valid; poll returns true (rear-invalid ≠ whole-frame-invalid). */
    clear_queue_preserve_map();
    g_rear_td.reportNum = 5u;
    CHECK(aod_input_vita_poll(&v, true));
    expect_touch(AOD_TOUCH_UP, 0);
    CHECK(v.state.dir_owned == 0);
    /* Neutral clean. */
    clear_queue_preserve_map();
    g_rear_td.reportNum = 0u;
    CHECK(aod_input_vita_cancel(&v));
}

/* ── Bad-pad-rc helpers (group 1): rc ∈ {0, -1, 2} ─────────────────────── */

/*
 * run_bad_pad_rc: exercise one bad g_pad_rc value.
 * Fresh init, neutral arm, CROSS DOWN → queue one event.
 * Set g_pad_rc=bad_rc → poll false, one UP id=0, cursor gone.
 * Restore rc=1, CROSS still held → no DOWN yet.
 * Release + fresh CROSS → new DOWN; cancel → UP; cursor clear.
 */
static void run_bad_pad_rc(int bad_rc)
{
    aod_input_queue_init(&g_queue);
    reset_fixtures();

    aod_input_vita v;
    memset(&v, 0, sizeof(v));
    CHECK(aod_input_vita_init(&v, AOD_INPUT_SHOULDERS, 960.0f, 544.0f,
                              s_emit, &g_queue));
    CHECK(v.initialized);

    /* Neutral arm. */
    set_buttons(0);
    CHECK(aod_input_vita_poll(&v, true));
    CHECK(aod_input_queue_count(&g_queue) == 0u);

    /* CROSS → DOWN id=0. */
    set_buttons(SCE_CTRL_CROSS);
    CHECK(aod_input_vita_poll(&v, true));
    expect_touch(AOD_TOUCH_DOWN, 0);
    CHECK(v.state.cursor.active);

    /* Bad pad rc → poll false, UP id=0, cursor released. */
    clear_queue_preserve_map();
    g_pad_rc = bad_rc;
    CHECK(!aod_input_vita_poll(&v, true));
    CHECK(!v.state.cursor.active);
    expect_touch(AOD_TOUCH_UP, 0);

    /* Restore rc=1; CROSS still held → no new DOWN yet. */
    clear_queue_preserve_map();
    g_pad_rc = 1;
    CHECK(aod_input_vita_poll(&v, true));
    CHECK(aod_input_queue_count(&g_queue) == 0u);

    /* Release CROSS → rearm. */
    set_buttons(0);
    CHECK(aod_input_vita_poll(&v, true));
    CHECK(aod_input_queue_count(&g_queue) == 0u);

    /* Fresh CROSS → new DOWN id=0. */
    set_buttons(SCE_CTRL_CROSS);
    CHECK(aod_input_vita_poll(&v, true));
    expect_touch(AOD_TOUCH_DOWN, 0);

    /* Cancel → UP, no leak. */
    clear_queue_preserve_map();
    CHECK(aod_input_vita_cancel(&v));
    expect_touch(AOD_TOUCH_UP, 0);
    CHECK(!v.state.cursor.active);
}

static void test_bad_pad_rc_zero(void)  { run_bad_pad_rc(0);  }
static void test_bad_pad_rc_neg1(void)  { run_bad_pad_rc(-1); }
/* rc=2 is the original scenario (test_adapter_scenario); exercise it here too. */
static void test_bad_pad_rc_two(void)   { run_bad_pad_rc(2);  }

/* ── Bad-front-rc helpers (group 2): rc ∈ {0, -1, 2} ───────────────────── */

/*
 * run_bad_front_rc: owned Cross cursor + bad front read.
 * Fresh init, CROSS DOWN owning cursor, set g_front_rc=bad_rc.
 * Poll → malformed front → false poll; exactly one UP id=0; cursor gone.
 * Restore g_front_rc=1, neutral, fresh CROSS → rearm DOWN; cancel → UP.
 */
static void run_bad_front_rc(int bad_rc)
{
    aod_input_queue_init(&g_queue);
    reset_fixtures();

    aod_input_vita v;
    memset(&v, 0, sizeof(v));
    CHECK(aod_input_vita_init(&v, AOD_INPUT_SHOULDERS, 960.0f, 544.0f,
                              s_emit, &g_queue));

    /* Neutral arm. */
    set_buttons(0);
    CHECK(aod_input_vita_poll(&v, true));
    CHECK(aod_input_queue_count(&g_queue) == 0u);

    /* CROSS → DOWN id=0, cursor owned. */
    set_buttons(SCE_CTRL_CROSS);
    CHECK(aod_input_vita_poll(&v, true));
    expect_touch(AOD_TOUCH_DOWN, 0);
    CHECK(v.state.cursor.active);

    /* Bad front rc while pad & Cross still held. */
    clear_queue_preserve_map();
    g_front_rc = bad_rc;
    CHECK(!aod_input_vita_poll(&v, true));
    CHECK(!v.state.cursor.active);
    expect_touch(AOD_TOUCH_UP, 0);

    /* Restore front rc; neutral + fresh CROSS → rearm. */
    clear_queue_preserve_map();
    g_front_rc = 1;
    set_buttons(0);
    CHECK(aod_input_vita_poll(&v, true));
    CHECK(aod_input_queue_count(&g_queue) == 0u);

    set_buttons(SCE_CTRL_CROSS);
    CHECK(aod_input_vita_poll(&v, true));
    expect_touch(AOD_TOUCH_DOWN, 0);

    clear_queue_preserve_map();
    CHECK(aod_input_vita_cancel(&v));
    expect_touch(AOD_TOUCH_UP, 0);
    CHECK(!v.state.cursor.active);
}

static void test_bad_front_rc_zero(void) { run_bad_front_rc(0);  }
static void test_bad_front_rc_neg1(void) { run_bad_front_rc(-1); }
static void test_bad_front_rc_two(void)  { run_bad_front_rc(2);  }

/* ── Bad-rear-rc helpers (group 3): rc ∈ {0, -1, 2} ────────────────────── */

/*
 * run_bad_rear_rc: BOTH mode; rear contact id=30 (x=600,y=500) → LEFT DOWN.
 * Then set g_rear_rc=bad_rc; poll may return true (per contract: rear-invalid
 * ≠ whole-frame-invalid); direction UP id=0 must appear; dir_owned cleared.
 * Keep bad rear rc; fresh CROSS → cursor DOWN (front/pad still valid).
 * Cancel releases cursor; no assertions on native hardware paths.
 */
static void run_bad_rear_rc(int bad_rc)
{
    aod_input_queue_init(&g_queue);
    reset_fixtures();
    g_rear_rc = 1;
    g_rear_td.reportNum = 0u;

    aod_input_vita v;
    memset(&v, 0, sizeof(v));
    CHECK(aod_input_vita_init(&v, AOD_INPUT_BOTH, 960.0f, 544.0f,
                              s_emit, &g_queue));
    CHECK(v.rear_ready);

    /* Neutral arm. */
    set_buttons(0);
    CHECK(aod_input_vita_poll(&v, true));
    CHECK(aod_input_queue_count(&g_queue) == 0u);

    /* One rear contact: id=30, left zone → LEFT DOWN id=0. */
    g_rear_td.reportNum = 1u;
    g_rear_td.report[0].id = 30u;
    g_rear_td.report[0].x  = (SceInt16)600;
    g_rear_td.report[0].y  = (SceInt16)500;
    CHECK(aod_input_vita_poll(&v, true));
    expect_touch(AOD_TOUCH_DOWN, 0);
    CHECK(v.state.dir_owned == -1);

    /* Set bad rear rc; held contact id=30 x=600 y=500 count=1 still in stub. */
    clear_queue_preserve_map();
    g_rear_rc = bad_rc;

    /* Invalid rear read still has held payload; globally valid front/pad gives
     * true: rear-invalid alone doesn't fail whole frame, direction UP released. */
    CHECK(aod_input_vita_poll(&v, true));
    expect_touch(AOD_TOUCH_UP, 0);
    CHECK(v.state.dir_owned == 0);

    /* With bad rear rc still set, front/pad remain valid.
     * Fresh CROSS → cursor DOWN id=0. */
    clear_queue_preserve_map();
    set_buttons(SCE_CTRL_CROSS);
    CHECK(aod_input_vita_poll(&v, true));
    expect_touch(AOD_TOUCH_DOWN, 0);
    CHECK(v.state.cursor.active);

    /* Cancel → UP id=0. */
    clear_queue_preserve_map();
    CHECK(aod_input_vita_cancel(&v));
    expect_touch(AOD_TOUCH_UP, 0);
    CHECK(!v.state.cursor.active);
}

static void test_bad_rear_rc_zero(void) { run_bad_rear_rc(0);  }
static void test_bad_rear_rc_neg1(void) { run_bad_rear_rc(-1); }
static void test_bad_rear_rc_two(void)  { run_bad_rear_rc(2);  }

/* ── Entry point ────────────────────────────────────────────────────────── */

int main(void)
{
    test_adapter_scenario();
    test_front_cap_boundary();
    test_rear_direction();

    /* Group 1: bad pad SDK rc. */
    test_bad_pad_rc_zero();
    test_bad_pad_rc_neg1();
    test_bad_pad_rc_two();

    /* Group 2: bad front SDK rc. */
    test_bad_front_rc_zero();
    test_bad_front_rc_neg1();
    test_bad_front_rc_two();

    /* Group 3: bad rear SDK rc. */
    test_bad_rear_rc_zero();
    test_bad_rear_rc_neg1();
    test_bad_rear_rc_two();

    printf("PASS\n");
    return 0;
}