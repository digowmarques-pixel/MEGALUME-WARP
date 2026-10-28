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
  // Reads available bytes (non-blocking, waits at most timeoutMs). `lost`:
  // the device went away (unplugged): the port must be closed and reopened.
  std::string read(int timeoutMs, bool *lost = nullptr);

private:
  static intptr_t invalid() { return -1; }
  intptr_t handle_ = -1;
  std::string name_;
};

// Reads lines in a background thread ("A0 512", "botao 1", "sensor 0.42").
// A port that is not there yet (Arduino still starting) or goes away
// (unplugged) is opened again every second.
class SerialReader {
public:
  ~SerialReader();
  // Starts reading; false (with the error) when the port could not be opened
  // now, but it keeps trying.
  bool start(const std::string &port, int baud, std::string *error = nullptr);
  void stop();
  bool running() const { return running_; }
  bool connected() const { return connected_; }
  void drain(std::vector<std::string> &lines);
  const std::string &port() const { return port_; }

private:
  void run();
  SerialPort port0_;
  std::string port_;
  int baud_ = 9600;
  std::thread thread_;
  std::atomic<bool> running_{false}, connected_{false};
  std::mutex mutex_;
  std::deque<std::string> lines_;
};

} // namespace lumen
