#define GL_LOADER_IMPL
#include "gl.h"
#include "../runtime/rt.h"
#include <SDL2/SDL_syswm.h>

#ifdef SH2_GLES
int gl_d3d_clip_control = 0;  /* ES has no glClipControl: the vertex shaders do it */
int gl_has_s3tc = 0;          /* set by gl_context_open when the driver has EXT_texture_compression_s3tc */
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
#if defined(__APPLE__) && defined(SH2_GLES) && !defined(SH2_ANGLE)
    /* UIKit has no window framebuffer: SDL's GL view renders into an FBO it made, which it names here. */
    static GLuint fbo;
    static int known;
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
