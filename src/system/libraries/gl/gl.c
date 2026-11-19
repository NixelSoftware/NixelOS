#include <GL/gl.h>
#include <math.h>
#include "mem.h"
#include "font8x8.h"

#define GL_PI 3.14159265358979323846f

static uint32_t *color_buffer;
static float    *depth_buffer;
static int       surf_w, surf_h;

uint32_t *gl_surface_pixels(void) { return color_buffer; }
int       gl_surface_width(void)  { return surf_w; }
int       gl_surface_height(void) { return surf_h; }

int gl_surface_init(int width, int height) {
    surf_w = width;
    surf_h = height;
    color_buffer = (uint32_t *)gc_malloc((uint32_t)width * height * 4u);
    depth_buffer = (float *)gc_malloc((uint32_t)width * height * (uint32_t)sizeof(float));
    return color_buffer && depth_buffer;
}

#define M(mat, col, row) ((mat)[(col) * 4 + (row)])

static void mat_identity(float *m) {
    for (int i = 0; i < 16; i++) m[i] = 0.0f;
    m[0] = m[5] = m[10] = m[15] = 1.0f;
}

static void mat_mul(float *out, const float *a, const float *b) {
    for (int c = 0; c < 4; c++)
        for (int r = 0; r < 4; r++) {
            float sum = 0.0f;
            for (int k = 0; k < 4; k++)
                sum += M(a, k, r) * M(b, c, k);
            M(out, c, r) = sum;
        }
}

static void mat_vec(float out[4], const float *m, const float v[4]) {
    for (int r = 0; r < 4; r++)
        out[r] = M(m, 0, r) * v[0] + M(m, 1, r) * v[1] +
                 M(m, 2, r) * v[2] + M(m, 3, r) * v[3];
}

#define STACK_DEPTH 16
static float proj_stack[STACK_DEPTH][16];
static float model_stack[STACK_DEPTH][16];
static int   proj_top, model_top;
static GLenum matrix_mode = GL_MODELVIEW;

static float *current_matrix(void) {
    return matrix_mode == GL_PROJECTION ? proj_stack[proj_top]
                                        : model_stack[model_top];
}

void glMatrixMode(GLenum mode) { matrix_mode = mode; }
void glLoadIdentity(void)      { mat_identity(current_matrix()); }
void glLoadMatrixf(const GLfloat *m) {
    float *c = current_matrix();
    for (int i = 0; i < 16; i++) c[i] = m[i];
}

void glPushMatrix(void) {
    if (matrix_mode == GL_PROJECTION) {
        if (proj_top + 1 >= STACK_DEPTH) return;
        for (int i = 0; i < 16; i++) proj_stack[proj_top + 1][i] = proj_stack[proj_top][i];
        proj_top++;
    } else {
        if (model_top + 1 >= STACK_DEPTH) return;
        for (int i = 0; i < 16; i++) model_stack[model_top + 1][i] = model_stack[model_top][i];
        model_top++;
    }
}

void glPopMatrix(void) {
    if (matrix_mode == GL_PROJECTION) { if (proj_top) proj_top--; }
    else                             { if (model_top) model_top--; }
}

void glMultMatrixf(const GLfloat *m) {
    float *c = current_matrix();
    float result[16];
    mat_mul(result, c, m);
    for (int i = 0; i < 16; i++) c[i] = result[i];
}

void glTranslatef(GLfloat x, GLfloat y, GLfloat z) {
    float m[16]; mat_identity(m);
    M(m, 3, 0) = x; M(m, 3, 1) = y; M(m, 3, 2) = z;
    glMultMatrixf(m);
}

void glScalef(GLfloat x, GLfloat y, GLfloat z) {
    float m[16]; mat_identity(m);
    M(m, 0, 0) = x; M(m, 1, 1) = y; M(m, 2, 2) = z;
    glMultMatrixf(m);
}

void glRotatef(GLfloat angle, GLfloat x, GLfloat y, GLfloat z) {
    float len = sqrtf(x * x + y * y + z * z);
    if (len < 1e-6f) return;
    x /= len; y /= len; z /= len;
    float a = angle * (GL_PI / 180.0f);
    float s = sinf(a), c = cosf(a), one = 1.0f - c;
    float m[16]; mat_identity(m);
    M(m, 0, 0) = x * x * one + c;     M(m, 1, 0) = x * y * one - z * s; M(m, 2, 0) = x * z * one + y * s;
    M(m, 0, 1) = y * x * one + z * s; M(m, 1, 1) = y * y * one + c;     M(m, 2, 1) = y * z * one - x * s;
    M(m, 0, 2) = x * z * one - y * s; M(m, 1, 2) = y * z * one + x * s; M(m, 2, 2) = z * z * one + c;
    glMultMatrixf(m);
}

void glFrustum(GLdouble l, GLdouble r, GLdouble b, GLdouble t, GLdouble n, GLdouble f) {
    float m[16];
    for (int i = 0; i < 16; i++) m[i] = 0.0f;
    M(m, 0, 0) = (float)(2.0 * n / (r - l));
    M(m, 1, 1) = (float)(2.0 * n / (t - b));
    M(m, 2, 0) = (float)((r + l) / (r - l));
    M(m, 2, 1) = (float)((t + b) / (t - b));
    M(m, 2, 2) = (float)(-(f + n) / (f - n));
    M(m, 2, 3) = -1.0f;
    M(m, 3, 2) = (float)(-(2.0 * f * n) / (f - n));
    glMultMatrixf(m);
}

void glOrtho(GLdouble l, GLdouble r, GLdouble b, GLdouble t, GLdouble n, GLdouble f) {
    float m[16]; mat_identity(m);
    M(m, 0, 0) = (float)(2.0 / (r - l));
    M(m, 1, 1) = (float)(2.0 / (t - b));
    M(m, 2, 2) = (float)(-2.0 / (f - n));
    M(m, 3, 0) = (float)(-(r + l) / (r - l));
    M(m, 3, 1) = (float)(-(t + b) / (t - b));
    M(m, 3, 2) = (float)(-(f + n) / (f - n));
    glMultMatrixf(m);
}

void gluPerspective(GLdouble fovy, GLdouble aspect, GLdouble near, GLdouble far) {
    float f = 1.0f / tanf((float)fovy * (GL_PI / 180.0f) * 0.5f);
    float m[16];
    for (int i = 0; i < 16; i++) m[i] = 0.0f;
    M(m, 0, 0) = f / (float)aspect;
    M(m, 1, 1) = f;
    M(m, 2, 2) = (float)((far + near) / (near - far));
    M(m, 2, 3) = -1.0f;
    M(m, 3, 2) = (float)((2.0 * far * near) / (near - far));
    glMultMatrixf(m);
}

void gluLookAt(GLdouble ex, GLdouble ey, GLdouble ez,
               GLdouble cx, GLdouble cy, GLdouble cz,
               GLdouble ux, GLdouble uy, GLdouble uz) {
    float fx = (float)(cx - ex), fy = (float)(cy - ey), fz = (float)(cz - ez);
    float fl = sqrtf(fx * fx + fy * fy + fz * fz);
    if (fl > 1e-6f) { fx /= fl; fy /= fl; fz /= fl; }
    float upx = (float)ux, upy = (float)uy, upz = (float)uz;

    float sx = fy * upz - fz * upy;
    float sy = fz * upx - fx * upz;
    float sz = fx * upy - fy * upx;
    float sl = sqrtf(sx * sx + sy * sy + sz * sz);
    if (sl > 1e-6f) { sx /= sl; sy /= sl; sz /= sl; }

    float ux2 = sy * fz - sz * fy;
    float uy2 = sz * fx - sx * fz;
    float uz2 = sx * fy - sy * fx;
    float m[16]; mat_identity(m);
    M(m, 0, 0) = sx;  M(m, 1, 0) = sy;  M(m, 2, 0) = sz;
    M(m, 0, 1) = ux2; M(m, 1, 1) = uy2; M(m, 2, 1) = uz2;
    M(m, 0, 2) = -fx; M(m, 1, 2) = -fy; M(m, 2, 2) = -fz;
    glMultMatrixf(m);
    glTranslatef((float)-ex, (float)-ey, (float)-ez);
}

void gluOrtho2D(GLdouble l, GLdouble r, GLdouble b, GLdouble t) {
    glOrtho(l, r, b, t, -1.0, 1.0);
}

#define MAX_TEXTURES 16
typedef struct { int w, h; uint32_t *pixels; } Texture;
static Texture textures[MAX_TEXTURES];
static int     tex_next = 1;
static int     tex_bound;
static int     tex_enabled;

static int   depth_test, cull_enabled;
static GLenum cull_mode = GL_BACK, front_face = GL_CCW;
static uint32_t clear_color = 0;
static int   vp_x, vp_y, vp_w, vp_h;

static uint32_t pack_color(float r, float g, float b) {
    if (r < 0) r = 0; if (r > 1) r = 1;
    if (g < 0) g = 0; if (g > 1) g = 1;
    if (b < 0) b = 0; if (b > 1) b = 1;

    uint32_t ri = (uint32_t)(int)(r * 255.0f + 0.5f);
    uint32_t gi = (uint32_t)(int)(g * 255.0f + 0.5f);
    uint32_t bi = (uint32_t)(int)(b * 255.0f + 0.5f);
    return (ri << 16) | (gi << 8) | bi;
}

void glEnable(GLenum cap) {
    if (cap == GL_DEPTH_TEST) depth_test = 1;
    else if (cap == GL_CULL_FACE) cull_enabled = 1;
    else if (cap == GL_TEXTURE_2D) tex_enabled = 1;

}
void glDisable(GLenum cap) {
    if (cap == GL_DEPTH_TEST) depth_test = 0;
    else if (cap == GL_CULL_FACE) cull_enabled = 0;
    else if (cap == GL_TEXTURE_2D) tex_enabled = 0;
}
void glCullFace(GLenum mode)  { cull_mode = mode; }
void glFrontFace(GLenum mode) { front_face = mode; }
void glShadeModel(GLenum mode) { (void)mode;  }

void glClearColor(GLfloat r, GLfloat g, GLfloat b, GLfloat a) {
    (void)a;
    clear_color = pack_color(r, g, b);
}

void glViewport(GLint x, GLint y, GLsizei w, GLsizei h) {
    vp_x = x; vp_y = y; vp_w = w; vp_h = h;
}

void glClear(GLbitfield mask) {
    int n = surf_w * surf_h;
    if (mask & GL_COLOR_BUFFER_BIT)
        for (int i = 0; i < n; i++) color_buffer[i] = clear_color;
    if (mask & GL_DEPTH_BUFFER_BIT)
        for (int i = 0; i < n; i++) depth_buffer[i] = 1.0f;
}

void glFlush(void)  {}
void glFinish(void) {}

void glGenTextures(GLsizei n, GLuint *out) {
    for (int i = 0; i < n; i++)
        out[i] = (tex_next < MAX_TEXTURES) ? (GLuint)tex_next++ : 0;
}

void glBindTexture(GLenum target, GLuint texture) {
    (void)target;
    if (texture < MAX_TEXTURES) tex_bound = (int)texture;
}

void glTexParameteri(GLenum t, GLenum p, GLint v) { (void)t; (void)p; (void)v; }
void glPixelStorei(GLenum p, GLint v)             { (void)p; (void)v; }

void glTexImage2D(GLenum target, GLint level, GLint internalformat,
                  GLsizei width, GLsizei height, GLint border,
                  GLenum format, GLenum type, const void *pixels) {
    (void)target; (void)level; (void)internalformat; (void)border; (void)type;
    if (tex_bound <= 0 || tex_bound >= MAX_TEXTURES) return;
    Texture *tx = &textures[tex_bound];
    if (tx->pixels) gc_free(tx->pixels);
    tx->w = width; tx->h = height;
    tx->pixels = (uint32_t *)gc_malloc((uint32_t)width * height * 4u);
    if (!tx->pixels) { tx->w = tx->h = 0; return; }
    const uint8_t *src = (const uint8_t *)pixels;
    int stride = (format == GL_RGBA) ? 4 : 3;
    for (int i = 0; i < width * height; i++) {
        uint32_t r = src[i * stride + 0], g = src[i * stride + 1], b = src[i * stride + 2];
        tx->pixels[i] = (r << 16) | (g << 8) | b;
    }
}

#define MAX_VERTS 4096
typedef struct { float x, y, z; float r, g, b; float u, v; } Vert;
static Vert   verts[MAX_VERTS];
static int    vert_count;
static GLenum prim_mode;
static float  cur_r = 1, cur_g = 1, cur_b = 1;
static float  cur_u = 0, cur_v = 0;

void glBegin(GLenum mode) { prim_mode = mode; vert_count = 0; }

void glColor3f(GLfloat r, GLfloat g, GLfloat b) { cur_r = r; cur_g = g; cur_b = b; }
void glColor3fv(const GLfloat *c) { glColor3f(c[0], c[1], c[2]); }
void glNormal3f(GLfloat x, GLfloat y, GLfloat z) { (void)x; (void)y; (void)z; }
void glNormal3fv(const GLfloat *n) { (void)n; }
void glTexCoord2f(GLfloat s, GLfloat t) { cur_u = s; cur_v = t; }
void glTexCoord2fv(const GLfloat *st) { cur_u = st[0]; cur_v = st[1]; }

void glVertex3f(GLfloat x, GLfloat y, GLfloat z) {
    if (vert_count >= MAX_VERTS) return;
    Vert *v = &verts[vert_count++];
    v->x = x; v->y = y; v->z = z;
    v->r = cur_r; v->g = cur_g; v->b = cur_b;
    v->u = cur_u; v->v = cur_v;
}
void glVertex3fv(const GLfloat *v) { glVertex3f(v[0], v[1], v[2]); }

typedef struct { float x, y, z; float r, g, b; float su, sv, invw; int valid; } Frag;

static Frag project(const Vert *v, const float *mvp) {
    Frag f;
    float in[4] = { v->x, v->y, v->z, 1.0f }, clip[4];
    mat_vec(clip, mvp, in);
    if (clip[3] <= 1e-6f) { f.valid = 0; return f; }
    float inv = 1.0f / clip[3];
    float ndx = clip[0] * inv, ndy = clip[1] * inv, ndz = clip[2] * inv;
    f.x = (float)vp_x + (ndx * 0.5f + 0.5f) * (float)vp_w;
    f.y = (float)vp_y + (0.5f - ndy * 0.5f) * (float)vp_h;
    f.z = ndz * 0.5f + 0.5f;
    f.r = v->r; f.g = v->g; f.b = v->b;
    f.invw = inv;
    f.su = v->u * inv; f.sv = v->v * inv;
    f.valid = 1;
    return f;
}

static float edge(const Frag *a, const Frag *b, float px, float py) {
    return (b->x - a->x) * (py - a->y) - (b->y - a->y) * (px - a->x);
}

static void raster_triangle(const Frag *a, const Frag *b, const Frag *c) {
    if (!a->valid || !b->valid || !c->valid) return;

    float area = (b->x - a->x) * (c->y - a->y) - (b->y - a->y) * (c->x - a->x);
    if (fabsf(area) < 1e-4f) return;

    if (cull_enabled) {
        int front = front_face == GL_CCW ? (area < 0.0f) : (area > 0.0f);
        int is_back = !front;
        if ((cull_mode == GL_BACK && is_back) || (cull_mode == GL_FRONT && front))
            return;
    }

    int minx = (int)(a->x < b->x ? (a->x < c->x ? a->x : c->x) : (b->x < c->x ? b->x : c->x));
    int maxx = (int)(a->x > b->x ? (a->x > c->x ? a->x : c->x) : (b->x > c->x ? b->x : c->x)) + 1;
    int miny = (int)(a->y < b->y ? (a->y < c->y ? a->y : c->y) : (b->y < c->y ? b->y : c->y));
    int maxy = (int)(a->y > b->y ? (a->y > c->y ? a->y : c->y) : (b->y > c->y ? b->y : c->y)) + 1;
    if (minx < 0) minx = 0; if (miny < 0) miny = 0;
    if (maxx > surf_w) maxx = surf_w; if (maxy > surf_h) maxy = surf_h;

    Texture *tx = (tex_enabled && tex_bound > 0 && tex_bound < MAX_TEXTURES &&
                   textures[tex_bound].pixels) ? &textures[tex_bound] : 0;

    float inv_area = 1.0f / area;
    for (int py = miny; py < maxy; py++) {
        for (int px = minx; px < maxx; px++) {
            float cx = (float)px + 0.5f, cy = (float)py + 0.5f;
            float w0 = edge(b, c, cx, cy);
            float w1 = edge(c, a, cx, cy);
            float w2 = edge(a, b, cx, cy);

            if (area > 0.0f) { if (w0 < 0 || w1 < 0 || w2 < 0) continue; }
            else             { if (w0 > 0 || w1 > 0 || w2 > 0) continue; }

            float l0 = w0 * inv_area, l1 = w1 * inv_area, l2 = w2 * inv_area;
            float depth = l0 * a->z + l1 * b->z + l2 * c->z;
            int idx = py * surf_w + px;
            if (depth_test) {
                if (depth >= depth_buffer[idx]) continue;
                depth_buffer[idx] = depth;
            }
            float r = l0 * a->r + l1 * b->r + l2 * c->r;
            float g = l0 * a->g + l1 * b->g + l2 * c->g;
            float bl = l0 * a->b + l1 * b->b + l2 * c->b;
            if (tx) {

                float iw = l0 * a->invw + l1 * b->invw + l2 * c->invw;
                float u = (l0 * a->su + l1 * b->su + l2 * c->su) / iw;
                float v = (l0 * a->sv + l1 * b->sv + l2 * c->sv) / iw;
                int iu = (int)(u * tx->w) % tx->w; if (iu < 0) iu += tx->w;
                int iv = (int)(v * tx->h) % tx->h; if (iv < 0) iv += tx->h;
                uint32_t texel = tx->pixels[iv * tx->w + iu];
                r  *= ((texel >> 16) & 255) * (1.0f / 255.0f);
                g  *= ((texel >> 8)  & 255) * (1.0f / 255.0f);
                bl *= ( texel        & 255) * (1.0f / 255.0f);
            }
            color_buffer[idx] = pack_color(r, g, bl);
        }
    }
}

void glEnd(void) {
    float mvp[16];
    mat_mul(mvp, proj_stack[proj_top], model_stack[model_top]);

    if (prim_mode == GL_TRIANGLES) {
        for (int i = 0; i + 2 < vert_count; i += 3) {
            Frag f0 = project(&verts[i], mvp);
            Frag f1 = project(&verts[i + 1], mvp);
            Frag f2 = project(&verts[i + 2], mvp);
            raster_triangle(&f0, &f1, &f2);
        }
    } else if (prim_mode == GL_QUADS) {
        for (int i = 0; i + 3 < vert_count; i += 4) {
            Frag f0 = project(&verts[i], mvp);
            Frag f1 = project(&verts[i + 1], mvp);
            Frag f2 = project(&verts[i + 2], mvp);
            Frag f3 = project(&verts[i + 3], mvp);
            raster_triangle(&f0, &f1, &f2);
            raster_triangle(&f0, &f2, &f3);
        }
    }
    vert_count = 0;
}

static void put_glyph(int x, int y, int c, uint32_t rgb) {
    if (c < 0 || c > 127) c = '?';
    const unsigned char *g = glcube_font8x8[c];
    for (int row = 0; row < 8; row++) {
        int py = y + row;
        if (py < 0 || py >= surf_h) continue;
        unsigned bits = g[row];
        for (int col = 0; col < 8; col++) {
            if (!(bits & (0x80u >> col))) continue;
            int px = x + col;
            if (px < 0 || px >= surf_w) continue;
            color_buffer[py * surf_w + px] = rgb;
        }
    }
}

void gl_draw_text(int x, int y, const char *s, uint32_t rgb) {
    for (; *s; s++) { put_glyph(x, y, (unsigned char)*s, rgb); x += 8; }
}

static float raster_x, raster_y;
static int   raster_valid;

void glRasterPos2f(GLfloat x, GLfloat y) {
    float in[4] = { x, y, 0.0f, 1.0f }, clip[4], mvp[16];
    mat_mul(mvp, proj_stack[proj_top], model_stack[model_top]);
    mat_vec(clip, mvp, in);
    if (clip[3] <= 1e-6f) { raster_valid = 0; return; }
    float inv = 1.0f / clip[3];
    raster_x = (float)vp_x + (clip[0] * inv * 0.5f + 0.5f) * (float)vp_w;
    raster_y = (float)vp_y + (0.5f - clip[1] * inv * 0.5f) * (float)vp_h;
    raster_valid = 1;
}
void glRasterPos2i(GLint x, GLint y) { glRasterPos2f((float)x, (float)y); }

void gl_bitmap_char(int c) {
    if (!raster_valid) return;
    put_glyph((int)raster_x, (int)raster_y - 7, c, pack_color(cur_r, cur_g, cur_b));
    raster_x += 8;
}
