// FFmpeg-based decoding. Video is decoded on a background thread into RGBA
// frames; the render thread picks the frame matching its playback clock.
#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace lumen {

struct VideoFrame {
  std::vector<uint8_t> rgba;
  int width = 0, height = 0;
  double pts = 0; // seconds, monotonic across loops
};

struct MediaInfo {
  int width = 0, height = 0;
  double duration = 0; // seconds (0 when unknown)
  double fps = 0;
  bool hasVideo = false;
  bool hasAudio = false;
  std::string codec;
};

// Probes a file without starting a decoder.
bool probeMedia(const std::string &path, MediaInfo &info, std::string *error = nullptr);

// Decodes the first frame of a file (images, or a poster frame for videos).
bool decodeStill(const std::string &path, VideoFrame &out, std::string *error = nullptr);

class VideoDecoder {
public:
  VideoDecoder() = default;
  ~VideoDecoder();
  VideoDecoder(const VideoDecoder &) = delete;
  VideoDecoder &operator=(const VideoDecoder &) = delete;

  bool open(const std::string &path, bool loop, std::string *error = nullptr);
  void close();

  // Restart from the beginning (thread-safe).
  void restart();
  void setLoop(bool loop) { loop_ = loop; }

  // Returns true and fills `out` with the newest frame whose pts <= time.
  // If `force` is set and nothing is due yet, returns the earliest queued frame.
  bool fetch(double time, VideoFrame &out, bool force);

  // True once a non-looping file has been fully consumed.
  bool finished();
  const MediaInfo &info() const { return info_; }

private:
  void run();

  std::string path_;
  MediaInfo info_;
  std::thread thread_;
  std::mutex mutex_;
  std::condition_variable cv_;
  std::deque<VideoFrame> queue_;
  std::vector<VideoFrame> pool_; // recycled buffers
  std::atomic<bool> running_{false};
  std::atomic<bool> restart_{false};
  std::atomic<bool> loop_{true};
  bool eof_ = false;
  static constexpr size_t kQueueSize = 6;
};

} // namespace lumen
