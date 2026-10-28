#include "render/gpu.h"

#include <cstdio>

namespace lumen {

const char *kPixelVertexShader = R"(#version 330 core
layout(location = 0) in vec2 aPos;
layout(location = 1) in vec2 aUV;
uniform vec2 uTarget; // target size in pixels
out vec2 vUV;
void main() {
  vUV = aUV;
  vec2 ndc = aPos / uTarget * 2.0 - 1.0;
  gl_Position = vec4(ndc.x, -ndc.y, 0.0, 1.0);
}
)";

static GLuint compile(GLenum type, const char *src, std::string *error) {
  GLuint s = glCreateShader(type);
  glShaderSource(s, 1, &src, nullptr);
  glCompileShader(s);
  GLint ok = 0;
  glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
  if (!ok) {
    char log[4096];
    glGetShaderInfoLog(s, sizeof(log), nullptr, log);
    if (error) *error = log;
    std::fprintf(stderr, "[shader] compile error:\n%s\n", log);
    glDeleteShader(s);
    return 0;
  }
  return s;
}

Shader::~Shader() {
  if (prog_) glDeleteProgram(prog_);
}

bool Shader::build(const char *vs, const char *fs, std::string *error) {
  GLuint v = compile(GL_VERTEX_SHADER, vs, error);
  if (!v) return false;
  GLuint f = compile(GL_FRAGMENT_SHADER, fs, error);
  if (!f) { glDeleteShader(v); return false; }
  GLuint p = glCreateProgram();
  glAttachShader(p, v);
  glAttachShader(p, f);
  glBindAttribLocation(p, 0, "aPos");
  glBindAttribLocation(p, 1, "aUV");
  glLinkProgram(p);
  glDeleteShader(v);
  glDeleteShader(f);
  GLint ok = 0;
  glGetProgramiv(p, GL_LINK_STATUS, &ok);
  if (!ok) {
    char log[4096];
    glGetProgramInfoLog(p, sizeof(log), nullptr, log);
    if (error) *error = log;
    std::fprintf(stderr, "[shader] link error:\n%s\n", log);
    glDeleteProgram(p);
    return false;
  }
  if (prog_) glDeleteProgram(prog_);
  prog_ = p;
  return true;
}

Texture::~Texture() {
  if (owned_ && id_) release();
}

void Texture::release() {
  glDeleteTextures(1, &id_);
  id_ = 0;
  w_ = h_ = 0;
}

static void pixelFormat(GLenum internal, GLenum &format, GLenum &type, int &bytesPerPixel) {
  switch (internal) {
  case GL_R8: format = GL_RED; type = GL_UNSIGNED_BYTE; bytesPerPixel = 1; break;
  case GL_RG8: format = GL_RG; type = GL_UNSIGNED_BYTE; bytesPerPixel = 2; break;
  case GL_R16: format = GL_RED; type = GL_UNSIGNED_SHORT; bytesPerPixel = 2; break;
  case GL_RG16: format = GL_RG; type = GL_UNSIGNED_SHORT; bytesPerPixel = 4; break;
  case GL_RGBA16F: format = GL_RGBA; type = GL_HALF_FLOAT; bytesPerPixel = 8; break;
  case GL_RGBA32F: format = GL_RGBA; type = GL_FLOAT; bytesPerPixel = 16; break;
  default: format = GL_RGBA; type = GL_UNSIGNED_BYTE; bytesPerPixel = 4; break;
  }
}

void Texture::allocate(int w, int h, GLenum fmt) {
  if (id_ && w == w_ && h == h_ && fmt == fmt_) return;
  if (!id_) glGenTextures(1, &id_);
  w_ = w; h_ = h; fmt_ = fmt;
  // The active unit may hold a texture a shader is about to sample (a render
  // target resized between binding the inputs and drawing): put it back, or
  // that pass would read its own target (black frames in ISF effects).
  GLint previous = 0;
  glGetIntegerv(GL_TEXTURE_BINDING_2D, &previous);
  glBindTexture(GL_TEXTURE_2D, id_);
  GLenum ext, type;
  int bpp;
  pixelFormat(fmt, ext, type, bpp);
  glTexImage2D(GL_TEXTURE_2D, 0, GLint(fmt), w, h, 0, ext, type, nullptr);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glBindTexture(GL_TEXTURE_2D, GLuint(previous));
}

void Texture::upload(const void *rgba, int w, int h, int stride) {
  allocate(w, h, GL_RGBA8);
  glBindTexture(GL_TEXTURE_2D, id_);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
  glPixelStorei(GL_UNPACK_ROW_LENGTH, stride ? stride / 4 : 0);
  glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
  glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
}

void Texture::uploadPlane(const void *data, int w, int h, int stride, GLenum internal) {
  allocate(w, h, internal);
  GLenum format, type;
  int bpp;
  pixelFormat(internal, format, type, bpp);
  glBindTexture(GL_TEXTURE_2D, id_);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  glPixelStorei(GL_UNPACK_ROW_LENGTH, stride ? stride / bpp : 0);
  glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, format, type, data);
  glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
}

void Texture::uploadCompressed(const void *data, int w, int h, size_t bytes, GLenum internal) {
  if (id_ && w == w_ && h == h_ && fmt_ == internal) { // same storage: update in place
    glBindTexture(GL_TEXTURE_2D, id_);
    glCompressedTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, internal, GLsizei(bytes), data);
    return;
  }
  if (!id_) glGenTextures(1, &id_);
  glBindTexture(GL_TEXTURE_2D, id_);
  glCompressedTexImage2D(GL_TEXTURE_2D, 0, internal, w, h, 0, GLsizei(bytes), data);
  if (w != w_ || h != h_ || fmt_ != internal) {
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  }
  w_ = w; h_ = h; fmt_ = internal;
}

void Texture::bind(int unit) const {
  glActiveTexture(GL_TEXTURE0 + unit);
  glBindTexture(GL_TEXTURE_2D, id_);
}

Framebuffer::~Framebuffer() {
  if (rbo_) glDeleteRenderbuffers(1, &rbo_);
  if (fbo_) glDeleteFramebuffers(1, &fbo_);
}

void Framebuffer::resize(int w, int h, bool stencil, GLenum format) {
  if (fbo_ && w == width() && h == height()) return;
  tex_.allocate(w, h, format);
  if (!fbo_) glGenFramebuffers(1, &fbo_);
  glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex_.id(), 0);
  if (stencil) {
    if (!rbo_) glGenRenderbuffers(1, &rbo_);
    glBindRenderbuffer(GL_RENDERBUFFER, rbo_);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, w, h);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, rbo_);
  }
  if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
    std::fprintf(stderr, "[gl] framebuffer %dx%d incomplete\n", w, h);
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void Framebuffer::bind() const {
  glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
  glViewport(0, 0, width(), height());
}

void Framebuffer::unbind() { glBindFramebuffer(GL_FRAMEBUFFER, 0); }

void Framebuffer::readPixels(std::vector<unsigned char> &rgba) const {
  rgba.resize(size_t(width()) * size_t(height()) * 4);
  glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
  glPixelStorei(GL_PACK_ALIGNMENT, 1);
  glReadPixels(0, 0, width(), height(), GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

DynamicMesh::~DynamicMesh() {
  if (vbo_) glDeleteBuffers(1, &vbo_);
  if (vao_) glDeleteVertexArrays(1, &vao_);
}

void DynamicMesh::init() {
  glGenVertexArrays(1, &vao_);
  glGenBuffers(1, &vbo_);
  glBindVertexArray(vao_);
  glBindBuffer(GL_ARRAY_BUFFER, vbo_);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), nullptr);
  glEnableVertexAttribArray(1);
  glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float),
                        reinterpret_cast<void *>(2 * sizeof(float)));
}

void DynamicMesh::draw(const std::vector<float> &v, GLenum mode) {
  if (v.empty()) return;
  if (!vao_) init();
  glBindVertexArray(vao_);
  glBindBuffer(GL_ARRAY_BUFFER, vbo_);
  // Re-specifying the store each frame lets the driver orphan the old one.
  glBufferData(GL_ARRAY_BUFFER, GLsizeiptr(v.size() * sizeof(float)), v.data(), GL_STREAM_DRAW);
  glDrawArrays(mode, 0, GLsizei(v.size() / 4));
  glBindVertexArray(0);
}

void DynamicMesh::drawQuad(float x0, float y0, float x1, float y1, float u0, float v0,
                           float u1, float v1) {
  std::vector<float> q = {x0, y0, u0, v0, x1, y0, u1, v0, x1, y1, u1, v1,
                          x0, y0, u0, v0, x1, y1, u1, v1, x0, y1, u0, v1};
  draw(q);
}

} // namespace lumen
