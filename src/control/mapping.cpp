// OSC address space and user-defined input mappings. See docs/OSC.md.
#include "engine/engine.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <sstream>

namespace lumen {

static std::vector<std::string> splitPath(const std::string &address) {
  std::vector<std::string> parts;
  std::stringstream ss(address);
  for (std::string part; std::getline(ss, part, '/');)
    if (!part.empty()) parts.push_back(part);
  return parts;
}

static bool parseInt(const std::string &s, int &out) {
  if (s.empty()) return false;
  char *end = nullptr;
  long v = std::strtol(s.c_str(), &end, 10);
  if (*end) return false;
  out = int(v);
  return true;
}

// Layers are addressed by 1-based position from the top, or by name.
Layer *Engine::findLayer(const std::string &key) {
  int n;
  if (parseInt(key, n)) {
    if (n >= 1 && n <= int(project.layers.size())) return &project.layers[size_t(n - 1)];
    return nullptr;
  }
  for (auto &l : project.layers)
    if (l.name == key) return &l;
  return nullptr;
}

void Engine::setParam(Layer &l, MappingTarget t, float v) {
  switch (t) {
  case MappingTarget::Opacity: l.opacity = std::clamp(v, 0.0f, 1.0f); break;
  case MappingTarget::PositionX: l.transform.position.x = v; break;
  case MappingTarget::PositionY: l.transform.position.y = v; break;
  case MappingTarget::Scale: l.transform.scale = v; break;
  case MappingTarget::Rotation: l.transform.rotation = v; break;
  case MappingTarget::Brightness: l.fx.brightness = v; break;
  case MappingTarget::Contrast: l.fx.contrast = v; break;
  case MappingTarget::Saturation: l.fx.saturation = v; break;
  case MappingTarget::Hue: l.fx.hue = v; break;
  case MappingTarget::Volume: l.volume = std::clamp(v, 0.0f, 2.0f); break;
  case MappingTarget::Speed: {
    auto it = states_.find(l.id);
    if (it != states_.end() && it->second.current) it->second.current->speed = v;
    break;
  }
  default: break;
  }
}

bool Engine::handleOsc(const OscMessage &m) {
  auto p = splitPath(m.address);
  if (p.empty() || p[0] != "lumen") return false;
  float a0 = m.number(0), a1 = m.number(1);
  bool hasArg = !m.args.empty();

  if (p.size() == 2) {
    const std::string &cmd = p[1];
    if (cmd == "go") { if (!hasArg || a0 > 0.5f) go(); return true; }
    if (cmd == "back") { if (!hasArg || a0 > 0.5f) back(); return true; }
    if (cmd == "stop") { stopAll(hasArg ? a0 : -1); return true; }
    if (cmd == "blackout") { blackout = !hasArg || a0 > 0.5f; return true; }
    if (cmd == "master") { masterOpacity = std::clamp(a0, 0.0f, 1.0f); return true; }
    if (cmd == "volume") { project.masterVolume = std::clamp(a0, 0.0f, 2.0f); return true; }
    return false;
  }

  if (p[1] == "column") {
    // /lumen/column/launch <n>   or   /lumen/column/<n>/launch
    int n = 0;
    if (p.size() == 3 && p[2] == "launch" && hasArg) { launchColumn(int(a0) - 1); return true; }
    if (p.size() == 4 && p[3] == "launch" && parseInt(p[2], n)) {
      if (!hasArg || a0 > 0.5f) launchColumn(n - 1);
      return true;
    }
    return false;
  }

  if (p[1] == "layer" && p.size() >= 4) {
    Layer *l = findLayer(p[2]);
    if (!l) return false;
    const std::string &prop = p[3];
    static const std::pair<const char *, MappingTarget> params[] = {
        {"opacity", MappingTarget::Opacity},       {"x", MappingTarget::PositionX},
        {"y", MappingTarget::PositionY},           {"scale", MappingTarget::Scale},
        {"rotation", MappingTarget::Rotation},     {"brightness", MappingTarget::Brightness},
        {"contrast", MappingTarget::Contrast},     {"saturation", MappingTarget::Saturation},
        {"hue", MappingTarget::Hue},               {"speed", MappingTarget::Speed},
        {"volume", MappingTarget::Volume}};
    for (auto &[name, target] : params)
      if (prop == name && hasArg) { setParam(*l, target, a0); return true; }
    if (prop == "position" && m.args.size() >= 2) {
      l->transform.position = {a0, a1};
      return true;
    }
    if (prop == "visible") { l->visible = !hasArg || a0 > 0.5f; return true; }
    if (prop == "mute") { l->mute = !hasArg || a0 > 0.5f; return true; }
    if (prop == "launch" && hasArg) { launchCell(int(a0) - 1, l->id); return true; }
    if (prop == "stop") { stopLayer(l->id, m.args.size() > 0 ? a0 : -1); return true; }
    if (prop == "restart") {
      auto it = states_.find(l->id);
      if (it != states_.end() && it->second.current) it->second.current->restart();
      return true;
    }
    if (prop == "tint" && m.args.size() >= 3) {
      l->fx.tint = {a0, a1, m.number(2), 1};
      return true;
    }
  }
  return false;
}

void Engine::applyMappings(const OscMessage &m) {
  if (learnMapping >= 0 && learnMapping < int(project.mappings.size())) {
    InputMapping &im = project.mappings[size_t(learnMapping)];
    im.address = m.address;
    // Guess a sensible input range from the first value.
    float v = m.number(size_t(im.argIndex));
    // MIDI is always 0..127 (pitch bend 0..16383); guess common sensor ranges otherwise.
    if (m.address.rfind("/midi/bend", 0) == 0) im.inMax = 16383.0f;
    else if (m.address.rfind("/midi/", 0) == 0) im.inMax = 127.0f;
    else if (v > 1.0f) im.inMax = v > 127.0f ? (v > 1023.0f ? 4095.0f : 1023.0f) : 127.0f;
    learnMapping = -1;
  }
  mappingStates_.resize(project.mappings.size());
  for (size_t i = 0; i < project.mappings.size(); ++i) {
    InputMapping &im = project.mappings[i];
    if (!im.enabled || !oscMatch(im.address.c_str(), m.address.c_str())) continue;
    if (size_t(im.argIndex) >= m.args.size()) continue;
    float span = im.inMax - im.inMin;
    float t = span != 0 ? (m.number(size_t(im.argIndex)) - im.inMin) / span : 0.0f;
    t = std::clamp(t, 0.0f, 1.0f);
    MappingState &st = mappingStates_[i];

    if (mappingTargetIsTrigger(im.target)) {
      bool high = t >= 0.5f;
      if (high && !st.triggered) { // rising edge
        if (im.target == MappingTarget::Go) go();
        else if (im.target == MappingTarget::StopAll) stopAll();
        else if (im.target == MappingTarget::LaunchColumn) launchColumn(int(im.outMin) - 1);
        else if (im.target == MappingTarget::Blackout) blackout = !blackout;
      }
      st.triggered = high;
      continue;
    }
    st.target = im.outMin + t * (im.outMax - im.outMin);
    if (!st.hasValue || im.smoothing <= 0) st.value = st.target;
    st.hasValue = true;
    st.active = true;
  }
}

void Engine::updateMappings(double dt) {
  mappingStates_.resize(project.mappings.size());
  for (size_t i = 0; i < project.mappings.size(); ++i) {
    InputMapping &im = project.mappings[i];
    MappingState &st = mappingStates_[i];
    if (!im.enabled || !st.active || mappingTargetIsTrigger(im.target)) continue;
    if (im.smoothing > 0) {
      // Frame-rate independent exponential smoothing.
      float k = 1.0f - std::pow(std::clamp(im.smoothing, 0.0f, 0.999f), float(dt) * 60.0f);
      st.value += (st.target - st.value) * k;
    }
    if (im.target == MappingTarget::MasterOpacity) masterOpacity = std::clamp(st.value, 0.0f, 1.0f);
    else if (im.target == MappingTarget::MasterVolume) project.masterVolume = std::clamp(st.value, 0.0f, 2.0f);
    else if (Layer *l = project.layer(im.layerId)) setParam(*l, im.target, st.value);
    // Once settled, stop writing so the parameter can still be edited by hand.
    if (std::fabs(st.value - st.target) < 1e-4f) st.active = false;
  }
}

} // namespace lumen
