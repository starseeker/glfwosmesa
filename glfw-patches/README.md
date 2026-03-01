# GLFW patches

This directory contains patches intended for submission to the upstream
[GLFW](https://www.glfw.org/) project, or for local application if upstream
declines to merge them.  Both patches are **independent alternatives**; apply
one or the other to a pristine GLFW 3.3.10 source tree.

---

## Which patch to use?

| Patch | Form | When to use |
|-------|------|-------------|
| `0001` | Clarity form | Reading, reviewing, or proposing the feature; all platform logic is in one self-contained file |
| `0002` | Standard GLFW pattern | "Final form" that mirrors how every other window function is structured in GLFW |

The public API (`include/GLFW/glfw3.h`) is **identical** in both patches.

---

## Shared public API (both patches)

Both patches add these to `include/GLFW/glfw3.h`:

```c
/* Pixel-format constants */
#define GLFW_PIXEL_FORMAT_RGBA    0x00037001
#define GLFW_PIXEL_FORMAT_BGRA    0x00037002
#define GLFW_PIXEL_FORMAT_RGB     0x00037003

/* New function */
void glfwBlitPixelBuffer(GLFWwindow* window,
                          const void* pixels,
                          int         srcWidth,
                          int         srcHeight,
                          int         format);
```

`glfwBlitPixelBuffer` copies a CPU-side pixel buffer to a
`GLFW_CLIENT_API = GLFW_NO_API` window using only the native OS drawing
primitives — **no GPU, no OpenGL, no Vulkan**.

| Platform  | Implementation                              |
|-----------|---------------------------------------------|
| Linux/X11 | `XCreateImage` + `XPutImage` (Xlib)        |
| Windows   | `SetDIBitsToDevice` (GDI)                  |
| macOS     | `CGImageCreate` + `CALayer setContents:` (CoreGraphics) |
| Wayland   | Stub — returns `GLFW_PLATFORM_ERROR`        |

---

## How to apply either patch

```bash
# Dry-run first (safe, no changes made):
patch -p1 --dry-run -d <glfw-source-dir> < 0001-Add-glfwBlitPixelBuffer.patch

# Apply for real:
patch -p1 -d <glfw-source-dir> < 0001-Add-glfwBlitPixelBuffer.patch

# Or with git apply:
git -C <glfw-source-dir> apply 0001-Add-glfwBlitPixelBuffer.patch
```

Replace `0001` with `0002` for the platform-pattern variant.

---

## 0001-Add-glfwBlitPixelBuffer.patch — Clarity form

**Files changed:**

| File | Change |
|------|--------|
| `include/GLFW/glfw3.h` | `GLFW_PIXEL_FORMAT_*` constants + `glfwBlitPixelBuffer` declaration |
| `src/blit.c` | **New file** — all platform logic in one place (`#if defined(_GLFW_X11)` / `_GLFW_WIN32` / `_GLFW_COCOA` / …) |
| `src/CMakeLists.txt` | Add `blit.c` to `common_SOURCES` |

### Design

`blit.c` contains the public `glfwBlitPixelBuffer()` function plus the
per-platform implementations inside a single `#if ... #elif ... #endif`
chain.  A shared `swapRB()` helper (file-local) converts RGBA→BGRA for
platforms that need it.

This form is intentionally readable: a reviewer sees the whole feature in
one file without having to jump between several source files.

---

## 0002-Add-glfwBlitPixelBuffer-platform-pattern.patch — Standard GLFW pattern

**Files changed:**

| File | Change |
|------|--------|
| `include/GLFW/glfw3.h` | Same as 0001 |
| `src/internal.h` | Declare `_glfwPlatformBlitPixelBuffer()` + `_glfwSwapRB()` |
| `src/blit.c` | **New file** — thin public API: validates args, defines `_glfwSwapRB`, calls `_glfwPlatformBlitPixelBuffer()` |
| `src/x11_window.c` | Add `_glfwPlatformBlitPixelBuffer()` implementation |
| `src/win32_window.c` | Add `_glfwPlatformBlitPixelBuffer()` implementation |
| `src/cocoa_window.m` | Add `_glfwPlatformBlitPixelBuffer()` implementation |
| `src/wl_window.c` | Add `_glfwPlatformBlitPixelBuffer()` stub |
| `src/null_window.c` | Add `_glfwPlatformBlitPixelBuffer()` stub |
| `src/CMakeLists.txt` | Add `blit.c` to `common_SOURCES` |

### Design

This mirrors the exact pattern GLFW uses for every other window operation
(e.g. `glfwSwapBuffers` → `_glfwPlatformSwapBuffers` in each platform
file).

```
glfwBlitPixelBuffer()          ← public API in src/blit.c
  validates arguments
  calls _glfwPlatformBlitPixelBuffer()
                                ← implemented per-platform:
                                   src/x11_window.c
                                   src/win32_window.c
                                   src/cocoa_window.m
                                   src/wl_window.c   (stub)
                                   src/null_window.c (stub)
```

`_glfwSwapRB()` (declared in `internal.h`, defined in `blit.c`) is the
shared R↔B channel-swap helper called by each platform implementation when
the caller supplies `GLFW_PIXEL_FORMAT_RGBA` but the native API needs BGRA.

---

## Relation to this repository

`glfw_osmesa_context_swap_buffers()` in `src/glfw_osmesa.c` detects the
patched GLFW at compile time via `#ifdef GLFW_PIXEL_FORMAT_BGRA` and
delegates to `glfwBlitPixelBuffer()` when available.  Either patch
satisfies this check.
