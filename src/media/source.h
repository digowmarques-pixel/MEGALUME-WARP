// Runtime media: playback instances, generator shaders and thumbnail cache.
#pragma once

#include "core/project.h"
#include "media/decoder.h"
#include "render/gpu.h"

#include <map>
#include <memory>

namespace lumen {

// Procedural content rendered by shaders (test card, plasma...).
class Generators {
public:
  bool init();
  void render(const MediaItem &m, double time, Framebuffer &target, DynamicMesh &mesh);

private:
  Shader shaders_[int(GeneratorKind::COUNT)];
};

// Caches decoded stills (images) and thumbnails, keyed by media id.
class MediaCache {
public:
  const Texture *still(const MediaItem &m);
  const Texture *thumbnail(const MediaItem &m, Generators &gen, DynamicMesh &mesh);
  const MediaInfo *info(const MediaItem &m);
  void invalidate(int mediaId);
  void clear();

private:
  struct Entry {
    std::string path;
    std::unique_ptr<Texture> still, thumb;
    std::unique_ptr<Framebuffer> thumbFb;
    MediaInfo info;
    bool probed = false, failed = false;
  };
  Entry &entry(const MediaItem &m);
  std::map<int, Entry> entries_;
};

// One playing instance of a media in a layer.
class Playback {
public:
  Playback(const MediaItem &m, const Cell &cell);
  int mediaId() const { return mediaId_; }

  // Advances the clock and refreshes the texture. Returns the texture to draw.
  const Texture *update(double dt, const MediaItem &m, MediaCache &cache, Generators &gen,
                        DynamicMesh &mesh);
  void restart();
  bool finished();
  double time() const { return clock_; }
  double duration() const;
  float speed = 1;
  bool loop = true;

private:
  int mediaId_;
  MediaType type_;
  double clock_ = 0;
  bool started_ = false;
  std::unique_ptr<VideoDecoder> decoder_;
  VideoFrame frame_;
  Texture videoTex_;
  bool hasFrame_ = false;
  std::unique_ptr<Framebuffer> genFb_;
};

} // namespace lumen
