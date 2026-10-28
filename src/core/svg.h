// SVG import / export of mask shapes (paths drawn in Illustrator / Inkscape
// over a photo of the set, or exported from Lumen).
#pragma once

#include "core/project.h"

#include <string>
#include <vector>

namespace lumen {

// Paths only, in target pixels (canvas px, or output px for output masks).
std::string masksToSvg(const std::vector<Mask> &masks, float width, float height);

// Every closed shape (path, polygon, polyline, rect, circle, ellipse) becomes a
// mask. The SVG viewBox (or width/height) is scaled to width x height.
std::vector<Mask> masksFromSvg(const std::string &svg, float width, float height,
                               std::string *error = nullptr);

} // namespace lumen
