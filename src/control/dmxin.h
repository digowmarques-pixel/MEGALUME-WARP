// Art-Net / sACN receiver: a lighting desk (or any DMX source) drives Lumen.
// Changed channels become /artnet/<universe>/<channel> or
// /sacn/<universe>/<channel> messages (value 0..255) for the mappings.
#pragma once

#include "control/osc.h"
#include "core/dmx.h"

#include <atomic>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace lumen {

class DmxReceiver {
public:
  ~DmxReceiver();
  // Universes [first, first + count) are kept; sACN joins their multicast groups.
  bool start(DmxProtocol protocol, int first, int count, std::string *error = nullptr);
  void stop();
  bool running() const { return running_; }
  // Emits one message per channel that changed since the last call.
  void drain(std::vector<OscMessage> &out);
  // Latest values of a universe (for the monitor). False if nothing received.
  bool universe(int u, DmxUniverse &out) const;
  int packets() const { return packets_; }

private:
  void run();
  std::thread thread_;
  std::atomic<bool> running_{false};
  std::atomic<int> packets_{0};
  mutable std::mutex mutex_;
  std::map<int, DmxUniverse> live_, seen_;
  std::map<int, std::string> source_;
  intptr_t socket_ = -1;
  DmxProtocol protocol_ = DmxProtocol::ArtNet;
  int first_ = 0, count_ = 1;
};

} // namespace lumen
