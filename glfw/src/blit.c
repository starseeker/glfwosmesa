//========================================================================
// GLFW 3.4 - www.glfw.org
//------------------------------------------------------------------------
// Copyright (c) 2002-2006 Marcus Geelnard
// Copyright (c) 2006-2019 Camilla Löwy <elmindreda@glfw.org>
//
// This software is provided 'as-is', without any express or implied
// warranty. In no event will the authors be held liable for any damages
// arising from the use of this software.
//
// Permission is granted to anyone to use this software for any purpose,
// including commercial applications, and to alter it and redistribute it
// freely, subject to the following restrictions:
//
// 1. The origin of this software must not be misrepresented; you must not
//    claim that you wrote the original software. If you use this software
//    in a product, an acknowledgment in the product documentation would
//    be appreciated but is not required.
//
// 2. Altered source versions must be plainly marked as such, and must not
//    be misrepresented as being the original software.
//
// 3. This notice may not be removed or altered from any source
//    distribution.
//
//========================================================================
// Please use C89 style variable declarations in this file because VS 2010
//========================================================================

// glfwBlitPixelBuffer -- copy a CPU pixel buffer to a GLFW_NO_API window.
//
// Platform implementations
// ========================
// Linux / X11  : XCreateImage() + XPutImage() via Xlib
// Windows      : SetDIBitsToDevice() via GDI
// macOS        : CALayer setContents: + CGImage via CoreGraphics
// Wayland      : not yet implemented

#include "internal.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

#if defined(_GLFW_COCOA)
// objc_msgSend is used to call [layer setContents:] from plain C.
#  include <objc/message.h>
#  include <CoreGraphics/CoreGraphics.h>
#endif


//////////////////////////////////////////////////////////////////////////
//////                    Internal helper: R/B swap                 //////
//////////////////////////////////////////////////////////////////////////

// Swap R and B channels in a tightly-packed 32-bit-per-pixel RGBA buffer,
// writing the result to dst (which may differ from src).  Used to convert
// RGBA to BGRA when the caller supplies RGBA but the platform needs BGRA.
//
static void swapRB(unsigned char* dst,
                   const unsigned char* src,
                   int width, int height)
{
    int i;
    int npixels = width * height;
    for (i = 0; i < npixels; i++)
    {
        dst[i * 4 + 0] = src[i * 4 + 2]; /* B <- R */
        dst[i * 4 + 1] = src[i * 4 + 1]; /* G      */
        dst[i * 4 + 2] = src[i * 4 + 0]; /* R <- B */
        dst[i * 4 + 3] = src[i * 4 + 3]; /* A      */
    }
}


//////////////////////////////////////////////////////////////////////////
//////                       GLFW public API                        //////
//////////////////////////////////////////////////////////////////////////

GLFWAPI void glfwBlitPixelBuffer(GLFWwindow* handle,
                                  const void* pixels,
                                  int         srcWidth,
                                  int         srcHeight,
                                  int         format)
{
    _GLFWwindow* window = (_GLFWwindow*) handle;
    assert(window != NULL);

    _GLFW_REQUIRE_INIT();

    if (!pixels || srcWidth <= 0 || srcHeight <= 0)
    {
        _glfwInputError(GLFW_INVALID_VALUE,
                        "glfwBlitPixelBuffer: invalid pixel buffer parameters");
        return;
    }

    if (window->context.client != GLFW_NO_API)
    {
        _glfwInputError(GLFW_INVALID_VALUE,
                        "glfwBlitPixelBuffer: window must be created with "
                        "GLFW_CLIENT_API = GLFW_NO_API");
        return;
    }

    if (format != GLFW_PIXEL_FORMAT_RGBA &&
        format != GLFW_PIXEL_FORMAT_BGRA &&
        format != GLFW_PIXEL_FORMAT_RGB)
    {
        _glfwInputError(GLFW_INVALID_VALUE,
                        "glfwBlitPixelBuffer: unknown pixel format 0x%x",
                        format);
        return;
    }

#if defined(_GLFW_X11)
    {
        Display*       dpy       = _glfw.x11.display;
        Window         xwin      = window->x11.handle;
        int            screen    = _glfw.x11.screen;
        Visual*        visual    = DefaultVisual(dpy, screen);
        GC             gc        = DefaultGC(dpy, screen);
        int            depth     = DefaultDepth(dpy, screen);
        size_t         nbytes    = (size_t)srcWidth * (size_t)srcHeight * 4u;
        const void*    bgraPixels;
        unsigned char* tmp       = NULL;
        char*          data;
        XImage*        img;

        // On little-endian x86/x86-64, XPutImage with a 32-bit TrueColor
        // visual expects BGRA byte order.  Convert RGBA->BGRA if needed.
        if (format == GLFW_PIXEL_FORMAT_RGBA)
        {
            tmp = (unsigned char*) malloc(nbytes);
            if (!tmp)
            {
                _glfwInputError(GLFW_OUT_OF_MEMORY,
                                "glfwBlitPixelBuffer: malloc failed");
                return;
            }
            swapRB(tmp, (const unsigned char*) pixels, srcWidth, srcHeight);
            bgraPixels = tmp;
        }
        else
        {
            bgraPixels = pixels;
        }

        // XDestroyImage will free() img->data, so hand it a malloc'd copy.
        data = (char*) malloc(nbytes);
        if (!data)
        {
            free(tmp);
            _glfwInputError(GLFW_OUT_OF_MEMORY,
                            "glfwBlitPixelBuffer: malloc failed");
            return;
        }
        memcpy(data, bgraPixels, nbytes);
        free(tmp);

        img = XCreateImage(dpy, visual,
                           (unsigned int)depth,
                           ZPixmap,
                           0,       /* offset */
                           data,
                           (unsigned int)srcWidth,
                           (unsigned int)srcHeight,
                           32,      /* bitmap_pad */
                           0);      /* bytes_per_line: 0 = auto */
        if (!img)
        {
            free(data);
            _glfwInputError(GLFW_PLATFORM_ERROR,
                            "glfwBlitPixelBuffer: XCreateImage failed");
            return;
        }

        XPutImage(dpy, xwin, gc, img, 0, 0, 0, 0,
                  (unsigned int)srcWidth, (unsigned int)srcHeight);
        XFlush(dpy);
        XDestroyImage(img); /* frees data */
    }

#elif defined(_GLFW_WIN32)
    {
        HWND           hwnd      = window->win32.handle;
        HDC            hdc;
        BITMAPINFO     bmi;
        const void*    bgraPixels;
        unsigned char* tmp       = NULL;
        size_t         nbytes    = (size_t)srcWidth * (size_t)srcHeight * 4u;

        hdc = GetDC(hwnd);
        if (!hdc)
        {
            _glfwInputError(GLFW_PLATFORM_ERROR,
                            "glfwBlitPixelBuffer: GetDC failed");
            return;
        }

        // GDI SetDIBitsToDevice with BI_RGB expects BGRA byte order.
        if (format == GLFW_PIXEL_FORMAT_RGBA)
        {
            tmp = (unsigned char*) malloc(nbytes);
            if (!tmp)
            {
                ReleaseDC(hwnd, hdc);
                _glfwInputError(GLFW_OUT_OF_MEMORY,
                                "glfwBlitPixelBuffer: malloc failed");
                return;
            }
            swapRB(tmp, (const unsigned char*) pixels, srcWidth, srcHeight);
            bgraPixels = tmp;
        }
        else
        {
            bgraPixels = pixels;
        }

        memset(&bmi, 0, sizeof(bmi));
        bmi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
        bmi.bmiHeader.biWidth       = srcWidth;
        bmi.bmiHeader.biHeight      = -srcHeight; /* negative = top-down */
        bmi.bmiHeader.biPlanes      = 1;
        bmi.bmiHeader.biBitCount    = 32;
        bmi.bmiHeader.biCompression = BI_RGB;

        SetDIBitsToDevice(hdc,
                          0, 0,
                          (DWORD)srcWidth, (DWORD)srcHeight,
                          0, 0, 0, (UINT)srcHeight,
                          bgraPixels, &bmi, DIB_RGB_COLORS);

        free(tmp);
        ReleaseDC(hwnd, hdc);
    }

#elif defined(_GLFW_COCOA)
    {
        // Use CoreGraphics to build a CGImage and set it on the window's
        // CALayer via [layer setContents: (id)image].
        id                layer     = window->ns.layer;
        CGColorSpaceRef   cs;
        CGDataProviderRef provider;
        CGImageRef        image;
        const void*       bgraPixels;
        unsigned char*    tmp       = NULL;
        size_t            nbytes    = (size_t)srcWidth * (size_t)srcHeight * 4u;

        if (!layer)
        {
            _glfwInputError(GLFW_PLATFORM_ERROR,
                            "glfwBlitPixelBuffer: window has no CALayer");
            return;
        }

        // CoreGraphics kCGBitmapByteOrder32Little|AlphaNoneSkipFirst reads
        // each 32-bit word as xRGB on little-endian, mapping BGRA memory
        // layout to display-native BGRX with no per-pixel conversion.
        if (format == GLFW_PIXEL_FORMAT_RGBA)
        {
            tmp = (unsigned char*) malloc(nbytes);
            if (!tmp)
            {
                _glfwInputError(GLFW_OUT_OF_MEMORY,
                                "glfwBlitPixelBuffer: malloc failed");
                return;
            }
            swapRB(tmp, (const unsigned char*) pixels, srcWidth, srcHeight);
            bgraPixels = tmp;
        }
        else
        {
            bgraPixels = pixels;
        }

        cs = CGColorSpaceCreateDeviceRGB();
        provider = CGDataProviderCreateWithData(NULL, bgraPixels, nbytes, NULL);
        image = CGImageCreate((size_t)srcWidth, (size_t)srcHeight,
                              8, 32, (size_t)srcWidth * 4u,
                              cs,
                              kCGBitmapByteOrder32Little |
                                  kCGImageAlphaNoneSkipFirst,
                              provider, NULL, false,
                              kCGRenderingIntentDefault);
        CGDataProviderRelease(provider);
        CGColorSpaceRelease(cs);

        if (image)
        {
            ((void (*)(id, SEL, id))objc_msgSend)(
                layer, sel_registerName("setContents:"), (id)image);
            CGImageRelease(image);
        }
        else
        {
            _glfwInputError(GLFW_PLATFORM_ERROR,
                            "glfwBlitPixelBuffer: CGImageCreate failed");
        }

        free(tmp);
    }

#elif defined(_GLFW_WAYLAND)
    {
        _glfwInputError(GLFW_PLATFORM_ERROR,
                        "glfwBlitPixelBuffer: not yet implemented for Wayland");
    }

#else
    {
        _glfwInputError(GLFW_PLATFORM_ERROR,
                        "glfwBlitPixelBuffer: not implemented for this platform");
    }
#endif /* platform */
}
