#include "core/dmx.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace lumen {

const char *ledFormatName(LedFormat f) {
  static const char *names[] = {"RGB", "BGR", "GRB", "RGBW", "Mono"};
  return names[int(f)];
}

int ledChannelsPerPixel(LedFormat f, bool wide) {
  int n = f == LedFormat::Mono ? 1 : f == LedFormat::RGBW ? 4 : 3;
  return wide ? n * 2 : n;
}

std::vector<LedPixelSlot> computeLedLayout(const LedLayoutParams &p) {
  int cols = std::max(1, p.cols), rows = std::max(1, p.rows);
  int lines = p.vertical ? cols : rows;      // wired lines
  int perLine = p.vertical ? rows : cols;    // pixels per line
  int cpp = ledChannelsPerPixel(p.format, p.wide);
  std::vector<LedPixelSlot> out;
  out.reserve(size_t(cols) * size_t(rows));
  int universe = p.universe;
  int ch = std::clamp(p.channel, 1, 512) - 1;
  for (int line = 0; line < lines; ++line) {
    for (int k = 0; k < perLine; ++k) {
      int along = (p.zigzag && (line % 2 == 1)) ? perLine - 1 - k : k;
      int col = p.vertical ? line : along;
      int row = p.vertical ? along : line;
      if (p.flipX) col = cols - 1 - col;
      if (p.flipY) row = rows - 1 - row;
      if (ch + cpp > 512) {
        ++universe;
        ch = 0;
      }
      out.push_back({col, row, universe, ch});
      ch += cpp;
    }
  }
  return out;
}

static void put(DmxUniverse &u, int &ch, float v, bool wide) {
  v = std::clamp(v, 0.0f, 1.0f);
  if (wide) {
    int x = int(std::lround(v * 65535.0f));
    if (ch < 512) u[size_t(ch)] = uint8_t(x >> 8);
    if (ch + 1 < 512) u[size_t(ch + 1)] = uint8_t(x & 0xFF);
    ch += 2;
  } else {
    if (ch < 512) u[size_t(ch)] = uint8_t(std::lround(v * 255.0f));
    ch += 1;
  }
}

void writeLedPixel(DmxUniverse &u, int ch, LedFormat f, bool wide, float r, float g, float b) {
  switch (f) {
  case LedFormat::RGB: put(u, ch, r, wide); put(u, ch, g, wide); put(u, ch, b, wide); break;
  case LedFormat::BGR: put(u, ch, b, wide); put(u, ch, g, wide); put(u, ch, r, wide); break;
  case LedFormat::GRB: put(u, ch, g, wide); put(u, ch, r, wide); put(u, ch, b, wide); break;
  case LedFormat::RGBW: {
    // The white chip takes over the part shared by the three colours.
    float w = std::min({r, g, b});
    put(u, ch, r - w, wide); put(u, ch, g - w, wide); put(u, ch, b - w, wide); put(u, ch, w, wide);
    break;
  }
  case LedFormat::Mono: put(u, ch, 0.2126f * r + 0.7152f * g + 0.0722f * b, wide); break;
  }
}

std::vector<uint8_t> encodeArtDmx(int universe, uint8_t sequence, const DmxUniverse &data) {
  std::vector<uint8_t> p = {'A', 'r', 't', '-', 'N', 'e', 't', 0,
                            0x00, 0x50,      // OpOutput / OpDmx, little endian
                            0x00, 14,        // protocol version
                            sequence, 0,     // sequence, physical port
                            uint8_t(universe & 0xFF),        // SubUni
                            uint8_t((universe >> 8) & 0x7F), // Net
                            0x02, 0x00};     // length 512, big endian
  p.insert(p.end(), data.begin(), data.end());
  return p;
}

static void be16(std::vector<uint8_t> &p, size_t at, int v) {
  p[at] = uint8_t((v >> 8) & 0xFF);
  p[at + 1] = uint8_t(v & 0xFF);
}

std::vector<uint8_t> encodeSacn(int universe, uint8_t sequence, const DmxUniverse &data,
                                const std::array<uint8_t, 16> &cid, const std::string &name,
                                uint8_t priority) {
  std::vector<uint8_t> p(126 + 512, 0);
  // Root layer
  be16(p, 0, 0x0010);
  const char acn[12] = {'A', 'S', 'C', '-', 'E', '1', '.', '1', '7', 0, 0, 0};
  std::memcpy(&p[4], acn, 12);
  be16(p, 16, 0x7000 | int(p.size() - 16));
  p[21] = 0x04; // VECTOR_ROOT_E131_DATA
  std::memcpy(&p[22], cid.data(), 16);
  // Framing layer
  be16(p, 38, 0x7000 | int(p.size() - 38));
  p[43] = 0x02; // VECTOR_E131_DATA_PACKET
  std::memcpy(&p[44], name.data(), std::min<size_t>(name.size(), 63));
  p[108] = priority;
  p[111] = sequence;
  be16(p, 113, universe);
  // DMP layer
  be16(p, 115, 0x7000 | int(p.size() - 115));
  p[117] = 0x02; // VECTOR_DMP_SET_PROPERTY
  p[118] = 0xA1; // address & data type
  be16(p, 121, 1);       // address increment
  be16(p, 123, 513);     // property count: start code + 512
  p[125] = 0;            // DMX start code
  std::memcpy(&p[126], data.data(), 512);
  return p;
}

std::string sacnMulticastAddress(int universe) {
  return "239.255." + std::to_string((universe >> 8) & 0xFF) + "." + std::to_string(universe & 0xFF);
}

bool decodeArtDmx(const uint8_t *p, size_t size, int &universe, DmxUniverse &data, int &count) {
  static const char id[8] = {'A', 'r', 't', '-', 'N', 'e', 't', 0};
  if (size < 18 || std::memcmp(p, id, 8) != 0) return false;
  if ((p[8] | (p[9] << 8)) != 0x5000) return false; // OpDmx (little endian)
  universe = p[14] | ((p[15] & 0x7F) << 8);
  int len = (p[16] << 8) | p[17];
  count = std::clamp(std::min(len, int(size) - 18), 0, 512);
  std::memcpy(data.data(), p + 18, size_t(count));
  return true;
}

bool decodeSacn(const uint8_t *p, size_t size, int &universe, DmxUniverse &data, int &count) {
  static const char acn[12] = {'A', 'S', 'C', '-', 'E', '1', '.', '1', '7', 0, 0, 0};
  if (size < 126 || std::memcmp(p + 4, acn, 12) != 0) return false;
  if (p[21] != 0x04 || p[43] != 0x02 || p[117] != 0x02) return false; // data packet
  if (p[125] != 0) return false;                                       // DMX start code only
  universe = (p[113] << 8) | p[114];
  int props = (p[123] << 8) | p[124]; // start code + channels
  count = std::clamp(std::min(props - 1, int(size) - 126), 0, 512);
  std::memcpy(data.data(), p + 126, size_t(count));
  return true;
}

} // namespace lumen
