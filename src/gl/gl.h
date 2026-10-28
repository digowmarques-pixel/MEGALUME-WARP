// Minimal OpenGL 3.3 core loader. Function pointers are resolved through
// glfwGetProcAddress so the same code works with WGL (Windows) and GLX/EGL.
#pragma once

#if defined(_WIN32) && !defined(APIENTRY)
#define APIENTRY __stdcall
#endif
#include <GL/glcorearb.h>

#define LUMEN_GL_FUNCS(X)                                                        \
  X(PFNGLCLEARPROC, glClear)                                                     \
  X(PFNGLCLEARCOLORPROC, glClearColor)                                           \
  X(PFNGLCLEARSTENCILPROC, glClearStencil)                                       \
  X(PFNGLVIEWPORTPROC, glViewport)                                               \
  X(PFNGLENABLEPROC, glEnable)                                                   \
  X(PFNGLDISABLEPROC, glDisable)                                                 \
  X(PFNGLBLENDFUNCPROC, glBlendFunc)                                             \
  X(PFNGLBLENDFUNCSEPARATEPROC, glBlendFuncSeparate)                             \
  X(PFNGLBLENDEQUATIONPROC, glBlendEquation)                                     \
  X(PFNGLCOLORMASKPROC, glColorMask)                                             \
  X(PFNGLSTENCILFUNCPROC, glStencilFunc)                                         \
  X(PFNGLSTENCILOPPROC, glStencilOp)                                             \
  X(PFNGLSTENCILMASKPROC, glStencilMask)                                         \
  X(PFNGLGETERRORPROC, glGetError)                                               \
  X(PFNGLGETINTEGERVPROC, glGetIntegerv)                                         \
  X(PFNGLGETSTRINGPROC, glGetString)                                             \
  X(PFNGLPIXELSTOREIPROC, glPixelStorei)                                         \
  X(PFNGLREADPIXELSPROC, glReadPixels)                                           \
  X(PFNGLFINISHPROC, glFinish)                                                   \
  X(PFNGLGENTEXTURESPROC, glGenTextures)                                         \
  X(PFNGLDELETETEXTURESPROC, glDeleteTextures)                                   \
  X(PFNGLBINDTEXTUREPROC, glBindTexture)                                         \
  X(PFNGLACTIVETEXTUREPROC, glActiveTexture)                                     \
  X(PFNGLTEXIMAGE2DPROC, glTexImage2D)                                           \
  X(PFNGLTEXSUBIMAGE2DPROC, glTexSubImage2D)                                     \
  X(PFNGLTEXPARAMETERIPROC, glTexParameteri)                                     \
  X(PFNGLGENERATEMIPMAPPROC, glGenerateMipmap)                                   \
  X(PFNGLGENFRAMEBUFFERSPROC, glGenFramebuffers)                                 \
  X(PFNGLDELETEFRAMEBUFFERSPROC, glDeleteFramebuffers)                           \
  X(PFNGLBINDFRAMEBUFFERPROC, glBindFramebuffer)                                 \
  X(PFNGLFRAMEBUFFERTEXTURE2DPROC, glFramebufferTexture2D)                       \
  X(PFNGLFRAMEBUFFERRENDERBUFFERPROC, glFramebufferRenderbuffer)                 \
  X(PFNGLCHECKFRAMEBUFFERSTATUSPROC, glCheckFramebufferStatus)                   \
  X(PFNGLGENRENDERBUFFERSPROC, glGenRenderbuffers)                               \
  X(PFNGLDELETERENDERBUFFERSPROC, glDeleteRenderbuffers)                         \
  X(PFNGLBINDRENDERBUFFERPROC, glBindRenderbuffer)                               \
  X(PFNGLRENDERBUFFERSTORAGEPROC, glRenderbufferStorage)                         \
  X(PFNGLGENBUFFERSPROC, glGenBuffers)                                           \
  X(PFNGLDELETEBUFFERSPROC, glDeleteBuffers)                                     \
  X(PFNGLBINDBUFFERPROC, glBindBuffer)                                           \
  X(PFNGLBUFFERDATAPROC, glBufferData)                                           \
  X(PFNGLGENVERTEXARRAYSPROC, glGenVertexArrays)                                 \
  X(PFNGLDELETEVERTEXARRAYSPROC, glDeleteVertexArrays)                           \
  X(PFNGLBINDVERTEXARRAYPROC, glBindVertexArray)                                 \
  X(PFNGLENABLEVERTEXATTRIBARRAYPROC, glEnableVertexAttribArray)                 \
  X(PFNGLVERTEXATTRIBPOINTERPROC, glVertexAttribPointer)                         \
  X(PFNGLDRAWARRAYSPROC, glDrawArrays)                                           \
  X(PFNGLDRAWELEMENTSPROC, glDrawElements)                                       \
  X(PFNGLCREATESHADERPROC, glCreateShader)                                       \
  X(PFNGLSHADERSOURCEPROC, glShaderSource)                                       \
  X(PFNGLCOMPILESHADERPROC, glCompileShader)                                     \
  X(PFNGLGETSHADERIVPROC, glGetShaderiv)                                         \
  X(PFNGLGETSHADERINFOLOGPROC, glGetShaderInfoLog)                               \
  X(PFNGLDELETESHADERPROC, glDeleteShader)                                       \
  X(PFNGLCREATEPROGRAMPROC, glCreateProgram)                                     \
  X(PFNGLATTACHSHADERPROC, glAttachShader)                                       \
  X(PFNGLBINDATTRIBLOCATIONPROC, glBindAttribLocation)                           \
  X(PFNGLLINKPROGRAMPROC, glLinkProgram)                                         \
  X(PFNGLGETPROGRAMIVPROC, glGetProgramiv)                                       \
  X(PFNGLGETPROGRAMINFOLOGPROC, glGetProgramInfoLog)                             \
  X(PFNGLDELETEPROGRAMPROC, glDeleteProgram)                                     \
  X(PFNGLUSEPROGRAMPROC, glUseProgram)                                           \
  X(PFNGLGETUNIFORMLOCATIONPROC, glGetUniformLocation)                           \
  X(PFNGLUNIFORM1IPROC, glUniform1i)                                             \
  X(PFNGLUNIFORM1FPROC, glUniform1f)                                             \
  X(PFNGLUNIFORM2FPROC, glUniform2f)                                             \
  X(PFNGLUNIFORM3FPROC, glUniform3f)                                             \
  X(PFNGLUNIFORM4FPROC, glUniform4f)                                             \
  X(PFNGLFENCESYNCPROC, glFenceSync)                                             \
  X(PFNGLWAITSYNCPROC, glWaitSync)                                               \
  X(PFNGLDELETESYNCPROC, glDeleteSync)                                           \
  X(PFNGLFLUSHPROC, glFlush)

#define LUMEN_GL_DECLARE(type, name) extern type lumen_##name;
LUMEN_GL_FUNCS(LUMEN_GL_DECLARE)
#undef LUMEN_GL_DECLARE

#define glClear lumen_glClear
#define glClearColor lumen_glClearColor
#define glClearStencil lumen_glClearStencil
#define glViewport lumen_glViewport
#define glEnable lumen_glEnable
#define glDisable lumen_glDisable
#define glBlendFunc lumen_glBlendFunc
#define glBlendFuncSeparate lumen_glBlendFuncSeparate
#define glBlendEquation lumen_glBlendEquation
#define glColorMask lumen_glColorMask
#define glStencilFunc lumen_glStencilFunc
#define glStencilOp lumen_glStencilOp
#define glStencilMask lumen_glStencilMask
#define glGetError lumen_glGetError
#define glGetIntegerv lumen_glGetIntegerv
#define glGetString lumen_glGetString
#define glPixelStorei lumen_glPixelStorei
#define glReadPixels lumen_glReadPixels
#define glFinish lumen_glFinish
#define glGenTextures lumen_glGenTextures
#define glDeleteTextures lumen_glDeleteTextures
#define glBindTexture lumen_glBindTexture
#define glActiveTexture lumen_glActiveTexture
#define glTexImage2D lumen_glTexImage2D
#define glTexSubImage2D lumen_glTexSubImage2D
#define glTexParameteri lumen_glTexParameteri
#define glGenerateMipmap lumen_glGenerateMipmap
#define glGenFramebuffers lumen_glGenFramebuffers
#define glDeleteFramebuffers lumen_glDeleteFramebuffers
#define glBindFramebuffer lumen_glBindFramebuffer
#define glFramebufferTexture2D lumen_glFramebufferTexture2D
#define glFramebufferRenderbuffer lumen_glFramebufferRenderbuffer
#define glCheckFramebufferStatus lumen_glCheckFramebufferStatus
#define glGenRenderbuffers lumen_glGenRenderbuffers
#define glDeleteRenderbuffers lumen_glDeleteRenderbuffers
#define glBindRenderbuffer lumen_glBindRenderbuffer
#define glRenderbufferStorage lumen_glRenderbufferStorage
#define glGenBuffers lumen_glGenBuffers
#define glDeleteBuffers lumen_glDeleteBuffers
#define glBindBuffer lumen_glBindBuffer
#define glBufferData lumen_glBufferData
#define glGenVertexArrays lumen_glGenVertexArrays
#define glDeleteVertexArrays lumen_glDeleteVertexArrays
#define glBindVertexArray lumen_glBindVertexArray
#define glEnableVertexAttribArray lumen_glEnableVertexAttribArray
#define glVertexAttribPointer lumen_glVertexAttribPointer
#define glDrawArrays lumen_glDrawArrays
#define glDrawElements lumen_glDrawElements
#define glCreateShader lumen_glCreateShader
#define glShaderSource lumen_glShaderSource
#define glCompileShader lumen_glCompileShader
#define glGetShaderiv lumen_glGetShaderiv
#define glGetShaderInfoLog lumen_glGetShaderInfoLog
#define glDeleteShader lumen_glDeleteShader
#define glCreateProgram lumen_glCreateProgram
#define glAttachShader lumen_glAttachShader
#define glBindAttribLocation lumen_glBindAttribLocation
#define glLinkProgram lumen_glLinkProgram
#define glGetProgramiv lumen_glGetProgramiv
#define glGetProgramInfoLog lumen_glGetProgramInfoLog
#define glDeleteProgram lumen_glDeleteProgram
#define glUseProgram lumen_glUseProgram
#define glGetUniformLocation lumen_glGetUniformLocation
#define glUniform1i lumen_glUniform1i
#define glUniform1f lumen_glUniform1f
#define glUniform2f lumen_glUniform2f
#define glUniform3f lumen_glUniform3f
#define glUniform4f lumen_glUniform4f
#define glFenceSync lumen_glFenceSync
#define glWaitSync lumen_glWaitSync
#define glDeleteSync lumen_glDeleteSync
#define glFlush lumen_glFlush

namespace gl {
// Must be called with a current context. Returns false if a function is missing.
bool load();
} // namespace gl
