/*
 * aod/input.c — AOD input subsystem, pointer/front/shoulder/rear.
 *
 * Shoulder (L1/R1, SHOULDERS/BOTH) and rear-panel contact direction
 * (REAR/BOTH) are handled here.  Rear raw IDs are tracked internally;
 * only aggregate source IDs 513/514 are emitted as direction events.
 *
 * Ownership transfers ONLY after the emit callback returns true (accepted).
 * A rejected UP retains the contact/key for retry on the next call.
 * An accepted release remains released even if a subsequent release in the
 * same cancel call is rejected.  Returns false on invalid inputs or any
 * callback rejection; delivery is never guaranteed if the callback never
 * accepts.
 *
 * No dynamic allocation.  No recursion.  All loops bounded by capacity caps.
 */

#include <math.h>
#include <string.h>
#include "aod/input.h"

/* ── internal constants ─────────────────────────────────────────────────── */

#define STICK_CENTER      128.0f
#define STICK_SCALE       128.0f
#define STICK_DEADZONE    0.16f
#define POINTER_SPEED_PPS 600.0f
#define MAX_DELTA_US      250000u

/* ── finite guard ───────────────────────────────────────────────────────── */

static bool is_finite_f(float v)
{
    return isfinite(v) != 0;
}

/* ── mode validation ────────────────────────────────────────────────────── */

static bool validate_mode(aod_input_mode m)
{
    return m == AOD_INPUT_REAR || m == AOD_INPUT_SHOULDERS
        || m == AOD_INPUT_BOTH;
}

/* ── emit helper ────────────────────────────────────────────────────────── */

static bool emit_event(aod_emit_fn emit, void *ud,
                       aod_event_kind kind, int32_t src_id, int action,
                       float x, float y)
{
    aod_event ev;
    ev.kind      = kind;
    ev.source_id = src_id;
    ev.action    = action;
    ev.x         = x;
    ev.y         = y;
    return emit(ud, &ev);
}

/* ── neutral frame check ────────────────────────────────────────────────── */

/* Considers only front contacts, buttons, and stick deadzone.
 * Rear contacts are intentionally excluded: rear grip must not prevent
 * global rearm, so held rear contacts do not starve front/button/pointer
 * at startup, focus return, or mode switch.  Rear contacts that were
 * present while rearm=false are added to the tracker as blocked
 * (blocked_arm=true when !s->rearm) and remain blocked until their ID
 * is absent from the frame and a fresh contact lands in a valid zone. */
static bool is_neutral_frame(const aod_input_frame *f)
{
    if (f->front_count != 0u) return false;
    if (f->buttons != 0u) return false;
    float dx = ((float)f->stick_x - STICK_CENTER) / STICK_SCALE;
    float dy = ((float)f->stick_y - STICK_CENTER) / STICK_SCALE;
    return (dx * dx + dy * dy) <= (STICK_DEADZONE * STICK_DEADZONE);
}

/* ── frame validation helpers ───────────────────────────────────────────── */

static bool validate_front_contacts(const aod_input_frame *f)
{
    for (unsigned i = 0u; i < f->front_count; i++) {
        const aod_raw_contact *ci = &f->front_contacts[i];
        if (ci->id < AOD_INPUT_SOURCE_FRONT_MIN
         || ci->id > AOD_INPUT_SOURCE_FRONT_MAX) return false;
        if (!is_finite_f(ci->x) || !is_finite_f(ci->y)) return false;
        if (ci->x < f->front.min_x || ci->x > f->front.max_x) return false;
        if (ci->y < f->front.min_y || ci->y > f->front.max_y) return false;
        for (unsigned j = i + 1u; j < f->front_count; j++) {
            if (f->front_contacts[j].id == ci->id) return false;
        }
    }
    return true;
}

static bool validate_frame(const aod_input_state *s, const aod_input_frame *f)
{
    if (!f->active || !f->front_valid || !f->pad_valid) return false;
    if (!is_finite_f(f->width) || !is_finite_f(f->height)) return false;
    if (f->width != s->width || f->height != s->height) return false;
    if (f->front_count > AOD_INPUT_FRONT_CAP) return false;
    if (f->rear_count  > AOD_INPUT_REAR_CAP)  return false;
    if (!is_finite_f(f->front.min_x) || !is_finite_f(f->front.min_y)
     || !is_finite_f(f->front.max_x) || !is_finite_f(f->front.max_y))
        return false;
    /* Span must be finite and positive even when finite endpoints subtract
     * to infinity (e.g. FLT_MAX - (-FLT_MAX) = +inf). */
    float span_x = f->front.max_x - f->front.min_x;
    float span_y = f->front.max_y - f->front.min_y;
    if (!is_finite_f(span_x) || span_x <= 0.0f) return false;
    if (!is_finite_f(span_y) || span_y <= 0.0f) return false;
    return validate_front_contacts(f);
}

/* ── cancel helpers ─────────────────────────────────────────────────────── */

static bool cancel_cursor(aod_input_state *s, aod_emit_fn emit, void *ud)
{
    if (!s->cursor.active) return true;
    bool ok = emit_event(emit, ud, AOD_EVENT_TOUCH, AOD_INPUT_SOURCE_CURSOR,
                         AOD_ACTION_UP, s->cursor.x, s->cursor.y);
    if (ok) s->cursor.active = false;
    return ok;
}

static bool cancel_front_owned(aod_input_state *s, aod_emit_fn emit, void *ud)
{
    bool all_ok = true;
    unsigned i = 0u;
    while (i < s->front_owned_count) {
        aod_raw_contact *c = &s->front_owned[i];
        bool ok = emit_event(emit, ud, AOD_EVENT_TOUCH, c->id,
                             AOD_ACTION_UP, c->x, c->y);
        if (ok) {
            for (unsigned j = i; j + 1u < s->front_owned_count; j++)
                s->front_owned[j] = s->front_owned[j + 1u];
            s->front_owned_count--;
        } else {
            all_ok = false;
            i++;
        }
    }
    return all_ok;
}

/* Key map: bounded to exactly 3 entries (Circle, Select, Start). */
static const struct { uint32_t btn; int32_t src; } KEY_MAP[3] = {
    { AOD_BTN_CIRCLE, AOD_INPUT_SOURCE_CIRCLE },
    { AOD_BTN_SELECT, AOD_INPUT_SOURCE_SELECT },
    { AOD_BTN_START,  AOD_INPUT_SOURCE_START  },
};

static bool cancel_keys(aod_input_state *s, aod_emit_fn emit, void *ud)
{
    bool all_ok = true;
    for (int i = 0; i < 3; i++) {
        if (!(s->accepted_key_buttons & KEY_MAP[i].btn)) continue;
        bool ok = emit_event(emit, ud, AOD_EVENT_KEY, KEY_MAP[i].src,
                             AOD_ACTION_UP, s->pointer_x, s->pointer_y);
        if (ok) s->accepted_key_buttons &= ~KEY_MAP[i].btn;
        else    all_ok = false;
    }
    return all_ok;
}

/* ── direction cancel helper ────────────────────────────────────────────── */

static bool cancel_direction(aod_input_state *s, aod_emit_fn emit, void *ud)
{
    if (s->dir_owned == 0) return true;
    if (!emit) { s->pending_cancel = true; return false; }
    int32_t src = (s->dir_owned < 0) ? AOD_INPUT_SOURCE_LEFT
                                      : AOD_INPUT_SOURCE_RIGHT;
    float cx = (s->dir_owned < 0) ? (s->width  - 1.0f) * 0.25f
                                   : (s->width  - 1.0f) * 0.75f;
    float cy = (s->height - 1.0f) * 0.5f;
    bool ok = emit_event(emit, ud, AOD_EVENT_TOUCH, src,
                         AOD_ACTION_UP, cx, cy);
    if (ok) {
        s->dir_owned = 0;
        /* Require fresh press after cancellation. */
        s->dir_l_suppressed = true;
        s->dir_r_suppressed = true;
    } else {
        s->pending_cancel = true;  /* UP rejected; retry next frame */
    }
    return ok;
}

/* ── public: cancel ─────────────────────────────────────────────────────── */

bool aod_input_cancel(aod_input_state *state, aod_emit_fn emit, void *userdata)
{
    if (!state) return false;
    if (!emit)  { state->pending_cancel = true; return false; }
    bool ok = true;
    if (!cancel_direction(state, emit, userdata))   ok = false;
    if (!cancel_cursor(state, emit, userdata))      ok = false;
    if (!cancel_front_owned(state, emit, userdata)) ok = false;
    if (!cancel_keys(state, emit, userdata))        ok = false;
    if (!ok) {
        state->pending_cancel = true;
    } else {
        state->pending_cancel  = false;
        state->cross_blocked   = true;  /* unblocked when raw Cross releases */
        state->rearm           = false;
        state->rear_seen_count = 0u;    /* require neutral rearm before rear rearms */
    }
    return ok;
}

/* ── public: init ───────────────────────────────────────────────────────── */

bool aod_input_init(aod_input_state *state, aod_input_mode mode,
                    float width, float height)
{
    if (!state) return false;
    if (!validate_mode(mode)) return false;
    if (!is_finite_f(width)  || width  < 1.0f) return false;
    if (!is_finite_f(height) || height < 1.0f) return false;
    memset(state, 0, sizeof(*state));
    state->width     = width;
    state->height    = height;
    state->mode      = mode;
    state->pointer_x = (width  - 1.0f) * 0.5f;
    state->pointer_y = (height - 1.0f) * 0.5f;
    /* rearm=false: valid neutral frame required before any input accepted */
    /* cross_blocked=false: no prior Cross held at this point               */
    return true;
}

/* ── public: set_mode ───────────────────────────────────────────────────── */

bool aod_input_set_mode(aod_input_state *state, aod_input_mode mode,
                        aod_emit_fn emit, void *userdata)
{
    if (!state || !validate_mode(mode)) return false;
    if (state->mode == mode) return true;  /* same mode: no cancellation needed */
    if (!aod_input_cancel(state, emit, userdata)) return false;
    state->mode  = mode;
    state->rearm = false;
    return true;
}

/* ── front-contact processing helpers ──────────────────────────────────── */

static int find_in_frame(const aod_input_frame *f, int32_t id)
{
    for (unsigned i = 0u; i < f->front_count; i++) {
        if (f->front_contacts[i].id == id) return (int)i;
    }
    return -1;
}

static int find_in_owned(const aod_input_state *s, int32_t id)
{
    for (unsigned i = 0u; i < s->front_owned_count; i++) {
        if (s->front_owned[i].id == id) return (int)i;
    }
    return -1;
}

static void map_to_screen(float rx, float ry, const aod_panel_rect *rect,
                           float width, float height,
                           float *out_x, float *out_y)
{
    float nx = (rx - rect->min_x) / (rect->max_x - rect->min_x);
    float ny = (ry - rect->min_y) / (rect->max_y - rect->min_y);
    if (nx < 0.0f) nx = 0.0f; else if (nx > 1.0f) nx = 1.0f;
    if (ny < 0.0f) ny = 0.0f; else if (ny > 1.0f) ny = 1.0f;
    *out_x = nx * (width  - 1.0f);
    *out_y = ny * (height - 1.0f);
}

/* Release owned contacts absent from the current frame snapshot. */
static bool release_absent_owned(aod_input_state *s, const aod_input_frame *f,
                                   aod_emit_fn emit, void *ud)
{
    bool all_ok = true;
    unsigned i = 0u;
    while (i < s->front_owned_count) {
        if (find_in_frame(f, s->front_owned[i].id) >= 0) { i++; continue; }
        bool ok = emit_event(emit, ud, AOD_EVENT_TOUCH, s->front_owned[i].id,
                             AOD_ACTION_UP,
                             s->front_owned[i].x, s->front_owned[i].y);
        if (ok) {
            for (unsigned j = i; j + 1u < s->front_owned_count; j++)
                s->front_owned[j] = s->front_owned[j + 1u];
            s->front_owned_count--;
        } else {
            all_ok = false; i++;
        }
    }
    return all_ok;
}

/* Emit MOVE for owned contacts still present in the frame, if coords changed. */
static bool move_present_owned(aod_input_state *s, const aod_input_frame *f,
                                 aod_emit_fn emit, void *ud)
{
    bool all_ok = true;
    for (unsigned k = 0u; k < s->front_owned_count; k++) {
        int fi = find_in_frame(f, s->front_owned[k].id);
        if (fi < 0) continue;
        float sx, sy;
        map_to_screen(f->front_contacts[fi].x, f->front_contacts[fi].y,
                       &f->front, s->width, s->height, &sx, &sy);
        if (sx == s->front_owned[k].x && sy == s->front_owned[k].y) continue;
        bool ok = emit_event(emit, ud, AOD_EVENT_TOUCH,
                              s->front_owned[k].id, AOD_ACTION_MOVE, sx, sy);
        if (ok) { s->front_owned[k].x = sx; s->front_owned[k].y = sy; }
        else     all_ok = false;
    }
    return all_ok;
}

/* Emit DOWN for contacts in the frame not yet owned (up to capacity). */
static bool down_new_contacts(aod_input_state *s, const aod_input_frame *f,
                               aod_emit_fn emit, void *ud)
{
    bool all_ok = true;
    for (unsigned k = 0u;
         k < f->front_count && s->front_owned_count < AOD_INPUT_FRONT_CAP;
         k++) {
        if (find_in_owned(s, f->front_contacts[k].id) >= 0) continue;
        float sx, sy;
        map_to_screen(f->front_contacts[k].x, f->front_contacts[k].y,
                       &f->front, s->width, s->height, &sx, &sy);
        bool ok = emit_event(emit, ud, AOD_EVENT_TOUCH,
                              f->front_contacts[k].id, AOD_ACTION_DOWN, sx, sy);
        if (ok) {
            unsigned n = s->front_owned_count;
            s->front_owned[n].id = f->front_contacts[k].id;
            s->front_owned[n].x  = sx;
            s->front_owned[n].y  = sy;
            s->front_owned_count++;
        } else {
            all_ok = false;
        }
    }
    return all_ok;
}

static bool process_front_contacts(aod_input_state *s, const aod_input_frame *f,
                                    aod_emit_fn emit, void *ud)
{
    if (f->front_count > 0u) {
        s->cross_blocked = true;
        /* Direction has priority: cancel it before any front DOWN. */
        if (s->dir_owned != 0) {
            if (!cancel_direction(s, emit, ud)) {
                /* pending_cancel already set; abort. */
                return false;
            }
        }
        /* Must release cursor before offering front DOWN. */
        if (s->cursor.active) {
            if (!cancel_cursor(s, emit, ud)) {
                s->pending_cancel = true;
                return false;
            }
        }
    }
    /* Rejected absent-owned UP: set pending_cancel and abort new owners. */
    bool release_ok = release_absent_owned(s, f, emit, ud);
    if (!release_ok) s->pending_cancel = true;
    bool move_ok = move_present_owned(s, f, emit, ud);
    /* Only offer DOWN for new contacts if all absent-owned UPs accepted. */
    bool down_ok = true;
    if (release_ok) down_ok = down_new_contacts(s, f, emit, ud);
    return release_ok && move_ok && down_ok;
}

/* ── rear-panel contact zone detection ─────────────────────────────────── */

/* Returns -1 (left), +1 (right), or 0 (deadband/outside).
 * Left zone:  normalised x in [0.25, 0.45], y in [0.25, 0.75].
 * Right zone: normalised x in [0.55, 0.75], y in [0.25, 0.75]. */
static int8_t rear_zone_side(float rx, float ry, const aod_panel_rect *rect)
{
    float span_x = rect->max_x - rect->min_x;
    float span_y = rect->max_y - rect->min_y;
    if (!is_finite_f(span_x) || span_x <= 0.0f) return 0;
    if (!is_finite_f(span_y) || span_y <= 0.0f) return 0;
    float nx = (rx - rect->min_x) / span_x;
    float ny = (ry - rect->min_y) / span_y;
    if (ny < 0.25f || ny > 0.75f) return 0;
    if (nx >= 0.25f && nx <= 0.45f) return -1;
    if (nx >= 0.55f && nx <= 0.75f) return  1;
    return 0;
}

/* ── rear-panel contact tracker ─────────────────────────────────────────── */

/* Update rear contact tracker from the current frame.  Drops absent IDs,
 * updates zone validity for existing entries, and arms fresh contacts when
 * arming conditions allow.  Never emits; emission is in process_shoulder_direction.
 * Rear raw IDs are internal only; aggregate 513/514 are emitted separately. */
static void update_rear_tracker(aod_input_state *s, const aod_input_frame *f)
{
    if (!f->rear_valid) { s->rear_seen_count = 0u; return; }
    float rsx = f->rear.max_x - f->rear.min_x;
    float rsy = f->rear.max_y - f->rear.min_y;
    if (!is_finite_f(rsx) || rsx <= 0.0f || !is_finite_f(rsy) || rsy <= 0.0f)
        { s->rear_seen_count = 0u; return; }
    /* Duplicate rear ID in frame: invalid; clear tracker. */
    for (unsigned a = 0u; a < f->rear_count; a++) {
        for (unsigned b = a + 1u; b < f->rear_count; b++) {
            if (f->rear_contacts[a].id == f->rear_contacts[b].id)
                { s->rear_seen_count = 0u; return; }
        }
    }
    /* Drop IDs absent from the current frame (compact array). */
    unsigned i = 0u;
    while (i < s->rear_seen_count) {
        bool found = false;
        for (unsigned j = 0u; j < f->rear_count; j++) {
            if (f->rear_contacts[j].id == s->rear_seen[i].id)
                { found = true; break; }
        }
        if (!found) {
            for (unsigned k = i; k + 1u < s->rear_seen_count; k++)
                s->rear_seen[k] = s->rear_seen[k + 1u];
            s->rear_seen_count--;
        } else { i++; }
    }
    /* New contacts blocked while front, Cross raw, active cursor, or not rearmed. */
    bool blocked_arm = f->front_count > 0u || s->front_owned_count > 0u
                    || (f->buttons & AOD_BTN_CROSS) != 0u
                    || s->cursor.active || !s->rearm;
    bool mode_rear = (s->mode == AOD_INPUT_REAR || s->mode == AOD_INPUT_BOTH);
    for (unsigned j = 0u; j < f->rear_count; j++) {
        const aod_raw_contact *rc = &f->rear_contacts[j];
        if (rc->id < 0) continue;  /* no negative IDs: sentinel collision guard */
        if (!is_finite_f(rc->x) || !is_finite_f(rc->y)) continue;
        if (rc->x < f->rear.min_x || rc->x > f->rear.max_x) continue;
        if (rc->y < f->rear.min_y || rc->y > f->rear.max_y) continue;
        int idx = -1;
        for (unsigned k = 0u; k < s->rear_seen_count; k++) {
            if (s->rear_seen[k].id == rc->id) { idx = (int)k; break; }
        }
        if (idx >= 0) {
            aod_rear_seen *rs = &s->rear_seen[idx];
            /* Leaving the armed zone permanently blocks until id absent. */
            if (!rs->blocked && rs->side != 0) {
                if (rear_zone_side(rc->x, rc->y, &f->rear) != rs->side)
                    { rs->side = 0; rs->blocked = true; }
            }
        } else if (s->rear_seen_count < AOD_INPUT_REAR_CAP && mode_rear) {
            aod_rear_seen nr;
            nr.id = rc->id; nr.side = 0; nr.blocked = true;
            if (!blocked_arm) {
                int8_t z = rear_zone_side(rc->x, rc->y, &f->rear);
                if (z != 0) { nr.side = z; nr.blocked = false; }
            }
            s->rear_seen[s->rear_seen_count++] = nr;
        }
    }
}

/* ── pointer movement helpers ───────────────────────────────────────────── */

static void compute_input_dir(const aod_input_frame *f, float *dx, float *dy)
{
    bool up    = (f->buttons & AOD_BTN_UP)    != 0u;
    bool dn    = (f->buttons & AOD_BTN_DOWN)  != 0u;
    bool lt    = (f->buttons & AOD_BTN_LEFT)  != 0u;
    bool rt    = (f->buttons & AOD_BTN_RIGHT) != 0u;
    if (up || dn || lt || rt) {
        float vx = (rt ? 1.0f : 0.0f) - (lt ? 1.0f : 0.0f);
        float vy = (dn ? 1.0f : 0.0f) - (up ? 1.0f : 0.0f);
        float m2 = vx * vx + vy * vy;
        if (m2 > 0.0f) { float m = sqrtf(m2); vx /= m; vy /= m; }
        *dx = vx; *dy = vy;
        return;
    }
    float sx = ((float)f->stick_x - STICK_CENTER) / STICK_SCALE;
    float sy = ((float)f->stick_y - STICK_CENTER) / STICK_SCALE;
    float m2 = sx * sx + sy * sy;
    if (m2 < STICK_DEADZONE * STICK_DEADZONE) { *dx = 0.0f; *dy = 0.0f; return; }
    /* Use true original magnitude for direction (unit vector), apply speed
     * factor separately.  Capping before dividing would shorten diagonals. */
    float mag = sqrtf(m2);
    float capped = (mag > 1.0f) ? 1.0f : mag;
    float remap = (capped - STICK_DEADZONE) / (1.0f - STICK_DEADZONE);
    if (remap > 1.0f) remap = 1.0f;
    *dx = (sx / mag) * remap;   /* mag is original (>= STICK_DEADZONE > 0) */
    *dy = (sy / mag) * remap;
}

static void process_pointer_movement(aod_input_state *s,
                                      const aod_input_frame *f, float dt_sec)
{
    float dx, dy;
    compute_input_dir(f, &dx, &dy);
    if (dx == 0.0f && dy == 0.0f) return;
    s->visible = true;
    float spd = POINTER_SPEED_PPS * dt_sec;
    float nx = s->pointer_x + dx * spd;
    float ny = s->pointer_y + dy * spd;
    if (nx < 0.0f)              nx = 0.0f;
    if (nx > s->width  - 1.0f) nx = s->width  - 1.0f;
    if (ny < 0.0f)              ny = 0.0f;
    if (ny > s->height - 1.0f) ny = s->height - 1.0f;
    /* Guard against unexpected non-finite results before committing. */
    if (is_finite_f(nx) && is_finite_f(ny)) {
        s->pointer_x = nx;
        s->pointer_y = ny;
    }
}

/* ── cross / cursor helper ──────────────────────────────────────────────── */

static bool process_cross(aod_input_state *s, const aod_input_frame *f,
                           aod_emit_fn emit, void *ud)
{
    bool cross_raw = (f->buttons & AOD_BTN_CROSS) != 0u;
    if (!cross_raw) {
        s->cross_blocked = false;   /* raw released: lift suppression */
        if (s->cursor.active) {
            bool ok = emit_event(emit, ud, AOD_EVENT_TOUCH,
                                  AOD_INPUT_SOURCE_CURSOR, AOD_ACTION_UP,
                                  s->cursor.x, s->cursor.y);
            if (ok) s->cursor.active = false;
            else    s->pending_cancel = true;
            return ok;
        }
        return true;
    }
    if (s->cross_blocked) return true;
    /* Cross held/active: cancel direction first; do not run cursor if rejected. */
    if (s->dir_owned != 0) {
        if (!cancel_direction(s, emit, ud)) {
            /* Rejected directionUP: do not proceed to cursor. */
            return false;
        }
    }
    s->visible = true;
    if (!s->cursor.active) {
        bool ok = emit_event(emit, ud, AOD_EVENT_TOUCH,
                              AOD_INPUT_SOURCE_CURSOR, AOD_ACTION_DOWN,
                              s->pointer_x, s->pointer_y);
        if (ok) {
            s->cursor.active = true;
            s->cursor.id     = AOD_INPUT_SOURCE_CURSOR;
            s->cursor.x      = s->pointer_x;
            s->cursor.y      = s->pointer_y;
        }
        return ok;
    }
    /* Cursor active: emit MOVE only when coordinate differs from last accepted. */
    if (s->pointer_x == s->cursor.x && s->pointer_y == s->cursor.y) return true;
    bool ok = emit_event(emit, ud, AOD_EVENT_TOUCH,
                          AOD_INPUT_SOURCE_CURSOR, AOD_ACTION_MOVE,
                          s->pointer_x, s->pointer_y);
    if (ok) { s->cursor.x = s->pointer_x; s->cursor.y = s->pointer_y; }
    return ok;
}

/* ── key-button helper ──────────────────────────────────────────────────── */

static bool process_key_buttons(aod_input_state *s, const aod_input_frame *f,
                                  aod_emit_fn emit, void *ud)
{
    bool all_ok = true;
    for (int i = 0; i < 3; i++) {
        uint32_t btn  = KEY_MAP[i].btn;
        int32_t  src  = KEY_MAP[i].src;
        bool raw_held = (f->buttons & btn) != 0u;
        bool accepted = (s->accepted_key_buttons & btn) != 0u;
        if (raw_held && !accepted) {
            bool ok = emit_event(emit, ud, AOD_EVENT_KEY, src,
                                  AOD_ACTION_DOWN, s->pointer_x, s->pointer_y);
            if (ok) s->accepted_key_buttons |= btn;
            else    all_ok = false;
        } else if (!raw_held && accepted) {
            bool ok = emit_event(emit, ud, AOD_EVENT_KEY, src,
                                  AOD_ACTION_UP, s->pointer_x, s->pointer_y);
            if (ok) s->accepted_key_buttons &= ~btn;
            else {
                /* Rejected keyUP: set pending_cancel so the next aod_input_step
                 * retries the UP via cancel_keys before any new DOWN is emitted.
                 * Return false immediately to prevent subsequent loop iterations
                 * from creating new owned events ahead of the pending old UP. */
                s->pending_cancel = true;
                return false;
            }
        }
    }
    return all_ok;
}

/* ── timing check (extracted to keep aod_input_step within 60 lines) ────── */

static bool step_check_timing(aod_input_state *state,
                               const aod_input_frame *frame,
                               aod_emit_fn emit, void *ud,
                               float *out_dt_sec)
{
    uint64_t now = frame->timestamp_us;
    *out_dt_sec  = 0.0f;
    if (!state->has_timestamp) {
        state->last_timestamp_us = now;
        state->has_timestamp     = true;
        return true;  /* first frame: dt=0, no regression check */
    }
    if (now < state->last_timestamp_us) {
        /* Timestamp regression: cancel owned state, require rearm.
         * Return value deferred via pending_cancel for next-frame retry. */
        (void)aod_input_cancel(state, emit, ud); /* pending_cancel set on rejection */
        state->last_timestamp_us = now;
        state->rearm = false;
        return false;
    }
    uint64_t dt_us = now - state->last_timestamp_us;
    if (dt_us > MAX_DELTA_US) {
        /* Large gap (suspension): cancel owned state, require rearm.
         * Return value deferred via pending_cancel for next-frame retry. */
        (void)aod_input_cancel(state, emit, ud); /* pending_cancel set on rejection */
        state->last_timestamp_us = now;
        state->rearm = false;
        return false;
    }
    state->last_timestamp_us = now;
    *out_dt_sec = (float)dt_us / 1000000.0f;
    return true;
}

/* ── aggregate shoulder/rear directional helper ─────────────────────────── */

/* Emits LEFT(id513,x=.25w) or RIGHT(id514,x=.75w) DOWN/UP as aggregate
 * direction events.  Source: shoulder L1/R1 (SHOULDERS/BOTH modes) OR tracked
 * rear left/right contact (REAR/BOTH modes); same-side OR-ownership means one
 * source holding keeps direction active while the other releases independently.
 * Old direction UP always emitted before new DOWN.
 * RejectedUP retains ownership; RejectedDOWN leaves neutral for retry. */
static bool process_shoulder_direction(aod_input_state *s,
                                        const aod_input_frame *f,
                                        aod_emit_fn emit, void *ud)
{
    bool l1        = (f->buttons & AOD_BTN_L1)    != 0u;
    bool r1        = (f->buttons & AOD_BTN_R1)    != 0u;
    bool cross_raw = (f->buttons & AOD_BTN_CROSS) != 0u;
    /* Raw release clears freshpress suppression. */
    if (!l1) s->dir_l_suppressed = false;
    if (!r1) s->dir_r_suppressed = false;
    /* Suppress direction while front contacts, Cross raw, or active cursor. */
    bool block_dir = f->front_count > 0u || s->front_owned_count > 0u
                  || cross_raw || s->cursor.active;
    if (block_dir) {
        if (l1) s->dir_l_suppressed = true;
        if (r1) s->dir_r_suppressed = true;
        if (s->dir_owned != 0) {
            if (!cancel_direction(s, emit, ud)) return false;
        }
        return true;
    }
    /* Aggregate: shoulder sources (SHOULDERS/BOTH). */
    bool mode_shld = (s->mode == AOD_INPUT_SHOULDERS || s->mode == AOD_INPUT_BOTH);
    bool l_shld = mode_shld && l1 && !s->dir_l_suppressed;
    bool r_shld = mode_shld && r1 && !s->dir_r_suppressed;
    /* Aggregate: rear sources (REAR/BOTH): any armed left/right contact. */
    bool l_rear = false, r_rear = false;
    if (s->mode == AOD_INPUT_REAR || s->mode == AOD_INPUT_BOTH) {
        for (unsigned i = 0u; i < s->rear_seen_count; i++) {
            if (!s->rear_seen[i].blocked) {
                if (s->rear_seen[i].side == -1) l_rear = true;
                if (s->rear_seen[i].side ==  1) r_rear = true;
            }
        }
    }
    bool l_active = l_shld || l_rear;
    bool r_active = r_shld || r_rear;
    int8_t desired = (l_active && !r_active) ? (int8_t)-1
                   : (r_active && !l_active) ? (int8_t) 1
                   :                           (int8_t) 0;
    if (desired == s->dir_owned) return true;
    /* Emit UP for old direction BEFORE DOWN for new. */
    if (s->dir_owned != 0) {
        int32_t old_src = (s->dir_owned < 0) ? AOD_INPUT_SOURCE_LEFT
                                              : AOD_INPUT_SOURCE_RIGHT;
        float old_x = (s->dir_owned < 0) ? (s->width  - 1.0f) * 0.25f
                                          : (s->width  - 1.0f) * 0.75f;
        float old_y = (s->height - 1.0f) * 0.5f;
        bool ok = emit_event(emit, ud, AOD_EVENT_TOUCH, old_src,
                             AOD_ACTION_UP, old_x, old_y);
        if (!ok) { s->pending_cancel = true; return false; }
        s->dir_owned = 0;
    }
    if (desired == 0) return true;
    int32_t new_src = (desired < 0) ? AOD_INPUT_SOURCE_LEFT : AOD_INPUT_SOURCE_RIGHT;
    float new_x = (desired < 0) ? (s->width  - 1.0f) * 0.25f
                                 : (s->width  - 1.0f) * 0.75f;
    float new_y = (s->height - 1.0f) * 0.5f;
    bool ok = emit_event(emit, ud, AOD_EVENT_TOUCH, new_src,
                          AOD_ACTION_DOWN, new_x, new_y);
    if (ok) s->dir_owned = desired;
    return ok;  /* RejectedDOWN: dir_owned stays 0; next step retries */
}

/* ── public: step ───────────────────────────────────────────────────────── */

bool aod_input_step(aod_input_state *state, const aod_input_frame *frame,
                    aod_emit_fn emit, void *userdata)
{
    if (!state || !frame || !emit) {
        if (state) aod_input_cancel(state, NULL, userdata);
        return false;
    }
    if (!validate_frame(state, frame)) {
        /* Frame invalid: cancel; pending_cancel deferred if emit rejects. */
        (void)aod_input_cancel(state, emit, userdata); /* pending_cancel set on rejection */
        return false;
    }
    float dt_sec;
    if (!step_check_timing(state, frame, emit, userdata, &dt_sec)) return false;
    update_rear_tracker(state, frame);
    if (state->pending_cancel) {
        if (!aod_input_cancel(state, emit, userdata)) return false;
    }
    if (!state->rearm) {
        if (is_neutral_frame(frame)) state->rearm = true;
        else return true;  /* not rearmed: no new events */
    }
    bool all_ok = true;
    if (!process_front_contacts(state, frame, emit, userdata)) {
        all_ok = false;
        /* Any rejected UP sets pending_cancel; abort further processing. */
        if (state->pending_cancel) return false;
    }
    /* Cursor and shoulder direction require no active or pending front contacts. */
    if (frame->front_count == 0u && state->front_owned_count == 0u) {
        process_pointer_movement(state, frame, dt_sec);
        if (!process_cross(state, frame, emit, userdata)) {
            all_ok = false;
            if (state->pending_cancel) return false;
        }
        if (!process_shoulder_direction(state, frame, emit, userdata)) {
            all_ok = false;
            if (state->pending_cancel) return false;
        }
    }
    if (!process_key_buttons(state, frame, emit, userdata)) all_ok = false;
    return all_ok;
}

/* ── public: pointer ────────────────────────────────────────────────────── */

bool aod_input_pointer(const aod_input_state *state,
                       float *out_x, float *out_y)
{
    if (!state || !out_x || !out_y) return false;
    if (state->width < 1.0f || state->height < 1.0f) return false;
    if (!is_finite_f(state->pointer_x) || !is_finite_f(state->pointer_y))
        return false;
    *out_x = state->pointer_x;
    *out_y = state->pointer_y;
    return state->visible;
}