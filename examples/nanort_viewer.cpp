/*
 * nanort_viewer.cpp — Interactive CPU ray tracer using NanoRT, displayed in a
 *                     GLFW window with no system OpenGL context.
 *
 * This example shows NanoRT as an alternative rendering backend to the OSMesa
 * path in examples/triangle.c.  The GLFW setup and pixel-blit strategy are
 * identical; only the source of the rendered pixels differs:
 *
 *   OSMesa path  : OpenGL → OSMesa software rasteriser → BGRA pixel buffer
 *   NanoRT path  : NanoRT CPU ray tracer                → BGRA pixel buffer
 *
 * Both paths end with the same glfw_blit_pixels() call to present the frame.
 *
 * Scene: a rainbow-coloured cube lit by a single directional light.
 *
 * Controls
 * --------
 *   Left-mouse drag  — orbit camera (horizontal = azimuth, vertical = elevation)
 *   Scroll wheel     — zoom in / out
 *   Escape           — quit
 */

#include "glfw_osmesa.h"        /* glfw_blit_pixels()                   */
#include <GLFW/glfw3.h>

#include "nanort.h"             /* NanoRT header-only ray tracer        */

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

/* -------------------------------------------------------------------------
 * Minimal 3-component float vector helpers
 * ---------------------------------------------------------------------- */

struct Vec3 {
    float x, y, z;
    Vec3()                     : x(0.f), y(0.f), z(0.f) {}
    Vec3(float x,float y,float z) : x(x),  y(y),  z(z)  {}
    Vec3 operator+(const Vec3 &b) const { return {x+b.x, y+b.y, z+b.z}; }
    Vec3 operator-(const Vec3 &b) const { return {x-b.x, y-b.y, z-b.z}; }
    Vec3 operator*(float s)       const { return {x*s,   y*s,   z*s  }; }
};

static inline float dot(const Vec3 &a, const Vec3 &b)
{
    return a.x*b.x + a.y*b.y + a.z*b.z;
}

static inline Vec3 cross(const Vec3 &a, const Vec3 &b)
{
    return { a.y*b.z - a.z*b.y,
             a.z*b.x - a.x*b.z,
             a.x*b.y - a.y*b.x };
}

static inline Vec3 normalize(const Vec3 &v)
{
    float r = 1.f / std::sqrt(dot(v, v));
    return v * r;
}

/* -------------------------------------------------------------------------
 * Scene geometry: a unit cube with 6 face colours
 *
 * Eight vertices are shared; each of the 6 faces consists of two triangles.
 * Per-face colour is looked up with  face_id = prim_id / 2.
 * ---------------------------------------------------------------------- */

static const float kVerts[] = {
    /* 0 */ -1.f, -1.f, -1.f,
    /* 1 */  1.f, -1.f, -1.f,
    /* 2 */  1.f,  1.f, -1.f,
    /* 3 */ -1.f,  1.f, -1.f,
    /* 4 */ -1.f, -1.f,  1.f,
    /* 5 */  1.f, -1.f,  1.f,
    /* 6 */  1.f,  1.f,  1.f,
    /* 7 */ -1.f,  1.f,  1.f,
};

/*
 * Counter-clockwise winding (viewed from outside) so that
 * cross(e0, e1) always points outward — used for shading normals.
 */
static const unsigned int kFaces[] = {
    /* front  (-Z) normal (0,0,-1) */ 0,3,2,  0,2,1,
    /* back   (+Z) normal (0,0,+1) */ 4,5,6,  4,6,7,
    /* left   (-X) normal (-1,0,0) */ 0,4,7,  0,7,3,
    /* right  (+X) normal (+1,0,0) */ 1,2,6,  1,6,5,
    /* bottom (-Y) normal (0,-1,0) */ 0,1,5,  0,5,4,
    /* top    (+Y) normal (0,+1,0) */ 2,3,7,  2,7,6,
};

static const int kNumTris = 12;

/* RGB face colours (faces 0-5, each face covers two consecutive triangles). */
static const float kFaceRGB[6][3] = {
    { 0.90f, 0.20f, 0.20f },  /* front  — red     */
    { 0.20f, 0.80f, 0.25f },  /* back   — green   */
    { 0.20f, 0.40f, 0.90f },  /* left   — blue    */
    { 0.90f, 0.80f, 0.10f },  /* right  — yellow  */
    { 0.10f, 0.80f, 0.80f },  /* bottom — cyan    */
    { 0.80f, 0.10f, 0.80f },  /* top    — magenta */
};

/* -------------------------------------------------------------------------
 * Orbit camera
 * ---------------------------------------------------------------------- */

struct Camera {
    float theta;   /* azimuth  (radians) */
    float phi;     /* elevation (radians) */
    float radius;  /* distance from origin */
};

static Vec3 cam_eye(const Camera &c)
{
    return {
        c.radius * std::cos(c.phi) * std::sin(c.theta),
        c.radius * std::sin(c.phi),
        c.radius * std::cos(c.phi) * std::cos(c.theta),
    };
}

/* -------------------------------------------------------------------------
 * Render one frame into a BGRA pixel buffer
 * ---------------------------------------------------------------------- */

static void render_frame(unsigned char               *fb,
                         int                          width,
                         int                          height,
                         const Camera                &camera,
                         const nanort::BVHAccel<float>&bvh)
{
    if (width <= 0 || height <= 0) return;

    /* Camera basis. */
    Vec3 eye     = cam_eye(camera);
    Vec3 target  = { 0.f, 0.f, 0.f };
    Vec3 up      = { 0.f, 1.f, 0.f };

    Vec3 forward = normalize(target - eye);
    Vec3 right   = normalize(cross(forward, up));
    Vec3 up_cam  = cross(right, forward);

    /* Vertical field of view (60°). */
    static const float kPI = 3.14159265358979323846f;
    float half_h = std::tan(60.f * kPI / 180.f * 0.5f);
    float half_w = half_h * ((float)width / (float)height);

    /* Fixed sun direction. */
    Vec3 sun = normalize({ 1.f, 2.f, 1.5f });

    for (int py = 0; py < height; ++py) {
        for (int px = 0; px < width; ++px) {

            /* Compute primary-ray direction (pinhole camera). */
            float u = (2.f * ((float)px + 0.5f) / (float)width  - 1.f) * half_w;
            float v = (1.f - 2.f * ((float)py + 0.5f) / (float)height) * half_h;

            Vec3 dir = normalize(forward + right * u + up_cam * v);

            nanort::Ray<float> ray;
            ray.org[0] = eye.x;  ray.org[1] = eye.y;  ray.org[2] = eye.z;
            ray.dir[0] = dir.x;  ray.dir[1] = dir.y;  ray.dir[2] = dir.z;
            ray.min_t  = 1e-4f;

            nanort::TriangleIntersector<float>   isector(kVerts, kFaces,
                                                          sizeof(float) * 3);
            nanort::TriangleIntersection<float>  hit;

            float r, g, b;

            if (bvh.Traverse(ray, isector, &hit)) {
                /* Face colour. */
                unsigned int fi = hit.prim_id / 2u;
                const float *fc = kFaceRGB[fi < 6u ? fi : 0u];

                /* Geometric normal from vertex data. */
                unsigned int i0 = kFaces[hit.prim_id * 3 + 0];
                unsigned int i1 = kFaces[hit.prim_id * 3 + 1];
                unsigned int i2 = kFaces[hit.prim_id * 3 + 2];
                Vec3 p0 = { kVerts[i0*3], kVerts[i0*3+1], kVerts[i0*3+2] };
                Vec3 p1 = { kVerts[i1*3], kVerts[i1*3+1], kVerts[i1*3+2] };
                Vec3 p2 = { kVerts[i2*3], kVerts[i2*3+1], kVerts[i2*3+2] };
                Vec3 n  = normalize(cross(p1 - p0, p2 - p0));

                /* Diffuse + ambient shading. */
                float diff      = std::max(0.f, dot(n, sun));
                /* Blinn-Phong specular highlight. */
                Vec3  hit_pos   = eye + dir * hit.t;
                Vec3  view_dir  = normalize(eye - hit_pos);
                Vec3  half_vec  = normalize(sun + view_dir);
                float spec      = std::pow(std::max(0.f, dot(n, half_vec)), 32.f);
                float intensity = 0.15f + 0.75f * diff + 0.10f * spec;

                r = fc[0] * intensity;
                g = fc[1] * intensity;
                b = fc[2] * intensity;
            } else {
                /* Sky gradient. */
                float t = 0.5f * (dir.y + 1.f);
                r = (1.f - t) * 1.f + t * 0.40f;
                g = (1.f - t) * 1.f + t * 0.55f;
                b = (1.f - t) * 1.f + t * 0.90f;
            }

            /* Write BGRA (matches glfw_blit_pixels / _platform_blit format). */
            int idx       = (py * width + px) * 4;
            fb[idx + 0]   = (unsigned char)(std::min(b, 1.f) * 255.f);
            fb[idx + 1]   = (unsigned char)(std::min(g, 1.f) * 255.f);
            fb[idx + 2]   = (unsigned char)(std::min(r, 1.f) * 255.f);
            fb[idx + 3]   = 255u;
        }
    }
}

/* -------------------------------------------------------------------------
 * Application state — shared with GLFW callbacks via glfwSetWindowUserPointer
 * ---------------------------------------------------------------------- */

struct AppState {
    Camera                     camera;
    std::vector<unsigned char> fb;       /* BGRA pixel buffer    */
    int                        fb_w;
    int                        fb_h;
    bool                       dirty;   /* re-render needed?    */

    /* Mouse drag state. */
    bool   btn_down;
    double last_x;
    double last_y;

    nanort::BVHAccel<float>    bvh;
};

/* -------------------------------------------------------------------------
 * GLFW callbacks
 * ---------------------------------------------------------------------- */

static void cb_mouse_button(GLFWwindow *win, int button, int action, int /*mods*/)
{
    AppState *app = static_cast<AppState *>(glfwGetWindowUserPointer(win));
    if (button == GLFW_MOUSE_BUTTON_LEFT) {
        app->btn_down = (action == GLFW_PRESS);
        if (app->btn_down)
            glfwGetCursorPos(win, &app->last_x, &app->last_y);
    }
}

static void cb_cursor_pos(GLFWwindow *win, double xpos, double ypos)
{
    AppState *app = static_cast<AppState *>(glfwGetWindowUserPointer(win));
    if (!app->btn_down) return;

    double dx = xpos - app->last_x;
    double dy = ypos - app->last_y;
    app->last_x = xpos;
    app->last_y = ypos;

    /* Horizontal drag → azimuth, vertical drag → elevation. */
    app->camera.theta -= (float)(dx * 0.005);
    app->camera.phi   -= (float)(dy * 0.005);

    /* Clamp elevation away from the poles to avoid gimbal flip. */
    static const float kLimit = 3.14159265358979323846f * 0.49f;
    if (app->camera.phi >  kLimit) app->camera.phi =  kLimit;
    if (app->camera.phi < -kLimit) app->camera.phi = -kLimit;

    app->dirty = true;
}

static void cb_scroll(GLFWwindow *win, double /*xoff*/, double yoff)
{
    AppState *app = static_cast<AppState *>(glfwGetWindowUserPointer(win));
    app->camera.radius *= (float)std::pow(0.9, yoff);
    if (app->camera.radius <  0.5f) app->camera.radius =  0.5f;
    if (app->camera.radius > 50.f)  app->camera.radius = 50.f;
    app->dirty = true;
}

static void cb_framebuffer_size(GLFWwindow *win, int w, int h)
{
    AppState *app = static_cast<AppState *>(glfwGetWindowUserPointer(win));
    if (w <= 0 || h <= 0) return;
    app->fb_w = w;
    app->fb_h = h;
    app->fb.resize((size_t)w * (size_t)h * 4u);
    app->dirty = true;
}

static void cb_key(GLFWwindow *win, int key, int /*scan*/, int action, int /*mods*/)
{
    if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS)
        glfwSetWindowShouldClose(win, GLFW_TRUE);
}

static void cb_error(int code, const char *desc)
{
    fprintf(stderr, "GLFW error %d: %s\n", code, desc);
}

/* -------------------------------------------------------------------------
 * main
 * ---------------------------------------------------------------------- */

int main(void)
{
    /* ---- Build BVH from the cube geometry ---- */
    nanort::TriangleMesh<float>  mesh(kVerts, kFaces, sizeof(float) * 3);
    nanort::TriangleSAHPred<float> pred(kVerts, kFaces, sizeof(float) * 3);
    nanort::BVHBuildOptions<float> build_opts;

    AppState app;
    app.camera   = { 0.8f, 0.4f, 5.0f };  /* theta, phi, radius */
    app.dirty    = true;
    app.btn_down = false;
    /* last_x / last_y are only read after btn_down becomes true (set on first press). */
    app.last_x   = app.last_y = 0.0;
    app.fb_w     = 0;
    app.fb_h     = 0;

    if (!app.bvh.Build((unsigned int)kNumTris, mesh, pred, build_opts)) {
        fprintf(stderr, "nanort_viewer: BVH build failed\n");
        return EXIT_FAILURE;
    }

    /* ---- GLFW initialisation ---- */
    glfwSetErrorCallback(cb_error);

    if (!glfwInit()) {
        fprintf(stderr, "nanort_viewer: failed to initialise GLFW\n");
        return EXIT_FAILURE;
    }

    /*
     * GLFW_NO_API — no system OpenGL/Vulkan context is needed.
     * NanoRT renders entirely in software; we blit the pixel buffer to the
     * window using glfw_blit_pixels() (OS-native drawing primitives).
     */
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE,  GLFW_TRUE);

    GLFWwindow *window = glfwCreateWindow(800, 600,
                                          "NanoRT + GLFW (no system GL)",
                                          NULL, NULL);
    if (!window) {
        fprintf(stderr, "nanort_viewer: failed to create GLFW window\n");
        glfwTerminate();
        return EXIT_FAILURE;
    }

    glfwGetFramebufferSize(window, &app.fb_w, &app.fb_h);
    app.fb.resize((size_t)app.fb_w * (size_t)app.fb_h * 4u);

    glfwSetWindowUserPointer(window, &app);
    glfwSetMouseButtonCallback(window,     cb_mouse_button);
    glfwSetCursorPosCallback(window,       cb_cursor_pos);
    glfwSetScrollCallback(window,          cb_scroll);
    glfwSetFramebufferSizeCallback(window, cb_framebuffer_size);
    glfwSetKeyCallback(window,             cb_key);

    printf("NanoRT viewer ready.\n"
           "  Left-drag  : orbit camera\n"
           "  Scroll     : zoom in / out\n"
           "  Escape     : quit\n");

    /* ---- Main loop ---- */
    while (!glfwWindowShouldClose(window)) {
        /*
         * Only ray-trace when something has changed (camera moved, window
         * resized).  Otherwise sleep until the next GLFW event so we do not
         * burn CPU spinning.
         */
        if (app.dirty) {
            render_frame(app.fb.data(), app.fb_w, app.fb_h,
                         app.camera, app.bvh);

            /*
             * Present the ray-traced framebuffer using the same platform blit
             * used by the OSMesa backend (glfw_osmesa_context_swap_buffers).
             * This is the only display call needed — no OpenGL context required.
             */
            glfw_blit_pixels(window, app.fb.data(), app.fb_w, app.fb_h);

            app.dirty = false;
        }

        glfwWaitEvents();   /* sleep until next input event */
    }

    /* ---- Cleanup ---- */
    glfwDestroyWindow(window);
    glfwTerminate();
    return EXIT_SUCCESS;
}
