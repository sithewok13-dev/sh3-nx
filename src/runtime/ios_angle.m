/* The GL context on iOS (see d3d8/gl.h): ANGLE, OpenGL ES 3.0 on Metal, by default; Apple's deprecated OpenGL ES
 * when ANGLE cannot start (its frameworks missing or refused, no Metal device, no surface) or when sh3.ini says
 * renderer=opengles. The log says which one draws.
 *
 * Why: Apple's OpenGL ES is a layer over Metal that costs tens of microseconds of CPU per draw call; ANGLE's Metal
 * backend is far cheaper, and the game makes hundreds of draw calls a frame.
 *
 * Both libraries have the same entry points (glDrawArrays...), so neither is linked: ANGLE's two frameworks are
 * bundled in the app's Frameworks folder and opened here, Apple's OpenGLES is opened from the system, and the GL
 * table (gl.h, SH2_GL_TABLE) is filled from whichever started. SDL keeps the window, events and its own OpenGL ES
 * support for the fallback. ANGLE draws into an EGL window surface on SDL's Metal view (a CAMetalLayer), whose
 * framebuffer is 0; Apple's draws into the FBO SDL's GL view made (gl_screen_fbo). The context is made on the
 * main thread and then made current on the game thread (runtime/ios.c), with EGL as with EAGL. */
#ifdef SH2_IOS
#define IOS_GL_IMPL
#include "rt.h"
#include "../d3d8/gl.h"
#include <SDL2/SDL.h>
#include <SDL2/SDL_metal.h>
#include <dlfcn.h>
#include <stdio.h>
#include <string.h>
#import <Foundation/Foundation.h>
#import <QuartzCore/QuartzCore.h>

static int angle_on;
const char *ios_gl_name(void) { return angle_on ? "ANGLE (Metal)" : "Apple OpenGL ES"; }
int ios_gl_angle(void) { return angle_on; }

static void *dl_lookup(const char *name, void *lib) { return dlsym(lib, name); }

#ifndef IOS_SETTINGS_INI
#define IOS_SETTINGS_INI "sh3.ini"  /* the settings file next to the game */
#endif

/* renderer=opengles in the settings: Apple's OpenGL ES even when ANGLE would start */
static int ini_wants_apple(void) {
    FILE *f = fopen(IOS_SETTINGS_INI, "r");
    char line[128], v[32];
    int apple = 0;
    while (f && fgets(line, sizeof line, f))
        if (sscanf(line, " renderer = %31s", v) == 1) apple = !strcasecmp(v, "opengles");
    if (f) fclose(f);
    return apple;
}

#ifndef SH2_NO_ANGLE
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <EGL/eglext_angle.h>

#define EGL_FUNCS(X) \
    X(PFNEGLGETPROCADDRESSPROC, eglGetProcAddress) \
    X(PFNEGLGETERRORPROC, eglGetError) \
    X(PFNEGLINITIALIZEPROC, eglInitialize) \
    X(PFNEGLTERMINATEPROC, eglTerminate) \
    X(PFNEGLQUERYSTRINGPROC, eglQueryString) \
    X(PFNEGLCHOOSECONFIGPROC, eglChooseConfig) \
    X(PFNEGLCREATEWINDOWSURFACEPROC, eglCreateWindowSurface) \
    X(PFNEGLDESTROYSURFACEPROC, eglDestroySurface) \
    X(PFNEGLQUERYSURFACEPROC, eglQuerySurface) \
    X(PFNEGLCREATECONTEXTPROC, eglCreateContext) \
    X(PFNEGLDESTROYCONTEXTPROC, eglDestroyContext) \
    X(PFNEGLMAKECURRENTPROC, eglMakeCurrent) \
    X(PFNEGLGETCURRENTCONTEXTPROC, eglGetCurrentContext) \
    X(PFNEGLSWAPBUFFERSPROC, eglSwapBuffers) \
    X(PFNEGLSWAPINTERVALPROC, eglSwapInterval)
#define EGL_DEFINE(type, name) static type e_##name;
EGL_FUNCS(EGL_DEFINE)

static void *egl_lib, *gles_lib;
static SDL_MetalView metal_view;
static EGLDisplay dpy = EGL_NO_DISPLAY;
static EGLSurface surf = EGL_NO_SURFACE;
static EGLContext ctx = EGL_NO_CONTEXT;

static void angle_close(void) {
    if (dpy != EGL_NO_DISPLAY) {
        e_eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        if (ctx != EGL_NO_CONTEXT) e_eglDestroyContext(dpy, ctx);
        if (surf != EGL_NO_SURFACE) e_eglDestroySurface(dpy, surf);
        e_eglTerminate(dpy);
    }
    ctx = EGL_NO_CONTEXT, surf = EGL_NO_SURFACE, dpy = EGL_NO_DISPLAY;
    if (metal_view) {  /* the window was turned into a Metal one: a fresh one for Apple's OpenGL ES */
        SDL_Metal_DestroyView(metal_view);
        metal_view = NULL;
        SDL_DestroyWindow(rt_window);
        rt_window = NULL;
        rt_window_open();
    }
}

/* 0 with the reason logged when ANGLE does not start; nothing of it is left behind then. */
static int angle_open(void) {
    @autoreleasepool {
        NSString *fw = [[NSBundle mainBundle] privateFrameworksPath];
        NSString *gles = [fw stringByAppendingPathComponent:@"libGLESv2.framework/libGLESv2"];
        NSString *egl = [fw stringByAppendingPathComponent:@"libEGL.framework/libEGL"];
        /* libGLESv2 first: libEGL is linked against it */
        if (!gles_lib) gles_lib = dlopen(gles.fileSystemRepresentation, RTLD_NOW | RTLD_LOCAL);
        if (gles_lib && !egl_lib) egl_lib = dlopen(egl.fileSystemRepresentation, RTLD_NOW | RTLD_LOCAL);
        if (!gles_lib || !egl_lib) {
            rt_log("GL: ANGLE's frameworks did not load (%s)", dlerror());
            return 0;
        }
    }
#define EGL_LOAD(type, name) if (!(e_##name = (type)dlsym(egl_lib, #name))) { rt_log("GL: ANGLE has no " #name); return 0; }
    EGL_FUNCS(EGL_LOAD)
    /* ANGLE picks a backend per display: ask for Metal by name */
    PFNEGLGETPLATFORMDISPLAYEXTPROC get_platform_display =
        (PFNEGLGETPLATFORMDISPLAYEXTPROC)e_eglGetProcAddress("eglGetPlatformDisplayEXT");
    const EGLint display_attribs[] = {EGL_PLATFORM_ANGLE_TYPE_ANGLE, EGL_PLATFORM_ANGLE_TYPE_METAL_ANGLE, EGL_NONE};
    if (get_platform_display)
        dpy = get_platform_display(EGL_PLATFORM_ANGLE_ANGLE, (void *)(uintptr_t)EGL_DEFAULT_DISPLAY, display_attribs);
    EGLint major = 0, minor = 0;
    if (dpy == EGL_NO_DISPLAY || !e_eglInitialize(dpy, &major, &minor)) {
        rt_log("GL: ANGLE found no Metal display (EGL error 0x%04X)", e_eglGetError());
        dpy = EGL_NO_DISPLAY;
        return 0;
    }
    /* Only Present's blit and the overlay draw to the window: no depth or stencil of its own */
    const EGLint config_attribs[] = {EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT, EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
                                     EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8,
                                     EGL_DEPTH_SIZE, 0, EGL_STENCIL_SIZE, 0, EGL_NONE};
    EGLConfig config = NULL;
    EGLint configs = 0;
    const char *missing = "";
    if (!e_eglChooseConfig(dpy, config_attribs, &config, 1, &configs) || configs < 1) {
        rt_log("GL: ANGLE has no RGBA8 OpenGL ES 3 window config (EGL error 0x%04X)", e_eglGetError());
    } else if (!gl_load_with(dl_lookup, gles_lib, &missing)) {
        rt_log("GL: ANGLE has no %s", missing);
    } else if (!(metal_view = SDL_Metal_CreateView(rt_window))) {
        rt_log("GL: no Metal view from SDL (%s)", SDL_GetError());
    } else {
        void *layer = SDL_Metal_GetLayer(metal_view);
        /* What is drawn is the picture, whatever its alpha: the layer covers the window */
        if (layer) ((__bridge CALayer *)layer).opaque = YES;
        const EGLint context_attribs[] = {EGL_CONTEXT_MAJOR_VERSION, 3, EGL_CONTEXT_MINOR_VERSION, 0, EGL_NONE};
        if (!layer)
            rt_log("GL: SDL's Metal view has no layer (%s)", SDL_GetError());
        else if ((surf = e_eglCreateWindowSurface(dpy, config, (EGLNativeWindowType)layer, NULL)) == EGL_NO_SURFACE)
            rt_log("GL: ANGLE made no window surface (EGL error 0x%04X)", e_eglGetError());
        else if ((ctx = e_eglCreateContext(dpy, config, EGL_NO_CONTEXT, context_attribs)) == EGL_NO_CONTEXT)
            rt_log("GL: ANGLE made no OpenGL ES 3.0 context (EGL error 0x%04X)", e_eglGetError());
        else if (!e_eglMakeCurrent(dpy, surf, surf, ctx))
            rt_log("GL: ANGLE's context could not be made current (EGL error 0x%04X)", e_eglGetError());
        else {
            e_eglSwapInterval(dpy, 1);
            rt_log("GL: ANGLE, EGL %d.%d (%s)", major, minor, e_eglQueryString(dpy, EGL_VERSION));
            return 1;
        }
    }
    angle_close();
    return 0;
}
#else
static int angle_open(void) {
    rt_log("GL: this build has no ANGLE");
    return 0;
}
#endif

static void *apple_ctx;

static int apple_open(void) {
    void *lib = dlopen("/System/Library/Frameworks/OpenGLES.framework/OpenGLES", RTLD_NOW | RTLD_LOCAL);
    const char *missing = "";
    if (!lib) {
        rt_log("GL: Apple's OpenGL ES did not load (%s)", dlerror());
        return 0;
    }
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 0);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 0);
    if (!(apple_ctx = SDL_GL_CreateContext(rt_window))) {
        rt_log("GL: no OpenGL ES 3.0 context: %s", SDL_GetError());
        return 0;
    }
    if (!gl_load_with(dl_lookup, lib, &missing)) {
        rt_log("GL: Apple's OpenGL ES has no %s", missing);
        return 0;
    }
    SDL_GL_SetSwapInterval(1);
    return 1;
}

int ios_gl_open(void) {
    if (ini_wants_apple()) rt_log("GL: " IOS_SETTINGS_INI " asks for Apple's OpenGL ES (renderer=opengles)");
    else if (angle_open()) angle_on = 1;
    if (!angle_on && !apple_open()) return 0;
    rt_log("GL: drawing with %s: %s / %s", ios_gl_name(), glGetString(GL_RENDERER), glGetString(GL_VERSION));
    return 1;
}

void *ios_gl_current(void) {
#ifndef SH2_NO_ANGLE
    if (angle_on) return e_eglGetCurrentContext();
#endif
    return SDL_GL_GetCurrentContext();
}

int ios_gl_make_current(void *c) {
#ifndef SH2_NO_ANGLE
    if (angle_on) return e_eglMakeCurrent(dpy, c ? surf : EGL_NO_SURFACE, c ? surf : EGL_NO_SURFACE,
                                          c ? (EGLContext)c : EGL_NO_CONTEXT) ? 0 : -1;
#endif
    return SDL_GL_MakeCurrent(rt_window, c);
}

void ios_gl_swap(void) {
#ifndef SH2_NO_ANGLE
    if (angle_on) {
        e_eglSwapBuffers(dpy, surf);
        return;
    }
#endif
    SDL_GL_SwapWindow(rt_window);
}

void ios_gl_drawable_size(int *w, int *h) {
#ifndef SH2_NO_ANGLE
    if (angle_on) {
        EGLint ew = 0, eh = 0;
        e_eglQuerySurface(dpy, surf, EGL_WIDTH, &ew);
        e_eglQuerySurface(dpy, surf, EGL_HEIGHT, &eh);
        *w = ew, *h = eh;
        return;
    }
#endif
    SDL_GL_GetDrawableSize(rt_window, w, h);
}

int ios_gl_set_swap_interval(int interval) {
#ifndef SH2_NO_ANGLE
    if (angle_on) return e_eglSwapInterval(dpy, interval) ? 0 : -1;
#endif
    return SDL_GL_SetSwapInterval(interval);
}
#endif
