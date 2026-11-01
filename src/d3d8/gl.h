/* The GL the D3D8 layer draws with.
 * Desktop and Switch: OpenGL 4.5 core entry points, loaded through SDL (the same on Linux and on the Switch's mesa).
 * SH2_GLES (iOS, or -DSH2_GLES=ON on Linux to test it): OpenGL ES 3.0, linked directly. ES lacks clip control,
 * depth clamp, polygon modes, base-vertex draws, BGRA data and DXT textures; d3d8*.c work around each where
 * gl_d3d_clip_control / gl_has_s3tc say so. */
#pragma once
#include <SDL2/SDL.h>

#ifdef SH2_GLES
#if defined(__APPLE__) && !defined(SH2_ANGLE)
#define GLES_SILENCE_DEPRECATION
#include <OpenGLES/ES3/gl.h>
#include <OpenGLES/ES3/glext.h>
#else
#include <GLES3/gl3.h>
#include <GLES2/gl2ext.h>
#endif
/* ES spells these with an f. */
#define glClearDepth glClearDepthf
#define glDepthRange glDepthRangef
/* Desktop-only enums the format and state tables still name; ES code paths never pass them to GL. */
#ifndef GL_BGRA
#define GL_BGRA 0x80E1
#endif
#define GL_UNSIGNED_SHORT_1_5_5_5_REV 0x8366
#define GL_UNSIGNED_SHORT_4_4_4_4_REV 0x8365
#ifndef GL_CLAMP_TO_BORDER
#define GL_CLAMP_TO_BORDER 0x812D
#endif
#ifndef GL_MIRROR_CLAMP_TO_EDGE
#define GL_MIRROR_CLAMP_TO_EDGE 0x8743
#endif
#ifndef GL_TEXTURE_BORDER_COLOR
#define GL_TEXTURE_BORDER_COLOR 0x1004
#endif
static inline void gl_load(void) {}
#else
#include <SDL2/SDL_opengl.h>

/* GL 1.x entry points are typed from SDL_opengl.h's prototypes and loaded like the rest: the Switch
 * has no libGL to link them from. */
#define GL_FUNCS(X) \
    X(__typeof__(&glBindTexture), glBindTexture) \
    X(__typeof__(&glClear), glClear) \
    X(__typeof__(&glClearColor), glClearColor) \
    X(__typeof__(&glClearDepth), glClearDepth) \
    X(__typeof__(&glClearStencil), glClearStencil) \
    X(__typeof__(&glColorMask), glColorMask) \
    X(__typeof__(&glCullFace), glCullFace) \
    X(__typeof__(&glDeleteTextures), glDeleteTextures) \
    X(__typeof__(&glDepthFunc), glDepthFunc) \
    X(__typeof__(&glDepthMask), glDepthMask) \
    X(__typeof__(&glDepthRange), glDepthRange) \
    X(__typeof__(&glDisable), glDisable) \
    X(__typeof__(&glDrawArrays), glDrawArrays) \
    X(__typeof__(&glDrawElements), glDrawElements) \
    X(__typeof__(&glEnable), glEnable) \
    X(__typeof__(&glFrontFace), glFrontFace) \
    X(__typeof__(&glGenTextures), glGenTextures) \
    X(__typeof__(&glGetString), glGetString) \
    X(__typeof__(&glGetIntegerv), glGetIntegerv) \
    X(PFNGLGETSTRINGIPROC, glGetStringi) \
    X(__typeof__(&glPixelStorei), glPixelStorei) \
    X(__typeof__(&glPolygonMode), glPolygonMode) \
    X(__typeof__(&glPolygonOffset), glPolygonOffset) \
    X(__typeof__(&glReadPixels), glReadPixels) \
    X(__typeof__(&glScissor), glScissor) \
    X(__typeof__(&glStencilFunc), glStencilFunc) \
    X(__typeof__(&glStencilMask), glStencilMask) \
    X(__typeof__(&glTexImage2D), glTexImage2D) \
    X(__typeof__(&glTexParameteri), glTexParameteri) \
    X(__typeof__(&glTexParameteriv), glTexParameteriv) \
    X(__typeof__(&glTexSubImage2D), glTexSubImage2D) \
    X(__typeof__(&glViewport), glViewport) \
    X(PFNGLCLIPCONTROLPROC, glClipControl) \
    X(PFNGLGENBUFFERSPROC, glGenBuffers) \
    X(PFNGLBINDBUFFERPROC, glBindBuffer) \
    X(PFNGLBUFFERDATAPROC, glBufferData) \
    X(PFNGLBUFFERSUBDATAPROC, glBufferSubData) \
    X(PFNGLDELETEBUFFERSPROC, glDeleteBuffers) \
    X(PFNGLGENVERTEXARRAYSPROC, glGenVertexArrays) \
    X(PFNGLBINDVERTEXARRAYPROC, glBindVertexArray) \
    X(PFNGLVERTEXATTRIBPOINTERPROC, glVertexAttribPointer) \
    X(PFNGLVERTEXATTRIBIPOINTERPROC, glVertexAttribIPointer) \
    X(PFNGLENABLEVERTEXATTRIBARRAYPROC, glEnableVertexAttribArray) \
    X(PFNGLDISABLEVERTEXATTRIBARRAYPROC, glDisableVertexAttribArray) \
    X(PFNGLVERTEXATTRIB4FPROC, glVertexAttrib4f) \
    X(PFNGLCREATESHADERPROC, glCreateShader) \
    X(PFNGLSHADERSOURCEPROC, glShaderSource) \
    X(PFNGLCOMPILESHADERPROC, glCompileShader) \
    X(PFNGLGETSHADERIVPROC, glGetShaderiv) \
    X(PFNGLGETSHADERINFOLOGPROC, glGetShaderInfoLog) \
    X(PFNGLCREATEPROGRAMPROC, glCreateProgram) \
    X(PFNGLATTACHSHADERPROC, glAttachShader) \
    X(PFNGLBINDATTRIBLOCATIONPROC, glBindAttribLocation) \
    X(PFNGLLINKPROGRAMPROC, glLinkProgram) \
    X(PFNGLGETPROGRAMIVPROC, glGetProgramiv) \
    X(PFNGLGETPROGRAMINFOLOGPROC, glGetProgramInfoLog) \
    X(PFNGLUSEPROGRAMPROC, glUseProgram) \
    X(PFNGLDELETESHADERPROC, glDeleteShader) \
    X(PFNGLGETUNIFORMLOCATIONPROC, glGetUniformLocation) \
    X(PFNGLUNIFORM1IPROC, glUniform1i) \
    X(PFNGLUNIFORM1FPROC, glUniform1f) \
    X(PFNGLUNIFORM2FPROC, glUniform2f) \
    X(PFNGLUNIFORM4FVPROC, glUniform4fv) \
    X(PFNGLUNIFORM1IVPROC, glUniform1iv) \
    X(PFNGLUNIFORMMATRIX4FVPROC, glUniformMatrix4fv) \
    X(PFNGLUNIFORMMATRIX3FVPROC, glUniformMatrix3fv) \
    X(PFNGLACTIVETEXTUREPROC, glActiveTexture) \
    X(PFNGLCOMPRESSEDTEXIMAGE2DPROC, glCompressedTexImage2D) \
    X(PFNGLCOMPRESSEDTEXSUBIMAGE2DPROC, glCompressedTexSubImage2D) \
    X(PFNGLGENERATEMIPMAPPROC, glGenerateMipmap) \
    X(PFNGLGENFRAMEBUFFERSPROC, glGenFramebuffers) \
    X(PFNGLBINDFRAMEBUFFERPROC, glBindFramebuffer) \
    X(PFNGLFRAMEBUFFERTEXTURE2DPROC, glFramebufferTexture2D) \
    X(PFNGLCHECKFRAMEBUFFERSTATUSPROC, glCheckFramebufferStatus) \
    X(PFNGLDELETEFRAMEBUFFERSPROC, glDeleteFramebuffers) \
    X(PFNGLBLITFRAMEBUFFERPROC, glBlitFramebuffer) \
    X(PFNGLBLENDFUNCSEPARATEPROC, glBlendFuncSeparate) \
    X(PFNGLBLENDEQUATIONPROC, glBlendEquation) \
    X(PFNGLBLENDCOLORPROC, glBlendColor) \
    X(PFNGLSTENCILOPSEPARATEPROC, glStencilOpSeparate) \
    X(PFNGLDRAWELEMENTSBASEVERTEXPROC, glDrawElementsBaseVertex) \
    X(PFNGLGENSAMPLERSPROC, glGenSamplers) \
    X(PFNGLBINDSAMPLERPROC, glBindSampler) \
    X(PFNGLSAMPLERPARAMETERIPROC, glSamplerParameteri) \
    X(PFNGLSAMPLERPARAMETERFPROC, glSamplerParameterf) \
    X(PFNGLSAMPLERPARAMETERFVPROC, glSamplerParameterfv)

#define GL_DECLARE(type, name) extern type p_##name;
GL_FUNCS(GL_DECLARE)
#undef GL_DECLARE
#ifndef GL_LOADER_IMPL
#define glBindTexture p_glBindTexture
#define glClear p_glClear
#define glClearColor p_glClearColor
#define glClearDepth p_glClearDepth
#define glClearStencil p_glClearStencil
#define glColorMask p_glColorMask
#define glCullFace p_glCullFace
#define glDeleteTextures p_glDeleteTextures
#define glDepthFunc p_glDepthFunc
#define glDepthMask p_glDepthMask
#define glDepthRange p_glDepthRange
#define glDisable p_glDisable
#define glDrawArrays p_glDrawArrays
#define glDrawElements p_glDrawElements
#define glEnable p_glEnable
#define glFrontFace p_glFrontFace
#define glGenTextures p_glGenTextures
#define glGetString p_glGetString
#define glGetIntegerv p_glGetIntegerv
#define glGetStringi p_glGetStringi
#define glPixelStorei p_glPixelStorei
#define glPolygonMode p_glPolygonMode
#define glPolygonOffset p_glPolygonOffset
#define glReadPixels p_glReadPixels
#define glScissor p_glScissor
#define glStencilFunc p_glStencilFunc
#define glStencilMask p_glStencilMask
#define glTexImage2D p_glTexImage2D
#define glTexParameteri p_glTexParameteri
#define glTexParameteriv p_glTexParameteriv
#define glTexSubImage2D p_glTexSubImage2D
#define glViewport p_glViewport
#define glClipControl p_glClipControl
#define glGenBuffers p_glGenBuffers
#define glBindBuffer p_glBindBuffer
#define glBufferData p_glBufferData
#define glBufferSubData p_glBufferSubData
#define glDeleteBuffers p_glDeleteBuffers
#define glGenVertexArrays p_glGenVertexArrays
#define glBindVertexArray p_glBindVertexArray
#define glVertexAttribPointer p_glVertexAttribPointer
#define glVertexAttribIPointer p_glVertexAttribIPointer
#define glEnableVertexAttribArray p_glEnableVertexAttribArray
#define glDisableVertexAttribArray p_glDisableVertexAttribArray
#define glVertexAttrib4f p_glVertexAttrib4f
#define glCreateShader p_glCreateShader
#define glShaderSource p_glShaderSource
#define glCompileShader p_glCompileShader
#define glGetShaderiv p_glGetShaderiv
#define glGetShaderInfoLog p_glGetShaderInfoLog
#define glCreateProgram p_glCreateProgram
#define glAttachShader p_glAttachShader
#define glBindAttribLocation p_glBindAttribLocation
#define glLinkProgram p_glLinkProgram
#define glGetProgramiv p_glGetProgramiv
#define glGetProgramInfoLog p_glGetProgramInfoLog
#define glUseProgram p_glUseProgram
#define glDeleteShader p_glDeleteShader
#define glGetUniformLocation p_glGetUniformLocation
#define glUniform1i p_glUniform1i
#define glUniform1f p_glUniform1f
#define glUniform2f p_glUniform2f
#define glUniform4fv p_glUniform4fv
#define glUniform1iv p_glUniform1iv
#define glUniformMatrix4fv p_glUniformMatrix4fv
#define glUniformMatrix3fv p_glUniformMatrix3fv
#define glActiveTexture p_glActiveTexture
#define glCompressedTexImage2D p_glCompressedTexImage2D
#define glCompressedTexSubImage2D p_glCompressedTexSubImage2D
#define glGenerateMipmap p_glGenerateMipmap
#define glGenFramebuffers p_glGenFramebuffers
#define glBindFramebuffer p_glBindFramebuffer
#define glFramebufferTexture2D p_glFramebufferTexture2D
#define glCheckFramebufferStatus p_glCheckFramebufferStatus
#define glDeleteFramebuffers p_glDeleteFramebuffers
#define glBlitFramebuffer p_glBlitFramebuffer
#define glBlendFuncSeparate p_glBlendFuncSeparate
#define glBlendEquation p_glBlendEquation
#define glBlendColor p_glBlendColor
#define glStencilOpSeparate p_glStencilOpSeparate
#define glDrawElementsBaseVertex p_glDrawElementsBaseVertex
#define glGenSamplers p_glGenSamplers
#define glBindSampler p_glBindSampler
#define glSamplerParameteri p_glSamplerParameteri
#define glSamplerParameterf p_glSamplerParameterf
#define glSamplerParameterfv p_glSamplerParameterfv
#endif

void gl_load(void);
#endif

/* 1 when GL maps D3D's y-down, [0,1]-depth clip space itself (glClipControl); 0 when every vertex shader
 * flips y and remaps z (gl_clip_vertex_fixup) and the winding that faces front flips with it. */
extern int gl_d3d_clip_control;
static inline const char *gl_clip_vertex_fixup(void) {
    return gl_d3d_clip_control ? "" : "  p.y = -p.y; p.z = 2.0 * p.z - p.w;\n";
}
static inline GLenum gl_d3d_front_face(void) { return gl_d3d_clip_control ? GL_CW : GL_CCW; }
/* DXT textures go to GL as they are (1) or are decoded to RGBA8 first (0: Apple's ES has no S3TC). */
extern int gl_has_s3tc;
/* The window's framebuffer: 0, except on iOS where SDL draws into a framebuffer object of its own. */
GLuint gl_screen_fbo(void);
