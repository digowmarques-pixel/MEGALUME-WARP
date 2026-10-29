// Lumen colours: black and yellow first; one thin colour per kind of thing
// (a stripe, an outline, an icon), never on large areas.
#pragma once

#include <imgui.h>

namespace lumen::pal {

struct Rgb { int r, g, b; };
constexpr Rgb kYellow{255, 196, 0};   // the brand, what you act on (GO, active tool, cue on air)
constexpr Rgb kGreen{64, 214, 120};   // what is playing (cells on air, time, sound meters)
constexpr Rgb kCyan{56, 196, 255};    // mapping and outputs (corners, mesh, projectors)
constexpr Rgb kMagenta{255, 82, 196}; // masks
constexpr Rgb kPurple{168, 120, 255}; // effects and timeline
constexpr Rgb kOrange{255, 140, 40};  // warnings, the last seconds of a clip
constexpr Rgb kRed{235, 64, 64};      // stop, blackout

inline ImU32 u32(Rgb c, int alpha = 255) { return IM_COL32(c.r, c.g, c.b, alpha); }
inline ImVec4 vec(Rgb c, float alpha = 1.0f) { return ImVec4(c.r / 255.0f, c.g / 255.0f, c.b / 255.0f, alpha); }

} // namespace lumen::pal
