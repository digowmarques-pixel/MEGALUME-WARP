#include "ui/icons.h"

#include <algorithm>
#include <cmath>

namespace lumen {

namespace {

void drawHand(ImDrawList *dl, ImVec2 c, float s, ImU32 col) {
  // Four fingers, the palm and the thumb, as a filled silhouette.
  const float fw = s * 0.11f, r = fw * 0.5f;
  const float tops[4] = {-0.26f, -0.36f, -0.34f, -0.22f};
  for (int i = 0; i < 4; ++i) {
    float x = c.x + s * (-0.20f + 0.125f * float(i));
    dl->AddRectFilled({x, c.y + s * tops[i]}, {x + fw, c.y + s * 0.10f}, col, r);
  }
  dl->AddRectFilled({c.x - s * 0.21f, c.y - s * 0.02f}, {c.x + s * 0.28f, c.y + s * 0.36f}, col, s * 0.12f);
  dl->AddLine({c.x - s * 0.17f, c.y + s * 0.20f}, {c.x - s * 0.36f, c.y + s * 0.0f}, col, fw * 1.1f);
  dl->AddCircleFilled({c.x - s * 0.36f, c.y + s * 0.0f}, fw * 0.55f, col);
}

void drawMagnet(ImDrawList *dl, ImVec2 c, float s, ImU32 col) {
  // A horseshoe magnet opening upwards, with light pole tips.
  const float r = s * 0.22f, t = s * 0.15f, top = c.y - s * 0.32f, bend = c.y + s * 0.06f;
  dl->PathArcTo({c.x, bend}, r, 0.0f, 3.14159265f, 20);
  dl->PathStroke(col, 0, t);
  dl->AddLine({c.x - r, bend}, {c.x - r, top}, col, t);
  dl->AddLine({c.x + r, bend}, {c.x + r, top}, col, t);
  const ImU32 tip = IM_COL32(235, 70, 70, 255);
  dl->AddRectFilled({c.x - r - t * 0.5f, top - t * 0.1f}, {c.x - r + t * 0.5f, top + s * 0.12f}, tip);
  dl->AddRectFilled({c.x + r - t * 0.5f, top - t * 0.1f}, {c.x + r + t * 0.5f, top + s * 0.12f}, tip);
}

} // namespace

bool iconButton(const char *id, Icon icon, ImVec2 size) {
  bool pressed = ImGui::Button(id, size);
  ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
  ImVec2 c{(a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f};
  float s = std::min(b.x - a.x, b.y - a.y) * 0.95f;
  ImU32 col = ImGui::GetColorU32(ImGuiCol_Text);
  ImDrawList *dl = ImGui::GetWindowDrawList();
  if (icon == Icon::Hand) drawHand(dl, c, s, col);
  else drawMagnet(dl, c, s, col);
  return pressed;
}

} // namespace lumen
