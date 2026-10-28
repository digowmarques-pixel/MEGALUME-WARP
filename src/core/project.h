// Project data model: everything that is saved to a .lumen file.
// Runtime state (decoders, textures, playback clocks) lives in the engine.
#pragma once

#include "core/blend.h"
#include "core/dmx.h"
#include "core/isf.h"
#include "core/warp.h"

#include <map>
#include <string>
#include <vector>

namespace lumen {

struct Color {
  float r = 0, g = 0, b = 0, a = 1;
};

// Values are stored in project files: only append.
enum class MediaType { Video, Image, Generator, Live, Spout, Canvas, Shader };
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
  // Canvas (video routing): shows another canvas of the project.
  int canvasRef = 0;
  // Shader (ISF generator): `path` is the .fs file, rendered at width x height.
  IsfValues shaderParams;
};

// Built-in effects and generators (.fs files shipped next to lumen.exe).
std::string builtinEffectsDir();
constexpr const char *kBlobTrackerId = "lumen:blob";

// One effect of a layer's stack: an ISF filter (or the blob tracker).
struct Effect {
  std::string path; // ISF file, or kBlobTrackerId
  std::string name;
  bool enabled = true;
  IsfValues params;
  bool isBlobTracker() const { return path == kBlobTrackerId; }
};

// Canvas 0 is the main canvas (Project::canvasW / canvasH / background); more
// canvases can be added, each with its own layers ("video routing": a canvas
// can be shown inside another one as a media, outputs pick their canvas).
struct CanvasConfig {
  int id = 0;
  std::string name = "Canvas";
  int width = 1920, height = 1080;
  Color background{0, 0, 0, 1};
};

// Mattes: the layer is not drawn; it masks the layer just below it.
enum class BlendMode { Normal, Add, Multiply, Screen, AlphaMatte, AlphaMatteInverted, LumaMatte, LumaMatteInverted, COUNT };
inline bool isMatte(BlendMode b) { return b >= BlendMode::AlphaMatte && b < BlendMode::COUNT; }
const char *blendName(BlendMode b);

// A region of the media (crop in UV) mapped onto a surface with its own warp.
struct Slice {
  std::string name = "Slice";
  float u0 = 0, v0 = 0, u1 = 1, v1 = 1;
  Warp warp; // canvas pixels (a copy of the surface's warp when bound)
  bool enabled = true;
  int surfaceId = -1; // bound to a shared Surface: follows its mapping
};

// A named mapping shared by several layers ("surfaces" in Millumin): when the
// set or a projector moves, adjusting the surface moves every layer bound to it.
struct Surface {
  int id = 0;
  std::string name = "Surface";
  Color color{1.0f, 0.77f, 0.0f, 1};
  Warp warp; // canvas pixels
};

// Masks are applied in order, bottom of the list last (like Millumin): the
// first mask decides the start (Show: nothing visible, Hide: all visible).
struct Mask {
  std::string name = "Mask";
  std::vector<Vec2> points; // canvas pixels
  // Bezier handles, relative to their point (0,0 = sharp corner). Same size
  // as `points` when present (may be empty: all sharp).
  std::vector<Vec2> tanIn, tanOut;
  bool subtract = false;    // false: Show pixels inside, true: Hide pixels inside
  bool inverted = false;    // acts outside the shape instead of inside
  float opacity = 1;        // 0 = no effect
  float feather = 0;        // pixels
  float scale = 1;          // around the centre of the points
  bool enabled = true;
  bool isBezier(size_t i) const;
  void ensureTangents(); // tanIn / tanOut sized like points
};

// Polygon of a mask (bezier curves flattened, scale applied).
std::vector<Vec2> maskOutline(const Mask &m, int stepsPerCurve = 16);
Vec2 maskCenter(const Mask &m);

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
  int canvasId = 0;
  bool visible = true;
  float opacity = 1;
  float volume = 1; // audio of the media playing in this layer
  bool mute = false;
  BlendMode blend = BlendMode::Normal;
  LayerFx fx;
  LayerTransform transform;
  std::vector<Slice> slices;
  std::vector<Mask> masks;
  std::vector<Effect> effects; // applied in order to the media, before mapping
};

// What a media does when it reaches its end. Values are stored: only append.
enum class EndMode {
  Loop,       // starts again
  Freeze,     // holds the last frame
  Stop,       // disappears (fades out with the column transition)
  NextColumn, // launches the next column (auto-follow)
  COUNT
};
const char *endModeName(EndMode m);

struct Cell {
  int mediaId = -1;
  bool loop = true; // kept in sync with endMode == Loop
  EndMode endMode = EndMode::Loop;
  float speed = 1;
  void setEndMode(EndMode m) { endMode = m; loop = m == EndMode::Loop; }
};

// Value of a layer parameter recorded in a column ("states" in Millumin): on
// launch, the parameter moves to it during the column transition. Columns
// without a value inherit from the previous columns.
struct ColumnState {
  int layerId = -1;
  int param = 0; // MappingTarget (continuous layer parameter)
  float value = 0;
};

// Values are stored in project files: only append.
enum class TransitionType { Crossfade, Cut, ThroughBlack, COUNT };

struct Column {
  int id = 0;
  std::string name;
  std::string notes;     // cue notes for the operator
  TransitionType transitionType = TransitionType::Crossfade;
  float transition = -1; // seconds, <0 = project default
  std::map<int, Cell> cells; // by layer id
  // OSC messages sent to every OSC output target when the column is launched,
  // one per line: "/address arg1 arg2" (numbers or "quoted strings").
  std::vector<std::string> oscActions;
  std::vector<ColumnState> states;
  // Auto-go: after `autoGo` seconds (counted from the launch, or from the end
  // of the transition) launch another column. <0 = off.
  float autoGo = -1;
  bool autoGoAfterTransition = false;
  int autoGoTarget = 0; // 0 = next column, n = column n (1-based)
  float color[3] = {0, 0, 0}; // 0,0,0 = default
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

// Per-projector colour matching (applied to everything the output shows).
struct OutputColor {
  float brightness = 1, contrast = 1, gamma = 1;
  Color gain{1, 1, 1, 1};
  bool isIdentity() const {
    return brightness == 1 && contrast == 1 && gamma == 1 && gain.r == 1 && gain.g == 1 && gain.b == 1;
  }
};

// Values are stored in project files: only append.
enum class TestPattern { None, Card, BlendGrid, White, Black, COUNT };

struct OutputConfig {
  int id = 0;
  std::string name = "Output";
  bool enabled = true;
  int monitor = -1;               // -1: windowed
  int winX = 100, winY = 100, winW = 960, winH = 540;
  float srcX = 0, srcY = 0, srcW = 1920, srcH = 1080; // canvas region (px)
  Warp warp;                      // output-normalised space (0..1)
  SoftEdge softEdge;
  TestPattern pattern = TestPattern::None;
  OutputColor color;
  // Black level compensation: light a projector leaks when showing black
  // (0..1 of white, linear). Areas outside the overlaps are lifted to match
  // the brighter black inside them.
  Color blackLevel{0, 0, 0, 1};
  std::vector<Mask> masks;        // output space: points in 0..1, feather in output px
  int delayFrames = 0;            // compensates projectors with different latency
  int canvasId = 0;               // canvas this output shows
  // Spout sender of this output (warp, blend and colour applied), for LED
  // processors, other machines or capture. "" = off.
  std::string spoutName;
  bool windowless = false;        // no window: Spout only
};

// An Art-Net node or sACN receiver (LED controller such as DMXking LeDMX4 Pro,
// Enttec ODE...).
struct DmxDevice {
  int id = 0;
  std::string name = "Controlador";
  bool enabled = true;
  DmxProtocol protocol = DmxProtocol::ArtNet;
  std::string host; // "" = broadcast (Art-Net) / multicast (sACN); or the node IP
  int rate = 40;    // packets per second per universe
};

// A "DMX layer": reads the composed canvas inside `area` and sends it to LEDs.
struct LedMapping {
  int id = 0;
  std::string name = "Fita LED";
  bool enabled = true; // eye: hidden mappings send nothing
  bool locked = false; // lock: keeps sending, not editable in the workspace
  Warp area;           // canvas px (corners only)
  int cols = 60, rows = 1;
  LedFormat format = LedFormat::RGB;
  bool wide = false;   // 16-bit
  bool zigzag = false, vertical = false, flipX = false, flipY = false;
  float gamma = 2.2f;  // LED brightness curve (1 = linear)
  float brightness = 1;
  int canvasId = 0;    // canvas the LEDs sample
  int deviceId = -1;
  int universe = 0;    // as the protocol numbers it: Art-Net starts at 0, sACN at 1
  int channel = 1;
  LedLayoutParams layout() const {
    return {cols, rows, format, wide, zigzag, vertical, flipX, flipY, universe, channel};
  }
};

// Values are stored in project files: only append.
enum class MappingTarget {
  Opacity, PositionX, PositionY, Scale, Rotation, Brightness, Contrast, Saturation, Hue,
  Speed, LaunchColumn, Go, StopAll, Volume, MasterOpacity, MasterVolume, Blackout, DmxMaster, COUNT
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

// How an input value becomes the output value. Values are stored: only append.
enum class Transformer {
  Linear,      // output follows the input inside the range
  Switch,      // output is max while the input is high, min otherwise
  Toggle,      // each press flips the output between min and max
  Accumulator, // each press adds `step`; wraps back to min after max
  COUNT
};
const char *transformerName(Transformer t);

// Maps an incoming OSC value onto a parameter (like "MIDI learn"). Inputs are
// addressed as OSC: MIDI (/midi/...), keyboard (/key/...), DMX (/artnet/...,
// /sacn/...) and plain OSC.
struct InputMapping {
  bool enabled = true;
  std::string address = "/sensor/1";
  int argIndex = 0;
  float inMin = 0, inMax = 1;
  MappingTarget target = MappingTarget::Opacity;
  int layerId = -1;
  float outMin = 0, outMax = 1; // for LaunchColumn: outMin = column number (1-based)
  float smoothing = 0;          // 0 = immediate, 0.99 = very slow
  Transformer transformer = Transformer::Linear;
  float step = 0.1f;            // accumulator increment (output units)
  // Only active while the launched column is in this range (1-based, 0 = no limit).
  int firstColumn = 0, lastColumn = 0;
};

// Values are stored: only append.
enum class ScheduleAction { LaunchColumn, Go, Stop, BlackoutOn, BlackoutOff, COUNT };
const char *scheduleActionName(ScheduleAction a);

// Unattended installations: actions at a time of day on selected weekdays.
struct ScheduleEntry {
  bool enabled = true;
  int hour = 9, minute = 0;
  int days = 0x7F; // bit 0 = Sunday ... bit 6 = Saturday
  ScheduleAction action = ScheduleAction::LaunchColumn;
  int column = 1; // 1-based, for LaunchColumn
};

// Art-Net / sACN receiver: incoming channels become /artnet/<u>/<ch> or
// /sacn/<u>/<ch> inputs (value 0..255) for the mappings.
struct DmxInputConfig {
  bool enabled = false;
  DmxProtocol protocol = DmxProtocol::ArtNet;
  int universe = 0;  // first universe (Art-Net from 0, sACN from 1)
  int count = 1;     // number of universes
};

struct Project {
  std::string filePath; // not serialised
  int canvasW = 1920, canvasH = 1080; // main canvas (id 0)
  Color background{0, 0, 0, 1};
  std::string canvasName = "Canvas 1";
  std::vector<CanvasConfig> canvases; // extra canvases (ids > 0)
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
  float dmxMaster = 1; // intensity of every LED mapping (blackout of the lights)
  // Output layout of the Outputs window (projector grid). Applied with
  // applyBlendLayout; kept so the window shows the current layout.
  BlendLayoutParams outputLayout{1, 1, 1920, 1080, 0, 0, {}, {}};
  std::vector<ScheduleEntry> schedule;
  DmxInputConfig dmxInput;
  bool keyboardInput = true; // keys become /key/<name> inputs
  // Installation start-up: open the outputs and launch a column when the
  // project is opened (0 = none).
  bool startOutputs = false;
  int startColumn = 0;
  int nextId = 1;

  std::vector<MediaItem> library;
  std::vector<Layer> layers; // index 0 is the front-most layer
  std::vector<Column> columns;
  std::vector<OutputConfig> outputs;
  std::vector<InputMapping> mappings;
  std::vector<DmxDevice> dmxDevices;
  std::vector<LedMapping> ledMappings;
  std::vector<Surface> surfaces;

  int newId() { return nextId++; }
  DmxDevice *dmxDevice(int id);
  LedMapping *ledMapping(int id);
  const DmxDevice *dmxDevice(int id) const { return const_cast<Project *>(this)->dmxDevice(id); }
  const LedMapping *ledMapping(int id) const { return const_cast<Project *>(this)->ledMapping(id); }
  DmxDevice &addDmxDevice(DmxProtocol protocol);
  LedMapping &addLedMapping();
  MediaItem *media(int id);
  Layer *layer(int id);
  int layerIndex(int id) const;
  OutputConfig *output(int id);
  const OutputConfig *output(int id) const { return const_cast<Project *>(this)->output(id); }

  Layer &addLayer(const std::string &name = "", int canvasId = 0);
  Column &addColumn();
  OutputConfig &addOutput();
  MediaItem &addMedia(const std::string &path);
  MediaItem &addGenerator(GeneratorKind kind);
  MediaItem &addLive(const std::string &name, const std::string &url, const std::string &format,
                     const std::string &options);
  MediaItem &addSpout(const std::string &senderName);
  // Canvases: 0 = main. Unknown ids fall back to the main canvas.
  CanvasConfig *extraCanvas(int id);
  std::vector<int> canvasIds() const;
  int canvasWidth(int id) const;
  int canvasHeight(int id) const;
  Color canvasBackground(int id) const;
  std::string canvasLabel(int id) const;
  bool hasCanvas(int id) const { return id == 0 || const_cast<Project *>(this)->extraCanvas(id); }
  CanvasConfig &addCanvas(int w, int h);
  // Layers of the canvas are removed; outputs, LEDs and media move to canvas 0.
  void removeCanvas(int id);
  MediaItem &addCanvasMedia(int canvasId);
  MediaItem &addShader(const std::string &path);
  Surface *surface(int id);
  Surface &addSurface(const Warp &w);
  // Copies every surface's mapping into the slices bound to it (and unbinds
  // slices whose surface was removed).
  void resolveSurfaces();
  // A slice bound to a surface was edited (its warp differs from the
  // surface): the surface takes the new mapping, then every bound slice.
  void pushSurfaceEdits();
  void removeLayer(int id);
  void removeMedia(int id);

  static Project makeDefault();
};

// Blend assistant: makes exactly cols*rows outputs (reusing existing ones, so
// their warps and colour settings survive), sets canvas size, regions and soft
// edges. firstMonitor >= 0 assigns monitors in order; -1 keeps windows.
// scaleContent rescales slices, masks and LED areas to the new canvas size.
void applyBlendLayout(Project &p, const BlendLayoutParams &params, int firstMonitor, bool scaleContent);

// Free layouts (projectors placed anywhere on a canvas, not in a grid): sets
// every output's soft edges from how its canvas region overlaps the others.
// Returns the number of blended seams.
int autoBlendOverlaps(Project &p, int canvasId);

std::string serializeProject(const Project &p, const std::string &baseDir);
bool deserializeProject(const std::string &json, const std::string &baseDir, Project &out,
                        std::string *error = nullptr);
bool saveProject(Project &p, const std::string &path, std::string *error = nullptr);
bool loadProject(const std::string &path, Project &out, std::string *error = nullptr);

bool isImagePath(const std::string &path);
bool isMediaPath(const std::string &path);

} // namespace lumen
