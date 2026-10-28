// Renders the layer stack into the canvas framebuffer (main GL context only).
#pragma once

#include "core/project.h"
#include "render/gpu.h"
#include "render/mask.h"

#include <map>
#include <memory>
#include <vector>

namespace lumen {

struct LayerDraw {
  const Layer *layer = nullptr;
  const Texture *texture = nullptr;
  float opacity = 1; // transition fade, multiplied with the layer opacity
};

// Applies position / scale / rotation of the layer to a slice warp.
Warp transformedWarp(const Layer &layer, const Slice &slice);
Vec2 layerPivot(const Layer &layer);

class Compositor {
public:
  bool init();
  // `draws` are ordered back to front, all layers of canvas `canvasId`.
  void render(const Project &p, int canvasId, const std::vector<LayerDraw> &draws, DynamicMesh &mesh,
              float masterOpacity);
  Framebuffer &canvas() { return canvas_; }
  void releaseLayer(int layerId) { maskFbs_.erase(layerId); }

private:
  void renderMask(const Layer &l, int w, int h, DynamicMesh &mesh);

  Shader layerShader_;
  Framebuffer canvas_;
  MaskRenderer masks_;
  std::map<int, std::unique_ptr<Framebuffer>> maskFbs_;
};

} // namespace lumen
