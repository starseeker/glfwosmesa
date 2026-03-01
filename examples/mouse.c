/*
 * mouse.c — Demonstrate GLFW mouse input with an OSMesa-rendered scene.
 *
 * GLFW features shown
 * -------------------
 * glfwSetCursorPosCallback()   Fires every time the cursor moves.
 * glfwSetMouseButtonCallback() Fires on button press and release.
 * glfwSetScrollCallback()      Fires on scroll-wheel or touchpad scroll.
 * glfwSetWindowUserPointer()   Pass application state into callbacks.
 * glfwGetWindowSize()          Convert cursor pixel coords to scene coords.
 *
 * Interactions
 * ------------
 *   Move cursor    — a crosshair tracks the cursor
 *   Left click     — stamp a coloured dot at the cursor position
 *   Right click    — clear all dots
 *   Scroll up/down — resize the crosshair
 *   ESC / Q        — close the window
 */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "glfw_osmesa.h"
#include <GLFW/glfw3.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define MAX_DOTS 256

/* One stamped dot */
typedef struct { float x, y, r, g, b; } dot_t;

/* Palette to cycle through on each left-click */
static const float PALETTE[][3] = {
    {1.0f, 0.3f, 0.3f},
    {0.3f, 1.0f, 0.3f},
    {0.3f, 0.3f, 1.0f},
    {1.0f, 1.0f, 0.3f},
    {0.3f, 1.0f, 1.0f},
    {1.0f, 0.3f, 1.0f},
    {1.0f, 0.6f, 0.2f},
    {0.8f, 0.8f, 0.8f},
};
#define PALETTE_SIZE ((int)(sizeof(PALETTE) / sizeof(PALETTE[0])))

/* Application state */
typedef struct {
    glfw_osmesa_context_t *ctx;

    /* Scene-space cursor position (updated in cursor_pos_callback) */
    float cursor_sx, cursor_sy;

    /* Crosshair radius in scene units */
    float cursor_radius;

    /* Stamped dots */
    dot_t dots[MAX_DOTS];
    int   dot_count;
    int   palette_idx;  /* which palette colour to use next */
} app_state_t;

/* Forward declarations */
static void cursor_pos_callback(GLFWwindow *window, double xpos, double ypos);
static void mouse_button_callback(GLFWwindow *window, int button,
                                  int action, int mods);
static void scroll_callback(GLFWwindow *window, double xoff, double yoff);
static void key_callback(GLFWwindow *window, int key, int scancode,
                         int action, int mods);
static void framebuffer_size_callback(GLFWwindow *window, int w, int h);
static void error_callback(int code, const char *desc);
static void draw_frame(int width, int height, const app_state_t *s);
static void draw_dot(float cx, float cy, float radius,
                     float r, float g, float b);
static void draw_crosshair(float cx, float cy, float radius);

/* ------------------------------------------------------------------ */
int main(void)
{
    GLFWwindow *window;
    app_state_t state;
    int fb_width  = 800;
    int fb_height = 600;

    memset(&state, 0, sizeof(state));
    state.cursor_radius = 0.08f;

    glfwSetErrorCallback(error_callback);

    if (!glfwInit()) {
        fprintf(stderr, "Failed to initialise GLFW\n");
        return EXIT_FAILURE;
    }

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE,  GLFW_TRUE);

    window = glfwCreateWindow(fb_width, fb_height,
                              "OSMesa + GLFW -- Mouse Demo", NULL, NULL);
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

    /*
     * Register all mouse and keyboard callbacks.  Each callback receives
     * the window pointer; glfwGetWindowUserPointer() recovers app state.
     */
    glfwSetCursorPosCallback(window,   cursor_pos_callback);
    glfwSetMouseButtonCallback(window, mouse_button_callback);
    glfwSetScrollCallback(window,      scroll_callback);
    glfwSetKeyCallback(window,         key_callback);
    glfwSetFramebufferSizeCallback(window, framebuffer_size_callback);

    printf("Renderer : %s\n", (const char *)glGetString(GL_RENDERER));
    printf("Version  : %s\n\n", (const char *)glGetString(GL_VERSION));
    printf("Interactions:\n");
    printf("  Move cursor    -- crosshair tracks the cursor\n");
    printf("  Left click     -- stamp a coloured dot\n");
    printf("  Right click    -- clear all dots\n");
    printf("  Scroll up/down -- resize the crosshair\n");
    printf("  ESC / Q        -- close\n\n");

    while (!glfwWindowShouldClose(window)) {
        glfwGetFramebufferSize(window, &fb_width, &fb_height);
        draw_frame(fb_width, fb_height, &state);
        glFinish();
        glfw_osmesa_context_swap_buffers(state.ctx);
        glfwPollEvents();
    }

    glfw_osmesa_context_destroy(state.ctx);
    glfwDestroyWindow(window);
    glfwTerminate();
    return EXIT_SUCCESS;
}

/*
 * cursor_pos_callback — fired whenever the cursor moves.
 *
 * GLFW delivers cursor coordinates in window-pixel space (top-left origin).
 * We convert to scene space so the rest of the rendering code can work in
 * the same coordinate system as the GL projection
 * (x in [-aspect, +aspect], y in [-1, +1]).
 */
static void cursor_pos_callback(GLFWwindow *window, double xpos, double ypos)
{
    app_state_t *s = (app_state_t *)glfwGetWindowUserPointer(window);
    int win_w, win_h;
    float aspect;

    glfwGetWindowSize(window, &win_w, &win_h);
    if (win_w <= 0 || win_h <= 0) return;

    aspect = (float)win_w / (float)win_h;
    s->cursor_sx = (float)(xpos / win_w)  * 2.0f * aspect - aspect;
    s->cursor_sy = 1.0f - (float)(ypos / win_h) * 2.0f;
}

/*
 * mouse_button_callback — fired on button press and release.
 *
 * Left-click  — stamp a dot at the current cursor scene position.
 * Right-click — clear all previously stamped dots.
 */
static void mouse_button_callback(GLFWwindow *window, int button,
                                  int action, int mods)
{
    app_state_t *s = (app_state_t *)glfwGetWindowUserPointer(window);
    (void)mods;

    if (action != GLFW_PRESS)
        return;

    if (button == GLFW_MOUSE_BUTTON_LEFT) {
        if (s->dot_count < MAX_DOTS) {
            const float *col = PALETTE[s->palette_idx % PALETTE_SIZE];
            dot_t *d = &s->dots[s->dot_count++];
            d->x = s->cursor_sx;
            d->y = s->cursor_sy;
            d->r = col[0];
            d->g = col[1];
            d->b = col[2];
            s->palette_idx = (s->palette_idx + 1) % PALETTE_SIZE;
            printf("Dot stamped at (%.2f, %.2f) -- %d total\n",
                   (double)d->x, (double)d->y, s->dot_count);
        }
    } else if (button == GLFW_MOUSE_BUTTON_RIGHT) {
        s->dot_count   = 0;
        s->palette_idx = 0;
        printf("Dots cleared\n");
    }
}

/*
 * scroll_callback — fired on scroll-wheel or touchpad two-finger scroll.
 *
 * yoff > 0 => scroll up   => larger crosshair
 * yoff < 0 => scroll down => smaller crosshair
 */
static void scroll_callback(GLFWwindow *window, double xoff, double yoff)
{
    app_state_t *s = (app_state_t *)glfwGetWindowUserPointer(window);
    (void)xoff;
    s->cursor_radius += (float)yoff * 0.01f;
    if (s->cursor_radius < 0.01f) s->cursor_radius = 0.01f;
    if (s->cursor_radius > 0.50f) s->cursor_radius = 0.50f;
}

static void key_callback(GLFWwindow *window, int key, int scancode,
                         int action, int mods)
{
    (void)scancode;
    (void)mods;
    if (action != GLFW_PRESS) return;
    if (key == GLFW_KEY_ESCAPE || key == GLFW_KEY_Q)
        glfwSetWindowShouldClose(window, GLFW_TRUE);
}

static void framebuffer_size_callback(GLFWwindow *window, int w, int h)
{
    app_state_t *s = (app_state_t *)glfwGetWindowUserPointer(window);
    if (s && s->ctx)
        glfw_osmesa_context_resize(s->ctx, w, h);
}

static void error_callback(int code, const char *desc)
{
    fprintf(stderr, "GLFW error %d: %s\n", code, desc);
}

/* ------------------------------------------------------------------
 * Rendering helpers
 * ------------------------------------------------------------------ */

/* Draw a filled circle using a GL_TRIANGLE_FAN. */
static void draw_dot(float cx, float cy, float radius,
                     float r, float g, float b)
{
    int i;
    int segments = 24;
    float step = 2.0f * (float)M_PI / (float)segments;

    glColor3f(r, g, b);
    glBegin(GL_TRIANGLE_FAN);
        glVertex2f(cx, cy);
        for (i = 0; i <= segments; ++i) {
            float a = (float)i * step;
            glVertex2f(cx + cosf(a) * radius, cy + sinf(a) * radius);
        }
    glEnd();
}

/* Draw a crosshair: outer ring + two perpendicular lines. */
static void draw_crosshair(float cx, float cy, float radius)
{
    int i;
    int segments = 32;
    float step = 2.0f * (float)M_PI / (float)segments;
    float arm  = radius * 1.4f;

    /* Outer ring */
    glColor3f(1.0f, 1.0f, 1.0f);
    glBegin(GL_LINE_LOOP);
        for (i = 0; i < segments; ++i) {
            float a = (float)i * step;
            glVertex2f(cx + cosf(a) * radius, cy + sinf(a) * radius);
        }
    glEnd();

    /* Cross arms */
    glBegin(GL_LINES);
        glVertex2f(cx - arm, cy);  glVertex2f(cx + arm, cy);
        glVertex2f(cx, cy - arm);  glVertex2f(cx, cy + arm);
    glEnd();
}

static void draw_frame(int width, int height, const app_state_t *s)
{
    int i;
    float aspect = (height > 0) ? (float)width / (float)height : 1.0f;

    glViewport(0, 0, width, height);
    glClearColor(0.08f, 0.08f, 0.12f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(-aspect, aspect, -1.0, 1.0, -1.0, 1.0);

    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    /* Draw all stamped dots */
    for (i = 0; i < s->dot_count; ++i) {
        const dot_t *d = &s->dots[i];
        draw_dot(d->x, d->y, 0.04f, d->r, d->g, d->b);
    }

    /* Draw the cursor crosshair on top */
    draw_crosshair(s->cursor_sx, s->cursor_sy, s->cursor_radius);
}
