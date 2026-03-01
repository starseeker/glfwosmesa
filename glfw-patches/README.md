# GLFW patches

This directory contains patches intended for submission to the upstream
[GLFW](https://www.glfw.org/) project, or for local application if upstream
declines to merge them.

---

## 0001-Add-glfwBlitPixelBuffer.patch

**Applies to:** GLFW 3.3.10 source tree  
**Status:** Proposal — not yet submitted upstream

### What it does

Adds a single new public function to GLFW:

```c
void glfwBlitPixelBuffer(GLFWwindow* window,
                          const void* pixels,
                          int         srcWidth,
                          int         srcHeight,
                          int         format);
```

and three pixel-format constants (`GLFW_PIXEL_FORMAT_RGBA`,
`GLFW_PIXEL_FORMAT_BGRA`, `GLFW_PIXEL_FORMAT_RGB`).

`glfwBlitPixelBuffer` copies a CPU-side pixel buffer to a
`GLFW_CLIENT_API = GLFW_NO_API` window using only the native OS drawing
layer — **no GPU, no OpenGL, no Vulkan**.

| Platform | Implementation         |
|----------|------------------------|
| Linux/X11 | `XCreateImage` + `XPutImage` (Xlib) |
| Windows   | `SetDIBitsToDevice` (GDI) |
| macOS     | `CGImageCreate` + `CALayer setContents:` (CoreGraphics) |
| Wayland   | Not yet implemented (returns `GLFW_PLATFORM_ERROR`) |

### Files changed by the patch

| File | Change |
|------|--------|
| `include/GLFW/glfw3.h` | Add `GLFW_PIXEL_FORMAT_*` constants and `glfwBlitPixelBuffer` declaration |
| `src/blit.c` | New file — cross-platform implementation |
| `src/CMakeLists.txt` | Add `blit.c` to the common source list |

### How to apply

```bash
# From inside your GLFW 3.3.10 source directory:
patch -p1 < /path/to/glfw-patches/0001-Add-glfwBlitPixelBuffer.patch
```

Or with git:

```bash
git apply /path/to/glfw-patches/0001-Add-glfwBlitPixelBuffer.patch
```

### How to verify (dry-run)

```bash
patch -p1 --dry-run < /path/to/glfw-patches/0001-Add-glfwBlitPixelBuffer.patch
```

### Relation to this repository

`glfw_osmesa_context_swap_buffers()` in `src/glfw_osmesa.c` automatically
uses `glfwBlitPixelBuffer()` when it detects the patched GLFW (via
`#ifdef GLFW_PIXEL_FORMAT_BGRA`), and falls back to its own inline
platform implementation otherwise.  With the patched GLFW installed, the
inline fallback becomes dead code.
