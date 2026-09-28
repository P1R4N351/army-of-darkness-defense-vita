/*
 * aod-vita DIAGNOSTIC layer (boot-05 black screen). See gl_diag.h. Not a fix.
 * GL wrappers and frame hooks run on the main thread (libgame renders there);
 * aod_gldiag_display_cb runs on the GXM display-queue thread and shares only the atomics and the
 * slot/dump hand-over below with it.
 *
 * This file is part of aod-vita and is distributed under the MIT license.
 */
#include "aod/gl_diag.h"
#include "aod/gxm_diag.h"
#include "aod/port.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static aod_gldiag_backend be;
void aod_gldiag_set_backend(const aod_gldiag_backend *b) { be = *b; }

/* Opt-in (boot-10): off in normal builds. When off, the wrappers only pass through and count;
 * no traces, value logs, A/B arms, display-callback marker/samples/dumps or per-frame summaries. */
static int enabled;
void aod_gldiag_set_enabled(int on) { enabled = on != 0; }
int aod_gldiag_enabled(void) { return enabled; }

#define LOAD(p) __atomic_load_n((p), __ATOMIC_ACQUIRE)
#define STORE(p, v) __atomic_store_n((p), (v), __ATOMIC_RELEASE)
#define INC(p) __atomic_fetch_add((p), 1, __ATOMIC_RELAXED)

/* ---- per-frame counters (main thread) ---- */
typedef struct {
	unsigned draws, draws_fbo, draw_verts, clears, clears_fbo, fb_bind_zero, fb_bind_other;
	unsigned tex_uploads, tex_sub, tex_compressed, programs, attrib_ptrs, gles1_calls, gl_errors;
	unsigned blend_on_draws, depth_on_draws, cull_on_draws, scissor_on_draws, colormask_off_draws;
} counters;

static counters fc;             /* current frame */
static unsigned long cur_frame;
static int in_frame;
static unsigned trace_lines;
static unsigned tex_logged, attrib_logged, err_logged;
/* tracked state */
static dg_uint cur_fb, cur_prog, cur_tex;
static int st_blend, st_depth, st_cull, st_scissor, st_colormask_all = 1;
static dg_int vp[4];
static float cc[4];

int aod_gldiag_is_trace_frame(unsigned long f) { return f <= 4 || f == 300; }
int aod_gldiag_is_summary_frame(unsigned long f) {
	return f <= 5 || f == 60 || f == 120 || f == 300 || f == 600 || f == 1800 || f == 3600 ||
	       (f >= 400 && f <= 450 && f % 10 == 0) || aod_gldiag_is_preflight(f);
}
/* E8 A/B frames: A0 baseline 400/440; single-factor arms A1 410 (depth), A2 420 (blend), A4 450 (magenta
 * clear); A3 430 is a two-factor interaction arm (depth and blend), read against A1 and A2. */
int aod_gldiag_arm(unsigned long f) {
	if (!enabled)
		return AOD_DIAG_ARM_NONE;
	switch (f) {
	case 410: return AOD_DIAG_ARM_DEPTH_OFF;
	case 420: return AOD_DIAG_ARM_BLEND_OFF;
	case 430: return AOD_DIAG_ARM_DEPTH_OFF | AOD_DIAG_ARM_BLEND_OFF;
	case 450: return AOD_DIAG_ARM_MAGENTA_CLEAR;
	default: return AOD_DIAG_ARM_NONE;
	}
}
int aod_gldiag_is_preflight(unsigned long f) {
	return f == AOD_DIAG_PREFLIGHT_P1 || f == AOD_DIAG_PREFLIGHT_P1 + 1 || f == AOD_DIAG_PREFLIGHT_P2 || f == AOD_DIAG_PREFLIGHT_P2 + 1;
}
int aod_gldiag_is_dump_frame(unsigned long f) {
	return f == AOD_DIAG_PREFLIGHT_P1 + 1 || f == AOD_DIAG_PREFLIGHT_P2 + 1 || f == 120 || f == 600 || f == 1800 || (f >= 400 && f <= 450 && f % 10 == 0);
}

int aod_gldiag_memtype_uncached(unsigned t) {
	return t == AOD_DIAG_MEMTYPE_CDRAM_RW || t == AOD_DIAG_MEMTYPE_RW_UNCACHE || t == AOD_DIAG_MEMTYPE_PHYCONT_NC_RW ||
	       t == AOD_DIAG_MEMTYPE_CDIALOG_NC_RW;
}

static int tracing(void) { return enabled && in_frame && aod_gldiag_is_trace_frame(cur_frame); }

static void trace(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void trace(const char *fmt, ...) {
	if (!tracing()) return;
	if (trace_lines >= AOD_DIAG_TRACE_LINES_PER_FRAME) {
		if (trace_lines++ == AOD_DIAG_TRACE_LINES_PER_FRAME) aod_log("DIAG f%lu trace: line cap reached", cur_frame);
		return;
	}
	trace_lines++;
	char buf[256];
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(buf, sizeof buf, fmt, ap);
	va_end(ap);
	aod_log("DIAG f%lu %s", cur_frame, buf);
}

/* ---- wrappers (pure pass-through plus counting; no extra GL calls) ---- */
#define GL_BLEND_ 0x0BE2
#define GL_DEPTH_TEST_ 0x0B71
#define GL_CULL_FACE_ 0x0B44
#define GL_SCISSOR_TEST_ 0x0C11

/* A/B arms: the factor is switched off around each draw and restored right after it */
static unsigned arm_modified;
static int arm_before(void) {
	int arm = in_frame ? aod_gldiag_arm(cur_frame) : 0, undo = 0;
	if ((arm & AOD_DIAG_ARM_DEPTH_OFF) && st_depth) { be.disable(GL_DEPTH_TEST_); undo |= AOD_DIAG_ARM_DEPTH_OFF; }
	if ((arm & AOD_DIAG_ARM_BLEND_OFF) && st_blend) { be.disable(GL_BLEND_); undo |= AOD_DIAG_ARM_BLEND_OFF; }
	if (undo) arm_modified++;
	return undo;
}
static void arm_after(int undo) {
	if (undo & AOD_DIAG_ARM_DEPTH_OFF) be.enable(GL_DEPTH_TEST_);
	if (undo & AOD_DIAG_ARM_BLEND_OFF) be.enable(GL_BLEND_);
}

static void note_draw(void) {
	fc.draws++;
	if (cur_fb) fc.draws_fbo++;
	if (st_blend) fc.blend_on_draws++;
	if (st_depth) fc.depth_on_draws++;
	if (st_cull) fc.cull_on_draws++;
	if (st_scissor) fc.scissor_on_draws++;
	if (!st_colormask_all) fc.colormask_off_draws++;
}

void aod_diag_glDrawArrays(dg_enum mode, dg_int first, dg_sizei count) {
	note_draw();
	fc.draw_verts += count > 0 ? (unsigned)count : 0;
	int undo = arm_before();
	be.draw_arrays(mode, first, count);
	arm_after(undo);
	trace("DrawArrays mode=%#x first=%d n=%d prog=%u fb=%#x tex=%u blend=%d depth=%d cull=%d", mode, first, count,
	      cur_prog, cur_fb, cur_tex, st_blend, st_depth, st_cull);
}
void aod_diag_glDrawElements(dg_enum mode, dg_sizei count, dg_enum type, const void *idx) {
	note_draw();
	fc.draw_verts += count > 0 ? (unsigned)count : 0;
	int undo = arm_before();
	be.draw_elements(mode, count, type, idx);
	arm_after(undo);
	trace("DrawElements mode=%#x n=%d type=%#x idx=%p prog=%u fb=%#x tex=%u blend=%d depth=%d cull=%d", mode, count, type,
	      idx, cur_prog, cur_fb, cur_tex, st_blend, st_depth, st_cull);
}
void aod_diag_glClear(dg_bitfield mask) {
	fc.clears++;
	if (cur_fb) fc.clears_fbo++;
	if (in_frame && (aod_gldiag_arm(cur_frame) & AOD_DIAG_ARM_MAGENTA_CLEAR) && (mask & 0x4000u)) {
		be.clear_color(1.0f, 0.0f, 1.0f, 1.0f);
		be.clear(mask);
		be.clear_color(cc[0], cc[1], cc[2], cc[3]);
		arm_modified++;
	} else
		be.clear(mask);
	trace("Clear mask=%#x fb=%#x color=(%.2f,%.2f,%.2f,%.2f) vp=%d,%d,%d,%d", mask, cur_fb, cc[0], cc[1], cc[2], cc[3], vp[0], vp[1], vp[2], vp[3]);
}
void aod_diag_glClearColor(dg_float r, dg_float g, dg_float b, dg_float a) {
	cc[0] = r; cc[1] = g; cc[2] = b; cc[3] = a;
	be.clear_color(r, g, b, a);
}
void aod_diag_glBindFramebuffer(dg_enum target, dg_uint fb) {
	if (fb) fc.fb_bind_other++; else fc.fb_bind_zero++;
	cur_fb = fb;
	be.bind_framebuffer(target, fb);
	trace("BindFramebuffer target=%#x fb=%#x", target, fb);
}
void aod_diag_glViewport(dg_int x, dg_int y, dg_sizei w, dg_sizei h) {
	vp[0] = x; vp[1] = y; vp[2] = w; vp[3] = h;
	be.viewport(x, y, w, h);
	trace("Viewport %d,%d %dx%d fb=%#x", x, y, w, h, cur_fb);
}
void aod_diag_glScissor(dg_int x, dg_int y, dg_sizei w, dg_sizei h) {
	be.scissor(x, y, w, h);
	trace("Scissor %d,%d %dx%d", x, y, w, h);
}
static void set_cap(dg_enum cap, int on) {
	if (cap == GL_BLEND_) st_blend = on;
	else if (cap == GL_DEPTH_TEST_) st_depth = on;
	else if (cap == GL_CULL_FACE_) st_cull = on;
	else if (cap == GL_SCISSOR_TEST_) st_scissor = on;
}
void aod_diag_glEnable(dg_enum cap) { set_cap(cap, 1); be.enable(cap); trace("Enable %#x", cap); }
void aod_diag_glDisable(dg_enum cap) { set_cap(cap, 0); be.disable(cap); trace("Disable %#x", cap); }
void aod_diag_glBlendFunc(dg_enum s, dg_enum d) { be.blend_func(s, d); trace("BlendFunc %#x %#x", s, d); }
void aod_diag_glColorMask(dg_bool r, dg_bool g, dg_bool b, dg_bool a) {
	st_colormask_all = r && g && b && a;
	be.color_mask(r, g, b, a);
	trace("ColorMask %d%d%d%d", r, g, b, a);
}
void aod_diag_glDepthMask(dg_bool f) { be.depth_mask(f); trace("DepthMask %d", f); }
void aod_diag_glBindTexture(dg_enum target, dg_uint tex) { cur_tex = tex; be.bind_texture(target, tex); }
/* Source-pixel statistics of an upload (CPU memory the game passes; read within the GL contract:
 * (h-1) rows of the unpack stride plus one packed row). */
static unsigned tex_stats_logged, unpack_align = 4;
static void tex_stats(const char *what, dg_sizei w, dg_sizei h, dg_enum fmt, dg_enum type, const void *px) {
	unsigned bpp = fmt == 0x1908 ? 4 : fmt == 0x1907 ? 3 : fmt == 0x190A ? 2 : (fmt == 0x1906 || fmt == 0x1909) ? 1 : 0;
	if (!enabled || tex_stats_logged >= AOD_DIAG_TEXSTAT_MAX || !px || type != 0x1401 || !bpp || w <= 0 || h <= 0) return;
	tex_stats_logged++;
	size_t row = ((size_t)w * bpp + unpack_align - 1) / unpack_align * unpack_align;
	size_t n = (size_t)w * h, S = n < AOD_DIAG_TEXSTAT_SAMPLES ? n : AOD_DIAG_TEXSTAT_SAMPLES;
	unsigned a0 = 0, a255 = 0, rgb = 0;
	unsigned long long sr = 0, sg = 0, sb = 0, sa = 0;
	for (size_t k = 0; k < S; k++) {
		size_t i = k * n / S, x = i % (size_t)w, y = i / (size_t)w;
		const unsigned char *p = (const unsigned char *)px + y * row + x * bpp;
		unsigned r = p[0], g = bpp >= 3 ? p[1] : p[0], b = bpp >= 3 ? p[2] : p[0];
		unsigned a = bpp == 4 ? p[3] : bpp == 2 ? p[1] : fmt == 0x1906 ? p[0] : 255;
		if (fmt == 0x1906) r = g = b = 0;
		a0 += a == 0; a255 += a == 255; rgb += r > 8 || g > 8 || b > 8;
		sr += r; sg += g; sb += b; sa += a;
	}
	aod_log("DIAG texstat f%lu %s tex=%u %dx%d fmt=%#x: %zu samples alpha0=%u alpha255=%u rgb-nonblack=%u avg=(%llu,%llu,%llu,%llu)",
	        cur_frame, what, cur_tex, w, h, fmt, S, a0, a255, rgb, sr / S, sg / S, sb / S, sa / S);
}

void aod_diag_glTexImage2D(dg_enum t, dg_int lvl, dg_int ifmt, dg_sizei w, dg_sizei h, dg_int border, dg_enum fmt, dg_enum type, const void *px) {
	fc.tex_uploads++;
	be.tex_image_2d(t, lvl, ifmt, w, h, border, fmt, type, px);
	if (lvl == 0) tex_stats("TexImage2D", w, h, fmt, type, px);
	if (enabled && tex_logged < AOD_DIAG_TEX_LOG_MAX) {
		tex_logged++;
		aod_log("DIAG tex#%u f%lu TexImage2D tex=%u lvl=%d ifmt=%#x %dx%d fmt=%#x type=%#x data=%s",
		        tex_logged, cur_frame, cur_tex, lvl, ifmt, w, h, fmt, type, px ? "yes" : "null");
	}
}
void aod_diag_glTexSubImage2D(dg_enum t, dg_int lvl, dg_int x, dg_int y, dg_sizei w, dg_sizei h, dg_enum fmt, dg_enum type, const void *px) {
	fc.tex_sub++;
	be.tex_sub_image_2d(t, lvl, x, y, w, h, fmt, type, px);
	if (lvl == 0) tex_stats("TexSubImage2D", w, h, fmt, type, px);
	if (enabled && tex_logged < AOD_DIAG_TEX_LOG_MAX) {
		tex_logged++;
		aod_log("DIAG tex#%u f%lu TexSubImage2D tex=%u lvl=%d at %d,%d %dx%d fmt=%#x type=%#x",
		        tex_logged, cur_frame, cur_tex, lvl, x, y, w, h, fmt, type);
	}
}
void aod_diag_glCompressedTexImage2D(dg_enum t, dg_int lvl, dg_enum ifmt, dg_sizei w, dg_sizei h, dg_int border, dg_sizei size, const void *data) {
	fc.tex_compressed++;
	be.compressed_tex_image_2d(t, lvl, ifmt, w, h, border, size, data);
	if (enabled && tex_logged < AOD_DIAG_TEX_LOG_MAX) {
		tex_logged++;
		aod_log("DIAG tex#%u f%lu CompressedTexImage2D tex=%u lvl=%d ifmt=%#x %dx%d size=%d",
		        tex_logged, cur_frame, cur_tex, lvl, ifmt, w, h, size);
	}
}
void aod_diag_glVertexAttribPointer(dg_uint idx, dg_int size, dg_enum type, dg_bool norm, dg_sizei stride, const void *ptr) {
	fc.attrib_ptrs++;
	be.vertex_attrib_pointer(idx, size, type, norm, stride, ptr);
	trace("VertexAttribPointer idx=%u size=%d type=%#x norm=%d stride=%d ptr=%p", idx, size, type, norm, stride, ptr);
}
void aod_diag_glEnableVertexAttribArray(dg_uint idx) { be.enable_vertex_attrib_array(idx); trace("EnableVertexAttribArray %u", idx); }
dg_int aod_diag_glGetAttribLocation(dg_uint prog, const char *name) {
	dg_int r = be.get_attrib_location(prog, name);
	if (enabled && attrib_logged < AOD_DIAG_ATTRIB_LOG_MAX) {
		attrib_logged++;
		aod_log("DIAG GetAttribLocation prog=%u '%s' -> %d", prog, name ? name : "(null)", r);
	}
	return r;
}
void aod_diag_glUseProgram(dg_uint prog) { fc.programs++; cur_prog = prog; be.use_program(prog); trace("UseProgram %u", prog); }
static dg_uint cur_array_buf, cur_elem_buf;
void aod_diag_glBindBuffer(dg_enum target, dg_uint buf) {
	if (target == 0x8892) cur_array_buf = buf;
	else if (target == 0x8893) cur_elem_buf = buf;
	be.bind_buffer(target, buf);
	trace("BindBuffer %#x %u", target, buf);
}
void aod_diag_glVertexPointer(dg_int size, dg_enum type, dg_sizei stride, const void *ptr) {
	fc.gles1_calls++;
	be.vertex_pointer(size, type, stride, ptr);
	trace("GLES1 VertexPointer size=%d type=%#x stride=%d", size, type, stride);
}
void aod_diag_glEnableClientState(dg_enum cap) { fc.gles1_calls++; be.enable_client_state(cap); trace("GLES1 EnableClientState %#x", cap); }

/* libgame's own glGetError calls: value passed through unchanged, nonzero results counted.
 * vitaGL is built with NO_DEBUG=1 (SKIP_ERROR_HANDLING), so most GL errors are never raised. */
dg_enum aod_diag_glGetError(void) {
	dg_enum e = be.get_error();
	if (e) {
		fc.gl_errors++;
		if (err_logged < AOD_DIAG_ERR_LOG_MAX) {
			err_logged++;
			aod_log("DIAG f%lu game glGetError -> %#x", cur_frame, e);
		}
	}
	return e;
}

/* ---- E8 value capture: state calls, buffer contents, uniform names and values ---- */
static unsigned state_logged;
#define STATE_LOG(...) do { \
	if (tracing()) trace(__VA_ARGS__); \
	else if (enabled && state_logged < AOD_DIAG_STATE_LOG_MAX) { state_logged++; char b_[160]; snprintf(b_, sizeof b_, __VA_ARGS__); aod_log("DIAG f%lu %s", cur_frame, b_); } \
} while (0)
void aod_diag_glDepthFunc(dg_enum f) { be.depth_func(f); STATE_LOG("DepthFunc %#x", f); }
void aod_diag_glClearDepthf(dg_float d) { be.clear_depthf(d); STATE_LOG("ClearDepthf %g", (double)d); }
void aod_diag_glCullFace(dg_enum m) { be.cull_face(m); STATE_LOG("CullFace %#x", m); }
void aod_diag_glActiveTexture(dg_enum t) { be.active_texture(t); STATE_LOG("ActiveTexture %#x", t); }
void aod_diag_glTexParameteri(dg_enum t, dg_enum p, dg_int v) { be.tex_parameteri(t, p, v); STATE_LOG("TexParameteri tex=%u %#x=%#x", cur_tex, p, (unsigned)v); }
void aod_diag_glPixelStorei(dg_enum p, dg_int v) {
	if (p == 0x0CF5 && (v == 1 || v == 2 || v == 4 || v == 8)) unpack_align = (unsigned)v; /* GL_UNPACK_ALIGNMENT */
	be.pixel_storei(p, v);
	STATE_LOG("PixelStorei %#x=%d", p, v);
}
void aod_diag_glGenerateMipmap(dg_enum t) { be.generate_mipmap(t); STATE_LOG("GenerateMipmap tex=%u", cur_tex); }

static unsigned buf_logged;
static void log_buffer(const char *what, dg_enum t, long off, long n, const void *d) {
	if (!enabled)
		return;
	if (!tracing() && buf_logged >= AOD_DIAG_BUFFER_LOG_MAX) return;
	if (!tracing()) buf_logged++;
	char head[200];
	int o = 0;
	head[0] = 0;
	/* GL buffer payloads are byte pointers with no alignment guarantee: decode through memcpy */
	const unsigned char *bytes = (const unsigned char *)d;
	if (d && n > 0 && t == 0x8892)
		for (long i = 0; i < n / 4 && i < 16 && o < (int)sizeof head - 16; i++) {
			float f;
			memcpy(&f, bytes + 4 * i, sizeof f);
			o += snprintf(head + o, sizeof head - o, " %.4g", (double)f);
		}
	else if (d && n > 0 && t == 0x8893)
		for (long i = 0; i < n / 2 && i < 12 && o < (int)sizeof head - 8; i++) {
			unsigned short u;
			memcpy(&u, bytes + 2 * i, sizeof u);
			o += snprintf(head + o, sizeof head - o, " %u", u);
		}
	aod_log("DIAG f%lu %s target=%#x buf=%u off=%ld size=%ld head:%s", cur_frame, what, t, t == 0x8893 ? cur_elem_buf : cur_array_buf, off, n,
	        d ? head : " (null)");
}
void aod_diag_glBufferData(dg_enum t, dg_sizeiptr n, const void *d, dg_enum usage) { be.buffer_data(t, n, d, usage); log_buffer("BufferData", t, 0, (long)n, d); }
void aod_diag_glBufferSubData(dg_enum t, dg_intptr off, dg_sizeiptr n, const void *d) { be.buffer_sub_data(t, off, n, d); log_buffer("BufferSubData", t, (long)off, (long)n, d); }

typedef struct { dg_uint prog; dg_int id; char name[48]; } uname;
static uname unames[AOD_DIAG_UNIFORM_NAME_MAX];
static unsigned n_unames;
dg_int aod_diag_glGetUniformLocation(dg_uint prog, const char *name) {
	dg_int r = be.get_uniform_location(prog, name);
	if (enabled && n_unames < AOD_DIAG_UNIFORM_NAME_MAX) {
		unames[n_unames].prog = prog; unames[n_unames].id = r;
		snprintf(unames[n_unames].name, sizeof unames[0].name, "%s", name ? name : "(null)");
		n_unames++;
		aod_log("DIAG uniform prog=%u '%s' -> id %d", prog, name ? name : "(null)", r);
	}
	return r;
}
static const char *uniform_name(dg_int id) {
	for (unsigned i = 0; i < n_unames; i++)
		if (unames[i].prog == cur_prog && unames[i].id == id) return unames[i].name;
	return "?";
}
static void log_floats(const char *fn, dg_int l, dg_sizei n, int per, const dg_float *v) {
	if (!tracing()) return;
	char b[300];
	/* GL ignores location -1 (and n <= 0) without reading the array, so the logger does not read it either */
	if (l == -1 || n <= 0 || !v) {
		trace("%s prog=%u (id %d) n=%d: not read (%s)", fn, cur_prog, l, n, l == -1 ? "location -1, ignored by GL" : "no values");
		return;
	}
	/* clamp the element count before multiplying: n is caller-controlled */
	int cnt = n > 16 ? 16 : (int)n, total = cnt * per, o = 0;
	if (total > 16) total = 16;
	b[0] = 0;
	for (int i = 0; i < total && o < (int)sizeof b - 16; i++) {
		float f;
		memcpy(&f, v + i, sizeof f);
		o += snprintf(b + o, sizeof b - o, " %.4g", (double)f);
	}
	trace("%s prog=%u %s(id %d) n=%d:%s", fn, cur_prog, uniform_name(l), l, n, b);
}
void aod_diag_glUniform1f(dg_int l, dg_float v) { be.u1f(l, v); log_floats("Uniform1f", l, 1, 1, &v); }
void aod_diag_glUniform1i(dg_int l, dg_int v) { be.u1i(l, v); if (tracing()) trace("Uniform1i prog=%u %s(id %d) = %d", cur_prog, uniform_name(l), l, v); }
void aod_diag_glUniform2fv(dg_int l, dg_sizei n, const dg_float *v) { be.u2fv(l, n, v); log_floats("Uniform2fv", l, n, 2, v); }
void aod_diag_glUniform3fv(dg_int l, dg_sizei n, const dg_float *v) { be.u3fv(l, n, v); log_floats("Uniform3fv", l, n, 3, v); }
void aod_diag_glUniform4fv(dg_int l, dg_sizei n, const dg_float *v) { be.u4fv(l, n, v); log_floats("Uniform4fv", l, n, 4, v); }
void aod_diag_glUniform2iv(dg_int l, dg_sizei n, const dg_int *v) {
	be.u2iv(l, n, v);
	if (!tracing()) return;
	if (l == -1 || n <= 0 || !v) {
		trace("Uniform2iv prog=%u (id %d) n=%d: not read (%s)", cur_prog, l, n, l == -1 ? "location -1, ignored by GL" : "no values");
		return;
	}
	dg_int iv[2];
	memcpy(iv, v, sizeof iv);
	trace("Uniform2iv prog=%u %s(id %d) n=%d: %d %d", cur_prog, uniform_name(l), l, n, iv[0], iv[1]);
}
void aod_diag_glUniformMatrix4fv(dg_int l, dg_sizei n, dg_bool t, const dg_float *v) {
	be.um4fv(l, n, t, v);
	if (tracing() && t) trace("UniformMatrix4fv transpose=%d", t);
	log_floats("UniformMatrix4fv", l, n, 16, v);
}

/* ---- display-queue evidence ----
 * Main thread: before_swap() pushes the frame number; vitaGL queues exactly one display entry per
 * vglSwapBuffers and the GXM display queue calls back in submission order, so the callback pops
 * the tag of the frame whose buffer it is handed. Single producer, single consumer. */
#define TAG_RING 16
#define UNTAGGED (~0ul)
static unsigned long tag_ring[TAG_RING];
static unsigned tag_head, tag_tail;   /* head: main, tail: callback */
static unsigned swaps, tag_overflow;  /* main only */

/* callback-side counters (read by main) */
static unsigned cb_count, cb_untagged, cb_bad_geometry, cb_bad_memblock, cb_cached_memblock, marker_writes, samples_dropped;
static unsigned long marker_onset = UNTAGGED;

#define MAX_SURFACES 4
typedef struct { uintptr_t addr; int ok; unsigned memtype; uintptr_t blk_base; size_t blk_size; } surface_check;
static surface_check surfaces[MAX_SURFACES]; /* written by callback; entry published via n_surfaces */
static unsigned n_surfaces;

typedef struct {
	unsigned long tag;
	unsigned seq, samples, nonblack, marker_rect_magenta, marker_rect_samples;
	unsigned minx, miny, maxx, maxy;
	unsigned long long r, g, b;
	uintptr_t fb;
	unsigned w, h, stride, memtype;
	double t;
} sample_slot;
#define SLOTS 8
static sample_slot slots[SLOTS];
static int slot_state[SLOTS]; /* 0 free (main owns), 1 ready (published by callback) */

static uint32_t dump_px[AOD_DIAG_DUMP_MAX_BYTES / 4];
static int dump_state;        /* 0 free, 1 ready */
static unsigned long dump_tag;
static unsigned dump_w, dump_h, dump_stride;
static unsigned dumps_dropped;
static unsigned long last_dump_dropped = UNTAGGED; /* tag of the most recent dropped dump (callback -> main) */

void aod_gldiag_before_swap(unsigned long frame) {
	swaps++;
	unsigned tail = LOAD(&tag_tail);
	if (tag_head - tail >= TAG_RING) { tag_overflow++; return; }
	tag_ring[tag_head % TAG_RING] = frame;
	STORE(&tag_head, tag_head + 1);
}

static unsigned long pop_tag(void) {
	unsigned head = LOAD(&tag_head);
	if (tag_tail == head) return UNTAGGED;
	unsigned long t = tag_ring[tag_tail % TAG_RING];
	STORE(&tag_tail, tag_tail + 1);
	return t;
}

static int in_marker_rect(unsigned x, unsigned y) {
	return x >= AOD_DIAG_MARKER_X && x < AOD_DIAG_MARKER_X + AOD_DIAG_MARKER_SIZE && y >= AOD_DIAG_MARKER_Y &&
	       y < AOD_DIAG_MARKER_Y + AOD_DIAG_MARKER_SIZE;
}

/* Verify once per distinct buffer address: whole buffer inside one non-cacheable memblock. */
static const surface_check *check_surface(uintptr_t fb, size_t len) {
	unsigned n = LOAD(&n_surfaces);
	for (unsigned i = 0; i < n; i++)
		if (surfaces[i].addr == fb) return &surfaces[i];
	surface_check c = { fb, 0, 0, 0, 0 };
	if (be.mem_block && be.mem_block((const void *)fb, &c.blk_base, &c.blk_size, &c.memtype) == 0 && fb >= c.blk_base &&
	    len <= c.blk_size && fb - c.blk_base <= c.blk_size - len) {
		c.ok = aod_gldiag_memtype_uncached(c.memtype) ? 1 : -1; /* -1: in a cacheable block, not touched */
	}
	if (n < MAX_SURFACES) {
		surfaces[n] = c;
		STORE(&n_surfaces, n + 1);
		return &surfaces[n];
	}
	static surface_check overflow;
	overflow = c;
	return &overflow;
}

static void sample_buffer(sample_slot *s, const uint32_t *px, unsigned w, unsigned h, unsigned stride) {
	s->minx = s->miny = ~0u;
	for (unsigned y = 0; y < h; y += AOD_DIAG_SAMPLE_STEP)
		for (unsigned x = 0; x < w; x += AOD_DIAG_SAMPLE_STEP) {
			uint32_t v = px[(size_t)y * stride + x];
			if (in_marker_rect(x, y)) { /* excluded from game statistics */
				s->marker_rect_samples++;
				s->marker_rect_magenta += v == AOD_DIAG_MARKER_ABGR;
				continue;
			}
			unsigned r = v & 0xFF, g = (v >> 8) & 0xFF, b = (v >> 16) & 0xFF;
			s->samples++;
			if (r > 8 || g > 8 || b > 8) {
				s->nonblack++;
				s->r += r; s->g += g; s->b += b;
				if (x < s->minx) s->minx = x;
				if (y < s->miny) s->miny = y;
				if (x > s->maxx) s->maxx = x;
				if (y > s->maxy) s->maxy = y;
			}
		}
}

void aod_gldiag_display_cb(void *framebuf) {
	if (!enabled)
		return;
	unsigned long tag = pop_tag();
	unsigned seq = INC(&cb_count);
	if (tag == UNTAGGED) INC(&cb_untagged);

	unsigned w = 0, h = 0, stride = 0;
	uintptr_t fb = (uintptr_t)framebuf;
	if (!be.display_geometry || be.display_geometry(&w, &h, &stride) != 0 || !fb || (fb & 3) || w < 64 || h < 64 ||
	    w > 4096 || h > 4096 || stride < w || stride > 4096 || w < AOD_DIAG_MARKER_X + AOD_DIAG_MARKER_SIZE ||
	    h < AOD_DIAG_MARKER_Y + AOD_DIAG_MARKER_SIZE) {
		INC(&cb_bad_geometry);
		return;
	}
	size_t len = (size_t)stride * h * 4;
	const surface_check *c = check_surface(fb, len);
	if (c->ok == 0) { INC(&cb_bad_memblock); return; }
	if (c->ok < 0) { INC(&cb_cached_memblock); return; }
	uint32_t *px = (uint32_t *)framebuf;

	/* 1. evidence from the GPU-completed buffer, before the marker is written */
	if (tag != UNTAGGED && aod_gldiag_is_summary_frame(tag)) {
		int k = -1;
		for (int i = 0; i < SLOTS; i++)
			if (LOAD(&slot_state[i]) == 0) { k = i; break; }
		if (k < 0) INC(&samples_dropped);
		else {
			sample_slot *s = &slots[k];
			memset(s, 0, sizeof *s);
			s->tag = tag; s->seq = seq; s->fb = fb; s->w = w; s->h = h; s->stride = stride; s->memtype = c->memtype;
			s->t = be.now_s ? be.now_s() : 0.0;
			sample_buffer(s, px, w, h, stride);
			STORE(&slot_state[k], 1);
		}
	}
	if (tag != UNTAGGED && aod_gldiag_is_dump_frame(tag)) {
		if (LOAD(&dump_state) != 0 || len > sizeof dump_px) { INC(&dumps_dropped); STORE(&last_dump_dropped, tag); }
		else {
			memcpy(dump_px, px, len);
			dump_tag = tag; dump_w = w; dump_h = h; dump_stride = stride;
			STORE(&dump_state, 1);
		}
	}

	/* 2. positive control: from frame AOD_DIAG_MARKER_FROM_FRAME's buffer on (untagged entries after onset too) */
	if (LOAD(&marker_onset) == UNTAGGED && tag != UNTAGGED && tag >= AOD_DIAG_MARKER_FROM_FRAME) STORE(&marker_onset, tag);
	if (LOAD(&marker_onset) != UNTAGGED) {
		for (unsigned y = AOD_DIAG_MARKER_Y; y < AOD_DIAG_MARKER_Y + AOD_DIAG_MARKER_SIZE; y++)
			for (unsigned x = AOD_DIAG_MARKER_X; x < AOD_DIAG_MARKER_X + AOD_DIAG_MARKER_SIZE; x++)
				px[(size_t)y * stride + x] = AOD_DIAG_MARKER_ABGR;
		INC(&marker_writes);
	}
}

/* ---- main-thread reporting ---- */
static void drain_samples(void) {
	for (int i = 0; i < SLOTS; i++) {
		if (LOAD(&slot_state[i]) != 1) continue;
		const sample_slot *s = &slots[i];
		char st[160];
		if (s->nonblack)
			snprintf(st, sizeof st, "nonblack %u/%u avg=(%llu,%llu,%llu) bbox=%u,%u-%u,%u", s->nonblack, s->samples,
			         s->r / s->nonblack, s->g / s->nonblack, s->b / s->nonblack, s->minx, s->miny, s->maxx, s->maxy);
		else
			snprintf(st, sizeof st, "ALL BLACK (%u samples)", s->samples);
		aod_log("DIAG handoff f%lu (entry %u) fb=%#lx %ux%u stride=%u memtype=%#x t=%.1fs: GPU-completed buffer at "
		        "sceDisplaySetFrameBuf hand-off, before marker, step %u, marker rect excluded: %s | marker rect magenta %u/%u",
		        s->tag, s->seq, (unsigned long)s->fb, s->w, s->h, s->stride, s->memtype, s->t, AOD_DIAG_SAMPLE_STEP, st,
		        s->marker_rect_magenta, s->marker_rect_samples);
		STORE(&slot_state[i], 0);
	}
}

static void write_dump(void) {
	if (LOAD(&dump_state) != 1) return;
	char name[64];
	snprintf(name, sizeof name, "frame_%05lu_handoff.rgba", dump_tag);
	uint32_t hdr[6] = { 0x47414944u /* 'DIAG' */, AOD_DIAG_DUMP_VERSION, dump_w, dump_h, dump_stride, 0 /* A8B8G8R8 */ };
	int r = be.write_file ? be.write_file(name, hdr, sizeof hdr, dump_px, (size_t)dump_stride * dump_h * 4) : -1;
	aod_log("DIAG dump f%lu %s -> %s (hand-off buffer before marker)", dump_tag, name, r == 0 ? "ok" : "FAILED");
	STORE(&dump_state, 0);
}

static void log_queue_state(unsigned long frame) {
	unsigned n = LOAD(&n_surfaces);
	char surf[200];
	int off = 0;
	surf[0] = 0;
	for (unsigned i = 0; i < n && off < (int)sizeof surf - 48; i++)
		off += snprintf(surf + off, sizeof surf - off, " %#lx:%s/%#x", (unsigned long)surfaces[i].addr,
		                surfaces[i].ok > 0 ? "ok" : surfaces[i].ok < 0 ? "cached-untouched" : "bad-range", surfaces[i].memtype);
	unsigned long onset = LOAD(&marker_onset);
	aod_log("DIAG f%lu display-queue: swaps=%u callbacks=%u untagged=%u bad-geometry=%u bad-memblock=%u cached=%u "
	        "tag-overflow=%u samples-dropped=%u dumps-dropped=%u marker-writes=%u marker-onset=%s%lu | surfaces:%s",
	        frame, swaps, LOAD(&cb_count), LOAD(&cb_untagged), LOAD(&cb_bad_geometry), LOAD(&cb_bad_memblock),
	        LOAD(&cb_cached_memblock), tag_overflow, LOAD(&samples_dropped), LOAD(&dumps_dropped), LOAD(&marker_writes),
	        onset == UNTAGGED ? "none" : "f", onset == UNTAGGED ? 0ul : onset, n ? surf : " none");
	aod_diag_display d;
	if (be.query_display && be.query_display(&d) == 0) {
		int known = 0;
		for (unsigned i = 0; i < n; i++) known |= surfaces[i].addr == (uintptr_t)d.base;
		aod_log("DIAG f%lu sceDisplayGetFrameBuf (read-only, no pixel access): base=%p pitch=%u fmt=%u %ux%u %s", frame,
		        d.base, d.pitch, d.format, d.width, d.height, known ? "= a vitaGL hand-off buffer" : "NOT a hand-off buffer seen");
	}
}

/* E9 GL positive control outside the game: one colour clear in AOD_DIAG_PREFLIGHT_ABGR, then the
 * clear colour the game last set (or GL's default) is restored. The caller swaps. */
void aod_gldiag_preflight_clear(void) {
	be.clear_color(0.0f, 1.0f, 0.0f, 1.0f);
	be.clear(0x4000u);
	be.clear_color(cc[0], cc[1], cc[2], cc[3]);
}

void aod_gldiag_frame_begin(unsigned long frame) {
	aod_gxmdiag_frame(frame);
	memset(&fc, 0, sizeof fc);
	cur_frame = frame;
	in_frame = 1;
	trace_lines = 0;
	if (enabled && aod_gldiag_is_trace_frame(frame))
		aod_log("DIAG f%lu begin: fb=%#x prog=%u vp=%d,%d,%d,%d blend=%d depth=%d cull=%d scissor=%d", frame, cur_fb, cur_prog,
		        vp[0], vp[1], vp[2], vp[3], st_blend, st_depth, st_cull, st_scissor);
}

void aod_gldiag_frame_end(unsigned long frame) {
	in_frame = 0;
	if (enabled && aod_gldiag_is_summary_frame(frame)) {
		aod_log("DIAG f%lu summary: draws=%u (fbo %u, verts %u) clears=%u (fbo %u) fbbind0=%u fbbindN=%u tex=%u sub=%u comp=%u "
		        "prog=%u attrptr=%u gles1=%u game-glerr=%u | draws with blend=%u depth=%u cull=%u scissor=%u colormask-off=%u | end fb=%#x vp=%d,%d,%d,%d cc=(%.2f,%.2f,%.2f,%.2f) t=%.1fs",
		        frame, fc.draws, fc.draws_fbo, fc.draw_verts, fc.clears, fc.clears_fbo, fc.fb_bind_zero, fc.fb_bind_other,
		        fc.tex_uploads, fc.tex_sub, fc.tex_compressed, fc.programs, fc.attrib_ptrs, fc.gles1_calls, fc.gl_errors,
		        fc.blend_on_draws, fc.depth_on_draws, fc.cull_on_draws, fc.scissor_on_draws, fc.colormask_off_draws,
		        cur_fb, vp[0], vp[1], vp[2], vp[3], cc[0], cc[1], cc[2], cc[3], be.now_s ? be.now_s() : 0.0);
		log_queue_state(frame);
		aod_gxmdiag_report(frame);
	}
	if (aod_gldiag_arm(frame) != AOD_DIAG_ARM_NONE)
		aod_log("DIAG f%lu A/B arm %s%s%s%s: %u draws/clears modified (state restored after each)", frame,
		        aod_gldiag_arm(frame) == (AOD_DIAG_ARM_DEPTH_OFF | AOD_DIAG_ARM_BLEND_OFF) ? "two-factor interaction " : "single-factor ",
		        aod_gldiag_arm(frame) & AOD_DIAG_ARM_DEPTH_OFF ? "depth-off " : "",
		        aod_gldiag_arm(frame) & AOD_DIAG_ARM_BLEND_OFF ? "blend-off " : "",
		        aod_gldiag_arm(frame) & AOD_DIAG_ARM_MAGENTA_CLEAR ? "magenta-clear " : "", arm_modified);
	arm_modified = 0;
	drain_samples();
	write_dump();
	static unsigned long dropped_logged = UNTAGGED;
	unsigned long dropped = LOAD(&last_dump_dropped);
	if (dropped != UNTAGGED && dropped != dropped_logged) {
		dropped_logged = dropped;
		aod_log("DIAG dump f%lu SKIPPED: the single dump slot was still busy (dumps-dropped=%u)", dropped, LOAD(&dumps_dropped));
	}
}
