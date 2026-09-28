/*
 * Host test for source/aod/uniform_remap.c (boot-04 crash: libgame indexed a table with a vitaGL
 * uniform location). The mock backend transcribes vitaGL 9c23758 built with SAFE_UNIFORMS=1
 * (STRICT_UNIFORMS_COMPLIANCE), from lib/vitagl/source/custom_shaders.c and gxm.c:
 *   - glGetUniformLocation: raw = offset:12 | program_idx:10 | uniform_idx:8 | is_vertex:1,
 *     "name[i]" sets offset = i (unscaled), unknown -> -1
 *   - glUniform*: location -1 or 0 -> ignored; offset passed to vglSetUniformData
 *   - vglSetUniformData F32: buffer += (components < 3 ? 8 : 16) * offset; count elements copied
 *     (matrices are written as rows: mat4 = 4 rows of 4, count 4*n)
 * Each mock uniform owns a byte buffer so tests check the bytes a real update would write.
 */
#include "aod/uniform_remap.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>

void aod_log(const char *fmt, ...) { (void)fmt; }

static int fails, checks;
#define CHECK(c, ...) do { checks++; if (!(c)) { fails++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

/* ---------------- mock vitaGL (strict) ---------------- */
#define MAT4 0x8B5C
#define VEC4 0x8B52
#define FLT  0x1406
/* gtype: 0/'F' = SCE_GXM_PARAMETER_TYPE_F32, 'S' = S32; sampler: sampler parameter; unres: the
 * backend's glGetUniformLocation cannot resolve the (enumerated) name */
typedef struct { const char *name; int size; unsigned type; int comps; int is_vertex; int gtype; int sampler; int unres;
                 int sampler_index; uint8_t buf[1024]; } munif;
typedef struct { int used, linked; munif u[8]; int n; } mprog;
static mprog mp[64];
static int n_backend_calls;

static aod_glint pack(int prog, int ui, int off) {   /* uniform_location union, little-endian bitfields */
	return (aod_glint)((off & 0xFFF) | ((prog - 1) & 0x3FF) << 12 | (ui & 0xFF) << 22 | (mp[prog].u[ui].is_vertex & 1) << 30);
}
static munif *unpack(aod_glint raw, int *off, int *prog_out) {
	int prog = ((raw >> 12) & 0x3FF) + 1, ui = (raw >> 22) & 0xFF;
	*off = raw & 0xFFF; *prog_out = prog;
	return &mp[prog].u[ui];
}
static aod_glint m_getloc(aod_gluint p, const char *name) {
	char tmp[64]; int index = 0;
	snprintf(tmp, sizeof tmp, "%s", name);
	char *br = strchr(tmp, '[');
	if (br) { index = atoi(br + 1); *br = 0; }            /* vitaGL: offset = index, unscaled */
	for (int i = 0; i < mp[p].n; i++) if (!strcmp(mp[p].u[i].name, tmp)) return mp[p].u[i].unres ? -1 : pack((int)p, i, index);
	return -1;
}
static void m_getiv(aod_gluint p, aod_glenum pn, aod_glint *o) { *o = pn == 0x8B82 ? mp[p].linked : pn == 0x8B86 ? mp[p].n : 0; }
static void m_active(aod_gluint p, aod_gluint i, aod_glsizei bs, aod_glsizei *len, aod_glint *size, aod_glenum *type, char *name) {
	munif *u = &mp[p].u[i];
	snprintf(name, bs, "%s", u->name); *len = (aod_glsizei)strlen(name);
	/* gxm_unif_type_to_gl: S32/U32 -> GL_INT*, F32 -> GL_FLOAT*; samplers GL_SAMPLER_2D size 1;
	 * matrices keep the declared MAT type (vitaGL's gxm_unif_to_mat) */
	*size = u->sampler ? 1 : u->size;
	if (u->sampler) *type = 0x8B5E;
	else if (u->type == MAT4) *type = MAT4;
	else if (u->gtype == 'S') *type = u->comps == 1 ? 0x1404 : 0x8B53 + (u->comps - 2);
	else *type = u->comps == 1 ? FLT : 0x8B50 + (u->comps - 2);
}
static aod_gluint mock_cur;
static void m_use(aod_gluint p) { mock_cur = p; }
static void m_link(aod_gluint p) { mp[p].linked = 1; }
static void m_delete(aod_gluint p) { mp[p].used = 0; mp[p].linked = 0; }
static aod_gluint m_create(void) { for (aod_gluint i = 1; i < 64; i++) if (!mp[i].used) { memset(&mp[i], 0, sizeof mp[i]); mp[i].used = 1; return i; } return 0; }
/* vglSetUniformData (gxm_utils.c), F32 and S32 parameter types, fast and converting paths */
static void mock_set(aod_glint raw, int comps, int count, const void *srcv, int input_int) {
	n_backend_calls++;
	if (raw == -1 || raw == 0) return;                    /* vitaGL: location -1 or 0 ignored */
	int off, prog; munif *u = unpack(raw, &off, &prog);
	int t_int = u->gtype == 'S';
	uint8_t *dst = u->buf + (t_int ? comps * off : (comps < 3 ? 8 : 16) * off);
	uint8_t *end = u->buf + sizeof u->buf;
	for (int i = 0; i < count; i++) {
		int stride = t_int ? comps * 4 : (comps < 3 ? 8 : 16);   /* S32 packed; F32 8/16 (2,4 contiguous) */
		if (dst + comps * 4 > end) { printf("mock overflow\n"); return; }
		for (int c = 0; c < comps; c++) {
			if (t_int) { int32_t v = input_int ? ((const int32_t *)srcv)[i * comps + c] : (int32_t)((const float *)srcv)[i * comps + c]; memcpy(dst + 4 * c, &v, 4); }
			else { float v = input_int ? (float)((const int32_t *)srcv)[i * comps + c] : ((const float *)srcv)[i * comps + c]; memcpy(dst + 4 * c, &v, 4); }
		}
		dst += stride;
	}
}
static void m_u4fv(aod_glint l, aod_glsizei n, const float *v) { mock_set(l, 4, n, v, 0); }
static void m_u1fv(aod_glint l, aod_glsizei n, const float *v) { mock_set(l, 1, n, v, 0); }
static void m_u1f(aod_glint l, float a) { mock_set(l, 1, 1, &a, 0); }
static void m_um4fv(aod_glint l, aod_glsizei n, aod_glboolean t, const float *v) { mock_set(l, 4, 4 * n, v, 0); }
static void m_u2iv(aod_glint l, aod_glsizei n, const aod_glint *v) { mock_set(l, 2, n, v, 1); }
/* glUniform1i / glUniform1iv: samplers take sampler_index = value[0], offset ignored */
static aod_glint last_sampler_loc;
static void m_u1iv(aod_glint l, aod_glsizei n, const aod_glint *v) {
	if (l == -1 || l == 0) { n_backend_calls++; return; }
	int off, prog; munif *u = unpack(l, &off, &prog);
	if (u->sampler) { n_backend_calls++; last_sampler_loc = l; u->sampler_index = v[0]; return; }
	mock_set(l, 1, n, v, 1);
}
static void m_u1i(aod_glint l, aod_glint a) { m_u1iv(l, 1, &a); }

static void setprog(aod_gluint p, int n, const munif *u) { mp[p].n = n; for (int i = 0; i < n; i++) mp[p].u[i] = u[i]; }
static float rowf(munif *u, int unit, int row, int c) { float f; memcpy(&f, u->buf + unit * row + 4 * c, 4); return f; }
static int32_t inti(munif *u, int byte) { int32_t v; memcpy(&v, u->buf + byte, 4); return v; }

int main(void) {
	aod_uremap_backend b = {0};
	b.get_uniform_location = m_getloc; b.get_programiv = m_getiv; b.get_active_uniform = m_active;
	b.use_program = m_use; b.link_program = m_link; b.delete_program = m_delete; b.create_program = m_create;
	b.u4fv = m_u4fv; b.u1fv = m_u1fv; b.u1f = m_u1f; b.um4fv = m_um4fv;
	b.u2iv = m_u2iv; b.u1iv = m_u1iv; b.u1i = m_u1i;
	aod_uremap_set_backend(&b);

	/* Program id 1 is reserved: its first fragment uniform would be strict location 0 (ignored) */
	aod_gluint A = aod_glCreateProgram();
	CHECK(A == 2 && mp[1].used, "program id 1 reserved, first game program is 2 (got %u)", A);
	munif ua[] = { {"u_color", 1, VEC4, 4, 0}, {"u_mvp", 1, MAT4, 16, 1}, {"u_bones", 4, MAT4, 16, 1}, {"u_w", 3, FLT, 1, 0} };
	setprog(A, 4, ua);

	/* P1: backend locations are not small (as in the boot-04 cores); ids are */
	aod_glint raw = m_getloc(A, "u_bones");
	CHECK(raw >= (1 << 12), "strict raw location is large (%#x)", raw);
	CHECK(aod_glGetUniformLocation(A, "u_color") == -1, "lookup before link -> -1");
	aod_glLinkProgram(A);

	/* P3 alias/order: element before base, base, [0] */
	aod_glint b2 = aod_glGetUniformLocation(A, "u_bones[2]");
	aod_glint bb = aod_glGetUniformLocation(A, "u_bones"), b0 = aod_glGetUniformLocation(A, "u_bones[0]");
	CHECK(bb == b0 && b2 == bb + 2, "name/name[0]/name[i] -> base/base/base+i (%d %d %d)", bb, b0, b2);
	CHECK(aod_glGetUniformLocation(A, "u_bones[4]") == -1, "element past size -> -1");
	/* P2 dense ids in active order; arrays reserve size */
	aod_glint col = aod_glGetUniformLocation(A, "u_color"), mvp = aod_glGetUniformLocation(A, "u_mvp"), w = aod_glGetUniformLocation(A, "u_w");
	CHECK(col == 0 && mvp == 1 && bb == 2 && w == 6, "dense ids (%d %d %d %d)", col, mvp, bb, w);
	/* game-style: the table the crash site indexed; ids fit, the raw value would not */
	unsigned long long table[8] = {0};
	aod_glint ids[] = { col, mvp, bb, b2, w };
	int ok = 1;
	for (unsigned k = 0; k < 5; k++) { if (ids[k] < 0 || ids[k] >= 8) ok = 0; else table[ids[k]] = k; }
	CHECK(ok && (unsigned)raw >= 8u, "ids index an 8-slot table; raw location would not (boot-04 store)");
	/* location-equality match used by libgame attachAndLink: active name vs name[0] */
	char nm[64]; aod_glsizei ln; aod_glint sz; aod_glenum ty;
	m_active(A, 2, sizeof nm, &ln, &sz, &ty, nm);
	CHECK(aod_glGetUniformLocation(A, nm) == aod_glGetUniformLocation(A, "u_bones[0]"), "active name == name[0]");

	aod_glUseProgram(A);
	float m[4 * 16];
	for (int i = 0; i < 4 * 16; i++) m[i] = (float)(100 + i);

	/* Nonzero array element, bulk count: bones[2..3] from id base+2, count 2 (mat4 = 4 rows) */
	munif *ub = &mp[A].u[2];
	memset(ub->buf, 0, sizeof ub->buf);
	aod_glUniformMatrix4fv(b2, 2, 0, m);
	CHECK(rowf(ub, 16, 8, 0) == 100.0f && rowf(ub, 16, 11, 3) == 115.0f, "bones[2] written at rows 8..11 (mat4 stride)");
	CHECK(rowf(ub, 16, 12, 0) == 116.0f && rowf(ub, 16, 15, 3) == 131.0f, "bones[3] written at rows 12..15");
	CHECK(rowf(ub, 16, 7, 3) == 0.0f && rowf(ub, 16, 0, 0) == 0.0f, "bones[0..1] untouched");
	/* count past the end is clamped (GLES): bones[3] with count 3 writes only bones[3] */
	memset(ub->buf, 0, sizeof ub->buf);
	aod_glUniformMatrix4fv(bb + 3, 3, 0, m);
	CHECK(rowf(ub, 16, 12, 0) == 100.0f && rowf(ub, 16, 15, 3) == 115.0f, "bones[3] written");
	CHECK(rowf(ub, 16, 16, 0) == 0.0f, "no write past the array (count clamped 3 -> 1)");
	/* whole array from base */
	memset(ub->buf, 0, sizeof ub->buf);
	aod_glUniformMatrix4fv(bb, 4, 0, m);
	CHECK(rowf(ub, 16, 0, 0) == 100.0f && rowf(ub, 16, 15, 3) == 163.0f, "whole bones array from base");
	/* scalar float array element: u_w[1..2] (F32 1 comp -> 8-byte units per vitaGL) */
	munif *uw = &mp[A].u[3];
	float fw[3] = { 1.5f, 2.5f, 3.5f };
	aod_glUniform1fv(w + 1, 5, fw);
	CHECK(rowf(uw, 8, 1, 0) == 1.5f && rowf(uw, 8, 2, 0) == 2.5f && rowf(uw, 8, 0, 0) == 0.0f && rowf(uw, 8, 3, 0) == 0.0f,
	      "u_w[1..2] written, clamped to array end");
	/* vec4 scalar uniform */
	float c4[4] = { 0.25f, 0.5f, 0.75f, 1.0f };
	aod_glUniform4fv(col, 1, c4);
	CHECK(rowf(&mp[A].u[0], 16, 0, 2) == 0.75f, "u_color written");
	/* -1 / out-of-table ids never reach the backend */
	n_backend_calls = 0;
	aod_glUniform4fv(-1, 1, c4); aod_glUniform4fv(99, 1, c4); aod_glUniform1f(-5, 1.0f);
	CHECK(n_backend_calls == 0, "-1 and ids outside the table are no-ops");

	/* P5 per-program: B's id 0 resolves in B only while B is current */
	aod_gluint B = aod_glCreateProgram();
	munif ubp[] = { {"u_color", 1, VEC4, 4, 0} };
	setprog(B, 1, ubp);
	aod_glLinkProgram(B);
	CHECK(aod_glGetUniformLocation(B, "u_color") == 0, "B dense id 0");
	aod_glUseProgram(B);
	float z4[4] = { 9, 9, 9, 9 };
	aod_glUniform4fv(0, 1, z4);
	CHECK(rowf(&mp[B].u[0], 16, 0, 0) == 9.0f && rowf(&mp[A].u[0], 16, 0, 0) == 0.25f, "id 0 wrote B's uniform, not A's");

	/* P6 lifecycle: delete while current stays usable; after switching away the table is gone.
	 * (Stale integers after a relink cannot be told apart from new ids of the same value; only
	 * fresh mapping and current-program correctness are asserted.) */
	aod_glDeleteProgram(B);
	n_backend_calls = 0;
	aod_glUniform4fv(0, 1, z4);
	CHECK(n_backend_calls == 1, "deleted-but-current program still translates");
	aod_glUseProgram(A);
	aod_glUseProgram(B);
	n_backend_calls = 0;
	aod_glUniform4fv(0, 1, z4);
	CHECK(n_backend_calls == 0, "after switching away, the deleted program has no table");
	aod_gluint C = aod_glCreateProgram();
	CHECK(C == B, "backend reuses the program id");
	munif ucp[] = { {"u_x", 1, VEC4, 4, 0}, {"u_color", 1, VEC4, 4, 0} };
	setprog(C, 2, ucp);
	CHECK(aod_glGetUniformLocation(C, "u_color") == -1, "reused id starts without a table");
	aod_glLinkProgram(C);
	CHECK(aod_glGetUniformLocation(C, "u_color") == 1, "fresh mapping after link (u_color now id 1)");
	aod_glUseProgram(C);
	aod_glUniform4fv(1, 1, c4);
	CHECK(rowf(&mp[C].u[1], 16, 0, 1) == 0.5f, "fresh mapping translates to the new uniform");
	/* reserved id 1 is never deleted or used */
	aod_glDeleteProgram(1);
	CHECK(mp[1].used, "reserved program 1 not deletable through the game");

	/* lazy append (name vitaGL resolves but did not enumerate) and capacity */
	mp[C].u[mp[C].n++] = (munif){ "late", 1, VEC4, 4, 0 };
	CHECK(aod_glGetUniformLocation(C, "late") == 2, "lazy append gets next dense id");
	aod_gluint D = aod_glCreateProgram();
	munif ud[] = { {"small", 1, VEC4, 4, 0}, {"huge", AOD_UREMAP_MAX_IDS, VEC4, 4, 0} };
	setprog(D, 2, ud);
	aod_glLinkProgram(D);
	CHECK(aod_glGetUniformLocation(D, "small") == 0, "entries before overflow kept");
	CHECK(aod_glGetUniformLocation(D, "huge") == -1 && aod_uremap_get_stats().capacity_failures >= 1, "overflowed array -> -1 (no lazy re-add)");
	CHECK(aod_glGetUniformLocation(0, "x") == -1 && aod_glGetUniformLocation(5000, "x") == -1, "invalid program ids -> -1");

	/* ---- integer arrays, samplers, unresolved active uniforms (program E) ---- */
	aod_gluint E = aod_glCreateProgram();
	munif ue[] = {
		{ "u_iv", 3, 0, 2, 0, 'S' },            /* ivec2[3] backed by S32 */
		{ "u_i", 4, 0, 1, 0, 'S' },             /* int[4] backed by S32 */
		{ "u_fv", 3, 0, 2, 0, 'F' },            /* GLSL ivec2 that the backend holds as F32 */
		{ "u_tex", 1, 0, 1, 0, 0, 1 },          /* sampler */
		{ "u_gone", 2, 0, 4, 0, 'F', 0, 1 },    /* enumerated, but backend location is -1 */
		{ "u_after", 1, 0, 4, 0, 'F' },
	};
	setprog(E, 6, ue);
	aod_glLinkProgram(E);
	aod_glint iv = aod_glGetUniformLocation(E, "u_iv"), ii = aod_glGetUniformLocation(E, "u_i");
	aod_glint fv = aod_glGetUniformLocation(E, "u_fv"), tx = aod_glGetUniformLocation(E, "u_tex");
	/* (1) enumerated but unresolved -> -1 for every alias; no id reserved; later ids stay dense */
	CHECK(aod_glGetUniformLocation(E, "u_gone") == -1 && aod_glGetUniformLocation(E, "u_gone[0]") == -1 &&
	      aod_glGetUniformLocation(E, "u_gone[1]") == -1, "unresolved active uniform -> -1 (name, [0], [1])");
	aod_glint after = aod_glGetUniformLocation(E, "u_after");
	CHECK(iv == 0 && ii == 3 && fv == 7 && tx == 10 && after == 11, "dense ids around the unresolved one (%d %d %d %d %d)", iv, ii, fv, tx, after);
	CHECK(aod_uremap_get_stats().unresolved == 1, "unresolved counted");
	aod_glUseProgram(E);
	/* (2) S32 ivec2 element 1..2 with bulk count: packed 8 bytes per element (componentCount*4) */
	munif *uiv = &mp[E].u[0];
	aod_glint vals[6] = { 11, 12, 21, 22, 31, 32 };
	aod_glUniform2iv(iv + 1, 5, vals);   /* count clamped to 2 remaining elements */
	CHECK(inti(uiv, 0) == 0 && inti(uiv, 4) == 0, "u_iv[0] untouched");
	CHECK(inti(uiv, 8) == 11 && inti(uiv, 12) == 12 && inti(uiv, 16) == 21 && inti(uiv, 20) == 22, "u_iv[1..2] written at bytes 8..23");
	CHECK(inti(uiv, 24) == 0, "no write past u_iv[2]");
	/* S32 int[4] element 2 */
	munif *ui = &mp[E].u[1];
	aod_glint one[2] = { 77, 78 };
	aod_glUniform1iv(ii + 2, 2, one);
	CHECK(inti(ui, 8) == 77 && inti(ui, 12) == 78 && inti(ui, 4) == 0 && inti(ui, 16) == 0, "u_i[2..3] written at bytes 8..15");
	/* F32-backed ivec2: element 1 at 8-byte stride, ints converted to floats */
	munif *ufv = &mp[E].u[2];
	aod_glint two[2] = { 7, 8 };
	aod_glUniform2iv(fv + 1, 1, two);
	CHECK(rowf(ufv, 8, 1, 0) == 7.0f && rowf(ufv, 8, 1, 1) == 8.0f && rowf(ufv, 8, 0, 0) == 0.0f, "F32-backed ivec2[1] written at byte 8 as floats");
	/* sampler: base location, offset 0, sampler_index set */
	aod_glUniform1i(tx, 3);
	CHECK(mp[E].u[3].sampler_index == 3 && (last_sampler_loc & 0xFFF) == 0, "sampler unit set via base location");
	CHECK(aod_glGetUniformLocation(E, "u_tex[1]") == -1, "sampler has a single id");
	/* 12-bit offset field: the id cap bounds every array to AOD_UREMAP_MAX_IDS elements, and the
	 * largest factor is 4 (mat4, S32), so the last element offset is at most 4*(1024-1) = 4092 */
	CHECK(4 * (AOD_UREMAP_MAX_IDS - 1) <= 4095, "max offset fits 12 bits by construction");

	printf("%d/%d checks passed (uniform remap, strict vitaGL mock)\n", checks - fails, checks);
	return fails != 0;
}
