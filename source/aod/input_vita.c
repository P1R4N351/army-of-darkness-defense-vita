/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 AOD contributors
 *
 * aod/input_vita.c — PS Vita SDK adapter implementation.
 */

#include "aod/input_vita.h"

#include <string.h>
#include <math.h>

#include <psp2/touch.h>
#include <psp2/ctrl.h>
#include <psp2/kernel/processmgr.h>

/* ── internal constants ──────────────────────────────────────────────────── */

/* SDK port numbers */
#define VITA_PORT_FRONT  SCE_TOUCH_PORT_FRONT   /* 0 */
#define VITA_PORT_REAR   SCE_TOUCH_PORT_BACK    /* 1 */

/* Per-panel report caps enforced before array copy */
#define VITA_FRONT_REPORT_CAP  6u
#define VITA_REAR_REPORT_CAP   4u

/* ── static helpers ──────────────────────────────────────────────────────── */

/* Build an aod_panel_rect from SceTouchPanelInfo active-area int16 fields.
 * Returns false when the span in either axis is not finite and positive. */
static bool s_rect_from_panel(aod_panel_rect *r, const SceTouchPanelInfo *pi)
{
    float min_x = (float)pi->minAaX;
    float min_y = (float)pi->minAaY;
    float max_x = (float)pi->maxAaX;
    float max_y = (float)pi->maxAaY;
    if (!isfinite(min_x) || !isfinite(min_y) ||
        !isfinite(max_x) || !isfinite(max_y)) {
        return false;
    }
    if ((max_x - min_x) <= 0.0f || (max_y - min_y) <= 0.0f) {
        return false;
    }
    r->min_x = min_x;
    r->min_y = min_y;
    r->max_x = max_x;
    r->max_y = max_y;
    return true;
}

/* Initialise one touch panel: start sampling, query info, build rect.
 * Returns false on any SDK error or invalid geometry. */
static bool s_init_panel(SceUInt32 port, aod_panel_rect *rect_out)
{
    int rc = sceTouchSetSamplingState(port, SCE_TOUCH_SAMPLING_STATE_START);
    if (rc < 0) {
        return false;
    }
    SceTouchPanelInfo pi;
    memset(&pi, 0, sizeof(pi));
    rc = sceTouchGetPanelInfo(port, &pi);
    if (rc < 0) {
        return false;
    }
    return s_rect_from_panel(rect_out, &pi);
}

/* Map SDK buttons to AOD_BTN bitmask (named buttons only; ignore others). */
static uint32_t s_map_buttons(unsigned int sdk_buttons)
{
    uint32_t out = 0u;
    if (sdk_buttons & SCE_CTRL_L1)     { out |= AOD_BTN_L1;    }
    if (sdk_buttons & SCE_CTRL_R1)     { out |= AOD_BTN_R1;    }
    if (sdk_buttons & SCE_CTRL_START)  { out |= AOD_BTN_START;  }
    if (sdk_buttons & SCE_CTRL_SELECT) { out |= AOD_BTN_SELECT; }
    if (sdk_buttons & SCE_CTRL_CIRCLE) { out |= AOD_BTN_CIRCLE; }
    if (sdk_buttons & SCE_CTRL_CROSS)  { out |= AOD_BTN_CROSS;  }
    if (sdk_buttons & SCE_CTRL_UP)     { out |= AOD_BTN_UP;     }
    if (sdk_buttons & SCE_CTRL_DOWN)   { out |= AOD_BTN_DOWN;   }
    if (sdk_buttons & SCE_CTRL_LEFT)   { out |= AOD_BTN_LEFT;   }
    if (sdk_buttons & SCE_CTRL_RIGHT)  { out |= AOD_BTN_RIGHT;  }
    return out;
}

/* Copy front touch reports into frame; fail-closed: reportNum > cap => false.
 * Returns true and sets front_count only when the count is within bounds. */
static bool s_copy_front(aod_input_frame *frame, const SceTouchData *td)
{
    unsigned count = (unsigned)td->reportNum;
    if (count > VITA_FRONT_REPORT_CAP || count > AOD_INPUT_FRONT_CAP) {
        frame->front_valid = false;
        frame->front_count = 0u;
        return false;
    }
    unsigned i;
    for (i = 0u; i < count; ++i) {
        frame->front_contacts[i].id = (int32_t)td->report[i].id;
        frame->front_contacts[i].x  = (float)td->report[i].x;
        frame->front_contacts[i].y  = (float)td->report[i].y;
    }
    frame->front_count = count;
    return true;
}

/* Copy rear touch reports into frame; fail-closed: reportNum > cap => false.
 * Returns true and sets rear_count only when the count is within bounds. */
static bool s_copy_rear(aod_input_frame *frame, const SceTouchData *td)
{
    unsigned count = (unsigned)td->reportNum;
    if (count > VITA_REAR_REPORT_CAP || count > AOD_INPUT_REAR_CAP) {
        frame->rear_valid = false;
        frame->rear_count = 0u;
        return false;
    }
    unsigned i;
    for (i = 0u; i < count; ++i) {
        frame->rear_contacts[i].id = (int32_t)td->report[i].id;
        frame->rear_contacts[i].x  = (float)td->report[i].x;
        frame->rear_contacts[i].y  = (float)td->report[i].y;
    }
    frame->rear_count = count;
    return true;
}

/* Read one front-panel frame from the SDK; sets front_valid only on
 * accepted copy (rc==1 exactly: one buffer returned). Any other rc rejected. */
static void s_read_front(aod_input_frame *frame, SceUInt32 port)
{
    SceTouchData td;
    memset(&td, 0, sizeof(td));
    int rc = sceTouchPeek(port, &td, 1);
    if (rc != 1) { return; }
    if (s_copy_front(frame, &td)) {
        frame->front_valid = true;
    }
}

/* Read one rear-panel frame from the SDK; sets rear_valid only on
 * accepted copy (rc==1 exactly: one buffer returned). Any other rc rejected. */
static void s_read_rear(aod_input_frame *frame, SceUInt32 port)
{
    SceTouchData tdr;
    memset(&tdr, 0, sizeof(tdr));
    int rc = sceTouchPeek(port, &tdr, 1);
    if (rc != 1) { return; }
    if (s_copy_rear(frame, &tdr)) {
        frame->rear_valid = true;
    }
}

/* ── public API ──────────────────────────────────────────────────────────── */

bool aod_input_vita_init(aod_input_vita *v,
                         aod_input_mode  mode,
                         float           width,
                         float           height,
                         aod_emit_fn     emit,
                         void           *userdata)
{
    /* userdata may be NULL; emit must not be. */
    if (!v || !emit) { return false; }

    /* Zero first: any early return leaves v in a defined fail-closed state. */
    memset(v, 0, sizeof(*v));

    /* Geometry validity is enforced by aod_input_init (width/height >= 1 and
     * finite); we do not duplicate that check here. */
    if (!aod_input_init(&v->state, mode, width, height)) {
        return false;
    }

    /* Controller sampling mode — required for L1/R1 via Ext2. */
    int rc = sceCtrlSetSamplingModeExt(SCE_CTRL_MODE_ANALOG_WIDE);
    if (rc < 0) {
        v->initialized = false;
        return false;
    }
    v->pad_ready = true;

    /* Front panel — failure is fatal. */
    v->front_ready = s_init_panel((SceUInt32)VITA_PORT_FRONT, &v->front);
    if (!v->front_ready) {
        v->initialized = false;
        return false;
    }

    /* Rear panel — failure is non-fatal; shoulders mode still usable. */
    v->rear_ready = s_init_panel((SceUInt32)VITA_PORT_REAR, &v->rear);

    v->emit     = emit;
    v->userdata = userdata;
    v->initialized = true;
    return true;
}

bool aod_input_vita_poll(aod_input_vita *v, bool active)
{
    if (!v || !v->initialized) { return false; }

    aod_input_frame frame;
    memset(&frame, 0, sizeof(frame));

    frame.active  = active;
    frame.width   = v->state.width;
    frame.height  = v->state.height;
    frame.front   = v->front;
    frame.rear    = v->rear;

    /* Timestamp is independent of panel sampling. */
    frame.timestamp_us = (uint64_t)sceKernelGetProcessTimeWide();

    if (!active) {
        /* Inactive frame: no SDK reads at all; pass to core for bookkeeping. */
        return aod_input_step(&v->state, &frame, v->emit, v->userdata);
    }

    /* ── Front touch ──────────────────────────────────────────────────── */
    if (v->front_ready) {
        s_read_front(&frame, (SceUInt32)VITA_PORT_FRONT);
    }

    /* ── Rear touch ───────────────────────────────────────────────────── */
    if (v->rear_ready) {
        s_read_rear(&frame, (SceUInt32)VITA_PORT_REAR);
    }

    /* ── Controller ───────────────────────────────────────────────────── */
    if (v->pad_ready) {
        SceCtrlData pad;
        memset(&pad, 0, sizeof(pad));
        int rc = sceCtrlPeekBufferPositiveExt2(0, &pad, 1);
        if (rc == 1) {
            frame.pad_valid = true;
            frame.buttons   = s_map_buttons(pad.buttons);
            frame.stick_x   = pad.lx;
            frame.stick_y   = pad.ly;
        }
    }

    return aod_input_step(&v->state, &frame, v->emit, v->userdata);
}

bool aod_input_vita_cancel(aod_input_vita *v)
{
    if (!v || !v->initialized) { return false; }
    return aod_input_cancel(&v->state, v->emit, v->userdata);
}

bool aod_input_vita_pointer(const aod_input_vita *v,
                             float               *out_x,
                             float               *out_y)
{
    if (!v || !v->initialized) { return false; }
    return aod_input_pointer(&v->state, out_x, out_y);
}