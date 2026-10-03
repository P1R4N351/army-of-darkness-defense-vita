/*
 * aod/input.h — AOD input subsystem public API, pointer/front/shoulder/rear.
 *
 * Rear-panel contact tracking and direction emission are handled here.  Raw
 * rear contact IDs are tracked internally; only aggregate source IDs 513/514
 * are emitted as direction events.
 * Shoulder button direction (L1/R1) IS handled here (SHOULDERS/BOTH modes).
 *
 * Ownership / contract summary
 * ────────────────────────────
 *  • aod_input_init   – must be called before any other function; never emits.
 *  • aod_input_step   – drives the state machine; returns false on invalid
 *                       inputs or if the emit callback rejects an event.
 *                       Ownership of a contact/key changes ONLY when the emit
 *                       callback returns true (accepted).  A rejected event
 *                       retains the associated contact/key for retry on the
 *                       next call.  Delivery is never guaranteed if the
 *                       callback never accepts; this is not a delivery promise.
 *  • aod_input_cancel – synthesises a release for every owned contact/key;
 *                       returns false on invalid inputs or if any emit call is
 *                       rejected (pending-rejection flag set, retry next frame).
 *                       Each accepted UP is final: an accepted release remains
 *                       released even if a subsequent release in the same call
 *                       is rejected.
 *  • aod_input_set_mode – cancels all owned/held inputs before switching mode;
 *                         inputs must be released before the new mode can rearm.
 *                         Returns false on invalid inputs or pending cancel.
 *  • aod_input_pointer – reads last accepted pointer position; fails closed on
 *                        NULL arguments or invalid geometry.
 *  • Null pointer arguments to any function are rejected (return false/0).
 *  • Invalid geometry (width ≤ 0 or height ≤ 0) causes init to fail closed.
 *  • Front panel always has priority over rear panel contacts.
 *  • Old contact is always released before a new one is claimed.
 *
 * No dynamic allocation.  All state is caller-owned via aod_input_state.
 */

#ifndef AOD_INPUT_H
#define AOD_INPUT_H

#include <stdbool.h>
#include <stdint.h>
#include "input_config.h"

/* ── capacity constants ──────────────────────────────────────────────────── */

#define AOD_INPUT_FRONT_CAP   6u   /* maximum simultaneous front contacts   */
#define AOD_INPUT_REAR_CAP    4u   /* maximum simultaneous rear contacts     */

/* ── source-ID constants ─────────────────────────────────────────────────── */

/* Touch source IDs: front panel occupies [0, 255]; full unsigned SDK byte range. */
#define AOD_INPUT_SOURCE_FRONT_MIN   0
#define AOD_INPUT_SOURCE_FRONT_MAX   255

/* Synthesised pointer / cursor source IDs. */
#define AOD_INPUT_SOURCE_CURSOR      512
#define AOD_INPUT_SOURCE_LEFT        513
#define AOD_INPUT_SOURCE_RIGHT       514

/* Key source IDs. */
#define AOD_INPUT_SOURCE_CIRCLE      97
#define AOD_INPUT_SOURCE_SELECT      109
#define AOD_INPUT_SOURCE_START       108

/* ── button bitmask constants (non-overlapping uint32 bits) ──────────────── */

#define AOD_BTN_CROSS    (UINT32_C(1) << 0)
#define AOD_BTN_CIRCLE   (UINT32_C(1) << 1)
#define AOD_BTN_SELECT   (UINT32_C(1) << 2)
#define AOD_BTN_START    (UINT32_C(1) << 3)
#define AOD_BTN_L1       (UINT32_C(1) << 4)
#define AOD_BTN_R1       (UINT32_C(1) << 5)
#define AOD_BTN_UP       (UINT32_C(1) << 6)
#define AOD_BTN_DOWN     (UINT32_C(1) << 7)
#define AOD_BTN_LEFT     (UINT32_C(1) << 8)
#define AOD_BTN_RIGHT    (UINT32_C(1) << 9)

/* ── action constants ────────────────────────────────────────────────────── */
/*
 * INTEGRATION NOTE: These values differ from Android MotionEvent ordinals.
 * Integrators MUST convert: AOD UP(0)→Android UP(2), AOD DOWN(1)→Android
 * DOWN(0), AOD MOVE(2)→Android MOVE(1).  Do NOT cast AOD_ACTION_* directly
 * to Android MotionEvent actions; the ordinals are intentionally incompatible.
 */
#define AOD_ACTION_UP    0
#define AOD_ACTION_DOWN  1
#define AOD_ACTION_MOVE  2

/* ── event kind ──────────────────────────────────────────────────────────── */

typedef enum {
    AOD_EVENT_TOUCH = 0,
    AOD_EVENT_KEY   = 1
} aod_event_kind;

/* ── event ───────────────────────────────────────────────────────────────── */

typedef struct {
    aod_event_kind kind;
    int32_t        source_id; /* touch: contact id; key: AOD_INPUT_SOURCE_* */
    int            action;    /* AOD_ACTION_UP / DOWN / MOVE                */
    float          x;
    float          y;
} aod_event;

/*
 * Emit callback type.
 *
 * Returns true  → event accepted; ownership of the associated contact passes
 *                  to the caller's downstream consumer.
 * Returns false → event rejected; the subsystem retains ownership of the
 *                  contact/key and will retry on the next aod_input_step call.
 *                  Ownership changes ONLY on acceptance; delivery is NEVER
 *                  guaranteed if the callback never accepts.
 */
typedef bool (*aod_emit_fn)(void *userdata, const aod_event *event);

/* ── raw contact ─────────────────────────────────────────────────────────── */

typedef struct {
    int32_t id;
    float   x;
    float   y;
} aod_raw_contact;

/* ── panel rect ──────────────────────────────────────────────────────────── */

typedef struct {
    float min_x;
    float min_y;
    float max_x;
    float max_y;
} aod_panel_rect;

/* ── input frame (caller fills, subsystem reads) ─────────────────────────── */

typedef struct {
    uint64_t       timestamp_us;
    bool           active;
    bool           front_valid;
    bool           rear_valid;
    bool           pad_valid;

    float          width;
    float          height;

    aod_panel_rect front;
    aod_panel_rect rear;

    /* Front contacts (bounded to AOD_INPUT_FRONT_CAP). */
    aod_raw_contact front_contacts[AOD_INPUT_FRONT_CAP];
    unsigned        front_count;

    /* Rear contacts (bounded to AOD_INPUT_REAR_CAP). */
    aod_raw_contact rear_contacts[AOD_INPUT_REAR_CAP];
    unsigned        rear_count;

    uint32_t       buttons;
    uint8_t        stick_x;
    uint8_t        stick_y;
} aod_input_frame;

/* ── rear-panel contact tracking entry ──────────────────────────────────── */

typedef struct {
    int32_t id;
    int8_t  side;    /* -1 = left zone, 0 = none/deadband, +1 = right zone  */
    bool    blocked; /* true until id absent and fresh press in valid zone   */
} aod_rear_seen;

/* ── cursor owned-contact sub-state ─────────────────────────────────────── */

typedef struct {
    bool    active;
    int32_t id;
    float   x;
    float   y;
} aod_cursor_contact;

/* ── subsystem state (caller-owned, no opaque pointer, no allocation) ────── */

typedef struct {
    /* Geometry validated at init time. */
    float          width;
    float          height;

    /* Current input mode. */
    aod_input_mode mode;

    /* Last accepted pointer position. */
    float          pointer_x;
    float          pointer_y;
    bool           visible;

    /* Cursor ownership. */
    aod_cursor_contact cursor;

    /* Owned front contacts (bounded array). */
    aod_raw_contact front_owned[AOD_INPUT_FRONT_CAP];
    unsigned        front_owned_count;

    /* Timing. */
    uint64_t       last_timestamp_us;
    bool           has_timestamp;

    /* State-machine flags. */
    bool           pending_cancel;  /* cancel in progress; callback rejected  */
    bool           rearm;           /* ready to accept new contacts after mode change */

    /* Cross button suppression (held before init or across mode change). */
    bool           cross_blocked;

    /* Bitmask of key buttons whose DOWN events have been accepted. */
    uint32_t       accepted_key_buttons;

    /* Shoulder directional ownership (SHOULDERS/BOTH modes only). */
    int8_t         dir_owned;        /* -1=left owned, 0=neutral, +1=right owned */
    bool           dir_l_suppressed; /* L1 held through front/cross; freshpress req'd */
    bool           dir_r_suppressed; /* R1 held through front/cross; freshpress req'd */

    /* Rear-panel contact tracker (REAR/BOTH modes). */
    aod_rear_seen  rear_seen[AOD_INPUT_REAR_CAP];
    uint8_t        rear_seen_count;
} aod_input_state;

/* ── public API ──────────────────────────────────────────────────────────── */

/*
 * aod_input_init
 * Initialise *state with the given mode and panel dimensions.
 * Returns false if state is NULL, or width/height are not finite or < 1
 * (finite and >= 1 required; fractional values >= 1 accepted).
 * Never emits.
 */
bool aod_input_init(aod_input_state *state,
                    aod_input_mode   mode,
                    float            width,
                    float            height);

/*
 * aod_input_step
 * Advance the state machine by one frame.
 * Returns false if state or frame is NULL, or if the emit callback rejected
 * an event (pending-rejection flag is set; retry on next frame).
 */
bool aod_input_step(aod_input_state      *state,
                    const aod_input_frame *frame,
                    aod_emit_fn           emit,
                    void                 *userdata);

/*
 * aod_input_cancel
 * Synthesise AOD_ACTION_UP for every owned contact and accepted key button.
 * Returns false if state is NULL or if any emit call is rejected (flag set).
 */
bool aod_input_cancel(aod_input_state *state,
                      aod_emit_fn      emit,
                      void            *userdata);

/*
 * aod_input_set_mode
 * Cancel all owned/held inputs then switch mode.
 * All inputs must be released (via accepted UP events) before the subsystem
 * rearms for the new mode.
 * Returns false if state is NULL or cancel is still pending.
 */
bool aod_input_set_mode(aod_input_state *state,
                        aod_input_mode   mode,
                        aod_emit_fn      emit,
                        void            *userdata);

/*
 * aod_input_pointer
 * Write last accepted pointer coordinates into *out_x and *out_y.
 * Returns false if state, out_x, or out_y is NULL, or geometry is invalid.
 */
bool aod_input_pointer(const aod_input_state *state,
                       float                 *out_x,
                       float                 *out_y);

#endif /* AOD_INPUT_H */