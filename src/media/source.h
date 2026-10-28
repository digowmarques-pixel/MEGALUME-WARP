// Runtime media: playback instances, generator shaders, live inputs and the
// thumbnail cache.
#pragma once

#include "core/project.h"
#include "media/audio.h"
#include "media/decoder.h"
#include "media/text.h"
#include "media/upload.h"
#include "render/gpu.h"
#include "render/isf_renderer.h"

#include <functional>
#include <map>
#include <memory>

namespace lumen {

class SpoutIn;
class NdiReceiver;

// Procedural content rendered by shaders (test card, plasma...).
class Generators {
public:
  bool init();
  void render(const MediaItem &m, double time, Framebuffer &target, DynamicMesh &mesh);
  // Copies a framebuffer texture (bottom-up) into `target` as an image (row 0 = top).
  void copyFlipped(const Texture &src, Framebuffer &target, DynamicMesh &mesh);
  // Cross-dissolve of two images (row 0 = top) at `t` (0 = a, 1 = b) into
  // `target`, sized like b. A real mix: the middle of a fade between two
  // opaque images is as bright as both, unlike two layers drawn one over the other.
  void crossfade(const Texture &a, const Texture &b, float t, Framebuffer &target, DynamicMesh &mesh);

private:
  Shader shaders_[int(GeneratorKind::COUNT)];
  Shader copy_, mix_;
};

// A camera / capture card / stream / Spout sender. Shared by every layer that
// shows it, because a device can only be opened once.
class LiveInput {
public:
  LiveInput(const MediaItem &m, const DecoderSettings &s);
  ~LiveInput();
  const Texture *update(uint64_t frame, DynamicMesh &mesh);
  std::string status() const;

private:
  MediaType type_;
  std::string url_;
  VideoDecoder decoder_;
  VideoFrame frame_;
  FrameUploader uploader_;
  const Texture *tex_ = nullptr;
  uint64_t lastFrame_ = ~0ull;
  std::unique_ptr<SpoutIn> spout_;
  Texture wrapped_; // non-owning view of the Spout texture
  std::unique_ptr<NdiReceiver> ndi_;
  Texture ndiTex_;
  std::vector<uint8_t> ndiFrame_;
  bool opened_ = false;
};

// Everything a playback needs from the engine.
struct MediaContext {
  class MediaCache &cache;
  Generators &generators;
  DynamicMesh &mesh;
  AudioEngine *audio;
  DecoderSettings decoder;
  uint64_t frame;
  // Composed texture of a canvas (video routing); null if unknown.
  std::function<const Texture *(int canvasId)> canvasTexture;
};

// Caches decoded stills (images), thumbnails and live inputs, keyed by media id.
class MediaCache {
public:
  const Texture *still(const MediaItem &m);
  const Texture *thumbnail(const MediaItem &m, Generators &gen, DynamicMesh &mesh);
  const MediaInfo *info(const MediaItem &m);
  std::shared_ptr<LiveInput> live(const MediaItem &m, const DecoderSettings &s);
  // Live thumbnails show the current picture when the input is open.
  void setLiveTexture(int mediaId, const Texture *t);
  void invalidate(int mediaId);
  void clear();
  void pruneLive(); // closes inputs no layer uses any more

private:
  struct Entry {
    std::string path;
    std::unique_ptr<Texture> still, thumb;
    std::unique_ptr<Framebuffer> thumbFb;
    MediaInfo info;
    bool probed = false, failed = false;
    std::shared_ptr<LiveInput> live;
    const Texture *liveTex = nullptr;
    std::unique_ptr<IsfRenderer> isf; // shader thumbnails
    std::unique_ptr<TextRenderer> text;
  };
  Entry &entry(const MediaItem &m);
  std::map<int, Entry> entries_;
};

// One playing instance of a media in a layer.
class Playback {
public:
  Playback(const MediaItem &m, const Cell &cell, MediaContext &ctx);
  ~Playback();
  int mediaId() const { return mediaId_; }

  // Advances the clock and refreshes the texture. Returns the texture to draw.
  const Texture *update(double dt, const MediaItem &m, MediaContext &ctx);
  void restart();
  // Jumps to `time` (seconds on the playback clock, loops included).
  void seek(double time);
  bool finished();
  double time() const { return clock_; }
  double duration() const;
  bool hasAudio() const { return voice_ != nullptr; }
  bool audioPacing() const { return audioClock_; } // the sound card drives the picture
  float audioPeak() const { return voice_ ? voice_->peak() : 0.0f; }
  void setGain(float g);
  void setRoute(int firstOutput, const std::vector<AudioPatch> &matrix);
  int audioChannels() const { return voice_ ? voice_->channels() : 0; }
  // Files and generated media can jump in time; live inputs (camera, NDI,
  // Spout, canvas) cannot.
  bool seekable() const {
    return type_ != MediaType::Live && type_ != MediaType::NDI && type_ != MediaType::Spout && type_ != MediaType::Canvas;
  }
  std::string hwaccel() const { return decoder_ ? decoder_->hwaccel() : std::string(); }
  float speed = 1;
  bool loop = true;

private:
  int mediaId_;
  MediaType type_;
  double clock_ = 0;
  bool started_ = false;
  std::unique_ptr<VideoDecoder> decoder_;
  VideoFrame frame_;
  FrameUploader uploader_;
  const Texture *videoTex_ = nullptr;
  bool hasFrame_ = false;
  std::unique_ptr<Framebuffer> genFb_;
  std::unique_ptr<IsfRenderer> isf_;
  std::unique_ptr<TextRenderer> text_;
  std::shared_ptr<LiveInput> live_;
  // Audio: the voice's played-sample counter is the master clock.
  AudioEngine *audio_ = nullptr;
  std::shared_ptr<AudioVoice> voice_;
  std::unique_ptr<AudioDecoder> audioDecoder_;
  bool audioClock_ = false;
  // Why the sound stopped pacing the picture; it comes back in step when the
  // cause goes away (speed back to 1, sound card reopened).
  enum class AudioOff { No, Speed, Stall, Ended };
  AudioOff audioOff_ = AudioOff::No;
  double lastPlayed_ = -1, stallFor_ = 0;
  int audioGen_ = 0;
  void watchAudio(double dt);
};

} // namespace lumen
