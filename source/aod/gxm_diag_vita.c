/*
 * aod-vita DIAGNOSTIC E9: -Wl,--wrap shims for vitaGL's sceGxm* calls (see gxm_diag.h). Every
 * shim calls the real function with the same arguments and returns its result unchanged.
 * This file is part of aod-vita and is distributed under the MIT license.
 */
#include "aod/gxm_diag.h"

#include <psp2/gxm.h>
#include <stdio.h>

int __real_sceGxmBeginScene(SceGxmContext *, unsigned int, const SceGxmRenderTarget *, const SceGxmValidRegion *,
                            SceGxmSyncObject *, SceGxmSyncObject *, const SceGxmColorSurface *, const SceGxmDepthStencilSurface *);
int __wrap_sceGxmBeginScene(SceGxmContext *ctx, unsigned int flags, const SceGxmRenderTarget *rt, const SceGxmValidRegion *vr,
                            SceGxmSyncObject *vs, SceGxmSyncObject *fs, const SceGxmColorSurface *cs, const SceGxmDepthStencilSurface *ds) {
	int rc = __real_sceGxmBeginScene(ctx, flags, rt, vr, vs, fs, cs, ds);
	aod_gxmdiag_scene(rc, cs ? (uintptr_t)sceGxmColorSurfaceGetData(cs) : 0, ds ? (uintptr_t)ds->depthData : 0, (uintptr_t)rt);
	return rc;
}

int __real_sceGxmEndScene(SceGxmContext *, const SceGxmNotification *, const SceGxmNotification *);
int __wrap_sceGxmEndScene(SceGxmContext *ctx, const SceGxmNotification *v, const SceGxmNotification *f) {
	int rc = __real_sceGxmEndScene(ctx, v, f);
	aod_gxmdiag_note(AOD_GXD_END, rc);
	return rc;
}

int __real_sceGxmDraw(SceGxmContext *, SceGxmPrimitiveType, SceGxmIndexFormat, const void *, unsigned int);
int __wrap_sceGxmDraw(SceGxmContext *ctx, SceGxmPrimitiveType p, SceGxmIndexFormat f, const void *i, unsigned int n) {
	int rc = __real_sceGxmDraw(ctx, p, f, i, n);
	aod_gxmdiag_note(AOD_GXD_DRAW, rc);
	return rc;
}

int __real_sceGxmDrawInstanced(SceGxmContext *, SceGxmPrimitiveType, SceGxmIndexFormat, const void *, unsigned int, unsigned int);
int __wrap_sceGxmDrawInstanced(SceGxmContext *ctx, SceGxmPrimitiveType p, SceGxmIndexFormat f, const void *i, unsigned int n, unsigned int w) {
	int rc = __real_sceGxmDrawInstanced(ctx, p, f, i, n, w);
	aod_gxmdiag_note(AOD_GXD_DRAW_INSTANCED, rc);
	return rc;
}

int __real_sceGxmCreateRenderTarget(const SceGxmRenderTargetParams *, SceGxmRenderTarget **);
int __wrap_sceGxmCreateRenderTarget(const SceGxmRenderTargetParams *p, SceGxmRenderTarget **out) {
	int rc = __real_sceGxmCreateRenderTarget(p, out);
	char a[160];
	if (p) snprintf(a, sizeof a, "flags=%#x %ux%u scenes=%u msaa=%u -> rt=%p", (unsigned)p->flags, p->width, p->height, p->scenesPerFrame,
	                p->multisampleMode, out ? (void *)*out : NULL);
	else snprintf(a, sizeof a, "NULL params");
	aod_gxmdiag_create(AOD_GXD_RENDER_TARGET, rc, a);
	return rc;
}

int __real_sceGxmColorSurfaceInit(SceGxmColorSurface *, SceGxmColorFormat, SceGxmColorSurfaceType, SceGxmColorSurfaceScaleMode,
                                  SceGxmOutputRegisterSize, unsigned int, unsigned int, unsigned int, void *);
int __wrap_sceGxmColorSurfaceInit(SceGxmColorSurface *s, SceGxmColorFormat f, SceGxmColorSurfaceType t, SceGxmColorSurfaceScaleMode sc,
                                  SceGxmOutputRegisterSize o, unsigned int w, unsigned int h, unsigned int stride, void *data) {
	int rc = __real_sceGxmColorSurfaceInit(s, f, t, sc, o, w, h, stride, data);
	char a[160];
	snprintf(a, sizeof a, "fmt=%#x type=%#x scale=%#x out=%#x %ux%u stride=%u data=%p", (unsigned)f, (unsigned)t, (unsigned)sc, (unsigned)o, w, h,
	         stride, data);
	aod_gxmdiag_create(AOD_GXD_COLOR_SURFACE, rc, a);
	return rc;
}

int __real_sceGxmDepthStencilSurfaceInit(SceGxmDepthStencilSurface *, SceGxmDepthStencilFormat, SceGxmDepthStencilSurfaceType,
                                         unsigned int, void *, void *);
int __wrap_sceGxmDepthStencilSurfaceInit(SceGxmDepthStencilSurface *s, SceGxmDepthStencilFormat f, SceGxmDepthStencilSurfaceType t,
                                         unsigned int stride, void *d, void *st) {
	int rc = __real_sceGxmDepthStencilSurfaceInit(s, f, t, stride, d, st);
	char a[160];
	snprintf(a, sizeof a, "fmt=%#x type=%#x strideInSamples=%u depth=%p stencil=%p", (unsigned)f, (unsigned)t, stride, d, st);
	aod_gxmdiag_create(AOD_GXD_DEPTH_SURFACE, rc, a);
	return rc;
}

int __real_sceGxmDisplayQueueAddEntry(SceGxmSyncObject *, SceGxmSyncObject *, const void *);
int __wrap_sceGxmDisplayQueueAddEntry(SceGxmSyncObject *o, SceGxmSyncObject *n, const void *cb) {
	int rc = __real_sceGxmDisplayQueueAddEntry(o, n, cb);
	aod_gxmdiag_note(AOD_GXD_DISPLAY_QUEUE, rc);
	return rc;
}

int __real_sceGxmShaderPatcherCreateVertexProgram(SceGxmShaderPatcher *, SceGxmShaderPatcherId, const SceGxmVertexAttribute *, unsigned int,
                                                  const SceGxmVertexStream *, unsigned int, SceGxmVertexProgram **);
int __wrap_sceGxmShaderPatcherCreateVertexProgram(SceGxmShaderPatcher *sp, SceGxmShaderPatcherId id, const SceGxmVertexAttribute *a, unsigned int na,
                                                  const SceGxmVertexStream *s, unsigned int ns, SceGxmVertexProgram **out) {
	int rc = __real_sceGxmShaderPatcherCreateVertexProgram(sp, id, a, na, s, ns, out);
	aod_gxmdiag_note(AOD_GXD_VPROG, rc);
	return rc;
}

int __real_sceGxmShaderPatcherCreateFragmentProgram(SceGxmShaderPatcher *, SceGxmShaderPatcherId, SceGxmOutputRegisterFormat, SceGxmMultisampleMode,
                                                    const SceGxmBlendInfo *, const SceGxmProgram *, SceGxmFragmentProgram **);
int __wrap_sceGxmShaderPatcherCreateFragmentProgram(SceGxmShaderPatcher *sp, SceGxmShaderPatcherId id, SceGxmOutputRegisterFormat of,
                                                    SceGxmMultisampleMode ms, const SceGxmBlendInfo *b, const SceGxmProgram *vp,
                                                    SceGxmFragmentProgram **out) {
	int rc = __real_sceGxmShaderPatcherCreateFragmentProgram(sp, id, of, ms, b, vp, out);
	char a[96];
	if (aod_gxmdiag_create_wanted(AOD_GXD_FPROG)) {
		snprintf(a, sizeof a, "outfmt=%#x msaa=%u", (unsigned)of, (unsigned)ms);
		aod_gxmdiag_create(AOD_GXD_FPROG, rc, a);
	} else
		aod_gxmdiag_note(AOD_GXD_FPROG, rc);
	return rc;
}
