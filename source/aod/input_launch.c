/*
 * input_launch.c — AOD input-launch settings helpers
 * This file is part of aod-vita and is distributed under the MIT license.
 *
 * Implements aod_input_launch_init, aod_input_launch_matches,
 * and aod_input_launch_poll.  No dynamic allocation, no retry,
 * bounded SDK buffer of 1024 bytes, checked SDK returns throughout.
 */

#include "input_launch.h"

#include <psp2/apputil.h>
#include <psp2/appmgr.h>
#include <string.h>
#include <stdint.h>

/* The exact expected payload string and its length including NUL. */
static const char   k_config_str[]  = "-config"; /* 7 chars + NUL = 8 bytes */
static const size_t k_config_len    = 7u;         /* strlen("-config")       */
static const size_t k_max_capacity  = 1024u;

/* AppUtil event type for LiveArea launches. */
static const unsigned int k_event_type_livearea = 0x05u;

/* Module-level initialized flag; set exactly once by a successful init. */
static int s_initialized = 0;

/* --------------------------------------------------------------------------
 * aod_input_launch_init
 * -------------------------------------------------------------------------- */
int aod_input_launch_init(void)
{
    SceAppUtilInitParam init_param;
    SceAppUtilBootParam boot_param;
    int rc;

    if (s_initialized) {
        return 0;
    }

    memset(&init_param, 0, sizeof(init_param));
    memset(&boot_param, 0, sizeof(boot_param));

    rc = sceAppUtilInit(&init_param, &boot_param);
    if (rc < 0) {
        return -1;
    }

    s_initialized = 1;
    return 0;
}

/* --------------------------------------------------------------------------
 * aod_input_launch_matches
 * -------------------------------------------------------------------------- */
int aod_input_launch_matches(const char *buffer, size_t capacity)
{
    /* NULL, zero capacity, out-of-spec capacity, or too small to hold token. */
    if (buffer == NULL) {
        return 0;
    }
    if (capacity == 0u || capacity > k_max_capacity) {
        return 0;
    }
    /* NUL terminator must fit within capacity: index 7 < capacity. */
    if (capacity <= k_config_len) {
        return 0;
    }
    /* Exact byte-for-byte match of the token. */
    if (memcmp(buffer, k_config_str, k_config_len) != 0) {
        return 0;
    }
    /* NUL must be at exactly index k_config_len. */
    if (buffer[k_config_len] != '\0') {
        return 0;
    }
    return 1;
}

/* --------------------------------------------------------------------------
 * aod_input_launch_poll
 * -------------------------------------------------------------------------- */
int aod_input_launch_poll(void)
{
    SceAppUtilAppEventParam event_param;
    char parse_buf[1024];
    int rc_recv;
    int rc_parse;
    int rc_exec;

    if (!s_initialized) {
        return 0;
    }

    memset(&event_param, 0, sizeof(event_param));
    rc_recv = sceAppUtilReceiveAppEvent(&event_param);
    if (rc_recv < 0) {
        return 0;
    }

    /* Only handle LiveArea launch events. */
    if ((unsigned int)event_param.type != k_event_type_livearea) {
        return 0;
    }

    memset(parse_buf, 0, sizeof(parse_buf));
    rc_parse = sceAppUtilAppEventParseLiveArea(&event_param, parse_buf);
    if (rc_parse < 0) {
        return 0;
    }

    /* Validate the parsed payload is exactly "-config\0" within 1024 bytes. */
    if (!aod_input_launch_matches(parse_buf, sizeof(parse_buf))) {
        return 0;
    }

    rc_exec = sceAppMgrLoadExec("app0:/configurator.bin", NULL, NULL);
    if (rc_exec < 0) {
        return -1;
    }
    return 1;
}