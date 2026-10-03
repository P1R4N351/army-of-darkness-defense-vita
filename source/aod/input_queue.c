/*
 * aod-vita: fixed-capacity input event queue implementation.
 * This file is part of aod-vita and is distributed under the MIT license.
 */

#include "aod/input_queue.h"
#include "aod/touch_ids.h"
#include <math.h>   /* isfinite */

/* input.h pulled transitively via input_queue.h; no second include needed. */

/* ---- helpers ------------------------------------------------------------ */

static bool vita_id_valid(int vita_id)
{
    return (vita_id >= AOD_INPUT_SOURCE_FRONT_MIN &&
            vita_id <= AOD_INPUT_SOURCE_FRONT_MAX) ||
           (vita_id >= 512 && vita_id <= 514);
}

static bool coords_finite(float x, float y)
{
    /* isfinite defined in C99 <math.h>; rejects NaN and ±Inf.
     * WARNING: isfinite is NOT reliable under -ffast-math (the compiler may
     * fold or elide the check).  This production file MUST be compiled with
     * -fno-fast-math.  Host/unit tests must use the release flags and then
     * append -fno-fast-math to override any fast-math flag inherited from the
     * release profile. */
    return isfinite(x) && isfinite(y);
}

/* ---- public API --------------------------------------------------------- */

void aod_input_queue_init(aod_input_queue *q)
{
    if (!q) { return; }
    q->count = 0U;
    aod_touch_reset();
}

void aod_input_queue_clear(aod_input_queue *q)
{
    if (!q) { return; }
    q->count = 0U;
    /* touch-map ownership deliberately preserved — see header contract */
}

unsigned aod_input_queue_count(const aod_input_queue *q)
{
    if (!q) { return 0U; }
    /* Defensive clamp; count must never exceed capacity. */
    return (q->count <= AOD_QUEUE_CAPACITY) ? q->count : AOD_QUEUE_CAPACITY;
}

const aod_input_event *aod_input_queue_at(const aod_input_queue *q,
                                           unsigned index)
{
    if (!q) { return NULL; }
    if (index >= aod_input_queue_count(q)) { return NULL; }
    return &q->events[index];
}

bool aod_input_queue_push(aod_input_queue *q, const aod_input_event *ev)
{
    if (!q || !ev) { return false; }
    if (ev->kind == AOD_KIND_TOUCH) { return false; }
    if (ev->kind != AOD_KIND_KEY && ev->kind != AOD_KIND_WINDOW) { return false; }
    if (q->count >= AOD_QUEUE_CAPACITY) { return false; }

    q->events[q->count] = *ev;
    q->count++;
    return true;
}

bool aod_input_queue_emit(aod_input_queue *queue,
                          const aod_event *event,
                          int64_t          timestamp_ms)
{
    if (!queue || !event)         { return false; }
    if (timestamp_ms < 0)         { return false; }

    if (event->kind == AOD_EVENT_TOUCH) {
        /* Map AOD_ACTION_* to AOD_TOUCH_* (Android MotionEvent ordinals). */
        int android_action;
        switch (event->action) {
            case AOD_ACTION_DOWN: android_action = AOD_TOUCH_DOWN; break;
            case AOD_ACTION_MOVE: android_action = AOD_TOUCH_MOVE; break;
            case AOD_ACTION_UP:   android_action = AOD_TOUCH_UP;   break;
            default:              return false;
        }
        return aod_input_queue_touch(queue,
                                     (int)event->source_id,
                                     android_action,
                                     event->x, event->y,
                                     timestamp_ms);
    }

    if (event->kind == AOD_EVENT_KEY) {
        /* MOVE is not a valid key action; reject without mutation. */
        if (event->action == AOD_ACTION_MOVE) { return false; }

        /* Map source_id to engine key id; reject unknown sources. */
        int key_id;
        switch (event->source_id) {
            case AOD_INPUT_SOURCE_CIRCLE: /* fall-through */
            case AOD_INPUT_SOURCE_SELECT: key_id = 0; break;
            case AOD_INPUT_SOURCE_START:  key_id = 1; break;
            default:                      return false;
        }

        /* Map action to native key type: DOWN => 0, UP => 1. */
        int native_type;
        switch (event->action) {
            case AOD_ACTION_DOWN: native_type = 0; break;
            case AOD_ACTION_UP:   native_type = 1; break;
            default:              return false;
        }

        aod_input_event kev;
        kev.kind = AOD_KIND_KEY;
        kev.type = native_type;
        kev.id   = key_id;
        kev.x    = 0.0f;
        kev.y    = 0.0f;
        kev.t    = timestamp_ms;
        return aod_input_queue_push(queue, &kev);
    }

    /* Unknown kind — reject without mutation. */
    return false;
}

bool aod_input_queue_touch(aod_input_queue *q,
                            int vita_id, int action,
                            float x, float y, int64_t t)
{
    if (!q) { return false; }
    if (!vita_id_valid(vita_id)) { return false; }
    if (action < AOD_TOUCH_DOWN || action > AOD_TOUCH_UP) { return false; }
    if (!coords_finite(x, y)) { return false; }

    /* Reject BEFORE touching the slot map so a full-queue DOWN doesn't consume
     * a pointer slot that can never be freed by a subsequent UP. */
    if (q->count >= AOD_QUEUE_CAPACITY) { return false; }

    int ptr_id = aod_touch_map(vita_id, action);
    if (ptr_id < 0) { return false; }

    aod_input_event *ev = &q->events[q->count];
    ev->kind = AOD_KIND_TOUCH;
    ev->type = action;
    ev->id   = ptr_id;
    ev->x    = x;
    ev->y    = y;
    ev->t    = t;
    q->count++;
    return true;
}