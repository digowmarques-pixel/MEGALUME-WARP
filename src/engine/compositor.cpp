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
uniform sampler2D uMatte; // layer above used as a matte
uniform int uMatteMode;   // 0 none, 1 alpha, 2 alpha inverted, 3 luma, 4 luma inverted

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
  // A mirrored crop (u0 > u1) has a negative extent: keep its sign.
  vec2 ext = uCrop.zw - uCrop.xy;
  ext = vec2(abs(ext.x) < 1e-5 ? 1e-5 : ext.x, abs(ext.y) < 1e-5 ? 1e-5 : ext.y);
  vec2 local = (vUV - uCrop.xy) / ext;
  vec2 fw = max(fwidth(local), vec2(1e-6));
  vec2 edge = min(local, 1.0 - local) / fw;
  float aa = clamp(min(edge.x, edge.y) + 0.5, 0.0, 1.0);

  float a = c.a * uOpacity * aa;
  if (uUseMask != 0) a *= texture(uMask, gl_FragCoord.xy / uCanvas).r;
  if (uMatteMode != 0) {
    vec4 m = texture(uMatte, gl_FragCoord.xy / uCanvas);
    float v = uMatteMode <= 2 ? m.a : dot(m.rgb, vec3(0.2126, 0.7152, 0.0722));
    if (uMatteMode == 2 || uMatteMode == 4) v = 1.0 - v;
    a *= v;
  }
  fragColor = vec4(rgb * a, a); // premultiplied
}
)";

bool Compositor::init() {
  return layerShader_.build(kPixelVertexShader, kLayerFS) &&
         masks_.init();
}

void Compositor::renderMask(const Layer &l, int w, int h, DynamicMesh &mesh) {
  auto &fb = maskFbs_[l.id];
  if (!fb) fb = std::make_unique<Framebuffer>();
  masks_.render(l.masks, 1, 1, w, h, *fb, mesh);
}

void Compositor::render(const Project &p, int canvasId, const std::vector<LayerDraw> &draws,
                        DynamicMesh &mesh, float master) {
  int W = p.canvasWidth(canvasId), H = p.canvasHeight(canvasId);

  // Masks first: they need their own framebuffer bindings.
  for (auto &d : draws) {
    if (MaskRenderer::anyActive(d.layer->masks)) renderMask(*d.layer, W, H, mesh);
    else maskFbs_.erase(d.layer->id);
  }

  const Shader &s = layerShader_;
  std::vector<float> verts;
  // asMatte: drawn alone into a matte buffer, Normal blend and no master (the
  // master already dims the layer that the matte cuts; twice would square it).
  auto drawLayer = [&](const LayerDraw &d, bool asMatte) {
    const Layer &l = *d.layer;
    if (!d.texture || d.texture->empty()) return;
    switch (asMatte ? BlendMode::Normal : l.blend) {
    case BlendMode::Add: glBlendFunc(GL_ONE, GL_ONE); break;
    case BlendMode::Multiply: glBlendFunc(GL_DST_COLOR, GL_ONE_MINUS_SRC_ALPHA); break;
    case BlendMode::Screen: glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_COLOR); break;
    default: glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA); break;
    }
    d.texture->bind(0);
    auto mask = maskFbs_.find(l.id);
    if (mask != maskFbs_.end()) mask->second->texture().bind(1);
    s.set("uUseMask", mask != maskFbs_.end() ? 1 : 0);
    s.set("uOpacity", std::clamp(l.opacity * d.opacity * (asMatte ? 1.0f : master), 0.0f, 1.0f));
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
  };
  auto setup = [&]() {
    glEnable(GL_BLEND);
    glBlendEquation(GL_FUNC_ADD);
    s.use();
    s.set("uTarget", float(W), float(H));
    s.set("uCanvas", float(W), float(H));
    s.set("uTex", 0);
    s.set("uMask", 1);
    s.set("uMatte", 2);
    s.set("uMatteMode", 0);
  };

  // Matte layers are drawn alone first (an empty matte hides everything).
  std::map<int, Framebuffer *> mattes;
  for (auto &l : p.layers)
    if (l.canvasId == canvasId && l.visible && isMatte(l.blend)) {
      auto &fb = matteFbs_[l.id];
      if (!fb) fb = std::make_unique<Framebuffer>();
      fb->resize(W, H);
      fb->bind();
      glClearColor(0, 0, 0, 0);
      glClear(GL_COLOR_BUFFER_BIT);
      setup();
      for (auto &d : draws)
        if (d.layer->id == l.id) drawLayer(d, true);
      mattes[l.id] = fb.get();
    }
  // The matte of a layer is the layer just above it on the same canvas.
  auto matteFor = [&](const Layer &l, int &mode) -> Framebuffer * {
    const Layer *above = nullptr;
    for (auto &x : p.layers) {
      if (&x == &l) break;
      if (x.canvasId == canvasId) above = &x;
    }
    if (!above || !isMatte(above->blend)) return nullptr;
    auto it = mattes.find(above->id);
    if (it == mattes.end()) return nullptr;
    mode = int(above->blend) - int(BlendMode::AlphaMatte) + 1;
    return it->second;
  };

  canvas_.resize(W, H);
  canvas_.bind();
  const Color bg = p.canvasBackground(canvasId);
  glClearColor(bg.r * bg.a, bg.g * bg.a, bg.b * bg.a, bg.a);
  glClear(GL_COLOR_BUFFER_BIT);
  setup();
  for (auto &d : draws) {
    if (isMatte(d.layer->blend)) continue;
    int mode = 0;
    Framebuffer *matte = matteFor(*d.layer, mode);
    if (matte) matte->texture().bind(2);
    s.set("uMatteMode", matte ? mode : 0);
    drawLayer(d, false);
  }
  glActiveTexture(GL_TEXTURE0);
  glDisable(GL_BLEND);
  Framebuffer::unbind();
}

} // namespace lumen
