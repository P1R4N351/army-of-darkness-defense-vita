/*
 * aod-vita: binds source/aod/uniform_remap.c to vitaGL (built with SAFE_UNIFORMS=1).
 * This file is part of aod-vita and is distributed under the MIT license.
 */
#include "aod/uniform_remap.h"
#include "utils/glutil.h"
#include <vitaGL.h>

#ifndef STRICT_UNIFORMS_COMPLIANCE_EXPECTED
#define STRICT_UNIFORMS_COMPLIANCE_EXPECTED 1
#endif

static void get_active_uniform(aod_gluint p, aod_gluint i, aod_glsizei bs, aod_glsizei *len, aod_glint *size,
                               aod_glenum *type, char *name) {
	glGetActiveUniform(p, i, bs, len, size, type, name);
}

void aod_uremap_install_vitagl(void) {
	aod_uremap_backend b = {
		.get_uniform_location = (aod_glint (*)(aod_gluint, const char *))glGetUniformLocation,
		.get_programiv = (void (*)(aod_gluint, aod_glenum, aod_glint *))glGetProgramiv,
		.get_active_uniform = get_active_uniform,
		.use_program = glUseProgram,
		.link_program = glLinkProgram_soloader,   /* keeps the link-status logging */
		.delete_program = glDeleteProgram,
		.create_program = glCreateProgram,
		.u1f = glUniform1f, .u2f = glUniform2f, .u3f = glUniform3f, .u4f = glUniform4f,
		.u1i = glUniform1i, .u2i = glUniform2i, .u3i = glUniform3i, .u4i = glUniform4i,
		.u1fv = glUniform1fv, .u2fv = glUniform2fv, .u3fv = glUniform3fv, .u4fv = glUniform4fv,
		.u1iv = glUniform1iv, .u2iv = glUniform2iv, .u3iv = glUniform3iv, .u4iv = glUniform4iv,
		.um2fv = glUniformMatrix2fv, .um3fv = glUniformMatrix3fv, .um4fv = glUniformMatrix4fv,
	};
	aod_uremap_set_backend(&b);
}
