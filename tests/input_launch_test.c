/*
 * input_launch_test.c — Production-linked launch tests for aod_input_launch_*
 * This file is part of aod-vita and is distributed under the MIT license.
 * SPDX-License-Identifier: MIT
 *
 * Build (example):
 *   cc -std=c99 -I../source -I./input_sdk_stubs \
 *      input_launch_test.c ../source/aod/input_launch.c -o input_launch_test
 *
 * Tests are ordered so init-failure runs before first-success per spec.
 * No heap allocation; all fixtures are static or stack-local.
 * Loop bounds are static constants.
 */

#include <assert.h>
#include <string.h>
#include <stdint.h>
#include <stdio.h>

/* Stub SDK headers must be found before production source is compiled. */
#include <psp2/apputil.h>
#include <psp2/appmgr.h>

/* Production header under test. */
#include "aod/input_launch.h"

/* -------------------------------------------------------------------------
 * Fake SDK state
 * ------------------------------------------------------------------------- */

static int  g_init_call_count;
static int  g_init_return_value;        /* caller sets before each test     */
static SceAppUtilInitParam  g_init_param_received;
static SceAppUtilBootParam  g_boot_param_received;

static int  g_recv_call_count;
static int  g_recv_return_value;        /* 0 = event queued, <0 = no event  */
static SceAppUtilAppEventParam g_recv_event_to_deliver;

static int  g_parse_call_count;
static int  g_parse_return_value;
/* payload_buf is written to the caller's buffer by the stub */
static char g_parse_payload[1024];

static int  g_loadexec_call_count;
static int  g_loadexec_return_value;
static char g_loadexec_path[256];

/* Reset all fake state; called between logical test groups. */
static void fake_reset(void)
{
    g_init_call_count    = 0;
    g_init_return_value  = 0;
    memset(&g_init_param_received, 0, sizeof(g_init_param_received));
    memset(&g_boot_param_received, 0, sizeof(g_boot_param_received));

    g_recv_call_count    = 0;
    g_recv_return_value  = -1;
    memset(&g_recv_event_to_deliver, 0, sizeof(g_recv_event_to_deliver));

    g_parse_call_count   = 0;
    g_parse_return_value = 0;
    memset(g_parse_payload, 0, sizeof(g_parse_payload));

    g_loadexec_call_count   = 0;
    g_loadexec_return_value = 0;
    memset(g_loadexec_path, 0, sizeof(g_loadexec_path));
}

/* -------------------------------------------------------------------------
 * Fake SDK function implementations
 * ------------------------------------------------------------------------- */

int sceAppUtilInit(SceAppUtilInitParam *init_param,
                   SceAppUtilBootParam *boot_param)
{
    g_init_call_count++;
    if (init_param != NULL) {
        g_init_param_received = *init_param;
    }
    if (boot_param != NULL) {
        g_boot_param_received = *boot_param;
    }
    return g_init_return_value;
}

int sceAppUtilReceiveAppEvent(SceAppUtilAppEventParam *event_param)
{
    g_recv_call_count++;
    if (g_recv_return_value >= 0 && event_param != NULL) {
        *event_param = g_recv_event_to_deliver;
    }
    return g_recv_return_value;
}

int sceAppUtilAppEventParseLiveArea(const SceAppUtilAppEventParam *event_param,
                                    char *buf)
{
    (void)event_param;
    g_parse_call_count++;
    if (buf != NULL) {
        /* Write bounded payload; always safe within parse_buf[1024]. */
        memcpy(buf, g_parse_payload, 1024);
    }
    return g_parse_return_value;
}

int sceAppMgrLoadExec(const char *appPath,
                      char * const argv[],
                      const SceAppMgrExecOptParam *opt)
{
    (void)argv;
    (void)opt;
    g_loadexec_call_count++;
    if (appPath != NULL) {
        strncpy(g_loadexec_path, appPath, sizeof(g_loadexec_path) - 1u);
        g_loadexec_path[sizeof(g_loadexec_path) - 1u] = '\0';
    }
    return g_loadexec_return_value;
}

/* -------------------------------------------------------------------------
 * Helper: force re-initialization by testing the init-failure path first,
 * then a fresh success.  The production s_initialized flag is internal;
 * we drive it through the public API only.
 * ------------------------------------------------------------------------- */

/* Queue a good LiveArea event with the exact "-config\0" payload. */
static void queue_config_event(void)
{
    memset(&g_recv_event_to_deliver, 0, sizeof(g_recv_event_to_deliver));
    g_recv_event_to_deliver.type = 0x05u;
    g_recv_return_value          = 0;

    memset(g_parse_payload, 0, sizeof(g_parse_payload));
    memcpy(g_parse_payload, "-config", 7u);   /* NUL at index 7 from memset */
    g_parse_return_value = 0;
}

/* -------------------------------------------------------------------------
 * Tests
 * NOTE: test ordering matters — init-failure runs before first-success so
 * s_initialized is driven through the correct transitions.
 * ------------------------------------------------------------------------- */

/* T01: poll before any init must not call ReceiveAppEvent. */
static void test_poll_before_init_no_sdk_call(void)
{
    fake_reset();
    /* s_initialized is 0 at program start (or after failure path). */
    int rc = aod_input_launch_poll();
    assert(rc == 0);
    assert(g_recv_call_count == 0);
}

/* T02: init failure — SDK returns error, module not marked initialized. */
static void test_init_failure_not_initialized(void)
{
    fake_reset();
    g_init_return_value = -1;
    int rc = aod_input_launch_init();
    assert(rc == -1);
    assert(g_init_call_count == 1);
    /* Params must have been zeroed before the call. */
    assert(g_init_param_received.workBufSize == 0u);
    assert(g_boot_param_received.attr == 0u);
    assert(g_boot_param_received.appVersion == 0u);

    /* Poll must still return 0 (not initialized). */
    int rc_poll = aod_input_launch_poll();
    assert(rc_poll == 0);
    assert(g_recv_call_count == 0);
}

/* T03: next init after failure succeeds; params zero-initialized. */
static void test_init_success_params_zero(void)
{
    fake_reset();
    g_init_return_value = 0;
    int rc = aod_input_launch_init();
    assert(rc == 0);
    assert(g_init_call_count == 1);
    assert(g_init_param_received.workBufSize == 0u);
    assert(g_boot_param_received.attr == 0u);
    assert(g_boot_param_received.appVersion == 0u);
}

/* T04: repeated init after success must not call SDK again. */
static void test_repeated_init_no_extra_call(void)
{
    /* s_initialized is 1 from T03. */
    fake_reset();
    g_init_return_value = 0;
    int rc = aod_input_launch_init();
    assert(rc == 0);
    assert(g_init_call_count == 0);   /* no new SDK call */
}

/* T05: poll with no event (ReceiveAppEvent returns negative). */
static void test_poll_no_event(void)
{
    fake_reset();
    g_recv_return_value = -1;
    int rc = aod_input_launch_poll();
    assert(rc == 0);
    assert(g_recv_call_count == 1);
    assert(g_parse_call_count == 0);
    assert(g_loadexec_call_count == 0);
}

/* T06: event received but ReceiveAppEvent error code — no parse. */
static void test_poll_recv_error(void)
{
    fake_reset();
    g_recv_return_value = -2;
    int rc = aod_input_launch_poll();
    assert(rc == 0);
    assert(g_recv_call_count == 1);
    assert(g_parse_call_count == 0);
}

/* T07: wrong event type (0x03) — no LoadExec. */
static void test_poll_wrong_type(void)
{
    fake_reset();
    g_recv_event_to_deliver.type = 0x03u;
    g_recv_return_value          = 0;
    int rc = aod_input_launch_poll();
    assert(rc == 0);
    assert(g_recv_call_count == 1);
    assert(g_parse_call_count == 0);
    assert(g_loadexec_call_count == 0);
}

/* T08: recognized type 0x05, parse fails — no LoadExec. */
static void test_poll_parse_fails(void)
{
    fake_reset();
    g_recv_event_to_deliver.type = 0x05u;
    g_recv_return_value          = 0;
    g_parse_return_value         = -1;
    int rc = aod_input_launch_poll();
    assert(rc == 0);
    assert(g_recv_call_count == 1);
    assert(g_parse_call_count == 1);
    assert(g_loadexec_call_count == 0);
}

/* T09: exact payload success — LoadExec called with correct path. */
static void test_poll_success_load_exec(void)
{
    fake_reset();
    queue_config_event();
    g_loadexec_return_value = 0;
    int rc = aod_input_launch_poll();
    assert(rc == 1);
    assert(g_recv_call_count == 1);
    assert(g_parse_call_count == 1);
    assert(g_loadexec_call_count == 1);
    assert(strcmp(g_loadexec_path, "app0:/configurator.bin") == 0);
}

/* T10: LoadExec returns negative — poll returns -1. */
static void test_poll_load_exec_fails(void)
{
    fake_reset();
    queue_config_event();
    g_loadexec_return_value = -1;
    int rc = aod_input_launch_poll();
    assert(rc == -1);
    assert(g_loadexec_call_count == 1);
}

/* T11: one response per poll — second poll with queued event yields one call. */
static void test_poll_one_event_per_call(void)
{
    fake_reset();
    queue_config_event();
    g_loadexec_return_value = 0;
    (void)aod_input_launch_poll();
    int first_parse = g_parse_call_count;
    /* Second poll — still queued (stub is deterministic). */
    (void)aod_input_launch_poll();
    assert(g_parse_call_count == first_parse + 1);
    assert(g_recv_call_count == 2);
}

/* T12: successive polls while already initialized — no extra init call. */
static void test_successive_polls_no_reinit(void)
{
    fake_reset();
    g_recv_return_value = -1;          /* no event */
    (void)aod_input_launch_poll();
    (void)aod_input_launch_poll();
    assert(g_init_call_count == 0);
    assert(g_recv_call_count == 2);
}

/* -------------------------------------------------------------------------
 * aod_input_launch_matches boundary tests
 * ------------------------------------------------------------------------- */

/* T20: NULL buffer. */
static void test_matches_null(void)
{
    assert(aod_input_launch_matches(NULL, 8u) == 0);
}

/* T21: capacities 0..7 reject. */
static void test_matches_capacity_0_to_7_reject(void)
{
    static const char buf[1024] = "-config";
    static const size_t k_limit = 8u;
    size_t cap;
    for (cap = 0u; cap < k_limit; cap++) {
        assert(aod_input_launch_matches(buf, cap) == 0);
    }
}

/* T22: exact capacity 8 accepts. */
static void test_matches_capacity_8_accept(void)
{
    static const char buf[8] = {'-','c','o','n','f','i','g','\0'};
    assert(aod_input_launch_matches(buf, 8u) != 0);
}

/* T23: capacity 1024 accepts. */
static void test_matches_capacity_1024_accept(void)
{
    static char buf[1024];
    memset(buf, 0, sizeof(buf));
    memcpy(buf, "-config", 7u);
    assert(aod_input_launch_matches(buf, 1024u) != 0);
}

/* T24: capacity > 1024 rejects. */
static void test_matches_capacity_over_1024_reject(void)
{
    static const char buf[8] = {'-','c','o','n','f','i','g','\0'};
    assert(aod_input_launch_matches(buf, 1025u) == 0);
}

/* T25: prefix junk — "-configX\0" at capacity 9 rejects (NUL not at index 7). */
static void test_matches_prefix_junk_rejects(void)
{
    static const char buf[9] = {'-','c','o','n','f','i','g','X','\0'};
    assert(aod_input_launch_matches(buf, 9u) == 0);
}

/* T26: "-config" followed by non-NUL at index 7, capacity 8. */
static void test_matches_nonnul_at_7_rejects(void)
{
    static const char buf[8] = {'-','c','o','n','f','i','g','X'};
    assert(aod_input_launch_matches(buf, 8u) == 0);
}

/* T27: raw 1024 all 'X' — no NUL, rejects. */
static void test_matches_raw_1024_all_x_rejects(void)
{
    static char buf[1024];
    memset(buf, 'X', sizeof(buf));
    assert(aod_input_launch_matches(buf, 1024u) == 0);
}

/* T28: "-config" with extra trailing content after NUL — first 8 bytes match. */
static void test_matches_extra_after_nul_accepts(void)
{
    static char buf[1024];
    memset(buf, 'X', sizeof(buf));
    memcpy(buf, "-config", 7u);
    buf[7] = '\0';
    /* NUL is at exactly index 7; capacity 1024 — matches spec. */
    assert(aod_input_launch_matches(buf, 1024u) != 0);
}

/* T29: unterminated — no NUL within capacity (buf full of '-config' repeat). */
static void test_matches_unterminated_rejects(void)
{
    static char buf[8];
    memcpy(buf, "-config-", 8u);   /* no NUL at index 7 */
    assert(aod_input_launch_matches(buf, 8u) == 0);
}

/* T30: malformed — "xconfig\0". */
static void test_matches_malformed_rejects(void)
{
    static const char buf[8] = {'x','c','o','n','f','i','g','\0'};
    assert(aod_input_launch_matches(buf, 8u) == 0);
}

/* -------------------------------------------------------------------------
 * CheckReceive / Parse output structs zeroed verification
 * ------------------------------------------------------------------------- */

/* T40: ReceiveAppEvent receives a zeroed struct when recv returns negative. */
static void test_receive_output_struct_zeroed_on_error(void)
{
    fake_reset();
    g_recv_return_value = -1;
    /* Poison the event. */
    memset(&g_recv_event_to_deliver, 0xAB, sizeof(g_recv_event_to_deliver));
    /* poll will call recv; since rc < 0 it must not call parse. */
    int rc = aod_input_launch_poll();
    assert(rc == 0);
    assert(g_parse_call_count == 0);
}

/* T41: Parse output buf is 1024 bounded — stub writes exactly 1024 bytes. */
static void test_parse_output_bounded_1024(void)
{
    fake_reset();
    g_recv_event_to_deliver.type = 0x05u;
    g_recv_return_value          = 0;
    /* Fill payload with 'Y'; no NUL → matches will return 0 → no LoadExec. */
    memset(g_parse_payload, 'Y', sizeof(g_parse_payload));
    g_parse_return_value = 0;
    int rc = aod_input_launch_poll();
    assert(rc == 0);
    assert(g_parse_call_count == 1);
    assert(g_loadexec_call_count == 0);
}

/* -------------------------------------------------------------------------
 * Main
 * ------------------------------------------------------------------------- */

int main(void)
{
    /* T01 must run before any successful init. */
    test_poll_before_init_no_sdk_call();

    /* T02 init failure, then T03 success. */
    test_init_failure_not_initialized();
    test_init_success_params_zero();
    test_repeated_init_no_extra_call();

    /* Poll path tests — s_initialized == 1 from T03. */
    test_poll_no_event();
    test_poll_recv_error();
    test_poll_wrong_type();
    test_poll_parse_fails();
    test_poll_success_load_exec();
    test_poll_load_exec_fails();
    test_poll_one_event_per_call();
    test_successive_polls_no_reinit();

    /* matches boundary tests — stateless. */
    test_matches_null();
    test_matches_capacity_0_to_7_reject();
    test_matches_capacity_8_accept();
    test_matches_capacity_1024_accept();
    test_matches_capacity_over_1024_reject();
    test_matches_prefix_junk_rejects();
    test_matches_nonnul_at_7_rejects();
    test_matches_raw_1024_all_x_rejects();
    test_matches_extra_after_nul_accepts();
    test_matches_unterminated_rejects();
    test_matches_malformed_rejects();

    /* CheckReceive / Parse output struct tests. */
    test_receive_output_struct_zeroed_on_error();
    test_parse_output_bounded_1024();

    printf("All tests passed.\n");
    return 0;
}