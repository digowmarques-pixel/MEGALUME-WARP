#include "core/timecode.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace lumen {

static int nominal(double fps) { return int(std::lround(fps)); }

double Timecode::seconds() const {
  int n = nominal(fps);
  long frames = ((long(h) * 60 + m) * 60 + s) * n + f;
  if (dropFrame && n == 30) { // 29.97 DF: frames 0 and 1 are skipped each minute except every 10th
    long totalMinutes = long(h) * 60 + m;
    frames -= 2 * (totalMinutes - totalMinutes / 10);
    return double(frames) / 29.97;
  }
  return double(frames) / double(n);
}

Timecode Timecode::fromSeconds(double t, double fps) {
  Timecode tc;
  tc.fps = fps;
  t = std::max(0.0, t);
  int n = nominal(fps);
  long frames = long(std::floor(t * fps + 1e-6));
  if (std::fabs(fps - 29.97) < 0.01) {
    tc.dropFrame = true;
    // Back from real frames to drop-frame labels.
    long d = frames / 17982, mm = frames % 17982;
    frames += 18 * d + (mm > 1 ? 2 * ((mm - 2) / 1798) : 0);
  }
  tc.f = int(frames % n);
  long sec = frames / n;
  tc.s = int(sec % 60);
  tc.m = int((sec / 60) % 60);
  tc.h = int((sec / 3600) % 24);
  return tc;
}

std::string Timecode::str() const {
  char b[32];
  std::snprintf(b, sizeof(b), "%02d:%02d:%02d%c%02d", h, m, s, dropFrame ? ';' : ':', f);
  return b;
}

bool parseTimecode(const std::string &text, double fps, double &seconds) {
  int v[4] = {0, 0, 0, 0}, n = 0;
  std::string cur;
  for (char c : text + ":") {
    if (c == ':' || c == ';') {
      if (n >= 4 || cur.empty()) return false;
      v[n++] = std::atoi(cur.c_str());
      cur.clear();
    } else if ((c >= '0' && c <= '9') || c == '.') {
      cur += c;
    } else if (c != ' ') {
      return false;
    }
  }
  if (n < 3) return false;
  if (n == 4) { // frames: through the frame count, right for 29.97 drop frame too
    Timecode tc;
    tc.fps = fps > 0 ? fps : 25;
    tc.dropFrame = std::fabs(tc.fps - 29.97) < 0.01;
    tc.h = std::max(0, v[0]);
    tc.m = std::clamp(v[1], 0, 59);
    tc.s = std::clamp(v[2], 0, 59);
    tc.f = std::clamp(v[3], 0, nominal(tc.fps) - 1);
    seconds = tc.seconds();
    return true;
  }
  seconds = v[0] * 3600.0 + v[1] * 60.0 + std::atof(text.substr(text.rfind(':') + 1).c_str());
  return true;
}

static double mtcRate(int code) {
  static const double r[4] = {24, 25, 29.97, 30};
  return r[code & 3];
}

static int mtcCode(double fps) {
  if (std::fabs(fps - 24) < 0.5) return 0;
  if (std::fabs(fps - 25) < 0.5) return 1;
  if (std::fabs(fps - 29.97) < 0.01) return 2;
  return 3;
}

bool MtcDecoder::feed(const uint8_t *msg, size_t n, Timecode &out) {
  if (n >= 10 && msg[0] == 0xF0 && msg[1] == 0x7F && msg[3] == 0x01 && msg[4] == 0x01) {
    // Full frame: F0 7F <dev> 01 01 hr mn sc fr F7 (rate in hr bits 5-6).
    out.fps = mtcRate(msg[5] >> 5);
    out.dropFrame = (msg[5] >> 5 & 3) == 2;
    out.h = msg[5] & 0x1F;
    out.m = msg[6];
    out.s = msg[7];
    out.f = msg[8];
    received_ = 0;
    return true;
  }
  if (n < 2 || msg[0] != 0xF1) return false;
  int piece = (msg[1] >> 4) & 7, val = msg[1] & 0x0F;
  if (piece != (lastPiece_ + 1) % 8) received_ = 0; // lost one: start over
  lastPiece_ = piece;
  pieces_[piece] = val;
  ++received_;
  if (piece != 7 || received_ < 8) return false;
  out.f = pieces_[0] | (pieces_[1] << 4);
  out.s = pieces_[2] | (pieces_[3] << 4);
  out.m = pieces_[4] | (pieces_[5] << 4);
  out.h = pieces_[6] | ((pieces_[7] & 1) << 4);
  int code = (pieces_[7] >> 1) & 3;
  out.fps = mtcRate(code);
  out.dropFrame = code == 2;
  // The 8 pieces take 2 frames: the time they describe is 2 frames old.
  double t = out.seconds() + 2.0 / out.fps;
  out = Timecode::fromSeconds(t, out.fps);
  return true;
}

std::vector<uint8_t> mtcQuarterFrame(const Timecode &tc, int piece) {
  int v = 0;
  switch (piece & 7) {
  case 0: v = tc.f & 0xF; break;
  case 1: v = (tc.f >> 4) & 1; break;
  case 2: v = tc.s & 0xF; break;
  case 3: v = (tc.s >> 4) & 3; break;
  case 4: v = tc.m & 0xF; break;
  case 5: v = (tc.m >> 4) & 3; break;
  case 6: v = tc.h & 0xF; break;
  case 7: v = ((tc.h >> 4) & 1) | (mtcCode(tc.fps) << 1); break;
  }
  return {0xF1, uint8_t(((piece & 7) << 4) | v)};
}

std::vector<uint8_t> mtcFullFrame(const Timecode &tc) {
  return {0xF0, 0x7F, 0x7F, 0x01, 0x01, uint8_t((mtcCode(tc.fps) << 5) | (tc.h & 0x1F)), uint8_t(tc.m),
          uint8_t(tc.s), uint8_t(tc.f), 0xF7};
}

// ---------------------------------------------------------------- LTC

static void ltcBits(const Timecode &tc, uint8_t bits[80]) {
  std::fill(bits, bits + 80, 0);
  auto put = [&](int pos, int value, int count) {
    for (int i = 0; i < count; ++i) bits[pos + i] = uint8_t((value >> i) & 1);
  };
  put(0, tc.f % 10, 4);
  put(8, tc.f / 10, 2);
  bits[10] = tc.dropFrame ? 1 : 0;
  put(16, tc.s % 10, 4);
  put(24, tc.s / 10, 3);
  put(32, tc.m % 10, 4);
  put(40, tc.m / 10, 3);
  put(48, tc.h % 10, 4);
  put(56, tc.h / 10, 2);
  // Sync word 0011 1111 1111 1101.
  static const uint8_t sync[16] = {0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1};
  std::copy(sync, sync + 16, bits + 64);
  // Polarity correction bit (27, at 25 fps 59): even number of zeros.
  int ones = 0;
  for (int i = 0; i < 80; ++i) ones += bits[i];
  if (ones % 2) bits[nominal(tc.fps) == 25 ? 59 : 27] ^= 1;
}

void LtcEncoder::encode(const Timecode &tc, std::vector<float> &out, float amp) {
  uint8_t bits[80];
  ltcBits(tc, bits);
  double bitLen = double(rate_) / (tc.fps * 80.0);
  for (int b = 0; b < 80; ++b) {
    double start = clock_, mid = start + bitLen * 0.5, end = start + bitLen;
    level_ = -level_; // a transition at the start of every bit
    bool flipped = false;
    while (sample_ < end) {
      if (bits[b] && !flipped && sample_ >= mid) { level_ = -level_; flipped = true; } // ones: one more in the middle
      out.push_back(level_ * amp);
      sample_ += 1;
    }
    clock_ = end;
  }
}

void LtcDecoder::feed(const float *s, size_t count, size_t stride) {
  for (size_t i = 0; i < count; ++i) {
    float x = s[i * stride];
    level_ = std::max(std::fabs(x), level_ * 0.9995f);
    float hyst = std::max(0.02f, level_ * 0.2f);
    since_ += 1;
    bool sign = sign_;
    if (x > hyst) sign = true;
    else if (x < -hyst) sign = false;
    if (sign == sign_) continue;
    sign_ = sign;
    double d = since_;
    since_ = 0;
    if (d > bitLen_ * 2.5 || d < bitLen_ * 0.2) { // noise or silence: relearn
      half_ = false;
      if (d < 200) bitLen_ = std::clamp(d, 8.0, 60.0);
      continue;
    }
    if (d > bitLen_ * 0.75) { // a whole bit without a middle transition: 0
      if (half_) half_ = false;
      bitLen_ = bitLen_ * 0.95 + d * 0.05;
      bit(0);
    } else if (!half_) {
      half_ = true;
      halfLen_ = d;
    } else { // two halves: 1
      half_ = false;
      bitLen_ = bitLen_ * 0.95 + (d + halfLen_) * 0.05;
      bit(1);
    }
  }
}

void LtcDecoder::bit(int b) {
  std::copy(bits_ + 1, bits_ + 80, bits_);
  bits_[79] = uint8_t(b);
  if (count_ < 80) { ++count_; if (count_ < 80) return; }
  static const uint8_t sync[16] = {0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1};
  if (!std::equal(sync, sync + 16, bits_ + 64)) return;
  auto get = [&](int pos, int n) {
    int v = 0;
    for (int i = 0; i < n; ++i) v |= bits_[pos + i] << i;
    return v;
  };
  Timecode tc;
  tc.f = get(0, 4) + 10 * get(8, 2);
  tc.s = get(16, 4) + 10 * get(24, 3);
  tc.m = get(32, 4) + 10 * get(40, 3);
  tc.h = get(48, 4) + 10 * get(56, 2);
  tc.dropFrame = bits_[10] != 0;
  double fps = double(rate_) / (bitLen_ * 80.0);
  tc.fps = tc.dropFrame ? 29.97 : fps < 24.5 ? 24 : fps < 27.5 ? 25 : 30;
  if (tc.f >= nominal(tc.fps) || tc.s > 59 || tc.m > 59 || tc.h > 23) return;
  last_ = tc;
  valid_ = true;
  ++frames_;
}

} // namespace lumen
