/*
 * triangle.c — Render a coloured triangle with OSMesa and display it in a
 *              GLFW window, using no system OpenGL or Vulkan.
 *
 * Build (after cmake --build):
 *   Produced automatically as the "triangle" target by CMakeLists.txt.
 *
 * What this demonstrates
 * ----------------------
 * 1.  A GLFW window is opened with GLFW_CLIENT_API = GLFW_NO_API so that
 *     GLFW does not create a system OpenGL context.
 * 2.  glfw_osmesa_context_create() wraps an OSMesa context + pixel buffer.
 *     Note the snake_case prefix: this is a USER-LAND HELPER, not a
 *     proposed addition to GLFW itself.
 * 3.  An ordinary OpenGL fixed-function triangle is drawn each frame.
 * 4.  glfw_osmesa_context_swap_buffers() blits the rendered pixels to the
 *     window using only OS drawing primitives (XPutImage / SetDIBitsToDevice
 *     / CALayer setContents).  This is the user-land implementation of the
 *     single function that IS proposed for GLFW upstream: glfwBlitPixelBuffer.
 * 5.  A framebuffer size callback calls glfw_osmesa_context_resize() so the
 *     pixel buffer tracks window resizes.
 */

#include <stdio.h>
#include <stdlib.h>

#include <GLFW/glfw3.h>
#include "glfw_osmesa.h"

/* Forward declarations */
static void draw_frame(int width, int height, float angle);
static void framebuffer_size_callback(GLFWwindow *window, int w, int h);
static void error_callback(int code, const char *desc);

/* Global — shared with the resize callback.
 * In a real application use glfwGetWindowUserPointer() instead. */
static glfw_osmesa_context_t *g_ctx = NULL;

/* ------------------------------------------------------------------ */

int main(void)
{
    GLFWwindow *window;
    int         fb_width  = 800;
    int         fb_height = 600;
    float       angle     = 0.0f;
    double      last_time;

    glfwSetErrorCallback(error_callback);

    if (!glfwInit()) {
        fprintf(stderr, "Failed to initialise GLFW\n");
        return EXIT_FAILURE;
    }

    /* ----------------------------------------------------------------
     * Create a GLFW window with NO system OpenGL context.
     * GLFW_NO_API is the key hint; GLFW will manage the window and
     * events without asking the OS for a GL/Vulkan context.
     * --------------------------------------------------------------- */
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE,  GLFW_TRUE);

    window = glfwCreateWindow(fb_width, fb_height,
                              "OSMesa + GLFW (no system GL)", NULL, NULL);
    if (!window) {
        fprintf(stderr, "Failed to create GLFW window\n");
        glfwTerminate();
        return EXIT_FAILURE;
    }

    /* HiDPI: actual framebuffer may differ from requested window size. */
    glfwGetFramebufferSize(window, &fb_width, &fb_height);

    /* ----------------------------------------------------------------
     * Set up the OSMesa context (user-land helper, NOT a GLFW API).
     * --------------------------------------------------------------- */
    g_ctx = glfw_osmesa_context_create(window, fb_width, fb_height);
    if (!g_ctx) {
        fprintf(stderr, "Failed to create OSMesa context\n");
        glfwDestroyWindow(window);
        glfwTerminate();
        return EXIT_FAILURE;
    }

    if (!glfw_osmesa_context_make_current(g_ctx)) {
        fprintf(stderr, "Failed to make OSMesa context current\n");
        glfw_osmesa_context_destroy(g_ctx);
        glfwDestroyWindow(window);
        glfwTerminate();
        return EXIT_FAILURE;
    }

    glfwSetFramebufferSizeCallback(window, framebuffer_size_callback);

    printf("Vendor   : %s\n", glGetString(GL_VENDOR));
    printf("Renderer : %s\n", glGetString(GL_RENDERER));
    printf("Version  : %s\n", glGetString(GL_VERSION));

    last_time = glfwGetTime();

    /* ----------------------------------------------------------------
     * Main loop
     * --------------------------------------------------------------- */
    while (!glfwWindowShouldClose(window)) {
        double now   = glfwGetTime();
        double delta = now - last_time;
        last_time    = now;

        angle += (float)(delta * 60.0);   /* 60 degrees per second */

        glfwGetFramebufferSize(window, &fb_width, &fb_height);
        draw_frame(fb_width, fb_height, angle);

        glFinish();  /* ensure all OSMesa rendering has completed */

        /*
         * Present the frame.  Internally this calls the user-land
         * implementation of glfwBlitPixelBuffer — the ONLY function
         * proposed for addition to GLFW itself.
         */
        glfw_osmesa_context_swap_buffers(g_ctx);

        glfwPollEvents();
    }

    /* ----------------------------------------------------------------
     * Cleanup
     * --------------------------------------------------------------- */
    glfw_osmesa_context_destroy(g_ctx);
    glfwDestroyWindow(window);
    glfwTerminate();
    return EXIT_SUCCESS;
}

/* ------------------------------------------------------------------
 * Render one frame: a rotating RGB triangle
 * ------------------------------------------------------------------ */
static void draw_frame(int width, int height, float angle)
{
    float aspect = (height > 0) ? (float)width / (float)height : 1.0f;

    glViewport(0, 0, width, height);
    glClearColor(0.1f, 0.1f, 0.15f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(-aspect, aspect, -1.0, 1.0, -1.0, 1.0);

    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glRotatef(angle, 0.0f, 0.0f, 1.0f);

    glBegin(GL_TRIANGLES);
        glColor3f(1.0f, 0.0f, 0.0f);  glVertex2f( 0.0f,  0.8f);
        glColor3f(0.0f, 1.0f, 0.0f);  glVertex2f(-0.7f, -0.4f);
        glColor3f(0.0f, 0.0f, 1.0f);  glVertex2f( 0.7f, -0.4f);
    glEnd();
}

/* ------------------------------------------------------------------
 * GLFW callbacks
 * ------------------------------------------------------------------ */
static void framebuffer_size_callback(GLFWwindow *window, int w, int h)
{
    (void)window;
    if (g_ctx)
        glfw_osmesa_context_resize(g_ctx, w, h);
}

static void error_callback(int code, const char *desc)
{
    fprintf(stderr, "GLFW error %d: %s\n", code, desc);
}
