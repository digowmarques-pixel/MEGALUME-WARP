// Renders polygon masks into an R8 framebuffer. Masks apply in order (Show /
// Hide, optional inverted, opacity and feather of their own, bezier curves).
// Used for layer masks (canvas space) and output masks (projector space).
#pragma once

#include "core/project.h"
#include "render/gpu.h"

#include <vector>

namespace lumen {

class MaskRenderer {
public:
  bool init();
  static bool anyActive(const std::vector<Mask> &masks);
  // Mask points are multiplied by (sx, sy) to get target pixels; feather is in
  // target pixels. `target` is resized to w x h (R8 + stencil). 1 = visible.
  void render(const std::vector<Mask> &masks, float sx, float sy, int w, int h, Framebuffer &target,
              DynamicMesh &mesh);

private:
  Shader fill_, blur_, apply_;
  Framebuffer cover_, tmp_;
};

} // namespace lumen
