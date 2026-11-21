#ifndef NIXEL_GL_GLUT_H
#define NIXEL_GL_GLUT_H

#include <GL/gl.h>

#define GLUT_RGB       0x0000
#define GLUT_RGBA      0x0000
#define GLUT_DOUBLE    0x0002
#define GLUT_DEPTH     0x0010

#define GLUT_WINDOW_WIDTH   102
#define GLUT_WINDOW_HEIGHT  103
#define GLUT_ELAPSED_TIME   700

#define GLUT_KEY_LEFT   100
#define GLUT_KEY_UP     101
#define GLUT_KEY_RIGHT  102
#define GLUT_KEY_DOWN   103

#define GLUT_BITMAP_HELVETICA_18 ((void *)1)
#define GLUT_BITMAP_HELVETICA_12 ((void *)2)
#define GLUT_BITMAP_8_BY_13      ((void *)3)

void glutInit(int *argcp, char **argv);
void glutInitDisplayMode(unsigned int mode);
void glutInitWindowSize(int width, int height);
int  glutCreateWindow(const char *title);

void glutDisplayFunc(void (*func)(void));
void glutReshapeFunc(void (*func)(int width, int height));
void glutKeyboardFunc(void (*func)(unsigned char key, int x, int y));
void glutSpecialFunc(void (*func)(int key, int x, int y));
void glutTimerFunc(unsigned int millis, void (*func)(int value), int value);

void exit(int status) __attribute__((noreturn));

void glutMainLoop(void);
void glutPostRedisplay(void);
void glutSwapBuffers(void);
void glutSetWindowTitle(const char *title);
void glutBitmapCharacter(void *font, int character);
int  glutGet(unsigned int token);

#endif
