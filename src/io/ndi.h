// NDI (video over the network): receive sources and send outputs. The NDI
// runtime is loaded when first needed (Windows: install "NDI Tools" or the
// NDI Runtime; Linux: libndi.so). Without it these classes do nothing and
// ndiAvailable() is false.
#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace lumen {

bool ndiAvailable();
std::string ndiStatus(); // why it is not available
std::vector<std::string> ndiSources(); // names seen on the network (refreshed in the background)

class NdiReceiver {
public:
  explicit NdiReceiver(const std::string &source);
  ~NdiReceiver();
  // Latest frame (RGBA, row 0 = top). Returns true when a new one arrived.
  bool latest(std::vector<uint8_t> &rgba, int &w, int &h);
  bool connected() const { return connected_; }

private:
  void run();
  std::string source_;
  std::thread thread_;
  std::atomic<bool> running_{true}, connected_{false};
  std::mutex mutex_;
  std::vector<uint8_t> frame_;
  int w_ = 0, h_ = 0;
  bool fresh_ = false;
};

class NdiSender {
public:
  NdiSender() = default;
  ~NdiSender();
  // `rgba` row 0 = top. Copied: the caller can reuse its buffer.
  bool send(const std::string &name, const uint8_t *rgba, int w, int h, int fpsN = 60, int fpsD = 1);

private:
  void *inst_ = nullptr;
  std::string name_;
  std::vector<uint8_t> buf_[2];
  int cur_ = 0;
};

} // namespace lumen
