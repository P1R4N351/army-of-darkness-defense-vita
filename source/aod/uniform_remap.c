/*
 * aod-vita: game-facing dense uniform locations over vitaGL. See uniform_remap.h.
 *
 * Per linked program, the active uniforms are enumerated once (GL_ACTIVE_UNIFORMS +
 * glGetActiveUniform) in index order; each uniform gets a base id and an array of `size`
 * reserves `size` consecutive ids, so "u", "u[0]" -> base and "u[i]" -> base+i no matter
 * which name the game looks up first. glUniform* translate an id of the current program
 * (tracked through glUseProgram) back to a vitaGL location; -1 and ids outside the current
 * program's table are no-ops. A raw vitaGL location is never handed to the game.
 *
 * Backend: vitaGL built with SAFE_UNIFORMS=1 (STRICT_UNIFORMS_COMPLIANCE). Its location is
 * `offset:12 | program_idx:10 | uniform_idx:8 | is_vertex:1`, and glUniform* pass `offset` to
 * vglSetUniformData, which advances by vector rows (F32: 8 bytes for 1-2 components, 16 for 3-4).
 * vglSetUniformData's offset unit depends on the parameter's GXM type (the type
 * glGetActiveUniform reports): F32/F16/16-bit/8-bit advance one element per unit, S32/U32
 * advance `componentCount` bytes per unit while elements are packed at componentCount*4 bytes.
 * Element i of an array is therefore `base + i * units_per_element` with units = 2/3/4 for
 * mat2/3/4 (rows), 4 for GL_INT* (S32/U32), 1 otherwise; vitaGL's own "name[i]" parsing does not
 * apply these factors, so it is not used. Samplers (size 1) ignore the offset entirely.
 * `count` is clamped to the elements remaining in the array, as GLES specifies.
 * vitaGL ignores location 0, which strict packing yields for program id 1's first fragment
 * uniform, so program id 1 is reserved at the first glCreateProgram and never used.
 *
 * Single-threaded by design: libgame issues GL from the render thread only.
 *
 * This file is part of aod-vita and is distributed under the MIT license.
 */
#include "aod/uniform_remap.h"
#include "aod/port.h"

#include <stdlib.h>
#include <string.h>

#define GL_LINK_STATUS_ 0x8B82
#define GL_ACTIVE_UNIFORMS_ 0x8B86

typedef struct {
	char name[AOD_UREMAP_NAME_MAX];   /* base name, without "[0]" */
	aod_glint vloc;                   /* vitaGL location of element 0 (may be -1) */
	int size;                         /* array size (1 for scalars) */
	int base_id;
	int rows;                         /* offset units per array element */
} uentry;

typedef struct {
	int n;
	int next_id;
	int overflowed;                   /* enumeration hit capacity: no lazy appends */
	uentry e[AOD_UREMAP_MAX_UNIFORMS];
} ptable;

static aod_uremap_backend be;
static ptable *tables[AOD_UREMAP_MAX_PROGRAMS + 1];
static aod_gluint cur_prog;
static aod_gluint pending_delete;   /* deleted while current: freed when no longer in use */
static aod_uremap_stats st;
static int reserved_program_one;

#define GL_FLOAT_MAT2_ 0x8B5A
#define GL_FLOAT_MAT3_ 0x8B5B
#define GL_FLOAT_MAT4_ 0x8B5C
#define STRICT_OFFSET_MAX 4095    /* 12-bit offset field */

#define GL_INT_ 0x1404
#define GL_INT_VEC2_ 0x8B53
#define GL_INT_VEC3_ 0x8B54
#define GL_INT_VEC4_ 0x8B55

/* offset units per array element, from the type vitaGL reports (derived from the GXM type) */
static int rows_for_type(aod_glenum type) {
	switch (type) {
	case GL_FLOAT_MAT4_: return 4;
	case GL_FLOAT_MAT3_: return 3;
	case GL_FLOAT_MAT2_: return 2;
	case GL_INT_: case GL_INT_VEC2_: case GL_INT_VEC3_: case GL_INT_VEC4_: return 4;   /* S32/U32 */
	default: return 1;
	}
}

void aod_uremap_set_backend(const aod_uremap_backend *b) { be = *b; }
aod_uremap_stats aod_uremap_get_stats(void) { return st; }

void aod_uremap_log_stats(void) {
	aod_log("uniform remap: lookups=%u misses=%u lazy=%u capfail=%u unresolved=%u translated=%u ignored(-1)=%u ignored(unknown)=%u",
	        st.lookups, st.lookup_misses, st.lazy_appends, st.capacity_failures, st.unresolved, st.calls_translated,
	        st.calls_ignored_minus1, st.calls_ignored_unknown);
}

static int prog_ok(aod_gluint p) { return p >= 1 && p <= AOD_UREMAP_MAX_PROGRAMS; }

static void table_free(aod_gluint p) {
	if (!prog_ok(p)) return;
	free(tables[p]);
	tables[p] = NULL;
}

/* Split "name", "name[3]" into base and index. Returns 0 if the syntax is not name or name[N]. */
static int split_name(const char *in, char *base, size_t cap, int *index) {
	const char *br = strchr(in, '[');
	*index = 0;
	if (!br) {
		if (strlen(in) >= cap) return 0;
		strcpy(base, in);
		return 1;
	}
	size_t bl = (size_t)(br - in);
	if (bl == 0 || bl >= cap) return 0;
	const char *p = br + 1;
	if (*p < '0' || *p > '9') return 0;
	long v = 0;
	while (*p >= '0' && *p <= '9') { v = v * 10 + (*p - '0'); if (v > 100000) return 0; p++; }
	if (p[0] != ']' || p[1] != 0) return 0;   /* struct members etc. are not handled */
	memcpy(base, in, bl);
	base[bl] = 0;
	*index = (int)v;
	return 1;
}

static uentry *add_entry(ptable *t, const char *base, aod_glint vloc, int size, int rows) {
	if (size < 1) size = 1;
	if (rows < 1) rows = 1;
	if (t->n >= AOD_UREMAP_MAX_UNIFORMS || t->next_id + size > AOD_UREMAP_MAX_IDS ||
	    (size - 1) * rows > STRICT_OFFSET_MAX) {
		st.capacity_failures++;
		t->overflowed = 1;
		aod_log("uniform remap: capacity exceeded adding '%s' (uniforms %d, ids %d, size %d)", base, t->n, t->next_id, size);
		return NULL;
	}
	uentry *e = &t->e[t->n++];
	memset(e, 0, sizeof(*e));
	strncpy(e->name, base, sizeof(e->name) - 1);
	e->vloc = vloc;
	e->size = size;
	e->rows = rows;
	e->base_id = t->next_id;
	t->next_id += size;
	return e;
}

static uentry *find_entry(ptable *t, const char *base) {
	for (int i = 0; i < t->n; i++)
		if (strcmp(t->e[i].name, base) == 0) return &t->e[i];
	return NULL;
}

static void build_table(aod_gluint p) {
	table_free(p);
	if (!prog_ok(p) || !be.get_programiv) return;
	aod_glint linked = 0, count = 0;
	be.get_programiv(p, GL_LINK_STATUS_, &linked);
	if (!linked) return;
	ptable *t = calloc(1, sizeof(ptable));
	if (!t) { aod_log("uniform remap: out of memory for program %u", p); return; }
	be.get_programiv(p, GL_ACTIVE_UNIFORMS_, &count);
	for (aod_glint i = 0; i < count; i++) {
		char name[AOD_UREMAP_NAME_MAX] = {0}, base[AOD_UREMAP_NAME_MAX];
		aod_glsizei len = 0;
		aod_glint size = 1;
		aod_glenum type = 0;
		be.get_active_uniform(p, (aod_gluint)i, sizeof(name) - 1, &len, &size, &type, name);
		int idx;
		if (!split_name(name, base, sizeof(base), &idx) || idx != 0) {
			aod_log("uniform remap: program %u active uniform '%s' has an unsupported name", p, name);
			continue;
		}
		if (find_entry(t, base)) continue;
		aod_glint v = be.get_uniform_location(p, base);
		if (v == -1 || v == 0) {
			/* Active but not resolvable by the backend (0 would be ignored by vitaGL): the game
			 * gets -1 for it, exactly as for an unknown name; no id is reserved. */
			st.unresolved++;
			aod_log("uniform remap: program %u active uniform '%s' has no usable backend location (%d); lookups return -1",
			        p, base, v);
			continue;
		}
		if (!add_entry(t, base, v, size, rows_for_type(type))) break;
	}
	tables[p] = t;
	aod_log("uniform remap: program %u -> %d uniforms, %d ids", p, t->n, t->next_id);
}

aod_glint aod_glGetUniformLocation(aod_gluint prog, const char *name) {
	st.lookups++;
	char base[AOD_UREMAP_NAME_MAX];
	int idx;
	ptable *t = prog_ok(prog) ? tables[prog] : NULL;
	if (!t || !name || !split_name(name, base, sizeof(base), &idx)) {
		st.lookup_misses++;
		return -1;
	}
	uentry *e = find_entry(t, base);
	if (!e) {
		/* Not in the active list (e.g. vitaGL lists it under another name): ask vitaGL once.
		 * Its array size is unknown here, so it is treated as a single element. Refused if the
		 * enumeration overflowed, so a dropped array cannot come back with the wrong size. */
		if (t->overflowed) { st.lookup_misses++; return -1; }
		aod_glint v = be.get_uniform_location(prog, base);
		if (v == -1 || v == 0) { st.lookup_misses++; return -1; }
		e = add_entry(t, base, v, 1, 1);
		if (!e) return -1;
		st.lazy_appends++;
		aod_log("uniform remap: program %u '%s' not in the active list; appended as a single element", prog, base);
	}
	if (idx >= e->size) { st.lookup_misses++; return -1; }
	return e->base_id + idx;
}

/* Resolve a game id in the current program to a vitaGL location. *remaining = elements from
 * this one to the end of its array (1 for scalars). Returns -1 when the call must be skipped. */
static aod_glint resolve(aod_glint id, int *remaining) {
	*remaining = 0;
	if (id == -1) { st.calls_ignored_minus1++; return -1; }
	ptable *t = prog_ok(cur_prog) ? tables[cur_prog] : NULL;
	if (!t || id < 0 || id >= t->next_id) { st.calls_ignored_unknown++; return -1; }
	for (int i = 0; i < t->n; i++) {
		uentry *e = &t->e[i];
		if (id >= e->base_id && id < e->base_id + e->size) {
			if (e->vloc == -1 || e->vloc == 0) { st.calls_ignored_unknown++; return -1; }
			int elem = id - e->base_id;
			*remaining = e->size - elem;
			st.calls_translated++;
			return e->vloc + elem * e->rows;   /* offset field is the low 12 bits; bounded at add */
		}
	}
	st.calls_ignored_unknown++;
	return -1;
}

void aod_glUseProgram(aod_gluint prog) {
	if (pending_delete && pending_delete != prog) {
		table_free(pending_delete);
		pending_delete = 0;
	}
	cur_prog = prog;
	be.use_program(prog);
}

void aod_glLinkProgram(aod_gluint prog) {
	be.link_program(prog);
	if (pending_delete == prog) pending_delete = 0;
	build_table(prog);
}

void aod_glDeleteProgram(aod_gluint prog) {
	if (prog == 1 && reserved_program_one) return;   /* never handed out; never freed */
	be.delete_program(prog);
	if (prog == 0) return;
	if (prog == cur_prog) pending_delete = prog;   /* GL: stays usable while current */
	else table_free(prog);
}

aod_gluint aod_glCreateProgram(void) {
	aod_gluint p = be.create_program();
	if (p == 1 && !reserved_program_one) {
		/* strict vitaGL: program id 1 can yield location 0, which vitaGL ignores. Keep it. */
		reserved_program_one = 1;
		aod_log("uniform remap: program id 1 reserved (strict location 0 is ignored by vitaGL)");
		p = be.create_program();
	}
	if (pending_delete == p) pending_delete = 0;
	table_free(p);   /* a reused id must not inherit an old table */
	return p;
}

#define U_ONE(fn, ...) do { int r_; aod_glint v_ = resolve(l, &r_); if (v_ != -1 && be.fn) be.fn(v_, __VA_ARGS__); } while (0)
/* GLES: elements past the end of the array are ignored, so count is clamped to what remains. */
#define U_VEC(fn, ...) do { \
		int r_; \
		aod_glint v_ = resolve(l, &r_); \
		if (v_ == -1 || !be.fn || n <= 0) break; \
		if (n > r_) n = r_; \
		be.fn(v_, n, __VA_ARGS__); \
	} while (0)
void aod_glUniform1f(aod_glint l, float a) { U_ONE(u1f, a); }
void aod_glUniform2f(aod_glint l, float a, float b) { U_ONE(u2f, a, b); }
void aod_glUniform3f(aod_glint l, float a, float b, float c) { U_ONE(u3f, a, b, c); }
void aod_glUniform4f(aod_glint l, float a, float b, float c, float d) { U_ONE(u4f, a, b, c, d); }
void aod_glUniform1i(aod_glint l, aod_glint a) { U_ONE(u1i, a); }
void aod_glUniform2i(aod_glint l, aod_glint a, aod_glint b) { U_ONE(u2i, a, b); }
void aod_glUniform3i(aod_glint l, aod_glint a, aod_glint b, aod_glint c) { U_ONE(u3i, a, b, c); }
void aod_glUniform4i(aod_glint l, aod_glint a, aod_glint b, aod_glint c, aod_glint d) { U_ONE(u4i, a, b, c, d); }
void aod_glUniform1fv(aod_glint l, aod_glsizei n, const float *v) { U_VEC(u1fv, v); }
void aod_glUniform2fv(aod_glint l, aod_glsizei n, const float *v) { U_VEC(u2fv, v); }
void aod_glUniform3fv(aod_glint l, aod_glsizei n, const float *v) { U_VEC(u3fv, v); }
void aod_glUniform4fv(aod_glint l, aod_glsizei n, const float *v) { U_VEC(u4fv, v); }
void aod_glUniform1iv(aod_glint l, aod_glsizei n, const aod_glint *v) { U_VEC(u1iv, v); }
void aod_glUniform2iv(aod_glint l, aod_glsizei n, const aod_glint *v) { U_VEC(u2iv, v); }
void aod_glUniform3iv(aod_glint l, aod_glsizei n, const aod_glint *v) { U_VEC(u3iv, v); }
void aod_glUniform4iv(aod_glint l, aod_glsizei n, const aod_glint *v) { U_VEC(u4iv, v); }
void aod_glUniformMatrix2fv(aod_glint l, aod_glsizei n, aod_glboolean t, const float *v) { U_VEC(um2fv, t, v); }
void aod_glUniformMatrix3fv(aod_glint l, aod_glsizei n, aod_glboolean t, const float *v) { U_VEC(um3fv, t, v); }
void aod_glUniformMatrix4fv(aod_glint l, aod_glsizei n, aod_glboolean t, const float *v) { U_VEC(um4fv, t, v); }
