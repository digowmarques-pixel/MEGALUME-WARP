// MIDI input (RtMidi: WinMM on Windows, ALSA on Linux). Incoming MIDI is
// translated to OSC-style messages so it goes through the same mappings and
// "learn" as OSC:
//   /midi/note/<channel>/<note>   velocity (note off = 0)
//   /midi/cc/<channel>/<number>   value 0..127
//   /midi/pc/<channel>            program
//   /midi/bend/<channel>          0..16383
#pragma once

#include "control/osc.h"
#include "core/timecode.h"

#include <chrono>

#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace lumen {

// Converts one raw MIDI message; returns false for messages we ignore.
bool midiToOsc(const unsigned char *bytes, size_t size, OscMessage &out);

class MidiInputs {
public:
  MidiInputs();
  ~MidiInputs();
  void start();  // opens every available input port
  void stop();
  void rescan(); // picks up devices plugged in later
  void drain(std::vector<OscMessage> &out);
  std::vector<std::string> ports() const;
  bool available() const; // false when built without a MIDI backend

  struct Port;                          // one open RtMidi input
  void pushFromCallback(OscMessage m);  // called on RtMidi's thread
  void timecodeFromCallback(const unsigned char *bytes, size_t size);
  // Last MIDI Time Code received and its age in seconds.
  bool timecode(Timecode &tc, double &age) const;

private:
  std::vector<std::unique_ptr<Port>> ports_;
  mutable std::mutex mutex_;
  std::vector<OscMessage> queue_;
  MtcDecoder mtc_;
  Timecode mtcLast_;
  bool mtcHave_ = false;
  std::chrono::steady_clock::time_point mtcAt_{};
  bool running_ = false;
  bool failed_ = false; // backend unavailable (e.g. no ALSA sequencer): stop retrying
};

// One MIDI output port (MIDI Time Code generation).
class MidiOutPort {
public:
  MidiOutPort();
  ~MidiOutPort();
  bool open(const std::string &nameMatch); // "" = first port
  void close();
  bool isOpen() const { return open_; }
  const std::string &name() const { return name_; }
  void send(const std::vector<uint8_t> &bytes);

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
  bool open_ = false;
  std::string name_;
};

} // namespace lumen
