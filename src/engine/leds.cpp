#include "engine/leds.h"

#include <algorithm>
#include <cmath>
#include <random>

namespace lumen {

namespace {

// One fragment per LED: averages a 4x4 grid of canvas samples inside the
// LED's cell of the (perspective) area.
const char *kSampleFS = R"(#version 330 core
out vec4 fragColor;
uniform sampler2D uCanvas;
uniform mat3 uH;        // unit square -> canvas pixels
uniform vec2 uCanvasSize;
uniform vec2 uGrid;     // cols, rows
vec2 toCanvas(vec2 uv) {
  vec3 p = uH * vec3(uv, 1.0);
  vec2 c = p.xy / p.z;
  return vec2(c.x / uCanvasSize.x, 1.0 - c.y / uCanvasSize.y);
}
void main() {
  vec2 cell = floor(gl_FragCoord.xy); // row 0 = top (we read rows back in this order)
  vec4 sum = vec4(0.0);
  for (int j = 0; j < 4; ++j)
    for (int i = 0; i < 4; ++i) {
      vec2 uv = (cell + (vec2(i, j) + 0.5) / 4.0) / uGrid;
      sum += texture(uCanvas, toCanvas(uv));
    }
  fragColor = sum / 16.0;
}
)";

} // namespace

bool LedEngine::init() {
  // Random source id for sACN (stable for the session).
  std::random_device rd;
  for (auto &b : cid_) b = uint8_t(rd());
  return shader_.build(kPixelVertexShader, kSampleFS);
}

const std::vector<float> *LedEngine::colours(int id) const {
  auto it = samplers_.find(id);
  return it == samplers_.end() ? nullptr : &it->second->rgb;
}

void LedEngine::update(const Project &p, const Texture &canvas, DynamicMesh &mesh, double now) {
  // Drop samplers of deleted mappings.
  for (auto it = samplers_.begin(); it != samplers_.end();)
    it = p.ledMapping(it->first) ? std::next(it) : samplers_.erase(it);
  if (p.ledMappings.empty()) {
    universes_.clear();
    conflicts_.clear();
    return;
  }

  shader_.use();
  shader_.set("uCanvas", 0);
  shader_.set("uCanvasSize", float(std::max(1, p.canvasW)), float(std::max(1, p.canvasH)));
  canvas.bind(0);
  glDisable(GL_BLEND);
  for (auto &m : p.ledMappings) {
    auto &s = samplers_[m.id];
    if (!s) s = std::make_unique<Sampler>();
    int cols = std::clamp(m.cols, 1, 4096), rows = std::clamp(m.rows, 1, 4096);
    // 16-bit target so gradients stay smooth on 16-bit fixtures.
    s->fb.resize(cols, rows, false, GL_RGBA16);
    s->fb.bind();
    Mat3 h = Mat3::squareToQuad(m.area.corners);
    // GLSL matrices are column-major.
    float cm[9] = {h.m[0], h.m[3], h.m[6], h.m[1], h.m[4], h.m[7], h.m[2], h.m[5], h.m[8]};
    glUniformMatrix3fv(shader_.loc("uH"), 1, GL_FALSE, cm);
    shader_.set("uGrid", float(cols), float(rows));
    shader_.set("uTarget", float(cols), float(rows));
    mesh.drawQuad(0, 0, float(cols), float(rows));
    s->raw.resize(size_t(cols) * size_t(rows) * 4);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, cols, rows, GL_RGBA, GL_UNSIGNED_SHORT, s->raw.data());
    s->rgb.resize(size_t(cols) * size_t(rows) * 3);
    for (size_t i = 0, n = size_t(cols) * size_t(rows); i < n; ++i)
      for (int c = 0; c < 3; ++c) s->rgb[i * 3 + size_t(c)] = float(s->raw[i * 4 + size_t(c)]) / 65535.0f;
  }
  Framebuffer::unbind();
  send(p, now);
}

void LedEngine::send(const Project &p, double now) {
  // Rebuild every universe from scratch: channels no mapping uses stay at 0.
  universes_.clear();
  conflicts_.clear();
  std::map<std::pair<UniverseKey, int>, int> owner;

  for (auto &m : p.ledMappings) {
    const DmxDevice *dev = p.dmxDevice(m.deviceId);
    if (!dev) continue;
    auto it = samplers_.find(m.id);
    if (it == samplers_.end()) continue;
    const std::vector<float> &rgb = it->second->rgb;
    int cpp = ledChannelsPerPixel(m.format, m.wide);
    float gain = m.enabled ? std::clamp(m.brightness, 0.0f, 1.0f) * std::clamp(p.dmxMaster, 0.0f, 1.0f) : 0.0f;
    float gamma = std::max(0.1f, m.gamma);
    bool reported = false;
    for (const LedPixelSlot &slot : computeLedLayout(m.layout())) {
      UniverseKey key{dev->id, slot.universe};
      DmxUniverse &u = universes_.try_emplace(key, DmxUniverse{}).first->second;
      size_t i = size_t(slot.row * std::max(1, m.cols) + slot.col) * 3;
      float c[3] = {0, 0, 0};
      if (i + 2 < rgb.size())
        for (int k = 0; k < 3; ++k) c[k] = std::pow(rgb[i + size_t(k)], gamma) * gain;
      writeLedPixel(u, slot.channel, m.format, m.wide, c[0], c[1], c[2]);
      // Two mappings driving the same channel: tell the user once.
      for (int ch = slot.channel; ch < slot.channel + cpp && !reported; ++ch) {
        auto [pos, inserted] = owner.try_emplace({key, ch}, m.id);
        if (!inserted && pos->second != m.id) {
          const LedMapping *other = p.ledMapping(pos->second);
          conflicts_.push_back("\"" + m.name + "\" e \"" + (other ? other->name : "?") +
                               "\" usam o mesmo canal " + std::to_string(ch + 1) + " do universo " +
                               std::to_string(slot.universe));
          reported = true;
        }
      }
    }
  }

  for (auto &[key, data] : universes_) {
    const DmxDevice *dev = p.dmxDevice(key.deviceId);
    if (!dev || !dev->enabled) continue;
    double interval = 1.0 / std::clamp(dev->rate, 1, 60);
    double &last = lastSend_[dev->id * 65536 + key.universe];
    if (now - last < interval) continue;
    last = now;
    uint8_t &seq = sequence_[key];
    seq = uint8_t(seq + 1);
    if (dev->protocol == DmxProtocol::ArtNet) {
      if (seq == 0) seq = 1; // 0 means "sequencing disabled" in Art-Net
      auto pkt = encodeArtDmx(key.universe, seq, data);
      std::string host = dev->host.empty() ? "255.255.255.255" : dev->host;
      if (socket_.sendRaw(host, 6454, pkt.data(), pkt.size())) ++packetsSent_;
    } else {
      auto pkt = encodeSacn(key.universe, seq, data, cid_, "Lumen");
      std::string host = dev->host.empty() ? sacnMulticastAddress(key.universe) : dev->host;
      if (socket_.sendRaw(host, 5568, pkt.data(), pkt.size())) ++packetsSent_;
    }
  }
}

} // namespace lumen
