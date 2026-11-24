#include <GL/glut.h>
#include <math.h>

#include "boing_data.h"
#include "brick_data.h"

#define WINDOW_WIDTH  800
#define WINDOW_HEIGHT 600
#define PI 3.14159265358979323846f

static GLuint g_ball_texture;
static GLuint g_brick_texture;
static GLUquadric *g_sphere_quadric;

static int g_sphere_textured = 1;

static float g_cam_yaw      = 35.0f;
static float g_cam_pitch    = 18.0f;
static float g_cam_distance = 6.0f;

static float g_spin_angle;

static int g_fps_time;
static int g_last_time;
static int g_frames;
static int g_fps;

static void setup_camera(void) {
    float yaw = g_cam_yaw * PI / 180.0f;
    float pitch = g_cam_pitch * PI / 180.0f;
    float horizontal = cosf(pitch) * g_cam_distance;
    float eye_x = horizontal * sinf(yaw);
    float eye_y = 1.0f + g_cam_distance * sinf(pitch);
    float eye_z = horizontal * cosf(yaw);

    gluLookAt(eye_x, eye_y, eye_z,
              0.0, 1.0, 0.0,
              0.0, 1.0, 0.0);
}

static void draw_floor(void) {
    const float half_size = 40.0f;
    const float repeats = 40.0f;

    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, g_brick_texture);
    glColor3f(1.0f, 1.0f, 1.0f);

    glNormal3f(0.0f, 1.0f, 0.0f);
    glBegin(GL_QUADS);
        glTexCoord2f(0.0f, 0.0f);        glVertex3f(-half_size, 0.0f, -half_size);
        glTexCoord2f(0.0f, repeats);     glVertex3f(-half_size, 0.0f,  half_size);
        glTexCoord2f(repeats, repeats);  glVertex3f( half_size, 0.0f,  half_size);
        glTexCoord2f(repeats, 0.0f);     glVertex3f( half_size, 0.0f, -half_size);
    glEnd();
}

static void draw_shadow(void) {
    const float radius = 0.85f;
    int i;

    glDisable(GL_LIGHTING);
    glDisable(GL_TEXTURE_2D);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glColor4f(0.0f, 0.0f, 0.0f, 0.30f);

    glBegin(GL_TRIANGLE_FAN);
    glVertex3f(0.0f, 0.02f, 0.0f);
    for (i = 0; i <= 32; ++i) {
        float angle = 2.0f * PI * (float)i / 32.0f;
        glVertex3f(radius * cosf(angle), 0.02f, -radius * sinf(angle));
    }
    glEnd();

    glDisable(GL_BLEND);
    glEnable(GL_LIGHTING);
}

static void draw_sphere(void) {
    glPushMatrix();
    glTranslatef(0.0f, 1.0f, 0.0f);
    glRotatef(g_spin_angle, 0.0f, 1.0f, 0.0f);
    glRotatef(-90.0f, 1.0f, 0.0f, 0.0f);

    if (g_sphere_textured) {
        glEnable(GL_TEXTURE_2D);
        glBindTexture(GL_TEXTURE_2D, g_ball_texture);
        glColor3f(1.0f, 1.0f, 1.0f);
    } else {
        glDisable(GL_TEXTURE_2D);
        glColor3f(0.88f, 0.46f, 0.18f);
    }

    gluSphere(g_sphere_quadric, 1.0, 48, 32);
    glPopMatrix();
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
    char line[32], *p = line;
    const char *lbl = "FPS: ";
    while (*lbl) *p++ = *lbl++;
    p = put_int(p, g_fps);
    *p = 0;
    gl_draw_text(9, 9, line, 0x000000);
    gl_draw_text(8, 8, line, 0xFFFFFF);
}

static void display(void) {
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    setup_camera();

    {
        static const GLfloat light_position[4] = {6.0f, 10.0f, 5.0f, 1.0f};
        glLightfv(GL_LIGHT0, GL_POSITION, light_position);
    }

    draw_floor();
    draw_shadow();
    draw_sphere();

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
    case 't': case 'T': g_sphere_textured = !g_sphere_textured; break;
    case '+': case '=':
        g_cam_distance -= 0.3f;
        if (g_cam_distance < 3.0f) g_cam_distance = 3.0f;
        break;
    case '-': case '_':
        g_cam_distance += 0.3f;
        if (g_cam_distance > 12.0f) g_cam_distance = 12.0f;
        break;
    case ' ':
        g_cam_yaw = 35.0f; g_cam_pitch = 18.0f; g_cam_distance = 6.0f;
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
    case GLUT_KEY_LEFT:  g_cam_yaw -= 3.0f; break;
    case GLUT_KEY_RIGHT: g_cam_yaw += 3.0f; break;
    case GLUT_KEY_UP:
        g_cam_pitch += 3.0f;
        if (g_cam_pitch > 80.0f) g_cam_pitch = 80.0f;
        break;
    case GLUT_KEY_DOWN:
        g_cam_pitch -= 3.0f;
        if (g_cam_pitch < 5.0f) g_cam_pitch = 5.0f;
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

    g_spin_angle += 40.0f * (float)delta / 1000.0f;
    if (g_spin_angle >= 360.0f) g_spin_angle -= 360.0f;

    glutPostRedisplay();
    glutTimerFunc(16, timer, 0);
}

static void init(void) {
    static const GLfloat sky[4] = {0.42f, 0.66f, 0.94f, 1.0f};
    static const GLfloat light_diffuse[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    static const GLfloat light_ambient[4] = {0.35f, 0.35f, 0.38f, 1.0f};
    static const GLfloat material_specular[4] = {0.20f, 0.20f, 0.20f, 1.0f};

    g_fps_time = glutGet(GLUT_ELAPSED_TIME);
    g_last_time = g_fps_time;

    glClearColor(sky[0], sky[1], sky[2], sky[3]);

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glShadeModel(GL_SMOOTH);

    glEnable(GL_LIGHTING);
    glEnable(GL_LIGHT0);
    glLightfv(GL_LIGHT0, GL_DIFFUSE, light_diffuse);
    glLightfv(GL_LIGHT0, GL_AMBIENT, light_ambient);

    glEnable(GL_COLOR_MATERIAL);
    glColorMaterial(GL_FRONT, GL_AMBIENT_AND_DIFFUSE);
    glMaterialfv(GL_FRONT, GL_SPECULAR, material_specular);
    glMaterialf(GL_FRONT, GL_SHININESS, 24.0f);

    glEnable(GL_FOG);
    glFogi(GL_FOG_MODE, GL_LINEAR);
    glFogfv(GL_FOG_COLOR, sky);
    glFogf(GL_FOG_START, 16.0f);
    glFogf(GL_FOG_END, 34.0f);

    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

    glGenTextures(1, &g_ball_texture);
    glBindTexture(GL_TEXTURE_2D, g_ball_texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    gluBuild2DMipmaps(GL_TEXTURE_2D, GL_RGB, BOING_WIDTH, BOING_HEIGHT,
                      GL_RGB, GL_UNSIGNED_BYTE, kBoingPixels);

    glGenTextures(1, &g_brick_texture);
    glBindTexture(GL_TEXTURE_2D, g_brick_texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    gluBuild2DMipmaps(GL_TEXTURE_2D, GL_RGB, BRICK_WIDTH, BRICK_HEIGHT,
                      GL_RGB, GL_UNSIGNED_BYTE, kBrickPixels);

    g_sphere_quadric = gluNewQuadric();
    gluQuadricTexture(g_sphere_quadric, GL_TRUE);
    gluQuadricNormals(g_sphere_quadric, GLU_SMOOTH);
}

int main(int argc, char **argv) {
    glutInit(&argc, argv);
    glutInitDisplayMode(GLUT_DOUBLE | GLUT_RGB | GLUT_DEPTH);
    glutInitWindowSize(WINDOW_WIDTH, WINDOW_HEIGHT);
    glutCreateWindow("SCENE");

    init();

    glutDisplayFunc(display);
    glutReshapeFunc(reshape);
    glutKeyboardFunc(keyboard);
    glutSpecialFunc(special);
    glutTimerFunc(16, timer, 0);

    glutMainLoop();
    return 0;
}
