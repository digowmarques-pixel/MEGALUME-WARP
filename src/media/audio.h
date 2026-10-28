// Audio output (miniaudio: WASAPI on Windows, PulseAudio/ALSA on Linux) and
// FFmpeg audio decoding. Every playing video with sound owns an AudioVoice;
// the device callback mixes all voices. The audio clock drives the video
// clock of the same playback so picture and sound stay in sync.
#pragma once

#include "core/timecode.h"

#include <chrono>

#include "media/decoder.h"

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace lumen {

constexpr int kAudioRate = 48000;
constexpr int kAudioChannels = 2;

class AudioVoice {
public:
  explicit AudioVoice(size_t capacityFrames = kAudioRate);
  // Producer side (decoder thread). Returns frames accepted.
  size_t write(const float *interleaved, size_t frames);
  size_t space();
  void reset();               // drop buffered audio and the played counter
  // Consumer side (audio callback): mixes into `out` with gain ramp.
  void mixInto(float *out, size_t frames);

  std::atomic<float> gain{1.0f};
  std::atomic<bool> active{true}; // false: consumes nothing, outputs nothing
  double playedSeconds() const { return double(played_.load()) / kAudioRate; }
  float peak() const { return peak_.load(); }
  bool starving() const { return starving_.load(); }

private:
  std::mutex mutex_;
  std::vector<float> ring_;
  size_t capacity_, readPos_ = 0, count_ = 0;
  float currentGain_ = 0;
  std::atomic<uint64_t> played_{0};
  std::atomic<float> peak_{0};
  std::atomic<bool> starving_{false};
};

class AudioEngine {
public:
  AudioEngine();
  ~AudioEngine();
  // deviceName "" = system default. Falls back to a silent clock-only device.
  bool start(const std::string &deviceName);
  void stop();
  bool running() const { return running_; }
  bool silent() const { return silent_; } // no real device: null backend
  const std::string &deviceName() const { return deviceName_; }
  const std::string &error() const { return error_; }
  static std::vector<std::string> devices();

  std::shared_ptr<AudioVoice> createVoice();
  void removeVoice(const std::shared_ptr<AudioVoice> &v);
  std::atomic<float> master{1.0f};
  float peak(int ch) const { return peak_[ch].load(); }

  void callback(float *out, size_t frames); // called by the device thread

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
  std::mutex voicesMutex_;
  std::vector<std::shared_ptr<AudioVoice>> voices_;
  std::atomic<float> peak_[2];
  bool running_ = false, silent_ = false;
  std::string deviceName_, error_;
};

// Decodes the audio track of a file into an AudioVoice on its own thread.
// Loops with exactly the same loop length as the video decoder.
class AudioDecoder {
public:
  ~AudioDecoder();
  bool open(const std::string &path, std::shared_ptr<AudioVoice> voice, bool loop,
            double loopLength, std::string *error = nullptr);
  void close();
  void restart();
  void setLoop(bool loop) { loop_ = loop; }
  bool finished() const { return finished_; }

private:
  void run();
  std::string path_;
  std::shared_ptr<AudioVoice> voice_;
  std::thread thread_;
  std::atomic<bool> running_{false}, restart_{false}, loop_{true}, finished_{false};
  double loopLength_ = 0;
};

// Reads LTC (SMPTE timecode as audio) from an input of the sound card.
class LtcCapture {
public:
  LtcCapture();
  ~LtcCapture();
  bool start(const std::string &deviceName, int channel, std::string *error = nullptr);
  void stop();
  bool running() const { return running_; }
  // Last decoded frame and how long ago it arrived (seconds).
  bool latest(Timecode &tc, double &age) const;
  float level() const { return level_.load(); }
  static std::vector<std::string> devices(); // capture devices
  void callback(const float *in, size_t frames, int channels); // device thread

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
  LtcDecoder decoder_{kAudioRate};
  mutable std::mutex mutex_;
  Timecode last_;
  uint64_t lastFrames_ = 0;
  std::chrono::steady_clock::time_point at_{};
  bool have_ = false, running_ = false;
  int channel_ = 0;
  std::atomic<float> level_{0};
};

} // namespace lumen
