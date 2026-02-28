/*
 * glfw_osmesa.h — User-land helper library for using OSMesa (or any
 * CPU-side rasteriser) with a GLFW GLFW_NO_API window.
 *
 * NAMING CONVENTION
 * =================
 * Functions here use the snake_case prefix  glfw_osmesa_  to make it
 * explicit that they are application-side helpers, NOT proposed additions
 * to the upstream GLFW library.
 *
 * The only item proposed for upstream GLFW is the single generic primitive:
 *
 *   void glfwBlitPixelBuffer(GLFWwindow *window,
 *                            const void *pixels,
 *                            int         width,
 *                            int         height,
 *                            int         format);
 *
 * Everything else in this file lives in user-land and is named accordingly.
 *
 * HOW IT WORKS
 * ============
 * 1.  Application creates a GLFW window with GLFW_CLIENT_API = GLFW_NO_API.
 * 2.  Application calls glfw_osmesa_context_create() to get a context handle
 *     that owns an OSMesa context + a BGRA pixel buffer.
 * 3.  Application renders using standard OpenGL calls (satisfied by OSMesa).
 * 4.  glfw_osmesa_context_swap_buffers() calls glfwBlitPixelBuffer() which
 *     copies the finished pixels to the native window using only basic OS
 *     drawing primitives:
 *       - Linux/X11  : XPutImage()
 *       - Windows    : SetDIBitsToDevice()
 *       - macOS      : CALayer setContents + CGImage
 *
 * TYPICAL USAGE
 * =============
 *   glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
 *   GLFWwindow *win = glfwCreateWindow(800, 600, "OSMesa", NULL, NULL);
 *
 *   int w, h;
 *   glfwGetFramebufferSize(win, &w, &h);
 *   glfw_osmesa_context_t *ctx = glfw_osmesa_context_create(win, w, h);
 *   glfw_osmesa_context_make_current(ctx);
 *
 *   while (!glfwWindowShouldClose(win)) {
 *       // ... OpenGL rendering calls here ...
 *       glFinish();
 *       glfw_osmesa_context_swap_buffers(ctx);
 *       glfwPollEvents();
 *   }
 *
 *   glfw_osmesa_context_destroy(ctx);
 *   glfwDestroyWindow(win);
 */

#ifndef GLFW_OSMESA_H
#define GLFW_OSMESA_H

#include <GLFW/glfw3.h>
#include <GL/osmesa.h>

#ifdef __cplusplus
extern "C" {
#endif

/* -------------------------------------------------------------------------
 * Pixel format constants — used with glfwBlitPixelBuffer (the proposed
 * upstream GLFW primitive).  Named with the GLFW_OSMESA_ prefix since they
 * bridge the two libraries; they do NOT imply these constants would ship
 * inside GLFW itself (upstream would use its own token names).
 * ------------------------------------------------------------------------- */

/** RGBA, 8 bits per channel, byte order R-G-B-A in memory. */
#define GLFW_OSMESA_FORMAT_RGBA  0x1908  /* GL_RGBA  */

/** BGRA, 8 bits per channel, byte order B-G-R-A in memory.
 *  OSMESA_BGRA = 0x1 (Mesa-specific value, not the same as GL_BGRA 0x80E1). */
#define GLFW_OSMESA_FORMAT_BGRA  0x1

/** RGB, 8 bits per channel, byte order R-G-B in memory. */
#define GLFW_OSMESA_FORMAT_RGB   0x1907  /* GL_RGB   */

/* -------------------------------------------------------------------------
 * Opaque context type
 *
 * The snake_case name and _t suffix signal that this is a user-land type,
 * not an official GLFW typedef (which would be GLFWsomething).
 * ------------------------------------------------------------------------- */

/**
 * Opaque handle representing an OSMesa context bound to a GLFW window.
 *
 * This typedef names the struct itself (not the pointer), following the same
 * convention used by GLFW (GLFWwindow, GLFWmonitor, etc.).  All function
 * signatures therefore use an explicit  glfw_osmesa_context_t *  pointer.
 */
typedef struct glfw_osmesa_context_s glfw_osmesa_context_t;

/* -------------------------------------------------------------------------
 * Lifecycle
 * ------------------------------------------------------------------------- */

/**
 * Creates an OSMesa rendering context associated with a GLFW window.
 *
 * The window MUST have been created with:
 *   glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
 *
 * Allocates an internal BGRA pixel buffer of width x height x 4 bytes and
 * creates an OSMesa context backed by that buffer.
 *
 * @param window  GLFW window to present into.
 * @param width   Initial framebuffer width  in pixels.
 * @param height  Initial framebuffer height in pixels.
 * @return        New context on success, NULL on failure.
 *
 * @note  Pass the values from glfwGetFramebufferSize(), not from
 *        glfwGetWindowSize(), so HiDPI displays are handled correctly.
 */
glfw_osmesa_context_t *glfw_osmesa_context_create(GLFWwindow *window,
                                                   int         width,
                                                   int         height);

/**
 * Makes the context current on the calling thread.
 *
 * After this call all OpenGL API calls are routed to the OSMesa software
 * rasteriser and write into the context's internal BGRA pixel buffer.
 *
 * @param ctx  Context to make current.  Pass NULL to detach.
 * @return     The underlying OSMesaContext (for raw OSMesa calls such as
 *             OSMesaGetIntegerv), or NULL on failure / when ctx is NULL.
 */
OSMesaContext glfw_osmesa_context_make_current(glfw_osmesa_context_t *ctx);

/**
 * Reallocates the pixel buffer after a framebuffer resize.
 *
 * Call this from a glfwSetFramebufferSizeCallback:
 *   glfw_osmesa_context_resize(ctx, new_width, new_height);
 *
 * @param ctx     Context to resize.
 * @param width   New framebuffer width  in pixels.
 * @param height  New framebuffer height in pixels.
 * @return        GLFW_TRUE on success, GLFW_FALSE on allocation failure.
 */
int glfw_osmesa_context_resize(glfw_osmesa_context_t *ctx,
                                int width, int height);

/**
 * Presents the finished frame to the associated GLFW window.
 *
 * Equivalent to glfwSwapBuffers() for a system-GL context.  Internally this
 * is the user-land implementation of what glfwBlitPixelBuffer() would do
 * once added to GLFW upstream.
 *
 * The caller must call glFinish() before this function to ensure all
 * rendering commands have completed.
 *
 * @param ctx  Context whose pixel buffer to present.
 */
void glfw_osmesa_context_swap_buffers(glfw_osmesa_context_t *ctx);

/**
 * Destroys the context and releases all associated resources, including
 * the pixel buffer.  The handle must not be used after this call.
 *
 * @param ctx  Context to destroy.
 */
void glfw_osmesa_context_destroy(glfw_osmesa_context_t *ctx);

/* -------------------------------------------------------------------------
 * Accessors
 * ------------------------------------------------------------------------- */

/**
 * Returns a read-only pointer to the raw pixel buffer.
 *
 * Useful for saving frames to disk, compositing, or feeding a custom display
 * path without going through glfw_osmesa_context_swap_buffers().
 *
 * The pointer is valid until the next glfw_osmesa_context_resize() or
 * glfw_osmesa_context_destroy() call on the same context.
 *
 * @param ctx     Context whose buffer to retrieve.
 * @param width   If non-NULL, receives the buffer width  in pixels.
 * @param height  If non-NULL, receives the buffer height in pixels.
 * @param format  If non-NULL, receives the pixel format
 *                (currently always GLFW_OSMESA_FORMAT_BGRA).
 * @return        Pointer to the pixel data, or NULL if ctx is NULL.
 */
const void *glfw_osmesa_context_get_pixels(glfw_osmesa_context_t *ctx,
                                            int *width,
                                            int *height,
                                            int *format);

/**
 * Returns the underlying OSMesaContext handle.
 *
 * Use this when you need to call raw OSMesa functions (e.g. OSMesaGetIntegerv,
 * OSMesaPixelStore) that are not wrapped by this helper API.
 *
 * @param ctx  Helper context.
 * @return     The raw OSMesaContext, or NULL if ctx is NULL.
 */
OSMesaContext glfw_osmesa_context_get_raw(glfw_osmesa_context_t *ctx);

#ifdef __cplusplus
}
#endif

#endif /* GLFW_OSMESA_H */
