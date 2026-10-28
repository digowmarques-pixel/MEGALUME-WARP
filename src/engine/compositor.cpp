#include "engine/compositor.h"

#include <algorithm>
#include <cmath>

namespace lumen {

static const char *kLayerFS = R"(#version 330 core
in vec2 vUV;
out vec4 fragColor;
uniform sampler2D uTex;
uniform sampler2D uMask;
uniform int uUseMask;
uniform vec2 uCanvas;
uniform vec4 uCrop;      // u0 v0 u1 v1
uniform float uOpacity;
uniform float uBrightness;
uniform float uContrast;
uniform float uSaturation;
uniform float uHue;      // radians
uniform int uInvert;
uniform vec4 uTint;

vec3 hueRotate(vec3 c, float a) {
  const vec3 k = vec3(0.57735);
  float ca = cos(a);
  return c * ca + cross(k, c) * sin(a) + k * dot(k, c) * (1.0 - ca);
}

void main() {
  vec4 c = texture(uTex, vUV);
  vec3 rgb = c.rgb;
  if (uHue != 0.0) rgb = hueRotate(rgb, uHue);
  float luma = dot(rgb, vec3(0.2126, 0.7152, 0.0722));
  rgb = mix(vec3(luma), rgb, uSaturation);
  rgb = (rgb - 0.5) * uContrast + 0.5 + uBrightness;
  if (uInvert != 0) rgb = 1.0 - rgb;
  rgb = clamp(rgb * uTint.rgb, 0.0, 1.0);

  // Anti-aliased slice edges (the quad borders are otherwise jagged).
  vec2 local = (vUV - uCrop.xy) / max(uCrop.zw - uCrop.xy, vec2(1e-5));
  vec2 fw = max(fwidth(local), vec2(1e-6));
  vec2 edge = min(local, 1.0 - local) / fw;
  float aa = clamp(min(edge.x, edge.y) + 0.5, 0.0, 1.0);

  float a = c.a * uOpacity * aa;
  if (uUseMask != 0) a *= texture(uMask, gl_FragCoord.xy / uCanvas).r;
  fragColor = vec4(rgb * a, a); // premultiplied
}
)";

static const char *kFillFS = R"(#version 330 core
out vec4 fragColor;
uniform vec4 uColor;
void main() { fragColor = uColor; }
)";

static const char *kBlurFS = R"(#version 330 core
in vec2 vUV;
out vec4 fragColor;
uniform sampler2D uTex;
uniform vec2 uDir;     // texel step (already scaled)
uniform float uSigma;  // in taps
void main() {
  vec2 uv = gl_FragCoord.xy / vec2(textureSize(uTex, 0));
  float sum = 0.0, wsum = 0.0;
  for (int i = -12; i <= 12; ++i) {
    float w = exp(-0.5 * float(i * i) / (uSigma * uSigma));
    sum += texture(uTex, uv + uDir * float(i)).r * w;
    wsum += w;
  }
  fragColor = vec4(sum / wsum);
}
)";

Vec2 layerPivot(const Layer &l) {
  Vec2 c;
  int n = 0;
  for (auto &s : l.slices) {
    c += s.warp.center();
    ++n;
  }
  return n ? c * (1.0f / float(n)) : c;
}

Warp transformedWarp(const Layer &l, const Slice &s) {
  Warp w = s.warp;
  const LayerTransform &t = l.transform;
  if (t.scale == 1 && t.rotation == 0 && t.position.x == 0 && t.position.y == 0) return w;
  Vec2 pivot = layerPivot(l);
  if (t.scale != 1) w.scaleAround(pivot, t.scale, t.scale);
  if (t.rotation != 0) w.rotateAround(pivot, t.rotation * 3.14159265f / 180.0f);
  w.translate(t.position);
  return w;
}

bool Compositor::init() {
  return layerShader_.build(kPixelVertexShader, kLayerFS) &&
         fillShader_.build(kPixelVertexShader, kFillFS) &&
         blurShader_.build(kPixelVertexShader, kBlurFS);
}

void Compositor::renderMask(const Project &p, const Layer &l, DynamicMesh &mesh) {
  auto &fb = masks_[l.id];
  if (!fb) fb = std::make_unique<Framebuffer>();
  fb->resize(p.canvasW, p.canvasH, true, GL_R8);
  fb->bind();

  // Start empty if any mask reveals, otherwise start fully visible.
  bool anyReveal = false;
  float feather = 0;
  for (auto &m : l.masks)
    if (m.enabled && m.points.size() >= 3) {
      anyReveal |= !m.subtract;
      feather = std::max(feather, m.feather);
    }
  float base = anyReveal ? 0.0f : 1.0f;
  glClearColor(base, base, base, 1);
  glClearStencil(0);
  glClear(GL_COLOR_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
  glDisable(GL_BLEND);

  fillShader_.use();
  fillShader_.set("uTarget", float(p.canvasW), float(p.canvasH));
  glEnable(GL_STENCIL_TEST);
  std::vector<float> fan;
  for (auto &m : l.masks) {
    if (!m.enabled || m.points.size() < 3) continue;
    // Even-odd fill of an arbitrary (concave) polygon using the stencil invert trick.
    glClear(GL_STENCIL_BUFFER_BIT);
    glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
    glStencilMask(0xFF);
    glStencilFunc(GL_ALWAYS, 0, 0xFF);
    glStencilOp(GL_KEEP, GL_KEEP, GL_INVERT);
    fan.clear();
    for (auto &pt : m.points) fan.insert(fan.end(), {pt.x, pt.y, 0, 0});
    mesh.draw(fan, GL_TRIANGLE_FAN);

    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glStencilFunc(GL_NOTEQUAL, 0, 0xFF);
    glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);
    float v = m.subtract ? 0.0f : 1.0f;
    fillShader_.set("uColor", v, v, v, 1.0f);
    mesh.drawQuad(0, 0, float(p.canvasW), float(p.canvasH));
  }
  glDisable(GL_STENCIL_TEST);

  if (feather > 0.5f) {
    blurTmp_.resize(p.canvasW, p.canvasH, false, GL_R8);
    float step = std::max(1.0f, feather / 12.0f);
    blurShader_.use();
    blurShader_.set("uTarget", float(p.canvasW), float(p.canvasH));
    blurShader_.set("uTex", 0);
    blurShader_.set("uSigma", feather / step / 2.0f);
    blurTmp_.bind();
    fb->texture().bind(0);
    blurShader_.set("uDir", step / float(p.canvasW), 0.0f);
    mesh.drawQuad(0, 0, float(p.canvasW), float(p.canvasH));
    fb->bind();
    blurTmp_.texture().bind(0);
    blurShader_.set("uDir", 0.0f, step / float(p.canvasH));
    mesh.drawQuad(0, 0, float(p.canvasW), float(p.canvasH));
  }
  Framebuffer::unbind();
}

void Compositor::render(const Project &p, const std::vector<LayerDraw> &draws,
                        DynamicMesh &mesh, float master) {
  int W = std::max(16, p.canvasW), H = std::max(16, p.canvasH);

  // Masks first: they need their own framebuffer bindings.
  for (auto &d : draws) {
    bool hasMask = std::any_of(d.layer->masks.begin(), d.layer->masks.end(),
                               [](const Mask &m) { return m.enabled && m.points.size() >= 3; });
    if (hasMask) renderMask(p, *d.layer, mesh);
    else masks_.erase(d.layer->id);
  }

  canvas_.resize(W, H);
  canvas_.bind();
  const Color &bg = p.background;
  glClearColor(bg.r * bg.a, bg.g * bg.a, bg.b * bg.a, bg.a);
  glClear(GL_COLOR_BUFFER_BIT);
  glEnable(GL_BLEND);
  glBlendEquation(GL_FUNC_ADD);

  const Shader &s = layerShader_;
  s.use();
  s.set("uTarget", float(W), float(H));
  s.set("uCanvas", float(W), float(H));
  s.set("uTex", 0);
  s.set("uMask", 1);

  std::vector<float> verts;
  for (auto &d : draws) {
    const Layer &l = *d.layer;
    if (!d.texture || d.texture->empty()) continue;
    switch (l.blend) {
    case BlendMode::Normal: glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA); break;
    case BlendMode::Add: glBlendFunc(GL_ONE, GL_ONE); break;
    case BlendMode::Multiply: glBlendFunc(GL_DST_COLOR, GL_ONE_MINUS_SRC_ALPHA); break;
    case BlendMode::Screen: glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_COLOR); break;
    }
    d.texture->bind(0);
    auto mask = masks_.find(l.id);
    if (mask != masks_.end()) mask->second->texture().bind(1);
    s.set("uUseMask", mask != masks_.end() ? 1 : 0);
    s.set("uOpacity", std::clamp(l.opacity * d.opacity * master, 0.0f, 1.0f));
    s.set("uBrightness", l.fx.brightness);
    s.set("uContrast", l.fx.contrast);
    s.set("uSaturation", l.fx.saturation);
    s.set("uHue", l.fx.hue * 3.14159265f / 180.0f);
    s.set("uInvert", l.fx.invert ? 1 : 0);
    s.set("uTint", l.fx.tint.r, l.fx.tint.g, l.fx.tint.b, l.fx.tint.a);

    for (auto &slice : l.slices) {
      if (!slice.enabled) continue;
      Warp w = transformedWarp(l, slice);
      s.set("uCrop", slice.u0, slice.v0, slice.u1, slice.v1);
      verts.clear();
      // Fine tessellation keeps perspective-correct texturing across the quad.
      w.tessellate(w.hasMesh() ? 48 : 24, slice.u0, slice.v0, slice.u1, slice.v1, verts);
      mesh.draw(verts);
    }
  }
  glActiveTexture(GL_TEXTURE0);
  glDisable(GL_BLEND);
  Framebuffer::unbind();
}

} // namespace lumen
