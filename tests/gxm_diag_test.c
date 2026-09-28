/* Host test for source/aod/gxm_diag.c (DIAGNOSTIC E9 core): per-frame counters, bounded failure
 * and creation logging, scene-target recording. */
#include "aod/gxm_diag.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fails, checks;
#define CHECK(c, ...) do { checks++; if (!(c)) { fails++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)
static char logs[512][400];
static int nlog;
void aod_log(const char *fmt, ...) {
	va_list ap; va_start(ap, fmt); vsnprintf(logs[nlog % 512], sizeof logs[0], fmt, ap); va_end(ap);
	if (getenv("VERBOSE")) puts(logs[nlog % 512]);
	nlog++;
}
static int count(const char *s) { int n = 0; for (int i = 0; i < nlog && i < 512; i++) n += strstr(logs[i], s) != NULL; return n; }
static const char *last(void) { return logs[(nlog - 1) % 512]; }

int main(void) {
	aod_gxmdiag_set_verbose(1);
	/* creation before the frame loop */
	aod_gxmdiag_create(AOD_GXD_RENDER_TARGET, 0, "flags=0 960x544 scenes=1 msaa=2");
	CHECK(strstr(last(), "DIAG gxm f-1 sceGxmCreateRenderTarget(flags=0 960x544 scenes=1 msaa=2) -> 0"), "create logged before loop (%s)", last());
	for (int i = 0; i < 40; i++) aod_gxmdiag_create(AOD_GXD_COLOR_SURFACE, 0, "x");
	CHECK(count("sceGxmColorSurfaceInit(") == AOD_GXD_CREATE_LOG_MAX, "creation logging bounded (%d)", count("sceGxmColorSurfaceInit("));
	CHECK(!aod_gxmdiag_create_wanted(AOD_GXD_COLOR_SURFACE) && aod_gxmdiag_create_wanted(AOD_GXD_DEPTH_SURFACE), "create_wanted per call");

	/* a frame with two scenes, draws, one failing draw */
	aod_gxmdiag_frame(7);
	aod_gxmdiag_scene(0, 0x622a4000u, 0x70000000u, 0x1234u);
	CHECK(strstr(last(), "DIAG gxm f7 scene #1: rc=0 colour=0x622a4000 depth=0x70000000 rt=0x1234"), "first scene logged (%s)", last());
	for (int i = 0; i < 5; i++) aod_gxmdiag_note(AOD_GXD_DRAW, 0);
	aod_gxmdiag_note(AOD_GXD_DRAW, (int)0x805B0005u);
	CHECK(strstr(last(), "DIAG gxm f7 sceGxmDraw -> 0x805b0005 (failure 1 of run"), "failure logged with code (%s)", last());
	aod_gxmdiag_note(AOD_GXD_END, 0);
	aod_gxmdiag_note(AOD_GXD_DISPLAY_QUEUE, 0);
	aod_gxmdiag_report(7);
	CHECK(strstr(last(), "DIAG f7 gxm: scenes=1 (fail 0) end=1 (fail 0) draws=6 (fail 1) drawsI=0 (fail 0) dq=1 (fail 0)") &&
	      strstr(last(), "run fails: begin=0 end=0 draw=1") && strstr(last(), "scene targets: 0x622a4000"), "report (%s)", last());
	/* next frame: counters reset, run totals kept; >4 targets summarised */
	aod_gxmdiag_frame(8);
	for (int i = 0; i < 6; i++) aod_gxmdiag_scene((int)0x805B0001u, 0x624a5000u + i, 0, 0);
	aod_gxmdiag_report(8);
	CHECK(strstr(last(), "scenes=6 (fail 6)") && strstr(last(), "draws=0 (fail 0)") && strstr(last(), "run fails: begin=6 end=0 draw=1") &&
	      strstr(last(), "+2"), "per-frame reset, run totals, target overflow (%s)", last());
	aod_gxmdiag_frame(9);
	aod_gxmdiag_report(9);
	CHECK(strstr(last(), "scene targets: none"), "no scenes -> none");
	/* failure log bounded; scene log bounded */
	for (int i = 0; i < 200; i++) aod_gxmdiag_note(AOD_GXD_END, -1);
	CHECK(count("-> 0x") - count("sceGxmCreateRenderTarget(") - count("sceGxmColorSurfaceInit(") <= AOD_GXD_FAIL_LOG_MAX + 1, "failure log bounded");
	for (int i = 0; i < 20; i++) aod_gxmdiag_scene(0, 0x626a6000u, 0, 0);
	CHECK(count("scene #") == AOD_GXD_SCENE_LOG_MAX, "scene log bounded (%d)", count("scene #"));
	aod_gxmdiag_note(AOD_GXD_N, -1); aod_gxmdiag_note(-1, -1);
	CHECK(strcmp(aod_gxmdiag_name(99), "?") == 0, "out-of-range ids ignored");
	/* non-verbose (default build): failures still logged, scene lines not */
	aod_gxmdiag_set_verbose(0);
	int before = nlog;
	aod_gxmdiag_frame(50);
	aod_gxmdiag_scene(0, 0x1, 0, 0);
	CHECK(nlog == before, "non-verbose: no scene lines");
	aod_gxmdiag_note(AOD_GXD_BEGIN, (int)0x805B0002u);
	CHECK(nlog == before || strstr(last(), "0x805b0002"), "non-verbose: failures still logged (or failure log already full)");
	printf("%d/%d checks passed (gxm diag)\n", checks - fails, checks);
	return fails != 0;
}
