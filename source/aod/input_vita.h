/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 AOD contributors
 *
 * aod/input_vita.h — PS Vita SDK adapter for the AOD input subsystem.
 *
 * Bridges sceTouchPeek / sceCtrlPeekBufferPositiveExt2 into aod_input_frame
 * and drives aod_input_step.  No dynamic allocation.  All state is
 * caller-owned via aod_input_vita.
 */

#ifndef AOD_INPUT_VITA_H
#define AOD_INPUT_VITA_H

#include <stdbool.h>
#include "aod/input.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ── Vita adapter state ──────────────────────────────────────────────────── */

typedef struct {
    aod_input_state  state;        /* core subsystem state                   */
    aod_panel_rect   front;        /* cached front panel active-area rect    */
    aod_panel_rect   rear;         /* cached rear  panel active-area rect    */
    bool             initialized;  /* true after successful aod_input_vita_init */
    bool             front_ready;  /* front panel sampling started + info ok */
    bool             rear_ready;   /* rear  panel sampling started + info ok */
    bool             pad_ready;    /* controller sampling mode set ok        */
    aod_emit_fn      emit;         /* event callback supplied at init        */
    void            *userdata;     /* opaque pointer forwarded to emit       */
} aod_input_vita;

/* ── public API ──────────────────────────────────────────────────────────── */

/*
 * aod_input_vita_init
 * Initialise the Vita SDK adapter and the underlying aod_input_state.
 * Returns false if v or emit is NULL, geometry is rejected by aod_input_init,
 * or the controller sampling-mode call fails.  userdata may be NULL.
 * Rear panel failure is non-fatal: returns true with rear_ready=false when
 * front and pad succeed.  On any failure v is zeroed (fail-closed).
 */
bool aod_input_vita_init(aod_input_vita *v,
                         aod_input_mode  mode,
                         float           width,
                         float           height,
                         aod_emit_fn     emit,
                         void           *userdata);

/*
 * aod_input_vita_poll
 * Sample the SDK, build an aod_input_frame, and drive aod_input_step.
 * Returns false if v is NULL, not initialized, or the core step rejects.
 * When active==false the frame is marked inactive and no SDK reads are
 * performed; the frame is passed to aod_input_step for bookkeeping only.
 */
bool aod_input_vita_poll(aod_input_vita *v, bool active);

/*
 * aod_input_vita_cancel
 * Delegate to aod_input_cancel on the wrapped state.
 */
bool aod_input_vita_cancel(aod_input_vita *v);

/*
 * aod_input_vita_pointer
 * Delegate to aod_input_pointer on the wrapped state.
 */
bool aod_input_vita_pointer(const aod_input_vita *v,
                             float               *out_x,
                             float               *out_y);

#ifdef __cplusplus
}
#endif

#endif /* AOD_INPUT_VITA_H */