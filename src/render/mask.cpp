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

static const char *kApplyFS = R"(#version 330 core
in vec2 vUV;
out vec4 fragColor;
uniform sampler2D uTex;  // coverage of one mask
uniform float uValue;    // 1 = show, 0 = hide
uniform float uOpacity;
void main() {
  float c = texture(uTex, gl_FragCoord.xy / vec2(textureSize(uTex, 0))).r;
  fragColor = vec4(vec3(uValue), c * uOpacity);
}
)";

bool MaskRenderer::init() {
  return fill_.build(kPixelVertexShader, kFillFS) && blur_.build(kPixelVertexShader, kBlurFS) &&
         apply_.build(kPixelVertexShader, kApplyFS);
}

bool MaskRenderer::anyActive(const std::vector<Mask> &masks) {
  return std::any_of(masks.begin(), masks.end(),
                     [](const Mask &m) { return m.enabled && m.points.size() >= 3 && m.opacity > 0; });
}

void MaskRenderer::render(const std::vector<Mask> &masks, float sx, float sy, int w, int h,
                          Framebuffer &fb, DynamicMesh &mesh) {
  fb.resize(w, h, false, GL_R8);
  cover_.resize(w, h, true, GL_R8);

  // The first mask decides the start: Show starts from nothing, Hide from everything.
  float base = 1;
  for (auto &m : masks)
    if (m.enabled && m.points.size() >= 3 && m.opacity > 0) { base = m.subtract ? 1.0f : 0.0f; break; }
  fb.bind();
  glClearColor(base, base, base, 1);
  glClear(GL_COLOR_BUFFER_BIT);

  std::vector<float> fan;
  for (auto &m : masks) {
    if (!m.enabled || m.points.size() < 3 || m.opacity <= 0) continue;
    // 1. Coverage of this mask (even-odd fill of any polygon with the stencil invert trick).
    cover_.bind();
    glDisable(GL_BLEND);
    float outside = m.inverted ? 1.0f : 0.0f;
    glClearColor(outside, outside, outside, 1);
    glClearStencil(0);
    glClear(GL_COLOR_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
    fill_.use();
    fill_.set("uTarget", float(w), float(h));
    glEnable(GL_STENCIL_TEST);
    glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
    glStencilMask(0xFF);
    glStencilFunc(GL_ALWAYS, 0, 0xFF);
    glStencilOp(GL_KEEP, GL_KEEP, GL_INVERT);
    fan.clear();
    for (auto &pt : maskOutline(m)) fan.insert(fan.end(), {pt.x * sx, pt.y * sy, 0, 0});
    mesh.draw(fan, GL_TRIANGLE_FAN);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glStencilFunc(GL_NOTEQUAL, 0, 0xFF);
    glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);
    fill_.set("uColor", 1 - outside, 1 - outside, 1 - outside, 1.0f);
    mesh.drawQuad(0, 0, float(w), float(h));
    glDisable(GL_STENCIL_TEST);

    // 2. Its own feather (separable gaussian).
    if (m.feather > 0.5f) {
      tmp_.resize(w, h, false, GL_R8);
      float step = std::max(1.0f, m.feather / 12.0f);
      blur_.use();
      blur_.set("uTarget", float(w), float(h));
      blur_.set("uTex", 0);
      blur_.set("uSigma", m.feather / step / 2.0f);
      tmp_.bind();
      cover_.texture().bind(0);
      blur_.set("uDir", step / float(w), 0.0f);
      mesh.drawQuad(0, 0, float(w), float(h));
      cover_.bind();
      tmp_.texture().bind(0);
      blur_.set("uDir", 0.0f, step / float(h));
      mesh.drawQuad(0, 0, float(w), float(h));
    }

    // 3. Applied over the result: Show pulls towards 1, Hide towards 0.
    fb.bind();
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    apply_.use();
    apply_.set("uTarget", float(w), float(h));
    apply_.set("uTex", 0);
    apply_.set("uValue", m.subtract ? 0.0f : 1.0f);
    apply_.set("uOpacity", std::clamp(m.opacity, 0.0f, 1.0f));
    cover_.texture().bind(0);
    mesh.drawQuad(0, 0, float(w), float(h));
    glDisable(GL_BLEND);
  }
  Framebuffer::unbind();
}

} // namespace lumen
