// Multi-projector edge blending: layout of a grid of overlapping projectors.
#pragma once

#include <vector>

namespace lumen {

struct BlendLayoutParams {
  int cols = 2, rows = 1;          // projectors side by side / stacked
  int projW = 1920, projH = 1080;  // native resolution of each projector
  float overlapX = 0.15f;          // fraction of the projector width shared with the neighbour
  float overlapY = 0.15f;
};

struct BlendProjector {
  int col = 0, row = 0;
  float srcX = 0, srcY = 0, srcW = 0, srcH = 0;     // canvas region (px)
  float left = 0, right = 0, top = 0, bottom = 0;   // soft edge widths (fraction of output)
};

struct BlendLayout {
  int canvasW = 0, canvasH = 0;
  int overlapPxX = 0, overlapPxY = 0;
  std::vector<BlendProjector> projectors; // row-major
};

BlendLayout computeBlendLayout(const BlendLayoutParams &p);

// Edge-blend curve used by the output shader (CPU copy for tests): weight of a
// projector at normalised position t (0 = outer border of the overlap, 1 = inner).
// c(t) + c(1 - t) == 1, so two overlapping projectors always sum to full light.
float blendCurve(float t, float power);

} // namespace lumen
