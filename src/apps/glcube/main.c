#include <GL/glut.h>

#include "texture_data.h"

#define WINDOW_WIDTH  800
#define WINDOW_HEIGHT 600

static GLuint g_texture;
static int    g_textured = 1;
static int    g_spin     = 1;

static float g_yaw      = 30.0f;
static float g_pitch    = 20.0f;
static float g_roll     = 0.0f;
static float g_distance = 3.4f;

static int    g_fps_time;
static int    g_last_time;
static int    g_frames;
static int    g_fps;

static void draw_face(const GLfloat v[4][3], const GLfloat colour[3]) {
    static const GLfloat uv[4][2] = { {0,0}, {1,0}, {1,1}, {0,1} };
    glColor3fv(colour);
    glBegin(GL_QUADS);
    for (int i = 0; i < 4; ++i) { glTexCoord2fv(uv[i]); glVertex3fv(v[i]); }
    glEnd();
}

static void draw_cube(void) {
    static const GLfloat white[3]   = {1.00f, 1.00f, 1.00f};
    static const GLfloat red[3]     = {0.85f, 0.15f, 0.15f};
    static const GLfloat green[3]   = {0.15f, 0.70f, 0.25f};
    static const GLfloat blue[3]    = {0.20f, 0.35f, 0.85f};
    static const GLfloat yellow[3]  = {0.90f, 0.80f, 0.20f};
    static const GLfloat cyan[3]    = {0.20f, 0.80f, 0.80f};
    static const GLfloat magenta[3] = {0.80f, 0.30f, 0.70f};

    static const GLfloat front_v[4][3] = {
        {-0.5f,-0.5f, 0.5f},{ 0.5f,-0.5f, 0.5f},{ 0.5f, 0.5f, 0.5f},{-0.5f, 0.5f, 0.5f}};
    static const GLfloat back_v[4][3] = {
        { 0.5f,-0.5f,-0.5f},{-0.5f,-0.5f,-0.5f},{-0.5f, 0.5f,-0.5f},{ 0.5f, 0.5f,-0.5f}};
    static const GLfloat right_v[4][3] = {
        { 0.5f,-0.5f, 0.5f},{ 0.5f,-0.5f,-0.5f},{ 0.5f, 0.5f,-0.5f},{ 0.5f, 0.5f, 0.5f}};
    static const GLfloat left_v[4][3] = {
        {-0.5f,-0.5f,-0.5f},{-0.5f,-0.5f, 0.5f},{-0.5f, 0.5f, 0.5f},{-0.5f, 0.5f,-0.5f}};
    static const GLfloat top_v[4][3] = {
        {-0.5f, 0.5f, 0.5f},{ 0.5f, 0.5f, 0.5f},{ 0.5f, 0.5f,-0.5f},{-0.5f, 0.5f,-0.5f}};
    static const GLfloat bottom_v[4][3] = {
        {-0.5f,-0.5f,-0.5f},{ 0.5f,-0.5f,-0.5f},{ 0.5f,-0.5f, 0.5f},{-0.5f,-0.5f, 0.5f}};

    const GLfloat *c = g_textured ? white : 0;
    draw_face(front_v,  c ? c : red);
    draw_face(back_v,   c ? c : green);
    draw_face(right_v,  c ? c : blue);
    draw_face(left_v,   c ? c : yellow);
    draw_face(top_v,    c ? c : cyan);
    draw_face(bottom_v, c ? c : magenta);
}

static char *put_int(char *p, int n) {
    char tmp[12];
    int i = 0;
    if (n < 0) { *p++ = '-'; n = -n; }
    do { tmp[i++] = (char)('0' + n % 10); n /= 10; } while (n);
    while (i) *p++ = tmp[--i];
    return p;
}

static void draw_hud(void) {
    char line[48], *p = line;
    p = put_int(p, g_fps);
    *p = 0;

    gl_draw_text(9, 9, "FPS:", 0x000000);
    gl_draw_text(8, 8, "FPS:", 0xFFFFFF);
    gl_draw_text(9 + 40, 9, line, 0x000000);
    gl_draw_text(8 + 40, 8, line, 0xFFFF66);

    char status[48], *s = status;
    const char *tex  = g_textured ? "TEX:ON  " : "TEX:OFF ";
    const char *spin = g_spin ? "SPIN:ON" : "SPIN:OFF";
    while (*tex) *s++ = *tex++;
    while (*spin) *s++ = *spin++;
    *s = 0;
    gl_draw_text(8, 22, status, 0xB0C4DE);
}

static void display(void) {
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    gluLookAt(0.0, 0.0, g_distance, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0);
    glRotatef(g_pitch, 1.0f, 0.0f, 0.0f);
    glRotatef(g_yaw,   0.0f, 1.0f, 0.0f);
    glRotatef(g_roll,  0.0f, 0.0f, 1.0f);

    if (g_textured) {
        glEnable(GL_TEXTURE_2D);
        glBindTexture(GL_TEXTURE_2D, g_texture);
    } else {
        glDisable(GL_TEXTURE_2D);
    }

    draw_cube();

    ++g_frames;
    {
        int now = glutGet(GLUT_ELAPSED_TIME);
        if (now - g_fps_time >= 500) {
            g_fps = g_frames * 1000 / (now - g_fps_time);
            g_frames = 0;
            g_fps_time = now;
        }
    }

    draw_hud();
    glutSwapBuffers();
}

static void reshape(int width, int height) {
    if (height == 0) height = 1;
    glViewport(0, 0, width, height);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    gluPerspective(45.0, (double)width / (double)height, 0.1, 100.0);
    glMatrixMode(GL_MODELVIEW);
}

static void keyboard(unsigned char key, int x, int y) {
    (void)x; (void)y;
    switch (key) {
    case 't': case 'T': g_textured = !g_textured; break;
    case 's': case 'S': g_spin = !g_spin; break;
    case 'q': case 'Q': g_roll += 5.0f; break;
    case 'e': case 'E': g_roll -= 5.0f; break;
    case '+': case '=':
        g_distance -= 0.2f;
        if (g_distance < 1.8f) g_distance = 1.8f;
        break;
    case '-': case '_':
        g_distance += 0.2f;
        if (g_distance > 8.0f) g_distance = 8.0f;
        break;
    case ' ':
        g_yaw = 30.0f; g_pitch = 20.0f; g_roll = 0.0f; g_distance = 3.4f;
        break;
    case 27:
        exit(0);
    default: break;
    }
    glutPostRedisplay();
}

static void special(int key, int x, int y) {
    (void)x; (void)y;
    switch (key) {
    case GLUT_KEY_LEFT:  g_yaw -= 5.0f; break;
    case GLUT_KEY_RIGHT: g_yaw += 5.0f; break;
    case GLUT_KEY_UP:
        g_pitch += 5.0f;
        if (g_pitch > 89.0f) g_pitch = 89.0f;
        break;
    case GLUT_KEY_DOWN:
        g_pitch -= 5.0f;
        if (g_pitch < -89.0f) g_pitch = -89.0f;
        break;
    default: break;
    }
    glutPostRedisplay();
}

static void timer(int value) {
    (void)value;
    int now = glutGet(GLUT_ELAPSED_TIME);
    int delta = now - g_last_time;
    if (delta < 1) delta = 1;
    g_last_time = now;

    if (g_spin) {
        g_yaw += 45.0f * (float)delta / 1000.0f;
        if (g_yaw >= 360.0f) g_yaw -= 360.0f;
    }

    glutPostRedisplay();
    glutTimerFunc(16, timer, 0);
}

static void init(void) {
    glClearColor(0.18f, 0.18f, 0.20f, 1.0f);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glShadeModel(GL_SMOOTH);

    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glGenTextures(1, &g_texture);
    glBindTexture(GL_TEXTURE_2D, g_texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, TEXTURE_WIDTH, TEXTURE_HEIGHT, 0,
                 GL_RGB, GL_UNSIGNED_BYTE, kTexturePixels);
}

int main(int argc, char **argv) {
    glutInit(&argc, argv);
    glutInitDisplayMode(GLUT_DOUBLE | GLUT_RGB | GLUT_DEPTH);
    glutInitWindowSize(WINDOW_WIDTH, WINDOW_HEIGHT);
    glutCreateWindow("OpenGL Cube");

    init();

    glutDisplayFunc(display);
    glutReshapeFunc(reshape);
    glutKeyboardFunc(keyboard);
    glutSpecialFunc(special);
    g_fps_time = glutGet(GLUT_ELAPSED_TIME);
    g_last_time = g_fps_time;
    glutTimerFunc(16, timer, 0);

    glutMainLoop();
    return 0;
}
