/*
 * aod-vita: game-facing dense uniform locations over vitaGL (hardware boot-04 crash).
 *
 * vitaGL's glGetUniformLocation returns large values (non-strict: -(address of its uniform
 * record); strict/SAFE_UNIFORMS: packed program/uniform/offset bit fields). libgame indexes per-material tables with the location and
 * matches uniforms by location equality, as Android GLES drivers (small, dense, per-program
 * locations) allow. This layer hands the game dense per-program IDs and translates them back.
 *
 * This file is part of aod-vita and is distributed under the MIT license.
 */
#ifndef AOD_UNIFORM_REMAP_H
#define AOD_UNIFORM_REMAP_H

#include <stdint.h>

/* GL scalar types, spelled out so this module builds on the host without GL headers. */
typedef int aod_glint;
typedef unsigned int aod_gluint;
typedef int aod_glsizei;
typedef unsigned int aod_glenum;
typedef unsigned char aod_glboolean;

#define AOD_UREMAP_MAX_PROGRAMS 1024     /* vitaGL MAX_CUSTOM_PROGRAMS: ids 1..1024 */
#define AOD_UREMAP_MAX_UNIFORMS 128      /* distinct uniforms per program */
#define AOD_UREMAP_MAX_IDS      1024     /* dense ids per program (array elements included) */
#define AOD_UREMAP_NAME_MAX     64

typedef struct aod_uremap_backend {
	aod_glint (*get_uniform_location)(aod_gluint prog, const char *name);
	void (*get_programiv)(aod_gluint prog, aod_glenum pname, aod_glint *out);
	void (*get_active_uniform)(aod_gluint prog, aod_gluint index, aod_glsizei bufsize, aod_glsizei *length,
	                           aod_glint *size, aod_glenum *type, char *name);
	void (*use_program)(aod_gluint prog);
	void (*link_program)(aod_gluint prog);
	void (*delete_program)(aod_gluint prog);
	aod_gluint (*create_program)(void);

	void (*u1f)(aod_glint, float);
	void (*u2f)(aod_glint, float, float);
	void (*u3f)(aod_glint, float, float, float);
	void (*u4f)(aod_glint, float, float, float, float);
	void (*u1i)(aod_glint, aod_glint);
	void (*u2i)(aod_glint, aod_glint, aod_glint);
	void (*u3i)(aod_glint, aod_glint, aod_glint, aod_glint);
	void (*u4i)(aod_glint, aod_glint, aod_glint, aod_glint, aod_glint);
	void (*u1fv)(aod_glint, aod_glsizei, const float *);
	void (*u2fv)(aod_glint, aod_glsizei, const float *);
	void (*u3fv)(aod_glint, aod_glsizei, const float *);
	void (*u4fv)(aod_glint, aod_glsizei, const float *);
	void (*u1iv)(aod_glint, aod_glsizei, const aod_glint *);
	void (*u2iv)(aod_glint, aod_glsizei, const aod_glint *);
	void (*u3iv)(aod_glint, aod_glsizei, const aod_glint *);
	void (*u4iv)(aod_glint, aod_glsizei, const aod_glint *);
	void (*um2fv)(aod_glint, aod_glsizei, aod_glboolean, const float *);
	void (*um3fv)(aod_glint, aod_glsizei, aod_glboolean, const float *);
	void (*um4fv)(aod_glint, aod_glsizei, aod_glboolean, const float *);
} aod_uremap_backend;

void aod_uremap_set_backend(const aod_uremap_backend *b);

/* Counters for diagnostics/tests. */
typedef struct {
	unsigned lookups, lookup_misses, lazy_appends, capacity_failures, unresolved;
	unsigned calls_translated, calls_ignored_minus1, calls_ignored_unknown;
} aod_uremap_stats;
aod_uremap_stats aod_uremap_get_stats(void);
void aod_uremap_log_stats(void);

/* Game-facing entry points (installed in the loader's import table). */
aod_glint aod_glGetUniformLocation(aod_gluint prog, const char *name);
void aod_glUseProgram(aod_gluint prog);
void aod_glLinkProgram(aod_gluint prog);
void aod_glDeleteProgram(aod_gluint prog);
aod_gluint aod_glCreateProgram(void);

void aod_glUniform1f(aod_glint l, float a);
void aod_glUniform2f(aod_glint l, float a, float b);
void aod_glUniform3f(aod_glint l, float a, float b, float c);
void aod_glUniform4f(aod_glint l, float a, float b, float c, float d);
void aod_glUniform1i(aod_glint l, aod_glint a);
void aod_glUniform2i(aod_glint l, aod_glint a, aod_glint b);
void aod_glUniform3i(aod_glint l, aod_glint a, aod_glint b, aod_glint c);
void aod_glUniform4i(aod_glint l, aod_glint a, aod_glint b, aod_glint c, aod_glint d);
void aod_glUniform1fv(aod_glint l, aod_glsizei n, const float *v);
void aod_glUniform2fv(aod_glint l, aod_glsizei n, const float *v);
void aod_glUniform3fv(aod_glint l, aod_glsizei n, const float *v);
void aod_glUniform4fv(aod_glint l, aod_glsizei n, const float *v);
void aod_glUniform1iv(aod_glint l, aod_glsizei n, const aod_glint *v);
void aod_glUniform2iv(aod_glint l, aod_glsizei n, const aod_glint *v);
void aod_glUniform3iv(aod_glint l, aod_glsizei n, const aod_glint *v);
void aod_glUniform4iv(aod_glint l, aod_glsizei n, const aod_glint *v);
void aod_glUniformMatrix2fv(aod_glint l, aod_glsizei n, aod_glboolean t, const float *v);
void aod_glUniformMatrix3fv(aod_glint l, aod_glsizei n, aod_glboolean t, const float *v);
void aod_glUniformMatrix4fv(aod_glint l, aod_glsizei n, aod_glboolean t, const float *v);

#endif
