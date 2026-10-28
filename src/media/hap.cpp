#include "media/hap.h"

#include <cstring>

namespace lumen {

namespace {

uint32_t le24(const uint8_t *p) { return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16); }
uint32_t le32(const uint8_t *p) { return le24(p) | (uint32_t(p[3]) << 24); }

// Section header: 3-byte size + type, or 0x000000 + type + 4-byte size.
bool readSection(const uint8_t *data, size_t size, size_t &headerLen, size_t &len, uint8_t &type) {
  if (size < 4) return false;
  len = le24(data);
  type = data[3];
  headerLen = 4;
  if (len == 0) {
    if (size < 8) return false;
    len = le32(data + 4);
    headerLen = 8;
  }
  return headerLen + len <= size;
}

bool decompressChunk(uint8_t compressor, const uint8_t *src, size_t n, std::vector<uint8_t> &out) {
  if (compressor == 0x0A) { // none
    out.insert(out.end(), src, src + n);
    return true;
  }
  if (compressor == 0x0B) { // snappy
    std::vector<uint8_t> tmp;
    if (!snappyDecompress(src, n, tmp)) return false;
    out.insert(out.end(), tmp.begin(), tmp.end());
    return true;
  }
  return false;
}

// Decodes the payload of one texture section into `out`.
bool decodeTexture(uint8_t type, const uint8_t *data, size_t size, HapFormat &format,
                   std::vector<uint8_t> &out, bool &isAlpha, std::string *error) {
  uint8_t compressor = type >> 4, tex = type & 0x0F;
  isAlpha = false;
  switch (tex) {
  case 0xB: format = HapFormat::DXT1; break;
  case 0xE: format = HapFormat::DXT5; break;
  case 0xF: format = HapFormat::YCoCgDXT5; break;
  case 0x1: isAlpha = true; break; // RGTC1 alpha (HAP Q Alpha second texture)
  default:
    if (error) *error = "unsupported HAP texture format (HAP R / BC7?)";
    return false;
  }
  out.clear();
  if (compressor == 0xA || compressor == 0xB)
    return decompressChunk(compressor == 0xA ? 0x0A : 0x0B, data, size, out) ||
           (error && (*error = "snappy error", false));
  if (compressor != 0xC) {
    if (error) *error = "unknown HAP compressor";
    return false;
  }
  // Complex: decode instructions container, then the chunked frame data.
  size_t h, len;
  uint8_t t;
  if (!readSection(data, size, h, len, t) || t != 0x01) {
    if (error) *error = "missing HAP decode instructions";
    return false;
  }
  const uint8_t *ins = data + h, *insEnd = ins + len;
  const uint8_t *frameData = insEnd;
  size_t frameSize = size - (h + len);
  std::vector<uint8_t> compressors;
  std::vector<uint32_t> sizes, offsets;
  while (ins < insEnd) {
    size_t sh, sl;
    uint8_t st;
    if (!readSection(ins, size_t(insEnd - ins), sh, sl, st)) return false;
    const uint8_t *p = ins + sh;
    if (st == 0x02) compressors.assign(p, p + sl);
    else if (st == 0x03) for (size_t i = 0; i + 4 <= sl; i += 4) sizes.push_back(le32(p + i));
    else if (st == 0x04) for (size_t i = 0; i + 4 <= sl; i += 4) offsets.push_back(le32(p + i));
    ins += sh + sl;
  }
  if (compressors.empty() || compressors.size() != sizes.size()) {
    if (error) *error = "bad HAP chunk tables";
    return false;
  }
  size_t pos = 0;
  for (size_t i = 0; i < compressors.size(); ++i) {
    size_t off = offsets.size() == sizes.size() ? offsets[i] : pos;
    if (off + sizes[i] > frameSize) {
      if (error) *error = "HAP chunk out of range";
      return false;
    }
    if (!decompressChunk(compressors[i], frameData + off, sizes[i], out)) {
      if (error) *error = "HAP chunk decompression failed";
      return false;
    }
    pos = off + sizes[i];
  }
  return true;
}

} // namespace

bool snappyDecompress(const uint8_t *src, size_t n, std::vector<uint8_t> &out) {
  size_t pos = 0;
  // Preamble: uncompressed length as a varint.
  uint64_t len = 0;
  for (int shift = 0; shift < 35; shift += 7) {
    if (pos >= n) return false;
    uint8_t b = src[pos++];
    len |= uint64_t(b & 0x7F) << shift;
    if (!(b & 0x80)) break;
  }
  if (len > (1u << 30)) return false;
  out.resize(size_t(len));
  size_t op = 0;
  while (pos < n) {
    uint8_t tag = src[pos++];
    size_t length, offset = 0;
    switch (tag & 3) {
    case 0: { // literal
      length = tag >> 2;
      if (length >= 60) {
        size_t extra = length - 59;
        if (pos + extra > n) return false;
        length = 0;
        for (size_t i = 0; i < extra; ++i) length |= size_t(src[pos + i]) << (8 * i);
        pos += extra;
      }
      length += 1;
      if (pos + length > n || op + length > out.size()) return false;
      std::memcpy(out.data() + op, src + pos, length);
      pos += length;
      op += length;
      continue;
    }
    case 1:
      if (pos + 1 > n) return false;
      length = 4 + ((tag >> 2) & 7);
      offset = (size_t(tag >> 5) << 8) | src[pos];
      pos += 1;
      break;
    case 2:
      if (pos + 2 > n) return false;
      length = 1 + (tag >> 2);
      offset = size_t(src[pos]) | (size_t(src[pos + 1]) << 8);
      pos += 2;
      break;
    default:
      if (pos + 4 > n) return false;
      length = 1 + (tag >> 2);
      offset = le32(src + pos);
      pos += 4;
      break;
    }
    if (offset == 0 || offset > op || op + length > out.size()) return false;
    // Byte-by-byte copy: source and destination may overlap (run-length style).
    for (size_t i = 0; i < length; ++i, ++op) out[op] = out[op - offset];
  }
  return op == out.size();
}

bool decodeHapFrame(const uint8_t *data, size_t size, HapFrame &out, std::string *error) {
  size_t h, len;
  uint8_t type;
  if (!readSection(data, size, h, len, type)) {
    if (error) *error = "truncated HAP frame";
    return false;
  }
  out.alpha.clear();
  if (type == 0x0D) { // multiple images (HAP Q Alpha): YCoCg + alpha
    const uint8_t *p = data + h, *end = p + len;
    bool gotColor = false;
    while (p < end) {
      size_t sh, sl;
      uint8_t st;
      if (!readSection(p, size_t(end - p), sh, sl, st)) return false;
      HapFormat f;
      bool isAlpha;
      std::vector<uint8_t> buf;
      if (!decodeTexture(st, p + sh, sl, f, buf, isAlpha, error)) return false;
      if (isAlpha) out.alpha = std::move(buf);
      else { out.color = std::move(buf); out.format = f; gotColor = true; }
      p += sh + sl;
    }
    return gotColor;
  }
  bool isAlpha;
  if (!decodeTexture(type, data + h, len, out.format, out.color, isAlpha, error)) return false;
  if (isAlpha && error) *error = "HAP Alpha-Only (only alpha, no colour texture)";
  return !isAlpha;
}

} // namespace lumen
