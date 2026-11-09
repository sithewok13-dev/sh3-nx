#define GL_LOADER_IMPL
#include "gl.h"
#include "../runtime/rt.h"
#include <SDL2/SDL_syswm.h>

#ifdef SH2_GLES
int gl_d3d_clip_control = 0;  /* ES has no glClipControl: the vertex shaders do it */
int gl_has_s3tc = 0;          /* set by gl_context_open when the driver has EXT_texture_compression_s3tc */
#ifdef SH2_GL_TABLE
#define GL_DEFINE(name) __typeof__(&name) p_##name;
GLES_FUNCS(GL_DEFINE)

int gl_load_with(void *(*lookup)(const char *name, void *ctx), void *ctx, const char **missing) {
#define GL_LOAD_FN(name) if (!(p_##name = (__typeof__(&name))lookup(#name, ctx))) { if (missing) *missing = #name; return 0; }
    GLES_FUNCS(GL_LOAD_FN)
    return 1;
}
static void *sdl_lookup(const char *name, void *ctx) { (void)ctx; return SDL_GL_GetProcAddress(name); }
void gl_load(void) {
    const char *missing = "";
    if (!gl_load_with(sdl_lookup, NULL, &missing)) rt_fatal("missing GL function %s", missing);
}
#endif
#else
int gl_d3d_clip_control = 1;  /* 4.5 core */
int gl_has_s3tc = 1;          /* every desktop driver the port runs on */

#define GL_DEFINE(type, name) type p_##name;
GL_FUNCS(GL_DEFINE)

void gl_load(void) {
#define GL_LOAD_FN(type, name) if (!(p_##name = (type)SDL_GL_GetProcAddress(#name))) rt_fatal("missing GL function " #name);
    GL_FUNCS(GL_LOAD_FN)
}
#endif

GLuint gl_screen_fbo(void) {
#ifdef SH2_IOS
    /* Apple's OpenGL ES on UIKit has no window framebuffer: SDL's GL view renders into an FBO it made, which it
     * names here. ANGLE's EGL surface is framebuffer 0, as everywhere else. */
    static GLuint fbo;
    static int known;
    if (ios_gl_angle()) return 0;
    if (!known) {
        SDL_SysWMinfo info;
        SDL_VERSION(&info.version);
        if (SDL_GetWindowWMInfo(rt_window, &info) && info.subsystem == SDL_SYSWM_UIKIT) fbo = info.info.uikit.framebuffer;
        else rt_log("GL: no UIKit framebuffer from SDL (%s); drawing to 0", SDL_GetError());
        known = 1;
    }
    return fbo;
#else
    return 0;
#endif
}
