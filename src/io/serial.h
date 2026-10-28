// Serial port (Arduino, Teensy, ESP32 over USB; RS-232 projectors and
// switchers). Windows: COM3; Linux / macOS: /dev/ttyUSB0, /dev/cu.usbmodem...
#pragma once

#include <atomic>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace lumen {

std::vector<std::string> serialPorts();

class SerialPort {
public:
  ~SerialPort();
  bool open(const std::string &name, int baud, std::string *error = nullptr);
  void close();
  bool isOpen() const { return handle_ != invalid(); }
  const std::string &name() const { return name_; }
  bool write(const std::string &data);
  // Reads available bytes (non-blocking, waits at most timeoutMs).
  std::string read(int timeoutMs);

private:
  static intptr_t invalid() { return -1; }
  intptr_t handle_ = -1;
  std::string name_;
};

// Reads lines in a background thread ("A0 512", "botao 1", "sensor 0.42").
class SerialReader {
public:
  ~SerialReader();
  bool start(const std::string &port, int baud, std::string *error = nullptr);
  void stop();
  bool running() const { return running_; }
  void drain(std::vector<std::string> &lines);
  const std::string &port() const { return port_; }

private:
  void run();
  SerialPort port0_;
  std::string port_;
  std::thread thread_;
  std::atomic<bool> running_{false};
  std::mutex mutex_;
  std::deque<std::string> lines_;
};

} // namespace lumen
