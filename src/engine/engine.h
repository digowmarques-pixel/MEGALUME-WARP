// Show engine: owns the project, playback state of every layer, the
// compositor, audio and the control inputs (OSC, MIDI). Everything runs on the
// main thread except decoding, the audio callback and the input sockets.
#pragma once

#include "control/dmxin.h"
#include "control/midi.h"
#include "control/osc.h"
#include "core/project.h"
#include "engine/compositor.h"
#include "engine/effects.h"
#include "engine/leds.h"
#include "io/spout.h"
#include "media/audio.h"
#include "media/source.h"

#include <deque>
#include <map>
#include <memory>
#include <set>

namespace lumen {

struct LayerState {
  std::unique_ptr<Playback> current, previous;
  float fade = 1;          // 0..1 progress of the current transition
  float fadeDuration = 0;  // seconds
  bool fadingOut = false;  // current is being stopped
  TransitionType type = TransitionType::Crossfade;
  int columnId = -1;       // column the current content came from
  EndMode endMode = EndMode::Loop;
  bool endHandled = false; // the end of the current media was acted upon
  const Texture *currentTex = nullptr, *previousTex = nullptr;
};

struct MappingState {
  float target = 0, value = 0;
  bool hasValue = false, active = false, triggered = false;
  bool toggled = false; // Toggle transformer state
  float accum = 0;      // Accumulator transformer value
  bool accumInit = false;
};

// A layer parameter moving to the value stored in a column (column states).
struct ParamTween {
  int layerId = -1;
  MappingTarget param = MappingTarget::Opacity;
  float from = 0, to = 0, elapsed = 0, duration = 0;
};

class Engine {
public:
  bool init();
  void shutdown();

  // Replaces the project (open / new / undo). Playback is kept for layers that still exist.
  void setProject(Project p, bool keepPlayback);
  Project project;

  // Show control.
  void launchColumn(int index);
  void go();   // next column
  void back(); // previous column
  void launchCell(int columnIndex, int layerId);
  void stopLayer(int layerId, float fade = -1);
  void stopAll(float fade = -1);
  int activeColumn() const { return activeColumn_; }
  const LayerState *layerState(int layerId) const;
  // Opacity of the outgoing / incoming content for a transition progress.
  static float outgoingOpacity(const LayerState &st);
  static float incomingOpacity(const LayerState &st);

  bool blackout = false;
  float masterOpacity = 1;

  // Per-frame.
  void update(double dt);
  void render();
  Framebuffer &canvas() { return compositor_.canvas(); } // main canvas
  // Composed texture of any canvas (the main one for unknown ids).
  const Texture &canvasTexture(int canvasId);
  double time() const { return time_; }

  // Shared GPU helpers for the main context.
  MediaCache cache;
  Generators generators;
  DynamicMesh mesh;

  // Audio.
  AudioEngine audio;
  void restartAudio();

  // OSC / MIDI / interaction.
  void restartOsc();
  const OscServer &oscServer() const { return osc_; }
  const std::string &oscError() const { return oscError_; }
  MidiInputs &midi() { return midi_; }
  void restartMidi();
  std::deque<std::string> oscLog; // newest last (OSC and MIDI input)
  int learnMapping = -1;          // index of a mapping waiting for an incoming address
  bool handleOsc(const OscMessage &m);
  void setParam(Layer &l, MappingTarget t, float v);
  float getParam(const Layer &l, MappingTarget t) const;
  // Records the current value of `param` of a layer in column `index`.
  void storeState(int index, int layerId, MappingTarget param);
  // Messages from the keyboard and other local inputs, handled like OSC.
  void injectInput(OscMessage m) { injected_.push_back(std::move(m)); }
  int selectedLayerId = -1; // for /selectedLayer/... (set by the UI)
  // Requests for the application (outputs live in the App): -1 none.
  int requestOutputs = -1;  // 0 close, 1 open
  // DMX input (Art-Net / sACN receive).
  void restartDmxInput();
  const DmxReceiver &dmxInput() const { return dmxIn_; }
  const std::string &dmxInputError() const { return dmxInError_; }
  // Seconds until auto-go fires (<0: none pending).
  float autoGoRemaining() const;
  // Sends to every enabled OSC output target.
  void sendOsc(const OscMessage &m, bool feedbackOnly = false);
  int oscSent() const { return oscSent_; }
  // Every input message of the last update (OSC, MIDI, keys, DMX).
  const std::vector<OscMessage> &frameInputs() const { return inbox_; }

  // LED pixel mapping (Art-Net / sACN).
  LedEngine leds;

  // Effects (ISF) and the blob tracker.
  IsfCatalog isfCatalog;
  const std::vector<Blob> *blobs(int layerId) const;
  const BlobTracker *blobTracker(int layerId) const;
  void learnBlobBackground(int layerId) { learnBackground_.insert(layerId); }

  // Spout output of the canvas (Windows).
  std::string spoutStatus() const { return spoutStatus_; }

private:
  void applyMappings(const OscMessage &m);
  void updateMappings(double dt);
  void startTransition(int layerId, const Cell *cell, float duration, int columnId,
                       TransitionType type = TransitionType::Crossfade);
  float columnTransition(int index) const;
  Layer *findLayer(const std::string &key);
  MediaContext context();
  void runColumnActions(const Column &c, int index);
  void applyColumnStates(int index, float duration);
  void renderCanvas(int canvasId);
  void updateTweens(double dt);
  void updateAutoGo();
  void updateSchedule();
  void handleMediaEnd(int layerId, LayerState &st);
  bool handleMillumin(const std::vector<std::string> &path, const OscMessage &m);
  bool setLayerProperty(Layer &l, const std::vector<std::string> &prop, const OscMessage &m);

  Compositor compositor_;                                 // main canvas
  std::map<int, std::unique_ptr<Compositor>> extraCompositors_; // other canvases
  std::map<int, LayerState> states_;
  std::vector<MappingState> mappingStates_;
  int activeColumn_ = -1;
  double columnLaunchedAt_ = 0;
  bool autoGoFired_ = false;
  std::vector<ParamTween> tweens_;
  std::vector<OscMessage> injected_;
  DmxReceiver dmxIn_;
  DmxInputConfig dmxInConfig_;
  std::string dmxInError_;
  int lastScheduleMinute_ = -1;
  EffectTools effectTools_;
  std::map<int, EffectChain> chains_;      // 2 * layer id + (0 current, 1 previous)
  std::map<int, BlobReport> blobReports_;  // by layer
  std::map<int, std::vector<int>> blobSlots_; // by layer: blob id in each slot (0 = free)
  std::set<int> learnBackground_;
  void emitBlobs(const Layer &l, const BlobReport &r);
  double time_ = 0;
  uint64_t frame_ = 0;
  OscServer osc_;
  OscSender oscOut_;
  int oscSent_ = 0;
  std::string oscError_;
  MidiInputs midi_;
  double midiRescanAt_ = 0;
  std::vector<OscMessage> inbox_;
  std::string audioDeviceInUse_ = "\x01"; // forces the first start
  SpoutOut spoutOut_;
  std::string spoutStatus_;
};

} // namespace lumen
