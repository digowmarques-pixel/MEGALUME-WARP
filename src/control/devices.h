// Device commands: lines of text that control other equipment, sent from
// columns, timeline flags, the schedule or the projector panel. They run in a
// background thread, in order (a "wait" line pauses the following ones).
//
//   pjlink <projector|host> power on|off | shutter close|open | input <n> | status
//   midi [@port] note <ch> <note> <vel> | noteoff <ch> <note> | cc <ch> <n> <v> | pc <ch> <p>
//   dmx [artnet|sacn] <universe> <channel>[-<last>] <value 0..255>
//   tcp <host>:<port> <text>      udp <host>:<port> <text>
//   serial <port>[@baud] <text>   osc <host>:<port> /address args...
//   wait <seconds>
// Text accepts [CR] [LF] [TAB] [HEX_0D] and \r \n \t.
#pragma once

#include "core/dmx.h"
#include "core/project.h"

#include <atomic>
#include <condition_variable>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

class RtMidiOut;

namespace lumen {

// "[CR]" -> "\r" etc.
std::string expandDeviceText(const std::string &text);

class DeviceCommands {
public:
  DeviceCommands();
  ~DeviceCommands();
  void run(const std::string &line, const Project &p) { runBatch({line}, p); }
  void runBatch(const std::vector<std::string> &lines, const Project &p);
  void drainLog(std::vector<std::string> &out);
  std::vector<std::string> midiOutPorts();
  // Last known PJLink power state by host ("ligado", "desligado", "erro: ...").
  std::string projectorStatus(const std::string &host);
  // DMX set by "dmx" commands, held until changed: (protocol, universe) ->
  // channels. Sent by the LED engine, merged with the LED universes and
  // refreshed like them (a single packet was lost, dropped by sACN receivers
  // after 2.5 s, or zeroed by the next LED frame on a shared universe).
  using HeldDmx = std::map<std::pair<int, int>, DmxUniverse>;
  bool heldDmx(HeldDmx &out, uint64_t &version); // copies when it changed since `version`
  // Closes the port a "serial" command keeps open, so the serial input can
  // take it (Windows lets only one handle open a COM port).
  void releaseSerial(const std::string &port);

private:
  struct Job {
    std::vector<std::string> lines;
    std::vector<DmxDevice> dmx;
    std::vector<PjlinkProjector> projectors;
  };
  void worker();
  void exec(const std::string &line, const Job &job);
  void log(const std::string &s);
  std::string pjlink(const std::string &host, const std::string &password, const std::string &cmd);
  RtMidiOut *midiPort(const std::string &match);

  std::thread thread_;
  std::mutex mutex_;
  std::condition_variable cv_;
  std::deque<Job> jobs_;
  std::atomic<bool> running_{true};
  std::vector<std::string> log_;
  std::map<std::string, std::string> status_;
  std::map<std::string, std::unique_ptr<RtMidiOut>> midi_;
  // Serial ports of "serial" commands stay open: opening one resets an Arduino
  // (DTR), so a port opened per line rebooted the board and lost the bytes.
  struct OpenSerial;
  std::map<std::string, std::unique_ptr<OpenSerial>> serial_; // under serialMutex_
  std::mutex serialMutex_;
  std::map<std::pair<int, int>, DmxUniverse> dmx_; // (protocol, universe), under mutex_
  uint64_t dmxVersion_ = 0;
  std::array<uint8_t, 16> cid_{};
};

} // namespace lumen
