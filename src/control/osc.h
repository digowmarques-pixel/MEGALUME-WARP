// Minimal OSC 1.0 over UDP: a background receiver thread plus a sender.
#pragma once

#include <atomic>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <variant>
#include <vector>

namespace lumen {

using OscArg = std::variant<int32_t, float, std::string, bool>;

struct OscMessage {
  std::string address;
  std::vector<OscArg> args;
  std::string sender; // "ip:port"

  float number(size_t i, float def = 0) const;
  std::string toString() const;
};

// Parses a datagram (message or bundle). Returns false on malformed data.
bool parseOsc(const uint8_t *data, size_t size, std::vector<OscMessage> &out);
std::vector<uint8_t> encodeOsc(const OscMessage &m);
// OSC address pattern match supporting '*', '?', '[abc]', '{foo,bar}'.
bool oscMatch(const char *pattern, const char *address);

class OscServer {
public:
  ~OscServer();
  bool start(int port, std::string *error = nullptr);
  void stop();
  bool running() const { return running_; }
  int port() const { return port_; }
  // Moves received messages into `out` (called from the main thread).
  void drain(std::vector<OscMessage> &out);

private:
  void run();
  std::thread thread_;
  std::atomic<bool> running_{false};
  std::mutex mutex_;
  std::deque<OscMessage> queue_;
  intptr_t socket_ = -1;
  int port_ = 0;
};

class OscSender {
public:
  ~OscSender();
  bool send(const std::string &host, int port, const OscMessage &m);

private:
  intptr_t socket_ = -1;
};

} // namespace lumen
