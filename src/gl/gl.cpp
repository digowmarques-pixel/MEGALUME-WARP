#include "gl/gl.h"

#include <GLFW/glfw3.h>
#include <cstdio>

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

} // namespace gl
