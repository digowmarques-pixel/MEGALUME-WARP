#include "media/text.h"

#include "platform/platform.h"

#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#endif
#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_STATIC
#include <imstb_truetype.h>
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <map>
#include <memory>

namespace lumen {

std::string defaultFontFile() {
  const char *candidates[] = {"C:/Windows/Fonts/segoeui.ttf", "C:/Windows/Fonts/arial.ttf",
                              "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
                              "/System/Library/Fonts/Supplemental/Arial.ttf", "/Library/Fonts/Arial.ttf"};
  std::error_code ec;
  for (const char *c : candidates)
    if (std::filesystem::exists(c, ec)) return c;
  return {};
}

namespace {

struct Font {
  std::string data;
  stbtt_fontinfo info{};
  bool ok = false;
};

// stb_truetype trusts the file: anything that is not a TTF/OTF/TTC (offset
// -1), or whose table directory points past the end, would be read out of
// bounds (a crash on every load of the project). Checked before using it.
bool plausibleFont(const std::string &d, int offset) {
  auto u16 = [&](size_t p) { return p + 2 <= d.size() ? unsigned(uint8_t(d[p])) << 8 | uint8_t(d[p + 1]) : 0u; };
  auto u32 = [&](size_t p) { return p + 4 <= d.size() ? (u16(p) << 16) | u16(p + 2) : 0u; };
  if (offset < 0 || size_t(offset) + 12 > d.size()) return false;
  size_t tables = u16(size_t(offset) + 4), dir = size_t(offset) + 12;
  if (tables == 0 || dir + tables * 16 > d.size()) return false;
  for (size_t t = 0; t < tables; ++t) {
    size_t at = u32(dir + t * 16 + 8), len = u32(dir + t * 16 + 12);
    if (at > d.size() || len > d.size() - at) return false;
  }
  return true;
}

Font *loadFont(const std::string &file) {
  static std::map<std::string, std::unique_ptr<Font>> cache;
  auto &f = cache[file];
  if (!f) {
    f = std::make_unique<Font>();
    if (platform::readFile(file, f->data) && !f->data.empty()) {
      auto *bytes = reinterpret_cast<const unsigned char *>(f->data.data());
      int offset = f->data.size() >= 12 ? stbtt_GetFontOffsetForIndex(bytes, 0) : -1;
      f->ok = plausibleFont(f->data, offset) && stbtt_InitFont(&f->info, bytes, offset) != 0;
      if (!f->ok) std::fprintf(stderr, "[texto] fonte inválida: %s\n", file.c_str());
    }
  }
  return f->ok ? f.get() : nullptr;
}

// UTF-8; bytes that are not valid UTF-8 are read as Windows-1252 (a .txt saved
// as "ANSI" by Excel or Notepad: "São Paulo"). A BOM at the start is skipped.
std::vector<uint32_t> decodeUtf8(const std::string &s) {
  static const uint16_t cp1252[32] = {0x20AC, 0x81,   0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021,
                                      0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0x8D,   0x017D, 0x8F,
                                      0x90,   0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014,
                                      0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0x9D,   0x017E, 0x0178};
  std::vector<uint32_t> out;
  size_t i = s.compare(0, 3, "\xEF\xBB\xBF") == 0 ? 3 : 0;
  while (i < s.size()) {
    unsigned char c = static_cast<unsigned char>(s[i]);
    int n = c < 0x80 ? 1 : (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : (c >> 3) == 30 ? 4 : 0;
    bool valid = n > 0 && i + size_t(n) <= s.size();
    for (int k = 1; valid && k < n; ++k) valid = (static_cast<unsigned char>(s[i + size_t(k)]) & 0xC0) == 0x80;
    if (!valid) {
      out.push_back(c >= 0x80 && c < 0xA0 ? cp1252[c - 0x80] : c);
      ++i;
      continue;
    }
    uint32_t cp = n == 1 ? c : n == 2 ? c & 0x1Fu : n == 3 ? c & 0x0Fu : c & 0x07u;
    for (int k = 1; k < n; ++k) cp = cp << 6 | (static_cast<unsigned char>(s[i + size_t(k)]) & 0x3Fu);
    out.push_back(cp);
    i += size_t(n);
  }
  return out;
}

} // namespace

void renderText(const std::string &utf8, const std::string &fontFile, float size, int align, int valign,
                float lineSpacing, const Color &fg, const Color &bg, int w, int h, std::vector<unsigned char> &rgba) {
  w = std::clamp(w, 1, 8192);
  h = std::clamp(h, 1, 8192);
  rgba.assign(size_t(w) * size_t(h) * 4, 0);
  for (size_t i = 0; i < rgba.size(); i += 4) { // background, premultiplied later by the compositor
    rgba[i] = (unsigned char)(std::clamp(bg.r, 0.0f, 1.0f) * 255);
    rgba[i + 1] = (unsigned char)(std::clamp(bg.g, 0.0f, 1.0f) * 255);
    rgba[i + 2] = (unsigned char)(std::clamp(bg.b, 0.0f, 1.0f) * 255);
    rgba[i + 3] = (unsigned char)(std::clamp(bg.a, 0.0f, 1.0f) * 255);
  }
  Font *font = loadFont(fontFile.empty() ? defaultFontFile() : fontFile);
  if (!font) return;
  const stbtt_fontinfo &fi = font->info;
  float scale = stbtt_ScaleForPixelHeight(&fi, std::max(1.0f, size));
  int ascent, descent, gap;
  stbtt_GetFontVMetrics(&fi, &ascent, &descent, &gap);
  float lineH = float(ascent - descent + gap) * scale * std::max(0.1f, lineSpacing);
  auto advance = [&](uint32_t cp, uint32_t next) {
    int adv, lsb;
    stbtt_GetCodepointHMetrics(&fi, int(cp), &adv, &lsb);
    float a = float(adv) * scale;
    if (next) a += float(stbtt_GetCodepointKernAdvance(&fi, int(cp), int(next))) * scale;
    return a;
  };
  // Word wrap into lines that fit the width (with a small margin).
  float maxW = float(w) - size * 0.2f;
  std::vector<std::vector<uint32_t>> lines(1);
  std::vector<uint32_t> cps = decodeUtf8(utf8);
  std::vector<uint32_t> word;
  float lineW = 0, wordW = 0;
  auto flushWord = [&]() {
    if (!lines.back().empty() && lineW + wordW > maxW) {
      while (!lines.back().empty() && lines.back().back() == ' ') lines.back().pop_back();
      lines.emplace_back();
      lineW = 0;
    }
    lines.back().insert(lines.back().end(), word.begin(), word.end());
    lineW += wordW;
    word.clear();
    wordW = 0;
  };
  for (uint32_t cp : cps) {
    if (cp == '\r') continue;
    if (cp == '\n') { flushWord(); lines.emplace_back(); lineW = 0; continue; }
    word.push_back(cp);
    wordW += advance(cp, 0);
    if (cp == ' ') flushWord();
  }
  flushWord();
  float total = lineH * float(lines.size());
  float y0 = valign == 0 ? size * 0.1f : valign == 2 ? float(h) - total - size * 0.1f : (float(h) - total) * 0.5f;
  std::vector<unsigned char> glyph;
  for (size_t li = 0; li < lines.size(); ++li) {
    auto &line = lines[li];
    float lw = 0;
    for (size_t i = 0; i < line.size(); ++i) lw += advance(line[i], i + 1 < line.size() ? line[i + 1] : 0);
    float x = align == 0 ? size * 0.1f : align == 2 ? float(w) - lw - size * 0.1f : (float(w) - lw) * 0.5f;
    float baseline = y0 + float(li) * lineH + float(ascent) * scale;
    for (size_t i = 0; i < line.size(); ++i) {
      uint32_t cp = line[i];
      int x0, y0g, x1, y1;
      stbtt_GetCodepointBitmapBoxSubpixel(&fi, int(cp), scale, scale, x - std::floor(x), 0, &x0, &y0g, &x1, &y1);
      int gw = x1 - x0, gh = y1 - y0g;
      if (gw > 0 && gh > 0) {
        glyph.assign(size_t(gw) * size_t(gh), 0);
        stbtt_MakeCodepointBitmapSubpixel(&fi, glyph.data(), gw, gh, gw, scale, scale, x - std::floor(x), 0, int(cp));
        int ox = int(std::floor(x)) + x0, oy = int(std::lround(baseline)) + y0g;
        for (int gy = 0; gy < gh; ++gy) {
          int py = oy + gy;
          if (py < 0 || py >= h) continue;
          for (int gx = 0; gx < gw; ++gx) {
            int px = ox + gx;
            if (px < 0 || px >= w) continue;
            float a = float(glyph[size_t(gy * gw + gx)]) / 255.0f * std::clamp(fg.a, 0.0f, 1.0f);
            if (a <= 0) continue;
            unsigned char *d = &rgba[(size_t(py) * size_t(w) + size_t(px)) * 4];
            // "Over" with straight alpha.
            float da = float(d[3]) / 255.0f, oa = a + da * (1 - a);
            auto mix = [&](unsigned char dc, float sc) {
              float v = oa > 0 ? (sc * a + float(dc) / 255.0f * da * (1 - a)) / oa : 0.0f;
              return (unsigned char)(std::clamp(v, 0.0f, 1.0f) * 255.0f + 0.5f);
            };
            d[0] = mix(d[0], fg.r);
            d[1] = mix(d[1], fg.g);
            d[2] = mix(d[2], fg.b);
            d[3] = (unsigned char)(oa * 255.0f + 0.5f);
          }
        }
      }
      x += advance(cp, i + 1 < line.size() ? line[i + 1] : 0);
    }
  }
}

const Texture *TextRenderer::render(const MediaItem &m) {
  // A .txt file is read again when it changes (checked every 2 s).
  std::string text = m.text;
  if (!m.path.empty()) {
    double now = std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
    if (now - checkedAt_ > 2.0) {
      checkedAt_ = now;
      std::error_code ec;
      auto t = std::filesystem::last_write_time(platform::toPath(m.path), ec);
      long long tt = ec ? 0 : (long long)t.time_since_epoch().count();
      std::string fresh;
      // A file being written can fail to open (locked): keep the last text
      // and try again in 2 s, instead of going blank until the next change.
      if (tt != fileTime_ && platform::readFile(m.path, fresh)) {
        fileTime_ = tt;
        fileText_ = std::move(fresh);
      }
    }
    text = fileText_;
  }
  char head[256];
  std::snprintf(head, sizeof(head), "%g|%d|%d|%g|%g %g %g %g|%g %g %g %g|%d|%d|", m.fontSize, m.align, m.valign,
                m.lineSpacing, m.color1.r, m.color1.g, m.color1.b, m.color1.a, m.color2.r, m.color2.g, m.color2.b,
                m.color2.a, m.width, m.height);
  std::string key = std::string(head) + m.font + "|" + text;
  if (key != key_ || tex_.empty()) {
    key_ = key;
    std::vector<unsigned char> rgba;
    renderText(text, m.font, m.fontSize, m.align, m.valign, m.lineSpacing, m.color1, m.color2, m.width, m.height, rgba);
    // Straight alpha: the compositor multiplies by alpha itself.
    tex_.upload(rgba.data(), std::clamp(m.width, 1, 8192), std::clamp(m.height, 1, 8192));
  }
  return &tex_;
}

} // namespace lumen
