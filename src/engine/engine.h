// Show engine: owns the project, playback state of every layer, the
// compositor, audio and the control inputs (OSC, MIDI). Everything runs on the
// main thread except decoding, the audio callback and the input sockets.
#pragma once

#include "control/devices.h"
#include "control/dmxin.h"
#include "control/midi.h"
#include "control/osc.h"
#include "core/project.h"
#include "engine/compositor.h"
#include "engine/effects.h"
#include "engine/leds.h"
#include "io/ndi.h"
#include "io/serial.h"
#include "io/spout.h"
#include "media/audio.h"
#include "media/source.h"

#include <deque>
#include <map>
#include <memory>
#include <set>

namespace lumen {

struct TimelineRun {
  double time = 0;
  bool playing = true;
  bool fromColumn = false; // started by a column (stopped by the next one)
};

struct LayerState {
  std::unique_ptr<Playback> current, previous;
  int run = 0;             // changes at every launch / stop / restart (redundancy)
  float fade = 1;          // 0..1 progress of the current transition
  float fadeDuration = 0;  // seconds
  bool fadingOut = false;  // current is being stopped
  TransitionType type = TransitionType::Crossfade;
  int columnId = -1;       // column the current content came from
  EndMode endMode = EndMode::Loop;
  float cellSpeed = -1;    // the cell's speed last applied (a change in the cell applies at once)
  bool manual = false;     // transition progress follows the transition fader
  bool composed = false;   // shader transition or crossfade: currentTex holds both contents
  std::string transitionShader;
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

// Redundancy: a main computer streams the show state; a backup follows it and
// takes over when the main goes silent (engine/sync.cpp).
enum class SyncRole { Off, Main, Backup };
enum class SyncState { Off, Main, Waiting, Following, Active };

class Engine {
public:
  bool init();
  void shutdown();

  // Redundancy (role, peer "ip[:port]" and port are settings of this computer).
  void setSync(SyncRole role, const std::string &peer, int port);
  SyncRole syncRole() const { return syncRole_; }
  SyncState syncState() const { return syncState_; }
  const std::string &syncStatus() const { return syncStatus_; }
  const std::string &syncPeerStatus() const { return syncPeerStatus_; }
  bool syncMainBack() const { return syncMainBack_; } // backup took over and the main is alive again
  void syncTakeOver();  // backup: take over now
  void syncFollow();    // backup: go back to following the main
  int startColumnOnTakeover = -1; // --launch: a backup that never heard the main starts here
  // True while the backup mirrors the main: local triggers (inputs, auto-GO,
  // schedule, timecode cues, column commands) must not fire twice.
  bool following() const { return syncState_ == SyncState::Waiting || syncState_ == SyncState::Following; }
  // Show commands from this computer are ignored while following (only the
  // follower itself, applying the main's state, may launch).
  bool localBlocked() const { return following() && !syncApplying_; }

  // Replaces the project (open / new / undo). Playback is kept for layers that still exist.
  void setProject(Project p, bool keepPlayback);
  Project project;

  // Show control.
  void launchColumn(int index);
  void go();   // next column
  void back(); // previous column
  void launchCell(int columnIndex, int layerId);
  void stopLayer(int layerId, float fade = -1);
  // Pause: the media on air in the layer holds its picture and sound (not for
  // live inputs). Launching a cell plays again from the start.
  void pauseLayer(int layerId, bool paused);
  // Jumps the media on air in the layer to `seconds` (files and generated media).
  void seekLayer(int layerId, double seconds);
  bool layerPaused(int layerId) const;
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

  // Device commands (PJLink, MIDI out, DMX, TCP/UDP, serial).
  DeviceCommands devices;
  bool forceSyncUpload = false; // --sync-upload (comparisons, driver problems)

  // Timecode (MTC / LTC input): seconds, valid while frames keep arriving.
  bool timecodeValid() const { return timecodeValid_; }
  double timecodeSeconds() const { return timecodeSeconds_; }
  double timecodeFps() const { return timecodeFps_; }
  const LtcCapture &ltcInput() const { return ltcIn_; }
  const std::string &timecodeError() const { return tcError_; }
  // Timecode being generated (valid while the source timeline plays).
  bool generatingTimecode() const { return genActive_; }
  double generatedTimecode() const { return genTime_; }

  // Timelines.
  void playTimeline(int id, bool fromStart = true, bool fromColumn = false);
  void pauseTimeline(int id);
  void stopTimeline(int id);
  void seekTimeline(int id, double t);
  const TimelineRun *timelineRun(int id) const;
  // Applies every track at time t (scrubbing a stopped timeline).
  void applyTimeline(const Timeline &tl, double t);
  // Parameters by name ("opacity", "effect1/radius", "master"...).
  bool setNamedParam(int layerId, const std::string &param, float v);
  float getNamedParam(int layerId, const std::string &param);
  // OSC line ("/lumen/go", "/luz/cena 3": local or sent to the OSC outputs)
  // or a device command (see docs/DISPOSITIVOS.md).
  void runCommand(const std::string &line);

  // Manual transitions: the fader (0..1) drives the progress once it has
  // been brought back to 0 ("armed").
  float transitionFader = 0;
  bool manualTransitionActive() const { return manualActive_; }
  bool manualTransitionArmed() const { return manualArmed_; }
  float manualProgress() const { return manualProgress_; }

  // Effects (ISF) and the blob tracker.
  IsfCatalog isfCatalog;
  const std::vector<Blob> *blobs(int layerId) const;
  const BlobTracker *blobTracker(int layerId) const;
  void learnBlobBackground(int layerId) { learnBackground_.insert(layerId); }

  // Spout output of the canvas (Windows).
  std::string spoutStatus() const { return spoutStatus_; }
  std::string ndiStatus() const { return ndiStatus_; }
  // Serial input (Arduino...).
  const SerialReader &serialInput() const { return serialIn_; }
  const std::string &serialError() const { return serialError_; }

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
  int activeColumn_ = -1, activeColumnId_ = -1; // index (follows the id, see resolveActiveColumn)
  double columnLaunchedAt_ = 0;
  int columnRun_ = 0; // changes at every column launch / stop all (redundancy)
  int stingerRun_ = 0; // changes when a stinger starts (redundancy)
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
  void updateTimelines(double dt);
  void launchColumnNow(int index, bool cut);
  std::map<int, TimelineRun> timelineRuns_;
  bool timecodeValid_ = false;
  double timecodeSeconds_ = 0, timecodeFps_ = 25, tcFreshAt_ = -100, tcPrev_ = -1;
  LtcCapture ltcIn_;
  std::string ltcDeviceInUse_ = "\x01", tcError_;
  int ltcChannelInUse_ = -1;
  MidiOutPort mtcOut_;
  std::string mtcPortInUse_ = "\x01";
  double mtcRetryAt_ = 0;
  bool genActive_ = false;
  double genTime_ = 0, mtcClock_ = -1;
  long long ltcFrame_ = -1; // last LTC frame queued (frame number at the generator's fps)
  int mtcPiece_ = 0;
  LtcEncoder ltcEnc_{kAudioRate};
  std::shared_ptr<AudioVoice> ltcVoice_;
  void updateTimecode(double dt);
  bool manualActive_ = false, manualArmed_ = false;
  float manualProgress_ = 0;
  // Shader transitions (one per layer) and the stinger.
  std::map<int, std::unique_ptr<IsfRenderer>> transitionIsf_;
  std::map<int, std::unique_ptr<Framebuffer>> transitionIn_[2], transitionOut_;
  std::map<int, std::unique_ptr<Framebuffer>> crossfadeFbs_; // per layer, during a crossfade
  Texture clearTex_;
  std::unique_ptr<Playback> stinger_;
  const Texture *stingerTex_ = nullptr;
  Layer stingerLayer_;
  int pendingColumn_ = -1, pendingColumnId_ = -1;
  double pendingAt_ = 0;
  const Texture *composeTransition(int layerId, LayerState &st);
  double time_ = 0;
  uint64_t frame_ = 0;
  OscServer osc_;
  OscSender oscOut_;

  // Redundancy (engine/sync.cpp).
  void updateSync();
  void resolveActiveColumn();
  void syncSendState();
  void syncApply(const std::string &json);
  void syncResetFollow();
  std::string syncProjectSignature() const;
  SyncRole syncRole_ = SyncRole::Off;
  SyncState syncState_ = SyncState::Off;
  std::string syncPeer_, syncStatus_, syncPeerStatus_, syncMainHost_, syncSignature_;
  int syncPort_ = 7400;
  OscServer syncIn_;
  OscSender syncOut_;
  double syncLastRx_ = -1, syncLastTx_ = -10, syncLastPeerRx_ = -1, syncSignatureAt_ = -10;
  long long syncSeq_ = 0, syncRxSeq_ = -1, syncSession_ = 0, syncMainSession_ = -1;
  double syncWaitingSince_ = 0;
  std::string syncIgnoredHost_;
  int syncSentColumnRun_ = -1, syncSentColumn_ = -2, syncAppliedColumnRun_ = -1;
  bool syncSentBlackout_ = false, syncMainBack_ = false, syncApplying_ = false;
  std::map<int, int> syncAppliedRun_; // layer -> main's run already applied
  // Drift of a layer's media against the main, averaged over the messages:
  // a single late packet or frame must not trigger a jump.
  struct SyncDrift {
    double average = 0;
    int samples = 0;
    double seekedAt = -10;
  };
  std::map<int, SyncDrift> syncDrift_;
  int syncAppliedStingerRun_ = -1;
  bool syncStingerFollow_ = false; // our stinger mirrors the main's: its cut is ours too
  int oscSent_ = 0;
  std::string oscError_;
  MidiInputs midi_;
  double midiRescanAt_ = 0;
  std::vector<OscMessage> inbox_;
  std::string audioInUse_ = "\x01"; // device + options in use; forces the first start
  double audioRetryAt_ = 0;         // next reopen of a sound card that stopped
  uint64_t heldDmxVersion_ = 0;     // of the "dmx" command values copied to the LEDs
  std::string audioKey() const;
  SpoutOut spoutOut_;
  std::string spoutStatus_, ndiStatus_;
  NdiSender ndiOut_;
  AsyncReadback ndiReadback_;
  std::vector<unsigned char> ndiPixels_;
  bool ndiOk_ = true;
  SerialReader serialIn_;
  std::string serialPortInUse_, serialError_;
  int serialBaudInUse_ = 0;
};

} // namespace lumen
