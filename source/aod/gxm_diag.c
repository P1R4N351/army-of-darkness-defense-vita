/*
 * aod-vita DIAGNOSTIC E9 core: see gxm_diag.h. Not a fix.
 * This file is part of aod-vita and is distributed under the MIT license.
 */
#include "aod/gxm_diag.h"
#include "aod/port.h"

#include <stdio.h>
#include <string.h>

static const char *names[AOD_GXD_N] = {
	"sceGxmBeginScene", "sceGxmEndScene", "sceGxmDraw", "sceGxmDrawInstanced", "sceGxmCreateRenderTarget",
	"sceGxmColorSurfaceInit", "sceGxmDepthStencilSurfaceInit", "sceGxmDisplayQueueAddEntry",
	"sceGxmShaderPatcherCreateVertexProgram", "sceGxmShaderPatcherCreateFragmentProgram",
};
const char *aod_gxmdiag_name(int fn) { return fn >= 0 && fn < AOD_GXD_N ? names[fn] : "?"; }

static unsigned long cur_frame = ~0ul;          /* ~0: before the frame loop / preflight */
static unsigned calls[AOD_GXD_N], fails[AOD_GXD_N];          /* this frame */
static unsigned tot_calls[AOD_GXD_N], tot_fails[AOD_GXD_N];  /* whole run */
static unsigned fail_logged, create_logged[AOD_GXD_N], scenes_logged;
static int last_fail_rc[AOD_GXD_N];
static uintptr_t targets[AOD_GXD_TARGETS_PER_FRAME];
static int verbose;
void aod_gxmdiag_set_verbose(int on) { verbose = on != 0; }
static unsigned n_targets, targets_more;

void aod_gxmdiag_frame(unsigned long frame) {
	cur_frame = frame;
	memset(calls, 0, sizeof calls);
	memset(fails, 0, sizeof fails);
	n_targets = targets_more = 0;
}

void aod_gxmdiag_note(int fn, int rc) {
	if (fn < 0 || fn >= AOD_GXD_N) return;
	calls[fn]++; tot_calls[fn]++;
	if (rc < 0) {
		fails[fn]++; tot_fails[fn]++;
		last_fail_rc[fn] = rc;
		if (fail_logged < AOD_GXD_FAIL_LOG_MAX) {
			fail_logged++;
			aod_log("DIAG gxm f%ld %s -> %#x (failure %u of run for this call)", (long)cur_frame, names[fn], (unsigned)rc, tot_fails[fn]);
		}
	}
}

void aod_gxmdiag_scene(int rc, uintptr_t color, uintptr_t depth, uintptr_t rt) {
	aod_gxmdiag_note(AOD_GXD_BEGIN, rc);
	if (n_targets < AOD_GXD_TARGETS_PER_FRAME) targets[n_targets++] = color;
	else targets_more++;
	if (verbose && scenes_logged < AOD_GXD_SCENE_LOG_MAX) {
		scenes_logged++;
		aod_log("DIAG gxm f%ld scene #%u: rc=%#x colour=%#lx depth=%#lx rt=%#lx", (long)cur_frame, scenes_logged, (unsigned)rc,
		        (unsigned long)color, (unsigned long)depth, (unsigned long)rt);
	}
}

int aod_gxmdiag_create_wanted(int fn) { return fn >= 0 && fn < AOD_GXD_N && create_logged[fn] < AOD_GXD_CREATE_LOG_MAX; }

void aod_gxmdiag_create(int fn, int rc, const char *args) {
	aod_gxmdiag_note(fn, rc);
	if (!aod_gxmdiag_create_wanted(fn)) return;
	create_logged[fn]++;
	aod_log("DIAG gxm f%ld %s(%s) -> %#x", (long)cur_frame, names[fn], args ? args : "", (unsigned)rc);
}

void aod_gxmdiag_report(unsigned long frame) {
	char t[128];
	int o = 0;
	t[0] = 0;
	for (unsigned i = 0; i < n_targets && o < (int)sizeof t - 16; i++) o += snprintf(t + o, sizeof t - o, " %#lx", (unsigned long)targets[i]);
	if (targets_more && o < (int)sizeof t - 16) snprintf(t + o, sizeof t - o, " +%u", targets_more);
	aod_log("DIAG f%lu gxm: scenes=%u (fail %u) end=%u (fail %u) draws=%u (fail %u) drawsI=%u (fail %u) dq=%u (fail %u) | "
	        "run fails: begin=%u end=%u draw=%u dq=%u rt=%u cs=%u ds=%u vp=%u fp=%u | scene targets:%s",
	        frame, calls[AOD_GXD_BEGIN], fails[AOD_GXD_BEGIN], calls[AOD_GXD_END], fails[AOD_GXD_END], calls[AOD_GXD_DRAW],
	        fails[AOD_GXD_DRAW], calls[AOD_GXD_DRAW_INSTANCED], fails[AOD_GXD_DRAW_INSTANCED], calls[AOD_GXD_DISPLAY_QUEUE],
	        fails[AOD_GXD_DISPLAY_QUEUE], tot_fails[AOD_GXD_BEGIN], tot_fails[AOD_GXD_END], tot_fails[AOD_GXD_DRAW],
	        tot_fails[AOD_GXD_DISPLAY_QUEUE], tot_fails[AOD_GXD_RENDER_TARGET], tot_fails[AOD_GXD_COLOR_SURFACE],
	        tot_fails[AOD_GXD_DEPTH_SURFACE], tot_fails[AOD_GXD_VPROG], tot_fails[AOD_GXD_FPROG], n_targets ? t : " none");
}
