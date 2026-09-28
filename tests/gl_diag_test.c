/* Host test for source/aod/gl_diag.c (DIAGNOSTIC layer): pass-through of arguments, no GL calls
 * of its own, bounded logging/tracing, display-queue callback evidence (frame tags, geometry and
 * memblock gating before any pixel access, pre-marker statistics and dumps), marker onset. */
#include "aod/gl_diag.h"
#include "aod/gxm_diag.h"
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fails, checks;
#define CHECK(c, ...) do { checks++; if (!(c)) { fails++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

#define MAXLOG 4096
static char logs[MAXLOG][640];
static int nlog;
void aod_log(const char *fmt, ...) {
	va_list ap; va_start(ap, fmt);
	vsnprintf(logs[nlog % MAXLOG], sizeof logs[0], fmt, ap);
	va_end(ap);
	if (getenv("VERBOSE")) puts(logs[nlog % MAXLOG]);
	nlog++;
}
/* index of the first log line at or after `from` containing all of a, b (b may be NULL) */
static int find_log(int from, const char *a, const char *b) {
	for (int i = from; i < nlog; i++)
		if (strstr(logs[i], a) && (!b || strstr(logs[i], b))) return i;
	return -1;
}
static int count_log(int from, const char *a) { int n = 0; for (int i = from; i < nlog; i++) n += strstr(logs[i], a) != NULL; return n; }

static int n_draw; static int last_mode, last_count; static unsigned n_err_polls; static dg_enum next_err;
static void m_draw_arrays(dg_enum m, dg_int f, dg_sizei n) { n_draw++; last_mode = (int)m; last_count = n; }
static void m_draw_elements(dg_enum m, dg_sizei n, dg_enum t, const void *i) { n_draw++; last_mode = (int)m; last_count = n; }
static float last_cc[4], clear_cc[4];
static void m_cc(dg_float a, dg_float b, dg_float c, dg_float d) { last_cc[0] = a; last_cc[1] = b; last_cc[2] = c; last_cc[3] = d; }
static void m_clear(dg_bitfield m) { memcpy(clear_cc, last_cc, sizeof clear_cc); }
static void m_bindfb(dg_enum t, dg_uint f) {}
static void m_vp(dg_int a, dg_int b, dg_sizei c, dg_sizei d) {}
/* enable/disable call log: +cap / -cap, and the clear colour in force at each clear */
static int caplog[64], ncap;
static void m_en(dg_enum c) { if (ncap < 64) caplog[ncap++] = (int)c; }
static void m_dis(dg_enum c) { if (ncap < 64) caplog[ncap++] = -(int)c; }
static void m_bf(dg_enum a, dg_enum b) {}
static void m_cm(dg_bool a, dg_bool b, dg_bool c, dg_bool d) {}
static void m_dm(dg_bool a) {}
static void m_bt(dg_enum a, dg_uint b) {}
static int tex_w;
static void m_ti(dg_enum t, dg_int l, dg_int i, dg_sizei w, dg_sizei h, dg_int b, dg_enum f, dg_enum ty, const void *p) { tex_w = w; }
static void m_tsi(dg_enum t, dg_int l, dg_int x, dg_int y, dg_sizei w, dg_sizei h, dg_enum f, dg_enum ty, const void *p) {}
static void m_cti(dg_enum t, dg_int l, dg_enum i, dg_sizei w, dg_sizei h, dg_int b, dg_sizei s, const void *d) {}
static void m_vap(dg_uint i, dg_int s, dg_enum t, dg_bool n, dg_sizei st, const void *p) {}
static void m_evaa(dg_uint i) {}
static dg_int m_gal(dg_uint p, const char *n) { return 3; }
static unsigned used_prog;
static void m_up(dg_uint p) { used_prog = p; }
static void m_bb(dg_enum t, dg_uint b) {}
static dg_enum m_err(void) { n_err_polls++; dg_enum e = next_err; next_err = 0; return e; }
static void m_vpnt(dg_int s, dg_enum t, dg_sizei st, const void *p) {}
static void m_ecs(dg_enum c) {}

#define W 960
#define H 544
static uint32_t fbmem[H * W];
static unsigned geo_stride = W;
static int m_geometry(unsigned *w, unsigned *h, unsigned *stride) { *w = W; *h = H; *stride = geo_stride; return 0; }
/* memblocks: fbmem is a CDRAM block; 0x100000 lies in a too-small block; 0x200000 in a cached block */
static int n_memblock_calls;
static int m_mem_block(const void *p, uintptr_t *base, size_t *size, unsigned *memtype) {
	n_memblock_calls++;
	if (p == (void *)fbmem) { *base = (uintptr_t)fbmem; *size = sizeof fbmem; *memtype = AOD_DIAG_MEMTYPE_CDRAM_RW; return 0; }
	if (p == (void *)0x100000) { *base = 0x100000; *size = 4096; *memtype = AOD_DIAG_MEMTYPE_CDRAM_RW; return 0; }
	if (p == (void *)0x200000) { *base = 0x200000; *size = 16u << 20; *memtype = 0x0C20D060u /* USER_RW, cached */; return 0; }
	return -1;
}
static int m_query(aod_diag_display *d) { d->base = fbmem; d->pitch = W; d->width = W; d->height = H; d->format = 0; return 0; }
static int n_dumps; static size_t last_dump_len; static uint32_t last_hdr[6]; static uint32_t dump_px_8_8, dump_px_300_100; static char last_dump_name[64];
static int m_write(const char *name, const void *h, size_t hl, const void *data, size_t len) {
	n_dumps++; last_dump_len = len; memcpy(last_hdr, h, sizeof last_hdr);
	dump_px_8_8 = ((const uint32_t *)data)[8 * W + 8];
	dump_px_300_100 = ((const uint32_t *)data)[100 * W + 300];
	snprintf(last_dump_name, sizeof last_dump_name, "%s", name);
	return 0;
}
static double m_now(void) { return 1.0; }
/* E8 pass-through targets */
static unsigned last_depth_func, last_pixel_p; static float last_clear_depth; static int last_pixel_v;
static void m_depth_func(dg_enum f) { last_depth_func = f; }
static void m_clear_depthf(dg_float d) { last_clear_depth = d; }
static void m_enum1(dg_enum e) {}
static void m_texparam(dg_enum t, dg_enum p, dg_int v) {}
static void m_pixel_storei(dg_enum p, dg_int v) { last_pixel_p = p; last_pixel_v = v; }
static void m_buffer_data(dg_enum t, dg_sizeiptr n, const void *d, dg_enum u) {}
static void m_buffer_sub_data(dg_enum t, dg_intptr o, dg_sizeiptr n, const void *d) {}
static dg_int m_get_uloc(dg_uint p, const char *n) { return strcmp(n, "global_color") == 0 ? 3 : strcmp(n, "model_view_projection_mx") == 0 ? 2 : -1; }
static float last_u4[4]; static int last_u4_loc = -99, last_um4_loc = -99; static float last_um4_0;
static void m_u1f(dg_int l, dg_float v) {}
static void m_u1i(dg_int l, dg_int v) {}
static void m_ufv(dg_int l, dg_sizei n, const dg_float *v) {}
static void m_u4fv(dg_int l, dg_sizei n, const dg_float *v) { if (l == -1 || n <= 0) return; last_u4_loc = l; memcpy(last_u4, v, sizeof last_u4); }
static void m_u2iv(dg_int l, dg_sizei n, const dg_int *v) {}
static void m_um4fv(dg_int l, dg_sizei n, dg_bool t, const dg_float *v) { if (l == -1 || n <= 0) return; last_um4_loc = l; last_um4_0 = v[0]; }

static int marker_at(unsigned x, unsigned y) { return fbmem[y * W + x] == AOD_DIAG_MARKER_ABGR; }
static int marker_exact(void) {
	return marker_at(8, 8) && marker_at(39, 39) && marker_at(8, 39) && marker_at(39, 8) && !marker_at(40, 40) &&
	       !marker_at(7, 8) && !marker_at(8, 7) && !marker_at(40, 8) && !marker_at(8, 40);
}
static void clear_marker_rect(void) { for (int y = 8; y < 40; y++) for (int x = 8; x < 40; x++) fbmem[y * W + x] = 0; }
/* one main-loop frame: begin, (draws), before_swap, optional in-order callback, end */
static void frame(unsigned long f, int with_cb) {
	aod_gldiag_frame_begin(f);
	aod_gldiag_before_swap(f);
	if (with_cb) aod_gldiag_display_cb(fbmem);
	aod_gldiag_frame_end(f);
}

int main(void) {
	aod_gldiag_backend b = {
		.draw_arrays = m_draw_arrays, .draw_elements = m_draw_elements, .clear = m_clear, .clear_color = m_cc,
		.bind_framebuffer = m_bindfb, .viewport = m_vp, .scissor = m_vp, .enable = m_en, .disable = m_dis,
		.blend_func = m_bf, .color_mask = m_cm, .depth_mask = m_dm, .bind_texture = m_bt, .tex_image_2d = m_ti,
		.tex_sub_image_2d = m_tsi, .compressed_tex_image_2d = m_cti, .vertex_attrib_pointer = m_vap,
		.enable_vertex_attrib_array = m_evaa, .get_attrib_location = m_gal, .use_program = m_up, .bind_buffer = m_bb,
		.get_error = m_err, .vertex_pointer = m_vpnt, .enable_client_state = m_ecs,
		.depth_func = m_depth_func, .clear_depthf = m_clear_depthf, .cull_face = m_enum1, .active_texture = m_enum1,
		.tex_parameteri = m_texparam, .pixel_storei = m_pixel_storei, .generate_mipmap = m_enum1, .buffer_data = m_buffer_data,
		.buffer_sub_data = m_buffer_sub_data, .get_uniform_location = m_get_uloc, .u1f = m_u1f, .u1i = m_u1i, .u2fv = m_ufv,
		.u3fv = m_ufv, .u4fv = m_u4fv, .u2iv = m_u2iv, .um4fv = m_um4fv,
		.display_geometry = m_geometry, .mem_block = m_mem_block, .query_display = m_query, .write_file = m_write, .now_s = m_now,
	};
	aod_gldiag_set_backend(&b);
	CHECK(!aod_gldiag_enabled() && aod_gldiag_arm(410) == AOD_DIAG_ARM_NONE, "diagnostics are off by default (no arms)");
	aod_gldiag_set_enabled(1);   /* the checks below exercise the opt-in diagnostic mode */
	aod_gxmdiag_set_verbose(1);

	/* frame 0: pass-through, no GL calls of our own, bounded trace, game glGetError pass-through */
	aod_gldiag_frame_begin(0);
	aod_diag_glUseProgram(7);
	aod_diag_glDrawArrays(4, 0, 6);
	CHECK(n_draw == 1 && last_mode == 4 && last_count == 6 && used_prog == 7, "draw/program pass-through");
	aod_diag_glTexImage2D(0xDE1, 0, 0x1908, 128, 64, 0, 0x1908, 0x1401, fbmem);
	CHECK(tex_w == 128, "TexImage2D pass-through");
	CHECK(aod_diag_glGetAttribLocation(7, "v_position") == 3, "GetAttribLocation returns backend value");
	for (int i = 0; i < 1000; i++) aod_diag_glDrawElements(4, 3, 0x1403, 0);
	CHECK(n_err_polls == 0, "wrappers never call glGetError themselves (%u calls)", n_err_polls);
	CHECK(count_log(0, "line cap") == 1, "trace line cap logged once");
	next_err = 0x502;
	CHECK(aod_diag_glGetError() == 0x502 && aod_diag_glGetError() == 0 && n_err_polls == 2, "game glGetError values passed through");
	aod_gldiag_before_swap(0);
	aod_gldiag_display_cb(fbmem);
	aod_gldiag_frame_end(0);
	int after_f0 = nlog;
	CHECK(after_f0 <= AOD_DIAG_TRACE_LINES_PER_FRAME + 15, "frame-0 output bounded (%d)", after_f0);
	CHECK(find_log(0, "f0 summary", "game-glerr=1") >= 0, "game-visible GL error counted in summary");
	CHECK(find_log(0, "DIAG handoff f0 (entry 0)", "ALL BLACK") >= 0, "all-black hand-off buffer reported for frame 0");
	CHECK(find_log(0, "display-queue: swaps=1 callbacks=1", "marker-onset=none") >= 0, "queue counters at f0");
	CHECK(find_log(0, "sceDisplayGetFrameBuf (read-only", "= a vitaGL hand-off buffer") >= 0, "read-only display query matched to hand-off buffer");
	CHECK(n_memblock_calls == 1, "memblock verified (%d calls)", n_memblock_calls);

	/* quiet frame 7: no logging */
	int before = nlog;
	aod_gldiag_frame_begin(7);
	for (int i = 0; i < 50; i++) aod_diag_glDrawArrays(4, 0, 3);
	aod_gldiag_before_swap(7);
	aod_gldiag_display_cb(fbmem);
	aod_gldiag_frame_end(7);
	CHECK(nlog == before, "quiet frames are silent (%d logs)", nlog - before);
	CHECK(n_memblock_calls == 1, "surface check cached per address (%d calls)", n_memblock_calls);

	/* FIFO tags with display lag: two swaps queued before their callbacks run */
	fbmem[100 * W + 300] = 0xFF2040C0u;
	aod_gldiag_frame_begin(1); aod_gldiag_before_swap(1); aod_gldiag_frame_end(1);
	aod_gldiag_frame_begin(2); aod_gldiag_before_swap(2); aod_gldiag_frame_end(2);
	before = nlog;
	aod_gldiag_display_cb(fbmem); /* frame 1's buffer */
	aod_gldiag_display_cb(fbmem); /* frame 2's buffer */
	aod_gldiag_frame_begin(8); aod_gldiag_before_swap(8); aod_gldiag_display_cb(fbmem); aod_gldiag_frame_end(8);
	int i1 = find_log(before, "DIAG handoff f1 ", NULL), i2 = find_log(before, "DIAG handoff f2 ", NULL);
	CHECK(i1 >= 0 && i2 >= 0 && strstr(logs[i1], "nonblack 1/") && strstr(logs[i1], "bbox=300,100-300,100"), "lagged callbacks carry their own frame tags and stats");

	/* gating: no pixel access unless geometry and memblock are verified (bad pointers would crash) */
	aod_gldiag_before_swap(9); aod_gldiag_display_cb((void *)0x100000);  /* block too small */
	aod_gldiag_before_swap(10); aod_gldiag_display_cb((void *)0x200000); /* cacheable block */
	aod_gldiag_before_swap(11); aod_gldiag_display_cb((void *)0x300000); /* no block */
	geo_stride = W - 1;
	aod_gldiag_before_swap(12); aod_gldiag_display_cb((void *)0x400000); /* stride < width */
	geo_stride = W;
	aod_gldiag_before_swap(13); aod_gldiag_display_cb((void *)0x100002); /* misaligned */
	before = nlog;
	aod_gldiag_frame_begin(5);
	aod_gldiag_frame_end(5); /* summary frame, no swap: reports the counters */
	CHECK(find_log(before, "bad-geometry=2 bad-memblock=2 cached=1", NULL) >= 0, "invalid buffers counted, never touched (%s)", logs[nlog - 2]);
	CHECK(find_log(before, "cached-untouched/0xc20d060", NULL) >= 0 && find_log(before, "bad-range/0x9408060", NULL) >= 0, "surface verdicts logged");

	/* marker onset: frame 59's buffer unmarked, frame 60's buffer marked after its sample */
	for (unsigned long f = 14; f < 60; f++) frame(f, 1);
	CHECK(!marker_at(8, 8) && !marker_at(39, 39), "no marker before frame 60's buffer");
	before = nlog;
	frame(60, 1);
	CHECK(marker_exact(), "marker is exactly the 32x32 square at 8,8 from frame 60's buffer");
	int i60 = find_log(before, "DIAG handoff f60 ", NULL);
	CHECK(i60 >= 0 && strstr(logs[i60], "marker rect magenta 0/64") && strstr(logs[i60], "nonblack 1/"), "f60 sample taken before marker, marker rect excluded");
	CHECK(find_log(before, "marker-onset=f60", NULL) >= 0, "onset logged as f60");

	/* a buffer whose content still holds an old marker (no full redraw): reported separately, not as game content */
	before = nlog;
	for (unsigned long f = 61; f < 120; f++) frame(f, 1);
	/* frame 120: simulated full redraw clears the old marker; dump must be the pre-marker content */
	clear_marker_rect();
	fbmem[8 * W + 8] = 0xFF00FF00u;
	frame(120, 1);
	int i120 = find_log(before, "DIAG handoff f120 ", NULL);
	CHECK(i120 >= 0 && strstr(logs[i120], "marker rect magenta 0/64") && strstr(logs[i120], "nonblack 1/"), "f120 stats exclude marker rect (%s)", i120 >= 0 ? logs[i120] : "none");
	CHECK(n_dumps == 1 && strcmp(last_dump_name, "frame_00120_handoff.rgba") == 0 && dump_px_8_8 == 0xFF00FF00u && dump_px_300_100 == 0xFF2040C0u,
	      "dump holds the hand-off buffer before the marker (%s %#x)", last_dump_name, dump_px_8_8);
	CHECK(marker_exact(), "marker rewritten after the dump");
	CHECK(last_dump_len == (size_t)W * H * 4 && last_hdr[0] == 0x47414944u && last_hdr[1] == AOD_DIAG_DUMP_VERSION && last_hdr[2] == W &&
	      last_hdr[3] == H && last_hdr[4] == W && last_hdr[5] == 0, "dump size and header");
	/* persisting marker at a later sample is visible in the marker-rect count */
	before = nlog;
	for (unsigned long f = 121; f <= 300; f++) frame(f, 1);
	int i300 = find_log(before, "DIAG handoff f300 ", NULL);
	CHECK(i300 >= 0 && strstr(logs[i300], "marker rect magenta 64/64") && strstr(logs[i300], "nonblack 1/"), "persisting marker reported in marker rect only");

	/* untagged entries after onset (e.g. a dialog swap) still get the marker and are counted */
	clear_marker_rect();
	aod_gldiag_display_cb(fbmem);
	CHECK(marker_exact(), "untagged entry after onset marked");
	/* after onset the marker would be written: unverified buffers must still not be touched (would fault) */
	aod_gldiag_display_cb((void *)0x200000);
	aod_gldiag_display_cb((void *)0x100000);
	aod_gldiag_display_cb((void *)0x100002);

	/* long run bounded */
	before = nlog;
	for (unsigned long f = 301; f <= 1800; f++) frame(f, 1);
	CHECK(n_dumps == 9, "dumps at 120, 400..450 (6), 600, 1800 only (%d)", n_dumps);
	CHECK(find_log(0, "frame_00410_handoff.rgba -> ok", NULL) >= 0 && find_log(0, "frame_00450_handoff.rgba -> ok", NULL) >= 0, "arm-frame dumps written");
	CHECK(find_log(before, "display-queue: swaps=", "untagged=4 bad-geometry=3 bad-memblock=3 cached=2") >= 0, "untagged and rejected callbacks counted after onset");
	CHECK(nlog - after_f0 < 120, "log lines after frame 0 bounded over 1800 frames (%d)", nlog - after_f0);
	CHECK(n_memblock_calls == 4, "one memblock query per distinct address (%d)", n_memblock_calls);
	/* ---- E8: A/B arms (one factor per frame, restored after each draw) ---- */
	CHECK(aod_gldiag_arm(400) == 0 && aod_gldiag_arm(410) == AOD_DIAG_ARM_DEPTH_OFF && aod_gldiag_arm(420) == AOD_DIAG_ARM_BLEND_OFF &&
	      aod_gldiag_arm(430) == (AOD_DIAG_ARM_DEPTH_OFF | AOD_DIAG_ARM_BLEND_OFF) && aod_gldiag_arm(440) == 0 &&
	      aod_gldiag_arm(450) == AOD_DIAG_ARM_MAGENTA_CLEAR && aod_gldiag_arm(300) == 0 && aod_gldiag_arm(3600) == 0, "arm schedule");
	CHECK(aod_gldiag_is_summary_frame(410) && aod_gldiag_is_summary_frame(450) && !aod_gldiag_is_summary_frame(415), "arm frames are sampled");
	aod_gldiag_frame_begin(1900);
	aod_diag_glEnable(0x0B71); aod_diag_glEnable(0x0BE2);   /* game: depth test + blend on */
	aod_gldiag_frame_end(1900);
	ncap = 0; aod_gldiag_frame_begin(1901); aod_diag_glDrawElements(4, 6, 0x1403, 0); aod_gldiag_frame_end(1901);
	CHECK(ncap == 0, "no state changes outside arm frames (%d)", ncap);
	/* drive the real arm frames */
	ncap = 0; aod_gldiag_frame_begin(410); aod_diag_glDrawElements(4, 6, 0x1403, 0); aod_diag_glDrawArrays(4, 0, 3); before = nlog; aod_gldiag_frame_end(410);
	CHECK(ncap == 4 && caplog[0] == -0x0B71 && caplog[1] == 0x0B71 && caplog[2] == -0x0B71 && caplog[3] == 0x0B71, "A1: depth off around each draw, restored (%d)", ncap);
	CHECK(find_log(before, "A/B arm single-factor depth-off", "2 draws/clears modified") >= 0, "A1 logged");
	ncap = 0; aod_gldiag_frame_begin(420); aod_diag_glDrawElements(4, 6, 0x1403, 0); aod_gldiag_frame_end(420);
	CHECK(ncap == 2 && caplog[0] == -0x0BE2 && caplog[1] == 0x0BE2, "A2: blend off around the draw, restored");
	ncap = 0; aod_gldiag_frame_begin(430); aod_diag_glDrawElements(4, 6, 0x1403, 0); aod_gldiag_frame_end(430);
	CHECK(ncap == 4 && caplog[0] == -0x0B71 && caplog[1] == -0x0BE2 && caplog[2] == 0x0B71 && caplog[3] == 0x0BE2, "A3: both off, both restored");
	aod_gldiag_frame_begin(431); aod_diag_glDisable(0x0B71); ncap = 0; aod_gldiag_frame_end(431);
	aod_gldiag_frame_begin(410); ncap = 0; aod_diag_glDrawElements(4, 6, 0x1403, 0); aod_gldiag_frame_end(410);
	CHECK(ncap == 0, "arm leaves a factor alone when the game has it off (%d)", ncap);
	/* A4: colour clears use magenta, game clear colour restored; depth-only clears untouched */
	aod_diag_glClearColor(0.f, 0.f, 0.f, 1.f);
	aod_gldiag_frame_begin(450); aod_diag_glClear(0x100);
	CHECK(clear_cc[0] == 0.f && clear_cc[2] == 0.f, "A4 leaves depth-only clears alone");
	aod_diag_glClear(0x4100); aod_gldiag_frame_end(450);
	CHECK(clear_cc[0] == 1.f && clear_cc[1] == 0.f && clear_cc[2] == 1.f && clear_cc[3] == 1.f, "A4 colour clear is magenta");
	CHECK(last_cc[0] == 0.f && last_cc[1] == 0.f && last_cc[2] == 0.f && last_cc[3] == 1.f, "A4 restores the game's clear colour");
	aod_gldiag_frame_begin(451); aod_diag_glClear(0x4100); aod_gldiag_frame_end(451);
	CHECK(clear_cc[0] == 0.f && clear_cc[2] == 0.f, "no magenta outside A4");

	/* ---- E8: values ---- */
	before = nlog;
	CHECK(aod_diag_glGetUniformLocation(2, "global_color") == 3 && aod_diag_glGetUniformLocation(2, "model_view_projection_mx") == 2 &&
	      aod_diag_glGetUniformLocation(2, "nope") == -1, "GetUniformLocation passes remap ids through");
	CHECK(find_log(before, "DIAG uniform prog=2 'global_color' -> id 3", NULL) >= 0, "uniform name logged");
	float col[4] = { 0.25f, 0.5f, 0.75f, 0.0f }, mvp[16] = { 2.f / 960, 0, 0, 0, 0, -2.f / 544, 0, 0, 0, 0, 1, 0, -1, 1, 0, 1 };
	aod_gldiag_frame_begin(300); aod_diag_glUseProgram(2);
	before = nlog;
	aod_diag_glUniform4fv(3, 1, col); aod_diag_glUniformMatrix4fv(2, 1, 0, mvp);
	aod_gldiag_frame_end(300);
	CHECK(last_u4_loc == 3 && last_u4[1] == 0.5f && last_um4_loc == 2 && last_um4_0 == mvp[0], "uniform values passed through");
	CHECK(find_log(before, "Uniform4fv prog=2 global_color(id 3) n=1: 0.25 0.5 0.75 0", NULL) >= 0, "global_color value logged by name");
	CHECK(find_log(before, "UniformMatrix4fv prog=2 model_view_projection_mx(id 2) n=1: 0.002083", " -1 1 0 1") >= 0, "MVP logged (16 floats)");
	before = nlog;
	aod_gldiag_frame_begin(1902); aod_diag_glUniform4fv(3, 1, col); aod_gldiag_frame_end(1902);
	CHECK(nlog == before, "uniform values not logged outside trace frames");
	aod_diag_glDepthFunc(0x203); aod_diag_glClearDepthf(0.5f);
	CHECK(last_depth_func == 0x203 && last_clear_depth == 0.5f && find_log(nlog - 2, "DepthFunc 0x203", NULL) >= 0 && find_log(nlog - 1, "ClearDepthf 0.5", NULL) >= 0,
	      "depth state passed through and logged");
	for (int i = 0; i < 200; i++) aod_diag_glDepthFunc(0x203);
	CHECK(count_log(0, "DepthFunc 0x203") <= AOD_DIAG_STATE_LOG_MAX, "state log bounded");
	/* buffer head decode */
	float verts[8] = { 10.f, 20.f, 0.f, 1.f, 1.f, 1.f, 0.5f, 0.25f };
	unsigned short idx16[6] = { 0, 1, 2, 2, 1, 3 };
	aod_diag_glBindBuffer(0x8892, 77); aod_diag_glBufferData(0x8892, sizeof verts, verts, 0x88E4);
	CHECK(find_log(nlog - 1, "BufferData target=0x8892 buf=77 off=0 size=32 head: 10 20 0 1 1 1 0.5 0.25", NULL) >= 0, "vertex buffer head decoded (%s)", logs[(nlog - 1) % MAXLOG]);
	aod_diag_glBindBuffer(0x8893, 78); aod_diag_glBufferData(0x8893, sizeof idx16, idx16, 0x88E4);
	CHECK(find_log(nlog - 1, "target=0x8893 buf=78 off=0 size=12 head: 0 1 2 2 1 3", NULL) >= 0, "index buffer head decoded");
	/* texture source statistics */
	static unsigned char rgba[64 * 32 * 4];
	for (int i = 0; i < 64 * 32; i++) { rgba[i * 4] = 200; rgba[i * 4 + 1] = 100; rgba[i * 4 + 2] = 50; rgba[i * 4 + 3] = i < 64 * 16 ? 0 : 255; }
	aod_diag_glBindTexture(0xDE1, 9);
	aod_diag_glTexImage2D(0xDE1, 0, 0x1908, 64, 32, 0, 0x1908, 0x1401, rgba);
	CHECK(find_log(nlog - 2, "texstat", "tex=9 64x32 fmt=0x1908: 2048 samples alpha0=1024 alpha255=1024 rgb-nonblack=2048 avg=(200,100,50,127)") >= 0, "RGBA stats (%s)", logs[(nlog - 1) % MAXLOG]);
	/* RGB with default unpack alignment 4: 3-pixel rows are 9 bytes packed into 12 */
	static unsigned char rgb[12 * 2];
	memset(rgb, 0, sizeof rgb);
	for (int x = 0; x < 3; x++) rgb[12 + x * 3] = 255;  /* second row red */
	memset(rgb + 9, 0x77, 3);                            /* padding of row 0: must never be sampled */
	aod_diag_glTexImage2D(0xDE1, 0, 0x1907, 3, 2, 0, 0x1907, 0x1401, rgb);
	CHECK(find_log(nlog - 2, "texstat", "3x2 fmt=0x1907: 6 samples alpha0=0 alpha255=6 rgb-nonblack=3 avg=(127,0,0,255)") >= 0, "RGB stats honour unpack alignment (%s)", logs[(nlog - 1) % MAXLOG]);
	aod_diag_glPixelStorei(0x0CF5, 1);
	CHECK(last_pixel_p == 0x0CF5 && last_pixel_v == 1, "PixelStorei passed through");
	static unsigned char rgb1[9 * 2];
	memset(rgb1, 0, sizeof rgb1);
	for (int x = 0; x < 3; x++) rgb1[9 + x * 3 + 1] = 255;  /* second row green, packed */
	aod_diag_glTexImage2D(0xDE1, 0, 0x1907, 3, 2, 0, 0x1907, 0x1401, rgb1);
	CHECK(find_log(nlog - 2, "texstat", "avg=(0,127,0,255)") >= 0, "alignment 1 rows packed");
	for (int i = 0; i < 100; i++) aod_diag_glTexImage2D(0xDE1, 0, 0x1908, 64, 32, 0, 0x1908, 0x1401, rgba);
	CHECK(count_log(0, "DIAG texstat") == AOD_DIAG_TEXSTAT_MAX, "texture stats bounded (%d)", count_log(0, "DIAG texstat"));

	/* ---- review fixes: unaligned buffer payloads, -1 / sentinel pointers, huge counts ---- */
	static unsigned char raw[64];
	float want[8] = { 1.5f, -2.f, 0.f, 1.f, 1.f, 1.f, 0.75f, 0.125f };
	unsigned short widx[6] = { 5, 6, 7, 7, 6, 8 };
	memcpy(raw + 1, want, sizeof want);              /* odd address: typed float loads would be misaligned */
	aod_diag_glBindBuffer(0x8892, 90); aod_diag_glBufferSubData(0x8892, 4, sizeof want, raw + 1);
	CHECK(find_log(nlog - 1, "BufferSubData target=0x8892 buf=90 off=4 size=32 head: 1.5 -2 0 1 1 1 0.75 0.125", NULL) >= 0, "unaligned float payload decoded (%s)", logs[(nlog - 1) % MAXLOG]);
	memcpy(raw + 3, widx, sizeof widx);
	aod_diag_glBindBuffer(0x8893, 91); aod_diag_glBufferSubData(0x8893, 0, sizeof widx, raw + 3);
	CHECK(find_log(nlog - 1, "target=0x8893 buf=91 off=0 size=12 head: 5 6 7 7 6 8", NULL) >= 0, "unaligned u16 payload decoded");
	aod_diag_glBufferData(0x8892, 0, (const void *)0x10, 0x88E4);  /* zero-size data must not be read */
	CHECK(find_log(nlog - 1, "size=0 head:", NULL) >= 0, "zero-size payload not read");
	aod_gldiag_frame_begin(300); aod_diag_glUseProgram(2);
	before = nlog;
	const float *sentinel = (const float *)0x10;   /* any read faults */
	aod_diag_glUniform4fv(-1, 1, sentinel);
	aod_diag_glUniformMatrix4fv(-1, 1, 0, sentinel);
	aod_diag_glUniform2fv(-1, 3, sentinel);
	aod_diag_glUniform2iv(-1, 1, (const dg_int *)0x10);
	aod_diag_glUniform4fv(3, 0, sentinel);
	CHECK(count_log(before, "not read (location -1, ignored by GL)") == 4 && count_log(before, "not read (no values)") == 1,
	      "location -1 and n=0 logged without reading the array");
	static float big[64];
	for (int i = 0; i < 64; i++) big[i] = (float)i;
	before = nlog;
	aod_diag_glUniform4fv(3, 0x7fffffff, big);    /* count * 4 would overflow int */
	aod_diag_glUniformMatrix4fv(2, 0x7fffffff, 0, big);
	CHECK(find_log(before, "Uniform4fv prog=2 global_color(id 3) n=2147483647: 0 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15", NULL) >= 0 &&
	      find_log(before, "UniformMatrix4fv prog=2 model_view_projection_mx(id 2) n=2147483647: 0 1 2", NULL) >= 0, "huge counts clamped to 16 values");
	aod_gldiag_frame_end(300);
	/* single dump slot: a second arm dump while the first is pending is skipped and logged */
	int nd = n_dumps;
	aod_gldiag_before_swap(400); aod_gldiag_before_swap(410);
	aod_gldiag_display_cb(fbmem); aod_gldiag_display_cb(fbmem);
	before = nlog;
	aod_gldiag_frame_begin(1903); aod_gldiag_frame_end(1903);
	CHECK(n_dumps == nd + 1 && find_log(before, "DIAG dump f410 SKIPPED: the single dump slot was still busy", NULL) >= 0, "busy slot: dump skipped and logged");
	CHECK(aod_gldiag_is_dump_frame(430) && !aod_gldiag_is_dump_frame(435) && !aod_gldiag_is_dump_frame(460), "dump schedule");
	CHECK(find_log(0, "A/B arm two-factor interaction depth-off blend-off", NULL) >= 0 && find_log(0, "A/B arm single-factor depth-off", NULL) >= 0,
	      "A3 labelled as a two-factor interaction arm");

	/* ---- E9: preflight GL clear positive control ---- */
	CHECK(aod_gldiag_is_preflight(AOD_DIAG_PREFLIGHT_P1) && aod_gldiag_is_preflight(AOD_DIAG_PREFLIGHT_P2 + 1) && !aod_gldiag_is_preflight(AOD_DIAG_PREFLIGHT_P1 + 2) &&
	      aod_gldiag_is_summary_frame(AOD_DIAG_PREFLIGHT_P2) && aod_gldiag_is_dump_frame(AOD_DIAG_PREFLIGHT_P1 + 1) && !aod_gldiag_is_dump_frame(AOD_DIAG_PREFLIGHT_P1),
	      "preflight tags sampled, second frame of each pair dumped");
	aod_diag_glClearColor(0.1f, 0.2f, 0.3f, 0.4f);
	aod_gldiag_preflight_clear();
	CHECK(clear_cc[0] == 0.f && clear_cc[1] == 1.f && clear_cc[2] == 0.f && clear_cc[3] == 1.f, "preflight clears in green");
	CHECK(last_cc[0] == 0.1f && last_cc[1] == 0.2f && last_cc[2] == 0.3f && last_cc[3] == 0.4f, "preflight restores the clear colour");
	CHECK(AOD_DIAG_PREFLIGHT_ABGR == 0xFF00FF00u, "green in A8B8G8R8");
	before = nlog;
	aod_gldiag_frame_begin(AOD_DIAG_PREFLIGHT_P1); aod_gldiag_frame_end(AOD_DIAG_PREFLIGHT_P1);
	CHECK(find_log(before, "DIAG f1000000 gxm: scenes=0", NULL) >= 0, "gxm report at preflight summary");

	/* ---- boot-10: default (diagnostics off) is pass-through only ---- */
	aod_gldiag_set_enabled(0);
	aod_gxmdiag_set_verbose(0);
	clear_marker_rect();
	int dumps0 = n_dumps;
	before = nlog;
	int draws0 = n_draw;
	ncap = 0;
	for (unsigned long f = 0; f <= 460; f++) {
		aod_gldiag_frame_begin(f);
		aod_diag_glUseProgram(2);
		aod_diag_glClear(0x4100);
		aod_diag_glDrawElements(4, 6, 0x1403, 0);
		aod_diag_glUniform4fv(3, 1, col);
		aod_diag_glTexImage2D(0xDE1, 0, 0x1908, 64, 32, 0, 0x1908, 0x1401, rgba);
		aod_diag_glBindBuffer(0x8892, 5); aod_diag_glBufferData(0x8892, sizeof verts, verts, 0x88E4);
		aod_diag_glDepthFunc(0x203);
		aod_gldiag_before_swap(f);
		aod_gldiag_display_cb(fbmem);          /* a valid buffer: only the enable gate keeps it untouched */
		aod_gldiag_frame_end(f);
	}
	CHECK(nlog == before, "diagnostics off: no log lines over 461 frames incl. trace/summary/arm frames (%d)", nlog - before);
	CHECK(ncap == 0 && !(clear_cc[0] == 1.f && clear_cc[1] == 0.f && clear_cc[2] == 1.f), "diagnostics off: no A/B state changes, no magenta clear (%d, %.1f,%.1f,%.1f)", ncap, clear_cc[0], clear_cc[1], clear_cc[2]);
	CHECK(!marker_at(8, 8) && !marker_at(39, 39) && n_dumps == dumps0, "diagnostics off: no marker written, no dumps");
	CHECK(n_draw - draws0 == 461 && last_u4_loc == 3 && tex_w == 64, "diagnostics off: calls still pass through");
	next_err = 0x505; before = nlog;
	CHECK(aod_diag_glGetError() == 0x505, "game glGetError still passed through");

	printf("%d/%d checks passed (gl diag)\n", checks - fails, checks);
	return fails != 0;
}
