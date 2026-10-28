// Runs an ISF shader on the GPU (main GL context): uniforms, image inputs,
// multiple passes and persistent buffers. Textures in and out are GL-native
// (row 0 = bottom), as ISF expects.
#pragma once

#include "core/isf.h"
#include "render/gpu.h"

#include <map>
#include <memory>
#include <string>

namespace lumen {

class IsfRenderer {
public:
  bool load(const std::string &path, std::string *error = nullptr);
  bool loadSource(const std::string &source, std::string *error = nullptr);
  bool valid() const { return shader_.valid(); }
  // Loads `path` the first time, and again when the file changes on disk
  // (checked about once a second): edit the .fs and see it live. A version
  // that does not compile keeps the last good one. Returns valid().
  bool sync(const std::string &path);
  const std::string &error() const { return error_; }
  const IsfInfo &info() const { return info_; }
  const std::string &path() const { return path_; }

  // Renders at w x h. `images`: textures for image inputs by name
  // (inputImage, startImage...). Returns the result (valid until the next call).
  const Texture *render(int w, int h, double time, const std::map<std::string, const Texture *> &images,
                        const IsfValues &values, DynamicMesh &mesh);

private:
  struct Target {
    std::unique_ptr<Framebuffer> fb[2];
    int current = 0; // last written
  };
  IsfInfo info_;
  Shader shader_;
  std::string path_, error_;
  long long mtime_ = 0;
  double checkedAt_ = -10;
  std::map<std::string, Target> targets_;
  Framebuffer out_;
  std::map<std::string, std::unique_ptr<Texture>> imported_;
  int frame_ = 0;
  double lastTime_ = -1;
};

} // namespace lumen
