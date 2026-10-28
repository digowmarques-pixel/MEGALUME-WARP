#include "core/project.h"

#include "platform/platform.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>

namespace lumen {

// float as the number type: values print in their shortest form (0.1, not 0.10000000149).
using json = nlohmann::basic_json<std::map, std::vector, std::string, bool, std::int64_t,
                                  std::uint64_t, float>;

static const char *kGeneratorNames[] = {"Test card", "Solid color", "Gradient",
                                         "Grid",      "Plasma",      "Stripes"};
static const char *kBlendNames[] = {"Normal", "Add", "Multiply", "Screen"};
static const char *kTargetNames[] = {"Opacity",    "Position X", "Position Y",   "Scale",
                                     "Rotation",   "Brightness", "Contrast",     "Saturation",
                                     "Hue",        "Speed",      "Launch column", "Go (next)",
                                     "Stop all",   "Volume",     "Master opacity", "Master volume",
                                     "Blackout",   "DMX master"};

const char *generatorName(GeneratorKind k) { return kGeneratorNames[int(k)]; }
const char *blendName(BlendMode b) { return kBlendNames[int(b)]; }
const char *mappingTargetName(MappingTarget t) { return kTargetNames[int(t)]; }

MediaItem *Project::media(int id) {
  for (auto &m : library)
    if (m.id == id) return &m;
  return nullptr;
}

Layer *Project::layer(int id) {
  for (auto &l : layers)
    if (l.id == id) return &l;
  return nullptr;
}

int Project::layerIndex(int id) const {
  for (size_t i = 0; i < layers.size(); ++i)
    if (layers[i].id == id) return int(i);
  return -1;
}

OutputConfig *Project::output(int id) {
  for (auto &o : outputs)
    if (o.id == id) return &o;
  return nullptr;
}

Layer &Project::addLayer(const std::string &name) {
  Layer l;
  l.id = newId();
  l.name = name.empty() ? "Layer " + std::to_string(layers.size() + 1) : name;
  Slice s;
  s.warp.setRect({0, 0}, {float(canvasW), float(canvasH)});
  l.slices.push_back(s);
  layers.insert(layers.begin(), l); // new layers go on top
  return layers.front();
}

Column &Project::addColumn() {
  Column c;
  c.id = newId();
  c.name = "Cue " + std::to_string(columns.size() + 1);
  columns.push_back(c);
  return columns.back();
}

OutputConfig &Project::addOutput() {
  OutputConfig o;
  o.id = newId();
  o.name = "Output " + std::to_string(outputs.size() + 1);
  o.srcW = float(canvasW);
  o.srcH = float(canvasH);
  o.warp.setRect({0, 0}, {1, 1});
  outputs.push_back(o);
  return outputs.back();
}

MediaItem &Project::addMedia(const std::string &path) {
  MediaItem m;
  m.id = newId();
  m.path = path;
  m.name = platform::toUtf8(platform::toPath(path).filename());
  m.type = isImagePath(path) ? MediaType::Image : MediaType::Video;
  library.push_back(m);
  return library.back();
}

MediaItem &Project::addGenerator(GeneratorKind kind) {
  MediaItem m;
  m.id = newId();
  m.type = MediaType::Generator;
  m.generator = kind;
  m.name = generatorName(kind);
  m.width = canvasW;
  m.height = canvasH;
  switch (kind) {
  case GeneratorKind::Solid: m.color1 = {0.9f, 0.2f, 0.3f, 1}; break;
  case GeneratorKind::Gradient:
    m.color1 = {0.05f, 0.1f, 0.6f, 1};
    m.color2 = {0.9f, 0.3f, 0.6f, 1};
    m.param1 = 90;
    break;
  case GeneratorKind::Grid: m.param1 = 80; m.param2 = 2; break;
  case GeneratorKind::Plasma:
    m.color1 = {0.1f, 0.4f, 1.0f, 1};
    m.color2 = {1.0f, 0.2f, 0.5f, 1};
    m.param1 = 1;
    m.param2 = 3;
    break;
  case GeneratorKind::Stripes: m.param1 = 0.5f; m.param2 = 12; break;
  default: break;
  }
  library.push_back(m);
  return library.back();
}

MediaItem &Project::addLive(const std::string &name, const std::string &url,
                             const std::string &format, const std::string &options) {
  MediaItem m;
  m.id = newId();
  m.type = MediaType::Live;
  m.name = name.empty() ? url : name;
  m.path = url;
  m.liveFormat = format;
  m.liveOptions = options;
  library.push_back(m);
  return library.back();
}

MediaItem &Project::addSpout(const std::string &sender) {
  MediaItem m;
  m.id = newId();
  m.type = MediaType::Spout;
  m.name = sender.empty() ? "Spout (ativo)" : "Spout: " + sender;
  m.path = sender;
  library.push_back(m);
  return library.back();
}

DmxDevice *Project::dmxDevice(int id) {
  for (auto &d : dmxDevices)
    if (d.id == id) return &d;
  return nullptr;
}

LedMapping *Project::ledMapping(int id) {
  for (auto &m : ledMappings)
    if (m.id == id) return &m;
  return nullptr;
}

DmxDevice &Project::addDmxDevice(DmxProtocol protocol) {
  DmxDevice d;
  d.id = newId();
  d.protocol = protocol;
  d.name = protocol == DmxProtocol::ArtNet ? "Art-Net " : "sACN ";
  d.name += std::to_string(dmxDevices.size() + 1);
  dmxDevices.push_back(d);
  return dmxDevices.back();
}

LedMapping &Project::addLedMapping() {
  LedMapping m;
  m.id = newId();
  m.name = "Fita LED " + std::to_string(ledMappings.size() + 1);
  // A thin strip across the lower part of the canvas.
  m.area.setRect({float(canvasW) * 0.1f, float(canvasH) * 0.85f}, {float(canvasW) * 0.8f, float(canvasH) * 0.04f});
  if (!dmxDevices.empty()) {
    m.deviceId = dmxDevices.front().id;
    if (dmxDevices.front().protocol == DmxProtocol::SACN) m.universe = 1;
  }
  // Continue where the previous mapping on this device ended.
  for (auto &o : ledMappings)
    if (o.deviceId == m.deviceId) {
      auto slots = computeLedLayout(o.layout());
      if (!slots.empty()) {
        int cpp = ledChannelsPerPixel(o.format, o.wide);
        m.universe = slots.back().universe;
        m.channel = slots.back().channel + cpp + 1;
        if (m.channel + 2 > 512) { ++m.universe; m.channel = 1; }
      }
    }
  ledMappings.push_back(m);
  return ledMappings.back();
}

void Project::removeLayer(int id) {
  layers.erase(std::remove_if(layers.begin(), layers.end(), [&](auto &l) { return l.id == id; }),
               layers.end());
  for (auto &c : columns) c.cells.erase(id);
  for (auto &m : mappings)
    if (m.layerId == id) m.layerId = -1;
}

void Project::removeMedia(int id) {
  library.erase(std::remove_if(library.begin(), library.end(), [&](auto &m) { return m.id == id; }),
                library.end());
  for (auto &c : columns)
    for (auto &[lid, cell] : c.cells)
      if (cell.mediaId == id) cell.mediaId = -1;
}

Project Project::makeDefault() {
  Project p;
  p.addLayer("Layer 1");
  for (int i = 0; i < 4; ++i) p.addColumn();
  p.addOutput();
  return p;
}

void applyBlendLayout(Project &p, const BlendLayoutParams &params, int firstMonitor, bool scaleContent) {
  p.outputLayout = params;
  BlendLayout lay = computeBlendLayout(params);
  if (scaleContent && p.canvasW > 0 && p.canvasH > 0) {
    float sx = float(lay.canvasW) / float(p.canvasW), sy = float(lay.canvasH) / float(p.canvasH);
    auto scale = [&](Vec2 &v) { v = {v.x * sx, v.y * sy}; };
    for (auto &l : p.layers) {
      for (auto &sl : l.slices)
        for (auto &c : sl.warp.corners) scale(c);
      for (auto &m : l.masks)
        for (auto &pt : m.points) scale(pt);
      scale(l.transform.position);
    }
    for (auto &m : p.ledMappings)
      for (auto &c : m.area.corners) scale(c);
  }
  p.canvasW = lay.canvasW;
  p.canvasH = lay.canvasH;

  size_t n = lay.projectors.size();
  while (p.outputs.size() < n) p.addOutput();
  p.outputs.resize(n);
  for (size_t i = 0; i < n; ++i) {
    OutputConfig &o = p.outputs[i];
    const BlendProjector &b = lay.projectors[i];
    o.name = "Projetor " + std::to_string(i + 1);
    o.enabled = true;
    o.srcX = b.srcX;
    o.srcY = b.srcY;
    o.srcW = b.srcW;
    o.srcH = b.srcH;
    o.softEdge.left = b.left;
    o.softEdge.right = b.right;
    o.softEdge.top = b.top;
    o.softEdge.bottom = b.bottom;
    if (firstMonitor >= 0) o.monitor = firstMonitor + int(i);
  }
}

// ------------------------------------------------------------------ paths

static std::string lower(std::string s) {
  for (auto &c : s) c = char(std::tolower(unsigned(c)));
  return s;
}

static std::string extension(const std::string &path) {
  return lower(platform::toUtf8(platform::toPath(path).extension()));
}

bool isImagePath(const std::string &path) {
  static const char *exts[] = {".png", ".jpg", ".jpeg", ".bmp", ".tga", ".tif", ".tiff", ".webp", ".exr"};
  std::string e = extension(path);
  return std::any_of(std::begin(exts), std::end(exts), [&](const char *x) { return e == x; });
}

bool isMediaPath(const std::string &path) {
  static const char *exts[] = {".mp4", ".mov", ".m4v", ".mkv", ".avi", ".webm", ".mpg", ".mpeg",
                               ".wmv", ".gif", ".mxf", ".hap", ".flv", ".ts"};
  std::string e = extension(path);
  return isImagePath(path) ||
         std::any_of(std::begin(exts), std::end(exts), [&](const char *x) { return e == x; });
}

static std::string makeRelative(const std::string &path, const std::string &baseDir) {
  if (baseDir.empty() || path.empty()) return path;
  std::error_code ec;
  auto rel = std::filesystem::relative(platform::toPath(path), platform::toPath(baseDir), ec);
  if (ec || rel.empty()) return path;
  auto generic = rel.generic_u8string(); // forward slashes: portable between OSes
  std::string r(generic.begin(), generic.end());
  // Keep absolute paths when the media lives on another drive / far away.
  if (r.rfind("../../../", 0) == 0) return path;
  return r;
}

static std::string makeAbsolute(const std::string &path, const std::string &baseDir) {
  if (path.empty()) return path;
  auto p = platform::toPath(path);
  if (p.is_absolute() || baseDir.empty()) return path;
  return platform::toUtf8((platform::toPath(baseDir) / p).lexically_normal());
}

// ------------------------------------------------------------------ json

static json toJson(const Color &c) { return {c.r, c.g, c.b, c.a}; }
static void fromJson(const json &j, Color &c) {
  if (!j.is_array() || j.size() < 3) return;
  c.r = j[0]; c.g = j[1]; c.b = j[2];
  c.a = j.size() > 3 ? float(j[3]) : 1.0f;
}

static json toJson(const Vec2 &v) { return {v.x, v.y}; }
static Vec2 vecFrom(const json &j) {
  return j.is_array() && j.size() >= 2 ? Vec2{j[0].get<float>(), j[1].get<float>()} : Vec2{};
}

static json toJson(const Warp &w) {
  json j;
  j["corners"] = json::array();
  for (auto &c : w.corners) j["corners"].push_back(toJson(c));
  if (!(w.meshCols == 2 && w.meshRows == 2 && w.meshIsIdentity())) {
    j["meshCols"] = w.meshCols;
    j["meshRows"] = w.meshRows;
    j["mesh"] = json::array();
    for (auto &p : w.mesh) j["mesh"].push_back(toJson(p));
  }
  return j;
}

static void fromJson(const json &j, Warp &w) {
  if (j.contains("corners"))
    for (size_t i = 0; i < 4 && i < j["corners"].size(); ++i) w.corners[i] = vecFrom(j["corners"][i]);
  if (j.contains("mesh")) {
    w.resetMesh(j.value("meshCols", 2), j.value("meshRows", 2));
    for (size_t i = 0; i < w.mesh.size() && i < j["mesh"].size(); ++i) w.mesh[i] = vecFrom(j["mesh"][i]);
  } else {
    w.resetMesh(2, 2);
  }
}

static json masksToJson(const std::vector<Mask> &masks) {
  json arr = json::array();
  for (auto &m : masks) {
    json jm = {{"name", m.name}, {"enabled", m.enabled}, {"subtract", m.subtract},
               {"feather", m.feather}, {"points", json::array()}};
    for (auto &pt : m.points) jm["points"].push_back(toJson(pt));
    arr.push_back(jm);
  }
  return arr;
}

static std::vector<Mask> masksFromJson(const json &arr) {
  std::vector<Mask> out;
  for (auto &jm : arr) {
    Mask m;
    m.name = jm.value("name", "Mask");
    m.enabled = jm.value("enabled", true);
    m.subtract = jm.value("subtract", false);
    m.feather = jm.value("feather", 0.0f);
    for (auto &pt : jm.value("points", json::array())) m.points.push_back(vecFrom(pt));
    out.push_back(m);
  }
  return out;
}

template <typename E> static E enumFrom(const json &j, const char *key, E def, int count) {
  int v = j.value(key, int(def));
  return (v >= 0 && v < count) ? E(v) : def;
}

std::string serializeProject(const Project &p, const std::string &baseDir) {
  json j;
  j["format"] = "lumen";
  j["version"] = 1;
  j["canvas"] = {{"width", p.canvasW}, {"height", p.canvasH}, {"background", toJson(p.background)}};
  j["defaultTransition"] = p.defaultTransition;
  j["osc"] = {{"enabled", p.oscEnabled}, {"port", p.oscPort}, {"targets", json::array()}};
  for (auto &t : p.oscTargets)
    j["osc"]["targets"].push_back({{"enabled", t.enabled}, {"host", t.host}, {"port", t.port},
                                   {"feedback", t.feedback}});
  j["midi"] = {{"enabled", p.midiEnabled}};
  j["audio"] = {{"device", p.audioDevice}, {"master", p.masterVolume}};
  j["video"] = {{"hardwareDecode", p.hardwareDecode}};
  j["spout"] = {{"send", p.spoutSend}, {"name", p.spoutName}};
  {
    const BlendLayoutParams &b = p.outputLayout;
    j["outputLayout"] = {{"cols", b.cols}, {"rows", b.rows}, {"size", {b.projW, b.projH}},
                         {"gapsX", b.gapsX}, {"gapsY", b.gapsY}};
  }
  j["dmx"] = {{"master", p.dmxMaster}, {"devices", json::array()}, {"leds", json::array()}};
  for (auto &d : p.dmxDevices)
    j["dmx"]["devices"].push_back({{"id", d.id}, {"name", d.name}, {"enabled", d.enabled},
                                   {"protocol", int(d.protocol)}, {"host", d.host}, {"rate", d.rate}});
  for (auto &m : p.ledMappings) {
    json jm = {{"id", m.id}, {"name", m.name}, {"enabled", m.enabled}, {"locked", m.locked},
               {"size", {m.cols, m.rows}}, {"format", int(m.format)}, {"wide", m.wide},
               {"zigzag", m.zigzag}, {"vertical", m.vertical}, {"flipX", m.flipX}, {"flipY", m.flipY},
               {"gamma", m.gamma}, {"brightness", m.brightness}, {"device", m.deviceId},
               {"universe", m.universe}, {"channel", m.channel}, {"area", json::array()}};
    for (auto &c : m.area.corners) jm["area"].push_back(toJson(c));
    j["dmx"]["leds"].push_back(jm);
  }
  j["nextId"] = p.nextId;

  for (auto &m : p.library) {
    json jm = {{"id", m.id}, {"name", m.name}, {"type", int(m.type)}};
    if (m.type == MediaType::Generator) {
      jm["generator"] = int(m.generator);
      jm["color1"] = toJson(m.color1);
      jm["color2"] = toJson(m.color2);
      jm["param1"] = m.param1;
      jm["param2"] = m.param2;
      jm["size"] = {m.width, m.height};
    } else if (m.type == MediaType::Live) {
      jm["path"] = m.path;
      jm["format"] = m.liveFormat;
      jm["options"] = m.liveOptions;
    } else if (m.type == MediaType::Spout) {
      jm["path"] = m.path;
    } else {
      jm["path"] = makeRelative(m.path, baseDir);
    }
    j["library"].push_back(jm);
  }

  for (auto &l : p.layers) {
    json jl = {{"id", l.id}, {"name", l.name}, {"visible", l.visible}, {"opacity", l.opacity},
               {"blend", int(l.blend)}, {"volume", l.volume}, {"mute", l.mute}};
    jl["fx"] = {{"brightness", l.fx.brightness}, {"contrast", l.fx.contrast},
                {"saturation", l.fx.saturation}, {"hue", l.fx.hue},
                {"invert", l.fx.invert}, {"tint", toJson(l.fx.tint)}};
    jl["transform"] = {{"position", toJson(l.transform.position)},
                       {"scale", l.transform.scale}, {"rotation", l.transform.rotation}};
    for (auto &s : l.slices)
      jl["slices"].push_back({{"name", s.name}, {"enabled", s.enabled},
                              {"crop", {s.u0, s.v0, s.u1, s.v1}}, {"warp", toJson(s.warp)}});
    jl["masks"] = masksToJson(l.masks);
    j["layers"].push_back(jl);
  }

  for (auto &c : p.columns) {
    json jc = {{"id", c.id}, {"name", c.name}, {"transition", c.transition}, {"cells", json::array()},
               {"transitionType", int(c.transitionType)}};
    if (!c.notes.empty()) jc["notes"] = c.notes;
    if (!c.oscActions.empty()) jc["oscActions"] = c.oscActions;
    for (auto &[lid, cell] : c.cells)
      if (cell.mediaId >= 0)
        jc["cells"].push_back({{"layer", lid}, {"media", cell.mediaId}, {"loop", cell.loop},
                               {"speed", cell.speed}});
    j["columns"].push_back(jc);
  }

  for (auto &o : p.outputs) {
    j["outputs"].push_back(
        {{"id", o.id}, {"name", o.name}, {"enabled", o.enabled}, {"monitor", o.monitor},
         {"window", {o.winX, o.winY, o.winW, o.winH}},
         {"source", {o.srcX, o.srcY, o.srcW, o.srcH}},
         {"warp", toJson(o.warp)},
         {"softEdge", {{"left", o.softEdge.left}, {"right", o.softEdge.right},
                       {"top", o.softEdge.top}, {"bottom", o.softEdge.bottom},
                       {"gamma", o.softEdge.gamma}, {"power", o.softEdge.power}}},
         {"pattern", int(o.pattern)},
         {"color", {{"brightness", o.color.brightness}, {"contrast", o.color.contrast},
                    {"gamma", o.color.gamma}, {"gain", toJson(o.color.gain)}}},
         {"blackLevel", toJson(o.blackLevel)},
         {"masks", masksToJson(o.masks)}});
  }

  for (auto &m : p.mappings)
    j["mappings"].push_back({{"enabled", m.enabled}, {"address", m.address}, {"arg", m.argIndex},
                             {"in", {m.inMin, m.inMax}}, {"target", int(m.target)},
                             {"layer", m.layerId}, {"out", {m.outMin, m.outMax}},
                             {"smoothing", m.smoothing}});
  return j.dump(2);
}

bool deserializeProject(const std::string &text, const std::string &baseDir, Project &out,
                        std::string *error) {
  json j;
  try {
    j = json::parse(text);
  } catch (const std::exception &e) {
    if (error) *error = e.what();
    return false;
  }
  try {
    Project p;
    if (j.contains("canvas")) {
      p.canvasW = j["canvas"].value("width", 1920);
      p.canvasH = j["canvas"].value("height", 1080);
      if (j["canvas"].contains("background")) fromJson(j["canvas"]["background"], p.background);
    }
    p.defaultTransition = j.value("defaultTransition", 1.0f);
    if (j.contains("osc")) {
      p.oscEnabled = j["osc"].value("enabled", true);
      p.oscPort = j["osc"].value("port", 8000);
      for (auto &jt : j["osc"].value("targets", json::array())) {
        OscTarget t;
        t.enabled = jt.value("enabled", true);
        t.host = jt.value("host", "127.0.0.1");
        t.port = jt.value("port", 9000);
        t.feedback = jt.value("feedback", true);
        p.oscTargets.push_back(t);
      }
    }
    if (j.contains("midi")) p.midiEnabled = j["midi"].value("enabled", true);
    if (j.contains("audio")) {
      p.audioDevice = j["audio"].value("device", "");
      p.masterVolume = j["audio"].value("master", 1.0f);
    }
    if (j.contains("video")) p.hardwareDecode = j["video"].value("hardwareDecode", true);
    if (j.contains("outputLayout")) {
      auto &b = j["outputLayout"];
      p.outputLayout.cols = std::clamp(b.value("cols", 1), 1, 16);
      p.outputLayout.rows = std::clamp(b.value("rows", 1), 1, 16);
      if (b.contains("size")) { p.outputLayout.projW = b["size"][0]; p.outputLayout.projH = b["size"][1]; }
      for (auto &g : b.value("gapsX", json::array())) p.outputLayout.gapsX.push_back(g.get<int>());
      for (auto &g : b.value("gapsY", json::array())) p.outputLayout.gapsY.push_back(g.get<int>());
    } else if (!p.outputs.empty()) {
      p.outputLayout.projW = int(p.outputs.front().srcW);
      p.outputLayout.projH = int(p.outputs.front().srcH);
    }
    if (j.contains("dmx")) {
      auto &jd = j["dmx"];
      p.dmxMaster = jd.value("master", 1.0f);
      for (auto &x : jd.value("devices", json::array())) {
        DmxDevice d;
        d.id = x.value("id", 0);
        d.name = x.value("name", "Controlador");
        d.enabled = x.value("enabled", true);
        d.protocol = enumFrom(x, "protocol", DmxProtocol::ArtNet, 2);
        d.host = x.value("host", "");
        d.rate = x.value("rate", 40);
        p.dmxDevices.push_back(d);
      }
      for (auto &x : jd.value("leds", json::array())) {
        LedMapping m;
        m.id = x.value("id", 0);
        m.name = x.value("name", "Fita LED");
        m.enabled = x.value("enabled", true);
        m.locked = x.value("locked", false);
        if (x.contains("size")) { m.cols = x["size"][0]; m.rows = x["size"][1]; }
        m.format = enumFrom(x, "format", LedFormat::RGB, 5);
        m.wide = x.value("wide", false);
        m.zigzag = x.value("zigzag", false);
        m.vertical = x.value("vertical", false);
        m.flipX = x.value("flipX", false);
        m.flipY = x.value("flipY", false);
        m.gamma = x.value("gamma", 2.2f);
        m.brightness = x.value("brightness", 1.0f);
        m.deviceId = x.value("device", -1);
        m.universe = x.value("universe", 0);
        m.channel = x.value("channel", 1);
        if (x.contains("area"))
          for (size_t i = 0; i < 4 && i < x["area"].size(); ++i) m.area.corners[i] = vecFrom(x["area"][i]);
        p.ledMappings.push_back(m);
      }
    }
    if (j.contains("spout")) {
      p.spoutSend = j["spout"].value("send", false);
      p.spoutName = j["spout"].value("name", "Lumen");
    }

    for (auto &jm : j.value("library", json::array())) {
      MediaItem m;
      m.id = jm.value("id", 0);
      m.name = jm.value("name", "");
      m.type = enumFrom(jm, "type", MediaType::Video, 5);
      if (m.type == MediaType::Generator) {
        m.generator = enumFrom(jm, "generator", GeneratorKind::TestCard, int(GeneratorKind::COUNT));
        if (jm.contains("color1")) fromJson(jm["color1"], m.color1);
        if (jm.contains("color2")) fromJson(jm["color2"], m.color2);
        m.param1 = jm.value("param1", 1.0f);
        m.param2 = jm.value("param2", 0.5f);
        if (jm.contains("size")) { m.width = jm["size"][0]; m.height = jm["size"][1]; }
      } else if (m.type == MediaType::Live) {
        m.path = jm.value("path", "");
        m.liveFormat = jm.value("format", "");
        m.liveOptions = jm.value("options", "");
      } else if (m.type == MediaType::Spout) {
        m.path = jm.value("path", "");
      } else {
        m.path = makeAbsolute(jm.value("path", ""), baseDir);
      }
      p.library.push_back(m);
    }

    for (auto &jl : j.value("layers", json::array())) {
      Layer l;
      l.id = jl.value("id", 0);
      l.name = jl.value("name", "Layer");
      l.visible = jl.value("visible", true);
      l.opacity = jl.value("opacity", 1.0f);
      l.blend = enumFrom(jl, "blend", BlendMode::Normal, 4);
      l.volume = jl.value("volume", 1.0f);
      l.mute = jl.value("mute", false);
      if (jl.contains("fx")) {
        auto &f = jl["fx"];
        l.fx.brightness = f.value("brightness", 0.0f);
        l.fx.contrast = f.value("contrast", 1.0f);
        l.fx.saturation = f.value("saturation", 1.0f);
        l.fx.hue = f.value("hue", 0.0f);
        l.fx.invert = f.value("invert", false);
        if (f.contains("tint")) fromJson(f["tint"], l.fx.tint);
      }
      if (jl.contains("transform")) {
        auto &t = jl["transform"];
        if (t.contains("position")) l.transform.position = vecFrom(t["position"]);
        l.transform.scale = t.value("scale", 1.0f);
        l.transform.rotation = t.value("rotation", 0.0f);
      }
      for (auto &js : jl.value("slices", json::array())) {
        Slice s;
        s.name = js.value("name", "Slice");
        s.enabled = js.value("enabled", true);
        if (js.contains("crop")) {
          auto &c = js["crop"];
          s.u0 = c[0]; s.v0 = c[1]; s.u1 = c[2]; s.v1 = c[3];
        }
        if (js.contains("warp")) fromJson(js["warp"], s.warp);
        l.slices.push_back(s);
      }
      if (l.slices.empty()) {
        Slice s;
        s.warp.setRect({0, 0}, {float(p.canvasW), float(p.canvasH)});
        l.slices.push_back(s);
      }
      l.masks = masksFromJson(jl.value("masks", json::array()));
      p.layers.push_back(l);
    }

    for (auto &jc : j.value("columns", json::array())) {
      Column c;
      c.id = jc.value("id", 0);
      c.name = jc.value("name", "");
      c.transition = jc.value("transition", -1.0f);
      c.notes = jc.value("notes", "");
      c.transitionType = enumFrom(jc, "transitionType", TransitionType::Crossfade, int(TransitionType::COUNT));
      for (auto &a : jc.value("oscActions", json::array()))
        if (a.is_string()) c.oscActions.push_back(a.get<std::string>());
      for (auto &cell : jc.value("cells", json::array())) {
        Cell ce;
        ce.mediaId = cell.value("media", -1);
        ce.loop = cell.value("loop", true);
        ce.speed = cell.value("speed", 1.0f);
        c.cells[cell.value("layer", -1)] = ce;
      }
      p.columns.push_back(c);
    }

    for (auto &jo : j.value("outputs", json::array())) {
      OutputConfig o;
      o.id = jo.value("id", 0);
      o.name = jo.value("name", "Output");
      o.enabled = jo.value("enabled", true);
      o.monitor = jo.value("monitor", -1);
      if (jo.contains("window")) {
        auto &w = jo["window"];
        o.winX = w[0]; o.winY = w[1]; o.winW = w[2]; o.winH = w[3];
      }
      if (jo.contains("source")) {
        auto &s = jo["source"];
        o.srcX = s[0]; o.srcY = s[1]; o.srcW = s[2]; o.srcH = s[3];
      }
      o.warp.setRect({0, 0}, {1, 1});
      if (jo.contains("warp")) fromJson(jo["warp"], o.warp);
      if (jo.contains("softEdge")) {
        auto &s = jo["softEdge"];
        o.softEdge.left = s.value("left", 0.0f);
        o.softEdge.right = s.value("right", 0.0f);
        o.softEdge.top = s.value("top", 0.0f);
        o.softEdge.bottom = s.value("bottom", 0.0f);
        o.softEdge.gamma = s.value("gamma", 2.2f);
        o.softEdge.power = s.value("power", 2.0f);
      }
      if (jo.contains("pattern")) o.pattern = enumFrom(jo, "pattern", TestPattern::None, int(TestPattern::COUNT));
      else if (jo.value("testCard", false)) o.pattern = TestPattern::Card; // v0.1 files
      if (jo.contains("color")) {
        auto &c = jo["color"];
        o.color.brightness = c.value("brightness", 1.0f);
        o.color.contrast = c.value("contrast", 1.0f);
        o.color.gamma = c.value("gamma", 1.0f);
        if (c.contains("gain")) fromJson(c["gain"], o.color.gain);
      }
      if (jo.contains("blackLevel")) fromJson(jo["blackLevel"], o.blackLevel);
      o.masks = masksFromJson(jo.value("masks", json::array()));
      p.outputs.push_back(o);
    }

    for (auto &jm : j.value("mappings", json::array())) {
      InputMapping m;
      m.enabled = jm.value("enabled", true);
      m.address = jm.value("address", "");
      m.argIndex = jm.value("arg", 0);
      if (jm.contains("in")) { m.inMin = jm["in"][0]; m.inMax = jm["in"][1]; }
      m.target = enumFrom(jm, "target", MappingTarget::Opacity, int(MappingTarget::COUNT));
      m.layerId = jm.value("layer", -1);
      if (jm.contains("out")) { m.outMin = jm["out"][0]; m.outMax = jm["out"][1]; }
      m.smoothing = jm.value("smoothing", 0.0f);
      p.mappings.push_back(m);
    }

    // Never hand out an id that already exists, even if the file's nextId is stale.
    int maxId = 0;
    for (auto &m : p.library) maxId = std::max(maxId, m.id);
    for (auto &l : p.layers) maxId = std::max(maxId, l.id);
    for (auto &c : p.columns) maxId = std::max(maxId, c.id);
    for (auto &o : p.outputs) maxId = std::max(maxId, o.id);
    for (auto &d : p.dmxDevices) maxId = std::max(maxId, d.id);
    for (auto &m : p.ledMappings) maxId = std::max(maxId, m.id);
    p.nextId = std::max(j.value("nextId", 1), maxId + 1);

    out = std::move(p);
    return true;
  } catch (const std::exception &e) {
    if (error) *error = e.what();
    return false;
  }
}

bool saveProject(Project &p, const std::string &path, std::string *error) {
  std::string baseDir = platform::toUtf8(platform::toPath(path).parent_path());
  if (!platform::writeFile(path, serializeProject(p, baseDir))) {
    if (error) *error = "could not write " + path;
    return false;
  }
  p.filePath = path;
  return true;
}

bool loadProject(const std::string &path, Project &out, std::string *error) {
  std::string text;
  if (!platform::readFile(path, text)) {
    if (error) *error = "could not read " + path;
    return false;
  }
  std::string baseDir = platform::toUtf8(platform::toPath(path).parent_path());
  if (!deserializeProject(text, baseDir, out, error)) return false;
  out.filePath = path;
  return true;
}

} // namespace lumen
