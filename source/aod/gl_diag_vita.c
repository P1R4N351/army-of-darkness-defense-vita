/*
 * aod-vita DIAGNOSTIC: binds source/aod/gl_diag.c to vitaGL and the Vita display.
 * This file is part of aod-vita and is distributed under the MIT license.
 */
#include "aod/gl_diag.h"
#include "aod/uniform_remap.h"
#include "aod/gxm_diag.h"
#include "aod/port.h"

#include <psp2/display.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/sysmem.h>
#include <string.h>
#include <stdio.h>
#include <vitaGL.h>

/* vitaGL display surface geometry (gxm.c globals, vitaGL 9c23758); display_queue_callback always
 * hands sceDisplaySetFrameBuf these values with SCE_DISPLAY_PIXELFORMAT_A8B8G8R8. */
extern int DISPLAY_WIDTH, DISPLAY_HEIGHT, DISPLAY_STRIDE;
static int display_geometry(unsigned *w, unsigned *h, unsigned *stride) {
	if (DISPLAY_WIDTH <= 0 || DISPLAY_HEIGHT <= 0 || DISPLAY_STRIDE <= 0) return -1;
	*w = (unsigned)DISPLAY_WIDTH;
	*h = (unsigned)DISPLAY_HEIGHT;
	*stride = (unsigned)DISPLAY_STRIDE;
	return 0;
}

static int mem_block(const void *p, uintptr_t *base, size_t *size, unsigned *memtype) {
	SceKernelMemBlockInfo info;
	memset(&info, 0, sizeof info);
	info.size = sizeof info;
	if (sceKernelGetMemBlockInfoByAddr((void *)p, &info) < 0) return -1;
	*base = (uintptr_t)info.mappedBase;
	*size = info.mappedSize;
	*memtype = (unsigned)info.type;
	return 0;
}

static int query_display(aod_diag_display *out) {
	SceDisplayFrameBuf fb;
	memset(&fb, 0, sizeof fb);
	fb.size = sizeof fb;
	if (sceDisplayGetFrameBuf(&fb, SCE_DISPLAY_SETBUF_NEXTFRAME) < 0) return -1;
	out->base = fb.base;
	out->pitch = fb.pitch;
	out->width = fb.width;
	out->height = fb.height;
	out->format = fb.pixelformat;
	return 0;
}

static int write_file(const char *name, const void *hdr, size_t hdr_len, const void *data, size_t len) {
	char path[256];
	sceIoMkdir(DATA_PATH "diag", 0777);
	snprintf(path, sizeof path, DATA_PATH "diag/%s", name);
	SceUID fd = sceIoOpen(path, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0666);
	if (fd < 0) return -1;
	int ok = sceIoWrite(fd, hdr, hdr_len) == (int)hdr_len && sceIoWrite(fd, data, len) == (int)len;
	sceIoClose(fd);
	return ok ? 0 : -1;
}

static double now_s(void) { return sceKernelGetProcessTimeWide() / 1000000.0; }

void aod_gldiag_install_vitagl(void) {
	aod_gldiag_backend b = {
		.draw_arrays = glDrawArrays, .draw_elements = glDrawElements, .clear = glClear, .clear_color = glClearColor,
		.bind_framebuffer = glBindFramebuffer, .viewport = glViewport, .scissor = glScissor, .enable = glEnable,
		.disable = glDisable, .blend_func = glBlendFunc, .color_mask = glColorMask, .depth_mask = glDepthMask,
		.bind_texture = glBindTexture, .tex_image_2d = glTexImage2D, .tex_sub_image_2d = glTexSubImage2D,
		.compressed_tex_image_2d = glCompressedTexImage2D, .vertex_attrib_pointer = glVertexAttribPointer,
		.enable_vertex_attrib_array = glEnableVertexAttribArray, .get_attrib_location = glGetAttribLocation,
		.use_program = aod_glUseProgram, .bind_buffer = glBindBuffer, .get_error = glGetError,
		.vertex_pointer = glVertexPointer, .enable_client_state = glEnableClientState,
		.depth_func = glDepthFunc, .clear_depthf = glClearDepthf, .cull_face = glCullFace, .active_texture = glActiveTexture,
		.tex_parameteri = glTexParameteri, .pixel_storei = glPixelStorei, .generate_mipmap = glGenerateMipmap,
		.buffer_data = (void (*)(dg_enum, dg_sizeiptr, const void *, dg_enum))glBufferData,
		.buffer_sub_data = (void (*)(dg_enum, dg_intptr, dg_sizeiptr, const void *))glBufferSubData,
		.get_uniform_location = aod_glGetUniformLocation, .u1f = aod_glUniform1f, .u1i = aod_glUniform1i,
		.u2fv = aod_glUniform2fv, .u3fv = aod_glUniform3fv, .u4fv = aod_glUniform4fv, .u2iv = aod_glUniform2iv,
		.um4fv = aod_glUniformMatrix4fv,
		.display_geometry = display_geometry, .mem_block = mem_block, .query_display = query_display, .write_file = write_file, .now_s = now_s,
	};
	aod_gldiag_set_backend(&b);
	/* Opt-in: marker, hand-off samples, dumps, traces and A/B frames cost CPU and I/O every frame. */
	SceIoStat st;
	int on = sceIoGetstat(DATA_PATH "diag.enable", &st) >= 0;
	aod_gldiag_set_enabled(on);
	aod_gxmdiag_set_verbose(on);
	if (on)
		vglSetDisplayCallback(aod_gldiag_display_cb);
	aod_log("diagnostics: %s (create %sdiag.enable to enable)", on ? "ENABLED" : "off", DATA_PATH);
}
