#include "core/audio_route.h"

namespace lumen {

std::vector<AudioPatch> resolveAudioRoute(int src, int out, int first, const std::vector<AudioPatch> &custom) {
  std::vector<AudioPatch> r;
  if (src <= 0 || out <= 0) return r;
  if (!custom.empty()) {
    for (auto &p : custom)
      if (p.src >= 0 && p.src < src && p.dst >= 0 && p.dst < out && p.gain != 0) r.push_back(p);
    return r;
  }
  if (first < 0 || first >= out) first = 0;
  int room = out - first; // outputs available from the first one
  if (room == 1) { // a single output: everything summed
    for (int c = 0; c < src; ++c) r.push_back({c, first, src > 1 ? 1.0f / float(src) : 1.0f});
    return r;
  }
  if (src == 1) {
    r.push_back({0, first, 1});
    r.push_back({0, first + 1, 1});
  } else if (src == 2 || room < src) {
    if (src == 2) {
      r.push_back({0, first, 1});
      r.push_back({1, first + 1, 1});
    } else {
      // Downmix to the pair (FFmpeg / SMPTE order: FL FR FC LFE BL BR SL SR).
      const float k = 0.7071f;
      r.push_back({0, first, 1});
      r.push_back({1, first + 1, 1});
      if (src > 2) { r.push_back({2, first, k}); r.push_back({2, first + 1, k}); }
      if (src > 4) r.push_back({4, first, k});
      if (src > 5) r.push_back({5, first + 1, k});
      if (src > 6) r.push_back({6, first, k});
      if (src > 7) r.push_back({7, first + 1, k});
    }
  } else {
    for (int c = 0; c < src; ++c) r.push_back({c, first + c, 1});
  }
  return r;
}

} // namespace lumen
