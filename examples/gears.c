/*
 * gears.c — Classic 3-D gear wheels demo adapted for OSMesa + GLFW.
 *
 * Original program by Brian Paul (public domain).
 * Adapted for GLFW by Marcus Geelnard and Camilla Löwy.
 * Adapted for OSMesa (no system GL) by the glfw_osmesa project.
 *
 * This file demonstrates that real-world GLFW examples that rely solely on
 * the fixed-function OpenGL pipeline work without modification to their GL
 * code when the OSMesa backend is substituted for the system GL driver.
 * The only changes from the original GLFW gears.c are:
 *
 *   1. Replace GLAD / glfwMakeContextCurrent with glfw_osmesa_context_create /
 *      glfw_osmesa_context_make_current.
 *   2. Replace glfwSwapBuffers with glFinish + glfw_osmesa_context_swap_buffers.
 *   3. Use GLFW_CLIENT_API = GLFW_NO_API (no system GL context is created).
 *   4. Store the OSMesa context in glfwSetWindowUserPointer so the resize
 *      callback can call glfw_osmesa_context_resize.
 *
 * Key bindings
 * ------------
 *   ESC          — close the window
 *   UP/DOWN      — tilt the scene (X-axis rotation)
 *   LEFT/RIGHT   — pan the scene (Y-axis rotation)
 *   Z            — rotate around Z axis
 *   Shift+Z      — rotate around Z axis (reverse)
 */

#if defined(_MSC_VER)
#  define _USE_MATH_DEFINES   /* make MSVC expose M_PI from math.h */
#endif

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "glfw_osmesa.h"
#include <GLFW/glfw3.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* ---- Application state ---- */
typedef struct {
    glfw_osmesa_context_t *ctx;
    GLfloat view_rotx;
    GLfloat view_roty;
    GLfloat view_rotz;
    GLint   gear1;
    GLint   gear2;
    GLint   gear3;
} app_state_t;

/* Forward declarations */
static void gear(GLfloat inner_radius, GLfloat outer_radius, GLfloat width,
                 GLint teeth, GLfloat tooth_depth);
static void init_scene(app_state_t *s);
static void draw(const app_state_t *s, GLfloat angle);
static void key_callback(GLFWwindow *window, int key, int scancode,
                         int action, int mods);
static void framebuffer_size_callback(GLFWwindow *window, int w, int h);
static void reshape(int width, int height);
static void error_callback(int code, const char *desc);

/* ------------------------------------------------------------------ */
int main(void)
{
    GLFWwindow *window;
    app_state_t state;
    int fb_width  = 300;
    int fb_height = 300;

    memset(&state, 0, sizeof(state));
    state.view_rotx = 20.0f;
    state.view_roty = 30.0f;
    state.view_rotz =  0.0f;

    glfwSetErrorCallback(error_callback);

    if (!glfwInit()) {
        fprintf(stderr, "Failed to initialise GLFW\n");
        return EXIT_FAILURE;
    }

    /* No system OpenGL context — OSMesa supplies the GL implementation. */
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE,  GLFW_TRUE);

    window = glfwCreateWindow(fb_width, fb_height, "Gears (OSMesa)", NULL, NULL);
    if (!window) {
        fprintf(stderr, "Failed to create GLFW window\n");
        glfwTerminate();
        return EXIT_FAILURE;
    }

    glfwGetFramebufferSize(window, &fb_width, &fb_height);

    state.ctx = glfw_osmesa_context_create(window, fb_width, fb_height);
    if (!state.ctx) {
        fprintf(stderr, "Failed to create OSMesa context\n");
        glfwDestroyWindow(window);
        glfwTerminate();
        return EXIT_FAILURE;
    }

    if (!glfw_osmesa_context_make_current(state.ctx)) {
        fprintf(stderr, "Failed to make OSMesa context current\n");
        glfw_osmesa_context_destroy(state.ctx);
        glfwDestroyWindow(window);
        glfwTerminate();
        return EXIT_FAILURE;
    }

    glfwSetWindowUserPointer(window, &state);
    glfwSetKeyCallback(window, key_callback);
    glfwSetFramebufferSizeCallback(window, framebuffer_size_callback);

    printf("Vendor   : %s\n", (const char *)glGetString(GL_VENDOR));
    printf("Renderer : %s\n", (const char *)glGetString(GL_RENDERER));
    printf("Version  : %s\n\n", (const char *)glGetString(GL_VERSION));
    printf("Key bindings:\n");
    printf("  ESC        -- close\n");
    printf("  UP/DOWN    -- tilt\n");
    printf("  LEFT/RIGHT -- pan\n");
    printf("  Z / Shift+Z -- rotate around Z\n\n");

    reshape(fb_width, fb_height);
    init_scene(&state);

    while (!glfwWindowShouldClose(window)) {
        GLfloat anim_angle = 100.0f * (GLfloat)glfwGetTime();

        glfwGetFramebufferSize(window, &fb_width, &fb_height);
        draw(&state, anim_angle);
        glFinish();
        glfw_osmesa_context_swap_buffers(state.ctx);

        glfwPollEvents();
    }

    /* Clean up display lists before destroying the context */
    glDeleteLists(state.gear1, 1);
    glDeleteLists(state.gear2, 1);
    glDeleteLists(state.gear3, 1);

    glfw_osmesa_context_destroy(state.ctx);
    glfwDestroyWindow(window);
    glfwTerminate();
    return EXIT_SUCCESS;
}

/*
 * gear — draw a single gear wheel using the fixed-function pipeline.
 *
 * Inputs:
 *   inner_radius — radius of hole at center
 *   outer_radius — radius at center of teeth
 *   width        — width of gear
 *   teeth        — number of teeth
 *   tooth_depth  — depth of tooth
 */
static void gear(GLfloat inner_radius, GLfloat outer_radius, GLfloat width,
                 GLint teeth, GLfloat tooth_depth)
{
    GLint   i;
    GLfloat r0, r1, r2;
    GLfloat angle, da;
    GLfloat u, v, len;

    r0 = inner_radius;
    r1 = outer_radius - tooth_depth / 2.0f;
    r2 = outer_radius + tooth_depth / 2.0f;

    da = 2.0f * (float)M_PI / (float)teeth / 4.0f;

    glShadeModel(GL_FLAT);
    glNormal3f(0.0f, 0.0f, 1.0f);

    /* front face */
    glBegin(GL_QUAD_STRIP);
    for (i = 0; i <= teeth; i++) {
        angle = (float)i * 2.0f * (float)M_PI / (float)teeth;
        glVertex3f(r0 * cosf(angle), r0 * sinf(angle),  width * 0.5f);
        glVertex3f(r1 * cosf(angle), r1 * sinf(angle),  width * 0.5f);
        if (i < teeth) {
            glVertex3f(r0 * cosf(angle),          r0 * sinf(angle),          width * 0.5f);
            glVertex3f(r1 * cosf(angle + 3 * da), r1 * sinf(angle + 3 * da), width * 0.5f);
        }
    }
    glEnd();

    /* front sides of teeth */
    glBegin(GL_QUADS);
    for (i = 0; i < teeth; i++) {
        angle = (float)i * 2.0f * (float)M_PI / (float)teeth;
        glVertex3f(r1 * cosf(angle),          r1 * sinf(angle),          width * 0.5f);
        glVertex3f(r2 * cosf(angle + da),     r2 * sinf(angle + da),     width * 0.5f);
        glVertex3f(r2 * cosf(angle + 2 * da), r2 * sinf(angle + 2 * da), width * 0.5f);
        glVertex3f(r1 * cosf(angle + 3 * da), r1 * sinf(angle + 3 * da), width * 0.5f);
    }
    glEnd();

    glNormal3f(0.0f, 0.0f, -1.0f);

    /* back face */
    glBegin(GL_QUAD_STRIP);
    for (i = 0; i <= teeth; i++) {
        angle = (float)i * 2.0f * (float)M_PI / (float)teeth;
        glVertex3f(r1 * cosf(angle), r1 * sinf(angle), -width * 0.5f);
        glVertex3f(r0 * cosf(angle), r0 * sinf(angle), -width * 0.5f);
        if (i < teeth) {
            glVertex3f(r1 * cosf(angle + 3 * da), r1 * sinf(angle + 3 * da), -width * 0.5f);
            glVertex3f(r0 * cosf(angle),           r0 * sinf(angle),          -width * 0.5f);
        }
    }
    glEnd();

    /* back sides of teeth */
    glBegin(GL_QUADS);
    for (i = 0; i < teeth; i++) {
        angle = (float)i * 2.0f * (float)M_PI / (float)teeth;
        glVertex3f(r1 * cosf(angle + 3 * da), r1 * sinf(angle + 3 * da), -width * 0.5f);
        glVertex3f(r2 * cosf(angle + 2 * da), r2 * sinf(angle + 2 * da), -width * 0.5f);
        glVertex3f(r2 * cosf(angle + da),     r2 * sinf(angle + da),     -width * 0.5f);
        glVertex3f(r1 * cosf(angle),          r1 * sinf(angle),          -width * 0.5f);
    }
    glEnd();

    /* outward faces of teeth */
    glBegin(GL_QUAD_STRIP);
    for (i = 0; i < teeth; i++) {
        angle = (float)i * 2.0f * (float)M_PI / (float)teeth;

        glVertex3f(r1 * cosf(angle), r1 * sinf(angle),  width * 0.5f);
        glVertex3f(r1 * cosf(angle), r1 * sinf(angle), -width * 0.5f);

        u = r2 * cosf(angle + da) - r1 * cosf(angle);
        v = r2 * sinf(angle + da) - r1 * sinf(angle);
        len = sqrtf(u * u + v * v);
        u /= len;
        v /= len;
        glNormal3f(v, -u, 0.0f);
        glVertex3f(r2 * cosf(angle + da),     r2 * sinf(angle + da),      width * 0.5f);
        glVertex3f(r2 * cosf(angle + da),     r2 * sinf(angle + da),     -width * 0.5f);
        glNormal3f(cosf(angle), sinf(angle), 0.0f);
        glVertex3f(r2 * cosf(angle + 2 * da), r2 * sinf(angle + 2 * da),  width * 0.5f);
        glVertex3f(r2 * cosf(angle + 2 * da), r2 * sinf(angle + 2 * da), -width * 0.5f);

        u = r1 * cosf(angle + 3 * da) - r2 * cosf(angle + 2 * da);
        v = r1 * sinf(angle + 3 * da) - r2 * sinf(angle + 2 * da);
        glNormal3f(v, -u, 0.0f);
        glVertex3f(r1 * cosf(angle + 3 * da), r1 * sinf(angle + 3 * da),  width * 0.5f);
        glVertex3f(r1 * cosf(angle + 3 * da), r1 * sinf(angle + 3 * da), -width * 0.5f);
        glNormal3f(cosf(angle), sinf(angle), 0.0f);
    }

    glVertex3f(r1 * cosf(0.0f), r1 * sinf(0.0f),  width * 0.5f);
    glVertex3f(r1 * cosf(0.0f), r1 * sinf(0.0f), -width * 0.5f);
    glEnd();

    glShadeModel(GL_SMOOTH);

    /* inside radius cylinder */
    glBegin(GL_QUAD_STRIP);
    for (i = 0; i <= teeth; i++) {
        angle = (float)i * 2.0f * (float)M_PI / (float)teeth;
        glNormal3f(-cosf(angle), -sinf(angle), 0.0f);
        glVertex3f(r0 * cosf(angle), r0 * sinf(angle), -width * 0.5f);
        glVertex3f(r0 * cosf(angle), r0 * sinf(angle),  width * 0.5f);
    }
    glEnd();
}

/* Set up lighting, materials, and compile gear display lists. */
static void init_scene(app_state_t *s)
{
    static const GLfloat pos[4]   = {5.0f,  5.0f, 10.0f, 0.0f};
    static const GLfloat red[4]   = {0.8f,  0.1f,  0.0f, 1.0f};
    static const GLfloat green[4] = {0.0f,  0.8f,  0.2f, 1.0f};
    static const GLfloat blue[4]  = {0.2f,  0.2f,  1.0f, 1.0f};

    glLightfv(GL_LIGHT0, GL_POSITION, pos);
    glEnable(GL_CULL_FACE);
    glEnable(GL_LIGHTING);
    glEnable(GL_LIGHT0);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_NORMALIZE);

    s->gear1 = glGenLists(1);
    glNewList(s->gear1, GL_COMPILE);
        glMaterialfv(GL_FRONT, GL_AMBIENT_AND_DIFFUSE, red);
        gear(1.0f, 4.0f, 1.0f, 20, 0.7f);
    glEndList();

    s->gear2 = glGenLists(1);
    glNewList(s->gear2, GL_COMPILE);
        glMaterialfv(GL_FRONT, GL_AMBIENT_AND_DIFFUSE, green);
        gear(0.5f, 2.0f, 2.0f, 10, 0.7f);
    glEndList();

    s->gear3 = glGenLists(1);
    glNewList(s->gear3, GL_COMPILE);
        glMaterialfv(GL_FRONT, GL_AMBIENT_AND_DIFFUSE, blue);
        gear(1.3f, 2.0f, 0.5f, 10, 0.7f);
    glEndList();
}

/* Draw all three gears at their current positions. */
static void draw(const app_state_t *s, GLfloat angle)
{
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    glPushMatrix();
        glRotatef(s->view_rotx, 1.0f, 0.0f, 0.0f);
        glRotatef(s->view_roty, 0.0f, 1.0f, 0.0f);
        glRotatef(s->view_rotz, 0.0f, 0.0f, 1.0f);

        glPushMatrix();
            glTranslatef(-3.0f, -2.0f, 0.0f);
            glRotatef(angle, 0.0f, 0.0f, 1.0f);
            glCallList(s->gear1);
        glPopMatrix();

        glPushMatrix();
            glTranslatef(3.1f, -2.0f, 0.0f);
            glRotatef(-2.0f * angle - 9.0f, 0.0f, 0.0f, 1.0f);
            glCallList(s->gear2);
        glPopMatrix();

        glPushMatrix();
            glTranslatef(-3.1f, 4.2f, 0.0f);
            glRotatef(-2.0f * angle - 25.0f, 0.0f, 0.0f, 1.0f);
            glCallList(s->gear3);
        glPopMatrix();
    glPopMatrix();
}

/* Update the GL projection and viewport to match the current framebuffer. */
static void reshape(int width, int height)
{
    GLfloat h     = (height > 0) ? (GLfloat)height / (GLfloat)width : 1.0f;
    GLfloat znear = 5.0f;
    GLfloat zfar  = 30.0f;
    GLfloat xmax  = znear * 0.5f;

    glViewport(0, 0, (GLint)width, (GLint)height);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glFrustum(-xmax, xmax, -xmax * h, xmax * h, znear, zfar);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glTranslatef(0.0f, 0.0f, -20.0f);
}

/* Arrow keys tilt / pan the scene; Z rotates around Z axis. */
static void key_callback(GLFWwindow *window, int key, int scancode,
                         int action, int mods)
{
    app_state_t *s = (app_state_t *)glfwGetWindowUserPointer(window);
    (void)scancode;

    if (action != GLFW_PRESS && action != GLFW_REPEAT)
        return;

    switch (key) {
        case GLFW_KEY_ESCAPE:
            glfwSetWindowShouldClose(window, GLFW_TRUE);
            break;
        case GLFW_KEY_UP:
            s->view_rotx += 5.0f;
            break;
        case GLFW_KEY_DOWN:
            s->view_rotx -= 5.0f;
            break;
        case GLFW_KEY_LEFT:
            s->view_roty += 5.0f;
            break;
        case GLFW_KEY_RIGHT:
            s->view_roty -= 5.0f;
            break;
        case GLFW_KEY_Z:
            if (mods & GLFW_MOD_SHIFT)
                s->view_rotz -= 5.0f;
            else
                s->view_rotz += 5.0f;
            break;
        default:
            break;
    }
}

static void framebuffer_size_callback(GLFWwindow *window, int w, int h)
{
    app_state_t *s = (app_state_t *)glfwGetWindowUserPointer(window);
    if (s && s->ctx)
        glfw_osmesa_context_resize(s->ctx, w, h);
    reshape(w, h);
}

static void error_callback(int code, const char *desc)
{
    fprintf(stderr, "GLFW error %d: %s\n", code, desc);
}
