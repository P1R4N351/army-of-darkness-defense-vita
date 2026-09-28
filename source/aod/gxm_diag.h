/*
 * aod-vita DIAGNOSTIC E9 (boot-07): what GXM makes of vitaGL's scenes. Not a fix.
 *
 * vitaGL is linked statically, so -Wl,--wrap interposes on its sceGxm* calls (gxm_diag_vita.c).
 * Arguments and return codes pass through unchanged; this core counts calls and failures per
 * frame, logs the first failures and creation arguments, and records each scene's colour target
 * so it can be compared with the display hand-off address. Main thread only (vitaGL's GXM calls
 * are made under its THREAD_SAFE section). All output is bounded.
 *
 * This file is part of aod-vita and is distributed under the MIT license.
 */
#ifndef AOD_GXM_DIAG_H
#define AOD_GXM_DIAG_H

#include <stdint.h>

enum {
	AOD_GXD_BEGIN, AOD_GXD_END, AOD_GXD_DRAW, AOD_GXD_DRAW_INSTANCED, AOD_GXD_RENDER_TARGET,
	AOD_GXD_COLOR_SURFACE, AOD_GXD_DEPTH_SURFACE, AOD_GXD_DISPLAY_QUEUE, AOD_GXD_VPROG, AOD_GXD_FPROG,
	AOD_GXD_N
};

#define AOD_GXD_FAIL_LOG_MAX 48      /* failing calls logged individually */
#define AOD_GXD_CREATE_LOG_MAX 16    /* creation calls (render targets, surfaces) logged with arguments */
#define AOD_GXD_SCENE_LOG_MAX 8      /* first scenes logged with their targets */
#define AOD_GXD_TARGETS_PER_FRAME 4

/* Failures and creation calls are always logged; per-scene lines only when verbose (opt-in). */
void aod_gxmdiag_set_verbose(int on);
void aod_gxmdiag_frame(unsigned long frame);          /* frame begin: resets per-frame counters */
void aod_gxmdiag_note(int fn, int rc);                /* every wrapped call */
void aod_gxmdiag_scene(int rc, uintptr_t color_data, uintptr_t depth_data, uintptr_t render_target);
void aod_gxmdiag_create(int fn, int rc, const char *args); /* creation calls, args preformatted */
int aod_gxmdiag_create_wanted(int fn);                /* whether create args would be logged */
void aod_gxmdiag_report(unsigned long frame);         /* summary frames */
const char *aod_gxmdiag_name(int fn);

#endif
