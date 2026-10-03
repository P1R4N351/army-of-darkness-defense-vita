/*
 * input_launch.h — AOD input-launch settings helpers
 * This file is part of aod-vita and is distributed under the MIT license.
 * SPDX-License-Identifier: MIT
 *
 * Provides init, event-match, and poll for the '-config' app-event launch path.
 * No allocation, no retry, no fatal dialogs. Callers handle all failures.
 */

#ifndef AOD_INPUT_LAUNCH_H
#define AOD_INPUT_LAUNCH_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Initialize the AppUtil library for input-launch polling.
 *
 * @return 0 on success, -1 on failure.
 * Safe to call once at cold startup. A failed init does not permanently disable
 * poll; the caller may retry aod_input_launch_init() on a subsequent attempt.
 */
int aod_input_launch_init(void);

/**
 * Return non-zero iff buffer holds exactly the NUL-terminated string "-config"
 * with the NUL terminator at index 7, strictly within [0, capacity).
 *
 * @param buffer   Pointer to the character buffer (may be NULL).
 * @param capacity Total byte capacity of buffer. Must be > 7 and <= 1024.
 * @return Non-zero if the buffer matches exactly, 0 otherwise.
 */
int aod_input_launch_matches(const char *buffer, size_t capacity);

/**
 * Receive at most one app event and, if it is a '-config' LiveArea event,
 * load app0:/configurator.bin via sceAppMgrLoadExec.
 *
 * @return  1  if LoadExec succeeded (non-negative SDK return).
 *          0  if no action was taken (not initialized, no event, wrong type,
 *             parse error, or unrecognized payload).
 *         -1  if a '-config' event was recognized but LoadExec failed.
 */
int aod_input_launch_poll(void);

#ifdef __cplusplus
}
#endif

#endif /* AOD_INPUT_LAUNCH_H */