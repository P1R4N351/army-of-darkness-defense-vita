/*
 * MIT License
 *
 * Copyright (c) 2026 aod-input-support contributors
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * Mandatory build flag: -fno-fast-math
 * CMake: set_source_files_properties(source/aod/input_overlay.c
 *        PROPERTIES COMPILE_FLAGS "-fno-fast-math")
 *
 * VitaGL integration: GL void calls have no return to check. glPushAttrib /
 * glPopAttrib are NOT used (VitaGL attribute stack has a known off-by-one on
 * glPopAttrib). All state is saved/restored via explicit supported queries.
 */

#include "input_overlay.h"
#include <vitaGL.h>
#include <math.h>
#include <stdbool.h>

/* ---- compile-time constants -------------------------------------------- */

/* Hard upper bound for per-unit texture-enable arrays; validated at runtime. */
#define AOD_MAX_TEX_UNITS    16
/* Number of clip planes enumerated by GL_CLIP_PLANE0..GL_CLIP_PLANE6.       */
#define AOD_NUM_CLIP_PLANES   7
/* Cursor diamond half-size (pixels): border drawn first, fill on top.       */
#define AOD_BORDER_HALF  7.0f
#define AOD_FILL_HALF    6.0f

/* ---- saved GL state bundle ---------------------------------------------- */

typedef struct {
    GLint     program;
    GLint     viewport[4];
    GLint     matrix_mode;
    GLfloat   proj[16];
    GLfloat   mv[16];
    GLfloat   color[4];
    GLint     active_tex;
    /* Per fixed-function texture unit enables; sized to compile-time max.   */
    GLboolean tex1d[AOD_MAX_TEX_UNITS];
    GLboolean tex2d[AOD_MAX_TEX_UNITS];
    /* Cap enables.                                                           */
    GLboolean alpha_test;
    GLboolean depth_test;
    GLboolean stencil_test;
    GLboolean blend;
    GLboolean scissor;
    GLboolean cull;
    GLboolean lighting;
    GLboolean fog;
    GLboolean poly_offset_fill;
    GLboolean clip_plane[AOD_NUM_CLIP_PLANES];
    /* Actual unit count validated to [1, AOD_MAX_TEX_UNITS] before use.     */
    int num_tex_units;
} aod_gl_state_t;

/* ---- small helpers ------------------------------------------------------ */

/* Toggle a GL capability from a saved GLboolean.
   glIsEnabled may return values != GL_TRUE but still != GL_FALSE (e.g. 2).  */
static void aod_set_cap(GLenum cap, GLboolean val) {
    if (val != GL_FALSE) {
        glEnable(cap);
    } else {
        glDisable(cap);
    }
}

static float aod_clampf(float v, float lo, float hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

/* ---- state save --------------------------------------------------------- */

/*
 * Save all GL state that aod_input_overlay_draw will touch.
 * The active texture unit is temporarily mutated during texture-unit
 * iteration and is restored inside this function before it returns.
 * s->num_tex_units must be set and validated in [1, AOD_MAX_TEX_UNITS]
 * before calling this function.
 */
static void aod_save_state(aod_gl_state_t *s) {
    int i;
    glGetIntegerv(GL_CURRENT_PROGRAM,    &s->program);
    glGetIntegerv(GL_VIEWPORT,            s->viewport);
    glGetIntegerv(GL_MATRIX_MODE,        &s->matrix_mode);
    /* Matrices read directly; matrix mode is not changed here.              */
    glGetFloatv(GL_PROJECTION_MATRIX,    s->proj);
    glGetFloatv(GL_MODELVIEW_MATRIX,     s->mv);
    glGetFloatv(GL_CURRENT_COLOR,        s->color);
    /* Save active texture before unit iteration so it can be restored.      */
    glGetIntegerv(GL_ACTIVE_TEXTURE,     &s->active_tex);
    /* Iterate fixed-function texture units; active tex temporarily mutated. */
    for (i = 0; i < s->num_tex_units; i++) {
        glActiveTexture((GLenum)(GL_TEXTURE0 + i));
        s->tex1d[i] = glIsEnabled(GL_TEXTURE_1D);
        s->tex2d[i] = glIsEnabled(GL_TEXTURE_2D);
    }
    /* Restore active texture immediately — must always happen after loop.   */
    glActiveTexture((GLenum)s->active_tex);
    /* Cap enables.                                                           */
    s->alpha_test       = glIsEnabled(GL_ALPHA_TEST);
    s->depth_test       = glIsEnabled(GL_DEPTH_TEST);
    s->stencil_test     = glIsEnabled(GL_STENCIL_TEST);
    s->blend            = glIsEnabled(GL_BLEND);
    s->scissor          = glIsEnabled(GL_SCISSOR_TEST);
    s->cull             = glIsEnabled(GL_CULL_FACE);
    s->lighting         = glIsEnabled(GL_LIGHTING);
    s->fog              = glIsEnabled(GL_FOG);
    s->poly_offset_fill = glIsEnabled(GL_POLYGON_OFFSET_FILL);
    for (i = 0; i < AOD_NUM_CLIP_PLANES; i++) {
        s->clip_plane[i] = glIsEnabled((GLenum)(GL_CLIP_PLANE0 + i));
    }
}

/* ---- draw setup --------------------------------------------------------- */

/* Disable capabilities that must be off for the overlay to appear correctly. */
static void aod_disable_caps(void) {
    int i;
    glDisable(GL_ALPHA_TEST);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_STENCIL_TEST);
    glDisable(GL_BLEND);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_LIGHTING);
    glDisable(GL_FOG);
    glDisable(GL_POLYGON_OFFSET_FILL);
    for (i = 0; i < AOD_NUM_CLIP_PLANES; i++) {
        glDisable((GLenum)(GL_CLIP_PLANE0 + i));
    }
}

/* Configure GL state for overlay rendering.
   w and h are the validated integer viewport dimensions.                    */
static void aod_setup_draw(const aod_gl_state_t *s, GLint w, GLint h) {
    int i;
    glUseProgram(0);
    glViewport(0, 0, w, h);
    /* Disable all fixed-function texture units.                             */
    for (i = 0; i < s->num_tex_units; i++) {
        glActiveTexture((GLenum)(GL_TEXTURE0 + i));
        glDisable(GL_TEXTURE_1D);
        glDisable(GL_TEXTURE_2D);
    }
    /* Leave active texture at unit 0 for draw; restore will fix it later.  */
    glActiveTexture(GL_TEXTURE0);
    aod_disable_caps();
    /* 2D ortho projection: x in [0,w], y in [0,h] top-to-bottom.           */
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0.0, (GLdouble)w, (GLdouble)h, 0.0, -1.0, 1.0);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
}

/* ---- state restore ------------------------------------------------------ */

static void aod_restore_matrices(const aod_gl_state_t *s) {
    glMatrixMode(GL_PROJECTION);
    glLoadMatrixf(s->proj);
    glMatrixMode(GL_MODELVIEW);
    glLoadMatrixf(s->mv);
    glMatrixMode((GLenum)s->matrix_mode);
}

static void aod_restore_textures(const aod_gl_state_t *s) {
    int i;
    for (i = 0; i < s->num_tex_units; i++) {
        glActiveTexture((GLenum)(GL_TEXTURE0 + i));
        aod_set_cap(GL_TEXTURE_1D, s->tex1d[i]);
        aod_set_cap(GL_TEXTURE_2D, s->tex2d[i]);
    }
    glActiveTexture((GLenum)s->active_tex);
}

static void aod_restore_caps(const aod_gl_state_t *s) {
    int i;
    aod_set_cap(GL_ALPHA_TEST,          s->alpha_test);
    aod_set_cap(GL_DEPTH_TEST,          s->depth_test);
    aod_set_cap(GL_STENCIL_TEST,        s->stencil_test);
    aod_set_cap(GL_BLEND,               s->blend);
    aod_set_cap(GL_SCISSOR_TEST,        s->scissor);
    aod_set_cap(GL_CULL_FACE,           s->cull);
    aod_set_cap(GL_LIGHTING,            s->lighting);
    aod_set_cap(GL_FOG,                 s->fog);
    aod_set_cap(GL_POLYGON_OFFSET_FILL, s->poly_offset_fill);
    for (i = 0; i < AOD_NUM_CLIP_PLANES; i++) {
        aod_set_cap((GLenum)(GL_CLIP_PLANE0 + i), s->clip_plane[i]);
    }
}

static void aod_restore_state(const aod_gl_state_t *s) {
    aod_restore_matrices(s);
    glUseProgram((GLuint)s->program);
    glViewport(s->viewport[0], s->viewport[1],
               (GLsizei)s->viewport[2], (GLsizei)s->viewport[3]);
    glColor4f(s->color[0], s->color[1], s->color[2], s->color[3]);
    aod_restore_textures(s);
    aod_restore_caps(s);
}

/* ---- geometry ----------------------------------------------------------- */

/*
 * Emit a filled diamond (rotated square) centered at (cx, cy) with the given
 * half-size and RGB color (alpha forced to 1.0). All vertex coordinates are
 * clamped to [0, max_x] x [0, max_y] so an edge cursor remains visible.
 * Diamond is split into two GL_TRIANGLES: right half (top→right→bottom) and
 * left half (top→bottom→left).
 */
static void aod_emit_diamond(float cx, float cy, float half,
                              float r, float g, float b,
                              float max_x, float max_y) {
    float tx = aod_clampf(cx,        0.0f, max_x);
    float ty = aod_clampf(cy - half, 0.0f, max_y);
    float rx = aod_clampf(cx + half, 0.0f, max_x);
    float ry = aod_clampf(cy,        0.0f, max_y);
    float bx = aod_clampf(cx,        0.0f, max_x);
    float by = aod_clampf(cy + half, 0.0f, max_y);
    float lx = aod_clampf(cx - half, 0.0f, max_x);
    float ly = aod_clampf(cy,        0.0f, max_y);

    glColor4f(r, g, b, 1.0f);
    glBegin(GL_TRIANGLES);
    /* Right half */
    glVertex2f(tx, ty);
    glVertex2f(rx, ry);
    glVertex2f(bx, by);
    /* Left half */
    glVertex2f(tx, ty);
    glVertex2f(bx, by);
    glVertex2f(lx, ly);
    glEnd();
}

/* ---- public API --------------------------------------------------------- */

bool aod_input_overlay_draw(float width, float height, float x, float y) {
    GLint          max_tex;
    aod_gl_state_t state;
    GLint          vp_w;
    GLint          vp_h;

    /*
     * Phase 1: validate parameters — no GL calls, no state changes.
     * All rejection before any GL interaction ensures a false return leaves
     * caller GL state completely untouched.
     */
    if (!isfinite(width) || !isfinite(height)) {
        return false;
    }
    if (width < 1.0f || height < 1.0f || width > 32767.0f || height > 32767.0f) {
        return false;
    }
    if (!isfinite(x) || !isfinite(y)) {
        return false;
    }
    if (x < 0.0f || x > width - 1.0f || y < 0.0f || y > height - 1.0f) {
        return false;
    }

    /*
     * Phase 2: read GL_MAX_TEXTURE_UNITS — read-only query, no mutation.
     * Validate before any glActiveTexture call (required by spec).
     */
    max_tex = 0;
    glGetIntegerv(GL_MAX_TEXTURE_UNITS, &max_tex);
    if (max_tex < 1 || max_tex > AOD_MAX_TEX_UNITS) {
        return false;
    }

    /*
     * Phase 3: save complete GL state.
     * aod_save_state temporarily mutates the active texture unit while
     * iterating texture units; it restores it before returning.
     */
    state.num_tex_units = (int)max_tex;
    aod_save_state(&state);

    /*
     * Phase 4: configure GL for overlay draw.
     */
    vp_w = (GLint)width;
    vp_h = (GLint)height;
    aod_setup_draw(&state, vp_w, vp_h);

    /*
     * Phase 5: emit diamond cursor — dark border first, cyan fill on top.
     */
    aod_emit_diamond(x, y, AOD_BORDER_HALF,
                     0.05f, 0.05f, 0.15f,
                     width, height);
    aod_emit_diamond(x, y, AOD_FILL_HALF,
                     0.0f, 1.0f, 1.0f,
                     width, height);

    /*
     * Phase 6: restore complete GL state — always reached on the draw path.
     * Restores: matrices, matrix mode, program, viewport, color, texture
     * unit enables, original active texture, and all cap enable states.
     */
    aod_restore_state(&state);

    return true;
}