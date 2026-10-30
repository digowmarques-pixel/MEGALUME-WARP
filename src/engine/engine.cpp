#include "engine/engine.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <set>
#include <sstream>

namespace lumen {

bool Engine::init() {
  bool ok = generators.init() && compositor_.init() && leds.init() && effectTools_.init();
  effectTools_.generators = &generators;
  if (!ok) std::fprintf(stderr, "[engine] shader initialisation failed\n");
  return ok;
}

void Engine::shutdown() {
  osc_.stop();
  midi_.stop();
  dmxIn_.stop();
  serialIn_.stop();
  ltcIn_.stop();
  if (ltcVoice_) audio.removeVoice(ltcVoice_);
  ltcVoice_.reset();
  states_.clear(); // playbacks remove their audio voices
  extraCompositors_.clear();
  chains_.clear();
  cache.clear();
  spoutOut_.release();
  audio.stop();
}

MediaContext Engine::context() {
  StreamUploader::setEnabled(project.asyncUpload && !forceSyncUpload);
  DecoderSettings s;
  s.hardware = project.hardwareDecode;
  s.nativeHap = FrameUploader::supportsS3tc();
  MediaContext ctx{cache, generators, mesh, &audio, s, frame_, {}};
  ctx.canvasTexture = [this](int id) -> const Texture * { return &canvasTexture(id); };
  return ctx;
}

const std::vector<Blob> *Engine::blobs(int layerId) const {
  auto it = blobReports_.find(layerId);
  return it == blobReports_.end() ? nullptr : &it->second.blobs;
}

const BlobTracker *Engine::blobTracker(int layerId) const {
  auto it = chains_.find(layerId * 2);
  return it == chains_.end() ? nullptr : it->second.tracker();
}

// Blobs become inputs for the mappings (next frame):
//   /blob/<layer>/count n
//   /blob/<layer>/<slot> x y w h        (slot 1.. keeps following the same blob)
//   /blob/<layer>/<slot>/x, /y, /size, /active
//   /blob/<layer>/centroid x y           (average of all blobs)
void Engine::emitBlobs(const Layer &l, const BlobReport &r) {
  std::string base = "/blob/" + l.name;
  auto &slots = blobSlots_[l.id];
  // Free the slots of lost blobs, then give new blobs the first free slot.
  for (size_t s = 0; s < slots.size(); ++s)
    if (slots[s] && std::none_of(r.blobs.begin(), r.blobs.end(), [&](const Blob &b) { return b.id == slots[s]; })) {
      injected_.push_back({base + "/" + std::to_string(s + 1) + "/active", {int32_t(0)}, "blob"});
      slots[s] = 0;
    }
  for (auto &b : r.blobs)
    if (std::find(slots.begin(), slots.end(), b.id) == slots.end()) {
      auto freeSlot = std::find(slots.begin(), slots.end(), 0);
      if (freeSlot != slots.end()) *freeSlot = b.id;
      else slots.push_back(b.id);
    }
  std::vector<OscMessage> out;
  out.push_back({base + "/count", {int32_t(r.blobs.size())}, "blob"});
  float cx = 0, cy = 0;
  for (size_t s = 0; s < slots.size(); ++s) {
    if (!slots[s]) continue;
    auto b = std::find_if(r.blobs.begin(), r.blobs.end(), [&](const Blob &x) { return x.id == slots[s]; });
    if (b == r.blobs.end()) continue;
    std::string a = base + "/" + std::to_string(s + 1);
    out.push_back({a, {b->x, b->y, b->w, b->h}, "blob"});
    out.push_back({a + "/x", {b->x}, "blob"});
    out.push_back({a + "/y", {b->y}, "blob"});
    out.push_back({a + "/size", {std::sqrt(b->area)}, "blob"});
    out.push_back({a + "/active", {int32_t(1)}, "blob"});
    cx += b->x;
    cy += b->y;
  }
  if (!r.blobs.empty())
    out.push_back({base + "/centroid", {cx / float(r.blobs.size()), cy / float(r.blobs.size())}, "blob"});
  for (auto &m : out) {
    if (r.sendOsc) sendOsc(m);
    injected_.push_back(std::move(m));
  }
}

const Texture &Engine::canvasTexture(int id) {
  if (id != 0) {
    auto it = extraCompositors_.find(id);
    if (it != extraCompositors_.end() && project.extraCanvas(id)) return it->second->canvas().texture();
  }
  return compositor_.canvas().texture();
}

void Engine::setProject(Project p, bool keepPlayback) {
  bool portChanged = p.oscPort != project.oscPort || p.oscEnabled != project.oscEnabled;
  bool midiChanged = p.midiEnabled != project.midiEnabled;
  project = std::move(p);
  if (!keepPlayback) {
    states_.clear();
    activeColumn_ = -1;
    activeColumnId_ = -1;
    cache.clear();
    // Nothing of the previous show keeps running: timelines (and their
    // flags), parameter animations, a pending stinger cut, a manual fade.
    timelineRuns_.clear();
    tweens_.clear();
    pendingColumn_ = pendingColumnId_ = -1;
    stinger_.reset();
    stingerTex_ = nullptr;
    manualActive_ = false;
  }
  // Drop playback of layers that no longer exist.
  for (auto it = states_.begin(); it != states_.end();) {
    if (!project.layer(it->first)) {
      compositor_.releaseLayer(it->first);
      for (auto &[id, c] : extraCompositors_) c->releaseLayer(it->first);
      it = states_.erase(it);
    } else {
      ++it;
    }
  }
  mappingStates_.assign(project.mappings.size(), {});
  resolveActiveColumn();
  if (portChanged || !keepPlayback) restartOsc();
  if (midiChanged || !keepPlayback) restartMidi();
  if (audioInUse_ != audioKey()) restartAudio();
  restartDmxInput();
  tweens_.erase(std::remove_if(tweens_.begin(), tweens_.end(),
                               [&](const ParamTween &t) { return !project.layer(t.layerId); }),
                tweens_.end());
  project.resolveSurfaces();
}

void Engine::resolveActiveColumn() {
  // The cue on air (and the one a stinger will cut to) are kept by id: when
  // columns are moved or removed during the show, the index follows them.
  auto indexOf = [&](int id) {
    for (size_t i = 0; i < project.columns.size(); ++i)
      if (project.columns[i].id == id) return int(i);
    return -1;
  };
  activeColumn_ = activeColumnId_ >= 0 ? indexOf(activeColumnId_) : -1;
  if (activeColumn_ < 0) activeColumnId_ = -1;
  pendingColumn_ = pendingColumnId_ >= 0 ? indexOf(pendingColumnId_) : -1;
  if (pendingColumn_ < 0) pendingColumnId_ = -1;
}

void Engine::restartDmxInput() {
  const DmxInputConfig &c = project.dmxInput;
  bool same = c.enabled == dmxInConfig_.enabled && c.protocol == dmxInConfig_.protocol &&
              c.universe == dmxInConfig_.universe && c.count == dmxInConfig_.count;
  if (same && (dmxIn_.running() || !c.enabled || !dmxInError_.empty())) return;
  dmxInConfig_ = c;
  dmxIn_.stop();
  dmxInError_.clear();
  if (c.enabled && !dmxIn_.start(c.protocol, c.universe, c.count, &dmxInError_))
    std::fprintf(stderr, "[dmx in] %s\n", dmxInError_.c_str());
}

const LayerState *Engine::layerState(int layerId) const {
  auto it = states_.find(layerId);
  return it == states_.end() ? nullptr : &it->second;
}

float Engine::columnTransition(int index) const {
  if (index < 0 || index >= int(project.columns.size())) return project.defaultTransition;
  float t = project.columns[size_t(index)].transition;
  return t < 0 ? project.defaultTransition : t;
}

float Engine::outgoingOpacity(const LayerState &st) {
  if (st.composed) return 0;
  // Through black: the old content is gone by the middle, the new one starts there.
  if (st.type == TransitionType::ThroughBlack) return std::max(0.0f, 1.0f - 2.0f * st.fade);
  return 1.0f - st.fade;
}

float Engine::incomingOpacity(const LayerState &st) {
  if (st.composed) return 1;
  if (st.type == TransitionType::ThroughBlack && st.previous) return std::max(0.0f, 2.0f * st.fade - 1.0f);
  return st.fade;
}

void Engine::startTransition(int layerId, const Cell *cell, float duration, int columnId,
                             TransitionType type) {
  if (type == TransitionType::Cut) duration = 0;
  LayerState &st = states_[layerId];
  const MediaItem *media = cell ? project.media(cell->mediaId) : nullptr;

  if (media && st.current && st.current->mediaId() == media->id && !st.fadingOut) {
    // Same media continues in the next cue: keep it running (no restart, no fade).
    st.current->loop = cell->loop;
    st.current->speed = cell->speed;
    st.cellSpeed = cell->speed;
    st.columnId = columnId;
    st.endMode = cell->endMode;
    st.endHandled = false; // the new cue's end mode applies (even if it already ended)
    return;
  }
  if (!media && !st.current) return;

  st.previous = std::move(st.current);
  st.previousTex = st.currentTex;
  st.currentTex = nullptr;
  if (media) {
    MediaContext ctx = context();
    st.current = std::make_unique<Playback>(*media, *cell, ctx);
  }
  st.fade = duration > 0 ? 0.0f : 1.0f;
  st.fadeDuration = duration;
  st.type = type;
  st.manual = false; // launchColumnNow sets it again for a manual column
  st.composed = false;
  ++st.run;
  st.fadingOut = !media;
  st.columnId = media ? columnId : -1;
  st.endMode = cell ? cell->endMode : EndMode::Loop;
  st.cellSpeed = cell ? cell->speed : -1;
  st.endHandled = false;
}

void Engine::sendOsc(const OscMessage &m, bool feedbackOnly) {
  if (following()) return;
  for (auto &t : project.oscTargets) {
    if (!t.enabled || (feedbackOnly && !t.feedback)) continue;
    if (oscOut_.send(t.host, t.port, m)) ++oscSent_;
  }
}

void Engine::runColumnActions(const Column &c, int index) {
  if (following()) return; // the main runs them; the backup must not repeat them
  if (!c.commands.empty()) devices.runBatch(c.commands, project);
  if (project.oscTargets.empty()) return;
  OscMessage status{"/lumen/status/column", {int32_t(index + 1), c.name}, ""};
  sendOsc(status, true);
  // Same feedback as Millumin, for Companion / TouchOSC / Chataigne templates.
  sendOsc(OscMessage{"/millumin/board/launchedColumn", {int32_t(index + 1), c.name}, ""}, true);
  for (auto &line : c.oscActions) {
    OscMessage m;
    if (parseOscText(line, m)) sendOsc(m);
  }
}

void Engine::launchColumn(int index) {
  if (index < 0 || index >= int(project.columns.size()) || localBlocked()) return;
  const Column &col = project.columns[size_t(index)];
  if (pendingColumn_ >= 0) {
    if (index == pendingColumn_) return; // already coming at the stinger's cut
    pendingColumn_ = pendingColumnId_ = -1; // another cue replaces the pending one
  }
  // Stinger: a video (with alpha) plays over everything and the cue changes
  // with a cut in the middle of it, hidden by the stinger.
  if (const MediaItem *sm = project.media(col.stingerMediaId); sm) {
    Cell c;
    c.mediaId = sm->id;
    c.setEndMode(EndMode::Stop);
    MediaContext ctx = context();
    stinger_ = std::make_unique<Playback>(*sm, c, ctx);
    double dur = stinger_->duration();
    if (dur <= 0) dur = 1.0;
    pendingColumn_ = index;
    pendingColumnId_ = col.id;
    pendingAt_ = time_ + dur * double(std::clamp(col.stingerCut, 0.0f, 1.0f));
    ++stingerRun_;
    return;
  }
  launchColumnNow(index, false);
}

void Engine::launchColumnNow(int index, bool cut) {
  const Column &col = project.columns[size_t(index)];
  float fade = cut ? 0.0f : columnTransition(index);
  TransitionType type = cut ? TransitionType::Cut : col.transitionType;
  bool manual = col.manualTransition && !cut && type != TransitionType::Cut;
  if (manual) {
    manualActive_ = true;
    manualArmed_ = transitionFader <= 0.02f;
    manualProgress_ = 0;
  }
  for (auto &l : project.layers) {
    auto it = col.cells.find(l.id);
    const Cell *cell = (it != col.cells.end() && it->second.mediaId >= 0) ? &it->second : nullptr;
    startTransition(l.id, cell, manual ? 1.0f : fade, col.id, type);
    auto st = states_.find(l.id);
    if (st != states_.end() && st->second.fade < 1) {
      st->second.manual = manual;
      st->second.transitionShader = type == TransitionType::Shader ? col.transitionShader : std::string();
    }
  }
  activeColumn_ = index;
  activeColumnId_ = col.id;
  columnLaunchedAt_ = time_;
  ++columnRun_;
  autoGoFired_ = false;
  applyColumnStates(index, type == TransitionType::Cut ? 0.0f : fade);
  // Timelines: the column's timeline starts (or keeps running if it was
  // already playing from the previous column); others started by columns stop.
  for (auto it = timelineRuns_.begin(); it != timelineRuns_.end();)
    it = it->second.fromColumn && it->first != col.timelineId ? timelineRuns_.erase(it) : std::next(it);
  if (col.timelineId >= 0 && project.timeline(col.timelineId)) {
    auto it = timelineRuns_.find(col.timelineId);
    if (it == timelineRuns_.end() || !it->second.playing) playTimeline(col.timelineId, true, true);
  }
  runColumnActions(col, index);
}

// ------------------------------------------------------------------ timelines

void Engine::playTimeline(int id, bool fromStart, bool fromColumn) {
  if (!project.timeline(id)) return;
  TimelineRun &r = timelineRuns_[id];
  if (fromStart) r.time = 0;
  r.playing = true;
  r.fromColumn = fromColumn;
}

void Engine::pauseTimeline(int id) {
  auto it = timelineRuns_.find(id);
  if (it != timelineRuns_.end()) it->second.playing = false;
}

void Engine::stopTimeline(int id) { timelineRuns_.erase(id); }

void Engine::seekTimeline(int id, double t) {
  Timeline *tl = project.timeline(id);
  if (!tl) return;
  auto it = timelineRuns_.find(id);
  if (it == timelineRuns_.end()) { // positioning a stopped timeline does not start it
    TimelineRun stopped;
    stopped.playing = false;
    it = timelineRuns_.emplace(id, stopped).first;
  }
  TimelineRun &r = it->second;
  r.time = std::clamp(t, 0.0, double(tl->duration));
  applyTimeline(*tl, float(r.time));
}

const TimelineRun *Engine::timelineRun(int id) const {
  auto it = timelineRuns_.find(id);
  return it == timelineRuns_.end() ? nullptr : &it->second;
}

static MappingTarget namedTarget(const std::string &p, bool &ok) {
  static const std::pair<const char *, MappingTarget> names[] = {
      {"opacity", MappingTarget::Opacity},       {"x", MappingTarget::PositionX},
      {"y", MappingTarget::PositionY},           {"scale", MappingTarget::Scale},
      {"rotation", MappingTarget::Rotation},     {"brightness", MappingTarget::Brightness},
      {"contrast", MappingTarget::Contrast},     {"saturation", MappingTarget::Saturation},
      {"hue", MappingTarget::Hue},               {"speed", MappingTarget::Speed},
      {"volume", MappingTarget::Volume}};
  for (auto &[n, t] : names)
    if (p == n) { ok = true; return t; }
  ok = false;
  return MappingTarget::Opacity;
}

// "effect2/color.1" -> effect index 1, input "color", component 1.
static bool parseEffectParam(const std::string &p, int &index, std::string &name, int &comp) {
  if (p.rfind("effect", 0) != 0) return false;
  size_t slash = p.find('/');
  if (slash == std::string::npos) return false;
  index = std::atoi(p.substr(6, slash - 6).c_str()) - 1;
  name = p.substr(slash + 1);
  comp = 0;
  size_t dot = name.find('.');
  if (dot != std::string::npos) {
    comp = std::atoi(name.substr(dot + 1).c_str());
    name = name.substr(0, dot);
  }
  return index >= 0;
}

bool Engine::setNamedParam(int layerId, const std::string &param, float v) {
  if (!std::isfinite(v)) return false;
  if (layerId < 0) {
    if (param == "master") masterOpacity = std::clamp(v, 0.0f, 1.0f);
    else if (param == "masterVolume") project.masterVolume = std::clamp(v, 0.0f, 2.0f);
    else if (param == "dmxMaster") project.dmxMaster = std::clamp(v, 0.0f, 1.0f);
    else if (param == "transition") transitionFader = std::clamp(v, 0.0f, 1.0f);
    else return false;
    return true;
  }
  Layer *l = project.layer(layerId);
  if (!l) return false;
  bool ok;
  MappingTarget t = namedTarget(param, ok);
  if (ok) { setParam(*l, t, v); return true; }
  int index, comp;
  std::string name;
  if (!parseEffectParam(param, index, name, comp) || index >= int(l->effects.size())) return false;
  Effect &e = l->effects[size_t(index)];
  if (name == "enabled") { e.enabled = v > 0.5f; return true; }
  auto &vals = e.params[name];
  if (const IsfInfo *info = isfCatalog.info(e.path); info && vals.empty()) {
    IsfValues d;
    isfDefaults(*info, d);
    vals = d[name];
  }
  if (comp >= int(vals.size())) vals.resize(size_t(comp) + 1, 0.0f);
  vals[size_t(comp)] = v;
  return true;
}

float Engine::getNamedParam(int layerId, const std::string &param) {
  if (layerId < 0) {
    if (param == "master") return masterOpacity;
    if (param == "masterVolume") return project.masterVolume;
    if (param == "dmxMaster") return project.dmxMaster;
    if (param == "transition") return transitionFader;
    return 0;
  }
  Layer *l = project.layer(layerId);
  if (!l) return 0;
  bool ok;
  MappingTarget t = namedTarget(param, ok);
  if (ok) return getParam(*l, t);
  int index, comp;
  std::string name;
  if (!parseEffectParam(param, index, name, comp) || index >= int(l->effects.size())) return 0;
  Effect &e = l->effects[size_t(index)];
  if (name == "enabled") return e.enabled ? 1.0f : 0.0f;
  auto it = e.params.find(name);
  if (it == e.params.end() || comp >= int(it->second.size())) {
    if (const IsfInfo *info = isfCatalog.info(e.path)) {
      IsfValues d;
      isfDefaults(*info, d);
      auto &v = d[name];
      return comp < int(v.size()) ? v[size_t(comp)] : 0.0f;
    }
    return 0;
  }
  return it->second[size_t(comp)];
}

void Engine::applyTimeline(const Timeline &tl, double t) {
  for (auto &tr : tl.tracks)
    if (tr.enabled && !tr.keys.empty()) setNamedParam(tr.layerId, tr.param, tr.valueAt(float(t)));
}

void Engine::updateTimelines(double dt) {
  bool next = false;
  // Flag commands run after the loop: a command can launch a column, which
  // starts and stops timelines (and erases the entries being iterated).
  std::vector<std::string> commands;
  for (auto it = timelineRuns_.begin(); it != timelineRuns_.end();) {
    Timeline *tl = project.timeline(it->first);
    if (!tl) { it = timelineRuns_.erase(it); continue; }
    TimelineRun &r = it->second;
    double before = r.time, ls = 0;
    bool wrapped = false;
    if (tl->followTimecode && timecodeValid_) {
      r.time = std::clamp(timecodeSeconds_ - double(tl->timecodeOffset), 0.0, double(tl->duration));
    } else if (r.playing) {
      r.time += dt;
      double dur = std::max(0.01f, tl->duration);
      if (r.time >= dur) {
        if (tl->loop) {
          ls = std::clamp(double(tl->loopStart), 0.0, dur - 0.01);
          r.time = ls + std::fmod(r.time - ls, dur - ls);
          wrapped = true;
        } else {
          r.time = dur;
          r.playing = false;
          if (tl->goAtEnd) next = true;
        }
      }
    }
    // Flags crossed this frame (both parts when the loop wrapped; the intro
    // before the loop start plays only once).
    for (auto &f : tl->flags) {
      double ft = f.time;
      bool hit = wrapped ? (ft > before || (ft >= ls && ft <= r.time))
                         : (ft > before && ft <= r.time) || (before == 0 && ft == 0 && r.time > 0);
      if (hit && !f.command.empty() && !following()) commands.push_back(f.command);
    }
    if (r.playing || tl->followTimecode || before != r.time) applyTimeline(*tl, r.time);
    ++it;
  }
  for (auto &c : commands) runCommand(c);
  if (next && !following()) go();
}

// Chase: the media of the column on air (cued on the timecode) plays at the
// timecode's position: paused while it is stopped (on the last frame it sent)
// or before the cue, and jumps when they drift apart.
void Engine::chaseTimecode() {
  if (following() || activeColumn_ < 0 || activeColumn_ >= int(project.columns.size())) return;
  const Column &c = project.columns[size_t(activeColumn_)];
  if (c.timecodeCue < 0 || !c.timecodeChase || project.timecode.source == TimecodeSource::Off) return;
  if (tcLastReal_ < 0) return; // no timecode yet: leave the column as launched
  // Pause at once when the frames stop (Reaper's pause), not after the freewheel:
  // the picture then stays on the last frame received.
  const bool moving = timecodeValid_ && time_ - tcFreshAt_ < 0.2;
  const double at = (moving ? timecodeSeconds_ : tcLastReal_) - c.timecodeCue;
  const bool run = moving && at >= 0;
  for (auto &[id, st] : states_) {
    Playback *p = st.current.get();
    if (!p || st.columnId != c.id || st.fadingOut || !p->seekable()) continue;
    double target = std::max(0.0, at) * double(std::max(0.0f, p->speed));
    if (!p->loop && p->duration() > 0) target = std::min(target, p->duration());
    p->paused = !run;
    // Playing: small drift is left alone (a seek costs a moment of decoding).
    if (std::fabs(p->time() - target) > (run ? 0.25 : 0.02)) {
      p->seek(target);
      ++st.run; // a backup machine follows the jump
    }
  }
}

void Engine::updateTimecode(double dt) {
  const TimecodeConfig &cfg = project.timecode;
  // LTC input device follows the settings.
  bool wantLtc = cfg.source == TimecodeSource::LTC;
  if (wantLtc && (ltcDeviceInUse_ != cfg.ltcDevice || ltcChannelInUse_ != cfg.ltcChannel)) {
    ltcDeviceInUse_ = cfg.ltcDevice;
    ltcChannelInUse_ = cfg.ltcChannel;
    tcError_.clear();
    if (!ltcIn_.start(cfg.ltcDevice, cfg.ltcChannel, &tcError_)) std::fprintf(stderr, "[ltc] %s\n", tcError_.c_str());
  } else if (!wantLtc && ltcIn_.running()) {
    ltcIn_.stop();
    ltcDeviceInUse_ = "\x01";
  }
  // Input: fresh frames set the clock; between frames (and during short
  // dropouts, "freewheel") it runs on its own.
  Timecode tc;
  double age = 1e9;
  bool got = cfg.source == TimecodeSource::MTC ? midi_.timecode(tc, age)
           : cfg.source == TimecodeSource::LTC ? ltcIn_.latest(tc, age) : false;
  if (!testTimecode.empty()) {
    got = false;
    for (auto &s : testTimecode)
      if (time_ >= s[0] && time_ < s[1]) {
        tc = Timecode::fromSeconds(s[2] + time_ - s[0], 30);
        age = 0;
        got = true;
      }
  }
  if (got && age < 0.25) {
    // An LTC frame is read when its last bit arrives: one frame after its label.
    double t = tc.seconds() + age + (cfg.source == TimecodeSource::LTC && tc.fps > 0 ? 1.0 / tc.fps : 0.0);
    // Small jitter: keep the running clock smooth; big differences jump.
    if (!timecodeValid_ || std::fabs(t - timecodeSeconds_) > 0.12) timecodeSeconds_ = t;
    else timecodeSeconds_ += dt + (t - timecodeSeconds_) * 0.1;
    timecodeFps_ = tc.fps;
    timecodeValid_ = true;
    tcFreshAt_ = time_;
    tcLastReal_ = t;
  } else if (timecodeValid_ && time_ - tcFreshAt_ < std::max(0.0f, cfg.freewheel)) {
    timecodeSeconds_ += dt;
  } else {
    timecodeValid_ = false;
  }
  // Columns cued on a timecode.
  if (timecodeValid_) {
    // Going back or jumping ahead (play from another point) is a new start.
    const double now = timecodeSeconds_;
    const bool fresh = tcPrev_ < 0 || now < tcPrev_ - 0.12 || now - tcPrev_ >= 1.0;
    auto chasing = [&](int i) {
      return i >= 0 && i < int(project.columns.size()) && project.columns[size_t(i)].timecodeCue >= 0 &&
             project.columns[size_t(i)].timecodeChase;
    };
    if (!following()) {
      int on = -1; // a start inside a chasing cue's span brings that cue up
      if (fresh) {
        std::vector<float> cues;
        for (auto &c : project.columns) cues.push_back(c.timecodeChase ? c.timecodeCue : -1.0f);
        on = timecodeCueAt(cues, now);
        if (on >= 0 && on != activeColumn_) launchColumn(on);
      }
      for (size_t i = 0; i < project.columns.size(); ++i)
        if (int(i) != on && !(chasing(int(i)) && int(i) == activeColumn_) &&
            timecodeCueHit(project.columns[i].timecodeCue, fresh ? -1.0 : tcPrev_, now))
          launchColumn(int(i));
    }
    tcPrev_ = fresh ? now : std::max(now, tcPrev_); // jitter back: no cue twice
  } else {
    tcPrev_ = -1;
  }
  chaseTimecode();

  // Generation from a timeline.
  const TimelineRun *run = timelineRun(cfg.genTimelineId);
  genActive_ = cfg.generate && run && run->playing;
  if (!genActive_) {
    mtcClock_ = -1;
    ltcFrame_ = -1;
    if (ltcVoice_) ltcVoice_->active = false;
    return;
  }
  genTime_ = cfg.genStart + run->time;
  double fps = cfg.genFps > 0 ? cfg.genFps : 25;
  if (cfg.genMtc) {
    if (mtcPortInUse_ != cfg.genMtcPort || (!mtcOut_.isOpen() && time_ >= mtcRetryAt_)) {
      mtcPortInUse_ = cfg.genMtcPort;
      mtcOut_.open(cfg.genMtcPort);
      mtcRetryAt_ = time_ + 2; // a missing port is looked for every 2 s, not at every frame
    }
    double qf = 1.0 / (fps * 4.0);
    if (mtcClock_ < 0 || std::fabs(genTime_ - mtcClock_) > 0.5) { // start or jump: full frame
      mtcOut_.send(mtcFullFrame(Timecode::fromSeconds(genTime_, fps)));
      mtcClock_ = genTime_;
      mtcPiece_ = 0;
    }
    while (mtcClock_ <= genTime_) {
      // Pieces 0..7 describe the frame at which piece 0 was sent.
      Timecode frame = Timecode::fromSeconds(mtcClock_ - mtcPiece_ * qf, fps);
      mtcOut_.send(mtcQuarterFrame(frame, mtcPiece_));
      mtcPiece_ = (mtcPiece_ + 1) % 8;
      mtcClock_ += qf;
    }
  }
  if (cfg.genLtc && audio.running()) {
    if (!ltcVoice_) {
      ltcVoice_ = audio.createVoice();
      ltcVoice_->bypassMaster = true; // the master volume must not cut the timecode
    }
    ltcVoice_->active = true;
    ltcVoice_->gain = 1.0f;
    // A mono voice routed to the chosen output: left, right, both or any
    // channel of a multichannel interface (a dedicated LTC output).
    ltcVoice_->setChannels(1);
    std::vector<AudioPatch> route;
    if (cfg.genLtcChannel == 0 || cfg.genLtcChannel == 2) route.push_back({0, 0, 1});
    if (cfg.genLtcChannel == 1 || cfg.genLtcChannel == 2) route.push_back({0, 1, 1});
    if (cfg.genLtcChannel >= 3) route.push_back({0, cfg.genLtcChannel - 1, 1});
    ltcVoice_->setRoute(0, route);
    // Paced by the sound card: frames are added as it consumes them (~100 ms
    // queued), each labelled with the time at which it will actually be
    // heard. Consecutive labels normally; a jump, a stall of the program or
    // the card's clock drifting from the computer's moves the label to the
    // right time (a frame repeated or skipped) instead of a lasting delay.
    double queued = double(ltcVoice_->buffered()) / kAudioRate;
    if (ltcFrame_ < 0 || queued > 0.5) { // start, or the card stopped draining
      ltcVoice_->reset();
      queued = 0;
      ltcFrame_ = -1;
    }
    std::vector<float> mono;
    while (queued < 0.1) {
      double ideal = (genTime_ + queued) * fps; // in frames, when this one starts playing
      long long next = ltcFrame_ + 1;
      if (ltcFrame_ < 0 || std::fabs(double(next) - ideal) > 1.5) next = std::llround(ideal);
      mono.clear();
      ltcEnc_.encode(Timecode::fromSeconds((double(next) + 0.5) / fps, fps), mono, 0.5f);
      if (mono.empty() || ltcVoice_->write(mono.data(), mono.size()) == 0) break;
      queued += double(mono.size()) / kAudioRate;
      ltcFrame_ = next;
    }
  }
}

void Engine::runCommand(const std::string &line) {
  std::string l = line;
  l.erase(0, l.find_first_not_of(" \t"));
  if (l.empty() || l[0] == '#') return;
  if (l[0] == '/') {
    OscMessage m;
    if (!parseOscText(l, m)) return;
    // Our own commands run here; anything else goes to the OSC outputs.
    if (!handleOsc(m)) sendOsc(m);
    return;
  }
  devices.run(l, project);
}

void Engine::applyColumnStates(int index, float duration) {
  // Every parameter tracked by any column; a column without a value for it
  // inherits the closest previous column that has one.
  std::set<std::pair<int, int>> tracked;
  for (auto &c : project.columns)
    for (auto &st : c.states) tracked.insert({st.layerId, st.param});
  for (auto &[layerId, param] : tracked) {
    const Layer *l = project.layer(layerId);
    if (!l) continue;
    const ColumnState *found = nullptr;
    for (int i = index; i >= 0 && !found; --i)
      for (auto &st : project.columns[size_t(i)].states)
        if (st.layerId == layerId && st.param == param) { found = &st; break; }
    if (!found) continue;
    auto t = MappingTarget(param);
    tweens_.erase(std::remove_if(tweens_.begin(), tweens_.end(),
                                 [&](const ParamTween &x) { return x.layerId == layerId && x.param == t; }),
                  tweens_.end());
    tweens_.push_back({layerId, t, getParam(*l, t), found->value, 0, duration});
  }
}

void Engine::updateTweens(double dt) {
  for (auto it = tweens_.begin(); it != tweens_.end();) {
    Layer *l = project.layer(it->layerId);
    if (!l) { it = tweens_.erase(it); continue; }
    it->elapsed += float(dt);
    float k = it->duration > 0 ? std::min(1.0f, it->elapsed / it->duration) : 1.0f;
    k = k * k * (3 - 2 * k); // ease in-out, like the opacity crossfade feels
    setParam(*l, it->param, it->from + (it->to - it->from) * k);
    if (it->elapsed >= it->duration) it = tweens_.erase(it);
    else ++it;
  }
}

void Engine::storeState(int index, int layerId, MappingTarget param) {
  if (index < 0 || index >= int(project.columns.size())) return;
  const Layer *l = project.layer(layerId);
  if (!l) return;
  auto &states = project.columns[size_t(index)].states;
  for (auto &st : states)
    if (st.layerId == layerId && st.param == int(param)) { st.value = getParam(*l, param); return; }
  states.push_back({layerId, int(param), getParam(*l, param)});
}

float Engine::autoGoRemaining() const {
  if (activeColumn_ < 0 || activeColumn_ >= int(project.columns.size()) || autoGoFired_) return -1;
  const Column &c = project.columns[size_t(activeColumn_)];
  if (c.autoGo < 0) return -1;
  float wait = c.autoGo + (c.autoGoAfterTransition && c.transitionType != TransitionType::Cut
                               ? columnTransition(activeColumn_) : 0.0f);
  return std::max(0.0f, wait - float(time_ - columnLaunchedAt_));
}

void Engine::updateAutoGo() {
  if (following()) return;
  float left = autoGoRemaining();
  if (left < 0 || left > 0) return;
  autoGoFired_ = true;
  const Column &c = project.columns[size_t(activeColumn_)];
  if (c.autoGoTarget > 0) launchColumn(c.autoGoTarget - 1);
  else if (int next = columnInBoard(activeColumn_, 1); next >= 0) launchColumn(next);
  else { // end of the show: an installation loops back to the start (of its board)
    int first = 0;
    for (int j = 0; j < int(project.columns.size()); ++j)
      if (project.columns[size_t(j)].board == c.board) { first = j; break; }
    launchColumn(first);
  }
}

void Engine::updateSchedule() {
  if (following()) return;
  if (project.schedule.empty()) return;
  std::time_t now = std::time(nullptr);
  std::tm lt{};
#ifdef _WIN32
  localtime_s(&lt, &now);
#else
  localtime_r(&now, &lt);
#endif
  int minuteOfWeek = (lt.tm_wday * 24 + lt.tm_hour) * 60 + lt.tm_min;
  if (minuteOfWeek == lastScheduleMinute_) return;
  bool first = lastScheduleMinute_ < 0;
  lastScheduleMinute_ = minuteOfWeek;
  if (first) return; // do not fire an entry of the minute the program started in
  for (auto &e : project.schedule) {
    if (!e.enabled || !(e.days & (1 << lt.tm_wday)) || e.hour != lt.tm_hour || e.minute != lt.tm_min) continue;
    switch (e.action) {
    case ScheduleAction::LaunchColumn: launchColumn(e.column - 1); break;
    case ScheduleAction::Go: go(); break;
    case ScheduleAction::Stop: stopAll(); break;
    case ScheduleAction::BlackoutOn: blackout = true; break;
    case ScheduleAction::BlackoutOff: blackout = false; break;
    case ScheduleAction::Command: runCommand(e.command); break;
    default: break;
    }
    oscLog.push_back(std::string("[agenda] ") + scheduleActionName(e.action));
  }
}

int Engine::columnInBoard(int from, int dir) const {
  int n = int(project.columns.size());
  if (from >= n) return -1;
  int board = from >= 0 ? project.columns[size_t(from)].board : project.currentBoard;
  if (from < 0 && dir < 0) return -1;
  for (int j = from < 0 ? 0 : from + dir; j >= 0 && j < n; j += dir)
    if (project.columns[size_t(j)].board == board) return j;
  return -1;
}

void Engine::go() {
  // During a stinger the cue is already the pending one: GO goes past it.
  int from = pendingColumn_ >= 0 ? pendingColumn_ : activeColumn_;
  // On the last cue of the board GO does nothing (relaunching it would run its commands again).
  int next = columnInBoard(from, 1);
  if (next >= 0) launchColumn(next);
}

void Engine::back() {
  int from = pendingColumn_ >= 0 ? pendingColumn_ : activeColumn_;
  int prev = from >= 0 ? columnInBoard(from, -1) : -1;
  if (prev >= 0) launchColumn(prev);
}

void Engine::launchCell(int columnIndex, int layerId) {
  if (columnIndex < 0 || columnIndex >= int(project.columns.size()) || localBlocked()) return;
  const Column &col = project.columns[size_t(columnIndex)];
  auto it = col.cells.find(layerId);
  if (it == col.cells.end() || it->second.mediaId < 0) {
    stopLayer(layerId);
    return;
  }
  LayerState &st = states_[layerId];
  if (st.current && st.current->mediaId() == it->second.mediaId && st.columnId == col.id) {
    st.current->restart(); // clicking a playing cell restarts it
    ++st.run;
    return;
  }
  startTransition(layerId, &it->second, columnTransition(columnIndex), col.id, col.transitionType);
}

void Engine::seekLayer(int layerId, double seconds) {
  auto it = states_.find(layerId);
  if (it == states_.end() || !it->second.current || !it->second.current->seekable()) return;
  it->second.current->seek(std::max(0.0, seconds));
  ++it->second.run; // a backup machine follows the jump
}

void Engine::pauseLayer(int layerId, bool paused) {
  auto it = states_.find(layerId);
  if (it == states_.end() || !it->second.current || !it->second.current->seekable()) return;
  it->second.current->paused = paused;
}

bool Engine::layerPaused(int layerId) const {
  auto it = states_.find(layerId);
  return it != states_.end() && it->second.current && it->second.current->paused;
}

void Engine::stopLayer(int layerId, float fade) {
  if (localBlocked()) return; // on a backup only the main stops layers
  startTransition(layerId, nullptr, fade < 0 ? project.defaultTransition : fade, -1);
}

void Engine::stopAll(float fade) {
  if (localBlocked()) return;
  // Timelines started by columns stop with them (their flags would keep
  // firing commands, and "GO at the end" would launch a cue again).
  for (auto it = timelineRuns_.begin(); it != timelineRuns_.end();)
    it = it->second.fromColumn ? timelineRuns_.erase(it) : std::next(it);
  pendingColumn_ = pendingColumnId_ = -1;
  stinger_.reset();
  stingerTex_ = nullptr;
  manualActive_ = false;
  for (auto &l : project.layers) stopLayer(l.id, fade);
  if (activeColumn_ >= 0 && activeColumn_ < int(project.columns.size()) && !project.oscTargets.empty())
    sendOsc(OscMessage{"/millumin/board/stoppedColumn",
                       {int32_t(activeColumn_ + 1), project.columns[size_t(activeColumn_)].name}, ""}, true);
  activeColumn_ = activeColumnId_ = -1;
  ++columnRun_;
  if (!project.oscTargets.empty()) sendOsc(OscMessage{"/lumen/status/column", {int32_t(0), std::string()}, ""}, true);
}

std::string Engine::audioKey() const {
  return project.audioDevice + "|" + std::to_string(project.audioChannels) + "|" +
         std::to_string(project.audioExclusive) + "|" + std::to_string(project.audioBufferMs);
}

void Engine::restartAudio() {
  audioInUse_ = audioKey();
  // Voices hold no reference to the device, so playing media keep their
  // voices; they are simply mixed by the new device.
  AudioDeviceOptions o;
  o.channels = project.audioChannels;
  o.exclusive = project.audioExclusive;
  o.bufferMs = project.audioBufferMs;
  if (!audio.start(project.audioDevice, o))
    std::fprintf(stderr, "[audio] %s\n", audio.error().c_str());
}

void Engine::restartMidi() {
  midi_.stop();
  if (project.midiEnabled) midi_.start();
}

void Engine::update(double dt) {
  time_ += dt;
  ++frame_;
  resolveActiveColumn();
  if (audioInUse_ != audioKey()) {
    restartAudio();
  } else if (audio.stalled() && time_ >= audioRetryAt_) {
    // The sound card stopped asking for sound (unplugged, driver): reopen it,
    // or the default device, or the silent clock. Meanwhile the videos with
    // sound run on their frame clock instead of freezing.
    oscLog.push_back("[audio] a placa de som parou; reabrindo");
    std::fprintf(stderr, "[audio] a placa de som parou; reabrindo\n");
    audioRetryAt_ = time_ + 3;
    restartAudio();
  }
  audio.purge();
  audio.master = project.masterVolume;

  // Control input first so parameter changes apply to this frame.
  inbox_.clear();
  osc_.drain(inbox_);
  size_t oscCount = inbox_.size();
  midi_.drain(inbox_);
  dmxIn_.drain(inbox_);
  // Serial: "A0 512" -> /serial/A0 512; "1 2 3" -> /serial 1 2 3.
  {
    const auto &cfg = project.serialIn;
    std::string want = cfg.enabled ? cfg.port : std::string();
    if (want != serialPortInUse_ || (cfg.enabled && cfg.baud != serialBaudInUse_)) {
      serialPortInUse_ = want;
      serialBaudInUse_ = cfg.baud;
      serialIn_.stop();
      serialError_.clear();
      if (!want.empty()) devices.releaseSerial(want);
      if (!want.empty() && !serialIn_.start(want, cfg.baud, &serialError_))
        std::fprintf(stderr, "[serial] %s\n", serialError_.c_str());
    }
    std::vector<std::string> lines;
    serialIn_.drain(lines);
    for (auto &line : lines) {
      std::vector<std::string> tok;
      std::string cur;
      for (char c : line + " ") {
        if (c == ' ' || c == ',' || c == ':' || c == '\t' || c == '=') {
          if (!cur.empty()) tok.push_back(cur);
          cur.clear();
        } else {
          cur += c;
        }
      }
      if (tok.empty()) continue;
      OscMessage m;
      m.sender = "serial";
      size_t first = 0;
      char *end = nullptr;
      std::strtod(tok[0].c_str(), &end);
      if (*end) { m.address = "/serial/" + tok[0]; first = 1; }
      else m.address = "/serial";
      for (size_t i = first; i < tok.size(); ++i) {
        float v = std::strtof(tok[i].c_str(), &end);
        if (*end) m.args.emplace_back(tok[i]);
        else m.args.emplace_back(v);
      }
      inbox_.push_back(std::move(m));
    }
  }
  for (auto &m : injected_) inbox_.push_back(std::move(m));
  injected_.clear();
  for (size_t i = 0; i < inbox_.size(); ++i) {
    const OscMessage &m = inbox_[i];
    if (following()) { // the main reacts to its inputs; the backup only watches
      oscLog.push_back("(reserva) " + m.toString() + "   [" + m.sender + "]");
      continue;
    }
    bool known = i < oscCount ? handleOsc(m) : true;
    applyMappings(m);
    if (m.sender == "blob") continue; // every frame: see the Interaction panel instead
    oscLog.push_back((known ? "" : "? ") + m.toString() + "   [" + m.sender + "]");
  }
  while (oscLog.size() > 200) oscLog.pop_front();
  updateMappings(dt);
  if (project.midiEnabled && time_ >= midiRescanAt_) { // hot-plugged controllers
    midi_.rescan();
    midiRescanAt_ = time_ + 3.0;
  }

  updateSync();
  updateTweens(dt);
  updateSchedule();
  updateTimecode(dt);
  updateTimelines(dt);
  project.resolveSurfaces();
  {
    std::vector<std::string> dlog;
    devices.drainLog(dlog);
    for (auto &l : dlog) oscLog.push_back(l);
  }
  // Manual transition: the fader drives the progress once it went back to 0.
  if (manualActive_) {
    if (!manualArmed_ && transitionFader <= 0.02f) manualArmed_ = true;
    if (manualArmed_) manualProgress_ = std::clamp(transitionFader, 0.0f, 1.0f);
    if (manualProgress_ >= 0.999f) manualActive_ = false;
  }
  // Stinger and the cue change it hides.
  if (pendingColumn_ >= 0 && time_ >= pendingAt_) {
    int c = pendingColumn_;
    pendingColumn_ = pendingColumnId_ = -1;
    if (c < int(project.columns.size())) launchColumnNow(c, true);
  }

  MediaContext ctx = context();
  std::vector<int> ended;
  for (auto it = states_.begin(); it != states_.end();) {
    if (!project.layer(it->first)) { // layer removed during the show: stop its media
      compositor_.releaseLayer(it->first);
      for (auto &[id, c] : extraCompositors_) c->releaseLayer(it->first);
      it = states_.erase(it);
      continue;
    }
    LayerState &st = it->second;
    if (st.manual) {
      st.fade = manualActive_ ? manualProgress_ : 1.0f;
      if (!manualActive_) st.manual = false;
    } else if (st.fade < 1) {
      st.fade = st.fadeDuration > 0 ? std::min(1.0f, st.fade + float(dt) / st.fadeDuration) : 1.0f;
    }
    if (st.fade >= 1 && st.previous) {
      st.previous.reset();
      st.previousTex = nullptr;
    }
    const Layer *layer = project.layer(it->first);
    float vol = layer && !layer->mute ? layer->volume : 0.0f;
    auto tick = [&](std::unique_ptr<Playback> &pb, const Texture *&tex, float fade, int slot) {
      tex = nullptr;
      if (!pb) { fitFbs_.erase(slot); return; }
      const MediaItem *m = project.media(pb->mediaId());
      if (!m) { pb.reset(); return; } // media removed from the library
      // Sound follows the visual crossfade of the layer.
      pb->setGain(vol * fade);
      if (layer) pb->setRoute(layer->audioOutput, layer->audioMatrix);
      tex = pb->update(dt, *m, ctx);
      // A media with another proportion than the canvas is placed in a canvas-sized
      // image (fit / fill), so effects, transitions and mapping all see it right.
      // Generators and shaders are drawn at any size: they always cover the canvas.
      const bool procedural = m->type == MediaType::Generator || m->type == MediaType::Shader;
      if (layer && tex && !tex->empty() && layer->fit != MediaFit::Stretch && !procedural) {
        int W = project.canvasWidth(layer->canvasId), H = project.canvasHeight(layer->canvasId);
        float ma = float(tex->width()) / float(tex->height()), ca = float(W) / float(std::max(1, H));
        if (std::abs(ma / ca - 1) > 0.005f) {
          auto &fb = fitFbs_[slot];
          if (!fb) fb = std::make_unique<Framebuffer>();
          generators.place(*tex, W, H, mediaFitRect(layer->fit, tex->width(), tex->height(), W, H), *fb, mesh);
          tex = &fb->texture();
          return;
        }
      }
      fitFbs_.erase(slot);
    };
    // The sound always crossfades: `composed` (one picture holding both
    // contents) is about drawing, and is set again below for this frame.
    st.composed = false;
    tick(st.current, st.currentTex, incomingOpacity(st), it->first * 2);
    tick(st.previous, st.previousTex, outgoingOpacity(st), it->first * 2 + 1);
    if (layer && !layer->effects.empty()) {
      int id = it->first;
      BlobReport report;
      bool learn = learnBackground_.erase(id) > 0;
      if (st.currentTex)
        st.currentTex = chains_[id * 2].apply(layer->effects, st.currentTex, time_, effectTools_, mesh, &report, learn);
      if (st.previousTex)
        st.previousTex = chains_[id * 2 + 1].apply(layer->effects, st.previousTex, time_, effectTools_, mesh, nullptr, false);
      bool tracking = std::any_of(layer->effects.begin(), layer->effects.end(),
                                  [](const Effect &e) { return e.enabled && e.isBlobTracker(); });
      if (tracking && st.currentTex) {
        blobReports_[id] = report;
        emitBlobs(*layer, report);
      }
    }
    st.composed = false;
    if (st.type == TransitionType::Shader && st.fade < 1 && !st.transitionShader.empty() &&
        (st.currentTex || st.previousTex)) {
      if (const Texture *t = composeTransition(it->first, st)) {
        st.currentTex = t;
        st.previousTex = nullptr;
        st.composed = true;
      }
    } else if (st.type == TransitionType::Crossfade && st.fade < 1 && st.currentTex && st.previousTex &&
               !st.currentTex->empty() && !st.previousTex->empty()) {
      // Mixed into one image: two layers drawn one over the other at 1-f and f
      // let the background through (25% darker in the middle of a white-to-white fade).
      auto &fb = crossfadeFbs_[it->first];
      if (!fb) fb = std::make_unique<Framebuffer>();
      generators.crossfade(*st.previousTex, *st.currentTex, st.fade, *fb, mesh);
      st.currentTex = &fb->texture();
      st.previousTex = nullptr;
      st.composed = true;
    }
    // The cell's end mode, changed while its media plays, applies now (not only at the next launch).
    if (st.current && !st.fadingOut && st.columnId >= 0)
      for (auto &col : project.columns) {
        if (col.id != st.columnId) continue;
        auto cell = col.cells.find(it->first);
        if (cell == col.cells.end() || cell->second.mediaId != st.current->mediaId()) break;
        Playback &pb = *st.current;
        if (pb.loop && !cell->second.loop) { // already in a later turn: stay in this one, play to its end
          double d = pb.duration();
          if (d > 0 && pb.time() >= d) pb.seek(std::fmod(pb.time(), d));
        }
        pb.loop = cell->second.loop;
        if (cell->second.speed != st.cellSpeed) { // edited in the cell: the playing media follows
          pb.speed = cell->second.speed;            // (OSC /speed still changes only the playback)
          st.cellSpeed = cell->second.speed;
        }
        if (st.endMode != cell->second.endMode) {
          st.endMode = cell->second.endMode;
          st.endHandled = false;
        }
        break;
      }
    if (st.current && !st.fadingOut && !st.endHandled && st.endMode != EndMode::Loop &&
        st.endMode != EndMode::Freeze && st.current->finished()) {
      st.endHandled = true;
      ended.push_back(it->first);
    }
    if (!st.current && !st.previous) it = states_.erase(it);
    else ++it;
  }
  // Effect chains and blob data of layers without effects / playback.
  for (auto it = chains_.begin(); it != chains_.end();) {
    const Layer *l = project.layer(it->first / 2);
    bool keep = l && !l->effects.empty() && states_.count(it->first / 2);
    it = keep ? std::next(it) : chains_.erase(it);
  }
  for (auto it = blobReports_.begin(); it != blobReports_.end();)
    it = chains_.count(it->first * 2) ? std::next(it) : blobReports_.erase(it);

  // Acted upon after the loop: these start transitions (they change states_).
  bool next = false;
  for (int id : ended) {
    auto it = states_.find(id);
    if (it == states_.end()) continue;
    if (it->second.endMode == EndMode::Stop) stopLayer(id, columnTransition(activeColumn_));
    else if (it->second.endMode == EndMode::NextColumn) next = true;
  }
  if (next && !following())
    if (int n = columnInBoard(activeColumn_, 1); n >= 0) launchColumn(n);
  // Stinger playback.
  stingerTex_ = nullptr;
  if (stinger_) {
    const MediaItem *sm = project.media(stinger_->mediaId());
    if (!sm || stinger_->finished()) {
      stinger_.reset();
    } else {
      stingerTex_ = stinger_->update(dt, *sm, ctx);
      stinger_->setGain(1);
    }
  }
  for (auto it = fitFbs_.begin(); it != fitFbs_.end();) // layer gone
    it = states_.count(it->first / 2) ? std::next(it) : fitFbs_.erase(it);
  for (auto it = crossfadeFbs_.begin(); it != crossfadeFbs_.end();) {
    auto st = states_.find(it->first);
    bool busy = st != states_.end() && st->second.type == TransitionType::Crossfade && st->second.fade < 1;
    it = busy ? std::next(it) : crossfadeFbs_.erase(it);
  }
  for (auto it = transitionIsf_.begin(); it != transitionIsf_.end();) {
    auto st = states_.find(it->first);
    bool busy = st != states_.end() && st->second.type == TransitionType::Shader && st->second.fade < 1;
    if (busy) { ++it; continue; }
    transitionIn_[0].erase(it->first);
    transitionIn_[1].erase(it->first);
    transitionOut_.erase(it->first);
    it = transitionIsf_.erase(it);
  }
  updateAutoGo();
  cache.pruneLive();
}

// ISF transition between the outgoing and the incoming content of a layer
// (inputs startImage / endImage / progress).
const Texture *Engine::composeTransition(int layerId, LayerState &st) {
  auto &r = transitionIsf_[layerId];
  if (!r) r = std::make_unique<IsfRenderer>();
  if (!r->sync(st.transitionShader) || !r->info().transition) return nullptr;
  if (clearTex_.empty()) {
    unsigned char px[4] = {0, 0, 0, 0};
    clearTex_.upload(px, 1, 1);
  }
  const Texture *from = st.previousTex ? st.previousTex : &clearTex_;
  const Texture *to = st.currentTex ? st.currentTex : &clearTex_;
  const Texture *size = st.currentTex ? st.currentTex : from;
  auto &a = transitionIn_[0][layerId], &b = transitionIn_[1][layerId], &out = transitionOut_[layerId];
  if (!a) a = std::make_unique<Framebuffer>();
  if (!b) b = std::make_unique<Framebuffer>();
  if (!out) out = std::make_unique<Framebuffer>();
  generators.copyFlipped(*from, *a, mesh); // ISF is GL-native
  generators.copyFlipped(*to, *b, mesh);
  IsfValues v;
  isfDefaults(r->info(), v);
  v["progress"] = {st.fade};
  const Texture *t = r->render(size->width(), size->height(), time_,
                               {{"startImage", &a->texture()}, {"endImage", &b->texture()}}, v, mesh);
  if (!t) return nullptr;
  generators.copyFlipped(*t, *out, mesh);
  return &out->texture();
}

void Engine::render() {
  // Compositors of canvases that were removed.
  for (auto it = extraCompositors_.begin(); it != extraCompositors_.end();)
    it = project.extraCanvas(it->first) ? std::next(it) : extraCompositors_.erase(it);
  for (int canvasId : project.canvasIds()) renderCanvas(canvasId);
  // LEDs sample the final canvases, so master and blackout darken them too;
  // the DMX master dims only the LEDs.
  leds.transmit = !following(); // the main drives the lights; the backup only previews
  devices.heldDmx(leds.held, heldDmxVersion_); // values of "dmx" commands, sent with the LEDs
  leds.update(project, [this](int id) -> const Texture & { return canvasTexture(id); }, mesh, time_);

  // NDI of the main canvas: asynchronous read back, sent a frame later.
  if (project.ndiSend) {
    if (!ndiAvailable()) {
      ndiStatus_ = lumen::ndiStatus();
    } else {
      ndiReadback_.request(canvas());
      int w = 0, h = 0;
      if (ndiReadback_.collect(ndiPixels_, w, h)) ndiOk_ = ndiOut_.send(project.ndiName, ndiPixels_.data(), w, h);
      ndiStatus_ = ndiOk_ ? "enviando \"" + project.ndiName + "\"" : "erro";
    }
  } else {
    ndiStatus_.clear();
  }
  if (project.spoutSend && spoutAvailable()) {
    bool ok = spoutOut_.send(canvas().texture().id(), canvas().width(), canvas().height(), project.spoutName);
    spoutStatus_ = ok ? "enviando \"" + project.spoutName + "\"" : "erro ao enviar";
  } else {
    if (!spoutStatus_.empty()) spoutOut_.release();
    spoutStatus_.clear();
  }
}

void Engine::renderCanvas(int canvasId) {
  Compositor *comp = &compositor_;
  if (canvasId != 0) {
    auto &c = extraCompositors_[canvasId];
    if (!c) {
      c = std::make_unique<Compositor>();
      if (!c->init()) std::fprintf(stderr, "[engine] compositor init failed\n");
    }
    comp = c.get();
  }
  std::vector<LayerDraw> draws;
  for (auto it = project.layers.rbegin(); it != project.layers.rend(); ++it) {
    const Layer &l = *it;
    if (!l.visible || l.canvasId != canvasId) continue;
    auto st = states_.find(l.id);
    if (st == states_.end()) continue;
    if (st->second.previous && st->second.previousTex)
      draws.push_back({&l, st->second.previousTex, outgoingOpacity(st->second)});
    if (st->second.current && st->second.currentTex)
      draws.push_back({&l, st->second.currentTex, incomingOpacity(st->second)});
  }
  if (canvasId == 0 && stingerTex_) { // stinger on top of everything
    stingerLayer_.slices.resize(1);
    stingerLayer_.slices[0].warp.setRect({0, 0}, {float(project.canvasW), float(project.canvasH)});
    stingerLayer_.id = -1000;
    draws.push_back({&stingerLayer_, stingerTex_, 1.0f});
  }
  comp->render(project, canvasId, draws, mesh, blackout ? 0.0f : masterOpacity);
}

void Engine::restartOsc() {
  osc_.stop();
  oscError_.clear();
  if (!project.oscEnabled) return;
  if (!osc_.start(project.oscPort, &oscError_))
    std::fprintf(stderr, "[osc] %s\n", oscError_.c_str());
}

} // namespace lumen
