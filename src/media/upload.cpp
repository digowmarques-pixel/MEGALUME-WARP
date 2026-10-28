#include "media/upload.h"

#include <cstdio>
#include <memory>

namespace lumen {

namespace {

const char *kConvertFS = R"(#version 330 core
in vec2 vUV;
out vec4 fragColor;
uniform sampler2D uP0;
uniform sampler2D uP1;
uniform sampler2D uP2;
uniform int uLayout;     // 1 planar YUV, 2 NV12, 3 compressed RGB(A), 4 YCoCg, 5 YCoCg + alpha
uniform float uScale;    // sample scale for 10/12-bit content stored in 16-bit
uniform int uMatrix;     // 0 BT.601, 1 BT.709, 2 BT.2020
uniform int uFull;       // full (JPEG) range
uniform vec2 uUVScale;   // image size / padded texture size (HAP)

vec3 yuvToRgb(float y, float u, float v) {
  if (uFull == 0) {
    y = (y - 16.0 / 255.0) * (255.0 / 219.0);
    u = (u - 128.0 / 255.0) * (255.0 / 224.0);
    v = (v - 128.0 / 255.0) * (255.0 / 224.0);
  } else {
    u -= 0.5;
    v -= 0.5;
  }
  if (uMatrix == 0) return vec3(y + 1.402 * v, y - 0.344136 * u - 0.714136 * v, y + 1.772 * u);
  if (uMatrix == 2) return vec3(y + 1.4746 * v, y - 0.164553 * u - 0.571353 * v, y + 1.8814 * u);
  return vec3(y + 1.5748 * v, y - 0.187324 * u - 0.468124 * v, y + 1.8556 * u);
}

void main() {
  vec2 uv = vUV * uUVScale;
  if (uLayout == 1) {
    float y = texture(uP0, uv).r * uScale;
    float u = texture(uP1, uv).r * uScale;
    float v = texture(uP2, uv).r * uScale;
    fragColor = vec4(clamp(yuvToRgb(y, u, v), 0.0, 1.0), 1.0);
  } else if (uLayout == 2) {
    float y = texture(uP0, uv).r * uScale;
    vec2 c = texture(uP1, uv).rg * uScale;
    fragColor = vec4(clamp(yuvToRgb(y, c.x, c.y), 0.0, 1.0), 1.0);
  } else if (uLayout == 3) {
    fragColor = texture(uP0, uv);
  } else {
    // HAP Q: scaled YCoCg in DXT5 (see the Vidvox reference shader).
    vec4 c = texture(uP0, uv);
    float scale = c.b * (255.0 / 8.0) + 1.0;
    float co = (c.r - 0.5 * 256.0 / 255.0) / scale;
    float cg = (c.g - 0.5 * 256.0 / 255.0) / scale;
    float y = c.a;
    float a = uLayout == 5 ? texture(uP1, uv).r : 1.0;
    fragColor = vec4(clamp(vec3(y + co - cg, y + cg, y - co - cg), 0.0, 1.0), a);
  }
}
)";

Shader &converter() {
  static std::unique_ptr<Shader> s;
  if (!s) {
    s = std::make_unique<Shader>();
    s->build(kPixelVertexShader, kConvertFS);
  }
  return *s;
}

int pad4(int v) { return (v + 3) & ~3; }

} // namespace

bool FrameUploader::supportsS3tc() {
  static int cached = -1;
  if (cached < 0)
    cached = (gl::hasExtension("GL_EXT_texture_compression_s3tc") ||
              gl::hasExtension("GL_ANGLE_texture_compression_dxt5"))
                 ? 1
                 : 0;
  return cached == 1;
}

void FrameUploader::send(StreamPlane *planes, int count) {
  size_t total = 0;
  for (int i = 0; i < count; ++i)
    total += planes[i].compressedBytes ? planes[i].compressedBytes
                                       : size_t(planes[i].stride ? planes[i].stride : planes[i].width) * size_t(planes[i].height);
  // Below ~0.5 MB a direct upload is cheaper than managing buffers.
  if (total >= (512u << 10) && stream_.upload(planes, count)) return;
  for (int i = 0; i < count; ++i) {
    StreamPlane &p = planes[i];
    if (p.compressedBytes) p.texture->uploadCompressed(p.data, p.width, p.height, p.compressedBytes, p.internalFormat);
    else p.texture->uploadPlane(p.data, p.width, p.height, p.stride, p.internalFormat);
  }
}

const Texture *FrameUploader::upload(const VideoFrame &f, DynamicMesh &mesh) {
  if (f.width <= 0 || f.height <= 0) return nullptr;
  auto plane = [&](int i, int w, int h, int stride, GLenum fmt) {
    StreamPlane p;
    p.data = f.plane[i].data();
    p.width = w;
    p.height = h;
    p.stride = stride;
    p.internalFormat = fmt;
    p.texture = &planes_[i];
    return p;
  };
  if (f.layout == FrameLayout::RGBA) {
    StreamPlane p = plane(0, f.width, f.height, f.stride[0] ? f.stride[0] : f.width * 4, GL_RGBA8);
    send(&p, 1);
    return &planes_[0];
  }

  int layout = 1;
  float uvx = 1, uvy = 1;
  switch (f.layout) {
  case FrameLayout::Planar: {
    GLenum fmt = f.wide ? GL_R16 : GL_R8;
    int bpp = f.wide ? 2 : 1;
    int cw = (f.width + (1 << f.chromaShiftX) - 1) >> f.chromaShiftX;
    int ch = (f.height + (1 << f.chromaShiftY) - 1) >> f.chromaShiftY;
    StreamPlane p[3] = {plane(0, f.width, f.height, f.stride[0] ? f.stride[0] : f.width * bpp, fmt),
                        plane(1, cw, ch, f.stride[1] ? f.stride[1] : cw * bpp, fmt),
                        plane(2, cw, ch, f.stride[2] ? f.stride[2] : cw * bpp, fmt)};
    send(p, 3);
    layout = 1;
    break;
  }
  case FrameLayout::NV12: {
    int bpp = f.wide ? 2 : 1;
    int cw = (f.width + 1) / 2, ch = (f.height + 1) / 2;
    StreamPlane p[2] = {plane(0, f.width, f.height, f.stride[0] ? f.stride[0] : f.width * bpp, f.wide ? GL_R16 : GL_R8),
                        plane(1, cw, ch, f.stride[1] ? f.stride[1] : cw * 2 * bpp, f.wide ? GL_RG16 : GL_RG8)};
    send(p, 2);
    layout = 2;
    break;
  }
  default: { // HAP block-compressed
    int pw = pad4(f.width), ph = pad4(f.height);
    size_t blocks = size_t(pw / 4) * size_t(ph / 4);
    bool dxt1 = f.layout == FrameLayout::DXT1;
    size_t need = blocks * (dxt1 ? 8 : 16);
    if (f.plane[0].size() < need) return out_.width() ? &out_.texture() : nullptr;
    StreamPlane p[2];
    p[0] = plane(0, pw, ph, 0, dxt1 ? GL_COMPRESSED_RGB_S3TC_DXT1_EXT : GL_COMPRESSED_RGBA_S3TC_DXT5_EXT);
    p[0].compressedBytes = need;
    int count = 1;
    layout = (f.layout == FrameLayout::DXT1 || f.layout == FrameLayout::DXT5) ? 3 : 4;
    if (f.layout == FrameLayout::YCoCgDXT5A && f.plane[1].size() >= blocks * 8) {
      p[1] = plane(1, pw, ph, 0, GL_COMPRESSED_RED_RGTC1);
      p[1].compressedBytes = blocks * 8;
      count = 2;
      layout = 5;
    }
    send(p, count);
    uvx = float(f.width) / float(pw);
    uvy = float(f.height) / float(ph);
    break;
  }
  }

  out_.resize(f.width, f.height);
  out_.bind();
  glDisable(GL_BLEND);
  Shader &s = converter();
  s.use();
  s.set("uTarget", float(f.width), float(f.height));
  s.set("uP0", 0);
  s.set("uP1", 1);
  s.set("uP2", 2);
  s.set("uLayout", layout);
  s.set("uScale", f.valueScale);
  s.set("uMatrix", f.matrix);
  s.set("uFull", f.fullRange ? 1 : 0);
  s.set("uUVScale", uvx, uvy);
  for (int i = 0; i < 3; ++i)
    if (!planes_[i].empty()) planes_[i].bind(i);
  // Flip V: output follows the "row 0 = top" convention of uploaded images.
  mesh.drawQuad(0, 0, float(f.width), float(f.height), 0, 1, 1, 0);
  glActiveTexture(GL_TEXTURE0);
  Framebuffer::unbind();
  return &out_.texture();
}

} // namespace lumen
