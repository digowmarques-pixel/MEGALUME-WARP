#include "engine/engine.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <sstream>

namespace lumen {

bool Engine::init() {
  bool ok = generators.init() && compositor_.init() && leds.init();
  if (!ok) std::fprintf(stderr, "[engine] shader initialisation failed\n");
  return ok;
}

void Engine::shutdown() {
  osc_.stop();
  midi_.stop();
  states_.clear(); // playbacks remove their audio voices
  cache.clear();
  spoutOut_.release();
  audio.stop();
}

MediaContext Engine::context() {
  DecoderSettings s;
  s.hardware = project.hardwareDecode;
  s.nativeHap = FrameUploader::supportsS3tc();
  return MediaContext{cache, generators, mesh, &audio, s, frame_};
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
}

// Splits `/addr 1 2.5 "some text" word` into an OSC message.
static bool parseOscLine(const std::string &line, OscMessage &m) {
  std::vector<std::string> tokens;
  std::string cur;
  bool quoted = false, any = false;
  for (char c : line) {
    if (c == '"') { quoted = !quoted; any = true; continue; }
    if (!quoted && (c == ' ' || c == '\t')) {
      if (!cur.empty() || any) tokens.push_back(cur);
      cur.clear();
      any = false;
      continue;
    }
    cur += c;
  }
  if (!cur.empty() || any) tokens.push_back(cur);
  if (tokens.empty() || tokens[0].empty() || tokens[0][0] != '/') return false;
  m.address = tokens[0];
  m.args.clear();
  for (size_t i = 1; i < tokens.size(); ++i) {
    const std::string &t = tokens[i];
    char *end = nullptr;
    long iv = std::strtol(t.c_str(), &end, 10);
    if (!t.empty() && *end == 0) { m.args.emplace_back(int32_t(iv)); continue; }
    float fv = std::strtof(t.c_str(), &end);
    if (!t.empty() && *end == 0) { m.args.emplace_back(fv); continue; }
    m.args.emplace_back(t);
  }
  return true;
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
  for (auto &line : c.oscActions) {
    OscMessage m;
    if (parseOscLine(line, m)) sendOsc(m);
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
  runColumnActions(col, index);
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
  for (size_t i = 0; i < inbox_.size(); ++i) {
    const OscMessage &m = inbox_[i];
    bool known = i < oscCount ? handleOsc(m) : true;
    applyMappings(m);
    oscLog.push_back((known ? "" : "? ") + m.toString() + "   [" + m.sender + "]");
  }
  while (oscLog.size() > 200) oscLog.pop_front();
  updateMappings(dt);
  if (project.midiEnabled && time_ >= midiRescanAt_) { // hot-plugged controllers
    midi_.rescan();
    midiRescanAt_ = time_ + 3.0;
  }

  MediaContext ctx = context();
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
    if (!st.current && !st.previous) it = states_.erase(it);
    else ++it;
  }
  cache.pruneLive();
}

void Engine::render() {
  std::vector<LayerDraw> draws;
  for (auto it = project.layers.rbegin(); it != project.layers.rend(); ++it) {
    const Layer &l = *it;
    if (!l.visible) continue;
    auto st = states_.find(l.id);
    if (st == states_.end()) continue;
    if (st->second.previous && st->second.previousTex)
      draws.push_back({&l, st->second.previousTex, outgoingOpacity(st->second)});
    if (st->second.current && st->second.currentTex)
      draws.push_back({&l, st->second.currentTex, incomingOpacity(st->second)});
  }
  compositor_.render(project, draws, mesh, blackout ? 0.0f : masterOpacity);
  // LEDs sample the final canvas, so master and blackout darken them too; the
  // DMX master dims only the LEDs.
  leds.update(project, canvas().texture(), mesh, time_);

  if (project.spoutSend && spoutAvailable()) {
    bool ok = spoutOut_.send(canvas().texture().id(), canvas().width(), canvas().height(), project.spoutName);
    spoutStatus_ = ok ? "enviando \"" + project.spoutName + "\"" : "erro ao enviar";
  } else {
    if (!spoutStatus_.empty()) spoutOut_.release();
    spoutStatus_.clear();
  }
}

void Engine::restartOsc() {
  osc_.stop();
  oscError_.clear();
  if (!project.oscEnabled) return;
  if (!osc_.start(project.oscPort, &oscError_))
    std::fprintf(stderr, "[osc] %s\n", oscError_.c_str());
}

} // namespace lumen
