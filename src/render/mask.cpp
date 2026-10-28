#include "render/mask.h"

#include <algorithm>

namespace lumen {

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

bool MaskRenderer::init() {
  return fill_.build(kPixelVertexShader, kFillFS) && blur_.build(kPixelVertexShader, kBlurFS);
}

bool MaskRenderer::anyActive(const std::vector<Mask> &masks) {
  return std::any_of(masks.begin(), masks.end(), [](const Mask &m) { return m.enabled && m.points.size() >= 3; });
}

void MaskRenderer::render(const std::vector<Mask> &masks, float sx, float sy, int w, int h,
                          Framebuffer &fb, DynamicMesh &mesh) {
  fb.resize(w, h, true, GL_R8);
  fb.bind();

  // Start empty if any mask reveals, otherwise start fully visible.
  bool anyReveal = false;
  float feather = 0;
  for (auto &m : masks)
    if (m.enabled && m.points.size() >= 3) {
      anyReveal |= !m.subtract;
      feather = std::max(feather, m.feather);
    }
  float base = anyReveal ? 0.0f : 1.0f;
  glClearColor(base, base, base, 1);
  glClearStencil(0);
  glClear(GL_COLOR_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
  glDisable(GL_BLEND);

  fill_.use();
  fill_.set("uTarget", float(w), float(h));
  glEnable(GL_STENCIL_TEST);
  std::vector<float> fan;
  for (auto &m : masks) {
    if (!m.enabled || m.points.size() < 3) continue;
    // Even-odd fill of an arbitrary (concave) polygon using the stencil invert trick.
    glClear(GL_STENCIL_BUFFER_BIT);
    glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
    glStencilMask(0xFF);
    glStencilFunc(GL_ALWAYS, 0, 0xFF);
    glStencilOp(GL_KEEP, GL_KEEP, GL_INVERT);
    fan.clear();
    for (auto &pt : m.points) fan.insert(fan.end(), {pt.x * sx, pt.y * sy, 0, 0});
    mesh.draw(fan, GL_TRIANGLE_FAN);

    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glStencilFunc(GL_NOTEQUAL, 0, 0xFF);
    glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);
    float v = m.subtract ? 0.0f : 1.0f;
    fill_.set("uColor", v, v, v, 1.0f);
    mesh.drawQuad(0, 0, float(w), float(h));
  }
  glDisable(GL_STENCIL_TEST);

  if (feather > 0.5f) {
    tmp_.resize(w, h, false, GL_R8);
    float step = std::max(1.0f, feather / 12.0f);
    blur_.use();
    blur_.set("uTarget", float(w), float(h));
    blur_.set("uTex", 0);
    blur_.set("uSigma", feather / step / 2.0f);
    tmp_.bind();
    fb.texture().bind(0);
    blur_.set("uDir", step / float(w), 0.0f);
    mesh.drawQuad(0, 0, float(w), float(h));
    fb.bind();
    tmp_.texture().bind(0);
    blur_.set("uDir", 0.0f, step / float(h));
    mesh.drawQuad(0, 0, float(w), float(h));
  }
  Framebuffer::unbind();
}

} // namespace lumen
