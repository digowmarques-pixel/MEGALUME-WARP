// Asynchronous pixel transfers through rings of pixel buffer objects (PBO).
//
// Upload: the frame is copied into a mapped buffer by several threads and the
// driver moves it to the texture by DMA, so the render thread does not wait
// for big frames (8K, several 4K at once). A fence per buffer makes sure a
// buffer is reused only after the GPU has read it.
//
// Readback: glReadPixels into a buffer returns immediately; the pixels are
// collected one or two frames later (NDI sending without stalling the GPU).
#pragma once

#include "gl/gl.h"
#include "render/gpu.h"

#include <cstddef>
#include <vector>

namespace lumen {

struct StreamPlane {
  const void *data = nullptr;
  int width = 0, height = 0;
  int stride = 0;            // bytes per source row (uncompressed)
  GLenum internalFormat = GL_RGBA8;
  size_t compressedBytes = 0; // > 0: block-compressed data of this size
  Texture *texture = nullptr;
};

class StreamUploader {
public:
  StreamUploader() = default;
  ~StreamUploader();
  StreamUploader(const StreamUploader &) = delete;
  StreamUploader &operator=(const StreamUploader &) = delete;

  // Uploads the planes into their textures. Returns false when the PBO path is
  // unavailable (disabled, mapping failed); the caller then uploads directly.
  bool upload(const StreamPlane *planes, int count);

  // Global switch (Project → asynchronous upload) and statistics.
  static void setEnabled(bool on);
  static bool enabled();
  // Bytes sent through PBOs and time the render thread spent on it, since the
  // last call (for the status bar / diagnostics).
  static void takeStats(double &megabytes, double &milliseconds);

private:
  static constexpr int kSlots = 3;
  struct Slot {
    GLuint pbo = 0;
    size_t size = 0;
    GLsync fence = nullptr;
  };
  Slot slots_[kSlots];
  int next_ = 0;
};

class AsyncReadback {
public:
  AsyncReadback() = default;
  ~AsyncReadback();
  AsyncReadback(const AsyncReadback &) = delete;
  AsyncReadback &operator=(const AsyncReadback &) = delete;

  // Starts reading the framebuffer (RGBA8). Cheap: no wait.
  void request(const Framebuffer &fb);
  // The newest finished read, rows top-down (flipped from GL's bottom-up).
  // Returns false when nothing new is ready yet.
  bool collect(std::vector<unsigned char> &topDown, int &w, int &h);

private:
  static constexpr int kSlots = 3;
  struct Slot {
    GLuint pbo = 0;
    size_t size = 0;
    GLsync fence = nullptr;
    int w = 0, h = 0;
    long long serial = 0;
  };
  Slot slots_[kSlots];
  int next_ = 0;
  long long serial_ = 0, collected_ = 0;
};

} // namespace lumen
