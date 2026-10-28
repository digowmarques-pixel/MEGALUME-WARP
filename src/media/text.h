// Text media: renders a MediaItem's text (or a .txt file) into a texture
// with stb_truetype. Re-rendered only when something changes.
#pragma once

#include "core/project.h"
#include "render/gpu.h"

#include <string>
#include <vector>

namespace lumen {

// CPU rendering (row 0 = top, RGBA). Also used by the tests.
void renderText(const std::string &utf8, const std::string &fontFile, float size, int align, int valign,
                float lineSpacing, const Color &fg, const Color &bg, int w, int h, std::vector<unsigned char> &rgba);
std::string defaultFontFile();

class TextRenderer {
public:
  const Texture *render(const MediaItem &m);

private:
  Texture tex_;
  std::string key_;
  std::string fileText_;
  long long fileTime_ = -1;
  double checkedAt_ = -10;
};

} // namespace lumen
