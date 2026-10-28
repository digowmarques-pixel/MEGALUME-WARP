// DMX for LED pixel mapping: channel layout of LED fixtures and Art-Net /
// sACN (E1.31) packet encoding. Pure functions, no sockets or GPU.
#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace lumen {

enum class DmxProtocol { ArtNet, SACN };
// Values are stored in project files: only append.
enum class LedFormat { RGB, BGR, GRB, RGBW, Mono };
const char *ledFormatName(LedFormat f);
int ledChannelsPerPixel(LedFormat f, bool wide);

using DmxUniverse = std::array<uint8_t, 512>;

struct LedLayoutParams {
  int cols = 60, rows = 1;
  LedFormat format = LedFormat::RGB;
  bool wide = false;     // 16-bit: two channels (coarse, fine) per colour
  bool zigzag = false;   // every other line runs backwards
  bool vertical = false; // wiring follows columns instead of rows
  bool flipX = false, flipY = false;
  int universe = 0;      // protocol universe of the first pixel
  int channel = 1;       // 1..512, first channel of the first pixel
};

struct LedPixelSlot {
  int col = 0, row = 0; // cell of the sampled image (row 0 = top)
  int universe = 0;
  int channel = 0;      // 0-based start channel inside the universe
};

// Pixel order along the wire. A pixel never straddles two universes: when it
// does not fit, it moves to channel 1 of the next universe (common practice
// of LED controllers, e.g. 170 RGB pixels per universe).
std::vector<LedPixelSlot> computeLedLayout(const LedLayoutParams &p);

// Writes one pixel's colour (0..1, already curved) into the universe.
void writeLedPixel(DmxUniverse &u, int channel, LedFormat f, bool wide, float r, float g, float b);

// ArtDmx packet (Art-Net 4, protocol 14). `universe` is the 15-bit port address.
std::vector<uint8_t> encodeArtDmx(int universe, uint8_t sequence, const DmxUniverse &data);

// E1.31 data packet (512 slots). `cid` is the 16-byte source UUID.
std::vector<uint8_t> encodeSacn(int universe, uint8_t sequence, const DmxUniverse &data,
                                const std::array<uint8_t, 16> &cid, const std::string &sourceName,
                                uint8_t priority = 100);
// Multicast group of an sACN universe: 239.255.hi.lo
std::string sacnMulticastAddress(int universe);

} // namespace lumen
