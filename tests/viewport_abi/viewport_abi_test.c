/* Calibrated ABI test for the boot-08 viewport fault and its fix (run under qemu-arm, softfp).
 * Links: vitaGL's own sceGxmSetViewport_sfp source text (extracted from lib/vitagl/source/vgl.c),
 * the SDK's sceGxmSetViewport.o adapter (extracted from the pinned libSceGxm_stub.a), our
 * source/aod/viewport_softfp.c, and a hard-float recording target. */
#include <stdio.h>
typedef struct SceGxmContext SceGxmContext;
extern volatile float rec[6];
extern volatile void *rec_ctx;
extern volatile int rec_calls;
void sceGxmSetViewport_sfp(SceGxmContext *, float, float, float, float, float, float);
void sceGxmSetViewport(SceGxmContext *, float, float, float, float, float, float);
void __wrap_sceGxmSetViewport_sfp(SceGxmContext *, float, float, float, float, float, float);

static int fails, checks;
static void expect(const char *what, const float *want) {
	int ok = 1;
	for (int i = 0; i < 6; i++) ok &= rec[i] == want[i];
	checks++;
	if (!ok) fails++;
	printf("%s %s: received (%g, %g, %g, %g, %g, %g)\n", ok ? "ok  " : "FAIL", what, rec[0], rec[1], rec[2], rec[3], rec[4], rec[5]);
}
int main(void) {
	SceGxmContext *ctx = (SceGxmContext *)0x1234;
	const float full[6] = { 480.f, 480.f, 272.f, -272.f, 0.5f, 0.5f };           /* vitaGL full-screen viewport */
	const float distinct[6] = { 11.f, 22.f, 33.f, 44.f, 55.f, 66.f };
	const float scrambled_full[6] = { -272.f, 0.5f, 0.5f, -272.f, 0.5f, 0.5f };   /* predicted double conversion */
	const float scrambled_distinct[6] = { 44.f, 55.f, 66.f, 44.f, 55.f, 66.f };

	/* the SDK adapter alone converts correctly (calibration) */
	sceGxmSetViewport(ctx, 11.f, 22.f, 33.f, 44.f, 55.f, 66.f);
	expect("SDK adapter alone, distinct values", distinct);
	/* old path: vitaGL shim -> SDK adapter (what boot-05..08 ran) */
	sceGxmSetViewport_sfp(ctx, 11.f, 22.f, 33.f, 44.f, 55.f, 66.f);
	expect("old path (vitaGL shim + SDK adapter) scrambles as predicted", scrambled_distinct);
	sceGxmSetViewport_sfp(ctx, 480.f, 480.f, 272.f, -272.f, 0.5f, 0.5f);
	expect("old path, full-screen viewport -> xOffset -272, xScale 0.5", scrambled_full);
	/* fix: our wrap -> SDK adapter */
	__wrap_sceGxmSetViewport_sfp(ctx, 11.f, 22.f, 33.f, 44.f, 55.f, 66.f);
	expect("fix, distinct values", distinct);
	__wrap_sceGxmSetViewport_sfp(ctx, 480.f, 480.f, 272.f, -272.f, 0.5f, 0.5f);
	expect("fix, full-screen viewport", full);
	checks++;
	if (rec_ctx != ctx || rec_calls != 5) { fails++; printf("FAIL context/call count (%p, %d)\n", (void *)rec_ctx, rec_calls); }
	printf("%d/%d checks passed (viewport ABI)\n", checks - fails, checks);
	return fails != 0;
}
