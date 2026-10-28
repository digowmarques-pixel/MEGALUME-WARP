// Project data model: everything that is saved to a .lumen file.
// Runtime state (decoders, textures, playback clocks) lives in the engine.
#pragma once

#include "core/warp.h"

#include <map>
#include <string>
#include <vector>

namespace lumen {

struct Color {
  float r = 0, g = 0, b = 0, a = 1;
};

// Values are stored in project files: only append.
enum class MediaType { Video, Image, Generator, Live, Spout };
enum class GeneratorKind { TestCard, Solid, Gradient, Grid, Plasma, Stripes, COUNT };
const char *generatorName(GeneratorKind k);

struct MediaItem {
  int id = 0;
  std::string name;
  MediaType type = MediaType::Video;
  std::string path; // absolute at runtime, stored relative to the project when possible
  // Generator settings.
  GeneratorKind generator = GeneratorKind::TestCard;
  Color color1{1, 1, 1, 1};
  Color color2{0, 0, 0, 1};
  float param1 = 1.0f; // speed / angle / cell size depending on generator
  float param2 = 0.5f;
  int width = 1920, height = 1080; // generator render size
  // Live input (camera, capture card, network stream): `path` is the URL / device.
  std::string liveFormat;  // "dshow", "v4l2", "lavfi", "" (network URL)...
  std::string liveOptions; // "video_size=1280x720:framerate=30"
  // Spout receiver: `path` is the sender name ("" = active sender).
};

enum class BlendMode { Normal, Add, Multiply, Screen };
const char *blendName(BlendMode b);

// A region of the media (crop in UV) mapped onto a surface with its own warp.
struct Slice {
  std::string name = "Slice";
  float u0 = 0, v0 = 0, u1 = 1, v1 = 1;
  Warp warp; // canvas pixels
  bool enabled = true;
};

struct Mask {
  std::string name = "Mask";
  std::vector<Vec2> points; // canvas pixels
  bool subtract = false;    // false: reveal inside, true: hide inside
  float feather = 0;        // pixels
  bool enabled = true;
};

struct LayerFx {
  float brightness = 0;  // -1..1
  float contrast = 1;    // 0..2
  float saturation = 1;  // 0..2
  float hue = 0;         // degrees
  bool invert = false;
  Color tint{1, 1, 1, 1};
};

struct LayerTransform {
  Vec2 position{0, 0}; // offset in canvas px
  float scale = 1;
  float rotation = 0; // degrees
};

struct Layer {
  int id = 0;
  std::string name = "Layer";
  bool visible = true;
  float opacity = 1;
  float volume = 1; // audio of the media playing in this layer
  bool mute = false;
  BlendMode blend = BlendMode::Normal;
  LayerFx fx;
  LayerTransform transform;
  std::vector<Slice> slices;
  std::vector<Mask> masks;
};

struct Cell {
  int mediaId = -1;
  bool loop = true;
  float speed = 1;
};

struct Column {
  int id = 0;
  std::string name;
  float transition = -1; // seconds, <0 = project default
  std::map<int, Cell> cells; // by layer id
  // OSC messages sent to every OSC output target when the column is launched,
  // one per line: "/address arg1 arg2" (numbers or "quoted strings").
  std::vector<std::string> oscActions;
};

struct OscTarget {
  bool enabled = true;
  std::string host = "127.0.0.1";
  int port = 9000;
  bool feedback = true; // also send /lumen/status/... messages
};

struct SoftEdge {
  float left = 0, right = 0, top = 0, bottom = 0; // fraction of output size
  float gamma = 2.2f;
  float power = 2.0f;
};

struct OutputConfig {
  int id = 0;
  std::string name = "Output";
  bool enabled = true;
  int monitor = -1;               // -1: windowed
  int winX = 100, winY = 100, winW = 960, winH = 540;
  float srcX = 0, srcY = 0, srcW = 1920, srcH = 1080; // canvas region (px)
  Warp warp;                      // output-normalised space (0..1)
  SoftEdge softEdge;
  bool testCard = false;
};

// Values are stored in project files: only append.
enum class MappingTarget {
  Opacity, PositionX, PositionY, Scale, Rotation, Brightness, Contrast, Saturation, Hue,
  Speed, LaunchColumn, Go, StopAll, Volume, MasterOpacity, MasterVolume, Blackout, COUNT
};
const char *mappingTargetName(MappingTarget t);
// Continuous parameter of a layer (needs a layer, uses the output range).
inline bool mappingTargetNeedsLayer(MappingTarget t) {
  return t < MappingTarget::LaunchColumn || t == MappingTarget::Volume;
}
// Trigger fired when the value crosses the middle of the input range.
inline bool mappingTargetIsTrigger(MappingTarget t) {
  return t == MappingTarget::LaunchColumn || t == MappingTarget::Go ||
         t == MappingTarget::StopAll || t == MappingTarget::Blackout;
}

// Maps an incoming OSC value onto a parameter (like "MIDI learn").
struct InputMapping {
  bool enabled = true;
  std::string address = "/sensor/1";
  int argIndex = 0;
  float inMin = 0, inMax = 1;
  MappingTarget target = MappingTarget::Opacity;
  int layerId = -1;
  float outMin = 0, outMax = 1; // for LaunchColumn: outMin = column number (1-based)
  float smoothing = 0;          // 0 = immediate, 0.99 = very slow
};

struct Project {
  std::string filePath; // not serialised
  int canvasW = 1920, canvasH = 1080;
  Color background{0, 0, 0, 1};
  float defaultTransition = 1.0f;
  int oscPort = 8000;
  bool oscEnabled = true;
  std::vector<OscTarget> oscTargets;
  bool midiEnabled = true;
  std::string audioDevice; // "" = system default
  float masterVolume = 1;
  bool hardwareDecode = true;
  bool spoutSend = false;       // publish the canvas as a Spout sender (Windows)
  std::string spoutName = "Lumen";
  int nextId = 1;

  std::vector<MediaItem> library;
  std::vector<Layer> layers; // index 0 is the front-most layer
  std::vector<Column> columns;
  std::vector<OutputConfig> outputs;
  std::vector<InputMapping> mappings;

  int newId() { return nextId++; }
  MediaItem *media(int id);
  Layer *layer(int id);
  int layerIndex(int id) const;
  OutputConfig *output(int id);

  Layer &addLayer(const std::string &name = "");
  Column &addColumn();
  OutputConfig &addOutput();
  MediaItem &addMedia(const std::string &path);
  MediaItem &addGenerator(GeneratorKind kind);
  MediaItem &addLive(const std::string &name, const std::string &url, const std::string &format,
                     const std::string &options);
  MediaItem &addSpout(const std::string &senderName);
  void removeLayer(int id);
  void removeMedia(int id);

  static Project makeDefault();
};

std::string serializeProject(const Project &p, const std::string &baseDir);
bool deserializeProject(const std::string &json, const std::string &baseDir, Project &out,
                        std::string *error = nullptr);
bool saveProject(Project &p, const std::string &path, std::string *error = nullptr);
bool loadProject(const std::string &path, Project &out, std::string *error = nullptr);

bool isImagePath(const std::string &path);
bool isMediaPath(const std::string &path);

} // namespace lumen
