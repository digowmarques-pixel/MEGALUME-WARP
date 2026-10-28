#include "engine/engine.h"

#include <algorithm>
#include <cstdio>

namespace lumen {

bool Engine::init() {
  bool ok = generators.init() && compositor_.init();
  if (!ok) std::fprintf(stderr, "[engine] shader initialisation failed\n");
  return ok;
}

void Engine::shutdown() {
  osc_.stop();
  states_.clear();
  cache.clear();
}

void Engine::setProject(Project p, bool keepPlayback) {
  bool portChanged = p.oscPort != project.oscPort || p.oscEnabled != project.oscEnabled;
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

void Engine::startTransition(int layerId, const Cell *cell, float duration, int columnId) {
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
  if (media) st.current = std::make_unique<Playback>(*media, *cell);
  st.fade = duration > 0 ? 0.0f : 1.0f;
  st.fadeDuration = duration;
  st.fadingOut = !media;
  st.columnId = media ? columnId : -1;
}

void Engine::launchColumn(int index) {
  if (index < 0 || index >= int(project.columns.size())) return;
  const Column &col = project.columns[size_t(index)];
  float fade = columnTransition(index);
  for (auto &l : project.layers) {
    auto it = col.cells.find(l.id);
    const Cell *cell = (it != col.cells.end() && it->second.mediaId >= 0) ? &it->second : nullptr;
    startTransition(l.id, cell, fade, col.id);
  }
  activeColumn_ = index;
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
  startTransition(layerId, &it->second, columnTransition(columnIndex), col.id);
}

void Engine::stopLayer(int layerId, float fade) {
  startTransition(layerId, nullptr, fade < 0 ? project.defaultTransition : fade, -1);
}

void Engine::stopAll(float fade) {
  for (auto &l : project.layers) stopLayer(l.id, fade);
  activeColumn_ = -1;
}

void Engine::update(double dt) {
  time_ += dt;

  // OSC input first so parameter changes apply to this frame.
  inbox_.clear();
  osc_.drain(inbox_);
  for (auto &m : inbox_) {
    bool known = handleOsc(m);
    applyMappings(m);
    oscLog.push_back((known ? "" : "? ") + m.toString() + "   [" + m.sender + "]");
  }
  while (oscLog.size() > 200) oscLog.pop_front();
  updateMappings(dt);

  for (auto it = states_.begin(); it != states_.end();) {
    LayerState &st = it->second;
    if (st.fade < 1) {
      st.fade = st.fadeDuration > 0 ? std::min(1.0f, st.fade + float(dt) / st.fadeDuration) : 1.0f;
    }
    if (st.fade >= 1 && st.previous) {
      st.previous.reset();
      st.previousTex = nullptr;
    }
    auto tick = [&](std::unique_ptr<Playback> &pb, const Texture *&tex) {
      tex = nullptr;
      if (!pb) return;
      const MediaItem *m = project.media(pb->mediaId());
      if (!m) { pb.reset(); return; } // media removed from the library
      tex = pb->update(dt, *m, cache, generators, mesh);
    };
    tick(st.current, st.currentTex);
    tick(st.previous, st.previousTex);
    if (!st.current && !st.previous) it = states_.erase(it);
    else ++it;
  }
}

void Engine::render() {
  std::vector<LayerDraw> draws;
  for (auto it = project.layers.rbegin(); it != project.layers.rend(); ++it) {
    const Layer &l = *it;
    if (!l.visible) continue;
    auto st = states_.find(l.id);
    if (st == states_.end()) continue;
    if (st->second.previous && st->second.previousTex)
      draws.push_back({&l, st->second.previousTex, 1.0f - st->second.fade});
    if (st->second.current && st->second.currentTex)
      draws.push_back({&l, st->second.currentTex, st->second.fade});
  }
  compositor_.render(project, draws, mesh, blackout ? 0.0f : masterOpacity);
}

void Engine::restartOsc() {
  osc_.stop();
  oscError_.clear();
  if (!project.oscEnabled) return;
  if (!osc_.start(project.oscPort, &oscError_))
    std::fprintf(stderr, "[osc] %s\n", oscError_.c_str());
}

} // namespace lumen
