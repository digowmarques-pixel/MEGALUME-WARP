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
         masks_.init();
}

void Compositor::renderMask(const Project &p, const Layer &l, DynamicMesh &mesh) {
  auto &fb = maskFbs_[l.id];
  if (!fb) fb = std::make_unique<Framebuffer>();
  masks_.render(l.masks, 1, 1, p.canvasW, p.canvasH, *fb, mesh);
}

void Compositor::render(const Project &p, const std::vector<LayerDraw> &draws,
                        DynamicMesh &mesh, float master) {
  int W = std::max(16, p.canvasW), H = std::max(16, p.canvasH);

  // Masks first: they need their own framebuffer bindings.
  for (auto &d : draws) {
    if (MaskRenderer::anyActive(d.layer->masks)) renderMask(p, *d.layer, mesh);
    else maskFbs_.erase(d.layer->id);
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
    auto mask = maskFbs_.find(l.id);
    if (mask != maskFbs_.end()) mask->second->texture().bind(1);
    s.set("uUseMask", mask != maskFbs_.end() ? 1 : 0);
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
