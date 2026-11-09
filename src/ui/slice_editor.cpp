// Slice editor of the Outputs window, modelled on Resolume's Advanced Output:
// "Input Selection" (which part of the canvas each slice takes) and "Output
// Transformation" (where it goes on the projector). A stage that shows every
// slice of the output (click one to select it, drag it to move it), a toolbar
// (zoom, undo, hand, magnet, Transform / Edit Points) and a properties panel.
#include "app.h"
#include "core/expr.h"
#include "core/machine_settings.h"
#include "engine/outputs.h"
#include "ui/palette.h"
#include "ui/ui.h"
#include "ui/icons.h"
#include "platform/platform.h"

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace lumen {

namespace {

constexpr float kPi = 3.14159265f;
constexpr float kSquare = 7;  // half size of a square handle (screen px)
constexpr float kRing = 16;   // radius of a ring handle
constexpr float kBig = 11;    // half size of a perspective corner (Edit Points, output)
constexpr float kSnapPx = 10; // magnet distance (screen px)

ImTextureID texId(const Texture &t) { return ImTextureID(intptr_t(t.id())); }

// One slice of an output, wherever it is stored: k = 0 is the output's own
// (Slice 1), k > 0 its extra slice k - 1.
struct SliceRef {
  float &x, &y, &w, &h, &inRot, &outRot;
  Warp &warp;
  bool &flipX, &flipY;
  SliceTone &tone;
  bool *enabled;     // null: Slice 1 is always on
  std::string *name; // null: "Slice 1"
};

SliceRef sliceRef(OutputConfig &o, int k) {
  if (k <= 0 || k > int(o.slices.size()))
    return {o.srcX, o.srcY, o.srcW, o.srcH, o.inRotation, o.outRotation, o.warp, o.flipX, o.flipY, o.tone, nullptr, nullptr};
  OutputSlice &s = o.slices[size_t(k - 1)];
  return {s.srcX, s.srcY, s.srcW, s.srcH, s.inRotation, s.outRotation, s.warp, s.flipX, s.flipY, s.tone, &s.enabled, &s.name};
}

const char *sliceName(const OutputConfig &o, int k) {
  return k <= 0 || k > int(o.slices.size()) ? "Slice 1" : o.slices[size_t(k - 1)].name.c_str();
}

Vec2 turn(Vec2 v, float degrees) {
  float a = degrees * kPi / 180.0f, c = std::cos(a), s = std::sin(a);
  return {v.x * c - v.y * s, v.x * s + v.y * c};
}

// Point (u, v) of a slice's canvas region (turned by its rotation), canvas px.
Vec2 regionPoint(const SliceRef &s, float u, float v) {
  Vec2 c{s.x + s.w * 0.5f, s.y + s.h * 0.5f};
  return c + turn({(u - 0.5f) * s.w, (v - 0.5f) * s.h}, s.inRot);
}

// Outline of a warp (output 0..1), with its curved mesh edges.
std::vector<Vec2> outline(const Warp &w, int perEdge = 16) {
  std::vector<Vec2> pts;
  for (int i = 0; i < perEdge; ++i) pts.push_back(w.map(float(i) / float(perEdge), 0));
  for (int i = 0; i < perEdge; ++i) pts.push_back(w.map(1, float(i) / float(perEdge)));
  for (int i = 0; i < perEdge; ++i) pts.push_back(w.map(1 - float(i) / float(perEdge), 1));
  for (int i = 0; i < perEdge; ++i) pts.push_back(w.map(0, 1 - float(i) / float(perEdge)));
  return pts;
}

struct Box {
  float x0 = 1e30f, y0 = 1e30f, x1 = -1e30f, y1 = -1e30f;
  void add(Vec2 v) { x0 = std::min(x0, v.x); y0 = std::min(y0, v.y); x1 = std::max(x1, v.x); y1 = std::max(y1, v.y); }
  Vec2 center() const { return {(x0 + x1) * 0.5f, (y0 + y1) * 0.5f}; }
};

Box warpBox(const Warp &w) {
  Box b;
  for (const Vec2 &v : outline(w)) b.add(v);
  return b;
}

// The 8 handle positions of a box: corners tl tr br bl, then edges t r b l.
void boxHandles(const Box &b, Vec2 out[8]) {
  Vec2 c = b.center();
  out[0] = {b.x0, b.y0}; out[1] = {b.x1, b.y0}; out[2] = {b.x1, b.y1}; out[3] = {b.x0, b.y1};
  out[4] = {c.x, b.y0};  out[5] = {b.x1, c.y};  out[6] = {c.x, b.y1};  out[7] = {b.x0, c.y};
}

// Turns a warp around `pivot` keeping the projector's proportions (the warp
// is in 0..1 of a w x h output).
void turnWarp(Warp &warp, Vec2 pivot, float degrees, float w, float h) {
  warp.scaleAround({0, 0}, w, h);
  warp.rotateAround({pivot.x * w, pivot.y * h}, degrees * kPi / 180.0f);
  warp.scaleAround({0, 0}, 1 / w, 1 / h);
}

void squareHandle(ImDrawList *dl, ImVec2 p, bool hot, ImU32 col) {
  float r = hot ? kSquare + 1 : kSquare - 1;
  dl->AddRectFilled({p.x - r, p.y - r}, {p.x + r, p.y + r}, hot ? IM_COL32(255, 255, 255, 255) : IM_COL32(18, 18, 20, 235));
  dl->AddRect({p.x - r, p.y - r}, {p.x + r, p.y + r}, col, 0, 0, 1.6f);
}

// Perspective corner of Edit Points (Resolume's big corner square); the mesh
// corner point is the small square inside it.
void bigSquare(ImDrawList *dl, ImVec2 p, bool hot, ImU32 col) {
  float r = hot ? kBig + 1 : kBig;
  dl->AddRectFilled({p.x - r, p.y - r}, {p.x + r, p.y + r}, (col & 0x00FFFFFF) | (hot ? 0x60000000u : 0x26000000u));
  dl->AddRect({p.x - r, p.y - r}, {p.x + r, p.y + r}, hot ? IM_COL32(255, 255, 255, 255) : col, 0, 0, hot ? 2.5f : 2.0f);
}

void ringHandle(ImDrawList *dl, ImVec2 p, bool hot, ImU32 col) {
  dl->AddCircleFilled(p, kRing, (col & 0x00FFFFFF) | (hot ? 0x60000000u : 0x22000000u));
  dl->AddCircle(p, kRing, hot ? IM_COL32(255, 255, 255, 255) : col, 0, hot ? 3.0f : 2.0f);
}

// "Label   [ value ] [-] [+]" row of the properties panel. The value is typed
// (with arithmetic, as in Resolume: "1920/3", "/3", "+100") and applied when
// the field is left (Enter, Tab or a click elsewhere).
bool propRow(const char *label, float &v, float step, float /*speed*/, const char *fmt) {
  static ImGuiID editing = 0; // the field being typed in, and its text
  static char text[64];
  // ...and the value it belongs to: the fields of every slice share their ids,
  // and clicking another slice while typing applied the text to that one.
  static const float *editingValue = nullptr;
  ImGui::PushID(label);
  ImGui::AlignTextToFramePadding();
  ImGui::TextUnformatted(label);
  ImGui::SameLine(112);
  ImGui::SetNextItemWidth(std::max(60.0f, ImGui::GetContentRegionAvail().x - 54));
  const ImGuiID id = ImGui::GetID("##v");
  char shown[64];
  std::snprintf(shown, sizeof(shown), fmt, v);
  char *buf = editing == id ? text : shown;
  ImGui::InputText("##v", buf, sizeof(text), ImGuiInputTextFlags_AutoSelectAll);
  if (ImGui::IsItemActivated()) {
    editing = id;
    editingValue = &v;
    std::snprintf(text, sizeof(text), "%s", shown);
  }
  bool changed = false;
  if (ImGui::IsItemDeactivated() && editing == id) {
    if (ImGui::IsItemDeactivatedAfterEdit() && editingValue == &v) {
      std::string t;
      for (const char *c = text; *c; ++c)
        if (static_cast<unsigned char>(*c) < 0x80) t += *c; // drops "°" and other units
      double r;
      // Within a million (px, degrees, counts): 1e39 became infinity, which
      // was saved as null and made the project impossible to open.
      if (evalNumber(t, v, r) && std::isfinite(r) && std::fabs(r) <= 1e6) {
        v = float(r);
        changed = true;
      }
    }
    editing = 0;
  }
  ImGui::SetItemTooltip("%s", T("Aceita contas: 1920/3 · /3 · *2 · +100"));
  ImGui::SameLine(0, 2);
  if (ImGui::Button("-", {24, 0})) { v -= step; changed = true; }
  ImGui::SameLine(0, 2);
  if (ImGui::Button("+", {24, 0})) { v += step; changed = true; }
  ImGui::PopID();
  return changed;
}

bool toggleIcon(const char *id, Icon icon, bool on) {
  if (on) {
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.22f, 0.70f, 0.62f, 0.95f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.30f, 0.80f, 0.70f, 1));
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.03f, 0.08f, 0.07f, 1));
  }
  bool hit = iconButton(id, icon, {30, ImGui::GetFrameHeight()});
  if (on) ImGui::PopStyleColor(3);
  return hit;
}

bool toggleButton(const char *label, bool on, ImVec2 size = {0, 0}) {
  if (on) {
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.22f, 0.70f, 0.62f, 0.95f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.30f, 0.80f, 0.70f, 1));
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.03f, 0.08f, 0.07f, 1));
  }
  bool hit = ImGui::Button(label, size);
  if (on) ImGui::PopStyleColor(3);
  return hit;
}

// Size of an output in pixels: its monitor's, or its window's.
void outputSize(const OutputConfig &o, float &ow, float &oh) {
  ow = float(o.winW);
  oh = float(o.winH);
  auto mons = OutputManager::monitors();
  if (o.monitor >= 0 && o.monitor < int(mons.size())) {
    ow = float(mons[size_t(o.monitor)].w);
    oh = float(mons[size_t(o.monitor)].h);
  }
  ow = std::max(16.0f, ow);
  oh = std::max(16.0f, oh);
}

} // namespace

void Ui::matchOutputShape(App &app, OutputConfig &o, int k) {
  float ow, oh;
  outputSize(o, ow, oh);
  SliceRef s = sliceRef(o, k);
  Box b = warpBox(s.warp);
  s.x = b.x0 * ow;
  s.y = b.y0 * oh;
  s.w = std::max(1.0f, (b.x1 - b.x0) * ow);
  s.h = std::max(1.0f, (b.y1 - b.y0) * oh);
  s.inRot = 0;
  app.edited();
}

void Ui::sliceEditor(App &app, OutputConfig &o, bool out) {
  Project &p = app.engine.project;
  Engine &e = app.engine;
  ImGuiIO &io = ImGui::GetIO();
  const int side = out ? 1 : 0;
  StageView &view = stageView_[side];
  const int n = int(o.slices.size()) + 1;
  outputSlice_ = std::clamp(outputSlice_, -1, n - 1); // -1: the screen itself
  int &sel = outputSlice_;

  const float cw = float(p.canvasWidth(o.canvasId)), ch = float(p.canvasHeight(o.canvasId));
  float ow, oh;
  outputSize(o, ow, oh);
  const float contentW = out ? ow : cw, contentH = out ? oh : ch;
  bool &editPoints = stageEditPoints_[side];

  // ---------------------------------------------------------------- toolbar
  {
    char zl[32];
    if (view.zoom <= 0) std::snprintf(zl, sizeof(zl), "%s", T("Ajustar"));
    else std::snprintf(zl, sizeof(zl), "%.0f%%", view.zoom * 100);
    ImGui::SetNextItemWidth(96);
    if (ImGui::BeginCombo("##zoom", zl)) {
      if (ImGui::Selectable(T("Ajustar"), view.zoom <= 0)) view = {};
      for (float z : {0.25f, 0.5f, 0.75f, 1.0f, 1.5f, 2.0f}) {
        char l[16];
        std::snprintf(l, sizeof(l), "%.0f%%", z * 100);
        if (ImGui::Selectable(l, std::fabs(view.zoom - z) < 1e-3f)) {
          view.zoom = z;
          view.pan = {-1e9f, -1e9f}; // centred below
        }
      }
      ImGui::EndCombo();
    }
    ImGui::SetItemTooltip("%s", T("Zoom do palco (roda do mouse também; botão do meio ou a Mão arrastam)"));
    ImGui::SameLine();
    if (ImGui::Button(T("\xe2\x86\xb6##undo"))) pendingHistory_ = -1; // ↶ (done at the end of the frame)
    ImGui::SetItemTooltip("%s", T("Desfazer (Ctrl+Z)"));
    ImGui::SameLine(0, 2);
    if (ImGui::Button(T("\xe2\x86\xb7##redo"))) pendingHistory_ = 1; // ↷
    ImGui::SetItemTooltip("%s", T("Refazer (Ctrl+Shift+Z)"));
    ImGui::SameLine();
    if (toggleIcon("##stagehand", Icon::Hand, stageHand_)) stageHand_ = !stageHand_;
    ImGui::SetItemTooltip("%s", T("Arrastar a vista do palco"));
    ImGui::SameLine();
    if (toggleIcon("##stagemagnet", Icon::Magnet, workspace.snap)) {
      workspace.snap = !workspace.snap;
      setMachineSetting("snap", workspace.snap ? 1 : 0);
    }
    ImGui::SetItemTooltip("%s", T("Ímã: gruda nas bordas, no centro e nos outros slices (Ctrl inverte)"));
    float right = ImGui::GetContentRegionAvail().x;
    ImGui::SameLine(ImGui::GetCursorPosX() + std::max(0.0f, right - 250));
    if (toggleButton(T("+ Editar pontos"), editPoints, {120, 0})) editPoints = true;
    ImGui::SetItemTooltip("%s", out ? T("Quadrados grandes: perspectiva (cantos) · pequenos: pontos da malha · duplo clique adiciona linha e coluna")
                                    : T("Deforma a parte do canvas (warping de entrada): arraste os pontos"));
    ImGui::SameLine(0, 2);
    if (toggleButton(T("Transformar"), !editPoints, {120, 0})) editPoints = false;
    ImGui::SetItemTooltip("%s", T("Arraste o slice para mover · quadrados: tamanho (Shift: proporcional) · bolas: girar (Shift: de 15 em 15°)"));
  }

  // ---------------------------------------------------------------- stage
  const float panelW = 290;
  ImVec2 avail = ImGui::GetContentRegionAvail();
  ImGui::BeginChild("##stage", {std::max(100.0f, avail.x - panelW - 8), 0}, ImGuiChildFlags_Borders,
                    ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
  ImVec2 sp = ImGui::GetCursorScreenPos(), ss = ImGui::GetContentRegionAvail();
  ss.x = std::max(ss.x, 50.0f);
  ss.y = std::max(ss.y, 50.0f);
  ImGui::InvisibleButton("##stagebtn", ss, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonMiddle);
  const bool hovered = ImGui::IsItemHovered(), active = ImGui::IsItemActive();
  ImDrawList *dl = ImGui::GetWindowDrawList();
  dl->PushClipRect(sp, {sp.x + ss.x, sp.y + ss.y}, true);
  dl->AddRectFilled(sp, {sp.x + ss.x, sp.y + ss.y}, IM_COL32(40, 40, 44, 255));

  // View: fit, or a zoom with a pan (wheel zooms around the mouse).
  const float fitZ = std::max(0.01f, std::min((ss.x - 48) / contentW, (ss.y - 48) / contentH));
  float z = view.zoom > 0 ? view.zoom : fitZ;
  if (view.zoom > 0 && view.pan.x < -1e8f) view.pan = {(ss.x - contentW * z) * 0.5f, (ss.y - contentH * z) * 0.5f};
  ImVec2 origin = view.zoom > 0 ? ImVec2{sp.x + view.pan.x, sp.y + view.pan.y}
                                : ImVec2{sp.x + (ss.x - contentW * z) * 0.5f, sp.y + (ss.y - contentH * z) * 0.5f};
  if (hovered && io.MouseWheel != 0) {
    float nz = std::clamp(z * std::pow(1.15f, io.MouseWheel), 0.02f, 16.0f);
    ImVec2 m{io.MousePos.x - origin.x, io.MousePos.y - origin.y};
    view.zoom = nz;
    view.pan = {io.MousePos.x - sp.x - m.x * nz / z, io.MousePos.y - sp.y - m.y * nz / z};
    z = nz;
    origin = {sp.x + view.pan.x, sp.y + view.pan.y};
  }
  const bool panning = active && (ImGui::IsMouseDragging(ImGuiMouseButton_Middle, 0) ||
                                  (stageHand_ && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0)));
  if (panning) {
    if (view.zoom <= 0) {
      view.zoom = z;
      view.pan = {origin.x - sp.x, origin.y - sp.y};
    }
    view.pan.x += io.MouseDelta.x;
    view.pan.y += io.MouseDelta.y;
    origin = {sp.x + view.pan.x, sp.y + view.pan.y};
  }
  // Content: canvas px (input) or projector px (output).
  auto toScreen = [&](Vec2 v) { return ImVec2{origin.x + v.x * z, origin.y + v.y * z}; };
  auto toContent = [&](ImVec2 s) { return Vec2{(s.x - origin.x) / z, (s.y - origin.y) / z}; };
  auto outPx = [&](Vec2 n01) { return Vec2{n01.x * ow, n01.y * oh}; };
  const Vec2 mouse = toContent(io.MousePos);
  const ImVec2 cMax = toScreen({contentW, contentH});

  // Picture: the canvas (input) or the output as sent to the projector.
  if (!out) {
    dl->AddImage(texId(e.canvasTexture(o.canvasId)), origin, cMax, {0, 1}, {1, 0});
  } else {
    int pw = int(std::clamp(ow, 16.0f, 1920.0f)), ph = std::max(16, int(float(pw) * oh / ow));
    stagePreview_.resize(pw, ph);
    int index = 0;
    for (size_t i = 0; i < p.outputs.size(); ++i)
      if (p.outputs[i].id == o.id) index = int(i);
    app.outputs.renderPreview(p, o, index, e.canvasTexture(o.canvasId), stagePreview_, e.mesh, e.time(), false);
    dl->AddImage(texId(stagePreview_.texture()), origin, cMax, {0, 1}, {1, 0});
    // Output guide: a picture of the surface over the output, here only.
    // Decoded on another thread and at most 4096 px: a 100 MP venue plan, or
    // a picture on a network share that does not answer, froze every output
    // while it loaded (at each switch between two outputs, too).
    if (guideJob_.valid() && guideJob_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
      VideoFrame f = guideJob_.get();
      if (guideJobPath_ == o.guideImage && f.width > 0) {
        guideTex_ = std::make_unique<Texture>();
        guideTex_->upload(f.rgba().data(), f.width, f.height, f.stride[0]);
      }
    }
    if (guideLoaded_ != o.guideImage && !guideJob_.valid()) {
      guideLoaded_ = guideJobPath_ = o.guideImage;
      guideTex_.reset();
      if (!o.guideImage.empty())
        guideJob_ = std::async(std::launch::async, [path = o.guideImage] {
          VideoFrame f;
          if (!decodeStill(path, f) || f.width <= 0 || f.height <= 0) return VideoFrame{};
          const int k = std::max(1, (std::max(f.width, f.height) + 4095) / 4096);
          if (k > 1) { // an underlay: every k-th pixel is plenty
            VideoFrame s;
            s.width = f.width / k;
            s.height = f.height / k;
            s.stride[0] = s.width * 4;
            s.rgba().resize(size_t(s.width) * size_t(s.height) * 4);
            for (int y = 0; y < s.height; ++y)
              for (int x = 0; x < s.width; ++x)
                std::copy_n(&f.rgba()[size_t(y * k) * size_t(f.stride[0]) + size_t(x * k) * 4], 4,
                            &s.rgba()[(size_t(y) * size_t(s.width) + size_t(x)) * 4]);
            return s;
          }
          return f;
        });
    }
    if (!o.guideImage.empty() && guideTex_ && !guideTex_->empty())
      dl->AddImage(texId(*guideTex_), origin, cMax, {0, 0}, {1, 1},
                   IM_COL32(255, 255, 255, int(std::clamp(o.guideOpacity, 0.0f, 1.0f) * 255)));
  }
  dl->AddRect(origin, cMax, IM_COL32(120, 120, 128, 255));

  const ImU32 accent = pal::u32(pal::kCyan), dim = pal::u32(pal::kCyan, 120);
  // Shape of slice k on the stage (content coordinates).
  auto shape = [&](int k) {
    SliceRef s = sliceRef(o, k);
    std::vector<Vec2> pts;
    if (!out) {
      for (Vec2 uv : {Vec2{0, 0}, Vec2{1, 0}, Vec2{1, 1}, Vec2{0, 1}}) pts.push_back(regionPoint(s, uv.x, uv.y));
    } else {
      for (const Vec2 &v : outline(s.warp)) pts.push_back(outPx(v));
    }
    return pts;
  };
  auto drawShape = [&](const std::vector<Vec2> &pts, ImU32 col, float thick, bool fill) {
    std::vector<ImVec2> sp2;
    for (const Vec2 &v : pts) sp2.push_back(toScreen(v));
    if (fill) dl->AddConvexPolyFilled(sp2.data(), int(sp2.size()), (col & 0x00FFFFFF) | 0x26000000u);
    dl->AddPolyline(sp2.data(), int(sp2.size()), col, ImDrawFlags_Closed, thick);
  };
  // Input: the other projectors' slices of the same canvas, dimmed.
  if (!out)
    for (auto &other : p.outputs) {
      if (other.id == o.id || other.canvasId != o.canvasId) continue;
      SliceRef s = sliceRef(other, 0);
      std::vector<Vec2> pts;
      for (Vec2 uv : {Vec2{0, 0}, Vec2{1, 0}, Vec2{1, 1}, Vec2{0, 1}}) pts.push_back(regionPoint(s, uv.x, uv.y));
      drawShape(pts, IM_COL32(90, 140, 255, 90), 1, false);
      dl->AddText(toScreen(pts[0]), IM_COL32(150, 180, 255, 160), other.name.c_str());
    }
  for (int k = 0; k < n; ++k) {
    SliceRef s = sliceRef(o, k);
    bool on = !s.enabled || *s.enabled;
    std::vector<Vec2> pts = shape(k);
    drawShape(pts, k == sel ? accent : on ? dim : IM_COL32(120, 120, 120, 90), k == sel ? 2.0f : 1.0f, k == sel);
    Vec2 c{0, 0};
    for (const Vec2 &v : pts) c += v;
    c = c * (1.0f / float(std::max<size_t>(1, pts.size())));
    if (k == sel) { // only the selected slice is named (as in Resolume)
      const char *nm = sliceName(o, k);
      ImVec2 ts = ImGui::CalcTextSize(nm), cs = toScreen(c);
      dl->AddText({cs.x - ts.x * 0.5f, cs.y - ts.y * 0.5f}, accent, nm);
    }
  }

  // ---- the selected slice: handles, hover, drags
  const bool screenSel = sel < 0; // the screen is selected: no slice handles
  SliceRef s = sliceRef(o, std::max(sel, 0));
  auto dist = [&](ImVec2 a) { return std::hypot(io.MousePos.x - a.x, io.MousePos.y - a.y); };
  Vec2 handles[8];
  if (!out) {
    const Vec2 uvs[8] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}, {0.5f, 0}, {1, 0.5f}, {0.5f, 1}, {0, 0.5f}};
    for (int h = 0; h < 8; ++h) handles[h] = regionPoint(s, uvs[h].x, uvs[h].y);
  } else {
    Box b = warpBox(s.warp);
    boxHandles(b, handles);
    for (auto &h : handles) h = outPx(h);
  }
  // Edit Points: input warping points (inside the region) or the output's
  // perspective corners (rings) and mesh points (squares).
  std::vector<Vec2> points;   // squares / circles (content coordinates)
  std::vector<Vec2> corners4; // output perspective corners
  if (editPoints && !screenSel) {
    if (!out) {
      s.warp.ensureUvMesh();
      for (const Vec2 &uv : s.warp.uvMesh) points.push_back(regionPoint(s, uv.x, uv.y));
    } else {
      for (int r = 0; r < s.warp.meshRows; ++r)
        for (int c = 0; c < s.warp.meshCols; ++c) points.push_back(outPx(s.warp.meshPointDest(c, r)));
      for (const Vec2 &c : s.warp.corners) corners4.push_back(outPx(c));
    }
  }
  // The points chosen together belong to this slice, side and mesh size.
  {
    const std::string owner = std::to_string(o.id) + (out ? "o" : "i") + std::to_string(sel) + "/" + std::to_string(points.size());
    if (owner != stageSelOwner_ || !editPoints || screenSel) {
      stageSel_.clear();
      stageBand_ = false;
      stageSelOwner_ = owner;
    }
  }
  auto stageSelected = [&](int i) { return std::find(stageSel_.begin(), stageSel_.end(), i) != stageSel_.end(); };
  // What is under the mouse (when not already dragging).
  int hoverSquare = -1, hoverRing = -1, hoverPoint = -1, hoverCorner = -1, hoverSlice = -1;
  if (hovered && stageDrag_ == 0 && !stageHand_) {
    if (screenSel) {
      // only the slices themselves can be picked
    } else if (!editPoints) {
      // The corner squares sit on the rings: they keep a tight reach so the
      // ring around them stays easy to grab; the side squares reach further.
      float best = kSquare + 8;
      for (int h = 0; h < 8; ++h)
        if (float d = dist(toScreen(handles[h])); d < best && (h >= 4 || d < kSquare + 3)) { best = d; hoverSquare = h; }
      if (hoverSquare < 0)
        for (int h = 0; h < 4; ++h)
          if (dist(toScreen(handles[h])) < kRing + 6) { hoverRing = h; break; }
    } else {
      // 16 px of reach; on the output a point sitting on a perspective corner
      // keeps a tight one, so the corner around it can still be grabbed.
      float best = 16;
      for (size_t i = 0; i < points.size(); ++i) {
        const ImVec2 q = toScreen(points[i]);
        const float d = dist(q);
        bool onCorner = false;
        if (out)
          for (const Vec2 &c : corners4)
            if (const ImVec2 k = toScreen(c); std::hypot(q.x - k.x, q.y - k.y) < kBig) onCorner = true;
        if (d < best && (!onCorner || d < kSquare + 3)) { best = d; hoverPoint = int(i); }
      }
      if (hoverPoint < 0)
        for (size_t c = 0; c < corners4.size(); ++c)
          if (dist(toScreen(corners4[c])) < kBig + 8) { hoverCorner = int(c); break; }
    }
    if (hoverSquare < 0 && hoverRing < 0 && hoverPoint < 0 && hoverCorner < 0)
      for (int k = n - 1; k >= 0; --k) { // the top one (drawn last) first
        SliceRef sk = sliceRef(o, k);
        if (sk.enabled && !*sk.enabled && k != sel) continue;
        if (pointInPolygon(mouse, shape(k))) {
          hoverSlice = k;
          break;
        }
      }
  }

  // Start of a drag.
  // The second click of a double click adds mesh lines (below): it must not
  // start a drag first (that left the double click with nothing to do).
  const bool meshDouble = out && editPoints && !screenSel && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
  if (ImGui::IsItemClicked(ImGuiMouseButton_Left) && !stageHand_ && !meshDouble) {
    stageDrag_ = 0;
    stageGrab_ = {0, 0};
    if (hoverSquare >= 0) {
      stageDrag_ = 2;
      stageHandle_ = hoverSquare;
      stageGrab_ = handles[hoverSquare] - mouse;
    }
    else if (hoverRing >= 0) { stageDrag_ = 3; stageHandle_ = hoverRing; }
    else if (hoverPoint >= 0) {
      // Shift/Ctrl+click adds or removes the point; a plain click on a point
      // already chosen keeps them all (the drag moves them together).
      const bool add = io.KeyShift || io.KeyCtrl;
      if (stageSel_.empty() && add && stagePoint_ >= 0 && stagePoint_ != hoverPoint && stagePoint_ < int(points.size()))
        stageSel_.push_back(stagePoint_);
      if (add && stageSelected(hoverPoint)) {
        stageSel_.erase(std::find(stageSel_.begin(), stageSel_.end(), hoverPoint));
        stagePoint_ = stageSel_.empty() ? -1 : stageSel_.back();
      } else {
        if (add) stageSel_.push_back(hoverPoint);
        else if (!stageSelected(hoverPoint)) stageSel_ = {hoverPoint};
        stageDrag_ = 4;
        stagePoint_ = hoverPoint;
        if (stageSel_.size() <= 1) stageGrab_ = points[size_t(hoverPoint)] - mouse; // the group moves by the mouse's movement
      }
    } else if (hoverCorner >= 0) {
      stageDrag_ = 5;
      stageHandle_ = hoverCorner;
      stageGrab_ = corners4[size_t(hoverCorner)] - mouse;
    }
    else if (editPoints && !screenSel) {
      // Editing points: a drag on empty space selects the points in a box; a
      // click without dragging still selects the slice under it.
      stageBand_ = true;
      stageBandAdd_ = io.KeyShift || io.KeyCtrl;
      stageBandFrom_ = io.MousePos;
      stageBandSlice_ = hoverSlice;
    } else if (hoverSlice >= 0) {
      sel = hoverSlice; // click selects; dragging moves it
      stageDrag_ = 1;
    }
    SliceRef cur = sliceRef(o, sel);
    stageStartMouse_ = io.MousePos;
    stageStart_[0] = cur.x; stageStart_[1] = cur.y; stageStart_[2] = cur.w; stageStart_[3] = cur.h;
    stageStart_[4] = cur.inRot; stageStart_[5] = cur.outRot;
    stageStartWarp_ = cur.warp;
  }
  if (stageBand_) {
    const ImVec2 a = stageBandFrom_, b = io.MousePos;
    const ImVec2 lo{std::min(a.x, b.x), std::min(a.y, b.y)}, hi{std::max(a.x, b.x), std::max(a.y, b.y)};
    dl->AddRectFilled(lo, hi, IM_COL32(255, 160, 40, 35));
    dl->AddRect(lo, hi, IM_COL32(255, 160, 40, 220), 0, 0, 1.0f);
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
      stageBand_ = false;
      if (hi.x - lo.x < 3 && hi.y - lo.y < 3) { // a click: the slice under it, nothing selected
        if (!stageBandAdd_) {
          stageSel_.clear();
          stagePoint_ = -1;
        }
        if (stageBandSlice_ >= 0) sel = stageBandSlice_;
      } else {
        if (!stageBandAdd_) stageSel_.clear();
        for (size_t i = 0; i < points.size(); ++i) {
          ImVec2 q = toScreen(points[i]);
          if (q.x >= lo.x && q.x <= hi.x && q.y >= lo.y && q.y <= hi.y && !stageSelected(int(i))) stageSel_.push_back(int(i));
        }
        stagePoint_ = stageSel_.empty() ? -1 : stageSel_.back();
      }
    }
  }
  // Ctrl+A: every point of the slice being edited; Esc: none.
  if (hovered && editPoints && !screenSel && !io.WantTextInput) {
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_A, false)) {
      stageSel_.clear();
      for (size_t i = 0; i < points.size(); ++i) stageSel_.push_back(int(i));
      stagePoint_ = stageSel_.empty() ? -1 : stageSel_.back();
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
      stageSel_.clear();
      stagePoint_ = -1;
    }
  }
  if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) stageDrag_ = 0;

  // Magnet targets: canvas / projector edges and centre, the other slices.
  const bool magnet = workspace.snap != io.KeyCtrl;
  float guideX = NAN, guideY = NAN;
  std::vector<float> tx, ty;
  tx = {0, contentW * 0.5f, contentW};
  ty = {0, contentH * 0.5f, contentH};
  for (int k = 0; k < n; ++k) {
    if (k == sel) continue;
    Box b;
    for (const Vec2 &v : shape(k)) b.add(v);
    tx.insert(tx.end(), {b.x0, b.center().x, b.x1});
    ty.insert(ty.end(), {b.y0, b.center().y, b.y1});
  }
  if (!out)
    for (auto &other : p.outputs)
      if (other.id != o.id && other.canvasId == o.canvasId) {
        tx.insert(tx.end(), {other.srcX, other.srcX + other.srcW});
        ty.insert(ty.end(), {other.srcY, other.srcY + other.srcH});
      }
  const float thr = kSnapPx / z;
  // Snaps the nearest of `features` (x or y values) onto a target; returns the shift.
  auto snapShift = [&](std::initializer_list<float> features, const std::vector<float> &targets, float &guide) {
    if (!magnet) return 0.0f;
    float best = thr, shift = 0;
    for (float f : features)
      for (float t : targets)
        if (std::fabs(t - f) < best) { best = std::fabs(t - f); shift = t - f; guide = t; }
    return shift;
  };

  if (stageDrag_ != 0 && active && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0)) {
    SliceRef cur = sliceRef(o, sel);
    const float sx = stageStart_[0], sy = stageStart_[1], sw = stageStart_[2];
    const Vec2 delta{(io.MousePos.x - stageStartMouse_.x) / z, (io.MousePos.y - stageStartMouse_.y) / z};
    const Vec2 m = mouse + stageGrab_; // stageGrab_ is zero but for one handle or point
    if (!out) {
      const float H = stageStart_[3];
      const Vec2 c0{sx + sw * 0.5f, sy + H * 0.5f};
      if (stageDrag_ == 1) { // move
        float nx = sx + delta.x, ny = sy + delta.y;
        if (cur.inRot == 0) {
          nx += snapShift({nx, nx + sw * 0.5f, nx + sw}, tx, guideX);
          ny += snapShift({ny, ny + H * 0.5f, ny + H}, ty, guideY);
        }
        cur.x = nx;
        cur.y = ny;
      } else if (stageDrag_ == 2) { // resize in the region's own (turned) frame
        const int h = stageHandle_;
        const bool L = h == 0 || h == 3 || h == 7, R = h == 1 || h == 2 || h == 5;
        const bool T_ = h == 0 || h == 1 || h == 4, B = h == 2 || h == 3 || h == 6;
        Vec2 target = m;
        if (cur.inRot == 0) {
          if (L || R) target.x += snapShift({target.x}, tx, guideX);
          if (T_ || B) target.y += snapShift({target.y}, ty, guideY);
        }
        Vec2 lm = turn(target - c0, -stageStart_[4]); // mouse in the region's frame
        float l = -sw * 0.5f, r = sw * 0.5f, t = -H * 0.5f, b = H * 0.5f;
        if (L) l = std::min(lm.x, r - 4);
        if (R) r = std::max(lm.x, l + 4);
        if (T_) t = std::min(lm.y, b - 4);
        if (B) b = std::max(lm.y, t + 4);
        if (io.KeyShift && h < 4) { // proportional, from the opposite corner
          float k = std::max((r - l) / sw, (b - t) / H);
          if (L) l = r - sw * k; else r = l + sw * k;
          if (T_) t = b - H * k; else b = t + H * k;
        }
        Vec2 nc = c0 + turn({(l + r) * 0.5f, (t + b) * 0.5f}, stageStart_[4]);
        cur.w = r - l;
        cur.h = b - t;
        cur.x = nc.x - cur.w * 0.5f;
        cur.y = nc.y - cur.h * 0.5f;
      } else if (stageDrag_ == 3) { // turn around the centre
        Vec2 a0 = toContent(stageStartMouse_) - c0, a1 = m - c0;
        float deg = (std::atan2(a1.y, a1.x) - std::atan2(a0.y, a0.x)) * 180.0f / kPi;
        float rot = stageStart_[4] + deg;
        if (io.KeyShift) rot = std::round(rot / 15.0f) * 15.0f;
        // remainder, not a loop: a huge value typed in the field (1e10 or
        // more is a fixed point of rot - 360 in float) spun here forever.
        rot = std::isfinite(rot) ? std::remainder(rot, 360.0f) : 0.0f;
        cur.inRot = rot;
      } else if (stageDrag_ == 4 && stageSel_.size() > 1 && stageStartWarp_.uvMesh.size() == cur.warp.uvMesh.size()) {
        // The group: each point moves by the mouse's movement (in the region's own turn).
        Vec2 dm = turn(m - toContent(stageStartMouse_), -cur.inRot);
        Vec2 duv{dm.x / std::max(1.0f, cur.w), dm.y / std::max(1.0f, cur.h)};
        for (int i : stageSel_)
          if (i >= 0 && i < int(cur.warp.uvMesh.size()))
            cur.warp.uvMesh[size_t(i)] = {std::clamp(stageStartWarp_.uvMesh[size_t(i)].x + duv.x, 0.0f, 1.0f),
                                          std::clamp(stageStartWarp_.uvMesh[size_t(i)].y + duv.y, 0.0f, 1.0f)};
      } else if (stageDrag_ == 4 && stagePoint_ >= 0 && stagePoint_ < int(cur.warp.uvMesh.size())) {
        Vec2 lm = turn(m - c0, -cur.inRot);
        cur.warp.uvMesh[size_t(stagePoint_)] = {std::clamp(lm.x / std::max(1.0f, cur.w) + 0.5f, 0.0f, 1.0f),
                                                std::clamp(lm.y / std::max(1.0f, cur.h) + 0.5f, 0.0f, 1.0f)};
      }
    } else {
      cur.warp = stageStartWarp_;
      const Box b0 = warpBox(stageStartWarp_);
      if (stageDrag_ == 1) { // move (projector px -> 0..1)
        Vec2 d = delta;
        float x0 = b0.x0 * ow + d.x, x1 = b0.x1 * ow + d.x, y0 = b0.y0 * oh + d.y, y1 = b0.y1 * oh + d.y;
        d.x += snapShift({x0, (x0 + x1) * 0.5f, x1}, tx, guideX);
        d.y += snapShift({y0, (y0 + y1) * 0.5f, y1}, ty, guideY);
        cur.warp.translate({d.x / ow, d.y / oh});
      } else if (stageDrag_ == 2) { // scale the box from the opposite handle
        const int h = stageHandle_;
        Vec2 hp[8];
        boxHandles(b0, hp);
        const bool onX = h < 4 || h == 5 || h == 7, onY = h < 4 || h == 4 || h == 6;
        const Vec2 anchor = h < 4 ? hp[(h + 2) % 4] : hp[4 + (h - 2) % 4], from = hp[h];
        Vec2 target = m;
        if (onX) target.x += snapShift({target.x}, tx, guideX);
        if (onY) target.y += snapShift({target.y}, ty, guideY);
        Vec2 tn{target.x / ow, target.y / oh};
        float fx = onX && std::fabs(from.x - anchor.x) > 1e-5f ? (tn.x - anchor.x) / (from.x - anchor.x) : 1;
        float fy = onY && std::fabs(from.y - anchor.y) > 1e-5f ? (tn.y - anchor.y) / (from.y - anchor.y) : 1;
        if (io.KeyShift && h < 4) fx = fy = std::fabs(fx) > std::fabs(fy) ? fx : fy;
        auto notFlat = [](float f) { return std::fabs(f) < 0.02f ? (f < 0 ? -0.02f : 0.02f) : f; };
        cur.warp.scaleAround(anchor, notFlat(fx), notFlat(fy));
      } else if (stageDrag_ == 3) { // turn around the box centre
        Vec2 c = outPx(b0.center());
        Vec2 a0 = toContent(stageStartMouse_) - c, a1 = m - c;
        float deg = (std::atan2(a1.y, a1.x) - std::atan2(a0.y, a0.x)) * 180.0f / kPi;
        float total = stageStart_[5] + deg;
        if (io.KeyShift) total = std::round(total / 15.0f) * 15.0f;
        turnWarp(cur.warp, b0.center(), total - stageStart_[5], ow, oh);
        cur.outRot = total;
      } else if (stageDrag_ == 4 && stageSel_.size() > 1) {
        // The group: from where each point was when the drag started.
        const Vec2 d{delta.x / ow, delta.y / oh};
        for (int i : stageSel_)
          if (i >= 0 && i < int(cur.warp.mesh.size()))
            cur.warp.mesh[size_t(i)] =
                cur.warp.unmapPerspective(stageStartWarp_.meshPointDest(i % cur.warp.meshCols, i / cur.warp.meshCols) + d);
      } else if (stageDrag_ == 4 && stagePoint_ >= 0 && stagePoint_ < int(cur.warp.mesh.size())) {
        Vec2 target = m;
        target.x += snapShift({target.x}, tx, guideX);
        target.y += snapShift({target.y}, ty, guideY);
        cur.warp.mesh[size_t(stagePoint_)] = cur.warp.unmapPerspective({target.x / ow, target.y / oh});
      } else if (stageDrag_ == 5 && stageHandle_ >= 0 && stageHandle_ < 4) {
        Vec2 target = m;
        std::vector<float> cx = tx, cy = ty;
        for (int c = 0; c < 4; ++c)
          if (c != stageHandle_) {
            cx.push_back(stageStartWarp_.corners[size_t(c)].x * ow);
            cy.push_back(stageStartWarp_.corners[size_t(c)].y * oh);
          }
        target.x += snapShift({target.x}, cx, guideX);
        target.y += snapShift({target.y}, cy, guideY);
        cur.warp.corners[size_t(stageHandle_)] = {target.x / ow, target.y / oh};
      }
    }
    app.edited();
  }
  // Double click in Edit Points (output): a new row and column of mesh points.
  if (meshDouble && hovered && stageDrag_ == 0) {
    Vec2 uv;
    if (s.warp.unmap({mouse.x / ow, mouse.y / oh}, uv) && s.warp.insertLines(uv.x, uv.y)) app.edited();
  }
  // Arrow keys nudge the selected slice (Shift: 10 px).
  if (hovered && !io.WantTextInput && !screenSel) {
    float step = io.KeyShift ? 10.0f : 1.0f;
    Vec2 d{0, 0};
    if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow)) d.x -= step;
    if (ImGui::IsKeyPressed(ImGuiKey_RightArrow)) d.x += step;
    if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) d.y -= step;
    if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) d.y += step;
    if (d.x != 0 || d.y != 0) {
      SliceRef cur = sliceRef(o, sel);
      std::vector<int> pts = stageSel_;
      if (pts.empty() && editPoints && stagePoint_ >= 0) pts.push_back(stagePoint_);
      if (editPoints && !pts.empty()) { // the chosen points (Shift: 10 px)
        for (int i : pts) {
          if (out && i >= 0 && i < int(cur.warp.mesh.size())) {
            Vec2 at = cur.warp.meshPointDest(i % cur.warp.meshCols, i / cur.warp.meshCols);
            cur.warp.mesh[size_t(i)] = cur.warp.unmapPerspective({at.x + d.x / ow, at.y + d.y / oh});
          } else if (!out && i >= 0 && i < int(cur.warp.uvMesh.size())) {
            Vec2 du = turn(d, -cur.inRot);
            Vec2 &uv = cur.warp.uvMesh[size_t(i)];
            uv = {std::clamp(uv.x + du.x / std::max(1.0f, cur.w), 0.0f, 1.0f), std::clamp(uv.y + du.y / std::max(1.0f, cur.h), 0.0f, 1.0f)};
          }
        }
      } else if (!out) { cur.x += d.x; cur.y += d.y; }
      else cur.warp.translate({d.x / ow, d.y / oh});
      app.edited();
    }
  }

  // ---- draw the handles of the selected slice (after any change this frame)
  if (!screenSel) {
    SliceRef cur = sliceRef(o, sel);
    if (!editPoints) {
      Vec2 hp[8];
      if (!out) {
        const Vec2 uvs[8] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}, {0.5f, 0}, {1, 0.5f}, {0.5f, 1}, {0, 0.5f}};
        for (int h = 0; h < 8; ++h) hp[h] = regionPoint(cur, uvs[h].x, uvs[h].y);
      } else {
        boxHandles(warpBox(cur.warp), hp);
        for (auto &h : hp) h = outPx(h);
        Box b;
        for (int h = 0; h < 4; ++h) b.add(hp[h]);
        dl->AddRect(toScreen({b.x0, b.y0}), toScreen({b.x1, b.y1}), accent, 0, 0, 1.0f);
      }
      for (int h = 0; h < 4; ++h)
        ringHandle(dl, toScreen(hp[h]), h == hoverRing || (stageDrag_ == 3 && stageHandle_ == h), accent);
      for (int h = 0; h < 8; ++h)
        squareHandle(dl, toScreen(hp[h]), h == hoverSquare || (stageDrag_ == 2 && stageHandle_ == h), accent);
    } else if (!out) {
      cur.warp.ensureUvMesh();
      const Warp &w = cur.warp;
      auto at = [&](int c, int r) { Vec2 uv = w.uvMesh[size_t(r * w.meshCols + c)]; return toScreen(regionPoint(cur, uv.x, uv.y)); };
      for (int r = 0; r < w.meshRows; ++r)
        for (int c = 0; c < w.meshCols; ++c) {
          if (c + 1 < w.meshCols) dl->AddLine(at(c, r), at(c + 1, r), pal::u32(pal::kOrange), 1.5f);
          if (r + 1 < w.meshRows) dl->AddLine(at(c, r), at(c, r + 1), pal::u32(pal::kOrange), 1.5f);
        }
      for (int i = 0; i < int(w.uvMesh.size()); ++i) {
        bool hot = i == hoverPoint || stageSelected(i) || (stageDrag_ == 4 && stagePoint_ == i);
        ImVec2 q = at(i % w.meshCols, i / w.meshCols);
        dl->AddCircleFilled(q, hot ? 9.0f : 7.0f, hot ? IM_COL32(255, 255, 255, 255) : IM_COL32(20, 20, 20, 220));
        dl->AddCircle(q, hot ? 9.0f : 7.0f, pal::u32(pal::kOrange), 0, 2);
      }
    } else {
      const Warp &w = cur.warp;
      for (int r = 0; r < w.meshRows; ++r)
        for (int c = 0; c < w.meshCols; ++c) {
          if (c + 1 < w.meshCols) dl->AddLine(toScreen(outPx(w.meshPointDest(c, r))), toScreen(outPx(w.meshPointDest(c + 1, r))), dim, 1);
          if (r + 1 < w.meshRows) dl->AddLine(toScreen(outPx(w.meshPointDest(c, r))), toScreen(outPx(w.meshPointDest(c, r + 1))), dim, 1);
        }
      for (int c = 0; c < 4; ++c)
        bigSquare(dl, toScreen(outPx(w.corners[size_t(c)])), c == hoverCorner || (stageDrag_ == 5 && stageHandle_ == c), accent);
      for (int r = 0; r < w.meshRows; ++r)
        for (int c = 0; c < w.meshCols; ++c) {
          int i = r * w.meshCols + c;
          squareHandle(dl, toScreen(outPx(w.meshPointDest(c, r))),
                       i == hoverPoint || stageSelected(i) || (stageDrag_ == 4 && stagePoint_ == i), accent);
        }
    }
  }
  // Magnet guides.
  if (!std::isnan(guideX)) dl->AddLine(toScreen({guideX, 0}), toScreen({guideX, contentH}), pal::u32(pal::kYellow, 220), 1);
  if (!std::isnan(guideY)) dl->AddLine(toScreen({0, guideY}), toScreen({contentW, guideY}), pal::u32(pal::kYellow, 220), 1);
  // Cursor.
  if (stageHand_ || panning) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
  else if (hoverSquare >= 0 || stageDrag_ == 2)
    ImGui::SetMouseCursor((hoverSquare >= 0 ? hoverSquare : stageHandle_) % 4 == 1 || (hoverSquare >= 0 ? hoverSquare : stageHandle_) % 4 == 3
                              ? ((hoverSquare >= 0 ? hoverSquare : stageHandle_) < 4 ? ImGuiMouseCursor_ResizeNESW : ImGuiMouseCursor_ResizeEW)
                              : ((hoverSquare >= 0 ? hoverSquare : stageHandle_) < 4 ? ImGuiMouseCursor_ResizeNWSE : ImGuiMouseCursor_ResizeNS));
  else if (hoverRing >= 0 || hoverPoint >= 0 || hoverCorner >= 0 || stageDrag_ >= 3) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
  else if (hoverSlice >= 0 || stageDrag_ == 1) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
  // Hint at the bottom of the stage.
  {
    const char *hint = editPoints
        ? (out ? T("Editar pontos: quadrados grandes = perspectiva · pequenos = malha · duplo clique adiciona linha e coluna · Shift/Ctrl+clique ou área: vários pontos")
               : T("Editar pontos: arraste os pontos para deformar a parte do canvas"))
        : T("Clique num slice para selecionar e arraste para mover · quadrados: tamanho · bolas: girar · setas: 1 px");
    dl->AddText({sp.x + 8, sp.y + ss.y - ImGui::GetTextLineHeight() - 6}, IM_COL32(200, 200, 205, 200), hint);
  }
  dl->PopClipRect();
  ImGui::EndChild();

  ImGui::SameLine();
  ImGui::BeginChild("##sliceprops", {0, 0}, ImGuiChildFlags_Borders);
  slicePanel(app, o, out, ow, oh);
  ImGui::EndChild();
}

void Ui::slicePanel(App &app, OutputConfig &o, bool out, float ow, float oh) {
  if (outputSlice_ < 0) {
    screenPanel(app, o, ow, oh);
    return;
  }
  Project &p = app.engine.project;
  const int sel = std::clamp(outputSlice_, 0, int(o.slices.size()));
  SliceRef s = sliceRef(o, sel);
  auto header = [](const char *t) {
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.84f, 0.42f, 1));
    ImGui::SeparatorText(t);
    ImGui::PopStyleColor();
  };
  if (s.name) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%s", s.name->c_str());
    ImGui::SetNextItemWidth(-1);
    if (ImGui::InputText("##slname2", buf, sizeof(buf))) { *s.name = buf; app.edited(); }
    if (ImGui::Checkbox(T("ligada##slp"), s.enabled)) app.edited();
  } else {
    ImGui::TextUnformatted("Slice 1");
    ImGui::SameLine();
    ImGui::TextDisabled("%s", T("(leva o soft edge)"));
  }

  if (!out) {
    header(T("Entrada (parte do canvas)"));
    float cx = s.x + s.w * 0.5f, cy = s.y + s.h * 0.5f;
    if (propRow("X", cx, 1, 1, "%.0f")) { s.x = cx - s.w * 0.5f; app.edited(); }
    if (propRow("Y", cy, 1, 1, "%.0f")) { s.y = cy - s.h * 0.5f; app.edited(); }
    if (propRow(T("Esquerda"), s.x, 1, 1, "%.0f")) app.edited();
    if (propRow(T("Topo"), s.y, 1, 1, "%.0f")) app.edited();
    if (propRow(T("Largura"), s.w, 1, 1, "%.0f")) { s.w = std::max(1.0f, s.w); app.edited(); }
    if (propRow(T("Altura"), s.h, 1, 1, "%.0f")) { s.h = std::max(1.0f, s.h); app.edited(); }
    if (propRow(T("Rotação"), s.inRot, 1, 0.2f, "%.1f°")) app.edited();
    if (sel == 0) { // the projector's own slice: blend with the neighbours
      if (ImGui::Button(T("blend automático"))) {
        int seams = autoBlendOverlaps(p, o.canvasId);
        app.status(std::to_string(seams) + T(" emenda(s) com soft edge calculado pelas sobreposições"));
        app.edited();
      }
      ImGui::SetItemTooltip("%s", T("Projetores fora de grade: as bordas suaves saem das sobreposições das fatias"));
      ImGui::SameLine();
    }
    if (ImGui::Button(T("Igualar à saída"))) matchOutputShape(app, o, sel);
    ImGui::SetItemTooltip("%s", T("Match Output Shape: a parte do canvas fica com o tamanho e a posição que o slice tem no projetor (pixel a pixel)"));
    ImGui::SameLine();
    if (ImGui::Button(T("Canvas inteiro"))) {
      s.x = 0;
      s.y = 0;
      s.w = float(p.canvasWidth(o.canvasId));
      s.h = float(p.canvasHeight(o.canvasId));
      s.inRot = 0;
      app.edited();
    }
    header(T("Warping de entrada"));
    bool warped = !s.warp.uvMesh.empty() && !s.warp.uvIsIdentity();
    if (ImGui::Button(T("Editar pontos##in"))) stageEditPoints_[0] = true;
    ImGui::SameLine();
    ImGui::BeginDisabled(!warped);
    if (ImGui::Button(T("Desfazer warping"))) {
      s.warp.uvMesh.clear();
      app.edited();
    }
    ImGui::EndDisabled();
  } else {
    header(T("Saída (no projetor)"));
    Box b = warpBox(s.warp);
    float x = b.center().x * ow, y = b.center().y * oh, w = (b.x1 - b.x0) * ow, h = (b.y1 - b.y0) * oh;
    if (propRow("X", x, 1, 1, "%.0f")) { s.warp.translate({x / ow - b.center().x, 0}); app.edited(); }
    if (propRow("Y", y, 1, 1, "%.0f")) { s.warp.translate({0, y / oh - b.center().y}); app.edited(); }
    if (propRow(T("Largura"), w, 1, 1, "%.0f") && w > 1) {
      s.warp.scaleAround(b.center(), w / std::max(1.0f, (b.x1 - b.x0) * ow), 1);
      app.edited();
    }
    if (propRow(T("Altura"), h, 1, 1, "%.0f") && h > 1) {
      s.warp.scaleAround(b.center(), 1, h / std::max(1.0f, (b.y1 - b.y0) * oh));
      app.edited();
    }
    float rot = s.outRot;
    if (propRow(T("Rotação"), rot, 1, 0.2f, "%.1f°")) {
      turnWarp(s.warp, b.center(), rot - s.outRot, ow, oh);
      s.outRot = rot;
      app.edited();
    }
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(T("Espelhar"));
    ImGui::SameLine(112);
    if (toggleButton("\xe2\x86\x94", s.flipX, {40, 0})) { s.flipX = !s.flipX; app.edited(); } // ↔
    ImGui::SameLine(0, 2);
    if (toggleButton("\xe2\x86\x95", s.flipY, {40, 0})) { s.flipY = !s.flipY; app.edited(); } // ↕
    header(T("Cor do slice"));
    struct Row { const char *label; float *v; };
    Row rows[] = {{T("Brilho"), &s.tone.brightness}, {T("Contraste"), &s.tone.contrast},
                  {T("Vermelho"), &s.tone.red},      {T("Verde"), &s.tone.green}, {T("Azul"), &s.tone.blue}};
    for (auto &r : rows) {
      ImGui::PushID(r.label);
      ImGui::AlignTextToFramePadding();
      ImGui::TextUnformatted(r.label);
      ImGui::SameLine(112);
      ImGui::SetNextItemWidth(-1);
      if (ImGui::SliderFloat("##t", r.v, -1, 1, "%+.2f")) app.edited();
      if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) { *r.v = 0; app.edited(); }
      ImGui::PopID();
    }
    ImGui::TextDisabled("%s", T("Duplo clique zera"));
    header(T("Warping"));
    // Resolume's subdivisions: points added between the corners (0 = only the corners).
    float subX = float(s.warp.meshCols - 2), subY = float(s.warp.meshRows - 2);
    bool resample = propRow(T("Subdivisões X"), subX, 1, 1, "%.0f");
    resample |= propRow(T("Subdivisões Y"), subY, 1, 1, "%.0f");
    if (resample) {
      s.warp.resampleMesh(std::clamp(int(std::lround(subX)), 0, 30) + 2, std::clamp(int(std::lround(subY)), 0, 30) + 2);
      stageEditPoints_[1] = true;
      app.edited();
    }
    if (ImGui::Button(T("Editar pontos##out"))) stageEditPoints_[1] = true;
    ImGui::SameLine();
    if (ImGui::Button(T("Resetar warp"))) {
      s.warp.setRect({0, 0}, {1, 1});
      s.warp.resetMesh(2, 2);
      s.outRot = 0;
      app.edited();
    }
  }
}

} // namespace lumen

namespace lumen {

// The screen (projector) itself, as Resolume's "Screen 1" panel: device, size,
// colour of the whole output and the output guide.
void Ui::screenPanel(App &app, OutputConfig &o, float ow, float oh) {
  auto header = [](const char *t) {
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.84f, 0.42f, 1));
    ImGui::SeparatorText(t);
    ImGui::PopStyleColor();
  };
  char buf[96];
  std::snprintf(buf, sizeof(buf), "%s", o.name.c_str());
  ImGui::SetNextItemWidth(-1);
  if (ImGui::InputText("##scrname", buf, sizeof(buf))) { o.name = buf; app.edited(); }
  header(T("Tela"));
  auto mons = OutputManager::monitors();
  const char *noDisplay = app.outputs.virtualMonitors ? T("(monitor virtual)") : T("(janela)");
  std::string cur = o.monitor < 0 ? noDisplay : T("Tela ") + std::to_string(o.monitor + 1);
  if (o.monitor >= 0 && o.monitor < int(mons.size())) cur += " · " + mons[size_t(o.monitor)].name;
  ImGui::AlignTextToFramePadding();
  ImGui::TextUnformatted(T("Dispositivo"));
  ImGui::SameLine(112);
  ImGui::SetNextItemWidth(-1);
  if (ImGui::BeginCombo("##dev", cur.c_str())) {
    if (ImGui::Selectable(noDisplay, o.monitor < 0)) { o.monitor = -1; app.edited(); }
    for (size_t i = 0; i < mons.size(); ++i) {
      std::string l = T("Tela ") + std::to_string(i + 1) + " · " + mons[i].name + " (" + std::to_string(mons[i].w) +
                      "×" + std::to_string(mons[i].h) + ")";
      if (ImGui::Selectable(l.c_str(), o.monitor == int(i))) { o.monitor = int(i); app.edited(); }
    }
    ImGui::EndCombo();
  }
  if (o.monitor < 0) {
    float w = float(o.winW), h = float(o.winH);
    if (propRow(T("Largura"), w, 1, 1, "%.0f")) { o.winW = std::max(16, int(w)); app.edited(); }
    if (propRow(T("Altura"), h, 1, 1, "%.0f")) { o.winH = std::max(16, int(h)); app.edited(); }
  } else {
    ImGui::TextDisabled(T("Resolução: %.0f × %.0f (do monitor)"), ow, oh);
  }
  {
    float d = float(o.delayFrames);
    if (propRow(T("Atraso (quadros)"), d, 1, 1, "%.0f")) {
      o.delayFrames = std::clamp(int(std::lround(d)), 0, 30);
      app.edited();
    }
  }
  if (ImGui::Checkbox(T("Tela ligada"), &o.enabled)) app.edited(); // off: its window closes
  ImGui::SameLine();
  bool live = !o.muted;
  if (ImGui::Checkbox(T("Imagem"), &live)) { o.muted = !live; app.edited(); }
  ImGui::SetItemTooltip("%s", T("Desligada: a tela mostra preto, com a janela aberta (como a caixinha da lista)"));
  header(T("Cor da tela"));
  struct Row { const char *label; float *v; };
  Row rows[] = {{T("Opacidade"), &o.opacity}, {T("Brilho"), &o.color.brightness}, {T("Contraste"), &o.color.contrast},
                {T("Vermelho"), &o.color.gain.r},    {T("Verde"), &o.color.gain.g}, {T("Azul"), &o.color.gain.b}};
  for (auto &r : rows) {
    ImGui::PushID(r.label);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(r.label);
    ImGui::SameLine(112);
    ImGui::SetNextItemWidth(-1);
    if (ImGui::SliderFloat("##c", r.v, 0, r.v == &o.opacity ? 1.0f : 2.0f, "%.2f")) app.edited();
    if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) { *r.v = 1; app.edited(); }
    ImGui::PopID();
  }
  ImGui::TextDisabled("%s", T("Duplo clique volta a 1"));
  header(T("Guia de saída"));
  ImGui::TextWrapped("%s", T("Uma foto ou planta da superfície, por cima do palco da Transformação de saída "
                             "para encaixar os slices. Nunca vai para o projetor."));
  if (ImGui::Button(T("Carregar imagem..."))) {
    auto files = platform::openFileDialog(T("Guia de saída"), "Imagem|*.png;*.jpg;*.jpeg;*.bmp;*.tif;*.tiff;*.webp", false);
    if (!files.empty()) { o.guideImage = files[0]; app.edited(); }
  }
  if (!o.guideImage.empty()) {
    ImGui::SameLine();
    if (ImGui::Button(T("Remover##guide"))) { o.guideImage.clear(); app.edited(); }
    ImGui::TextDisabled("%s", platform::toUtf8(platform::toPath(o.guideImage).filename()).c_str());
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(T("Opacidade"));
    ImGui::SameLine(112);
    ImGui::SetNextItemWidth(-1);
    if (ImGui::SliderFloat("##gop", &o.guideOpacity, 0, 1, "%.2f")) app.edited();
  }
}

} // namespace lumen
