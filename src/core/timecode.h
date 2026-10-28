// SMPTE timecode: MIDI Time Code (MTC) and Linear Timecode (LTC, the audio
// signal), decoding and generation. Pure code, no devices.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace lumen {

struct Timecode {
  int h = 0, m = 0, s = 0, f = 0;
  double fps = 25;   // 24, 25, 29.97 (drop frame) or 30
  bool dropFrame = false;
  double seconds() const;
  static Timecode fromSeconds(double t, double fps);
  std::string str() const; // "01:02:03:04"
};

// Parses "01:02:03:04" (or "01:02:03.5") into seconds; false on bad input.
bool parseTimecode(const std::string &text, double fps, double &seconds);

class MtcDecoder {
public:
  // Feeds one MIDI message (quarter frame F1 xx, or full frame sysex).
  // Returns true when it produced a new time.
  bool feed(const uint8_t *msg, size_t size, Timecode &out);

private:
  int pieces_[8] = {};
  int received_ = 0;
  int lastPiece_ = -1;
};

// The 8 quarter-frame messages of a frame, and the full-frame sysex.
std::vector<uint8_t> mtcQuarterFrame(const Timecode &tc, int piece);
std::vector<uint8_t> mtcFullFrame(const Timecode &tc);

class LtcDecoder {
public:
  explicit LtcDecoder(int sampleRate = 48000) : rate_(sampleRate) {}
  // Mono samples (stride = channels to skip interleaved audio).
  void feed(const float *samples, size_t count, size_t stride = 1);
  bool latest(Timecode &out) const { if (!valid_) return false; out = last_; return true; }
  uint64_t frames() const { return frames_; }

private:
  void bit(int b);
  int rate_;
  float prev_ = 0, level_ = 0;
  double since_ = 0, bitLen_ = 22, halfLen_ = 0;
  bool half_ = false, sign_ = false;
  uint8_t bits_[80] = {};
  int count_ = 0;
  Timecode last_;
  bool valid_ = false;
  uint64_t frames_ = 0;
};

// One frame of LTC audio (biphase mark), appended to `out` (mono, -amp..amp).
class LtcEncoder {
public:
  explicit LtcEncoder(int sampleRate = 48000) : rate_(sampleRate) {}
  void encode(const Timecode &tc, std::vector<float> &out, float amplitude = 0.5f);

private:
  int rate_;
  double clock_ = 0, sample_ = 0; // bit boundaries and samples written, in samples
  float level_ = 1;
};

} // namespace lumen
