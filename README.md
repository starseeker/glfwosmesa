# glfw_osmesa — GLFW + OSMesa integration library

A small C library that lets [GLFW](https://www.glfw.org/) manage windows and
events while [OSMesa](https://docs.mesa3d.org/osmesa.html) (Mesa's off-screen
software rasteriser) does the rendering — **without touching any system
OpenGL or Vulkan stack**.

## Motivation

GLFW normally requires a system OpenGL or Vulkan context to be associated with
every window it creates.  Some scenarios need only GLFW's window and
event-dispatch machinery while obtaining rendered images from a *software*
rasteriser:

* Headless / CI rendering environments that have no GPU driver.
* Applications that must run portably on machines where installing GPU drivers
  is not permitted or not possible.
* Unit tests that exercise GL rendering code without a display server.
* Any use-case where predictable, GPU-independent output is required.

This library satisfies those needs by bridging the gap between GLFW's
`GLFW_NO_API` window mode and OSMesa's CPU-side pixel buffer.

## How it works

```
+---------------------------------+
|  Application OpenGL calls       |
+---------------+-----------------+
                |  standard GL API
+---------------v-----------------+
|  OSMesa (Mesa software renderer)|  <- no GPU, no system GL driver
|  renders into a BGRA CPU buffer |
+---------------+-----------------+
                |  glfwSwapOSMesaBuffers()
+---------------v-----------------+
|  Native OS blit (no GL/Vulkan)  |
|  * Linux  : XPutImage (X11)     |
|  * Windows: SetDIBitsToDevice   |
|  * macOS  : CGContextDrawImage  |
+---------------+-----------------+
                |
+---------------v-----------------+
|  GLFW window                    |  <- GLFW_CLIENT_API = GLFW_NO_API
+---------------------------------+
```

## Proposed upstream GLFW API

The minimum addition required from GLFW itself is one new function:

```c
/*
 * Blit a CPU pixel buffer to a GLFW window using native OS drawing
 * primitives only.  The window must have been created with
 * GLFW_CLIENT_API = GLFW_NO_API.
 *
 * @param window    Target GLFW window.
 * @param pixels    Pixel data (format determined by the 'format' argument).
 * @param srcWidth  Width  of the pixel buffer in pixels.
 * @param srcHeight Height of the pixel buffer in pixels.
 * @param format    One of GLFWOSMESA_FORMAT_RGBA/BGRA/RGB.
 */
void glfwBlitPixelBuffer(GLFWwindow *window,
                         const void *pixels,
                         int srcWidth, int srcHeight,
                         int format);
```

Everything else in `include/glfw_osmesa.h` is a convenience wrapper that
could live in user-land (as this library does) without any upstream changes.

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

This opens an 800 x 600 window showing a rotating RGB triangle rendered
entirely by OSMesa.  No GPU or system OpenGL driver is used.

## API reference

See [`include/glfw_osmesa.h`](include/glfw_osmesa.h) for the full,
documented public API.  Summary:

| Function                             | Description                              |
|--------------------------------------|------------------------------------------|
| `glfwCreateOSMesaContext(win, w, h)` | Create context + pixel buffer            |
| `glfwMakeOSMesaContextCurrent(ctx)`  | Make OSMesa context current for rendering|
| `glfwResizeOSMesaContext(ctx, w, h)` | Reallocate buffer after window resize    |
| `glfwSwapOSMesaBuffers(ctx)`         | Blit rendered image to the native window |
| `glfwDestroyOSMesaContext(ctx)`      | Release all resources                    |
| `glfwGetOSMesaPixels(ctx, ...)`      | Access the raw pixel buffer              |
| `glfwGetOSMesaContext(ctx)`          | Retrieve the underlying `OSMesaContext`  |

## Typical usage pattern

```c
/* 1. Create a GLFW window with no system GL context. */
glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
GLFWwindow *win = glfwCreateWindow(800, 600, "My App", NULL, NULL);

/* 2. Create the OSMesa context bound to that window. */
int w, h;
glfwGetFramebufferSize(win, &w, &h);
GLFWosmesaContext ctx = glfwCreateOSMesaContext(win, w, h);
glfwMakeOSMesaContextCurrent(ctx);

/* 3. Register the resize callback. */
glfwSetFramebufferSizeCallback(win, my_resize_callback);
/*    Inside callback: glfwResizeOSMesaContext(ctx, new_w, new_h); */

/* 4. Main loop -- standard OpenGL here. */
while (!glfwWindowShouldClose(win)) {
    glClear(GL_COLOR_BUFFER_BIT);
    /* ... draw ... */
    glFinish();
    glfwSwapOSMesaBuffers(ctx);   /* <- the key call */
    glfwPollEvents();
}

/* 5. Clean up. */
glfwDestroyOSMesaContext(ctx);
glfwDestroyWindow(win);
glfwTerminate();
```

## Platform notes

* **Byte order**: The library uses `OSMESA_BGRA` pixel format.  On
  little-endian (x86/x86-64) hosts this matches the native pixel layout
  expected by all three blit paths, so no per-pixel conversion is needed.
* **Y-axis**: OSMesa's `OSMESA_Y_UP` parameter is set to `0` so row 0 is at
  the top of the image, consistent with the top-down blit used on all
  platforms.
* **Thread safety**: All functions must be called from the main thread, the
  same constraint that GLFW itself imposes.

## License

MIT — see source file headers.
