#include "core/project.h"

#include "platform/platform.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <functional>
#include <cctype>

namespace lumen {

// float as the number type: values print in their shortest form (0.1, not 0.10000000149).
using json = nlohmann::basic_json<std::map, std::vector, std::string, bool, std::int64_t,
                                  std::uint64_t, float>;

static const char *kGeneratorNames[] = {"Test card", "Solid color", "Gradient",
                                         "Grid",      "Plasma",      "Stripes"};
static const char *kBlendNames[] = {"Normal", "Add", "Multiply", "Screen", "Alpha matte", "Alpha matte inverted", "Luma matte", "Luma matte inverted"};
static const char *kTargetNames[] = {"Opacity",    "Position X", "Position Y",   "Scale",
                                     "Rotation",   "Brightness", "Contrast",     "Saturation",
                                     "Hue",        "Speed",      "Launch column", "Go (next)",
                                     "Stop all",   "Volume",     "Master opacity", "Master volume",
                                     "Blackout",   "DMX master", "Transition fader"};

const char *generatorName(GeneratorKind k) { return kGeneratorNames[int(k)]; }
const char *blendName(BlendMode b) { return kBlendNames[int(b)]; }
const char *mappingTargetName(MappingTarget t) { return kTargetNames[int(t)]; }

const char *endModeName(EndMode m) {
  static const char *n[] = {"Loop", "Freeze", "Stop", "Next column"};
  return n[int(m)];
}
const char *transformerName(Transformer t) {
  static const char *n[] = {"Linear", "Switch", "Toggle", "Accumulator"};
  return n[int(t)];
}
const char *scheduleActionName(ScheduleAction a) {
  static const char *n[] = {"Launch column", "Go (next)", "Stop all", "Blackout on", "Blackout off", "Command"};
  return n[int(a)];
}

bool Mask::isBezier(size_t i) const {
  auto nz = [](Vec2 v) { return v.x != 0 || v.y != 0; };
  return (i < tanIn.size() && nz(tanIn[i])) || (i < tanOut.size() && nz(tanOut[i]));
}

void Mask::ensureTangents() {
  tanIn.resize(points.size());
  tanOut.resize(points.size());
}

Vec2 maskCenter(const Mask &m) {
  Vec2 c;
  for (auto &p : m.points) c += p;
  return m.points.empty() ? c : c * (1.0f / float(m.points.size()));
}

std::vector<Vec2> maskOutline(const Mask &m, int steps) {
  std::vector<Vec2> out;
  size_t n = m.points.size();
  if (n == 0) return out;
  auto tin = [&](size_t i) { return i < m.tanIn.size() ? m.tanIn[i] : Vec2{}; };
  auto tout = [&](size_t i) { return i < m.tanOut.size() ? m.tanOut[i] : Vec2{}; };
  for (size_t i = 0; i < n; ++i) {
    size_t j = (i + 1) % n;
    Vec2 p0 = m.points[i], p3 = m.points[j];
    Vec2 c1 = p0 + tout(i), c2 = p3 + tin(j);
    out.push_back(p0);
    bool curve = tout(i).x != 0 || tout(i).y != 0 || tin(j).x != 0 || tin(j).y != 0;
    if (!curve || n < 2) continue;
    for (int k = 1; k < steps; ++k) {
      float t = float(k) / float(steps), u = 1 - t;
      out.push_back(p0 * (u * u * u) + c1 * (3 * u * u * t) + c2 * (3 * u * t * t) + p3 * (t * t * t));
    }
  }
  if (m.scale != 1) {
    Vec2 c = maskCenter(m);
    for (auto &p : out) p = c + (p - c) * m.scale;
  }
  return out;
}

const char *easeName(Ease e) {
  static const char *n[] = {"Linear", "Suave", "Acelera", "Desacelera", "Degrau"};
  return n[int(e)];
}

float TimelineTrack::valueAt(float t) const {
  if (keys.empty()) return 0;
  if (t <= keys.front().time) return keys.front().value;
  if (t >= keys.back().time) return keys.back().value;
  auto it = std::upper_bound(keys.begin(), keys.end(), t, [](float x, const Keyframe &k) { return x < k.time; });
  const Keyframe &b = *it, &a = *(it - 1);
  float span = b.time - a.time;
  float u = span > 0 ? (t - a.time) / span : 1.0f;
  switch (a.ease) {
  case Ease::Linear: break;
  case Ease::InOut: u = u * u * (3 - 2 * u); break;
  case Ease::In: u = u * u; break;
  case Ease::Out: u = 1 - (1 - u) * (1 - u); break;
  case Ease::Hold: u = 0; break;
  default: break;
  }
  return a.value + (b.value - a.value) * u;
}

Timeline *Project::timeline(int id) {
  for (auto &t : timelines)
    if (t.id == id) return &t;
  return nullptr;
}

// "Layer 3": the count plus one, or the next number not in use (after removals
// the count repeats a name, and OSC finds layers, timelines and LEDs by name).
template <class Items> static std::string nextName(const std::string &prefix, const Items &items) {
  for (size_t n = items.size() + 1;; ++n) {
    std::string name = prefix + std::to_string(n);
    if (std::none_of(items.begin(), items.end(), [&](const auto &it) { return it.name == name; })) return name;
  }
}

Timeline &Project::addTimeline() {
  Timeline t;
  t.id = newId();
  t.name = nextName("Timeline ", timelines);
  timelines.push_back(t);
  return timelines.back();
}

Surface *Project::surface(int id) {
  for (auto &s : surfaces)
    if (s.id == id) return &s;
  return nullptr;
}

Surface &Project::addSurface(const Warp &w) {
  Surface s;
  s.id = newId();
  s.name = nextName("Surface ", surfaces);
  static const Color palette[] = {{1.0f, 0.77f, 0.0f, 1}, {0.3f, 0.75f, 1, 1}, {0.5f, 0.9f, 0.4f, 1},
                                  {0.95f, 0.4f, 0.7f, 1},  {0.9f, 0.9f, 0.3f, 1}, {0.6f, 0.5f, 1, 1}};
  s.color = palette[surfaces.size() % 6];
  s.warp = w;
  surfaces.push_back(s);
  return surfaces.back();
}

void Project::pushSurfaceEdits() {
  for (auto &l : layers)
    for (auto &sl : l.slices)
      if (sl.surfaceId >= 0)
        if (Surface *s = surface(sl.surfaceId); s && s->warp != sl.warp) s->warp = sl.warp;
  resolveSurfaces();
}

void Project::resolveSurfaces() {
  for (auto &l : layers)
    for (auto &sl : l.slices) {
      if (sl.surfaceId < 0) continue;
      if (const Surface *s = surface(sl.surfaceId)) sl.warp = s->warp;
      else sl.surfaceId = -1; // surface deleted: the slice keeps its last mapping
    }
}

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

Layer &Project::addLayer(const std::string &name, int canvasId) {
  Layer l;
  l.id = newId();
  l.name = name.empty() ? nextName("Layer ", layers) : name;
  l.canvasId = hasCanvas(canvasId) ? canvasId : 0;
  Slice s;
  s.warp.setRect({0, 0}, {float(canvasWidth(l.canvasId)), float(canvasHeight(l.canvasId))});
  l.slices.push_back(s);
  layers.insert(layers.begin(), l); // new layers go on top
  return layers.front();
}

Column &Project::addColumn() {
  Column c;
  c.id = newId();
  c.name = nextName("Cue ", columns);
  columns.push_back(c);
  return columns.back();
}

OutputConfig &Project::addOutput() {
  OutputConfig o;
  o.id = newId();
  o.name = nextName("Output ", outputs);
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

CanvasConfig *Project::extraCanvas(int id) {
  if (id == 0) return nullptr;
  for (auto &c : canvases)
    if (c.id == id) return &c;
  return nullptr;
}

std::vector<int> Project::canvasIds() const {
  std::vector<int> ids{0};
  for (auto &c : canvases) ids.push_back(c.id);
  return ids;
}

int Project::canvasWidth(int id) const {
  const CanvasConfig *c = const_cast<Project *>(this)->extraCanvas(id);
  return std::max(16, c ? c->width : canvasW);
}

int Project::canvasHeight(int id) const {
  const CanvasConfig *c = const_cast<Project *>(this)->extraCanvas(id);
  return std::max(16, c ? c->height : canvasH);
}

Color Project::canvasBackground(int id) const {
  const CanvasConfig *c = const_cast<Project *>(this)->extraCanvas(id);
  return c ? c->background : background;
}

std::string Project::canvasLabel(int id) const {
  const CanvasConfig *c = const_cast<Project *>(this)->extraCanvas(id);
  return c ? c->name : canvasName;
}

CanvasConfig &Project::addCanvas(int w, int h) {
  CanvasConfig c;
  c.id = newId();
  c.name = "Canvas " + std::to_string(canvases.size() + 2);
  c.width = std::clamp(w, 16, 16384);
  c.height = std::clamp(h, 16, 16384);
  canvases.push_back(c);
  return canvases.back();
}

void Project::removeCanvas(int id) {
  if (id == 0) return;
  std::vector<int> gone;
  for (auto &l : layers)
    if (l.canvasId == id) gone.push_back(l.id);
  for (int l : gone) removeLayer(l);
  for (auto &o : outputs)
    if (o.canvasId == id) o.canvasId = 0;
  for (auto &m : ledMappings)
    if (m.canvasId == id) m.canvasId = 0;
  for (auto &m : library)
    if (m.type == MediaType::Canvas && m.canvasRef == id) m.canvasRef = 0;
  canvases.erase(std::remove_if(canvases.begin(), canvases.end(), [&](auto &c) { return c.id == id; }),
                 canvases.end());
}

std::string builtinEffectsDir() {
  static std::string dir = [] {
    namespace fs = std::filesystem;
    std::error_code ec;
    fs::path p = platform::toPath(platform::executableDir()) / "effects";
    if (fs::is_directory(p, ec)) return platform::toUtf8(p);
#ifdef LUMEN_SOURCE_DIR
    p = fs::path(LUMEN_SOURCE_DIR) / "effects"; // development build
    if (fs::is_directory(p, ec)) return platform::toUtf8(p);
#endif
    return platform::toUtf8(platform::toPath(platform::executableDir()) / "effects");
  }();
  return dir;
}

MediaItem &Project::addText(const std::string &text) {
  MediaItem m;
  m.id = newId();
  m.type = MediaType::Text;
  m.name = "Texto";
  m.text = text;
  m.color1 = {1, 1, 1, 1};
  m.color2 = {0, 0, 0, 0};
  library.push_back(m);
  return library.back();
}

MediaItem &Project::addNdi(const std::string &source) {
  MediaItem m;
  m.id = newId();
  m.type = MediaType::NDI;
  m.name = "NDI: " + source;
  m.path = source;
  library.push_back(m);
  return library.back();
}

MediaItem &Project::addShader(const std::string &path) {
  MediaItem m;
  m.id = newId();
  m.type = MediaType::Shader;
  m.path = path;
  m.name = platform::toUtf8(platform::toPath(path).stem());
  library.push_back(m);
  return library.back();
}

MediaItem &Project::addCanvasMedia(int canvasId) {
  MediaItem m;
  m.id = newId();
  m.type = MediaType::Canvas;
  m.canvasRef = canvasId;
  m.name = "Canvas: " + canvasLabel(canvasId);
  m.width = canvasWidth(canvasId);
  m.height = canvasHeight(canvasId);
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
  d.name = nextName(protocol == DmxProtocol::ArtNet ? "Art-Net " : "sACN ", dmxDevices);
  dmxDevices.push_back(d);
  return dmxDevices.back();
}

LedMapping &Project::addLedMapping() {
  LedMapping m;
  m.id = newId();
  m.name = nextName("Fita LED ", ledMappings);
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
  for (auto &c : columns)
    c.states.erase(std::remove_if(c.states.begin(), c.states.end(), [&](auto &s) { return s.layerId == id; }),
                   c.states.end());
  for (auto &t : timelines)
    t.tracks.erase(std::remove_if(t.tracks.begin(), t.tracks.end(), [&](auto &tr) { return tr.layerId == id; }),
                   t.tracks.end());
}

void Project::removeMedia(int id) {
  library.erase(std::remove_if(library.begin(), library.end(), [&](auto &m) { return m.id == id; }),
                library.end());
  for (auto &c : columns)
    for (auto &[lid, cell] : c.cells)
      if (cell.mediaId == id) cell.mediaId = -1;
  for (auto &c : columns)
    if (c.stingerMediaId == id) c.stingerMediaId = -1;
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
      if (l.canvasId != 0) continue; // the layout belongs to the main canvas
      for (auto &sl : l.slices)
        for (auto &c : sl.warp.corners) scale(c);
      for (auto &m : l.masks) {
        for (auto &pt : m.points) scale(pt);
        for (auto &pt : m.tanIn) scale(pt);
        for (auto &pt : m.tanOut) scale(pt);
      }
      scale(l.transform.position);
    }
    for (auto &m : p.ledMappings)
      if (m.canvasId == 0)
        for (auto &c : m.area.corners) scale(c);
    for (auto &sf : p.surfaces)
      for (auto &c : sf.warp.corners) scale(c);
  }
  p.canvasW = lay.canvasW;
  p.canvasH = lay.canvasH;

  // Outputs of the main canvas are reused (warps and colours survive); the
  // outputs of other canvases are kept as they are, after them.
  size_t n = lay.projectors.size();
  std::vector<OutputConfig> mainOuts, others;
  for (auto &o : p.outputs) (o.canvasId == 0 ? mainOuts : others).push_back(o);
  p.outputs = mainOuts;
  while (p.outputs.size() < n) p.addOutput();
  p.outputs.resize(n);
  p.outputs.insert(p.outputs.end(), others.begin(), others.end());
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

// Built-in shaders are stored as "@lumen/<file>" so projects move between machines.
static std::string shaderToJson(const std::string &path, const std::string &baseDir);
static std::string shaderFromJson(const std::string &stored, const std::string &baseDir);

static std::string makeAbsolute(const std::string &path, const std::string &baseDir) {
  if (path.empty()) return path;
  auto p = platform::toPath(path);
  if (p.is_absolute() || baseDir.empty()) return path;
  return platform::toUtf8((platform::toPath(baseDir) / p).lexically_normal());
}

static std::string shaderToJson(const std::string &path, const std::string &baseDir) {
  if (path == kBlobTrackerId) return path;
  std::string dir = builtinEffectsDir();
  auto p = platform::toPath(path), d = platform::toPath(dir);
  if (!dir.empty() && p.parent_path() == d) return "@lumen/" + platform::toUtf8(p.filename());
  return makeRelative(path, baseDir);
}

static std::string shaderFromJson(const std::string &stored, const std::string &baseDir) {
  if (stored == kBlobTrackerId) return stored;
  if (stored.rfind("@lumen/", 0) == 0)
    return platform::toUtf8(platform::toPath(builtinEffectsDir()) / platform::toPath(stored.substr(7)));
  return makeAbsolute(stored, baseDir);
}

static json valuesToJson(const IsfValues &v) {
  json j = json::object();
  for (auto &[k, x] : v) j[k] = x;
  return j;
}

static IsfValues valuesFromJson(const json &j) {
  IsfValues v;
  if (!j.is_object()) return v;
  for (auto &[k, x] : j.items()) {
    std::vector<float> f;
    if (x.is_array()) for (auto &e : x) { if (e.is_number()) f.push_back(e.get<float>()); }
    else if (x.is_number()) f.push_back(x.get<float>());
    v[k] = f;
  }
  return v;
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
  if (!w.uvIsIdentity()) {
    j["meshCols"] = w.meshCols;
    j["meshRows"] = w.meshRows;
    if (!j.contains("mesh")) {
      j["mesh"] = json::array();
      for (auto &p : w.mesh) j["mesh"].push_back(toJson(p));
    }
    j["uvMesh"] = json::array();
    for (auto &p : w.uvMesh) j["uvMesh"].push_back(toJson(p));
  }
  return j;
}

static void fromJson(const json &j, Warp &w) {
  if (j.contains("corners"))
    for (size_t i = 0; i < 4 && i < j["corners"].size(); ++i) w.corners[i] = vecFrom(j["corners"][i]);
  if (j.contains("mesh")) {
    w.resetMesh(j.value("meshCols", 2), j.value("meshRows", 2));
    for (size_t i = 0; i < w.mesh.size() && i < j["mesh"].size(); ++i) w.mesh[i] = vecFrom(j["mesh"][i]);
    if (j.contains("uvMesh") && j["uvMesh"].size() == w.mesh.size()) {
      w.ensureUvMesh();
      for (size_t i = 0; i < w.uvMesh.size(); ++i) w.uvMesh[i] = vecFrom(j["uvMesh"][i]);
    }
  } else {
    w.resetMesh(2, 2);
  }
}

static json masksToJson(const std::vector<Mask> &masks) {
  json arr = json::array();
  for (auto &m : masks) {
    json jm = {{"name", m.name}, {"enabled", m.enabled}, {"subtract", m.subtract},
               {"feather", m.feather}, {"points", json::array()}};
    if (m.inverted) jm["inverted"] = true;
    if (m.opacity != 1) jm["opacity"] = m.opacity;
    if (m.scale != 1) jm["scale"] = m.scale;
    for (auto &pt : m.points) jm["points"].push_back(toJson(pt));
    bool curves = false;
    for (size_t i = 0; i < m.points.size(); ++i) curves |= m.isBezier(i);
    if (curves) {
      jm["tanIn"] = json::array();
      jm["tanOut"] = json::array();
      for (size_t i = 0; i < m.points.size(); ++i) {
        jm["tanIn"].push_back(toJson(i < m.tanIn.size() ? m.tanIn[i] : Vec2{}));
        jm["tanOut"].push_back(toJson(i < m.tanOut.size() ? m.tanOut[i] : Vec2{}));
      }
    }
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
    m.inverted = jm.value("inverted", false);
    m.opacity = jm.value("opacity", 1.0f);
    m.scale = jm.value("scale", 1.0f);
    for (auto &pt : jm.value("points", json::array())) m.points.push_back(vecFrom(pt));
    if (jm.contains("tanIn") && jm.contains("tanOut")) {
      for (auto &pt : jm["tanIn"]) m.tanIn.push_back(vecFrom(pt));
      for (auto &pt : jm["tanOut"]) m.tanOut.push_back(vecFrom(pt));
      m.ensureTangents();
    }
    out.push_back(m);
  }
  return out;
}

template <typename E> static E enumFrom(const json &j, const char *key, E def, int count) {
  int v = j.value(key, int(def));
  return (v >= 0 && v < count) ? E(v) : def;
}

int autoBlendOverlaps(Project &p, int canvasId) {
  std::vector<OutputConfig *> outs;
  for (auto &o : p.outputs)
    if (o.enabled && o.canvasId == canvasId && o.srcW > 0 && o.srcH > 0) outs.push_back(&o);
  for (auto *o : outs) o->softEdge.left = o->softEdge.right = o->softEdge.top = o->softEdge.bottom = 0;
  int seams = 0;
  for (size_t i = 0; i < outs.size(); ++i)
    for (size_t j = i + 1; j < outs.size(); ++j) {
      OutputConfig &a = *outs[i], &b = *outs[j];
      float x0 = std::max(a.srcX, b.srcX), x1 = std::min(a.srcX + a.srcW, b.srcX + b.srcW);
      float y0 = std::max(a.srcY, b.srcY), y1 = std::min(a.srcY + a.srcH, b.srcY + b.srcH);
      float iw = x1 - x0, ih = y1 - y0;
      if (iw <= 0.5f || ih <= 0.5f) continue;
      float minW = std::min(a.srcW, b.srcW), minH = std::min(a.srcH, b.srcH);
      // Side by side: the overlap is a vertical band (tall and narrow).
      bool horizontal = ih > 0.5f * minH && iw < 0.9f * minW;
      bool vertical = iw > 0.5f * minW && ih < 0.9f * minH;
      if (horizontal == vertical) continue; // corner of a grid, or one inside the other
      if (horizontal) {
        OutputConfig &l = a.srcX + a.srcW * 0.5f <= b.srcX + b.srcW * 0.5f ? a : b;
        OutputConfig &r = &l == &a ? b : a;
        l.softEdge.right = std::max(l.softEdge.right, iw / l.srcW);
        r.softEdge.left = std::max(r.softEdge.left, iw / r.srcW);
      } else {
        OutputConfig &t = a.srcY + a.srcH * 0.5f <= b.srcY + b.srcH * 0.5f ? a : b;
        OutputConfig &u = &t == &a ? b : a;
        t.softEdge.bottom = std::max(t.softEdge.bottom, ih / t.srcH);
        u.softEdge.top = std::max(u.softEdge.top, ih / u.srcH);
      }
      ++seams;
    }
  return seams;
}

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

Vec2 untransform(const Layer &l, Vec2 p) {
  const LayerTransform &t = l.transform;
  if (t.scale == 1 && t.rotation == 0 && t.position.x == 0 && t.position.y == 0) return p;
  Vec2 pivot = layerPivot(l);
  p = p - t.position;
  float a = -t.rotation * 3.14159265f / 180.0f;
  Vec2 d = p - pivot;
  d = {d.x * std::cos(a) - d.y * std::sin(a), d.x * std::sin(a) + d.y * std::cos(a)};
  float s = std::fabs(t.scale) < 1e-4f ? 1e-4f : t.scale;
  return pivot + d * (1.0f / s);
}

Vec2 layerDelta(const Layer &l, Vec2 m) {
  const LayerTransform &t = l.transform;
  float s = std::fabs(t.scale) < 1e-4f ? 1e-4f : t.scale;
  float a = -t.rotation * 3.14159265f / 180.0f, cs = std::cos(a), sn = std::sin(a);
  return Vec2{m.x * cs - m.y * sn, m.x * sn + m.y * cs} * (1.0f / s);
}

void keepInPlace(Layer &l, const std::function<void()> &edit) {
  Vec2 before = layerPivot(l);
  edit();
  const LayerTransform &t = l.transform;
  if (t.scale == 1 && t.rotation == 0) return;
  // Drawn point = pivot + s.R(q - pivot) + position: keep it for every q.
  Vec2 d = before - layerPivot(l);
  float a = t.rotation * 3.14159265f / 180.0f, cs = std::cos(a), sn = std::sin(a);
  Vec2 turned{t.scale * (d.x * cs - d.y * sn), t.scale * (d.x * sn + d.y * cs)};
  l.transform.position = l.transform.position + d - turned;
}

void softEdgeSpans(const Project &p, const OutputConfig &o, float span[4][2]) {
  for (int k = 0; k < 4; ++k) { span[k][0] = 1; span[k][1] = 0; } // empty, grown by each neighbour
  for (auto &n : p.outputs) {
    if (&n == &o || !n.enabled || n.canvasId != o.canvasId || n.srcW <= 0 || n.srcH <= 0 || o.srcW <= 0 || o.srcH <= 0)
      continue;
    float x0 = std::max(o.srcX, n.srcX), x1 = std::min(o.srcX + o.srcW, n.srcX + n.srcW);
    float y0 = std::max(o.srcY, n.srcY), y1 = std::min(o.srcY + o.srcH, n.srcY + n.srcH);
    if (x1 - x0 <= 0.5f || y1 - y0 <= 0.5f) continue; // not overlapping
    float oc = o.srcX + o.srcW * 0.5f, nc = n.srcX + n.srcW * 0.5f;
    float om = o.srcY + o.srcH * 0.5f, nm = n.srcY + n.srcH * 0.5f;
    auto grow = [&](int k, float a, float b) {
      span[k][0] = std::min(span[k][0], std::clamp(a, 0.0f, 1.0f));
      span[k][1] = std::max(span[k][1], std::clamp(b, 0.0f, 1.0f));
    };
    // A neighbour across an edge covers, along that edge, the overlap's extent.
    float ya = (y0 - o.srcY) / o.srcH, yb = (y1 - o.srcY) / o.srcH;
    float xa = (x0 - o.srcX) / o.srcW, xb = (x1 - o.srcX) / o.srcW;
    if (nc < oc && x0 <= o.srcX + 0.5f) grow(0, ya, yb);                // on the left
    if (nc > oc && x1 >= o.srcX + o.srcW - 0.5f) grow(1, ya, yb);       // on the right
    if (nm < om && y0 <= o.srcY + 0.5f) grow(2, xa, xb);                // above
    if (nm > om && y1 >= o.srcY + o.srcH - 0.5f) grow(3, xa, xb);       // below
  }
  for (int k = 0; k < 4; ++k)
    if (span[k][0] >= span[k][1]) { span[k][0] = 0; span[k][1] = 1; } // no neighbour: the whole edge
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
  j["redundancy"] = {{"timeout", p.redundancy.timeout}, {"tolerance", p.redundancy.tolerance},
                     {"takeover", p.redundancy.takeoverCommands}};
  j["audio"] = {{"device", p.audioDevice}, {"master", p.masterVolume}, {"channels", p.audioChannels},
                {"exclusive", p.audioExclusive}, {"bufferMs", p.audioBufferMs}};
  j["video"] = {{"hardwareDecode", p.hardwareDecode}, {"asyncUpload", p.asyncUpload}};
  j["spout"] = {{"send", p.spoutSend}, {"name", p.spoutName}};
  {
    const BlendLayoutParams &b = p.outputLayout;
    j["outputLayout"] = {{"cols", b.cols}, {"rows", b.rows}, {"size", {b.projW, b.projH}},
                         {"gapsX", b.gapsX}, {"gapsY", b.gapsY},
                         {"edges", {b.edgeLeft, b.edgeRight, b.edgeTop, b.edgeBottom}}};
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
               {"universe", m.universe}, {"channel", m.channel}, {"area", json::array()},
               {"canvas", m.canvasId}};
    for (auto &c : m.area.corners) jm["area"].push_back(toJson(c));
    j["dmx"]["leds"].push_back(jm);
  }
  j["nextId"] = p.nextId;
  j["canvases"] = json::array();
  for (auto &c : p.canvases)
    j["canvases"].push_back({{"id", c.id}, {"name", c.name}, {"size", {c.width, c.height}},
                             {"background", toJson(c.background)}});
  j["canvasName"] = p.canvasName;
  j["serialIn"] = {{"enabled", p.serialIn.enabled}, {"port", p.serialIn.port}, {"baud", p.serialIn.baud}};
  j["ndi"] = {{"send", p.ndiSend}, {"name", p.ndiName}};
  j["projectors"] = json::array();
  for (auto &pj : p.projectors) j["projectors"].push_back({{"name", pj.name}, {"host", pj.host}, {"password", pj.password}});
  {
    const TimecodeConfig &t = p.timecode;
    j["timecode"] = {{"source", int(t.source)}, {"ltcDevice", t.ltcDevice}, {"ltcChannel", t.ltcChannel},
                     {"freewheel", t.freewheel}, {"generate", t.generate}, {"genTimeline", t.genTimelineId},
                     {"genStart", t.genStart}, {"genFps", t.genFps}, {"genMtc", t.genMtc}, {"genMtcPort", t.genMtcPort},
                     {"genLtc", t.genLtc}, {"genLtcChannel", t.genLtcChannel}};
  }
  j["timelines"] = json::array();
  for (auto &t : p.timelines) {
    json jt = {{"id", t.id}, {"name", t.name}, {"duration", t.duration}, {"loop", t.loop}, {"loopStart", t.loopStart},
               {"goAtEnd", t.goAtEnd}, {"followTimecode", t.followTimecode}, {"timecodeOffset", t.timecodeOffset},
               {"tracks", json::array()}, {"flags", json::array()}};
    for (auto &tr : t.tracks) {
      json jk = json::array();
      for (auto &k : tr.keys) jk.push_back({k.time, k.value, int(k.ease)});
      jt["tracks"].push_back({{"layer", tr.layerId}, {"param", tr.param}, {"enabled", tr.enabled}, {"keys", jk}});
    }
    for (auto &f : t.flags) jt["flags"].push_back({{"time", f.time}, {"name", f.name}, {"command", f.command}});
    j["timelines"].push_back(jt);
  }
  j["surfaces"] = json::array();
  for (auto &sf : p.surfaces)
    j["surfaces"].push_back({{"id", sf.id}, {"name", sf.name}, {"color", toJson(sf.color)}, {"warp", toJson(sf.warp)}});
  j["schedule"] = json::array();
  for (auto &e : p.schedule)
    j["schedule"].push_back({{"enabled", e.enabled}, {"time", {e.hour, e.minute}}, {"days", e.days},
                             {"action", int(e.action)}, {"column", e.column}, {"command", e.command}});
  j["dmxInput"] = {{"enabled", p.dmxInput.enabled}, {"protocol", int(p.dmxInput.protocol)},
                   {"universe", p.dmxInput.universe}, {"count", p.dmxInput.count}};
  j["keyboardInput"] = p.keyboardInput;
  j["startup"] = {{"outputs", p.startOutputs}, {"column", p.startColumn}};

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
    } else if (m.type == MediaType::Canvas) {
      jm["canvas"] = m.canvasRef;
    } else if (m.type == MediaType::Text) {
      jm["text"] = m.text;
      jm["path"] = m.path.empty() ? std::string() : makeRelative(m.path, baseDir);
      jm["font"] = m.font.empty() ? std::string() : makeRelative(m.font, baseDir);
      jm["fontSize"] = m.fontSize;
      jm["align"] = {m.align, m.valign};
      jm["lineSpacing"] = m.lineSpacing;
      jm["color1"] = toJson(m.color1);
      jm["color2"] = toJson(m.color2);
      jm["size"] = {m.width, m.height};
    } else if (m.type == MediaType::NDI) {
      jm["path"] = m.path;
    } else if (m.type == MediaType::Shader) {
      jm["path"] = shaderToJson(m.path, baseDir);
      jm["params"] = valuesToJson(m.shaderParams);
      jm["size"] = {m.width, m.height};
    } else {
      jm["path"] = makeRelative(m.path, baseDir);
    }
    j["library"].push_back(jm);
  }

  for (auto &l : p.layers) {
    json jl = {{"id", l.id}, {"name", l.name}, {"visible", l.visible}, {"opacity", l.opacity},
               {"blend", int(l.blend)}, {"volume", l.volume}, {"mute", l.mute}};
    if (l.canvasId != 0) jl["canvas"] = l.canvasId;
    if (l.audioOutput != 0) jl["audioOutput"] = l.audioOutput;
    if (!l.audioMatrix.empty()) {
      jl["audioMatrix"] = json::array();
      for (auto &a : l.audioMatrix) jl["audioMatrix"].push_back(json::array({a.src, a.dst, a.gain}));
    }
    jl["fx"] = {{"brightness", l.fx.brightness}, {"contrast", l.fx.contrast},
                {"saturation", l.fx.saturation}, {"hue", l.fx.hue},
                {"invert", l.fx.invert}, {"tint", toJson(l.fx.tint)}};
    jl["transform"] = {{"position", toJson(l.transform.position)},
                       {"scale", l.transform.scale}, {"rotation", l.transform.rotation}};
    for (auto &s : l.slices)
      jl["slices"].push_back({{"name", s.name}, {"enabled", s.enabled}, {"surface", s.surfaceId},
                              {"crop", {s.u0, s.v0, s.u1, s.v1}}, {"warp", toJson(s.warp)}});
    jl["masks"] = masksToJson(l.masks);
    for (auto &fx : l.effects)
      jl["effects"].push_back({{"path", shaderToJson(fx.path, baseDir)}, {"name", fx.name},
                               {"enabled", fx.enabled}, {"params", valuesToJson(fx.params)}});
    j["layers"].push_back(jl);
  }

  for (auto &c : p.columns) {
    json jc = {{"id", c.id}, {"name", c.name}, {"transition", c.transition}, {"cells", json::array()},
               {"transitionType", int(c.transitionType)}};
    if (!c.notes.empty()) jc["notes"] = c.notes;
    if (!c.oscActions.empty()) jc["oscActions"] = c.oscActions;
    if (c.autoGo >= 0)
      jc["autoGo"] = {{"wait", c.autoGo}, {"afterTransition", c.autoGoAfterTransition}, {"target", c.autoGoTarget}};
    if (c.color[0] > 0 || c.color[1] > 0 || c.color[2] > 0) jc["color"] = {c.color[0], c.color[1], c.color[2]};
    if (c.timelineId >= 0) jc["timeline"] = c.timelineId;
    if (!c.transitionShader.empty()) jc["transitionShader"] = shaderToJson(c.transitionShader, baseDir);
    if (c.manualTransition) jc["manualTransition"] = true;
    if (c.stingerMediaId >= 0) jc["stinger"] = {{"media", c.stingerMediaId}, {"cut", c.stingerCut}};
    if (!c.commands.empty()) jc["commands"] = c.commands;
    if (c.timecodeCue >= 0) jc["timecodeCue"] = c.timecodeCue;
    for (auto &st : c.states)
      jc["states"].push_back({{"layer", st.layerId}, {"param", st.param}, {"value", st.value}});
    for (auto &[lid, cell] : c.cells)
      if (cell.mediaId >= 0)
        jc["cells"].push_back({{"layer", lid}, {"media", cell.mediaId}, {"loop", cell.loop},
                               {"end", int(cell.endMode)}, {"speed", cell.speed}});
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
         {"masks", masksToJson(o.masks)}, {"delay", o.delayFrames}, {"canvas", o.canvasId},
         {"spout", o.spoutName}, {"windowless", o.windowless}, {"ndi", o.ndiName}});
  }

  for (auto &m : p.mappings)
    j["mappings"].push_back({{"enabled", m.enabled}, {"address", m.address}, {"arg", m.argIndex},
                             {"in", {m.inMin, m.inMax}}, {"target", int(m.target)},
                             {"layer", m.layerId}, {"out", {m.outMin, m.outMax}},
                             {"smoothing", m.smoothing}, {"transformer", int(m.transformer)},
                             {"step", m.step}, {"columns", {m.firstColumn, m.lastColumn}}});
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
    if (j.contains("redundancy")) {
      const auto &r = j["redundancy"];
      p.redundancy.timeout = std::clamp(r.value("timeout", 1.0f), 0.2f, 30.0f);
      p.redundancy.tolerance = std::clamp(r.value("tolerance", 0.07f), 0.01f, 2.0f);
      p.redundancy.takeoverCommands = r.value("takeover", "");
    }
    if (j.contains("audio")) {
      p.audioDevice = j["audio"].value("device", "");
      p.masterVolume = j["audio"].value("master", 1.0f);
      p.audioChannels = std::clamp(j["audio"].value("channels", 0), 0, kMaxOutputChannels);
      p.audioExclusive = j["audio"].value("exclusive", false);
      p.audioBufferMs = std::clamp(j["audio"].value("bufferMs", 10), 2, 100);
    }
    if (j.contains("video")) {
      p.hardwareDecode = j["video"].value("hardwareDecode", true);
      p.asyncUpload = j["video"].value("asyncUpload", true);
    }
    if (j.contains("outputLayout")) {
      auto &b = j["outputLayout"];
      p.outputLayout.cols = std::clamp(b.value("cols", 1), 1, 16);
      p.outputLayout.rows = std::clamp(b.value("rows", 1), 1, 16);
      if (b.contains("size")) { p.outputLayout.projW = b["size"][0]; p.outputLayout.projH = b["size"][1]; }
      for (auto &g : b.value("gapsX", json::array())) p.outputLayout.gapsX.push_back(g.get<int>());
      for (auto &g : b.value("gapsY", json::array())) p.outputLayout.gapsY.push_back(g.get<int>());
      if (b.contains("edges") && b["edges"].is_array() && b["edges"].size() == 4) {
        BlendLayoutParams &o = p.outputLayout;
        int *e[] = {&o.edgeLeft, &o.edgeRight, &o.edgeTop, &o.edgeBottom};
        for (size_t k = 0; k < 4; ++k) *e[k] = std::clamp(b["edges"][k].is_number() ? b["edges"][k].get<int>() : 0, 0, 16384);
      }
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
        m.canvasId = x.value("canvas", 0);
        if (x.contains("area"))
          for (size_t i = 0; i < 4 && i < x["area"].size(); ++i) m.area.corners[i] = vecFrom(x["area"][i]);
        p.ledMappings.push_back(m);
      }
    }
    for (auto &x : j.value("canvases", json::array())) {
      CanvasConfig c;
      c.id = x.value("id", 0);
      c.name = x.value("name", "Canvas");
      if (x.contains("size")) { c.width = std::clamp(int(x["size"][0]), 16, 16384); c.height = std::clamp(int(x["size"][1]), 16, 16384); }
      if (x.contains("background")) fromJson(x["background"], c.background);
      if (c.id > 0) p.canvases.push_back(c);
    }
    p.canvasName = j.value("canvasName", "Canvas 1");
    if (j.contains("serialIn")) {
      p.serialIn.enabled = j["serialIn"].value("enabled", false);
      p.serialIn.port = j["serialIn"].value("port", "");
      p.serialIn.baud = j["serialIn"].value("baud", 115200);
    }
    if (j.contains("ndi")) {
      p.ndiSend = j["ndi"].value("send", false);
      p.ndiName = j["ndi"].value("name", "Lumen");
    }
    for (auto &x : j.value("projectors", json::array()))
      p.projectors.push_back({x.value("name", "Projetor"), x.value("host", ""), x.value("password", "")});
    if (j.contains("timecode")) {
      auto &x = j["timecode"];
      TimecodeConfig &t = p.timecode;
      t.source = enumFrom(x, "source", TimecodeSource::Off, int(TimecodeSource::COUNT));
      t.ltcDevice = x.value("ltcDevice", "");
      t.ltcChannel = x.value("ltcChannel", 0);
      t.freewheel = x.value("freewheel", 1.0f);
      t.generate = x.value("generate", false);
      t.genTimelineId = x.value("genTimeline", -1);
      t.genStart = x.value("genStart", 3600.0f);
      t.genFps = x.value("genFps", 25.0f);
      t.genMtc = x.value("genMtc", true);
      t.genMtcPort = x.value("genMtcPort", "");
      t.genLtc = x.value("genLtc", false);
      t.genLtcChannel = x.value("genLtcChannel", 0);
    }
    for (auto &x : j.value("timelines", json::array())) {
      Timeline t;
      t.id = x.value("id", 0);
      t.name = x.value("name", "Timeline");
      t.duration = std::max(0.1f, x.value("duration", 30.0f));
      t.loop = x.value("loop", false);
      t.loopStart = x.value("loopStart", 0.0f);
      t.goAtEnd = x.value("goAtEnd", false);
      t.followTimecode = x.value("followTimecode", false);
      t.timecodeOffset = x.value("timecodeOffset", 0.0f);
      for (auto &y : x.value("tracks", json::array())) {
        TimelineTrack tr;
        tr.layerId = y.value("layer", -1);
        tr.param = y.value("param", "opacity");
        tr.enabled = y.value("enabled", true);
        for (auto &k : y.value("keys", json::array()))
          if (k.is_array() && k.size() >= 2)
            tr.keys.push_back({k[0].get<float>(), k[1].get<float>(),
                               k.size() > 2 && k[2].get<int>() >= 0 && k[2].get<int>() < int(Ease::COUNT) ? Ease(k[2].get<int>()) : Ease::InOut});
        std::sort(tr.keys.begin(), tr.keys.end(), [](const Keyframe &a, const Keyframe &b) { return a.time < b.time; });
        t.tracks.push_back(tr);
      }
      for (auto &y : x.value("flags", json::array()))
        t.flags.push_back({y.value("time", 0.0f), y.value("name", ""), y.value("command", "")});
      p.timelines.push_back(t);
    }
    for (auto &x : j.value("surfaces", json::array())) {
      Surface sf;
      sf.id = x.value("id", 0);
      sf.name = x.value("name", "Surface");
      if (x.contains("color")) fromJson(x["color"], sf.color);
      if (x.contains("warp")) fromJson(x["warp"], sf.warp);
      p.surfaces.push_back(sf);
    }
    for (auto &x : j.value("schedule", json::array())) {
      ScheduleEntry e;
      e.enabled = x.value("enabled", true);
      if (x.contains("time")) { e.hour = std::clamp(int(x["time"][0]), 0, 23); e.minute = std::clamp(int(x["time"][1]), 0, 59); }
      e.days = x.value("days", 0x7F);
      e.action = enumFrom(x, "action", ScheduleAction::LaunchColumn, int(ScheduleAction::COUNT));
      e.column = x.value("column", 1);
      e.command = x.value("command", "");
      p.schedule.push_back(e);
    }
    if (j.contains("dmxInput")) {
      auto &x = j["dmxInput"];
      p.dmxInput.enabled = x.value("enabled", false);
      p.dmxInput.protocol = enumFrom(x, "protocol", DmxProtocol::ArtNet, 2);
      p.dmxInput.universe = x.value("universe", 0);
      p.dmxInput.count = std::clamp(x.value("count", 1), 1, 64);
    }
    p.keyboardInput = j.value("keyboardInput", true);
    if (j.contains("startup")) {
      p.startOutputs = j["startup"].value("outputs", false);
      p.startColumn = j["startup"].value("column", 0);
    }
    if (j.contains("spout")) {
      p.spoutSend = j["spout"].value("send", false);
      p.spoutName = j["spout"].value("name", "Lumen");
    }

    for (auto &jm : j.value("library", json::array())) {
      MediaItem m;
      m.id = jm.value("id", 0);
      m.name = jm.value("name", "");
      m.type = enumFrom(jm, "type", MediaType::Video, 9);
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
      } else if (m.type == MediaType::Canvas) {
        m.canvasRef = jm.value("canvas", 0);
      } else if (m.type == MediaType::Text) {
        m.text = jm.value("text", "");
        m.path = jm.value("path", "").empty() ? std::string() : makeAbsolute(jm.value("path", ""), baseDir);
        m.font = jm.value("font", "").empty() ? std::string() : makeAbsolute(jm.value("font", ""), baseDir);
        m.fontSize = jm.value("fontSize", 96.0f);
        if (jm.contains("align")) { m.align = jm["align"][0]; m.valign = jm["align"][1]; }
        m.lineSpacing = jm.value("lineSpacing", 1.15f);
        if (jm.contains("color1")) fromJson(jm["color1"], m.color1);
        if (jm.contains("color2")) fromJson(jm["color2"], m.color2);
        if (jm.contains("size")) { m.width = jm["size"][0]; m.height = jm["size"][1]; }
      } else if (m.type == MediaType::NDI) {
        m.path = jm.value("path", "");
      } else if (m.type == MediaType::Shader) {
        m.path = shaderFromJson(jm.value("path", ""), baseDir);
        m.shaderParams = valuesFromJson(jm.value("params", json::object()));
        if (jm.contains("size")) { m.width = jm["size"][0]; m.height = jm["size"][1]; }
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
      l.blend = enumFrom(jl, "blend", BlendMode::Normal, int(BlendMode::COUNT));
      l.volume = jl.value("volume", 1.0f);
      l.mute = jl.value("mute", false);
      l.audioOutput = std::max(0, jl.value("audioOutput", 0));
      l.audioMatrix.clear();
      if (jl.contains("audioMatrix"))
        for (auto &x : jl["audioMatrix"])
          if (x.is_array() && x.size() >= 2)
            l.audioMatrix.push_back({x[0].get<int>(), x[1].get<int>(), x.size() > 2 ? x[2].get<float>() : 1.0f});
      l.canvasId = jl.value("canvas", 0);
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
        s.surfaceId = js.value("surface", -1);
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
      for (auto &x : jl.value("effects", json::array())) {
        Effect fx;
        fx.path = shaderFromJson(x.value("path", ""), baseDir);
        fx.name = x.value("name", "");
        fx.enabled = x.value("enabled", true);
        fx.params = valuesFromJson(x.value("params", json::object()));
        l.effects.push_back(fx);
      }
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
      if (jc.contains("autoGo")) {
        auto &a = jc["autoGo"];
        c.autoGo = a.value("wait", -1.0f);
        c.autoGoAfterTransition = a.value("afterTransition", false);
        c.autoGoTarget = a.value("target", 0);
      }
      if (jc.contains("color"))
        for (int k = 0; k < 3; ++k) c.color[k] = jc["color"][size_t(k)];
      c.timelineId = jc.value("timeline", -1);
      if (jc.contains("transitionShader")) c.transitionShader = shaderFromJson(jc["transitionShader"].get<std::string>(), baseDir);
      c.manualTransition = jc.value("manualTransition", false);
      if (jc.contains("stinger")) {
        c.stingerMediaId = jc["stinger"].value("media", -1);
        c.stingerCut = jc["stinger"].value("cut", 0.5f);
      }
      for (auto &a : jc.value("commands", json::array()))
        if (a.is_string()) c.commands.push_back(a.get<std::string>());
      c.timecodeCue = jc.value("timecodeCue", -1.0f);
      for (auto &x : jc.value("states", json::array()))
        c.states.push_back({x.value("layer", -1), x.value("param", 0), x.value("value", 0.0f)});
      for (auto &cell : jc.value("cells", json::array())) {
        Cell ce;
        ce.mediaId = cell.value("media", -1);
        ce.loop = cell.value("loop", true);
        // Files before 0.6 only had "loop": a clip that did not loop held its last frame.
        ce.setEndMode(cell.contains("end") ? enumFrom(cell, "end", EndMode::Loop, int(EndMode::COUNT))
                                           : (ce.loop ? EndMode::Loop : EndMode::Freeze));
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
      o.delayFrames = std::clamp(jo.value("delay", 0), 0, 8);
      o.canvasId = jo.value("canvas", 0);
      o.spoutName = jo.value("spout", "");
      o.windowless = jo.value("windowless", false);
      o.ndiName = jo.value("ndi", "");
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
      m.transformer = enumFrom(jm, "transformer", Transformer::Linear, int(Transformer::COUNT));
      m.step = jm.value("step", 0.1f);
      if (jm.contains("columns")) { m.firstColumn = jm["columns"][0]; m.lastColumn = jm["columns"][1]; }
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
    for (auto &sf : p.surfaces) maxId = std::max(maxId, sf.id);
    for (auto &c : p.canvases) maxId = std::max(maxId, c.id);
    for (auto &t : p.timelines) maxId = std::max(maxId, t.id);
    // References to canvases that do not exist fall back to the main one.
    for (auto &l : p.layers) if (!p.hasCanvas(l.canvasId)) l.canvasId = 0;
    for (auto &o : p.outputs) if (!p.hasCanvas(o.canvasId)) o.canvasId = 0;
    for (auto &m : p.ledMappings) if (!p.hasCanvas(m.canvasId)) m.canvasId = 0;
    p.nextId = std::max(j.value("nextId", 1), maxId + 1);

    p.resolveSurfaces();
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
