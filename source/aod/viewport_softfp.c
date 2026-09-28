/*
 * aod-vita FIX (boot-08 black screen): single softfp -> hard-float conversion for sceGxmSetViewport.
 *
 * vitaGL built with SOFTFP_ABI=1 (HAVE_SOFTFP_ABI) sends every viewport change through
 * sceGxmSetViewport_sfp, a naked shim that moves the six float arguments from r1-r3/stack into
 * s0-s5 and then branches to sceGxmSetViewport, assuming that is the raw hard-float import. In the
 * nightly-softfp VitaSDK used here, libSceGxm_stub.a already provides sceGxmSetViewport as a
 * softfp -> hard-float adapter (sceGxmSetViewport.o -> __vita_softfp_target_sceGxmSetViewport).
 * The shim reloads r1-r3 from the stack (yScale, zOffset, zScale) before branching, so the adapter
 * converts a second time and GXM receives (yScale, zOffset, zScale, yScale, zOffset, zScale):
 * for vitaGL's full-screen viewport (480, 480, 272, -272, 0.5, 0.5) that is xOffset -272,
 * xScale 0.5, which puts every primitive, clears included, off-screen. sceGxmSetViewport returns
 * void, so nothing reported it (boot-05..boot-08: all GXM calls returned 0, no output at all).
 *
 * Linked with -Wl,--wrap=sceGxmSetViewport_sfp (all 9 vitaGL call sites are outside vgl.o, the
 * object that defines the shim), this forwards the arguments unchanged, in the softfp convention
 * of this build, to the SDK adapter: exactly one conversion. tests/viewport_abi checks both paths
 * against the SDK's own adapter object under qemu-arm.
 *
 * This file is part of aod-vita and is distributed under the MIT license.
 */
typedef struct SceGxmContext SceGxmContext;
void sceGxmSetViewport(SceGxmContext *context, float xOffset, float xScale, float yOffset, float yScale, float zOffset, float zScale);

void __wrap_sceGxmSetViewport_sfp(SceGxmContext *context, float xOffset, float xScale, float yOffset, float yScale, float zOffset,
                                  float zScale) {
	sceGxmSetViewport(context, xOffset, xScale, yOffset, yScale, zOffset, zScale);
}
