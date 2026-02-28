/*
 * glfw_osmesa.c — Cross-platform implementation of the glfw_osmesa API.
 *
 * Platform pixel-blit strategy
 * =============================
 * Linux / X11    — XCreateImage() + XPutImage()
 * Windows        — SetDIBitsToDevice() via GDI
 * macOS          — CGBitmapContextCreate() + CGContextDrawImage()
 *
 * All three paths receive a BGRA (8-bit-per-channel) buffer produced by
 * OSMesa.  On little-endian x86/x86-64 this byte order matches the native
 * pixel format used by each display system, so no per-pixel colour conversion
 * is needed.
 */

/* ------------------------------------------------------------------
 * Platform detection and native handle exposure
 * ------------------------------------------------------------------ */
#if defined(_WIN32)
#  define NOMINMAX
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
#  define GLFW_EXPOSE_NATIVE_WIN32
#elif defined(__APPLE__)
#  include <CoreGraphics/CoreGraphics.h>
#  include <objc/objc.h>
#  include <objc/message.h>
#  define GLFW_EXPOSE_NATIVE_COCOA
#else
   /* Assume X11 on all other POSIX platforms */
#  include <X11/Xlib.h>
#  include <X11/Xutil.h>
#  define GLFW_EXPOSE_NATIVE_X11
#endif

#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>

#include <GL/osmesa.h>
#include "glfw_osmesa.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* ------------------------------------------------------------------
 * Internal context structure
 * ------------------------------------------------------------------ */

struct GLFWosmesaContext_s {
    GLFWwindow    *window;
    OSMesaContext  osmesa;
    unsigned char *buffer;   /* BGRA pixel buffer owned by this struct */
    int            width;
    int            height;

#if defined(_WIN32)
    HWND           hwnd;
    BITMAPINFO     bmi;      /* DIB header, recomputed on resize         */

#elif defined(__APPLE__)
    id             nswindow; /* NSWindow* stored as id                   */
    CGColorSpaceRef colorspace;

#else /* X11 */
    Display       *display;
    Window         xwindow;
    GC             gc;
    int            xdepth;
    Visual        *visual;
#endif
};

/* ------------------------------------------------------------------
 * Helper: allocate / reallocate the pixel buffer and rebind OSMesa
 * ------------------------------------------------------------------ */
static int _rebind_buffer(GLFWosmesaContext ctx, int width, int height)
{
    unsigned char *buf;

    if (width <= 0 || height <= 0)
        return GLFW_FALSE;

    buf = (unsigned char *)malloc((size_t)width * (size_t)height * 4u);
    if (!buf) {
        fprintf(stderr, "glfw_osmesa: out of memory (%dx%d)\n", width, height);
        return GLFW_FALSE;
    }

    /* Zero-initialize so uninitialised pixels show as black not garbage. */
    memset(buf, 0, (size_t)width * (size_t)height * 4u);

    if (!OSMesaMakeCurrent(ctx->osmesa, buf, GL_UNSIGNED_BYTE, width, height)) {
        free(buf);
        fprintf(stderr, "glfw_osmesa: OSMesaMakeCurrent failed\n");
        return GLFW_FALSE;
    }

    free(ctx->buffer);
    ctx->buffer = buf;
    ctx->width  = width;
    ctx->height = height;
    return GLFW_TRUE;
}

/* ------------------------------------------------------------------
 * Platform-specific initialisation helpers
 * ------------------------------------------------------------------ */
#if defined(_WIN32)

static int _platform_init(GLFWosmesaContext ctx)
{
    ctx->hwnd = glfwGetWin32Window(ctx->window);
    if (!ctx->hwnd) return GLFW_FALSE;
    return GLFW_TRUE;
}

static void _platform_update_bmi(GLFWosmesaContext ctx)
{
    BITMAPINFOHEADER *h = &ctx->bmi.bmiHeader;
    memset(&ctx->bmi, 0, sizeof(ctx->bmi));
    h->biSize        = sizeof(BITMAPINFOHEADER);
    h->biWidth       = ctx->width;
    /* Negative height: top-down DIB so row 0 is at the top of the window. */
    h->biHeight      = -ctx->height;
    h->biPlanes      = 1;
    h->biBitCount    = 32;
    h->biCompression = BI_RGB;
}

static void _platform_blit(GLFWosmesaContext ctx)
{
    HDC hdc;
    _platform_update_bmi(ctx);
    hdc = GetDC(ctx->hwnd);
    if (!hdc) return;
    SetDIBitsToDevice(hdc,
                      0, 0,
                      (DWORD)ctx->width, (DWORD)ctx->height,
                      0, 0,
                      0, (UINT)ctx->height,
                      ctx->buffer,
                      &ctx->bmi,
                      DIB_RGB_COLORS);
    ReleaseDC(ctx->hwnd, hdc);
}

static void _platform_destroy(GLFWosmesaContext ctx)
{
    (void)ctx; /* nothing to free on Windows */
}

/* ---------------------------------------------------------------------- */
#elif defined(__APPLE__)

static int _platform_init(GLFWosmesaContext ctx)
{
    ctx->nswindow   = (id)glfwGetCocoaWindow(ctx->window);
    ctx->colorspace = CGColorSpaceCreateDeviceRGB();
    if (!ctx->nswindow || !ctx->colorspace) return GLFW_FALSE;
    return GLFW_TRUE;
}

/*
 * Draw the BGRA pixel buffer into the NSWindow's content view using
 * CoreGraphics — no OpenGL involved.
 *
 * We set the CGImage directly on the contentView's CALayer via
 * [layer setContents: image].  This avoids the NSGraphicsContext
 * wrapper entirely and works reliably from a C context.
 */
static void _platform_blit(GLFWosmesaContext ctx)
{
    CGDataProviderRef provider;
    CGImageRef        image;
    id                contentView;
    id                layer;

    /* Build a CGImage backed by our pixel buffer (zero-copy reference). */
    provider = CGDataProviderCreateWithData(NULL, ctx->buffer,
                                            (size_t)ctx->width *
                                            (size_t)ctx->height * 4u,
                                            NULL);
    if (!provider) return;

    /*
     * kCGBitmapByteOrder32Little | kCGImageAlphaNoneSkipFirst:
     * Interprets each 32-bit word as xRGB on little-endian, which maps
     * the BGRA memory layout (B at byte 0 … A at byte 3) correctly to
     * display-native BGRX.
     */
    image = CGImageCreate((size_t)ctx->width, (size_t)ctx->height,
                          8, 32,
                          (size_t)ctx->width * 4u,
                          ctx->colorspace,
                          kCGBitmapByteOrder32Little |
                              kCGImageAlphaNoneSkipFirst,
                          provider, NULL, false,
                          kCGRenderingIntentDefault);
    CGDataProviderRelease(provider);
    if (!image) return;

    /*
     * Obtain the CALayer of the NSWindow's content view and set the
     * CGImage as its contents.  GLFW already enables wantsLayer on
     * the content view, so the layer always exists.
     *
     * [layer setContents: (id)image] uses the CGImageRef directly;
     * CALayer accepts a CGImageRef cast to id as its contents value.
     */
    contentView = ((id (*)(id, SEL))objc_msgSend)(
        ctx->nswindow,
        sel_registerName("contentView"));

    layer = ((id (*)(id, SEL))objc_msgSend)(
        contentView,
        sel_registerName("layer"));

    if (layer) {
        ((void (*)(id, SEL, id))objc_msgSend)(
            layer,
            sel_registerName("setContents:"),
            (id)image);
    }

    CGImageRelease(image);
}

static void _platform_destroy(GLFWosmesaContext ctx)
{
    if (ctx->colorspace)
        CGColorSpaceRelease(ctx->colorspace);
}

/* ---------------------------------------------------------------------- */
#else /* X11 */

static int _platform_init(GLFWosmesaContext ctx)
{
    XWindowAttributes attrs;

    ctx->display = glfwGetX11Display();
    ctx->xwindow = glfwGetX11Window(ctx->window);
    if (!ctx->display || !ctx->xwindow) return GLFW_FALSE;

    XGetWindowAttributes(ctx->display, ctx->xwindow, &attrs);
    ctx->visual = attrs.visual;
    ctx->xdepth = attrs.depth;

    ctx->gc = XCreateGC(ctx->display, ctx->xwindow, 0, NULL);
    if (!ctx->gc) return GLFW_FALSE;

    return GLFW_TRUE;
}

/*
 * Blit the BGRA pixel buffer into the X11 window via XPutImage.
 *
 * On little-endian (x86/x86-64) systems with a 32-bit TrueColor visual the
 * BGRA byte order matches the native pixel format, so no per-pixel conversion
 * is required.  On big-endian or 24-bit-depth displays a byte-swap step would
 * need to be inserted here before calling XPutImage.
 */
static void _platform_blit(GLFWosmesaContext ctx)
{
    char   *data;
    XImage *img;

    /* XDestroyImage will call free() on img->data, so we pass a copy. */
    data = (char *)malloc((size_t)ctx->width * (size_t)ctx->height * 4u);
    if (!data) return;
    memcpy(data, ctx->buffer, (size_t)ctx->width * (size_t)ctx->height * 4u);

    img = XCreateImage(ctx->display,
                       ctx->visual,
                       (unsigned int)ctx->xdepth,
                       ZPixmap,
                       0,
                       data,
                       (unsigned int)ctx->width,
                       (unsigned int)ctx->height,
                       32,   /* bitmap_pad: X11 pads each scanline to 32 bits */
                       0);   /* bytes_per_line: 0 = auto-compute              */
    if (!img) {
        free(data);
        return;
    }

    XPutImage(ctx->display, ctx->xwindow, ctx->gc,
              img, 0, 0, 0, 0,
              (unsigned int)ctx->width,
              (unsigned int)ctx->height);
    XFlush(ctx->display);

    /* XDestroyImage frees both the XImage struct and img->data. */
    XDestroyImage(img);
}

static void _platform_destroy(GLFWosmesaContext ctx)
{
    if (ctx->gc)
        XFreeGC(ctx->display, ctx->gc);
}

#endif /* platform */

/* ==================================================================
 * Public API implementation
 * ================================================================== */

GLFWosmesaContext glfwCreateOSMesaContext(GLFWwindow *window,
                                          int width, int height)
{
    GLFWosmesaContext ctx;
    const int attribs[] = {
        OSMESA_FORMAT,                OSMESA_BGRA,
        OSMESA_DEPTH_BITS,            24,
        OSMESA_STENCIL_BITS,          8,
        OSMESA_CONTEXT_MAJOR_VERSION, 2,
        OSMESA_CONTEXT_MINOR_VERSION, 0,
        0  /* terminate */
    };

    if (!window || width <= 0 || height <= 0) {
        fprintf(stderr, "glfw_osmesa: invalid parameters to "
                        "glfwCreateOSMesaContext\n");
        return NULL;
    }

    ctx = (GLFWosmesaContext)calloc(1, sizeof(struct GLFWosmesaContext_s));
    if (!ctx) return NULL;

    ctx->window = window;

    ctx->osmesa = OSMesaCreateContextAttribs(attribs, NULL);
    if (!ctx->osmesa) {
        fprintf(stderr, "glfw_osmesa: OSMesaCreateContextAttribs failed\n");
        free(ctx);
        return NULL;
    }

    if (!_rebind_buffer(ctx, width, height)) {
        OSMesaDestroyContext(ctx->osmesa);
        free(ctx);
        return NULL;
    }

    if (!_platform_init(ctx)) {
        fprintf(stderr, "glfw_osmesa: platform initialisation failed\n");
        free(ctx->buffer);
        OSMesaDestroyContext(ctx->osmesa);
        free(ctx);
        return NULL;
    }

    return ctx;
}

OSMesaContext glfwMakeOSMesaContextCurrent(GLFWosmesaContext ctx)
{
    if (!ctx) {
        OSMesaMakeCurrent(NULL, NULL, GL_UNSIGNED_BYTE, 0, 0);
        return NULL;
    }

    if (!OSMesaMakeCurrent(ctx->osmesa, ctx->buffer,
                           GL_UNSIGNED_BYTE, ctx->width, ctx->height)) {
        fprintf(stderr, "glfw_osmesa: OSMesaMakeCurrent failed\n");
        return NULL;
    }

    /*
     * Tell OSMesa that row 0 is at the *top* of the buffer (Y-down), matching
     * the convention used by all three platform blit paths above.
     */
    OSMesaPixelStore(OSMESA_Y_UP, 0);

    return ctx->osmesa;
}

int glfwResizeOSMesaContext(GLFWosmesaContext ctx, int width, int height)
{
    if (!ctx) return GLFW_FALSE;
    return _rebind_buffer(ctx, width, height);
}

void glfwSwapOSMesaBuffers(GLFWosmesaContext ctx)
{
    if (!ctx || !ctx->buffer) return;
    _platform_blit(ctx);
}

void glfwDestroyOSMesaContext(GLFWosmesaContext ctx)
{
    if (!ctx) return;
    _platform_destroy(ctx);
    if (ctx->osmesa)
        OSMesaDestroyContext(ctx->osmesa);
    free(ctx->buffer);
    free(ctx);
}

const void *glfwGetOSMesaPixels(GLFWosmesaContext ctx,
                                 int *width, int *height, int *format)
{
    if (!ctx) return NULL;
    if (width)  *width  = ctx->width;
    if (height) *height = ctx->height;
    if (format) *format = GLFWOSMESA_FORMAT_BGRA;
    return ctx->buffer;
}

OSMesaContext glfwGetOSMesaContext(GLFWosmesaContext ctx)
{
    if (!ctx) return NULL;
    return ctx->osmesa;
}
