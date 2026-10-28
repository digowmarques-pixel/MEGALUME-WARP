// OSC address space and user-defined input mappings. See docs/OSC.md.
#include "engine/engine.h"

#include <algorithm>
#include <cctype>
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

float Engine::getParam(const Layer &l, MappingTarget t) const {
  switch (t) {
  case MappingTarget::Opacity: return l.opacity;
  case MappingTarget::PositionX: return l.transform.position.x;
  case MappingTarget::PositionY: return l.transform.position.y;
  case MappingTarget::Scale: return l.transform.scale;
  case MappingTarget::Rotation: return l.transform.rotation;
  case MappingTarget::Brightness: return l.fx.brightness;
  case MappingTarget::Contrast: return l.fx.contrast;
  case MappingTarget::Saturation: return l.fx.saturation;
  case MappingTarget::Hue: return l.fx.hue;
  case MappingTarget::Volume: return l.volume;
  case MappingTarget::Speed: {
    auto it = states_.find(l.id);
    return it != states_.end() && it->second.current ? it->second.current->speed : 1.0f;
  }
  default: return 0;
  }
}

static std::string lower(std::string s) {
  for (auto &c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
  return s;
}

// Column given as a 1-based number or by name.
static int columnArg(const Project &p, const OscMessage &m) {
  if (m.args.empty()) return -1;
  if (auto *s = std::get_if<std::string>(&m.args[0])) {
    for (size_t i = 0; i < p.columns.size(); ++i)
      if (lower(p.columns[i].name) == lower(*s)) return int(i);
    int n;
    return parseInt(*s, n) ? n - 1 : -1;
  }
  return int(m.number(0)) - 1;
}

// Layer properties with Millumin's names (also used by /lumen/layer/<n>/...).
// A trailing "+" or "-" adds to / subtracts from the current value.
bool Engine::setLayerProperty(Layer &l, const std::vector<std::string> &propIn, const OscMessage &m) {
  std::vector<std::string> prop;
  for (auto &x : propIn) prop.push_back(lower(x));
  int rel = 0;
  if (!prop.empty() && (prop.back() == "+" || prop.back() == "-")) {
    rel = prop.back() == "+" ? 1 : -1;
    prop.pop_back();
  }
  if (prop.empty()) return false;
  if (prop[0] == "position" && prop.size() > 1) prop.erase(prop.begin()); // position/x -> x
  if (prop[0] == "media" && prop.size() > 1) prop.erase(prop.begin());    // media/speed -> speed
  bool hasArg = !m.args.empty();
  float a0 = m.number(0), a1 = m.number(1);
  auto value = [&](MappingTarget t, float v) { return rel ? getParam(l, t) + float(rel) * v : v; };

  static const std::pair<const char *, MappingTarget> params[] = {
      {"opacity", MappingTarget::Opacity},       {"x", MappingTarget::PositionX},
      {"y", MappingTarget::PositionY},           {"scale", MappingTarget::Scale},
      {"rotation", MappingTarget::Rotation},     {"brightness", MappingTarget::Brightness},
      {"contrast", MappingTarget::Contrast},     {"saturation", MappingTarget::Saturation},
      {"hue", MappingTarget::Hue},               {"speed", MappingTarget::Speed},
      {"volume", MappingTarget::Volume},         {"audio-volume", MappingTarget::Volume}};
  const std::string &p0 = prop[0];
  if (prop.size() == 1)
    for (auto &[name, target] : params)
      if (p0 == name && hasArg) { setParam(l, target, value(target, a0)); return true; }
  if ((p0 == "xy" || p0 == "position") && m.args.size() >= 2) {
    l.transform.position = {value(MappingTarget::PositionX, a0), value(MappingTarget::PositionY, a1)};
    return true;
  }
  if (p0 == "mapping" && prop.size() >= 2 && !l.slices.empty()) {
    // /mapping/topLeft [x y] (canvas px), or /mapping/topLeft/x [x]
    static const char *corners[] = {"topleft", "topright", "bottomright", "bottomleft"};
    for (int c = 0; c < 4; ++c) {
      if (prop[1] != corners[c]) continue;
      Slice &sl = l.slices.front();
      Vec2 &v = sl.surfaceId >= 0 && project.surface(sl.surfaceId) ? project.surface(sl.surfaceId)->warp.corners[size_t(c)]
                                                                   : sl.warp.corners[size_t(c)];
      float d = float(rel);
      if (prop.size() == 3 && prop[2] == "x" && hasArg) v.x = rel ? v.x + d * a0 : a0;
      else if (prop.size() == 3 && prop[2] == "y" && hasArg) v.y = rel ? v.y + d * a0 : a0;
      else if (m.args.size() >= 2) v = rel ? Vec2{v.x + d * a0, v.y + d * a1} : Vec2{a0, a1};
      else return false;
      project.resolveSurfaces();
      return true;
    }
    return false;
  }
  // /effect1/<param> [values]: parameters of the layer's effects (1 = first).
  if (p0.rfind("effect", 0) == 0 && prop.size() >= 2) {
    int n = 0;
    if (!parseInt(p0.substr(6), n) || n < 1 || n > int(l.effects.size())) return false;
    Effect &e = l.effects[size_t(n - 1)];
    if (prop[1] == "enabled") { e.enabled = !hasArg || a0 > 0.5f; return true; }
    const IsfInfo *info = isfCatalog.info(e.path);
    if (!info || !hasArg) return false;
    for (auto &in : info->inputs) {
      if (lower(in.name) != prop[1] || in.size() == 0) continue;
      auto &v = e.params[in.name];
      if (int(v.size()) != in.size()) { IsfValues d; isfDefaults(*info, d); v = d[in.name]; }
      for (size_t k = 0; k < v.size() && k < m.args.size(); ++k) v[k] = rel ? v[k] + float(rel) * m.number(k) : m.number(k);
      return true;
    }
    return false;
  }
  // /layer:Nome/text "..." (or /media/text): text of the text media playing in the layer.
  if (p0 == "text" && hasArg) {
    auto it = states_.find(l.id);
    MediaItem *media = it != states_.end() && it->second.current ? project.media(it->second.current->mediaId()) : nullptr;
    if (!media || media->type != MediaType::Text) return false;
    std::string text;
    for (size_t k = 0; k < m.args.size(); ++k) {
      if (k) text += ' ';
      if (auto *str = std::get_if<std::string>(&m.args[k])) text += *str;
      else {
        char b[32];
        std::snprintf(b, sizeof(b), "%g", m.number(k));
        text += b;
      }
    }
    media->text = text;
    return true;
  }
  if (p0 == "visible") { l.visible = !hasArg || a0 > 0.5f; return true; }
  if (p0 == "mute") { l.mute = !hasArg || a0 > 0.5f; return true; }
  if (p0 == "tint" && m.args.size() >= 3) { l.fx.tint = {a0, a1, m.number(2), 1}; return true; }
  if (p0 == "launch" && hasArg) { launchCell(int(a0) - 1, l.id); return true; }
  if (p0 == "startmedia") {
    if (!hasArg) {
      auto it = states_.find(l.id);
      if (it != states_.end() && it->second.current) it->second.current->restart();
    } else {
      int c = columnArg(project, m);
      if (c >= 0) launchCell(c, l.id);
    }
    return true;
  }
  if (p0 == "stop" || p0 == "stopmedia") { stopLayer(l.id, p0 == "stop" && hasArg ? a0 : -1); return true; }
  if (p0 == "restart") {
    auto it = states_.find(l.id);
    if (it != states_.end() && it->second.current) it->second.current->restart();
    return true;
  }
  return false;
}

// Millumin's OSC API (V5), so its TouchOSC / Companion / Open Stage Control
// templates and backup-machine setups work unchanged.
bool Engine::handleMillumin(const std::vector<std::string> &p, const OscMessage &m) {
  if (p.empty()) return false;
  std::string head = lower(p[0]);
  bool hasArg = !m.args.empty();
  float a0 = m.number(0);
  if (head == "action" && p.size() == 2) {
    std::string a = lower(p[1]);
    if (a == "launchcolumn") { launchColumn(columnArg(project, m)); return true; }
    if (a == "launchorstopcolumn") {
      int c = columnArg(project, m);
      if (c == activeColumn_) stopAll();
      else launchColumn(c);
      return true;
    }
    if (a == "stopcolumn") { stopAll(); return true; }
    if (a == "launchnextcolumn") { go(); return true; }
    if (a == "launchpreviouscolumn") { back(); return true; }
    if (a == "displaytestcard" || a == "hidetestcard") {
      for (auto &o : project.outputs) o.pattern = a == "displaytestcard" ? TestPattern::Card : TestPattern::None;
      return true;
    }
    if (a == "enterfullscreen") { requestOutputs = 1; return true; }
    if (a == "exitfullscreen") { requestOutputs = 0; return true; }
    if (a == "selectlayer" && hasArg) {
      Layer *l = nullptr;
      if (auto *s = std::get_if<std::string>(&m.args[0])) l = findLayer(*s);
      else l = findLayer(std::to_string(int(a0)));
      if (l) selectedLayerId = l->id;
      return l != nullptr;
    }
    return false;
  }
  if (p.size() == 1 && hasArg) {
    if (head == "mastervideo") { masterOpacity = std::clamp(a0, 0.0f, 1.0f); return true; }
    if (head == "masteraudio") { project.masterVolume = std::clamp(a0, 0.0f, 2.0f); return true; }
    if (head == "masterdmx") { project.dmxMaster = std::clamp(a0, 0.0f, 1.0f); return true; }
  }
  if (p.size() < 2) return false;
  // Element: /layer:Name/..., /index:2/..., /selectedLayer/..., /Name/...
  Layer *l = nullptr;
  if (head == "selectedlayer") l = project.layer(selectedLayerId);
  else if (head.rfind("layer:", 0) == 0) l = findLayer(p[0].substr(6));
  else if (head.rfind("index:", 0) == 0) l = findLayer(p[0].substr(6));
  else l = findLayer(p[0]);
  if (!l) return false;
  return setLayerProperty(*l, std::vector<std::string>(p.begin() + 1, p.end()), m);
}

bool Engine::handleOsc(const OscMessage &m) {
  auto p = splitPath(m.address);
  if (p.empty()) return false;
  if (lower(p[0]) == "millumin") return handleMillumin(std::vector<std::string>(p.begin() + 1, p.end()), m);
  if (p[0] != "lumen") return handleMillumin(p, m);
  float a0 = m.number(0);
  bool hasArg = !m.args.empty();

  if (p.size() == 2) {
    const std::string &cmd = p[1];
    if (cmd == "go") { if (!hasArg || a0 > 0.5f) go(); return true; }
    if (cmd == "back") { if (!hasArg || a0 > 0.5f) back(); return true; }
    if (cmd == "stop") { stopAll(hasArg ? a0 : -1); return true; }
    if (cmd == "blackout") { blackout = !hasArg || a0 > 0.5f; return true; }
    if (cmd == "master") { masterOpacity = std::clamp(a0, 0.0f, 1.0f); return true; }
    if (cmd == "volume") { project.masterVolume = std::clamp(a0, 0.0f, 2.0f); return true; }
    if (cmd == "transition") { transitionFader = std::clamp(a0, 0.0f, 1.0f); return true; }
    return false;
  }

  if (p[1] == "timeline" && p.size() == 4) {
    // /lumen/timeline/<n|name>/play|pause|stop|time <s>
    Timeline *tl = nullptr;
    int n;
    if (parseInt(p[2], n) && n >= 1 && n <= int(project.timelines.size())) tl = &project.timelines[size_t(n - 1)];
    for (auto &t : project.timelines)
      if (!tl && t.name == p[2]) tl = &t;
    if (!tl) return false;
    if (p[3] == "play") { playTimeline(tl->id, !hasArg || a0 > 0.5f); return true; }
    if (p[3] == "pause") { pauseTimeline(tl->id); return true; }
    if (p[3] == "stop") { stopTimeline(tl->id); return true; }
    if (p[3] == "time" && hasArg) { seekTimeline(tl->id, a0); return true; }
    return false;
  }
  if (p[1] == "command" && hasArg) { // /lumen/command "pjlink sala power on"
    if (auto *str = std::get_if<std::string>(&m.args[0])) { runCommand(*str); return true; }
    return false;
  }
  if (p[1] == "dmx" && p.size() == 3 && p[2] == "master" && hasArg) {
    project.dmxMaster = std::clamp(a0, 0.0f, 1.0f);
    return true;
  }

  if (p[1] == "led" && p.size() == 4) {
    // /lumen/led/<n|name>/enabled|brightness
    LedMapping *lm = nullptr;
    int n;
    if (parseInt(p[2], n) && n >= 1 && n <= int(project.ledMappings.size())) lm = &project.ledMappings[size_t(n - 1)];
    for (auto &x : project.ledMappings)
      if (!lm && x.name == p[2]) lm = &x;
    if (!lm) return false;
    if (p[3] == "enabled") { lm->enabled = !hasArg || a0 > 0.5f; return true; }
    if (p[3] == "brightness" && hasArg) { lm->brightness = std::clamp(a0, 0.0f, 1.0f); return true; }
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
    return setLayerProperty(*l, std::vector<std::string>(p.begin() + 3, p.end()), m);
  }
  return false;
}

void Engine::applyMappings(const OscMessage &m) {
  // Blob data arrives every frame: "learn" would always catch it (blobs have
  // their own shortcut buttons in the effect).
  if (learnMapping >= 0 && learnMapping < int(project.mappings.size()) && m.sender != "blob") {
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
    if (im.firstColumn > 0 || im.lastColumn > 0) { // active only for some cues
      int c = activeColumn_ + 1;
      if (c < std::max(1, im.firstColumn) || (im.lastColumn > 0 && c > im.lastColumn)) continue;
    }
    float span = im.inMax - im.inMin;
    float t = span != 0 ? (m.number(size_t(im.argIndex)) - im.inMin) / span : 0.0f;
    t = std::clamp(t, 0.0f, 1.0f);
    MappingState &st = mappingStates_[i];
    bool high = t >= 0.5f;
    bool rising = high && !st.triggered;
    st.triggered = high;

    if (mappingTargetIsTrigger(im.target)) {
      if (rising) {
        if (im.target == MappingTarget::Go) go();
        else if (im.target == MappingTarget::StopAll) stopAll();
        else if (im.target == MappingTarget::LaunchColumn) launchColumn(int(im.outMin) - 1);
        else if (im.target == MappingTarget::Blackout) blackout = !blackout;
      }
      continue;
    }
    switch (im.transformer) {
    case Transformer::Switch: st.target = high ? im.outMax : im.outMin; break;
    case Transformer::Toggle:
      if (!rising) continue;
      st.toggled = !st.toggled;
      st.target = st.toggled ? im.outMax : im.outMin;
      break;
    case Transformer::Accumulator:
      if (!rising) continue;
      if (!st.accumInit) { st.accum = im.outMin; st.accumInit = true; }
      st.accum += im.step;
      if ((im.step >= 0 && st.accum > im.outMax + 1e-5f) || (im.step < 0 && st.accum < im.outMax - 1e-5f))
        st.accum = im.outMin; // wraps around (e.g. next page ... back to the first)
      st.target = st.accum;
      break;
    default: st.target = im.outMin + t * (im.outMax - im.outMin); break;
    }
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
    else if (im.target == MappingTarget::DmxMaster) project.dmxMaster = std::clamp(st.value, 0.0f, 1.0f);
    else if (im.target == MappingTarget::TransitionFader) transitionFader = std::clamp(st.value, 0.0f, 1.0f);
    else if (Layer *l = project.layer(im.layerId)) setParam(*l, im.target, st.value);
    // Once settled, stop writing so the parameter can still be edited by hand.
    if (std::fabs(st.value - st.target) < 1e-4f) st.active = false;
  }
}

} // namespace lumen
