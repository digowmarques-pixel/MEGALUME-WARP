#include "core/blob.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <numeric>

namespace lumen {

void BlobTracker::setBackground(const uint8_t *luma, int w, int h) {
  background_.assign(luma, luma + size_t(w) * size_t(h));
  bgW_ = w;
  bgH_ = h;
}

const std::vector<Blob> &BlobTracker::process(const uint8_t *luma, int w, int h, const BlobParams &p) {
  w_ = w;
  h_ = h;
  size_t n = size_t(w) * size_t(h);
  mask_.assign(n, 0);
  bool bg = !background_.empty() && bgW_ == w && bgH_ == h;
  int thr = int(std::clamp(p.threshold, 0.0f, 1.0f) * 255.0f);
  for (size_t i = 0; i < n; ++i) {
    bool on = bg ? std::abs(int(luma[i]) - int(background_[i])) > thr
                 : (p.invert ? luma[i] < thr : luma[i] > thr);
    mask_[i] = on ? 255 : 0;
  }

  // Connected components (4-neighbours) with union-find.
  labels_.assign(n, -1);
  std::vector<int> parent;
  auto find = [&](int a) {
    while (parent[size_t(a)] != a) a = parent[size_t(a)] = parent[size_t(parent[size_t(a)])];
    return a;
  };
  for (int y = 0; y < h; ++y)
    for (int x = 0; x < w; ++x) {
      size_t i = size_t(y) * size_t(w) + size_t(x);
      if (!mask_[i]) continue;
      int left = x > 0 ? labels_[i - 1] : -1, up = y > 0 ? labels_[i - size_t(w)] : -1;
      if (left < 0 && up < 0) {
        labels_[i] = int(parent.size());
        parent.push_back(int(parent.size()));
      } else if (left >= 0 && up >= 0) {
        int a = find(left), b = find(up);
        labels_[i] = std::min(a, b);
        parent[size_t(std::max(a, b))] = std::min(a, b);
      } else {
        labels_[i] = left >= 0 ? left : up;
      }
    }
  struct Acc { double sx = 0, sy = 0; int count = 0, x0 = 1 << 30, y0 = 1 << 30, x1 = -1, y1 = -1; };
  std::vector<Acc> acc(parent.size());
  for (int y = 0; y < h; ++y)
    for (int x = 0; x < w; ++x) {
      int l = labels_[size_t(y) * size_t(w) + size_t(x)];
      if (l < 0) continue;
      Acc &a = acc[size_t(find(l))];
      a.sx += x; a.sy += y; ++a.count;
      a.x0 = std::min(a.x0, x); a.y0 = std::min(a.y0, y);
      a.x1 = std::max(a.x1, x); a.y1 = std::max(a.y1, y);
    }
  std::vector<Blob> found;
  float total = float(std::max<size_t>(1, n));
  for (auto &a : acc) {
    if (a.count == 0) continue;
    float area = float(a.count) / total;
    if (area < p.minArea) continue;
    Blob b;
    b.x = float((a.sx / a.count + 0.5) / w);
    b.y = float((a.sy / a.count + 0.5) / h);
    b.w = float(a.x1 - a.x0 + 1) / float(w);
    b.h = float(a.y1 - a.y0 + 1) / float(h);
    b.area = area;
    found.push_back(b);
  }
  // Merge close blobs (a person seen as several pieces), biggest first.
  std::sort(found.begin(), found.end(), [](const Blob &a, const Blob &b) { return a.area > b.area; });
  if (p.mergeDistance > 0) {
    std::vector<Blob> merged;
    for (auto &b : found) {
      Blob *into = nullptr;
      for (auto &m : merged) {
        // Distance between the boxes (0 when they touch or overlap).
        float dx = std::max(0.0f, std::fabs(b.x - m.x) - (b.w + m.w) * 0.5f);
        float dy = std::max(0.0f, std::fabs(b.y - m.y) - (b.h + m.h) * 0.5f);
        if (std::sqrt(dx * dx + dy * dy) < p.mergeDistance) { into = &m; break; }
      }
      if (!into) { merged.push_back(b); continue; }
      float x0 = std::min(into->x - into->w / 2, b.x - b.w / 2), x1 = std::max(into->x + into->w / 2, b.x + b.w / 2);
      float y0 = std::min(into->y - into->h / 2, b.y - b.h / 2), y1 = std::max(into->y + into->h / 2, b.y + b.h / 2);
      float a = into->area + b.area;
      into->x = (into->x * into->area + b.x * b.area) / a; // area-weighted centre
      into->y = (into->y * into->area + b.y * b.area) / a;
      into->w = x1 - x0;
      into->h = y1 - y0;
      into->area = a;
    }
    found = std::move(merged);
  }
  found.erase(std::remove_if(found.begin(), found.end(), [&](const Blob &b) { return b.area > p.maxArea; }), found.end());
  if (int(found.size()) > p.maxBlobs) found.resize(size_t(std::max(0, p.maxBlobs)));

  // Tracking: each previous blob takes the closest new one (greedy).
  std::vector<Blob> next;
  std::vector<bool> used(found.size(), false);
  float k = std::clamp(p.smoothing, 0.0f, 0.98f);
  for (auto &old : blobs_) {
    int best = -1;
    float bestD = 0.25f;
    for (size_t i = 0; i < found.size(); ++i) {
      if (used[i]) continue;
      float d = std::hypot(found[i].x - old.x, found[i].y - old.y);
      if (d < bestD) { bestD = d; best = int(i); }
    }
    if (best < 0) continue; // lost
    used[size_t(best)] = true;
    Blob b = found[size_t(best)];
    b.id = old.id;
    b.x = old.x + (b.x - old.x) * (1 - k);
    b.y = old.y + (b.y - old.y) * (1 - k);
    b.w = old.w + (b.w - old.w) * (1 - k);
    b.h = old.h + (b.h - old.h) * (1 - k);
    next.push_back(b);
  }
  for (size_t i = 0; i < found.size(); ++i)
    if (!used[i]) {
      Blob b = found[i];
      b.id = nextId_++;
      next.push_back(b);
    }
  std::sort(next.begin(), next.end(), [](const Blob &a, const Blob &b) { return a.id < b.id; });
  blobs_ = std::move(next);
  return blobs_;
}

} // namespace lumen
