// Blob tracking: finds bright (or changed) regions in a small grayscale image
// from a camera and follows them from frame to frame. Positions are 0..1 with
// (0, 0) at the top-left. Pure CPU code, no GL.
#pragma once

#include <cstdint>
#include <vector>

namespace lumen {

struct Blob {
  int id = 0;            // stable while the blob is followed
  float x = 0, y = 0;    // centre
  float w = 0, h = 0;    // bounding box size
  float area = 0;        // fraction of the image
};

struct BlobParams {
  float threshold = 0.5f;  // 0..1 (difference with the background when it is set)
  bool invert = false;     // dark objects on a light background
  float minArea = 0.002f;  // fraction of the image
  float maxArea = 0.5f;
  int maxBlobs = 8;
  float mergeDistance = 0.04f; // blobs closer than this become one (0 = never)
  float smoothing = 0.5f;      // 0 = raw, 0.95 = very smooth (more latency)
};

class BlobTracker {
public:
  // `luma`: w x h bytes, row 0 = top.
  const std::vector<Blob> &process(const uint8_t *luma, int w, int h, const BlobParams &p);
  // Background subtraction: compare with this image instead of a fixed threshold.
  void setBackground(const uint8_t *luma, int w, int h);
  void clearBackground() { background_.clear(); }
  bool hasBackground() const { return !background_.empty(); }
  const std::vector<Blob> &blobs() const { return blobs_; }
  // Binary image of the last frame (1 byte per pixel, 0 / 255), for previews.
  const std::vector<uint8_t> &mask() const { return mask_; }
  int width() const { return w_; }
  int height() const { return h_; }

private:
  std::vector<uint8_t> background_, mask_;
  std::vector<int> labels_;
  std::vector<Blob> blobs_;
  int w_ = 0, h_ = 0, bgW_ = 0, bgH_ = 0;
  int nextId_ = 1;
};

} // namespace lumen
