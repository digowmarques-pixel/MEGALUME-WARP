// MIDI input (RtMidi: WinMM on Windows, ALSA on Linux). Incoming MIDI is
// translated to OSC-style messages so it goes through the same mappings and
// "learn" as OSC:
//   /midi/note/<channel>/<note>   velocity (note off = 0)
//   /midi/cc/<channel>/<number>   value 0..127
//   /midi/pc/<channel>            program
//   /midi/bend/<channel>          0..16383
#pragma once

#include "control/osc.h"

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

private:
  std::vector<std::unique_ptr<Port>> ports_;
  mutable std::mutex mutex_;
  std::vector<OscMessage> queue_;
  bool running_ = false;
  bool failed_ = false; // backend unavailable (e.g. no ALSA sequencer): stop retrying
};

} // namespace lumen
