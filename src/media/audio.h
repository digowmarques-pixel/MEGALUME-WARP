// Audio output (miniaudio: WASAPI on Windows, PulseAudio/ALSA on Linux) and
// FFmpeg audio decoding. Every playing video with sound owns an AudioVoice
// that keeps the file's own channels (up to 7.1); the device callback mixes
// all voices into the device's channels through each voice's routing matrix.
// The audio clock drives the video clock of the same playback so picture and
// sound stay in sync.
#pragma once

#include "core/audio_route.h"
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

class AudioVoice {
public:
  explicit AudioVoice(size_t capacityFrames = kAudioRate);
  // Channels of the audio written to this voice (decoder thread, before
  // writing). Drops buffered audio when it changes.
  void setChannels(int channels);
  int channels() const { return channels_.load(); }
  // Producer side (decoder thread): `channels()` interleaved samples per frame.
  // Returns frames accepted.
  size_t write(const float *interleaved, size_t frames);
  size_t space();
  // Drops buffered audio; the played clock restarts at `atSeconds`.
  void reset(double atSeconds = 0);
  // Routing (engine thread, every frame; cheap when nothing changed).
  void setRoute(int firstOutput, const std::vector<AudioPatch> &custom);
  // Consumer side (audio callback): mixes into `out` (`outChannels`
  // interleaved) with a gain ramp.
  void mixInto(float *out, size_t frames, int outChannels);

  std::atomic<float> gain{1.0f};
  std::atomic<bool> active{true}; // false: consumes nothing, outputs nothing
  double playedSeconds() const { return double(played_.load()) / kAudioRate; }
  float peak() const { return peak_.load(); }
  bool starving() const { return starving_.load(); }

private:
  std::mutex mutex_;
  std::vector<float> ring_;
  size_t capacity_, readPos_ = 0, count_ = 0;
  std::atomic<int> channels_{2};
  int firstOutput_ = 0;
  std::vector<AudioPatch> custom_, patches_; // patches_: custom_ resolved for:
  int routeOut_ = -1, routeSrc_ = -1;        // what patches_ was resolved for
  float currentGain_ = 0;
  std::atomic<uint64_t> played_{0};
  std::atomic<float> peak_{0};
  std::atomic<bool> starving_{false};
};

struct AudioDeviceOptions {
  int channels = 0;       // 0 = all the channels of the device
  bool exclusive = false; // WASAPI exclusive mode: lowest latency, Lumen only
  int bufferMs = 10;      // period size
};

class AudioEngine {
public:
  AudioEngine();
  ~AudioEngine();
  // deviceName "" = system default. Falls back to a silent clock-only device.
  bool start(const std::string &deviceName, const AudioDeviceOptions &options = {});
  int channels() const { return channels_; }      // output channels in use
  int latencyMs() const { return latencyMs_; }    // period of the device
  bool exclusive() const { return exclusive_; }   // exclusive mode actually granted
  void stop();
  bool running() const { return running_; }
  bool silent() const { return silent_; } // no real device: null backend
  const std::string &deviceName() const { return deviceName_; }
  const std::string &error() const { return error_; }
  static std::vector<std::string> devices();

  std::shared_ptr<AudioVoice> createVoice();
  void removeVoice(const std::shared_ptr<AudioVoice> &v);
  std::atomic<float> master{1.0f};
  float peak(int ch) const { return ch >= 0 && ch < kMaxOutputChannels ? peak_[ch].load() : 0.0f; }

  void callback(float *out, size_t frames); // called by the device thread

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
  std::mutex voicesMutex_;
  std::vector<std::shared_ptr<AudioVoice>> voices_;
  std::atomic<float> peak_[kMaxOutputChannels];
  int channels_ = 2, latencyMs_ = 0;
  bool running_ = false, silent_ = false, exclusive_ = false;
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
  void restart() { seek(0); }
  // Continues from `time` on the monotonic clock (loops included).
  void seek(double time);
  void setLoop(bool loop) { loop_ = loop; }
  bool finished() const { return finished_; }

private:
  void run();
  std::string path_;
  std::shared_ptr<AudioVoice> voice_;
  std::thread thread_;
  std::atomic<bool> running_{false}, restart_{false}, loop_{true}, finished_{false};
  std::atomic<double> seekTo_{0};
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
