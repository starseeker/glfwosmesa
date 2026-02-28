/*
 * glfw_osmesa.h — Proposed public API for integrating OSMesa (or any
 * user-supplied software rasterizer) with GLFW window management.
 *
 * Problem solved
 * ==============
 * GLFW currently requires a system OpenGL or Vulkan context to be associated
 * with every window it creates.  Some use-cases need only GLFW's window
 * creation and event-dispatch machinery while obtaining rendered images from a
 * *software* rasterizer such as Mesa's Off-Screen rendering extension (OSMesa).
 * This library satisfies that need without touching any system GL/Vulkan stack.
 *
 * How it works
 * ============
 * 1.  A GLFW window is created with  GLFW_CLIENT_API = GLFW_NO_API  so that
 *     GLFW itself requests no graphics context from the OS.
 * 2.  An OSMesa context is created and bound to a CPU-side pixel buffer owned
 *     by this library.
 * 3.  The application renders normally using OpenGL calls (satisfied by OSMesa).
 * 4.  glfwSwapOSMesaBuffers() copies the finished pixel buffer to the native
 *     window surface using only basic OS drawing primitives:
 *       – Linux/X11  : XPutImage()
 *       – Windows    : SetDIBitsToDevice()
 *       – macOS      : CGBitmapContextCreate() / CGContextDrawImage()
 *
 * Proposed upstream API
 * =====================
 * The lowest-level primitive required from GLFW itself is a single new
 * function:
 *
 *   void glfwBlitPixelBuffer(GLFWwindow *window,
 *                            const void *pixels,
 *                            int srcWidth, int srcHeight,
 *                            int format);
 *
 * where format is one of the GLFWOSMESA_FORMAT_* constants below.  Everything
 * else in this header is a convenience wrapper that could live in user-land.
 *
 * Usage
 * =====
 *   glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
 *   GLFWwindow *win = glfwCreateWindow(800, 600, "OSMesa", NULL, NULL);
 *
 *   GLFWosmesaContext ctx = glfwCreateOSMesaContext(win, 800, 600);
 *   glfwMakeOSMesaContextCurrent(ctx);
 *
 *   while (!glfwWindowShouldClose(win)) {
 *       // ... OpenGL rendering calls here ...
 *       glFinish();
 *       glfwSwapOSMesaBuffers(ctx);
 *       glfwPollEvents();
 *   }
 *
 *   glfwDestroyOSMesaContext(ctx);
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
 * Pixel format constants (subset of OSMesa / GL format tokens)
 * ------------------------------------------------------------------------- */

/** RGBA, 8 bits per channel, byte order R-G-B-A in memory. */
#define GLFWOSMESA_FORMAT_RGBA  0x1908  /* GL_RGBA  */

/** BGRA, 8 bits per channel, byte order B-G-R-A in memory. */
#define GLFWOSMESA_FORMAT_BGRA  0x1    /* OSMESA_BGRA */

/** RGB, 8 bits per channel, byte order R-G-B in memory. */
#define GLFWOSMESA_FORMAT_RGB   0x1907  /* GL_RGB   */

/* -------------------------------------------------------------------------
 * Opaque context handle
 * ------------------------------------------------------------------------- */

/** Opaque handle representing an OSMesa rendering context bound to a GLFW
 *  window.  Do not dereference or copy this pointer; treat it like an opaque
 *  object returned by other GLFW factory functions. */
typedef struct GLFWosmesaContext_s *GLFWosmesaContext;

/* -------------------------------------------------------------------------
 * Lifecycle
 * ------------------------------------------------------------------------- */

/**
 * Creates an OSMesa rendering context associated with a GLFW window.
 *
 * The window *must* have been created with the window hint
 *   glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
 * so that GLFW does not attempt to create a system OpenGL/Vulkan context.
 *
 * The function allocates an internal BGRA pixel buffer of size
 * width × height × 4 bytes and creates an OSMesa context backed by that
 * buffer.
 *
 * @param window  GLFW window to present into.
 * @param width   Initial framebuffer width  in pixels.
 * @param height  Initial framebuffer height in pixels.
 * @return        New GLFWosmesaContext on success, NULL on failure.
 *
 * @note  width and height should match the window's current framebuffer
 *        dimensions.  Use glfwGetFramebufferSize() to obtain them.
 */
GLFWosmesaContext glfwCreateOSMesaContext(GLFWwindow *window,
                                          int width, int height);

/**
 * Makes the given OSMesa context current on the calling thread.
 *
 * After this call all OpenGL API calls are routed to the OSMesa software
 * rasterizer and write into the context's internal pixel buffer.
 *
 * @param ctx  Context to make current.  Pass NULL to detach any current
 *             OSMesa context from the calling thread.
 * @return     The underlying OSMesaContext for use with the raw OSMesa API
 *             (e.g.\ OSMesaGetIntegerv), or NULL on failure / when ctx==NULL.
 */
OSMesaContext glfwMakeOSMesaContextCurrent(GLFWosmesaContext ctx);

/**
 * Handles a framebuffer resize event by reallocating the pixel buffer.
 *
 * Call this from a glfwSetFramebufferSizeCallback when using OSMesa so that
 * the internal buffer and OSMesa context track the new window dimensions.
 *
 * @param ctx     Context to resize.
 * @param width   New framebuffer width  in pixels.
 * @param height  New framebuffer height in pixels.
 * @return        GLFW_TRUE on success, GLFW_FALSE on allocation failure.
 */
int glfwResizeOSMesaContext(GLFWosmesaContext ctx, int width, int height);

/**
 * Presents the finished OSMesa frame to the associated GLFW window.
 *
 * This is the OSMesa equivalent of glfwSwapBuffers().  It copies the pixels
 * from the OSMesa back-buffer to the native window surface using only basic
 * OS drawing primitives — no system OpenGL or Vulkan call is made.
 *
 * The caller is responsible for calling glFinish() (or equivalent) before
 * glfwSwapOSMesaBuffers() to ensure all rendering commands have completed.
 *
 * @param ctx  Context whose buffer to present.
 */
void glfwSwapOSMesaBuffers(GLFWosmesaContext ctx);

/**
 * Destroys an OSMesa context and releases all associated resources,
 * including the internal pixel buffer.
 *
 * The context must not be used after this call.
 *
 * @param ctx  Context to destroy.
 */
void glfwDestroyOSMesaContext(GLFWosmesaContext ctx);

/* -------------------------------------------------------------------------
 * Accessors
 * ------------------------------------------------------------------------- */

/**
 * Returns a read-only pointer to the raw pixel buffer.
 *
 * Useful for saving frames to disk, compositing, or feeding a custom
 * display path without going through glfwSwapOSMesaBuffers().
 *
 * The pointer is valid until the next call to glfwResizeOSMesaContext() or
 * glfwDestroyOSMesaContext() for the same context.
 *
 * @param ctx     Context whose buffer to retrieve.
 * @param width   If non-NULL, receives the buffer width  in pixels.
 * @param height  If non-NULL, receives the buffer height in pixels.
 * @param format  If non-NULL, receives the pixel format
 *                (currently always GLFWOSMESA_FORMAT_BGRA).
 * @return        Pointer to the pixel buffer, or NULL if ctx is NULL.
 */
const void *glfwGetOSMesaPixels(GLFWosmesaContext ctx,
                                 int *width, int *height, int *format);

/**
 * Returns the underlying OSMesaContext handle.
 *
 * Use this when you need to call OSMesa-specific functions (e.g.\
 * OSMesaGetIntegerv, OSMesaPixelStore) that are not wrapped by this API.
 *
 * @param ctx  GLFWosmesaContext wrapper.
 * @return     The raw OSMesaContext, or NULL if ctx is NULL.
 */
OSMesaContext glfwGetOSMesaContext(GLFWosmesaContext ctx);

#ifdef __cplusplus
}
#endif

#endif /* GLFW_OSMESA_H */
