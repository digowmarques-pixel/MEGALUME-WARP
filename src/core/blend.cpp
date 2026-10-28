#include "core/blend.h"

#include <algorithm>
#include <cmath>

namespace lumen {

int BlendLayoutParams::gapX(int i) const {
  int w = std::max(16, projW);
  int v = i < int(gapsX.size()) ? gapsX[size_t(i)] : int(std::lround(std::clamp(overlapX, 0.0f, 0.9f) * float(w)));
  return std::clamp(v, 0, w * 9 / 10);
}

int BlendLayoutParams::gapY(int i) const {
  int h = std::max(16, projH);
  int v = i < int(gapsY.size()) ? gapsY[size_t(i)] : int(std::lround(std::clamp(overlapY, 0.0f, 0.9f) * float(h)));
  return std::clamp(v, 0, h * 9 / 10);
}

BlendLayout computeBlendLayout(const BlendLayoutParams &p) {
  BlendLayout out;
  int cols = std::max(1, p.cols), rows = std::max(1, p.rows);
  int w = std::max(16, p.projW), h = std::max(16, p.projH);
  // Column / row origins: each seam may have its own overlap (whole pixels so
  // neighbouring regions line up exactly).
  std::vector<int> x0(size_t(cols), 0), y0(size_t(rows), 0);
  for (int c = 1; c < cols; ++c) x0[size_t(c)] = x0[size_t(c - 1)] + w - p.gapX(c - 1);
  for (int r = 1; r < rows; ++r) y0[size_t(r)] = y0[size_t(r - 1)] + h - p.gapY(r - 1);
  out.overlapPxX = cols > 1 ? p.gapX(0) : 0;
  out.overlapPxY = rows > 1 ? p.gapY(0) : 0;
  out.canvasW = x0.back() + w;
  out.canvasH = y0.back() + h;
  for (int r = 0; r < rows; ++r)
    for (int c = 0; c < cols; ++c) {
      BlendProjector b;
      b.col = c;
      b.row = r;
      b.srcX = float(x0[size_t(c)]);
      b.srcY = float(y0[size_t(r)]);
      b.srcW = float(w);
      b.srcH = float(h);
      b.left = c > 0 ? float(p.gapX(c - 1)) / float(w) : 0;
      b.right = c + 1 < cols ? float(p.gapX(c)) / float(w) : 0;
      b.top = r > 0 ? float(p.gapY(r - 1)) / float(h) : 0;
      b.bottom = r + 1 < rows ? float(p.gapY(r)) / float(h) : 0;
      out.projectors.push_back(b);
    }
  return out;
}

float blendCurve(float t, float power) {
  t = std::clamp(t, 0.0f, 1.0f);
  return t < 0.5f ? 0.5f * std::pow(2.0f * t, power) : 1.0f - 0.5f * std::pow(2.0f * (1.0f - t), power);
}

} // namespace lumen
