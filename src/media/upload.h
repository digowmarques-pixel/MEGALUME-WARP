// Uploads decoded frames to the GPU and converts them to RGBA there
// (YUV / NV12 / HAP), so the CPU never does colour conversion.
#pragma once

#include "media/decoder.h"
#include "render/gpu.h"

namespace lumen {

class FrameUploader {
public:
  // Returns an RGBA texture with row 0 = top of the image.
  const Texture *upload(const VideoFrame &f, DynamicMesh &mesh);
  // Whether HAP (S3TC) textures can be uploaded compressed on this GPU.
  static bool supportsS3tc();

private:
  Texture planes_[3];
  Framebuffer out_;
};

} // namespace lumen
