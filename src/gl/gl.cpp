#include "gl/gl.h"

#include <GLFW/glfw3.h>
#include <cstdio>
#include <cstring>

#define LUMEN_GL_DEFINE(type, name) type lumen_##name = nullptr;
LUMEN_GL_FUNCS(LUMEN_GL_DEFINE)
#undef LUMEN_GL_DEFINE

namespace gl {

bool load() {
  bool ok = true;
#define LUMEN_GL_LOAD(type, name)                                              \
  lumen_##name = reinterpret_cast<type>(glfwGetProcAddress(#name));            \
  if (!lumen_##name) {                                                         \
    std::fprintf(stderr, "[gl] missing function %s\n", #name);                 \
    ok = false;                                                                \
  }
  LUMEN_GL_FUNCS(LUMEN_GL_LOAD)
#undef LUMEN_GL_LOAD
  return ok;
}

bool hasExtension(const char *name) {
  GLint n = 0;
  glGetIntegerv(GL_NUM_EXTENSIONS, &n);
  for (GLint i = 0; i < n; ++i) {
    const char *e = reinterpret_cast<const char *>(glGetStringi(GL_EXTENSIONS, GLuint(i)));
    if (e && std::strcmp(e, name) == 0) return true;
  }
  return false;
}

} // namespace gl
