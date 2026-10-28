#include "control/midi.h"

#include <rtmidi/RtMidi.h>

#include <algorithm>
#include <cstdio>

namespace lumen {

bool midiToOsc(const unsigned char *b, size_t n, OscMessage &m) {
  if (n < 1) return false;
  int status = b[0] & 0xF0, ch = (b[0] & 0x0F) + 1;
  std::string c = std::to_string(ch);
  m.args.clear();
  switch (status) {
  case 0x90:
  case 0x80:
    if (n < 3) return false;
    m.address = "/midi/note/" + c + "/" + std::to_string(b[1]);
    m.args.emplace_back(int32_t(status == 0x90 ? b[2] : 0));
    return true;
  case 0xB0:
    if (n < 3) return false;
    m.address = "/midi/cc/" + c + "/" + std::to_string(b[1]);
    m.args.emplace_back(int32_t(b[2]));
    return true;
  case 0xC0:
    if (n < 2) return false;
    m.address = "/midi/pc/" + c;
    m.args.emplace_back(int32_t(b[1]));
    return true;
  case 0xE0:
    if (n < 3) return false;
    m.address = "/midi/bend/" + c;
    m.args.emplace_back(int32_t(b[1] | (b[2] << 7)));
    return true;
  default: return false; // clock, sysex, aftertouch...
  }
}

struct MidiInputs::Port {
  std::unique_ptr<RtMidiIn> in;
  std::string name;
  MidiInputs *owner = nullptr;
};

static void onMidi(double, std::vector<unsigned char> *msg, void *user) {
  auto *port = static_cast<MidiInputs::Port *>(user);
  OscMessage m;
  if (msg && !msg->empty() && ((*msg)[0] == 0xF1 || (*msg)[0] == 0xF0)) {
    port->owner->timecodeFromCallback(msg->data(), msg->size());
    return;
  }
  if (!msg || !midiToOsc(msg->data(), msg->size(), m)) return;
  m.sender = port->name;
  port->owner->pushFromCallback(std::move(m));
}

MidiInputs::MidiInputs() = default;
MidiInputs::~MidiInputs() { stop(); }

bool MidiInputs::available() const {
  std::vector<RtMidi::Api> apis;
  RtMidi::getCompiledApi(apis);
  return std::any_of(apis.begin(), apis.end(), [](RtMidi::Api a) { return a != RtMidi::RTMIDI_DUMMY; });
}

void MidiInputs::pushFromCallback(OscMessage m) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (queue_.size() < 4096) queue_.push_back(std::move(m));
}

void MidiInputs::timecodeFromCallback(const unsigned char *b, size_t n) {
  std::lock_guard<std::mutex> lock(mutex_);
  Timecode tc;
  if (mtc_.feed(b, n, tc)) {
    mtcLast_ = tc;
    mtcHave_ = true;
    mtcAt_ = std::chrono::steady_clock::now();
  }
}

bool MidiInputs::timecode(Timecode &tc, double &age) const {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!mtcHave_) return false;
  tc = mtcLast_;
  age = std::chrono::duration<double>(std::chrono::steady_clock::now() - mtcAt_).count();
  return true;
}

struct MidiOutPort::Impl {
  std::unique_ptr<RtMidiOut> out;
};

MidiOutPort::MidiOutPort() : impl_(std::make_unique<Impl>()) {}
MidiOutPort::~MidiOutPort() { close(); }

bool MidiOutPort::open(const std::string &match) {
  close();
  try {
    impl_->out = std::make_unique<RtMidiOut>();
    unsigned n = impl_->out->getPortCount();
    for (unsigned i = 0; i < n; ++i) {
      std::string pn = impl_->out->getPortName(i);
      if (match.empty() || pn.find(match) != std::string::npos) {
        impl_->out->openPort(i, "Lumen MTC");
        name_ = pn;
        open_ = true;
        return true;
      }
    }
  } catch (...) {
  }
  impl_->out.reset();
  return false;
}

void MidiOutPort::close() {
  if (impl_->out) impl_->out->closePort();
  impl_->out.reset();
  open_ = false;
}

void MidiOutPort::send(const std::vector<uint8_t> &bytes) {
  if (!open_ || !impl_->out) return;
  std::vector<unsigned char> m(bytes.begin(), bytes.end());
  try {
    impl_->out->sendMessage(&m);
  } catch (...) {
  }
}

void MidiInputs::start() {
  running_ = true;
  failed_ = false;
  rescan();
}

void MidiInputs::stop() {
  running_ = false;
  for (auto &p : ports_)
    if (p->in) p->in->closePort();
  ports_.clear();
}

void MidiInputs::rescan() {
  if (!running_ || failed_) return;
  try {
    RtMidiIn probe;
    unsigned n = probe.getPortCount();
    std::vector<std::string> names;
    for (unsigned i = 0; i < n; ++i) names.push_back(probe.getPortName(i));
    // Close ports that disappeared.
    ports_.erase(std::remove_if(ports_.begin(), ports_.end(),
                                [&](auto &p) { return std::find(names.begin(), names.end(), p->name) == names.end(); }),
                 ports_.end());
    for (unsigned i = 0; i < n; ++i) {
      if (std::any_of(ports_.begin(), ports_.end(), [&](auto &p) { return p->name == names[i]; })) continue;
      auto port = std::make_unique<Port>();
      port->name = names[i];
      port->owner = this;
      port->in = std::make_unique<RtMidiIn>();
      port->in->openPort(i, "Lumen");
      // Sysex and timing pass for MIDI Time Code (full frame / quarter frames).
      port->in->ignoreTypes(false, false, true);
      port->in->setCallback(onMidi, port.get());
      std::printf("[midi] opened %s\n", names[i].c_str());
      ports_.push_back(std::move(port));
    }
  } catch (const RtMidiError &e) {
    std::fprintf(stderr, "[midi] desativado: %s\n", e.getMessage().c_str());
    failed_ = true;
  }
}

void MidiInputs::drain(std::vector<OscMessage> &out) {
  std::lock_guard<std::mutex> lock(mutex_);
  for (auto &m : queue_) out.push_back(std::move(m));
  queue_.clear();
}

std::vector<std::string> MidiInputs::ports() const {
  std::vector<std::string> out;
  for (auto &p : ports_) out.push_back(p->name);
  return out;
}

} // namespace lumen
