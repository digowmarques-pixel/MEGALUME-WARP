// HAP video frames (Vidvox) parsed to GPU-ready compressed textures.
// Spec: https://github.com/Vidvox/hap/blob/master/documentation/HapVideoDRAFT.md
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace lumen {

enum class HapFormat { DXT1, DXT5, YCoCgDXT5 };

struct HapFrame {
  HapFormat format = HapFormat::DXT1;
  std::vector<uint8_t> color; // BC1 or BC3 blocks
  std::vector<uint8_t> alpha; // BC4 blocks (HAP Q Alpha only)
};

bool decodeHapFrame(const uint8_t *data, size_t size, HapFrame &out, std::string *error = nullptr);

// Raw Snappy block decompression (no framing). Exposed for tests.
bool snappyDecompress(const uint8_t *src, size_t size, std::vector<uint8_t> &out);

} // namespace lumen
