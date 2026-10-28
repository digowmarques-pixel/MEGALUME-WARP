// FFmpeg-based decoding. Video is decoded on a background thread; frames keep
// their native layout (YUV planes, NV12, compressed HAP) so colour conversion
// happens on the GPU. The render thread picks the frame matching its clock.
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

enum class FrameLayout {
  RGBA,    // plane 0: packed RGBA8
  Planar,  // planes 0,1,2: Y, U, V (8 or 16 bit)
  NV12,    // plane 0: Y, plane 1: interleaved UV (8 or 16 bit)
  DXT1,    // plane 0: BC1 blocks (HAP)
  DXT5,    // plane 0: BC3 blocks (HAP Alpha)
  YCoCgDXT5, // plane 0: BC3 blocks holding scaled YCoCg (HAP Q)
  YCoCgDXT5A, // plane 0: YCoCg BC3, plane 1: BC4 alpha (HAP Q Alpha)
};

struct VideoFrame {
  FrameLayout layout = FrameLayout::RGBA;
  int width = 0, height = 0;
  std::vector<uint8_t> plane[3];
  int stride[3] = {0, 0, 0}; // bytes per row (uncompressed layouts)
  int chromaShiftX = 1, chromaShiftY = 1;
  bool wide = false;      // 16-bit samples
  float valueScale = 1;   // multiply sampled values (10-bit stored in 16-bit LSBs)
  int matrix = 1;         // 0: BT.601, 1: BT.709, 2: BT.2020
  bool fullRange = false;
  double pts = 0;         // seconds, monotonic across loops
  std::vector<uint8_t> &rgba() { return plane[0]; }
  const std::vector<uint8_t> &rgba() const { return plane[0]; }
};

struct MediaInfo {
  int width = 0, height = 0;
  double duration = 0; // seconds (0 when unknown / live)
  double fps = 0;
  bool hasVideo = false;
  bool hasAudio = false;
  std::string codec;
  std::string hwaccel; // name of the hardware decoder in use, if any
};

// Where the media comes from. Files only need `url`; live sources (cameras,
// capture cards, network streams, lavfi test sources) set `format`.
struct MediaSource {
  std::string url;    // path, "video=Camera" (dshow), "/dev/video0" (v4l2), rtsp://...
  std::string format; // "" = auto, "dshow", "v4l2", "avfoundation", "lavfi"...
  std::string options; // "key=value:key=value" (video_size, framerate, rtsp_transport...)
  bool live = false;
};

struct DecoderSettings {
  bool hardware = true; // try D3D11VA / DXVA2 / VAAPI / VideoToolbox
  bool nativeHap = true; // upload HAP textures compressed (no CPU decode)
};

// Probes a file without starting a decoder.
bool probeMedia(const MediaSource &src, MediaInfo &info, std::string *error = nullptr);
inline bool probeMedia(const std::string &path, MediaInfo &info, std::string *error = nullptr) {
  return probeMedia(MediaSource{path, "", "", false}, info, error);
}

// Decodes the first frame of a file as RGBA (images, posters, thumbnails).
bool decodeStill(const std::string &path, VideoFrame &out, std::string *error = nullptr);

// Capture devices visible to FFmpeg (dshow on Windows, v4l2 on Linux).
struct CaptureDevice {
  std::string name, url, format;
};
std::vector<CaptureDevice> listCaptureDevices();

class VideoDecoder {
public:
  VideoDecoder() = default;
  ~VideoDecoder();
  VideoDecoder(const VideoDecoder &) = delete;
  VideoDecoder &operator=(const VideoDecoder &) = delete;

  bool open(const MediaSource &src, bool loop, const DecoderSettings &settings,
            std::string *error = nullptr);
  void close();

  // Restart from the beginning (thread-safe).
  void restart();
  void setLoop(bool loop) { loop_ = loop; }

  // Returns true and fills `out` with the newest frame whose pts <= time.
  // If `force` is set and nothing is due yet, returns the earliest queued frame.
  // Live sources always return the newest frame.
  bool fetch(double time, VideoFrame &out, bool force);

  // True once a non-looping file has been fully consumed.
  bool finished();
  const MediaInfo &info() const { return info_; }
  // Loop length used for monotonic timestamps (shared with the audio decoder).
  double loopLength() const { return loopLength_; }
  std::string hwaccel();

private:
  void run();

  MediaSource src_;
  DecoderSettings settings_;
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
  double loopLength_ = 0;
  std::string hwaccel_;
  size_t queueSize_ = 6;
};

} // namespace lumen
