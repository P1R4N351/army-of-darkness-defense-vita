/*
 * NOTE: this file relaxes P10 rule 3 because this is a host-only mock stub
 * header; there are no credentials or network calls present.
 *
 * Stub vitaGL.h for host-side mock-GL regression of input_overlay.c.
 * Only declares types, constants, and prototypes actually used by
 * source/aod/input_overlay.c.  No vitashark.h dependency.
 */

#ifndef _VITAGL_STUB_H_
#define _VITAGL_STUB_H_

#include <stdint.h>

/* ---- type aliases (mirror real vitaGL.h under HAVE_GL_HEADERS) ----------- */
#define GLboolean  uint8_t
#define GLint      int32_t
#define GLuint     uint32_t
#define GLfloat    float
#define GLdouble   double
#define GLenum     uint32_t
#define GLsizei    int32_t
#define GLclampf   float
#define GLvoid     void

/* ---- boolean values ------------------------------------------------------- */
#define GL_FALSE   0
#define GL_TRUE    1

/* ---- enumerants used by input_overlay.c ----------------------------------- */
#define GL_TRIANGLES              0x0004
#define GL_CULL_FACE              0x0B44
#define GL_LIGHTING               0x0B50
#define GL_FOG                    0x0B60
#define GL_DEPTH_TEST             0x0B71
#define GL_STENCIL_TEST           0x0B90
#define GL_MATRIX_MODE            0x0BA0
#define GL_VIEWPORT               0x0BA2
#define GL_MODELVIEW_MATRIX       0x0BA6
#define GL_PROJECTION_MATRIX      0x0BA7
#define GL_ALPHA_TEST             0x0BC0
#define GL_BLEND                  0x0BE2
#define GL_SCISSOR_TEST           0x0C11
#define GL_CURRENT_COLOR          0x0B00
#define GL_TEXTURE_1D             0x0DE0
#define GL_TEXTURE_2D             0x0DE1
#define GL_MODELVIEW              0x1700
#define GL_PROJECTION             0x1701
#define GL_TEXTURE                0x1702
#define GL_POLYGON_OFFSET_FILL    0x8037
#define GL_CLIP_PLANE0            0x3000
#define GL_CLIP_PLANE1            0x3001
#define GL_CLIP_PLANE2            0x3002
#define GL_CLIP_PLANE3            0x3003
#define GL_CLIP_PLANE4            0x3004
#define GL_CLIP_PLANE5            0x3005
#define GL_CLIP_PLANE6            0x3006
#define GL_TEXTURE0               0x84C0
#define GL_TEXTURE1               0x84C1
#define GL_ACTIVE_TEXTURE         0x84E0
#define GL_MAX_TEXTURE_UNITS      0x84E2
#define GL_CURRENT_PROGRAM        0x8B8D

/* ---- prototypes used by input_overlay.c ----------------------------------- */
void      glActiveTexture(GLenum texture);
void      glBegin(GLenum mode);
void      glColor4f(GLfloat r, GLfloat g, GLfloat b, GLfloat a);
void      glDisable(GLenum cap);
void      glEnable(GLenum cap);
void      glEnd(void);
void      glGetFloatv(GLenum pname, GLfloat *data);
void      glGetIntegerv(GLenum pname, GLint *data);
GLboolean glIsEnabled(GLenum cap);
void      glLoadIdentity(void);
void      glLoadMatrixf(const GLfloat *m);
void      glMatrixMode(GLenum mode);
void      glOrtho(GLdouble l, GLdouble r, GLdouble b, GLdouble t,
                  GLdouble n, GLdouble f);
void      glUseProgram(GLuint program);
void      glVertex2f(GLfloat x, GLfloat y);
void      glViewport(GLint x, GLint y, GLsizei w, GLsizei h);

#endif /* _VITAGL_STUB_H_ */