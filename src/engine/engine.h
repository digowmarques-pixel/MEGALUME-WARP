// Show engine: owns the project, playback state of every layer, the
// compositor, audio and the control inputs (OSC, MIDI). Everything runs on the
// main thread except decoding, the audio callback and the input sockets.
#pragma once

#include "control/midi.h"
#include "control/osc.h"
#include "core/project.h"
#include "engine/compositor.h"
#include "engine/leds.h"
#include "io/spout.h"
#include "media/audio.h"
#include "media/source.h"

#include <deque>
#include <map>
#include <memory>

namespace lumen {

struct LayerState {
  std::unique_ptr<Playback> current, previous;
  float fade = 1;          // 0..1 progress of the current transition
  float fadeDuration = 0;  // seconds
  bool fadingOut = false;  // current is being stopped
  int columnId = -1;       // column the current content came from
  const Texture *currentTex = nullptr, *previousTex = nullptr;
};

struct MappingState {
  float target = 0, value = 0;
  bool hasValue = false, active = false, triggered = false;
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

  bool blackout = false;
  float masterOpacity = 1;

  // Per-frame.
  void update(double dt);
  void render();
  Framebuffer &canvas() { return compositor_.canvas(); }
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
  // Sends to every enabled OSC output target.
  void sendOsc(const OscMessage &m, bool feedbackOnly = false);
  int oscSent() const { return oscSent_; }

  // LED pixel mapping (Art-Net / sACN).
  LedEngine leds;

  // Spout output of the canvas (Windows).
  std::string spoutStatus() const { return spoutStatus_; }

private:
  void applyMappings(const OscMessage &m);
  void updateMappings(double dt);
  void startTransition(int layerId, const Cell *cell, float duration, int columnId);
  float columnTransition(int index) const;
  Layer *findLayer(const std::string &key);
  MediaContext context();
  void runColumnActions(const Column &c, int index);

  Compositor compositor_;
  std::map<int, LayerState> states_;
  std::vector<MappingState> mappingStates_;
  int activeColumn_ = -1;
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
