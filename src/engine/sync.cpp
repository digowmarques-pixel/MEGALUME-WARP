// Redundancy: the main computer streams the show state ten times a second
// (and at once when a cue changes) as OSC "/lumen/sync/state" with a JSON
// argument. The backup launches the same columns and cells, mirrors masters,
// opacity and timelines, and jumps its videos to the main's time when they
// drift. If the main goes silent for `redundancy.timeout` seconds the backup
// takes over: it accepts inputs again and runs its takeover commands
// (switch a video router, unmute an amplifier...).
#include "engine/engine.h"

#include "core/md5.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <sstream>

namespace lumen {

namespace {

// Wall clock: the engine clock stops when the app stalls, this one does not.
double wallNow() {
  using namespace std::chrono;
  return duration<double>(steady_clock::now().time_since_epoch()).count();
}

// "ip[:port]"; an empty host means broadcast on the local network.
void splitPeer(const std::string &peer, int defaultPort, std::string &host, int &port) {
  host = peer;
  port = defaultPort;
  size_t c = peer.rfind(':');
  if (c != std::string::npos) {
    host = peer.substr(0, c);
    port = std::atoi(peer.c_str() + c + 1);
    if (port <= 0) port = defaultPort;
  }
  if (host.empty()) host = "255.255.255.255";
}

std::string hostOf(const std::string &sender) { return sender.substr(0, sender.rfind(':')); }

const char *stateName(SyncState s) {
  switch (s) {
  case SyncState::Waiting: return "waiting";
  case SyncState::Following: return "following";
  case SyncState::Active: return "active";
  default: return "off";
  }
}

} // namespace

void Engine::setSync(SyncRole role, const std::string &peer, int port) {
  syncIn_.stop();
  syncRole_ = role;
  syncPeer_ = peer;
  syncPort_ = port > 0 ? port : 7400;
  syncState_ = role == SyncRole::Main ? SyncState::Main : role == SyncRole::Backup ? SyncState::Waiting : SyncState::Off;
  syncLastRx_ = syncLastPeerRx_ = -1;
  syncRxSeq_ = -1;
  syncMainBack_ = false;
  syncAppliedColumnRun_ = syncSentColumnRun_ = -1;
  syncAppliedRun_.clear();
  syncSeekedAt_.clear();
  syncStatus_.clear();
  syncPeerStatus_.clear();
  if (role == SyncRole::Off) return;
  std::string err;
  // Both sides listen: the backup for the state, the main for the backup's status.
  if (!syncIn_.start(syncPort_, &err)) syncStatus_ = "porta " + std::to_string(syncPort_) + ": " + err;
}

std::string Engine::syncProjectSignature() const {
  // What must match on both computers: media, layers, columns and timelines.
  // Runtime values (opacity, masters...) are mirrored, so they are left out.
  std::ostringstream s;
  for (auto &m : project.library) {
    std::string name = m.path.substr(m.path.find_last_of("/\\") + 1);
    s << 'm' << m.id << ':' << int(m.type) << ':' << name << ';';
  }
  for (auto &l : project.layers) s << 'l' << l.id << ';';
  for (auto &c : project.columns) {
    s << 'c' << c.id << '{';
    for (auto &[layer, cell] : c.cells) s << layer << '=' << cell.mediaId << ',';
    s << '}';
  }
  for (auto &t : project.timelines) s << 't' << t.id << ':' << t.duration << ';';
  return md5Hex(s.str()).substr(0, 12);
}

void Engine::syncSendState() {
  nlohmann::json j;
  j["seq"] = ++syncSeq_;
  j["sig"] = syncSignature_;
  j["col"] = activeColumn_;
  j["colRun"] = columnRun_;
  j["blackout"] = blackout;
  j["master"] = masterOpacity;
  j["volume"] = project.masterVolume;
  j["dmx"] = project.dmxMaster;
  j["fader"] = transitionFader;
  auto &layers = j["layers"] = nlohmann::json::array();
  for (auto &l : project.layers) {
    nlohmann::json jl = {{"id", l.id}, {"op", l.opacity}, {"vis", l.visible}};
    auto it = states_.find(l.id);
    if (it != states_.end()) {
      const LayerState &st = it->second;
      jl["run"] = st.run;
      bool playing = st.current && !st.fadingOut;
      jl["m"] = playing ? st.current->mediaId() : -1;
      jl["col"] = st.columnId;
      if (playing) jl["t"] = st.current->time();
    }
    layers.push_back(std::move(jl));
  }
  auto &tl = j["tl"] = nlohmann::json::array();
  for (auto &[id, r] : timelineRuns_) tl.push_back({{"id", id}, {"t", r.time}, {"p", r.playing}});

  std::string host;
  int port;
  splitPeer(syncPeer_, syncPort_, host, port);
  syncOut_.send(host, port, OscMessage{"/lumen/sync/state", {j.dump()}, ""});
  syncLastTx_ = wallNow();
  syncSentColumnRun_ = columnRun_;
  syncSentBlackout_ = blackout;
}

void Engine::syncApply(const std::string &text) {
  auto j = nlohmann::json::parse(text, nullptr, false);
  if (!j.is_object()) return;
  long long seq = j.value("seq", 0LL);
  if (seq <= syncRxSeq_ && syncRxSeq_ - seq < 1000) return; // late / duplicated packet
  syncRxSeq_ = seq;
  double now = wallNow();
  syncApplying_ = true;

  // Cue changes first, with the same transitions as on the main.
  int colRun = j.value("colRun", -1), col = j.value("col", -1);
  bool columnChanged = colRun != syncAppliedColumnRun_;
  if (columnChanged) {
    syncAppliedColumnRun_ = colRun;
    if (col >= 0 && col < int(project.columns.size())) launchColumn(col);
    else if (col < 0 && activeColumn_ >= 0) stopAll();
  }

  for (auto &jl : j.value("layers", nlohmann::json::array())) {
    int id = jl.value("id", -1);
    Layer *layer = project.layer(id);
    if (!layer) continue;
    layer->opacity = jl.value("op", layer->opacity);
    layer->visible = jl.value("vis", layer->visible);
    if (!jl.contains("run")) continue;
    int run = jl.value("run", 0), media = jl.value("m", -1), columnId = jl.value("col", -1);
    auto applied = syncAppliedRun_.find(id);
    bool changed = applied == syncAppliedRun_.end() || applied->second != run;
    syncAppliedRun_[id] = run;
    auto st = states_.find(id);
    int mine = st != states_.end() && st->second.current && !st->second.fadingOut ? st->second.current->mediaId() : -1;
    // A cell launched or stopped on its own (the column launch above already
    // covers the others).
    if (changed && !columnChanged && media != mine) {
      if (media < 0) {
        stopLayer(id);
      } else {
        for (size_t c = 0; c < project.columns.size(); ++c)
          if (project.columns[c].id == columnId) launchCell(int(c), id);
      }
      st = states_.find(id);
    }
    // Drift: jump to the main's time (at most once a second per layer).
    if (media >= 0 && jl.contains("t") && st != states_.end() && st->second.current &&
        st->second.current->mediaId() == media) {
      double target = jl.value("t", 0.0), t = st->second.current->time();
      double &last = syncSeekedAt_[id];
      if (std::fabs(t - target) > double(project.redundancy.tolerance) && now - last > 1.0) {
        char msg[160];
        std::snprintf(msg, sizeof(msg), "[reserva] layer %s: %.2f s -> %.2f s (tempo da principal)", layer->name.c_str(), t, target);
        oscLog.push_back(msg);
        st->second.current->seek(target);
        last = now;
      }
    }
  }

  blackout = j.value("blackout", blackout);
  masterOpacity = j.value("master", masterOpacity);
  project.masterVolume = j.value("volume", project.masterVolume);
  project.dmxMaster = j.value("dmx", project.dmxMaster);
  transitionFader = j.value("fader", transitionFader);

  std::map<int, bool> mainTimelines;
  for (auto &jt : j.value("tl", nlohmann::json::array())) {
    int id = jt.value("id", -1);
    if (!project.timeline(id)) continue;
    mainTimelines[id] = true;
    double t = jt.value("t", 0.0);
    bool playing = jt.value("p", false);
    const TimelineRun *r = timelineRun(id);
    if (!r) playTimeline(id, true);
    if (playing && !(r && r->playing)) playTimeline(id, false);
    if (!playing && r && r->playing) pauseTimeline(id);
    r = timelineRun(id);
    if (r && std::fabs(r->time - t) > double(project.redundancy.tolerance)) seekTimeline(id, t);
  }
  std::vector<int> extra;
  for (auto &[id, r] : timelineRuns_)
    if (!mainTimelines.count(id)) extra.push_back(id);
  for (int id : extra) stopTimeline(id);

  syncApplying_ = false;
  std::string sig = j.value("sig", "");
  syncPeerStatus_ = sig.empty() || sig == syncSignature_ ? "" : "projeto diferente do da principal";
}

void Engine::syncTakeOver() {
  if (syncRole_ != SyncRole::Backup || syncState_ == SyncState::Active) return;
  syncState_ = SyncState::Active;
  syncMainBack_ = false;
  std::printf("[sync] reserva assumiu o show\n");
  oscLog.push_back("[reserva] assumiu o show");
  std::istringstream lines(project.redundancy.takeoverCommands);
  for (std::string line; std::getline(lines, line);) runCommand(line);
}

void Engine::syncFollow() {
  if (syncRole_ != SyncRole::Backup) return;
  syncState_ = syncLastRx_ >= 0 && wallNow() - syncLastRx_ < project.redundancy.timeout ? SyncState::Following
                                                                                        : SyncState::Waiting;
  syncMainBack_ = false;
}

void Engine::updateSync() {
  if (syncRole_ == SyncRole::Off) return;
  double now = wallNow();
  if (now - syncSignatureAt_ > 2) {
    syncSignature_ = syncProjectSignature();
    syncSignatureAt_ = now;
  }

  std::vector<OscMessage> in;
  syncIn_.drain(in);
  for (auto &m : in) {
    if (m.args.empty()) continue;
    const std::string *text = std::get_if<std::string>(&m.args[0]);
    if (!text) continue;
    if (m.address == "/lumen/sync/state" && syncRole_ == SyncRole::Backup) {
      syncLastRx_ = now;
      syncMainHost_ = hostOf(m.sender);
      if (syncState_ == SyncState::Active) {
        syncMainBack_ = true; // stays in charge until the operator says so
        continue;
      }
      syncState_ = SyncState::Following;
      syncApply(*text);
    } else if (m.address == "/lumen/sync/backup" && syncRole_ == SyncRole::Main) {
      syncLastPeerRx_ = now;
      auto j = nlohmann::json::parse(*text, nullptr, false);
      if (!j.is_object()) continue;
      std::string state = j.value("state", ""), sig = j.value("sig", "");
      std::string who = "reserva " + hostOf(m.sender) + ": ";
      if (state == "active") syncPeerStatus_ = who + "ASSUMIU o show";
      else if (!sig.empty() && sig != syncSignature_) syncPeerStatus_ = who + "projeto DIFERENTE";
      else if (state == "following") syncPeerStatus_ = who + "seguindo";
      else syncPeerStatus_ = who + "aguardando";
    }
  }

  if (syncRole_ == SyncRole::Main) {
    syncState_ = SyncState::Main;
    // Every 100 ms, and at once when a cue or the blackout changes.
    if (now - syncLastTx_ >= 0.1 || columnRun_ != syncSentColumnRun_ || blackout != syncSentBlackout_) syncSendState();
    if (syncLastPeerRx_ < 0) syncPeerStatus_ = "nenhuma reserva respondeu ainda";
    else if (now - syncLastPeerRx_ > 3) syncPeerStatus_ = "reserva sem contato há " + std::to_string(int(now - syncLastPeerRx_)) + " s";
    return;
  }

  // Backup.
  if (syncState_ == SyncState::Following && now - syncLastRx_ > double(project.redundancy.timeout)) syncTakeOver();
  if (now - syncLastTx_ >= 0.5) { // status for the main's screen
    nlohmann::json j = {{"state", stateName(syncState_)}, {"sig", syncSignature_}};
    std::string host;
    int port;
    splitPeer(syncPeer_, syncPort_, host, port);
    if (syncPeer_.empty() && !syncMainHost_.empty()) host = syncMainHost_;
    syncOut_.send(host, port, OscMessage{"/lumen/sync/backup", {j.dump()}, ""});
    syncLastTx_ = now;
  }
  switch (syncState_) {
  case SyncState::Waiting: syncStatus_ = "aguardando a principal"; break;
  case SyncState::Following: syncStatus_ = "seguindo a principal " + syncMainHost_; break;
  case SyncState::Active:
    syncStatus_ = syncMainBack_ ? "no comando (a principal voltou)" : "no comando: a principal parou de responder";
    break;
  default: break;
  }
}

} // namespace lumen
