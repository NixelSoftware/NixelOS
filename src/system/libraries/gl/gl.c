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

static int   blend_enabled;
static int   lighting_enabled, light0_enabled, color_material_enabled;
static float light_pos_eye[4] = {0, 0, 1, 0};
static float light_diffuse[3] = {1, 1, 1};
static float light_ambient[3] = {0, 0, 0};
static float light_specular[3] = {1, 1, 1};
static float mat_specular[3] = {0, 0, 0};
static float mat_shininess = 0.0f;

static int   fog_enabled;
static float fog_color[3] = {0, 0, 0};
static float fog_start = 0.0f, fog_end = 1.0f;

static float clampf(float v, float lo, float hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

static uint32_t pack_color(float r, float g, float b) {
    r = clampf(r, 0, 1); g = clampf(g, 0, 1); b = clampf(b, 0, 1);
    uint32_t ri = (uint32_t)(int)(r * 255.0f + 0.5f);
    uint32_t gi = (uint32_t)(int)(g * 255.0f + 0.5f);
    uint32_t bi = (uint32_t)(int)(b * 255.0f + 0.5f);
    return (ri << 16) | (gi << 8) | bi;
}

void glEnable(GLenum cap) {
    if (cap == GL_DEPTH_TEST) depth_test = 1;
    else if (cap == GL_CULL_FACE) cull_enabled = 1;
    else if (cap == GL_TEXTURE_2D) tex_enabled = 1;
    else if (cap == GL_BLEND) blend_enabled = 1;
    else if (cap == GL_LIGHTING) lighting_enabled = 1;
    else if (cap == GL_LIGHT0) light0_enabled = 1;
    else if (cap == GL_COLOR_MATERIAL) color_material_enabled = 1;
    else if (cap == GL_FOG) fog_enabled = 1;
}
void glDisable(GLenum cap) {
    if (cap == GL_DEPTH_TEST) depth_test = 0;
    else if (cap == GL_CULL_FACE) cull_enabled = 0;
    else if (cap == GL_TEXTURE_2D) tex_enabled = 0;
    else if (cap == GL_BLEND) blend_enabled = 0;
    else if (cap == GL_LIGHTING) lighting_enabled = 0;
    else if (cap == GL_LIGHT0) light0_enabled = 0;
    else if (cap == GL_COLOR_MATERIAL) color_material_enabled = 0;
    else if (cap == GL_FOG) fog_enabled = 0;
}
void glCullFace(GLenum mode)  { cull_mode = mode; }
void glFrontFace(GLenum mode) { front_face = mode; }
void glShadeModel(GLenum mode) { (void)mode; }
void glBlendFunc(GLenum s, GLenum d) { (void)s; (void)d; }

void glLightfv(GLenum light, GLenum pname, const GLfloat *params) {
    (void)light;
    if (pname == GL_POSITION) {
        float p[4] = { params[0], params[1], params[2], params[3] };
        mat_vec(light_pos_eye, model_stack[model_top], p);
    } else if (pname == GL_DIFFUSE) {
        for (int i = 0; i < 3; i++) light_diffuse[i] = params[i];
    } else if (pname == GL_AMBIENT) {
        for (int i = 0; i < 3; i++) light_ambient[i] = params[i];
    } else if (pname == GL_SPECULAR) {
        for (int i = 0; i < 3; i++) light_specular[i] = params[i];
    }
}
void glLightf(GLenum light, GLenum pname, GLfloat param) { (void)light; (void)pname; (void)param; }

void glMaterialfv(GLenum face, GLenum pname, const GLfloat *params) {
    (void)face;
    if (pname == GL_SPECULAR)
        for (int i = 0; i < 3; i++) mat_specular[i] = params[i];
}
void glMaterialf(GLenum face, GLenum pname, GLfloat param) {
    (void)face;
    if (pname == GL_SHININESS) mat_shininess = param;
}
void glColorMaterial(GLenum face, GLenum mode) { (void)face; (void)mode; }

void glFogi(GLenum pname, GLint param)  { (void)pname; (void)param; }
void glFogf(GLenum pname, GLfloat param) {
    if (pname == GL_FOG_START) fog_start = param;
    else if (pname == GL_FOG_END) fog_end = param;
}
void glFogfv(GLenum pname, const GLfloat *params) {
    if (pname == GL_FOG_COLOR)
        for (int i = 0; i < 3; i++) fog_color[i] = params[i];
}

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

int gluBuild2DMipmaps(GLenum target, GLint internalFormat, GLsizei width,
                      GLsizei height, GLenum format, GLenum type, const void *data) {
    glTexImage2D(target, 0, internalFormat, width, height, 0, format, type, data);
    return 0;
}

#define MAX_VERTS 4096
typedef struct {
    float x, y, z;
    float r, g, b, a;
    float u, v;
    float nx, ny, nz;
} Vert;
static Vert   verts[MAX_VERTS];
static int    vert_count;
static GLenum prim_mode;
static float  cur_r = 1, cur_g = 1, cur_b = 1, cur_a = 1;
static float  cur_u = 0, cur_v = 0;
static float  cur_nx = 0, cur_ny = 0, cur_nz = 1;

void glBegin(GLenum mode) { prim_mode = mode; vert_count = 0; }

void glColor3f(GLfloat r, GLfloat g, GLfloat b) { cur_r = r; cur_g = g; cur_b = b; cur_a = 1.0f; }
void glColor3fv(const GLfloat *c) { glColor3f(c[0], c[1], c[2]); }
void glColor4f(GLfloat r, GLfloat g, GLfloat b, GLfloat a) { cur_r = r; cur_g = g; cur_b = b; cur_a = a; }
void glColor4fv(const GLfloat *c) { glColor4f(c[0], c[1], c[2], c[3]); }
void glNormal3f(GLfloat x, GLfloat y, GLfloat z) { cur_nx = x; cur_ny = y; cur_nz = z; }
void glNormal3fv(const GLfloat *n) { glNormal3f(n[0], n[1], n[2]); }
void glTexCoord2f(GLfloat s, GLfloat t) { cur_u = s; cur_v = t; }
void glTexCoord2fv(const GLfloat *st) { cur_u = st[0]; cur_v = st[1]; }

void glVertex3f(GLfloat x, GLfloat y, GLfloat z) {
    if (vert_count >= MAX_VERTS) return;
    Vert *v = &verts[vert_count++];
    v->x = x; v->y = y; v->z = z;
    v->r = cur_r; v->g = cur_g; v->b = cur_b; v->a = cur_a;
    v->u = cur_u; v->v = cur_v;
    v->nx = cur_nx; v->ny = cur_ny; v->nz = cur_nz;
}
void glVertex3fv(const GLfloat *v) { glVertex3f(v[0], v[1], v[2]); }

typedef struct {
    float x, y, z;
    float r, g, b, a;
    float su, sv, invw;
    float fog;
    int valid;
} Frag;

/* Per-vertex Gouraud lighting + linear fog, computed in eye space. */
static void shade(const Vert *v, const float *model, const float eye[4],
                  float *or_, float *og, float *ob) {
    float mr = v->r, mg = v->g, mb = v->b;
    if (!lighting_enabled) { *or_ = mr; *og = mg; *ob = mb; return; }

    float nx = M(model, 0, 0) * v->nx + M(model, 1, 0) * v->ny + M(model, 2, 0) * v->nz;
    float ny = M(model, 0, 1) * v->nx + M(model, 1, 1) * v->ny + M(model, 2, 1) * v->nz;
    float nz = M(model, 0, 2) * v->nx + M(model, 1, 2) * v->ny + M(model, 2, 2) * v->nz;
    float nl = sqrtf(nx * nx + ny * ny + nz * nz);
    if (nl > 1e-6f) { nx /= nl; ny /= nl; nz /= nl; }

    float lx = light_pos_eye[0] - eye[0];
    float ly = light_pos_eye[1] - eye[1];
    float lz = light_pos_eye[2] - eye[2];
    float ll = sqrtf(lx * lx + ly * ly + lz * lz);
    if (ll > 1e-6f) { lx /= ll; ly /= ll; lz /= ll; }

    float ndotl = nx * lx + ny * ly + nz * lz;
    if (ndotl < 0) ndotl = 0;

    float cr = light_ambient[0] * mr + light_diffuse[0] * ndotl * mr;
    float cg = light_ambient[1] * mg + light_diffuse[1] * ndotl * mg;
    float cb = light_ambient[2] * mb + light_diffuse[2] * ndotl * mb;

    if (ndotl > 0 && mat_shininess > 0) {
        float vx = -eye[0], vy = -eye[1], vz = -eye[2];
        float vln = sqrtf(vx * vx + vy * vy + vz * vz);
        if (vln > 1e-6f) { vx /= vln; vy /= vln; vz /= vln; }
        float hx = lx + vx, hy = ly + vy, hz = lz + vz;
        float hln = sqrtf(hx * hx + hy * hy + hz * hz);
        if (hln > 1e-6f) { hx /= hln; hy /= hln; hz /= hln; }
        float ndoth = nx * hx + ny * hy + nz * hz;
        if (ndoth < 0) ndoth = 0;
        float spec = powf(ndoth, mat_shininess);
        cr += light_specular[0] * mat_specular[0] * spec;
        cg += light_specular[1] * mat_specular[1] * spec;
        cb += light_specular[2] * mat_specular[2] * spec;
    }
    *or_ = cr; *og = cg; *ob = cb;
}

/* A vertex in clip space (pre-divide), carrying interpolatable attributes, so
 * the near plane can be clipped before the perspective divide. */
typedef struct {
    float clip[4];
    float r, g, b, a;
    float u, v, fog;
} CVert;

static CVert to_clip(const Vert *v, const float *model, const float *proj) {
    CVert c;
    float obj[4] = { v->x, v->y, v->z, 1.0f }, eye[4];
    mat_vec(eye, model, obj);
    mat_vec(c.clip, proj, eye);
    shade(v, model, eye, &c.r, &c.g, &c.b);
    c.a = v->a;
    c.u = v->u; c.v = v->v;
    if (fog_enabled) {
        float dist = fabsf(eye[2]);
        float denom = fog_end - fog_start;
        c.fog = denom > 1e-6f ? clampf((fog_end - dist) / denom, 0.0f, 1.0f) : 1.0f;
    } else {
        c.fog = 1.0f;
    }
    return c;
}

static CVert lerp_cv(const CVert *a, const CVert *b, float t) {
    CVert o;
    for (int i = 0; i < 4; i++) o.clip[i] = a->clip[i] + (b->clip[i] - a->clip[i]) * t;
    o.r = a->r + (b->r - a->r) * t;
    o.g = a->g + (b->g - a->g) * t;
    o.b = a->b + (b->b - a->b) * t;
    o.a = a->a + (b->a - a->a) * t;
    o.u = a->u + (b->u - a->u) * t;
    o.v = a->v + (b->v - a->v) * t;
    o.fog = a->fog + (b->fog - a->fog) * t;
    return o;
}

/* Clip a polygon against the near plane (z + w >= 0 in clip space), which also
 * removes anything behind the eye. Returns the new vertex count (up to 4). */
static int clip_near(const CVert *in, int n, CVert *out) {
    int m = 0;
    for (int i = 0; i < n; i++) {
        const CVert *a = &in[i];
        const CVert *b = &in[(i + 1) % n];
        float da = a->clip[2] + a->clip[3];
        float db = b->clip[2] + b->clip[3];
        int ina = da >= 0.0f, inb = db >= 0.0f;
        if (ina) out[m++] = *a;
        if (ina != inb) out[m++] = lerp_cv(a, b, da / (da - db));
    }
    return m;
}

static Frag to_frag(const CVert *c) {
    Frag f;
    float w = c->clip[3];
    float inv = (w > 1e-6f) ? 1.0f / w : 0.0f;
    f.x = (float)vp_x + (c->clip[0] * inv * 0.5f + 0.5f) * (float)vp_w;
    f.y = (float)vp_y + (0.5f - c->clip[1] * inv * 0.5f) * (float)vp_h;
    f.z = c->clip[2] * inv * 0.5f + 0.5f;
    f.invw = inv;
    f.su = c->u * inv; f.sv = c->v * inv;
    f.r = c->r; f.g = c->g; f.b = c->b; f.a = c->a; f.fog = c->fog;
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
            }

            float r = l0 * a->r + l1 * b->r + l2 * c->r;
            float g = l0 * a->g + l1 * b->g + l2 * c->g;
            float bl = l0 * a->b + l1 * b->b + l2 * c->b;
            float iw = (tx || fog_enabled)
                ? (l0 * a->invw + l1 * b->invw + l2 * c->invw) : 0.0f;
            if (tx) {
                float u = (l0 * a->su + l1 * b->su + l2 * c->su) / iw;
                float v = (l0 * a->sv + l1 * b->sv + l2 * c->sv) / iw;
                int iu = (int)(u * tx->w) % tx->w; if (iu < 0) iu += tx->w;
                int iv = (int)(v * tx->h) % tx->h; if (iv < 0) iv += tx->h;
                uint32_t texel = tx->pixels[iv * tx->w + iu];
                r  *= ((texel >> 16) & 255) * (1.0f / 255.0f);
                g  *= ((texel >> 8)  & 255) * (1.0f / 255.0f);
                bl *= ( texel        & 255) * (1.0f / 255.0f);
            }
            if (fog_enabled) {
                /* Per-fragment linear fog. For a perspective projection the
                 * eye-space depth equals clip w == 1/invw. */
                float dist = iw > 1e-6f ? 1.0f / iw : 0.0f;
                float denom = fog_end - fog_start;
                float fg = denom > 1e-6f ? clampf((fog_end - dist) / denom, 0.0f, 1.0f) : 1.0f;
                r  = fg * r  + (1.0f - fg) * fog_color[0];
                g  = fg * g  + (1.0f - fg) * fog_color[1];
                bl = fg * bl + (1.0f - fg) * fog_color[2];
            }
            if (blend_enabled) {
                float al = clampf(l0 * a->a + l1 * b->a + l2 * c->a, 0.0f, 1.0f);
                uint32_t dst = color_buffer[idx];
                float dr = ((dst >> 16) & 255) * (1.0f / 255.0f);
                float dg = ((dst >> 8)  & 255) * (1.0f / 255.0f);
                float db = ( dst        & 255) * (1.0f / 255.0f);
                r  = r  * al + dr * (1.0f - al);
                g  = g  * al + dg * (1.0f - al);
                bl = bl * al + db * (1.0f - al);
            }
            if (depth_test) depth_buffer[idx] = depth;
            color_buffer[idx] = pack_color(r, g, bl);
        }
    }
}

static void emit_triangle(const Vert *a, const Vert *b, const Vert *c,
                          const float *model, const float *proj) {
    CVert tri[3] = { to_clip(a, model, proj), to_clip(b, model, proj), to_clip(c, model, proj) };
    CVert poly[8];
    int n = clip_near(tri, 3, poly);
    if (n < 3) return;
    Frag f0 = to_frag(&poly[0]);
    for (int i = 1; i + 1 < n; i++) {
        Frag f1 = to_frag(&poly[i]);
        Frag f2 = to_frag(&poly[i + 1]);
        raster_triangle(&f0, &f1, &f2);
    }
}

void glEnd(void) {
    const float *model = model_stack[model_top];
    const float *proj  = proj_stack[proj_top];

    if (prim_mode == GL_TRIANGLES) {
        for (int i = 0; i + 2 < vert_count; i += 3)
            emit_triangle(&verts[i], &verts[i + 1], &verts[i + 2], model, proj);
    } else if (prim_mode == GL_QUADS) {
        for (int i = 0; i + 3 < vert_count; i += 4) {
            emit_triangle(&verts[i], &verts[i + 1], &verts[i + 2], model, proj);
            emit_triangle(&verts[i], &verts[i + 2], &verts[i + 3], model, proj);
        }
    } else if (prim_mode == GL_TRIANGLE_FAN) {
        for (int i = 1; i + 1 < vert_count; i++)
            emit_triangle(&verts[0], &verts[i], &verts[i + 1], model, proj);
    } else if (prim_mode == GL_TRIANGLE_STRIP) {
        for (int i = 0; i + 2 < vert_count; i++) {
            if (i & 1) emit_triangle(&verts[i + 1], &verts[i], &verts[i + 2], model, proj);
            else       emit_triangle(&verts[i], &verts[i + 1], &verts[i + 2], model, proj);
        }
    }
    vert_count = 0;
}

/* ---- GLU quadrics (sphere only) ------------------------------------------ */
struct GLUquadric { int texture; int normals; };
static struct GLUquadric quadric_pool[4];
static int quadric_next;

GLUquadric *gluNewQuadric(void) {
    if (quadric_next >= 4) return 0;
    struct GLUquadric *q = &quadric_pool[quadric_next++];
    q->texture = 0; q->normals = 1;
    return q;
}
void gluDeleteQuadric(GLUquadric *q) { (void)q; }
void gluQuadricTexture(GLUquadric *q, GLboolean on) { if (q) q->texture = on ? 1 : 0; }
void gluQuadricNormals(GLUquadric *q, GLenum mode) { if (q) q->normals = (mode != 0); }

void gluSphere(GLUquadric *q, GLdouble radius, GLint slices, GLint stacks) {
    int textured = q ? q->texture : 0;
    float rad = (float)radius;
    for (int i = 0; i < stacks; i++) {
        float rho0 = GL_PI * (float)i / (float)stacks;
        float rho1 = GL_PI * (float)(i + 1) / (float)stacks;
        float sr0 = sinf(rho0), cr0 = cosf(rho0);
        float sr1 = sinf(rho1), cr1 = cosf(rho1);
        glBegin(GL_QUADS);
        for (int j = 0; j < slices; j++) {
            float th0 = 2.0f * GL_PI * (float)j / (float)slices;
            float th1 = 2.0f * GL_PI * (float)(j + 1) / (float)slices;
            float ct0 = cosf(th0), st0 = sinf(th0);
            float ct1 = cosf(th1), st1 = sinf(th1);
            float s0 = (float)j / (float)slices, s1 = (float)(j + 1) / (float)slices;
            float t0 = (float)i / (float)stacks, t1 = (float)(i + 1) / (float)stacks;

            if (textured) glTexCoord2f(s0, t0);
            glNormal3f(sr0 * ct0, cr0, sr0 * st0);
            glVertex3f(rad * sr0 * ct0, rad * cr0, rad * sr0 * st0);

            if (textured) glTexCoord2f(s0, t1);
            glNormal3f(sr1 * ct0, cr1, sr1 * st0);
            glVertex3f(rad * sr1 * ct0, rad * cr1, rad * sr1 * st0);

            if (textured) glTexCoord2f(s1, t1);
            glNormal3f(sr1 * ct1, cr1, sr1 * st1);
            glVertex3f(rad * sr1 * ct1, rad * cr1, rad * sr1 * st1);

            if (textured) glTexCoord2f(s1, t0);
            glNormal3f(sr0 * ct1, cr0, sr0 * st1);
            glVertex3f(rad * sr0 * ct1, rad * cr0, rad * sr0 * st1);
        }
        glEnd();
    }
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
