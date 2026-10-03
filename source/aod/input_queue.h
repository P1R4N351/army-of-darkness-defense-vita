#ifndef AOD_INPUT_QUEUE_H
#define AOD_INPUT_QUEUE_H

/*
 * aod-vita: fixed-capacity input event queue (256 events).
 * This file is part of aod-vita and is distributed under the MIT license.
 */

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "aod/input.h"

/* Event kinds. */
#define AOD_KIND_TOUCH  0
#define AOD_KIND_KEY    1
#define AOD_KIND_WINDOW 2

/* Touch action values (mirrors Android MotionEvent). */
#define AOD_TOUCH_DOWN  0
#define AOD_TOUCH_MOVE  1
#define AOD_TOUCH_UP    2

#define AOD_QUEUE_CAPACITY 256U

typedef struct {
    int      kind;   /* AOD_KIND_* */
    int      type;   /* action / key-code / window-event */
    int      id;     /* pointer id (TOUCH) or key id (KEY) */
    float    x;
    float    y;
    int64_t  t;      /* timestamp, caller units */
} aod_input_event;

typedef struct {
    aod_input_event events[AOD_QUEUE_CAPACITY];
    unsigned        count;
} aod_input_queue;

/* Initialise queue to empty; also resets the shared touch-id map. */
void aod_input_queue_init(aod_input_queue *q);

/* Clear all events; does NOT reset the touch-id map so ownership is preserved. */
void aod_input_queue_clear(aod_input_queue *q);

/* Number of events currently held (always <= AOD_QUEUE_CAPACITY). */
unsigned aod_input_queue_count(const aod_input_queue *q);

/* Return const pointer to event at index, or NULL if out of range. */
const aod_input_event *aod_input_queue_at(const aod_input_queue *q, unsigned index);

/*
 * Append a KEY or WINDOW event.  Rejects NULL, wrong kind (TOUCH), full queue.
 * Returns true on success.
 */
bool aod_input_queue_push(aod_input_queue *q, const aod_input_event *ev);

/*
 * Map vita_id / action / coords to a TOUCH event and append it.
 * Allowed vita source IDs: 0..255 (full physical byte range), 512..514.
 * action: AOD_TOUCH_DOWN / MOVE / UP.
 * Rejects: NULL q, out-of-range vita_id or action, non-finite coords, full queue
 * (checked BEFORE calling aod_touch_map so a dropped DOWN never allocates a slot).
 * Returns false and does not append on any rejection.
 */
bool aod_input_queue_touch(aod_input_queue *q,
                           int vita_id, int action,
                           float x, float y, int64_t t);

/*
 * aod_input_queue_emit
 * Bridge from the eventbridge aod_event API to the queue primitives.
 *
 * Validates: queue/event non-NULL; timestamp_ms >= 0; kind TOUCH or KEY only.
 *
 * TOUCH: maps AOD_ACTION_DOWN=>AOD_TOUCH_DOWN, MOVE=>AOD_TOUCH_MOVE,
 *        UP=>AOD_TOUCH_UP, then delegates to aod_input_queue_touch.
 *        Rejects any other action value.
 *
 * KEY:   rejects AOD_ACTION_MOVE (not a valid key action).
 *        Maps source_id: CIRCLE(97)/SELECT(109) => engine Back id 0,
 *                        START(108)             => engine Menu id 1.
 *        Any other source_id is rejected.
 *        Maps action: DOWN => native key type 0; UP => native key type 1.
 *        Builds aod_input_event (kind KEY, x/y 0, t=timestamp_ms) and
 *        delegates to aod_input_queue_push.
 *
 * Returns false without mutation on any rejection.
 */
bool aod_input_queue_emit(aod_input_queue  *queue,
                          const aod_event  *event,
                          int64_t           timestamp_ms);

#endif /* AOD_INPUT_QUEUE_H */