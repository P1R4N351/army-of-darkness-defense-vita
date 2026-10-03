/*
 * Mock-GL state regression for source/aod/input_overlay.c.
 * MIT License — host-only test binary; no credentials, network, or device.
 * Links production source directly; stub vitaGL.h supplies mock GL symbols.
 *
 * Test inventory:
 *   T01 valid draw origin (0,0) 960x544         — true, full state restore
 *   T02 valid draw max corner (959,543) 960x544  — true, full state restore
 *   T03 NaN width                                — false, no state change
 *   T04 Inf height                               — false, no state change
 *   T05 width=0.5 (< 1.0)                        — false, no state change
 *   T06 position offscreen x=960 y=0             — false, no state change
 *   T07 position x==width (not width-1)           — false, no state change
 *   T08 GL_MAX_TEXTURE_UNITS query returns 0     — false, no state change
 *   T09 GL_MAX_TEXTURE_UNITS query returns 17    — false, no state change
 *   T10 NaN x position                           — false, no state change
 *   T11 Inf y position                           — false, no state change
 *   T12 vertex in-bounds check for T01           — all 12 vertices in [0,960]x[0,544]
 *   T13 vertex in-bounds check for T02           — all 12 vertices clamped in bounds
 */

#include <vitaGL.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include "input_overlay.h"

/* =========================================================================
 * Mock GL state
 * ========================================================================= */

#define MOCK_MAX_UNITS  16
#define MOCK_NUM_CLIPS   7
#define MOCK_VLOG_CAP  128

typedef struct {
    GLint     program;
    GLint     viewport[4];
    GLenum    matrix_mode;
    GLfloat   proj[16];
    GLfloat   mv[16];
    GLfloat   tex_matrix[MOCK_MAX_UNITS][16];
    GLfloat   color[4];
    GLenum    active_tex;
    GLboolean tex1d[MOCK_MAX_UNITS];
    GLboolean tex2d[MOCK_MAX_UNITS];
    GLboolean alpha_test;
    GLboolean depth_test;
    GLboolean stencil_test;
    GLboolean blend;
    GLboolean scissor;
    GLboolean cull;
    GLboolean lighting;
    GLboolean fog;
    GLboolean poly_offset_fill;
    GLboolean clip_plane[MOCK_NUM_CLIPS];
} mock_snap_t;

static struct {
    mock_snap_t  s;
    GLint        max_tex_units;   /* GL_MAX_TEXTURE_UNITS override   */
    int          in_begin;
    float        vx[MOCK_VLOG_CAP];
    float        vy[MOCK_VLOG_CAP];
    int          vcount;
    int          oob_vertex;
    float        vbound_w;        /* set before draw to check bounds */
    float        vbound_h;
} g;

/* =========================================================================
 * Mock GL implementation
 * ========================================================================= */

static GLfloat *mock_cur_matrix(void) {
    int idx;
    if (g.s.matrix_mode == GL_PROJECTION) { return g.s.proj; }
    if (g.s.matrix_mode == GL_TEXTURE) {
        idx = (int)(g.s.active_tex - GL_TEXTURE0);
        if (idx < 0 || idx >= MOCK_MAX_UNITS) { idx = 0; }
        return g.s.tex_matrix[idx];
    }
    return g.s.mv;
}

void glUseProgram(GLuint p)    { g.s.program = (GLint)p; }
void glViewport(GLint x, GLint y, GLsizei w, GLsizei h) {
    g.s.viewport[0] = x; g.s.viewport[1] = y;
    g.s.viewport[2] = (GLint)w; g.s.viewport[3] = (GLint)h;
}
void glMatrixMode(GLenum m)    { g.s.matrix_mode = m; }
void glActiveTexture(GLenum t) { g.s.active_tex  = t; }
void glColor4f(GLfloat r, GLfloat gv, GLfloat b, GLfloat a) {
    g.s.color[0]=r; g.s.color[1]=gv; g.s.color[2]=b; g.s.color[3]=a;
}

void glLoadIdentity(void) {
    static const GLfloat I[16] = {
        1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1
    };
    memcpy(mock_cur_matrix(), I, 16 * sizeof(GLfloat));
}

void glLoadMatrixf(const GLfloat *m) {
    memcpy(mock_cur_matrix(), m, 16 * sizeof(GLfloat));
}

/* Ortho: compute and store result in current matrix (after LoadIdentity). */
void glOrtho(GLdouble l, GLdouble r, GLdouble b, GLdouble t,
             GLdouble n, GLdouble f) {
    GLfloat *m = mock_cur_matrix();
    memset(m, 0, 16 * sizeof(GLfloat));
    m[0]  = (GLfloat)(2.0/(r-l));
    m[5]  = (GLfloat)(2.0/(t-b));
    m[10] = (GLfloat)(-2.0/(f-n));
    m[12] = (GLfloat)(-(r+l)/(r-l));
    m[13] = (GLfloat)(-(t+b)/(t-b));
    m[14] = (GLfloat)(-(f+n)/(f-n));
    m[15] = 1.0f;
}

/* Forward declaration required: glEnable/glDisable call mock_set_cap. */
static void mock_set_cap(GLenum cap, GLboolean val);

void glEnable(GLenum cap)  { mock_set_cap(cap, GL_TRUE);  }
void glDisable(GLenum cap) { mock_set_cap(cap, GL_FALSE); }

void glBegin(GLenum mode) { (void)mode; g.in_begin = 1; }
void glEnd(void)          { g.in_begin = 0; }

void glVertex2f(GLfloat x, GLfloat y) {
    if (!g.in_begin) { return; }
    if (g.vcount < MOCK_VLOG_CAP) {
        g.vx[g.vcount] = x;
        g.vy[g.vcount] = y;
        g.vcount++;
    }
    if (x < 0.0f || x > g.vbound_w || y < 0.0f || y > g.vbound_h) {
        g.oob_vertex = 1;
    }
}

/* ---- glGetIntegerv -------------------------------------------------------- */
void glGetIntegerv(GLenum pname, GLint *data) {
    int i;
    switch (pname) {
    case GL_CURRENT_PROGRAM:   *data = g.s.program;          break;
    case GL_VIEWPORT:
        for (i = 0; i < 4; i++) { data[i] = g.s.viewport[i]; }
        break;
    case GL_MATRIX_MODE:       *data = (GLint)g.s.matrix_mode; break;
    case GL_ACTIVE_TEXTURE:    *data = (GLint)g.s.active_tex;  break;
    case GL_MAX_TEXTURE_UNITS: *data = g.max_tex_units;       break;
    default:                   *data = 0;                      break;
    }
}

/* ---- glGetFloatv ---------------------------------------------------------- */
void glGetFloatv(GLenum pname, GLfloat *data) {
    switch (pname) {
    case GL_PROJECTION_MATRIX: memcpy(data, g.s.proj,  16*sizeof(GLfloat)); break;
    case GL_MODELVIEW_MATRIX:  memcpy(data, g.s.mv,    16*sizeof(GLfloat)); break;
    case GL_CURRENT_COLOR:     memcpy(data, g.s.color, 4 *sizeof(GLfloat)); break;
    default:                   break;
    }
}

/* ---- glIsEnabled ---------------------------------------------------------- */
GLboolean glIsEnabled(GLenum cap) {
    int idx;
    switch (cap) {
    case GL_TEXTURE_1D:
        idx = (int)(g.s.active_tex - GL_TEXTURE0);
        if (idx < 0 || idx >= MOCK_MAX_UNITS) { return GL_FALSE; }
        return g.s.tex1d[idx];
    case GL_TEXTURE_2D:
        idx = (int)(g.s.active_tex - GL_TEXTURE0);
        if (idx < 0 || idx >= MOCK_MAX_UNITS) { return GL_FALSE; }
        return g.s.tex2d[idx];
    case GL_ALPHA_TEST:         return g.s.alpha_test;
    case GL_DEPTH_TEST:         return g.s.depth_test;
    case GL_STENCIL_TEST:       return g.s.stencil_test;
    case GL_BLEND:              return g.s.blend;
    case GL_SCISSOR_TEST:       return g.s.scissor;
    case GL_CULL_FACE:          return g.s.cull;
    case GL_LIGHTING:           return g.s.lighting;
    case GL_FOG:                return g.s.fog;
    case GL_POLYGON_OFFSET_FILL: return g.s.poly_offset_fill;
    default:
        idx = (int)(cap - GL_CLIP_PLANE0);
        if (idx >= 0 && idx < MOCK_NUM_CLIPS) { return g.s.clip_plane[idx]; }
        return GL_FALSE;
    }
}

/* ---- mock_set_cap (defined after glEnable/glDisable forward refs) --------- */
static void mock_set_cap(GLenum cap, GLboolean val) {
    int idx;
    switch (cap) {
    case GL_TEXTURE_1D:
        idx = (int)(g.s.active_tex - GL_TEXTURE0);
        /* VitaGL stores tex1d as state & (1<<0); preserve bitflag exactly. */
        if (idx >= 0 && idx < MOCK_MAX_UNITS) {
            g.s.tex1d[idx] = (val != GL_FALSE) ? (GLboolean)1 : (GLboolean)0;
        }
        break;
    case GL_TEXTURE_2D:
        idx = (int)(g.s.active_tex - GL_TEXTURE0);
        /* VitaGL stores tex2d as state & (1<<1) = 2; preserve bitflag exactly. */
        if (idx >= 0 && idx < MOCK_MAX_UNITS) {
            g.s.tex2d[idx] = (val != GL_FALSE) ? (GLboolean)2 : (GLboolean)0;
        }
        break;
    case GL_ALPHA_TEST:          g.s.alpha_test       = val; break;
    case GL_DEPTH_TEST:          g.s.depth_test       = val; break;
    case GL_STENCIL_TEST:        g.s.stencil_test     = val; break;
    case GL_BLEND:               g.s.blend            = val; break;
    case GL_SCISSOR_TEST:        g.s.scissor          = val; break;
    case GL_CULL_FACE:           g.s.cull             = val; break;
    case GL_LIGHTING:            g.s.lighting         = val; break;
    case GL_FOG:                 g.s.fog              = val; break;
    case GL_POLYGON_OFFSET_FILL: g.s.poly_offset_fill = val; break;
    default:
        idx = (int)(cap - GL_CLIP_PLANE0);
        if (idx >= 0 && idx < MOCK_NUM_CLIPS) { g.s.clip_plane[idx] = val; }
        break;
    }
}

/* =========================================================================
 * Test helpers
 * ========================================================================= */

static void snap(mock_snap_t *out) {
    memcpy(out, &g.s, sizeof(mock_snap_t));
}

static int snaps_equal(const mock_snap_t *a, const mock_snap_t *b) {
    return memcmp(a, b, sizeof(mock_snap_t)) == 0;
}

/* Set up the non-default initial GL state described in the task. */
static void init_gl_state(void) {
    static const GLfloat MV[16] = {
        2,0,0,0, 0,3,0,0, 0,0,5,0, 7,11,13,1
    };
    static const GLfloat PROJ[16] = {
        0.5f,0,0,0, 0,0.5f,0,0, 0,0,-1,0, -1,-1,0,1
    };
    int i;
    memset(&g, 0, sizeof(g));
    g.max_tex_units       = 2;
    g.s.program           = 71;
    g.s.viewport[0]       = 9;
    g.s.viewport[1]       = 17;
    g.s.viewport[2]       = 123;
    g.s.viewport[3]       = 234;
    g.s.matrix_mode       = GL_TEXTURE;
    memcpy(g.s.mv,   MV,   16 * sizeof(GLfloat));
    memcpy(g.s.proj, PROJ, 16 * sizeof(GLfloat));
    g.s.color[0]          = 0.2f;
    g.s.color[1]          = 0.4f;
    g.s.color[2]          = 0.6f;
    g.s.color[3]          = 0.8f;
    g.s.active_tex        = GL_TEXTURE1;
    /* unit 0: tex1d=1, tex2d=0; unit 1: tex1d=0, tex2d=2 */
    g.s.tex1d[0]          = 1;   /* VitaGL bitflag (1<<0) */
    g.s.tex2d[0]          = 0;
    g.s.tex1d[1]          = 0;
    g.s.tex2d[1]          = 2;   /* VitaGL bitflag (1<<1), truthy non-GL_TRUE */
    /* Non-default texture matrices for units 0 and 1. */
    g.s.tex_matrix[0][0]  = 3.0f; g.s.tex_matrix[0][5]  = 4.0f;
    g.s.tex_matrix[0][10] = 5.0f; g.s.tex_matrix[0][15] = 1.0f;
    g.s.tex_matrix[1][0]  = 7.0f; g.s.tex_matrix[1][5]  = 9.0f;
    g.s.tex_matrix[1][10] = 11.0f; g.s.tex_matrix[1][15] = 1.0f;
    /* alternating caps */
    g.s.alpha_test        = GL_TRUE;
    g.s.depth_test        = GL_FALSE;
    g.s.stencil_test      = GL_TRUE;
    g.s.blend             = GL_FALSE;
    g.s.scissor           = GL_TRUE;
    g.s.cull              = GL_FALSE;
    g.s.lighting          = GL_TRUE;
    g.s.fog               = GL_FALSE;
    g.s.poly_offset_fill  = GL_TRUE;
    for (i = 0; i < MOCK_NUM_CLIPS; i++) {
        g.s.clip_plane[i] = (i % 2 == 0) ? GL_TRUE : GL_FALSE;
    }
}

/* =========================================================================
 * Test cases
 * ========================================================================= */

static int t_pass = 0;
static int t_fail = 0;

#define PASS(name) do { printf("PASS %s\n", (name)); t_pass++; } while(0)
#define FAIL(name, ...) do { printf("FAIL %s: ", (name)); printf(__VA_ARGS__); printf("\n"); t_fail++; } while(0)

static void test_valid_draw_origin(void) {
    mock_snap_t before, after;
    bool ret;
    init_gl_state();
    g.vbound_w = 960.0f; g.vbound_h = 544.0f;
    snap(&before);
    ret = aod_input_overlay_draw(960.0f, 544.0f, 0.0f, 0.0f);
    snap(&after);
    if (!ret)            { FAIL("T01_ret",   "expected true"); return; }
    if (!snaps_equal(&before, &after)) {
        FAIL("T01_restore", "GL state not fully restored after draw at origin");
        return;
    }
    PASS("T01_valid_origin");
}

static void test_valid_draw_max_corner(void) {
    mock_snap_t before, after;
    bool ret;
    init_gl_state();
    g.vbound_w = 960.0f; g.vbound_h = 544.0f;
    snap(&before);
    ret = aod_input_overlay_draw(960.0f, 544.0f, 959.0f, 543.0f);
    snap(&after);
    if (!ret)            { FAIL("T02_ret",   "expected true"); return; }
    if (!snaps_equal(&before, &after)) {
        FAIL("T02_restore", "GL state not fully restored after draw at max corner");
        return;
    }
    PASS("T02_valid_max_corner");
}

static void test_nan_width(void) {
    mock_snap_t before, after;
    init_gl_state();
    snap(&before);
    if (aod_input_overlay_draw(NAN, 544.0f, 0.0f, 0.0f)) {
        FAIL("T03_ret", "expected false for NaN width"); return;
    }
    snap(&after);
    if (!snaps_equal(&before, &after)) {
        FAIL("T03_state", "state changed on rejected NaN width"); return;
    }
    PASS("T03_nan_width");
}

static void test_inf_height(void) {
    mock_snap_t before, after;
    init_gl_state();
    snap(&before);
    if (aod_input_overlay_draw(960.0f, INFINITY, 0.0f, 0.0f)) {
        FAIL("T04_ret", "expected false for Inf height"); return;
    }
    snap(&after);
    if (!snaps_equal(&before, &after)) {
        FAIL("T04_state", "state changed on rejected Inf height"); return;
    }
    PASS("T04_inf_height");
}

static void test_width_half(void) {
    mock_snap_t before, after;
    init_gl_state();
    snap(&before);
    if (aod_input_overlay_draw(0.5f, 544.0f, 0.0f, 0.0f)) {
        FAIL("T05_ret", "expected false for width=0.5"); return;
    }
    snap(&after);
    if (!snaps_equal(&before, &after)) {
        FAIL("T05_state", "state changed on rejected width=0.5"); return;
    }
    PASS("T05_width_half");
}

static void test_position_offscreen(void) {
    mock_snap_t before, after;
    init_gl_state();
    snap(&before);
    if (aod_input_overlay_draw(960.0f, 544.0f, 960.0f, 0.0f)) {
        FAIL("T06_ret", "expected false for x=960 (offscreen)"); return;
    }
    snap(&after);
    if (!snaps_equal(&before, &after)) {
        FAIL("T06_state", "state changed on rejected offscreen x"); return;
    }
    PASS("T06_offscreen");
}

static void test_x_equals_width(void) {
    mock_snap_t before, after;
    init_gl_state();
    snap(&before);
    /* x == width; valid range is [0, width-1] so this must be rejected */
    if (aod_input_overlay_draw(960.0f, 544.0f, 960.0f, 0.0f)) {
        FAIL("T07_ret", "expected false for x==width"); return;
    }
    snap(&after);
    if (!snaps_equal(&before, &after)) {
        FAIL("T07_state", "state changed on rejected x==width"); return;
    }
    PASS("T07_x_equals_width");
}

static void test_max_tex_zero(void) {
    mock_snap_t before, after;
    init_gl_state();
    g.max_tex_units = 0;
    snap(&before);
    if (aod_input_overlay_draw(960.0f, 544.0f, 0.0f, 0.0f)) {
        FAIL("T08_ret", "expected false for max_tex=0"); return;
    }
    snap(&after);
    if (!snaps_equal(&before, &after)) {
        FAIL("T08_state", "state changed on rejected max_tex=0"); return;
    }
    PASS("T08_max_tex_zero");
}

static void test_max_tex_seventeen(void) {
    mock_snap_t before, after;
    init_gl_state();
    g.max_tex_units = 17;
    snap(&before);
    if (aod_input_overlay_draw(960.0f, 544.0f, 0.0f, 0.0f)) {
        FAIL("T09_ret", "expected false for max_tex=17"); return;
    }
    snap(&after);
    if (!snaps_equal(&before, &after)) {
        FAIL("T09_state", "state changed on rejected max_tex=17"); return;
    }
    PASS("T09_max_tex_17");
}

static void test_nan_x(void) {
    mock_snap_t before, after;
    init_gl_state();
    snap(&before);
    if (aod_input_overlay_draw(960.0f, 544.0f, NAN, 0.0f)) {
        FAIL("T10_ret", "expected false for NaN x"); return;
    }
    snap(&after);
    if (!snaps_equal(&before, &after)) {
        FAIL("T10_state", "state changed on rejected NaN x"); return;
    }
    PASS("T10_nan_x");
}

static void test_inf_y(void) {
    mock_snap_t before, after;
    init_gl_state();
    snap(&before);
    if (aod_input_overlay_draw(960.0f, 544.0f, 0.0f, INFINITY)) {
        FAIL("T11_ret", "expected false for Inf y"); return;
    }
    snap(&after);
    if (!snaps_equal(&before, &after)) {
        FAIL("T11_state", "state changed on rejected Inf y"); return;
    }
    PASS("T11_inf_y");
}

static void test_vertex_bounds_origin(void) {
    int i;
    init_gl_state();
    g.vbound_w = 960.0f; g.vbound_h = 544.0f;
    aod_input_overlay_draw(960.0f, 544.0f, 0.0f, 0.0f);
    if (g.vcount != 12) {
        FAIL("T12_vcount", "expected 12 vertices, got %d", g.vcount); return;
    }
    if (g.oob_vertex) {
        FAIL("T12_oob", "out-of-bounds vertex emitted for origin draw");
        for (i = 0; i < g.vcount; i++) {
            printf("  v[%d] = (%.2f, %.2f)\n", i, g.vx[i], g.vy[i]);
        }
        return;
    }
    PASS("T12_vertex_bounds_origin");
}

static void test_vertex_bounds_max_corner(void) {
    int i;
    init_gl_state();
    g.vbound_w = 960.0f; g.vbound_h = 544.0f;
    aod_input_overlay_draw(960.0f, 544.0f, 959.0f, 543.0f);
    if (g.vcount != 12) {
        FAIL("T13_vcount", "expected 12 vertices, got %d", g.vcount); return;
    }
    if (g.oob_vertex) {
        FAIL("T13_oob", "out-of-bounds vertex emitted for max corner draw");
        for (i = 0; i < g.vcount; i++) {
            printf("  v[%d] = (%.2f, %.2f)\n", i, g.vx[i], g.vy[i]);
        }
        return;
    }
    PASS("T13_vertex_bounds_max_corner");
}

/* =========================================================================
 * main
 * ========================================================================= */

int main(void) {
    test_valid_draw_origin();
    test_valid_draw_max_corner();
    test_nan_width();
    test_inf_height();
    test_width_half();
    test_position_offscreen();
    test_x_equals_width();
    test_max_tex_zero();
    test_max_tex_seventeen();
    test_nan_x();
    test_inf_y();
    test_vertex_bounds_origin();
    test_vertex_bounds_max_corner();
    printf("\n%d passed, %d failed\n", t_pass, t_fail);
    return t_fail > 0 ? 1 : 0;
}