#include "core/blend.h"

#include <algorithm>
#include <cmath>

namespace lumen {

BlendLayout computeBlendLayout(const BlendLayoutParams &p) {
  BlendLayout out;
  int cols = std::max(1, p.cols), rows = std::max(1, p.rows);
  int w = std::max(16, p.projW), h = std::max(16, p.projH);
  // Whole pixels so neighbouring regions line up exactly.
  int ox = cols > 1 ? int(std::lround(std::clamp(p.overlapX, 0.0f, 0.9f) * float(w))) : 0;
  int oy = rows > 1 ? int(std::lround(std::clamp(p.overlapY, 0.0f, 0.9f) * float(h))) : 0;
  out.overlapPxX = ox;
  out.overlapPxY = oy;
  out.canvasW = cols * w - (cols - 1) * ox;
  out.canvasH = rows * h - (rows - 1) * oy;
  for (int r = 0; r < rows; ++r)
    for (int c = 0; c < cols; ++c) {
      BlendProjector b;
      b.col = c;
      b.row = r;
      b.srcX = float(c * (w - ox));
      b.srcY = float(r * (h - oy));
      b.srcW = float(w);
      b.srcH = float(h);
      float fx = float(ox) / float(w), fy = float(oy) / float(h);
      b.left = c > 0 ? fx : 0;
      b.right = c + 1 < cols ? fx : 0;
      b.top = r > 0 ? fy : 0;
      b.bottom = r + 1 < rows ? fy : 0;
      out.projectors.push_back(b);
    }
  return out;
}

float blendCurve(float t, float power) {
  t = std::clamp(t, 0.0f, 1.0f);
  return t < 0.5f ? 0.5f * std::pow(2.0f * t, power) : 1.0f - 0.5f * std::pow(2.0f * (1.0f - t), power);
}

} // namespace lumen
