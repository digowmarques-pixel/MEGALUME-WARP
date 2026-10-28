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
  states_.clear(); // playbacks remove their audio voices
  extraCompositors_.clear();
  chains_.clear();
  cache.clear();
  spoutOut_.release();
  audio.stop();
}

MediaContext Engine::context() {
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
    cache.clear();
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
  if (activeColumn_ >= int(project.columns.size())) activeColumn_ = -1;
  if (portChanged || !keepPlayback) restartOsc();
  if (midiChanged || !keepPlayback) restartMidi();
  if (audioDeviceInUse_ != project.audioDevice) restartAudio();
  restartDmxInput();
  tweens_.erase(std::remove_if(tweens_.begin(), tweens_.end(),
                               [&](const ParamTween &t) { return !project.layer(t.layerId); }),
                tweens_.end());
  project.resolveSurfaces();
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
  // Through black: the old content is gone by the middle, the new one starts there.
  if (st.type == TransitionType::ThroughBlack) return std::max(0.0f, 1.0f - 2.0f * st.fade);
  return 1.0f - st.fade;
}

float Engine::incomingOpacity(const LayerState &st) {
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
  st.fadingOut = !media;
  st.columnId = media ? columnId : -1;
  st.endMode = cell ? cell->endMode : EndMode::Loop;
  st.endHandled = false;
}

void Engine::sendOsc(const OscMessage &m, bool feedbackOnly) {
  for (auto &t : project.oscTargets) {
    if (!t.enabled || (feedbackOnly && !t.feedback)) continue;
    if (oscOut_.send(t.host, t.port, m)) ++oscSent_;
  }
}

void Engine::runColumnActions(const Column &c, int index) {
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
  if (index < 0 || index >= int(project.columns.size())) return;
  const Column &col = project.columns[size_t(index)];
  float fade = columnTransition(index);
  for (auto &l : project.layers) {
    auto it = col.cells.find(l.id);
    const Cell *cell = (it != col.cells.end() && it->second.mediaId >= 0) ? &it->second : nullptr;
    startTransition(l.id, cell, fade, col.id, col.transitionType);
  }
  activeColumn_ = index;
  columnLaunchedAt_ = time_;
  autoGoFired_ = false;
  applyColumnStates(index, col.transitionType == TransitionType::Cut ? 0.0f : fade);
  runColumnActions(col, index);
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
  float left = autoGoRemaining();
  if (left < 0 || left > 0) return;
  autoGoFired_ = true;
  const Column &c = project.columns[size_t(activeColumn_)];
  if (c.autoGoTarget > 0) launchColumn(c.autoGoTarget - 1);
  else if (activeColumn_ + 1 < int(project.columns.size())) launchColumn(activeColumn_ + 1);
  else launchColumn(0); // end of the show: an installation loops back to the start
}

void Engine::updateSchedule() {
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
    default: break;
    }
    oscLog.push_back(std::string("[agenda] ") + scheduleActionName(e.action));
  }
}

void Engine::go() {
  if (project.columns.empty()) return;
  launchColumn(std::min(activeColumn_ + 1, int(project.columns.size()) - 1));
}

void Engine::back() {
  if (project.columns.empty()) return;
  launchColumn(std::max(activeColumn_ - 1, 0));
}

void Engine::launchCell(int columnIndex, int layerId) {
  if (columnIndex < 0 || columnIndex >= int(project.columns.size())) return;
  const Column &col = project.columns[size_t(columnIndex)];
  auto it = col.cells.find(layerId);
  if (it == col.cells.end() || it->second.mediaId < 0) {
    stopLayer(layerId);
    return;
  }
  LayerState &st = states_[layerId];
  if (st.current && st.current->mediaId() == it->second.mediaId && st.columnId == col.id) {
    st.current->restart(); // clicking a playing cell restarts it
    return;
  }
  startTransition(layerId, &it->second, columnTransition(columnIndex), col.id, col.transitionType);
}

void Engine::stopLayer(int layerId, float fade) {
  startTransition(layerId, nullptr, fade < 0 ? project.defaultTransition : fade, -1);
}

void Engine::stopAll(float fade) {
  for (auto &l : project.layers) stopLayer(l.id, fade);
  if (activeColumn_ >= 0 && activeColumn_ < int(project.columns.size()) && !project.oscTargets.empty())
    sendOsc(OscMessage{"/millumin/board/stoppedColumn",
                       {int32_t(activeColumn_ + 1), project.columns[size_t(activeColumn_)].name}, ""}, true);
  activeColumn_ = -1;
  if (!project.oscTargets.empty()) sendOsc(OscMessage{"/lumen/status/column", {int32_t(0), std::string()}, ""}, true);
}

void Engine::restartAudio() {
  audioDeviceInUse_ = project.audioDevice;
  // Voices hold no reference to the device, so playing media keep their
  // voices; they are simply mixed by the new device.
  if (!audio.start(project.audioDevice))
    std::fprintf(stderr, "[audio] %s\n", audio.error().c_str());
}

void Engine::restartMidi() {
  midi_.stop();
  if (project.midiEnabled) midi_.start();
}

void Engine::update(double dt) {
  time_ += dt;
  ++frame_;
  if (audioDeviceInUse_ != project.audioDevice) restartAudio();
  audio.master = project.masterVolume;

  // Control input first so parameter changes apply to this frame.
  inbox_.clear();
  osc_.drain(inbox_);
  size_t oscCount = inbox_.size();
  midi_.drain(inbox_);
  dmxIn_.drain(inbox_);
  for (auto &m : injected_) inbox_.push_back(std::move(m));
  injected_.clear();
  for (size_t i = 0; i < inbox_.size(); ++i) {
    const OscMessage &m = inbox_[i];
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

  updateTweens(dt);
  updateSchedule();
  project.resolveSurfaces();

  MediaContext ctx = context();
  std::vector<int> ended;
  for (auto it = states_.begin(); it != states_.end();) {
    LayerState &st = it->second;
    if (st.fade < 1) {
      st.fade = st.fadeDuration > 0 ? std::min(1.0f, st.fade + float(dt) / st.fadeDuration) : 1.0f;
    }
    if (st.fade >= 1 && st.previous) {
      st.previous.reset();
      st.previousTex = nullptr;
    }
    const Layer *layer = project.layer(it->first);
    float vol = layer && !layer->mute ? layer->volume : 0.0f;
    auto tick = [&](std::unique_ptr<Playback> &pb, const Texture *&tex, float fade) {
      tex = nullptr;
      if (!pb) return;
      const MediaItem *m = project.media(pb->mediaId());
      if (!m) { pb.reset(); return; } // media removed from the library
      // Sound follows the visual crossfade of the layer.
      pb->setGain(vol * fade);
      tex = pb->update(dt, *m, ctx);
    };
    tick(st.current, st.currentTex, incomingOpacity(st));
    tick(st.previous, st.previousTex, outgoingOpacity(st));
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
  if (next && activeColumn_ + 1 < int(project.columns.size())) launchColumn(activeColumn_ + 1);
  updateAutoGo();
  cache.pruneLive();
}

void Engine::render() {
  // Compositors of canvases that were removed.
  for (auto it = extraCompositors_.begin(); it != extraCompositors_.end();)
    it = project.extraCanvas(it->first) ? std::next(it) : extraCompositors_.erase(it);
  for (int canvasId : project.canvasIds()) renderCanvas(canvasId);
  // LEDs sample the final canvases, so master and blackout darken them too;
  // the DMX master dims only the LEDs.
  leds.update(project, [this](int id) -> const Texture & { return canvasTexture(id); }, mesh, time_);

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
