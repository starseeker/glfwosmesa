/*
 * keyboard.c — Demonstrate GLFW keyboard input with an OSMesa-rendered scene.
 *
 * GLFW features shown
 * -------------------
 * glfwSetKeyCallback()         Register a callback for discrete key events
 *                              (fired on press, release, and key-repeat).
 * glfwGetKey()                 Poll a key's state directly in the main loop
 *                              for smooth, frame-rate-independent motion.
 * glfwSetWindowUserPointer()   Attach application state to a window handle
 *                              so callbacks can retrieve it with
 *                              glfwGetWindowUserPointer().
 * glfwSetWindowTitle()         Update the window title bar at runtime.
 *
 * Key bindings
 * ------------
 *   ESC / Q     — close the window
 *   SPACE       — toggle auto-rotation on/off
 *   R           — reset rotation angle to 0 degrees
 *   LEFT/RIGHT  — manually rotate (only while auto-rotation is off)
 *   UP/DOWN     — increase / decrease rotation speed
 *   +/-         — scale the triangle up / down
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "glfw_osmesa.h"
#include <GLFW/glfw3.h>

/* ---- Application state shared with callbacks ---- */
typedef struct {
    glfw_osmesa_context_t *ctx;
    int   auto_rotate;   /* GLFW_TRUE = rotating automatically */
    float angle;         /* current rotation in degrees        */
    float speed;         /* degrees per second                 */
    float scale;         /* uniform scale factor               */
} app_state_t;

/* Forward declarations */
static void key_callback(GLFWwindow *window, int key, int scancode,
                         int action, int mods);
static void framebuffer_size_callback(GLFWwindow *window, int w, int h);
static void error_callback(int code, const char *desc);
static void draw_frame(int width, int height, float angle, float scale);
static void update_title(GLFWwindow *window, const app_state_t *s);

/* ------------------------------------------------------------------ */
int main(void)
{
    GLFWwindow *window;
    app_state_t state;
    int         fb_width  = 800;
    int         fb_height = 600;
    double      last_time;

    memset(&state, 0, sizeof(state));
    state.auto_rotate = GLFW_TRUE;
    state.angle       = 0.0f;
    state.speed       = 60.0f;  /* 60 degrees/s */
    state.scale       = 1.0f;

    glfwSetErrorCallback(error_callback);

    if (!glfwInit()) {
        fprintf(stderr, "Failed to initialise GLFW\n");
        return EXIT_FAILURE;
    }

    /* No system OpenGL context — OSMesa will do the rendering. */
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE,  GLFW_TRUE);

    window = glfwCreateWindow(fb_width, fb_height,
                              "OSMesa + GLFW — Keyboard Demo", NULL, NULL);
    if (!window) {
        fprintf(stderr, "Failed to create GLFW window\n");
        glfwTerminate();
        return EXIT_FAILURE;
    }

    /* HiDPI: framebuffer may be larger than the requested window size. */
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

    /*
     * glfwSetWindowUserPointer() lets us pass arbitrary application state
     * into callbacks without global variables.  Retrieve it inside a
     * callback with glfwGetWindowUserPointer().
     */
    glfwSetWindowUserPointer(window, &state);
    glfwSetKeyCallback(window, key_callback);
    glfwSetFramebufferSizeCallback(window, framebuffer_size_callback);

    printf("Renderer : %s\n", (const char *)glGetString(GL_RENDERER));
    printf("Version  : %s\n\n", (const char *)glGetString(GL_VERSION));
    printf("Key bindings:\n");
    printf("  ESC / Q    -- close\n");
    printf("  SPACE      -- toggle auto-rotation\n");
    printf("  R          -- reset angle\n");
    printf("  LEFT/RIGHT -- manual rotation (auto-rotation off)\n");
    printf("  UP/DOWN    -- faster / slower\n");
    printf("  +/-        -- scale up / down\n\n");

    update_title(window, &state);
    last_time = glfwGetTime();

    /* ----------------------------------------------------------------
     * Main loop
     * --------------------------------------------------------------- */
    while (!glfwWindowShouldClose(window)) {
        double now   = glfwGetTime();
        double delta = now - last_time;
        last_time    = now;

        if (state.auto_rotate) {
            state.angle += (float)(state.speed * delta);
        } else {
            /*
             * glfwGetKey() — poll the key state directly in the main loop.
             * This produces smooth, frame-rate-proportional motion rather
             * than the coarser steps that key-repeat events alone would give.
             */
            if (glfwGetKey(window, GLFW_KEY_LEFT)  == GLFW_PRESS)
                state.angle -= (float)(state.speed * delta);
            if (glfwGetKey(window, GLFW_KEY_RIGHT) == GLFW_PRESS)
                state.angle += (float)(state.speed * delta);
        }

        glfwGetFramebufferSize(window, &fb_width, &fb_height);
        draw_frame(fb_width, fb_height, state.angle, state.scale);
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
 * key_callback — fired by GLFW on key press, release, and key-repeat.
 *
 * GLFW_PRESS   — use for single-shot actions (toggle, reset).
 * GLFW_REPEAT  — fires automatically when a key is held; use for
 *               incremental adjustments so the value keeps changing.
 * GLFW_RELEASE — not used here but available for release-triggered logic.
 */
static void key_callback(GLFWwindow *window, int key, int scancode,
                         int action, int mods)
{
    app_state_t *s = (app_state_t *)glfwGetWindowUserPointer(window);
    (void)scancode;
    (void)mods;

    if (action == GLFW_RELEASE)
        return;  /* ignore key-up for all bindings in this example */

    switch (key) {
        case GLFW_KEY_ESCAPE:
        case GLFW_KEY_Q:
            glfwSetWindowShouldClose(window, GLFW_TRUE);
            return;

        case GLFW_KEY_SPACE:
            if (action == GLFW_PRESS) {  /* toggle only on the first press */
                s->auto_rotate = !s->auto_rotate;
                printf("Auto-rotation: %s\n", s->auto_rotate ? "ON" : "OFF");
            }
            break;

        case GLFW_KEY_R:
            if (action == GLFW_PRESS) {
                s->angle = 0.0f;
                printf("Angle reset to 0\n");
            }
            break;

        case GLFW_KEY_UP:
            s->speed += 10.0f;
            printf("Speed: %.0f deg/s\n", (double)s->speed);
            break;

        case GLFW_KEY_DOWN:
            s->speed -= 10.0f;
            if (s->speed < 0.0f) s->speed = 0.0f;
            printf("Speed: %.0f deg/s\n", (double)s->speed);
            break;

        case GLFW_KEY_EQUAL:    /* = / + on US layouts */
        case GLFW_KEY_KP_ADD:
            s->scale += 0.1f;
            if (s->scale > 3.0f) s->scale = 3.0f;
            printf("Scale: %.1f\n", (double)s->scale);
            break;

        case GLFW_KEY_MINUS:
        case GLFW_KEY_KP_SUBTRACT:
            s->scale -= 0.1f;
            if (s->scale < 0.1f) s->scale = 0.1f;
            printf("Scale: %.1f\n", (double)s->scale);
            break;

        default:
            break;
    }

    update_title(window, s);
}

/*
 * update_title — demonstrate glfwSetWindowTitle() to reflect live state.
 * Can be called from any main-thread context.
 */
static void update_title(GLFWwindow *window, const app_state_t *s)
{
    char buf[128];
    snprintf(buf, sizeof(buf),
             "Keyboard Demo -- %s | speed %.0f deg/s | scale %.1f",
             s->auto_rotate ? "AUTO" : "MANUAL",
             (double)s->speed, (double)s->scale);
    glfwSetWindowTitle(window, buf);
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
 * Render one frame: rotating, scaled RGB triangle
 * ------------------------------------------------------------------ */
static void draw_frame(int width, int height, float angle, float scale)
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
    glScalef(scale, scale, 1.0f);

    glBegin(GL_TRIANGLES);
        glColor3f(1.0f, 0.0f, 0.0f);  glVertex2f( 0.0f,  0.8f);
        glColor3f(0.0f, 1.0f, 0.0f);  glVertex2f(-0.7f, -0.4f);
        glColor3f(0.0f, 0.0f, 1.0f);  glVertex2f( 0.7f, -0.4f);
    glEnd();
}
