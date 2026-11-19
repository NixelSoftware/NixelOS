#ifndef NIXEL_GL_GL_H
#define NIXEL_GL_GL_H

#include <stdint.h>

typedef float        GLfloat;
typedef double       GLdouble;
typedef int          GLint;
typedef int          GLsizei;
typedef unsigned int GLuint;
typedef unsigned int GLenum;
typedef unsigned int GLbitfield;
typedef unsigned char GLubyte;
typedef unsigned char GLboolean;
typedef void         GLvoid;

#define GL_FALSE            0
#define GL_TRUE             1
#define GL_DEPTH_TEST       0x0B71
#define GL_CULL_FACE        0x0B44
#define GL_LIGHTING         0x0B50
#define GL_LIGHT0           0x4000
#define GL_TEXTURE_2D       0x0DE1
#define GL_COLOR_MATERIAL   0x0B57
#define GL_SMOOTH           0x1D01
#define GL_FLAT             0x1D00
#define GL_FRONT            0x0404
#define GL_BACK             0x0405
#define GL_CCW              0x0901
#define GL_CW               0x0900
#define GL_PROJECTION       0x1701
#define GL_MODELVIEW        0x1700
#define GL_TRIANGLES        0x0004
#define GL_QUADS            0x0007
#define GL_COLOR_BUFFER_BIT 0x00004000
#define GL_DEPTH_BUFFER_BIT 0x00000100

#define GL_RGB                0x1907
#define GL_RGBA               0x1908
#define GL_UNSIGNED_BYTE      0x1401
#define GL_TEXTURE_MAG_FILTER 0x2800
#define GL_TEXTURE_MIN_FILTER 0x2801
#define GL_TEXTURE_WRAP_S     0x2802
#define GL_TEXTURE_WRAP_T     0x2803
#define GL_NEAREST            0x2600
#define GL_LINEAR             0x2601
#define GL_REPEAT             0x2901
#define GL_MODULATE           0x2100
#define GL_UNPACK_ALIGNMENT   0x0CF5

int             gl_surface_init(int width, int height);
uint32_t       *gl_surface_pixels(void);
int             gl_surface_width(void);
int             gl_surface_height(void);

void glEnable(GLenum cap);
void glDisable(GLenum cap);
void glCullFace(GLenum mode);
void glFrontFace(GLenum mode);
void glShadeModel(GLenum mode);
void glClearColor(GLfloat r, GLfloat g, GLfloat b, GLfloat a);
void glClear(GLbitfield mask);
void glViewport(GLint x, GLint y, GLsizei w, GLsizei h);
void glFlush(void);
void glFinish(void);

void glMatrixMode(GLenum mode);
void glLoadIdentity(void);
void glPushMatrix(void);
void glPopMatrix(void);
void glLoadMatrixf(const GLfloat *m);
void glMultMatrixf(const GLfloat *m);
void glRotatef(GLfloat angle, GLfloat x, GLfloat y, GLfloat z);
void glTranslatef(GLfloat x, GLfloat y, GLfloat z);
void glScalef(GLfloat x, GLfloat y, GLfloat z);
void glFrustum(GLdouble l, GLdouble r, GLdouble b, GLdouble t, GLdouble n, GLdouble f);
void glOrtho(GLdouble l, GLdouble r, GLdouble b, GLdouble t, GLdouble n, GLdouble f);

void glBegin(GLenum mode);
void glEnd(void);
void glVertex3f(GLfloat x, GLfloat y, GLfloat z);
void glVertex3fv(const GLfloat *v);
void glColor3f(GLfloat r, GLfloat g, GLfloat b);
void glColor3fv(const GLfloat *c);
void glNormal3f(GLfloat x, GLfloat y, GLfloat z);
void glNormal3fv(const GLfloat *n);
void glTexCoord2f(GLfloat s, GLfloat t);
void glTexCoord2fv(const GLfloat *st);

void glGenTextures(GLsizei n, GLuint *textures);
void glBindTexture(GLenum target, GLuint texture);
void glTexParameteri(GLenum target, GLenum pname, GLint param);
void glTexImage2D(GLenum target, GLint level, GLint internalformat,
                  GLsizei width, GLsizei height, GLint border,
                  GLenum format, GLenum type, const void *pixels);
void glPixelStorei(GLenum pname, GLint param);

void glRasterPos2f(GLfloat x, GLfloat y);
void glRasterPos2i(GLint x, GLint y);
void gl_draw_text(int x, int y, const char *s, uint32_t rgb);
void gl_bitmap_char(int c);

void gluPerspective(GLdouble fovy, GLdouble aspect, GLdouble near, GLdouble far);
void gluLookAt(GLdouble ex, GLdouble ey, GLdouble ez,
               GLdouble cx, GLdouble cy, GLdouble cz,
               GLdouble ux, GLdouble uy, GLdouble uz);
void gluOrtho2D(GLdouble l, GLdouble r, GLdouble b, GLdouble t);

#endif
