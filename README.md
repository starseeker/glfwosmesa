# glfw_osmesa — GLFW + OSMesa integration helper library

A small C helper library that lets [GLFW](https://www.glfw.org/) manage windows
and events while [OSMesa](https://docs.mesa3d.org/osmesa.html) (Mesa's
off-screen software rasteriser) does the rendering — **without touching any
system OpenGL or Vulkan stack**.

## Naming convention

Functions in this library use the **snake\_case** prefix `glfw_osmesa_context_`
to make it explicit that they are **application-side helpers, not proposed
additions to the GLFW public API**.  Official GLFW functions use `glfwCamelCase`
— the different style is intentional.

The **only** item proposed for addition to GLFW upstream is the single generic
blit primitive (documented below).  Everything else lives in user-land.

## Motivation

GLFW normally requires a system OpenGL or Vulkan context for every window it
creates.  Some scenarios need only GLFW's window and event machinery while
rendering comes from a *software* rasteriser:

* Headless / CI environments with no GPU driver.
* Machines where installing GPU drivers is not permitted.
* Unit tests that need deterministic, GPU-independent output.
* Any situation where a user-supplied rasteriser is preferred over the system
  graphics stack.

## How it works

```
+-----------------------------------+
|  Application OpenGL calls         |
+----------------+------------------+
                 |  standard GL API
+----------------v------------------+
|  OSMesa (CPU-side BGRA buffer)    |  <- no GPU, no system GL driver
+----------------+------------------+
                 |  glfw_osmesa_context_swap_buffers()
                 |  (user-land implementation of glfwBlitPixelBuffer)
+----------------v------------------+
|  Native OS blit  (no GL/Vulkan)   |
|  * Linux  : XPutImage (X11)       |
|  * Windows: SetDIBitsToDevice     |
|  * macOS  : CALayer setContents   |
+----------------+------------------+
                 |
+----------------v------------------+
|  GLFW window                      |  <- GLFW_CLIENT_API = GLFW_NO_API
+-----------------------------------+
```

## Proposed upstream GLFW addition

The **only** new function proposed for GLFW itself is a generic pixel-blit
primitive.  It has no knowledge of OSMesa — the application is responsible for
producing the pixel buffer using whatever renderer it chooses:

```c
/*
 * Blit a CPU pixel buffer to a GLFW window using native OS drawing
 * primitives.  The window must have been created with
 * GLFW_CLIENT_API = GLFW_NO_API.
 *
 * @param window    Target GLFW window.
 * @param pixels    Pixel data in the format specified by 'format'.
 * @param srcWidth  Width  of the pixel buffer in pixels.
 * @param srcHeight Height of the pixel buffer in pixels.
 * @param format    GLFW_OSMESA_FORMAT_RGBA / BGRA / RGB (or an equivalent
 *                  token that upstream GLFW would define).
 */
void glfwBlitPixelBuffer(GLFWwindow *window,
                         const void *pixels,
                         int         srcWidth,
                         int         srcHeight,
                         int         format);
```

## Quick-start

### Prerequisites

| Platform       | Packages                                              |
|----------------|-------------------------------------------------------|
| Debian/Ubuntu  | `libglfw3-dev libosmesa6-dev libx11-dev cmake`        |
| Fedora/RHEL    | `glfw-devel mesa-libOSMesa-devel libX11-devel cmake`  |
| Windows        | GLFW + Mesa (build from source or use MSYS2)          |
| macOS          | `brew install glfw mesa`                              |

### Build

```bash
cmake -S . -B build
cmake --build build
```

### Run the example

```bash
./build/triangle
```

Opens an 800 x 600 window with a rotating RGB triangle rendered entirely by
OSMesa.  No GPU or system OpenGL driver is used.

## API reference

See [`include/glfw_osmesa.h`](include/glfw_osmesa.h) for the full documented
API.

| Function (user-land helper — NOT GLFW API)              | Description                              |
|---------------------------------------------------------|------------------------------------------|
| `glfw_osmesa_context_create(win, w, h)`                 | Create OSMesa context + pixel buffer     |
| `glfw_osmesa_context_make_current(ctx)`                 | Route GL calls to software rasteriser    |
| `glfw_osmesa_context_resize(ctx, w, h)`                 | Reallocate buffer after window resize    |
| `glfw_osmesa_context_swap_buffers(ctx)`                 | Blit rendered image to the native window |
| `glfw_osmesa_context_destroy(ctx)`                      | Release all resources                    |
| `glfw_osmesa_context_get_pixels(ctx, w, h, fmt)`        | Access the raw pixel buffer              |
| `glfw_osmesa_context_get_raw(ctx)`                      | Get the underlying `OSMesaContext`       |

The opaque handle type is `glfw_osmesa_context_t *` (snake\_case `_t` suffix,
not a GLFW-style typedef).

## Typical usage

```c
/* 1. Create a GLFW window with no system GL context. */
glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
GLFWwindow *win = glfwCreateWindow(800, 600, "My App", NULL, NULL);

/* 2. Set up the OSMesa helper (user-land, not GLFW API). */
int w, h;
glfwGetFramebufferSize(win, &w, &h);
glfw_osmesa_context_t *ctx = glfw_osmesa_context_create(win, w, h);
glfw_osmesa_context_make_current(ctx);

/* 3. Handle resize via the standard GLFW callback. */
glfwSetFramebufferSizeCallback(win, my_resize_cb);
/* In my_resize_cb: glfw_osmesa_context_resize(ctx, new_w, new_h); */

/* 4. Main loop — use standard OpenGL calls. */
while (!glfwWindowShouldClose(win)) {
    glClear(GL_COLOR_BUFFER_BIT);
    /* ... render ... */
    glFinish();
    glfw_osmesa_context_swap_buffers(ctx);  /* <- user-land glfwBlitPixelBuffer */
    glfwPollEvents();
}

/* 5. Clean up. */
glfw_osmesa_context_destroy(ctx);
glfwDestroyWindow(win);
glfwTerminate();
```

## Platform notes

* **Byte order**: `OSMESA_BGRA` pixel format is used throughout.  On
  little-endian (x86/x86-64) this matches the native pixel layout on all
  three supported platforms, so no per-pixel colour conversion is needed.
* **Y-axis**: `OSMESA_Y_UP = 0` is set so row 0 is at the top of the image,
  consistent with the top-down blit used on all platforms.
* **Thread safety**: All functions must be called from the main thread — the
  same constraint that GLFW itself imposes.

## License

MIT — see source file headers.
