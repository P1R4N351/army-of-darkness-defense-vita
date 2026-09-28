/*
 * aod-vita DIAGNOSTIC layer (boot-05 black screen). Not a fix.
 *
 * Counts and (for a few frames) traces the GL calls libgame makes, without adding GL calls of its
 * own (no glGetError polling: libgame calls glGetError itself). Pixel evidence comes from vitaGL's
 * display-queue callback (vglSetDisplayCallback): it runs on the GXM display-queue thread once
 * the GPU has finished rendering a queued display buffer, immediately before vitaGL passes that
 * buffer to sceDisplaySetFrameBuf. Each callback is matched to the main-loop frame whose swap
 * queued it (FIFO tags). From frame 60's buffer on, a small magenta square is written into each
 * buffer at that point (the same place vitaGL's own debug overlay draws), as a positive control
 * for the path from the handed-off buffer to the panel. Pixels are only touched after the geometry
 * and the enclosing memblock (bounds and non-cacheable type) have been verified. All output is
 * bounded. This is evidence about the buffer handed to the display, not proof of scanout.
 *
 * This file is part of aod-vita and is distributed under the MIT license.
 */
#ifndef AOD_GL_DIAG_H
#define AOD_GL_DIAG_H

#include <stdint.h>
#include <stddef.h>

typedef unsigned int dg_enum;
typedef unsigned int dg_uint;
typedef int dg_int;
typedef int dg_sizei;
typedef unsigned char dg_bool;
typedef float dg_float;
typedef unsigned int dg_bitfield;
typedef intptr_t dg_intptr;
typedef intptr_t dg_sizeiptr;

typedef struct {
	void *base;           /* pixels, 32-bit A8B8G8R8 */
	unsigned pitch;       /* in pixels */
	unsigned width, height;
	unsigned format;      /* 0 = A8B8G8R8 */
} aod_diag_display;

typedef struct aod_gldiag_backend {
	/* real GL (vitaGL) */
	void (*draw_arrays)(dg_enum, dg_int, dg_sizei);
	void (*draw_elements)(dg_enum, dg_sizei, dg_enum, const void *);
	void (*clear)(dg_bitfield);
	void (*clear_color)(dg_float, dg_float, dg_float, dg_float);
	void (*bind_framebuffer)(dg_enum, dg_uint);
	void (*viewport)(dg_int, dg_int, dg_sizei, dg_sizei);
	void (*scissor)(dg_int, dg_int, dg_sizei, dg_sizei);
	void (*enable)(dg_enum);
	void (*disable)(dg_enum);
	void (*blend_func)(dg_enum, dg_enum);
	void (*color_mask)(dg_bool, dg_bool, dg_bool, dg_bool);
	void (*depth_mask)(dg_bool);
	void (*bind_texture)(dg_enum, dg_uint);
	void (*tex_image_2d)(dg_enum, dg_int, dg_int, dg_sizei, dg_sizei, dg_int, dg_enum, dg_enum, const void *);
	void (*tex_sub_image_2d)(dg_enum, dg_int, dg_int, dg_int, dg_sizei, dg_sizei, dg_enum, dg_enum, const void *);
	void (*compressed_tex_image_2d)(dg_enum, dg_int, dg_enum, dg_sizei, dg_sizei, dg_int, dg_sizei, const void *);
	void (*vertex_attrib_pointer)(dg_uint, dg_int, dg_enum, dg_bool, dg_sizei, const void *);
	void (*enable_vertex_attrib_array)(dg_uint);
	dg_int (*get_attrib_location)(dg_uint, const char *);
	void (*use_program)(dg_uint);          /* chains to the uniform remap wrapper */
	void (*bind_buffer)(dg_enum, dg_uint);
	dg_enum (*get_error)(void);
	/* GLES1 client-state path (counted only, to detect whether it is used) */
	void (*vertex_pointer)(dg_int, dg_enum, dg_sizei, const void *);
	void (*enable_client_state)(dg_enum);
	/* E8 value capture (boot-06): pass-through targets */
	void (*depth_func)(dg_enum);
	void (*clear_depthf)(dg_float);
	void (*cull_face)(dg_enum);
	void (*active_texture)(dg_enum);
	void (*tex_parameteri)(dg_enum, dg_enum, dg_int);
	void (*pixel_storei)(dg_enum, dg_int);
	void (*generate_mipmap)(dg_enum);
	void (*buffer_data)(dg_enum, dg_sizeiptr, const void *, dg_enum);
	void (*buffer_sub_data)(dg_enum, dg_intptr, dg_sizeiptr, const void *);
	dg_int (*get_uniform_location)(dg_uint, const char *);   /* uniform remap entry */
	void (*u1f)(dg_int, dg_float);
	void (*u1i)(dg_int, dg_int);
	void (*u2fv)(dg_int, dg_sizei, const dg_float *);
	void (*u3fv)(dg_int, dg_sizei, const dg_float *);
	void (*u4fv)(dg_int, dg_sizei, const dg_float *);
	void (*u2iv)(dg_int, dg_sizei, const dg_int *);
	void (*um4fv)(dg_int, dg_sizei, dg_bool, const dg_float *);
	/* platform */
	int (*display_geometry)(unsigned *width, unsigned *height, unsigned *stride); /* vitaGL display surfaces (A8B8G8R8) */
	int (*mem_block)(const void *p, uintptr_t *base, size_t *size, unsigned *memtype); /* memblock containing p */
	int (*query_display)(aod_diag_display *out);   /* read-only sceDisplayGetFrameBuf parameters, no pixel access */
	int (*write_file)(const char *name, const void *hdr, size_t hdr_len, const void *data, size_t len);
	double (*now_s)(void);
} aod_gldiag_backend;

void aod_gldiag_set_backend(const aod_gldiag_backend *b);

/* Diagnostics are opt-in (boot-10): off unless enabled, e.g. by DATA_PATH "diag.enable" on Vita. */
void aod_gldiag_set_enabled(int on);
int aod_gldiag_enabled(void);

/* Frame hooks from the main loop. */
void aod_gldiag_frame_begin(unsigned long frame);
void aod_gldiag_before_swap(unsigned long frame); /* tags the display-queue entry this frame's swap creates */
void aod_gldiag_frame_end(unsigned long frame);   /* after the swap */

/* vglSetDisplayCallback target: GXM display-queue thread, GPU done with framebuf, before
 * sceDisplaySetFrameBuf(framebuf). */
void aod_gldiag_display_cb(void *framebuf);

/* Memblock types whose CPU mapping is non-cacheable (psp2common/kernel/sysmem.h); the display
 * buffers are only read or written when they lie inside one block of these types. */
#define AOD_DIAG_MEMTYPE_CDRAM_RW        0x09408060u
#define AOD_DIAG_MEMTYPE_RW_UNCACHE      0x0C208060u
#define AOD_DIAG_MEMTYPE_PHYCONT_NC_RW   0x0D808060u
#define AOD_DIAG_MEMTYPE_CDIALOG_NC_RW   0x0CA08060u
int aod_gldiag_memtype_uncached(unsigned memtype);

/* Bounds (also used by tests). */
#define AOD_DIAG_TRACE_LINES_PER_FRAME 500
#define AOD_DIAG_TEX_LOG_MAX 150
#define AOD_DIAG_ATTRIB_LOG_MAX 64
#define AOD_DIAG_MARKER_FROM_FRAME 60
#define AOD_DIAG_MARKER_SIZE 32
#define AOD_DIAG_MARKER_X 8
#define AOD_DIAG_MARKER_Y 8
#define AOD_DIAG_SAMPLE_STEP 4
#define AOD_DIAG_ERR_LOG_MAX 32
#define AOD_DIAG_DUMP_MAX_BYTES (1024u * 544u * 4u)  /* stride 1024 max at 544 lines */
#define AOD_DIAG_DUMP_VERSION 2
/* E8 (boot-06 follow-up): value capture and single-factor A/B frames */
#define AOD_DIAG_STATE_LOG_MAX 64      /* depth/cull/texparam/pixelstore/mipmap/activetex calls logged outside traces */
#define AOD_DIAG_BUFFER_LOG_MAX 48     /* glBufferData/SubData calls logged with decoded head */
#define AOD_DIAG_UNIFORM_NAME_MAX 128  /* (program, id, name) table */
#define AOD_DIAG_TEXSTAT_MAX 32        /* texture uploads with source-pixel statistics */
#define AOD_DIAG_TEXSTAT_SAMPLES 4096
enum { AOD_DIAG_ARM_NONE = 0, AOD_DIAG_ARM_DEPTH_OFF = 1, AOD_DIAG_ARM_BLEND_OFF = 2, AOD_DIAG_ARM_MAGENTA_CLEAR = 4 };
int aod_gldiag_arm(unsigned long frame);   /* A/B arm for a frame (bit set) */
/* E9 (boot-07 follow-up): GL clear positive control outside the game. P1 right after gl_init,
 * P2 right after nativeCreate; two swapped frames each, tagged with these frame numbers. */
#define AOD_DIAG_PREFLIGHT_P1 1000000ul
#define AOD_DIAG_PREFLIGHT_P2 1000010ul
#define AOD_DIAG_PREFLIGHT_ABGR 0xFF00FF00u   /* opaque green */
int aod_gldiag_is_preflight(unsigned long frame);
void aod_gldiag_preflight_clear(void);
#define AOD_DIAG_MARKER_ABGR 0xFFFF00FFu   /* magenta, opaque */

int aod_gldiag_is_trace_frame(unsigned long frame);
int aod_gldiag_is_summary_frame(unsigned long frame);
int aod_gldiag_is_dump_frame(unsigned long frame);

/* Game-facing wrappers (import table). */
void aod_diag_glDrawArrays(dg_enum mode, dg_int first, dg_sizei count);
void aod_diag_glDrawElements(dg_enum mode, dg_sizei count, dg_enum type, const void *idx);
void aod_diag_glClear(dg_bitfield mask);
void aod_diag_glClearColor(dg_float r, dg_float g, dg_float b, dg_float a);
void aod_diag_glBindFramebuffer(dg_enum target, dg_uint fb);
void aod_diag_glViewport(dg_int x, dg_int y, dg_sizei w, dg_sizei h);
void aod_diag_glScissor(dg_int x, dg_int y, dg_sizei w, dg_sizei h);
void aod_diag_glEnable(dg_enum cap);
void aod_diag_glDisable(dg_enum cap);
void aod_diag_glBlendFunc(dg_enum s, dg_enum d);
void aod_diag_glColorMask(dg_bool r, dg_bool g, dg_bool b, dg_bool a);
void aod_diag_glDepthMask(dg_bool f);
void aod_diag_glBindTexture(dg_enum target, dg_uint tex);
void aod_diag_glTexImage2D(dg_enum t, dg_int lvl, dg_int ifmt, dg_sizei w, dg_sizei h, dg_int border, dg_enum fmt, dg_enum type, const void *px);
void aod_diag_glTexSubImage2D(dg_enum t, dg_int lvl, dg_int x, dg_int y, dg_sizei w, dg_sizei h, dg_enum fmt, dg_enum type, const void *px);
void aod_diag_glCompressedTexImage2D(dg_enum t, dg_int lvl, dg_enum ifmt, dg_sizei w, dg_sizei h, dg_int border, dg_sizei size, const void *data);
void aod_diag_glVertexAttribPointer(dg_uint idx, dg_int size, dg_enum type, dg_bool norm, dg_sizei stride, const void *ptr);
void aod_diag_glEnableVertexAttribArray(dg_uint idx);
dg_int aod_diag_glGetAttribLocation(dg_uint prog, const char *name);
void aod_diag_glUseProgram(dg_uint prog);
void aod_diag_glBindBuffer(dg_enum target, dg_uint buf);
void aod_diag_glVertexPointer(dg_int size, dg_enum type, dg_sizei stride, const void *ptr);
void aod_diag_glEnableClientState(dg_enum cap);
dg_enum aod_diag_glGetError(void);
void aod_diag_glDepthFunc(dg_enum f);
void aod_diag_glClearDepthf(dg_float d);
void aod_diag_glCullFace(dg_enum m);
void aod_diag_glActiveTexture(dg_enum t);
void aod_diag_glTexParameteri(dg_enum t, dg_enum p, dg_int v);
void aod_diag_glPixelStorei(dg_enum p, dg_int v);
void aod_diag_glGenerateMipmap(dg_enum t);
void aod_diag_glBufferData(dg_enum t, dg_sizeiptr n, const void *d, dg_enum usage);
void aod_diag_glBufferSubData(dg_enum t, dg_intptr off, dg_sizeiptr n, const void *d);
dg_int aod_diag_glGetUniformLocation(dg_uint prog, const char *name);
void aod_diag_glUniform1f(dg_int l, dg_float v);
void aod_diag_glUniform1i(dg_int l, dg_int v);
void aod_diag_glUniform2fv(dg_int l, dg_sizei n, const dg_float *v);
void aod_diag_glUniform3fv(dg_int l, dg_sizei n, const dg_float *v);
void aod_diag_glUniform4fv(dg_int l, dg_sizei n, const dg_float *v);
void aod_diag_glUniform2iv(dg_int l, dg_sizei n, const dg_int *v);
void aod_diag_glUniformMatrix4fv(dg_int l, dg_sizei n, dg_bool t, const dg_float *v);

#endif
