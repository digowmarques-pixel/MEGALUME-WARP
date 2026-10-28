// Thin RAII wrappers around OpenGL objects.
#pragma once

#include "gl/gl.h"

#include <string>
#include <vector>

namespace lumen {

class Shader {
public:
  Shader() = default;
  ~Shader();
  Shader(const Shader &) = delete;
  Shader &operator=(const Shader &) = delete;

  // Attribute 0 = aPos (vec2), attribute 1 = aUV (vec2).
  bool build(const char *vertexSrc, const char *fragmentSrc, std::string *error = nullptr);
  void use() const { glUseProgram(prog_); }
  GLint loc(const char *name) const { return glGetUniformLocation(prog_, name); }
  void set(const char *n, int v) const { glUniform1i(loc(n), v); }
  void set(const char *n, float v) const { glUniform1f(loc(n), v); }
  void set(const char *n, float a, float b) const { glUniform2f(loc(n), a, b); }
  void set(const char *n, float a, float b, float c) const { glUniform3f(loc(n), a, b, c); }
  void set(const char *n, float a, float b, float c, float d) const { glUniform4f(loc(n), a, b, c, d); }
  bool valid() const { return prog_ != 0; }
  void swap(Shader &o) { GLuint t = prog_; prog_ = o.prog_; o.prog_ = t; }

private:
  GLuint prog_ = 0;
};

class Texture {
public:
  Texture() = default;
  ~Texture();
  Texture(const Texture &) = delete;
  Texture &operator=(const Texture &) = delete;

  void allocate(int w, int h, GLenum internalFormat = GL_RGBA8);
  void upload(const void *rgba, int w, int h, int strideBytes = 0);
  // One plane of a video frame: GL_R8 / GL_RG8 / GL_R16 / GL_RG16 / GL_RGBA8.
  void uploadPlane(const void *data, int w, int h, int strideBytes, GLenum internalFormat);
  // Block-compressed data (BC1 / BC3 / BC4), dimensions padded to multiples of 4.
  void uploadCompressed(const void *data, int w, int h, size_t bytes, GLenum internalFormat);
  void bind(int unit = 0) const;
  // Refers to a texture owned elsewhere (e.g. Spout); never deleted by this object.
  void wrap(GLuint id, int w, int h) { if (owned_ && id_) release(); id_ = id; w_ = w; h_ = h; owned_ = false; }
  GLuint id() const { return id_; }
  int width() const { return w_; }
  int height() const { return h_; }
  bool empty() const { return id_ == 0 || w_ == 0; }

private:
  void release();
  GLuint id_ = 0;
  int w_ = 0, h_ = 0;
  GLenum fmt_ = GL_RGBA8;
  bool owned_ = true;
};

// Framebuffer with a colour texture and optional stencil buffer.
// Framebuffers are per-context objects: only use one from the context that created it.
class Framebuffer {
public:
  Framebuffer() = default;
  ~Framebuffer();
  Framebuffer(const Framebuffer &) = delete;
  Framebuffer &operator=(const Framebuffer &) = delete;

  void resize(int w, int h, bool withStencil = false, GLenum format = GL_RGBA8);
  void bind() const;
  static void unbind();
  Texture &texture() { return tex_; }
  const Texture &texture() const { return tex_; }
  int width() const { return tex_.width(); }
  int height() const { return tex_.height(); }
  void readPixels(std::vector<unsigned char> &rgba) const;
  GLuint id() const { return fbo_; }

private:
  GLuint fbo_ = 0, rbo_ = 0;
  Texture tex_;
};

// Interleaved (x, y, u, v) triangle list streamed every frame.
// VAOs are per-context: create one DynamicMesh per GL context.
class DynamicMesh {
public:
  DynamicMesh() = default;
  ~DynamicMesh();
  DynamicMesh(const DynamicMesh &) = delete;
  DynamicMesh &operator=(const DynamicMesh &) = delete;

  void draw(const std::vector<float> &xyuv, GLenum mode = GL_TRIANGLES);
  void drawQuad(float x0, float y0, float x1, float y1, float u0 = 0, float v0 = 0,
                float u1 = 1, float v1 = 1);

private:
  void init();
  GLuint vao_ = 0, vbo_ = 0;
};

// Common vertex shader: positions are in pixels of the current target, Y down.
extern const char *kPixelVertexShader;

} // namespace lumen
