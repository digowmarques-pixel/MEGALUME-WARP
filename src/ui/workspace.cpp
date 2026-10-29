// Interactive canvas / output view with the videomapping tools.
#include "app.h"
#include "engine/compositor.h"
#include "ui/ui.h"

#include <imgui.h>

#include <algorithm>
#include <cmath>

namespace lumen {

namespace {

constexpr float kHandle = 7.0f;   // handle half-size (screen px)
constexpr float kPick = 10.0f;    // pick radius (screen px)

float dist(ImVec2 a, ImVec2 b) { return std::hypot(a.x - b.x, a.y - b.y); }

std::vector<Vec2> warpOutline(const Warp &w, int perEdge = 12) {
  std::vector<Vec2> pts;
  for (int i = 0; i < perEdge; ++i) pts.push_back(w.map(float(i) / float(perEdge), 0));
  for (int i = 0; i < perEdge; ++i) pts.push_back(w.map(1, float(i) / float(perEdge)));
  for (int i = 0; i < perEdge; ++i) pts.push_back(w.map(1 - float(i) / float(perEdge), 1));
  for (int i = 0; i < perEdge; ++i) pts.push_back(w.map(0, 1 - float(i) / float(perEdge)));
  return pts;
}

float distToSegment(Vec2 p, Vec2 a, Vec2 b) {
  Vec2 ab = b - a, ap = p - a;
  float len2 = ab.x * ab.x + ab.y * ab.y;
  float t = len2 > 0 ? std::clamp((ap.x * ab.x + ap.y * ab.y) / len2, 0.0f, 1.0f) : 0.0f;
  return (p - (a + ab * t)).length();
}

void handle(ImDrawList *dl, ImVec2 p, ImU32 col, bool selected, bool round = false) {
  float r = selected ? kHandle + 2 : kHandle;
  if (round) {
    dl->AddCircleFilled(p, r * 0.7f, selected ? IM_COL32(255, 255, 255, 255) : IM_COL32(20, 20, 20, 220));
    dl->AddCircle(p, r * 0.7f, col, 0, 2);
  } else {
    dl->AddRectFilled({p.x - r, p.y - r}, {p.x + r, p.y + r},
                      selected ? IM_COL32(255, 255, 255, 255) : IM_COL32(20, 20, 20, 220));
    dl->AddRect({p.x - r, p.y - r}, {p.x + r, p.y + r}, col, 0, 0, 2);
  }
}

} // namespace

bool Workspace::editMask(Mask &m, int &point, const std::function<ImVec2(Vec2)> &toScreen, Vec2 mouse,
                         float ppu, ImDrawList *dl, ImU32 col, int base) {
  ImGuiIO &io = ImGui::GetIO();
  bool hovered = ImGui::IsItemHovered();
  bool clicked = hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !io.KeyAlt;
  bool rclicked = hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right);
  // Alt+click (without dragging, Alt+drag pans) turns a point into a curve and back.
  bool altClick = hovered && io.KeyAlt && ImGui::IsMouseReleased(ImGuiMouseButton_Left) &&
                  io.MouseDragMaxDistanceSqr[0] < 9;
  bool changed = false;
  m.ensureTangents();
  size_t n = m.points.size();
  if (point >= int(n)) point = -1;

  // Handles of the selected curve point.
  int nearHandle = -1; // 0 in, 1 out
  if (point >= 0 && m.isBezier(size_t(point))) {
    Vec2 pt = m.points[size_t(point)];
    ImVec2 a = toScreen(pt + m.tanIn[size_t(point)]), b = toScreen(pt + m.tanOut[size_t(point)]), c = toScreen(pt);
    dl->AddLine(a, c, IM_COL32(255, 255, 255, 160));
    dl->AddLine(c, b, IM_COL32(255, 255, 255, 160));
    dl->AddCircleFilled(a, 4, IM_COL32(255, 255, 255, 230));
    dl->AddCircleFilled(b, 4, IM_COL32(255, 255, 255, 230));
    if (dist(io.MousePos, a) < kPick) nearHandle = 0;
    if (dist(io.MousePos, b) < kPick) nearHandle = 1;
  }
  for (size_t i = 0; i < n; ++i) handle(dl, toScreen(m.points[i]), col, point == int(i), !m.isBezier(i));

  int nearIdx = -1;
  float bestD = kPick;
  for (size_t i = 0; i < n; ++i) {
    float d = dist(io.MousePos, toScreen(m.points[i]));
    if (d < bestD) { bestD = d; nearIdx = int(i); }
  }
  if (altClick && nearIdx >= 0) {
    size_t i = size_t(nearIdx);
    if (m.isBezier(i)) {
      m.tanIn[i] = m.tanOut[i] = {};
    } else if (n >= 2) {
      Vec2 d = (m.points[(i + 1) % n] - m.points[(i + n - 1) % n]) * (1.0f / 6.0f);
      m.tanIn[i] = d * -1.0f;
      m.tanOut[i] = d;
    }
    point = nearIdx;
    changed = true;
  }
  if (rclicked && nearIdx >= 0) {
    m.points.erase(m.points.begin() + nearIdx);
    m.tanIn.erase(m.tanIn.begin() + nearIdx);
    m.tanOut.erase(m.tanOut.begin() + nearIdx);
    point = -1;
    changed = true;
  } else if (clicked) {
    if (nearHandle >= 0) {
      dragging_ = true;
      dragKind_ = base + 2 + nearHandle;
    } else if (nearIdx >= 0) {
      point = nearIdx;
      dragging_ = true;
      dragKind_ = base;
    } else if (io.KeyShift && n >= 3 && pointInPolygon(mouse, maskOutline(m))) {
      dragging_ = true;
      dragKind_ = base + 1;
      dragLast_ = mouse;
    } else {
      // Insert on the closest edge if the click is near one, else append.
      int at = int(n);
      if (n >= 3) {
        float best = kPick / ppu;
        for (size_t i = 0; i < n; ++i) {
          float d = distToSegment(mouse, m.points[i], m.points[(i + 1) % n]);
          if (d < best) { best = d; at = int(i) + 1; }
        }
      }
      m.points.insert(m.points.begin() + at, mouse);
      m.tanIn.insert(m.tanIn.begin() + at, Vec2{});
      m.tanOut.insert(m.tanOut.begin() + at, Vec2{});
      point = at;
      dragging_ = true;
      dragKind_ = base;
      changed = true;
    }
  }
  if (dragging_ && point >= 0 && point < int(m.points.size())) {
    size_t i = size_t(point);
    if (dragKind_ == base) {
      m.points[i] = mouse;
      changed = true;
    } else if (dragKind_ == base + 2 || dragKind_ == base + 3) {
      // Handles move symmetrically; Shift moves one alone (a sharp curve).
      Vec2 d = mouse - m.points[i];
      Vec2 &self = dragKind_ == base + 2 ? m.tanIn[i] : m.tanOut[i];
      Vec2 &other = dragKind_ == base + 2 ? m.tanOut[i] : m.tanIn[i];
      self = d;
      if (!io.KeyShift) other = d * -1.0f;
      changed = true;
    }
  }
  if (dragging_ && dragKind_ == base + 1 && (io.MouseDelta.x != 0 || io.MouseDelta.y != 0)) {
    // The move in the mask's own units (an output's 0..1 is not square on screen).
    Vec2 d = mouse - dragLast_;
    dragLast_ = mouse;
    for (auto &pt : m.points) pt += d;
    changed = true;
  }
  return changed;
}

void Workspace::fit(ImVec2 avail, float cw, float ch) {
  zoom_ = std::min(avail.x / cw, avail.y / ch) * 0.94f;
  pan_ = {(avail.x - cw * zoom_) * 0.5f, (avail.y - ch * zoom_) * 0.5f};
}

void Workspace::draw(App &app, ImVec2 size) {
  Engine &e = app.engine;
  Project &p = e.project;
  OutputConfig *viewOut = viewOutputId >= 0 ? p.output(viewOutputId) : nullptr;
  if (!viewOut) viewOutputId = -1;

  // ---- header: view selector
  ImGui::SetNextItemWidth(200);
  if (!p.hasCanvas(viewCanvas)) viewCanvas = 0;
  std::string viewName = viewOut ? T("Saída: ") + viewOut->name : p.canvasLabel(viewCanvas);
  bool refit = false;
  if (lockView && lastView_ != viewOutputId) refit = true; // switched output in the embedded view
  if (lockView) lastView_ = viewOutputId;
  if (lockView) {
    ImGui::TextDisabled("%s", viewName.c_str());
  } else if (ImGui::BeginCombo("##view", viewName.c_str())) {
    for (int id : p.canvasIds())
      if (ImGui::Selectable((p.canvasLabel(id) + "##cv" + std::to_string(id)).c_str(), !viewOut && viewCanvas == id)) {
        viewOutputId = -1;
        viewCanvas = id;
        refit = true;
      }
    ImGui::Separator();
    for (auto &o : p.outputs)
      if (ImGui::Selectable((T("Saída: ") + o.name).c_str(), viewOutputId == o.id)) {
        viewOutputId = o.id;
        app.sel.outputId = o.id;
        refit = true;
      }
    ImGui::EndCombo();
  }
  ImGui::SameLine();
  if (ImGui::Button(T("Enquadrar (F)"))) refit = true;
  ImGui::SameLine();
  ImGui::TextDisabled("%.0f%%", zoom_ * 100);
  ImGui::SameLine();
  const char *hints[] = {T("Clique para selecionar · arraste para mover a fatia (Shift: layer inteira)"),
                         T("Arraste os cantos para encaixar a imagem na superfície"),
                         T("Arraste os pontos da malha para curvar a imagem · duplo clique adiciona linha e coluna"),
                         T("Clique adiciona ponto · arraste move · botão direito apaga · Alt+clique: curva · Shift+arraste: máscara inteira"),
                         T("LED: clique numa fita · arraste os cantos ou o meio para posicionar sobre a imagem")};
  ImGui::TextDisabled("%s", hints[int(tool)]);
  viewOut = viewOutputId >= 0 ? p.output(viewOutputId) : nullptr;

  ImVec2 avail = ImGui::GetContentRegionAvail();
  if (avail.x < 50 || avail.y < 50) return;

  if (!viewOut && lastCanvas_ != viewCanvas) refit = true;
  lastCanvas_ = viewCanvas;
  float cw = float(p.canvasWidth(viewCanvas)), ch = float(p.canvasHeight(viewCanvas));
  if (viewOut) {
    auto mons = OutputManager::monitors();
    if (viewOut->monitor >= 0 && viewOut->monitor < int(mons.size())) {
      cw = float(mons[size_t(viewOut->monitor)].w);
      ch = float(mons[size_t(viewOut->monitor)].h);
    } else {
      cw = float(viewOut->winW);
      ch = float(viewOut->winH);
    }
  }
  if (!fitted_ || refit || fitRequested) {
    fit(avail, cw, ch);
    fitted_ = true;
    fitRequested = false;
  }

  ImVec2 base = ImGui::GetCursorScreenPos();
  ImGui::InvisibleButton("##ws", avail, ImGuiButtonFlags_MouseButtonLeft |
                                            ImGuiButtonFlags_MouseButtonRight |
                                            ImGuiButtonFlags_MouseButtonMiddle);
  bool hovered = ImGui::IsItemHovered();
  ImGuiIO &io = ImGui::GetIO();
  ImDrawList *dl = ImGui::GetWindowDrawList();
  dl->PushClipRect(base, {base.x + avail.x, base.y + avail.y}, true);
  dl->AddRectFilled(base, {base.x + avail.x, base.y + avail.y}, IM_COL32(14, 14, 16, 255));

  // ---- zoom & pan
  if (hovered && io.MouseWheel != 0) {
    ImVec2 m{io.MousePos.x - base.x, io.MousePos.y - base.y};
    float old = zoom_;
    zoom_ = std::clamp(zoom_ * std::pow(1.15f, io.MouseWheel), 0.02f, 20.0f);
    pan_.x = m.x - (m.x - pan_.x) * (zoom_ / old);
    pan_.y = m.y - (m.y - pan_.y) * (zoom_ / old);
  }
  bool panning = ImGui::IsMouseDragging(ImGuiMouseButton_Middle) ||
                 (io.KeyAlt && ImGui::IsMouseDragging(ImGuiMouseButton_Left));
  if (ImGui::IsItemActive() && panning) {
    pan_.x += io.MouseDelta.x;
    pan_.y += io.MouseDelta.y;
  }

  ImVec2 origin{base.x + pan_.x, base.y + pan_.y};
  ImVec2 contentMax{origin.x + cw * zoom_, origin.y + ch * zoom_};

  if (!viewOut) {
    dl->AddImage(ImTextureID(intptr_t(e.canvasTexture(viewCanvas).id())), origin, contentMax, {0, 1}, {1, 0});
    dl->AddRect(origin, contentMax, IM_COL32(90, 90, 95, 255));
    drawCanvasOverlay(app, dl, origin, zoom_);
  } else {
    int pw = std::clamp(int(cw), 16, 3840), ph = std::clamp(int(ch), 16, 2160);
    preview_.resize(pw, ph);
    int index = 0;
    for (size_t i = 0; i < p.outputs.size(); ++i)
      if (p.outputs[i].id == viewOut->id) index = int(i);
    app.outputs.renderPreview(p, *viewOut, index, e.canvasTexture(viewOut->canvasId), preview_, e.mesh, e.time(), false);
    dl->AddImage(ImTextureID(intptr_t(preview_.texture().id())), origin, contentMax, {0, 1}, {1, 0});
    dl->AddRect(origin, contentMax, IM_COL32(90, 90, 95, 255));
    drawOutputOverlay(app, dl, origin, {cw * zoom_, ch * zoom_});
  }
  dl->PopClipRect();
}

void Workspace::drawCanvasOverlay(App &app, ImDrawList *dl, ImVec2 origin, float scale) {
  Project &p = app.engine.project;
  ImGuiIO &io = ImGui::GetIO();
  auto toScreen = [&](Vec2 v) { return ImVec2{origin.x + v.x * scale, origin.y + v.y * scale}; };
  auto toCanvas = [&](ImVec2 s) { return Vec2{(s.x - origin.x) / scale, (s.y - origin.y) / scale}; };
  bool hovered = ImGui::IsItemHovered();
  bool clicked = hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !io.KeyAlt;
  ImVec2 mouse = io.MousePos;
  Vec2 mc = toCanvas(mouse);

  // Output regions (useful for multi-projector layouts).
  for (auto &o : p.outputs) {
    if (!o.enabled || o.canvasId != viewCanvas) continue;
    ImVec2 a = toScreen({o.srcX, o.srcY}), b = toScreen({o.srcX + o.srcW, o.srcY + o.srcH});
    dl->AddRect(a, b, IM_COL32(90, 160, 255, 110), 0, 0, 1.0f);
    dl->AddText({a.x + 4, a.y + 2}, IM_COL32(90, 160, 255, 160), o.name.c_str());
  }

  // Outlines of every slice; selected layer drawn on top.
  Layer *sel = p.layer(app.sel.layerId);
  if (sel && sel->canvasId != viewCanvas) sel = nullptr; // selected layer lives on another canvas
  for (auto it = p.layers.rbegin(); it != p.layers.rend(); ++it) {
    if (&*it == sel || !it->visible || it->canvasId != viewCanvas) continue;
    for (auto &s : it->slices) {
      auto pts = warpOutline(transformedWarp(*it, s));
      std::vector<ImVec2> sp;
      for (auto &v : pts) sp.push_back(toScreen(v));
      dl->AddPolyline(sp.data(), int(sp.size()), IM_COL32(255, 255, 255, 40), ImDrawFlags_Closed, 1);
    }
  }

  // Surfaces (shared mappings) in their colour, with their name.
  for (auto &sf : p.surfaces) {
    auto pts = warpOutline(sf.warp);
    std::vector<ImVec2> sp;
    for (auto &v : pts) sp.push_back(toScreen(v));
    ImU32 col = ImGui::GetColorU32(ImVec4(sf.color.r, sf.color.g, sf.color.b, 0.55f));
    dl->AddPolyline(sp.data(), int(sp.size()), col, ImDrawFlags_Closed, 1.5f);
    ImVec2 c = toScreen(sf.warp.center());
    dl->AddText({c.x - ImGui::CalcTextSize(sf.name.c_str()).x * 0.5f, c.y - 7}, col, sf.name.c_str());
  }

  // ---- end of drag
  if (dragging_ && !ImGui::IsMouseDown(ImGuiMouseButton_Left)) dragging_ = false;

  if (drawLedOverlay(app, dl, origin, scale)) return;

  // ---- selection by click (Select tool, or clicking outside the current layer)
  auto pickLayer = [&](Vec2 c, int &sliceOut) -> Layer * {
    for (auto &l : p.layers) { // front-most first
      if (!l.visible || l.canvasId != viewCanvas) continue;
      for (size_t si = 0; si < l.slices.size(); ++si)
        if (pointInPolygon(c, warpOutline(transformedWarp(l, l.slices[si])))) {
          sliceOut = int(si);
          return &l;
        }
    }
    return nullptr;
  };

  if (sel) {
    app.sel.slice = std::clamp(app.sel.slice, 0, int(sel->slices.size()) - 1);
    Slice &slice = sel->slices[size_t(app.sel.slice)];
    Warp tw = transformedWarp(*sel, slice);

    // Selected layer outlines.
    for (size_t si = 0; si < sel->slices.size(); ++si) {
      auto pts = warpOutline(transformedWarp(*sel, sel->slices[si]));
      std::vector<ImVec2> sp;
      for (auto &v : pts) sp.push_back(toScreen(v));
      bool active = int(si) == app.sel.slice;
      dl->AddPolyline(sp.data(), int(sp.size()), active ? IM_COL32(255, 196, 0, 255) : IM_COL32(255, 196, 0, 110),
                      ImDrawFlags_Closed, active ? 2.0f : 1.0f);
    }
    // Masks of the selected layer.
    for (size_t mi = 0; mi < sel->masks.size(); ++mi) {
      const Mask &m = sel->masks[mi];
      if (m.points.empty()) continue;
      std::vector<ImVec2> sp;
      for (auto &v : maskOutline(m)) sp.push_back(toScreen(v));
      bool active = int(mi) == app.sel.mask && tool == Tool::Mask;
      ImU32 col = m.subtract ? IM_COL32(255, 90, 90, active ? 255 : 120) : IM_COL32(80, 220, 255, active ? 255 : 120);
      dl->AddPolyline(sp.data(), int(sp.size()), col, ImDrawFlags_Closed, active ? 2.0f : 1.0f);
    }

    switch (tool) {
    case Tool::Led: break; // handled by drawLedOverlay
    case Tool::Select: {
      if (clicked) {
        int si = 0;
        if (Layer *hit = pickLayer(mc, si)) {
          app.sel.layerId = hit->id;
          app.sel.slice = si;
          app.sel.inspector = Inspector::Layer;
          dragging_ = true;
          dragKind_ = 3;
        }
      }
      if (dragging_ && dragKind_ == 3 && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0)) {
        Layer *l = p.layer(app.sel.layerId);
        if (l && (io.MouseDelta.x != 0 || io.MouseDelta.y != 0)) {
          Vec2 d = layerDelta(*l, {io.MouseDelta.x / scale, io.MouseDelta.y / scale});
          keepInPlace(*l, [&] {
            if (io.KeyShift)
              for (auto &sl : l->slices) sl.warp.translate(d);
            else
              l->slices[size_t(std::clamp(app.sel.slice, 0, int(l->slices.size()) - 1))].warp.translate(d);
          });
          app.edited();
        }
      }
      break;
    }
    case Tool::Perspective: {
      for (int c = 0; c < 4; ++c) handle(dl, toScreen(tw.corners[size_t(c)]), IM_COL32(255, 196, 0, 255), app.sel.corner == c);
      if (clicked) {
        int best = -1;
        float bestD = kPick;
        for (int c = 0; c < 4; ++c) {
          float d = dist(mouse, toScreen(tw.corners[size_t(c)]));
          if (d < bestD) { bestD = d; best = c; }
        }
        if (best >= 0) {
          app.sel.corner = best;
          dragging_ = true;
          dragKind_ = 1;
        } else if (pointInPolygon(mc, warpOutline(tw))) {
          dragging_ = true;
          dragKind_ = 3;
        } else {
          int si = 0;
          if (Layer *hit = pickLayer(mc, si)) {
            app.sel.layerId = hit->id;
            app.sel.slice = si;
          }
        }
      }
      if (dragging_ && dragKind_ == 1 && app.sel.corner >= 0) {
        keepInPlace(*sel, [&] { slice.warp.corners[size_t(app.sel.corner)] = untransform(*sel, mc); });
        app.edited();
      } else if (dragging_ && dragKind_ == 3 && (io.MouseDelta.x != 0 || io.MouseDelta.y != 0)) {
        Vec2 d = layerDelta(*sel, {io.MouseDelta.x / scale, io.MouseDelta.y / scale});
        keepInPlace(*sel, [&] { slice.warp.translate(d); });
        app.edited();
      }
      break;
    }
    case Tool::Mesh: {
      Warp &w = slice.warp;
      // Grid lines through the control points.
      for (int r = 0; r < w.meshRows; ++r)
        for (int c = 0; c + 1 < w.meshCols; ++c)
          dl->AddLine(toScreen(transformedWarp(*sel, slice).meshPointDest(c, r)),
                      toScreen(transformedWarp(*sel, slice).meshPointDest(c + 1, r)), IM_COL32(255, 196, 0, 90));
      for (int c = 0; c < w.meshCols; ++c)
        for (int r = 0; r + 1 < w.meshRows; ++r)
          dl->AddLine(toScreen(transformedWarp(*sel, slice).meshPointDest(c, r)),
                      toScreen(transformedWarp(*sel, slice).meshPointDest(c, r + 1)), IM_COL32(255, 196, 0, 90));
      for (int r = 0; r < w.meshRows; ++r)
        for (int c = 0; c < w.meshCols; ++c)
          handle(dl, toScreen(tw.meshPointDest(c, r)), IM_COL32(255, 196, 0, 255),
                 app.sel.meshPoint == r * w.meshCols + c, true);
      if (clicked) {
        int best = -1;
        float bestD = kPick;
        for (int r = 0; r < w.meshRows; ++r)
          for (int c = 0; c < w.meshCols; ++c) {
            float d = dist(mouse, toScreen(tw.meshPointDest(c, r)));
            if (d < bestD) { bestD = d; best = r * w.meshCols + c; }
          }
        if (best >= 0) {
          app.sel.meshPoint = best;
          dragging_ = true;
          dragKind_ = 2;
        }
      }
      if (dragging_ && dragKind_ == 2 && app.sel.meshPoint >= 0 && app.sel.meshPoint < int(w.mesh.size())) {
        w.mesh[size_t(app.sel.meshPoint)] = w.unmapPerspective(untransform(*sel, mc));
        app.edited();
      }
      // Double click: a new row and column of points through the click.
      if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
        Vec2 uv;
        if (w.unmap(untransform(*sel, mc), uv) && w.insertLines(uv.x, uv.y)) {
          dragging_ = false;
          app.sel.meshPoint = -1;
          app.edited();
        }
      }
      if (w.meshCols == 2 && w.meshRows == 2)
        dl->AddText({origin.x + 8, origin.y + 8}, IM_COL32(255, 255, 255, 200),
                    T("Duplo clique adiciona pontos, ou aumente a malha em Propriedades > Fatias"));
      break;
    }
    case Tool::Mask: {
      if (app.sel.mask < 0 || app.sel.mask >= int(sel->masks.size())) {
        if (clicked) { // first click creates a mask
          Mask m;
          m.name = T("Máscara ") + std::to_string(sel->masks.size() + 1);
          sel->masks.push_back(m);
          app.sel.mask = int(sel->masks.size()) - 1;
        } else {
          break;
        }
      }
      Mask &m = sel->masks[size_t(app.sel.mask)];
      if (editMask(m, app.sel.maskPoint, toScreen, mc, scale, dl, IM_COL32(80, 220, 255, 255), 20)) app.edited();
      break;
    }
    }
  } else if (clicked) {
    int si = 0;
    if (Layer *hit = pickLayer(mc, si)) {
      app.sel.layerId = hit->id;
      app.sel.slice = si;
      app.sel.inspector = Inspector::Layer;
    }
  }

  // Cursor position readout.
  if (hovered) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.0f, %.0f", mc.x, mc.y);
    ImVec2 br = ImGui::GetItemRectMax();
    dl->AddText({br.x - 90, br.y - 20}, IM_COL32(180, 180, 180, 200), buf);
  }
}

bool Workspace::drawLedOverlay(App &app, ImDrawList *dl, ImVec2 origin, float scale) {
  if (!showLed) {
    if (tool == Tool::Led) tool = Tool::Select;
    return false;
  }
  Project &p = app.engine.project;
  ImGuiIO &io = ImGui::GetIO();
  auto toScreen = [&](Vec2 v) { return ImVec2{origin.x + v.x * scale, origin.y + v.y * scale}; };
  Vec2 mc{(io.MousePos.x - origin.x) / scale, (io.MousePos.y - origin.y) / scale};
  bool ledTool = tool == Tool::Led;

  for (auto &m : p.ledMappings) {
    if (m.canvasId != viewCanvas) continue;
    bool selected = ledTool && m.id == app.sel.ledId;
    ImU32 col = !m.enabled ? IM_COL32(120, 120, 120, 120)
                           : selected ? IM_COL32(60, 230, 255, 255) : IM_COL32(60, 230, 255, ledTool ? 170 : 90);
    ImVec2 q[4];
    for (int i = 0; i < 4; ++i) q[i] = toScreen(m.area.corners[size_t(i)]);
    dl->AddPolyline(q, 4, col, ImDrawFlags_Closed, selected ? 2.0f : 1.0f);
    // One dot per LED, coloured with what is being sent.
    int cols = std::max(1, m.cols), rows = std::max(1, m.rows);
    const std::vector<float> *rgb = app.engine.leds.colours(m.id);
    if (cols * rows <= 4096) {
      Mat3 h = Mat3::squareToQuad(m.area.corners);
      float r = std::clamp(std::min(scale * 6.0f, 4.0f), 1.5f, 4.0f);
      for (int y = 0; y < rows; ++y)
        for (int x = 0; x < cols; ++x) {
          ImVec2 c = toScreen(h.apply({(float(x) + 0.5f) / float(cols), (float(y) + 0.5f) / float(rows)}));
          size_t i = size_t(y * cols + x) * 3;
          ImU32 fill = rgb && i + 2 < rgb->size()
                           ? IM_COL32(int((*rgb)[i] * 255), int((*rgb)[i + 1] * 255), int((*rgb)[i + 2] * 255), 255)
                           : IM_COL32(40, 40, 40, 255);
          dl->AddCircleFilled(c, r, fill);
          if (selected) dl->AddCircle(c, r, IM_COL32(0, 0, 0, 160));
        }
    }
    if (ledTool || selected)
      dl->AddText({q[0].x, q[0].y - 16}, col, m.name.c_str());
  }
  if (!ledTool) return false;

  bool hovered = ImGui::IsItemHovered();
  bool clicked = hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !io.KeyAlt;
  LedMapping *sel = p.ledMapping(app.sel.ledId);
  if (sel && !sel->locked)
    for (int c = 0; c < 4; ++c) handle(dl, toScreen(sel->area.corners[size_t(c)]), IM_COL32(60, 230, 255, 255), app.sel.corner == c);

  if (clicked) {
    int best = -1;
    float bestD = kPick;
    if (sel && !sel->locked)
      for (int c = 0; c < 4; ++c) {
        float d = dist(io.MousePos, toScreen(sel->area.corners[size_t(c)]));
        if (d < bestD) { bestD = d; best = c; }
      }
    if (best >= 0) {
      app.sel.corner = best;
      dragging_ = true;
      dragKind_ = 8;
    } else {
      // Pick the top-most mapping under the cursor (last drawn wins).
      LedMapping *hit = nullptr;
      for (auto &m : p.ledMappings) {
        if (m.canvasId != viewCanvas) continue;
        std::vector<Vec2> poly(m.area.corners.begin(), m.area.corners.end());
        if (pointInPolygon(mc, poly)) hit = &m;
      }
      if (hit) {
        app.sel.ledId = hit->id;
        app.sel.inspector = Inspector::Led;
        if (!hit->locked) { dragging_ = true; dragKind_ = 9; }
      }
    }
  }
  sel = p.ledMapping(app.sel.ledId);
  if (sel && !sel->locked && dragging_) {
    if (dragKind_ == 8 && app.sel.corner >= 0) {
      sel->area.corners[size_t(app.sel.corner)] = mc;
      app.edited();
    } else if (dragKind_ == 9 && (io.MouseDelta.x != 0 || io.MouseDelta.y != 0)) {
      sel->area.translate({io.MouseDelta.x / scale, io.MouseDelta.y / scale});
      app.edited();
    }
  }
  if (p.ledMappings.empty())
    dl->AddText({origin.x + 8, origin.y + 8}, IM_COL32(255, 255, 255, 220),
                T("Crie uma fita em Propriedades > LED"));
  return true;
}

void Workspace::drawOutputOverlay(App &app, ImDrawList *dl, ImVec2 origin, ImVec2 size) {
  OutputConfig *o = app.engine.project.output(viewOutputId);
  if (!o) return;
  ImGuiIO &io = ImGui::GetIO();
  auto toScreen = [&](Vec2 v) { return ImVec2{origin.x + v.x * size.x, origin.y + v.y * size.y}; };
  Vec2 mn{(io.MousePos.x - origin.x) / size.x, (io.MousePos.y - origin.y) / size.y};
  bool clicked = ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !io.KeyAlt;
  if (dragging_ && !ImGui::IsMouseDown(ImGuiMouseButton_Left)) dragging_ = false;
  Warp &w = o->warp;

  auto pts = warpOutline(w);
  std::vector<ImVec2> sp;
  for (auto &v : pts) sp.push_back(toScreen(v));
  dl->AddPolyline(sp.data(), int(sp.size()), IM_COL32(90, 160, 255, 255), ImDrawFlags_Closed, 2);

  // Output masks (projector space, 0..1).
  for (size_t mi = 0; mi < o->masks.size(); ++mi) {
    const Mask &m = o->masks[mi];
    if (m.points.empty()) continue;
    std::vector<ImVec2> sp;
    for (auto &v : maskOutline(m)) sp.push_back(toScreen(v));
    bool active = int(mi) == app.sel.outMask && tool == Tool::Mask;
    ImU32 col = m.subtract ? IM_COL32(255, 90, 90, active ? 255 : 130) : IM_COL32(80, 220, 255, active ? 255 : 130);
    dl->AddPolyline(sp.data(), int(sp.size()), col, ImDrawFlags_Closed, active ? 2.0f : 1.0f);
  }

  if (tool == Tool::Mask) {
    if (app.sel.outMask < 0 || app.sel.outMask >= int(o->masks.size())) {
      if (!clicked) return;
      Mask m;
      m.name = T("Máscara ") + std::to_string(o->masks.size() + 1);
      m.subtract = true;
      m.feather = 20;
      o->masks.push_back(m);
      app.sel.outMask = int(o->masks.size()) - 1;
    }
    Mask &m = o->masks[size_t(app.sel.outMask)];
    if (editMask(m, app.sel.outMaskPoint, toScreen, mn, std::max(1.0f, size.x), dl, IM_COL32(255, 120, 120, 255), 30))
      app.edited();
    dl->AddText({origin.x + 8, origin.y + 8}, IM_COL32(255, 255, 255, 220),
                T("Máscara da saída: clique adiciona ponto, arraste move, botão direito apaga, Alt+clique faz curva."));
    return;
  }

  if (tool == Tool::Mesh) {
    for (int r = 0; r < w.meshRows; ++r)
      for (int c = 0; c < w.meshCols; ++c)
        handle(dl, toScreen(w.meshPointDest(c, r)), IM_COL32(90, 160, 255, 255),
               app.sel.meshPoint == r * w.meshCols + c, true);
    if (clicked) {
      int best = -1;
      float bestD = kPick;
      for (int r = 0; r < w.meshRows; ++r)
        for (int c = 0; c < w.meshCols; ++c) {
          float d = dist(io.MousePos, toScreen(w.meshPointDest(c, r)));
          if (d < bestD) { bestD = d; best = r * w.meshCols + c; }
        }
      if (best >= 0) { app.sel.meshPoint = best; dragging_ = true; dragKind_ = 7; }
    }
    if (dragging_ && dragKind_ == 7 && app.sel.meshPoint >= 0 && app.sel.meshPoint < int(w.mesh.size())) {
      w.mesh[size_t(app.sel.meshPoint)] = w.unmapPerspective(mn);
      app.edited();
    }
    if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
      Vec2 uv;
      if (w.unmap(mn, uv) && w.insertLines(uv.x, uv.y)) {
        dragging_ = false;
        app.sel.meshPoint = -1;
        app.edited();
      }
    }
  } else {
    for (int c = 0; c < 4; ++c) handle(dl, toScreen(w.corners[size_t(c)]), IM_COL32(90, 160, 255, 255), app.sel.corner == c);
    if (clicked) {
      int best = -1;
      float bestD = kPick;
      for (int c = 0; c < 4; ++c) {
        float d = dist(io.MousePos, toScreen(w.corners[size_t(c)]));
        if (d < bestD) { bestD = d; best = c; }
      }
      if (best >= 0) { app.sel.corner = best; dragging_ = true; dragKind_ = 6; }
    }
    if (dragging_ && dragKind_ == 6 && app.sel.corner >= 0) {
      w.corners[size_t(app.sel.corner)] = mn;
      app.edited();
    }
  }
  dl->AddText({origin.x + 8, origin.y + 8}, IM_COL32(255, 255, 255, 220),
              T("Warp da saída: cantos (M) ou malha (G). Ajusta a projeção inteira."));
}

void Workspace::handleShortcuts(App &app) {
  ImGuiIO &io = ImGui::GetIO();
  if (io.WantTextInput || io.KeyCtrl) return;
  if (ImGui::IsKeyPressed(ImGuiKey_V, false)) tool = Tool::Select;
  if (ImGui::IsKeyPressed(ImGuiKey_M, false)) tool = Tool::Perspective;
  if (ImGui::IsKeyPressed(ImGuiKey_G, false)) tool = Tool::Mesh;
  if (ImGui::IsKeyPressed(ImGuiKey_K, false)) tool = Tool::Mask;
  if (showLed && ImGui::IsKeyPressed(ImGuiKey_L, false)) tool = Tool::Led;
  if (ImGui::IsKeyPressed(ImGuiKey_F, false)) fitRequested = true;

  Project &p = app.engine.project;
  OutputConfig *o = viewOutputId >= 0 ? p.output(viewOutputId) : nullptr;
  Layer *l = p.layer(app.sel.layerId);

  if (ImGui::IsKeyPressed(ImGuiKey_Tab)) {
    if (tool == Tool::Perspective || tool == Tool::Led) app.sel.corner = (std::max(app.sel.corner, -1) + 1) % 4;
    if (tool == Tool::Mesh) {
      int n = 0;
      if (o) n = int(o->warp.mesh.size());
      else if (l) n = int(l->slices[size_t(std::clamp(app.sel.slice, 0, int(l->slices.size()) - 1))].warp.mesh.size());
      if (n) app.sel.meshPoint = (std::max(app.sel.meshPoint, -1) + 1) % n;
    }
    if (tool == Tool::Mask && l && app.sel.mask >= 0 && app.sel.mask < int(l->masks.size())) {
      int n = int(l->masks[size_t(app.sel.mask)].points.size());
      if (n) app.sel.maskPoint = (std::max(app.sel.maskPoint, -1) + 1) % n;
    }
  }

  // Arrow keys nudge the selected handle.
  Vec2 d;
  float step = io.KeyShift ? 10.0f : 1.0f;
  if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow)) d.x -= step;
  if (ImGui::IsKeyPressed(ImGuiKey_RightArrow)) d.x += step;
  if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) d.y -= step;
  if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) d.y += step;
  bool del = ImGui::IsKeyPressed(ImGuiKey_Delete) || ImGui::IsKeyPressed(ImGuiKey_Backspace);
  if (d.x == 0 && d.y == 0 && !del) return;

  if (tool == Tool::Led) {
    LedMapping *m = p.ledMapping(app.sel.ledId);
    if (!m || m->locked || (d.x == 0 && d.y == 0)) return;
    if (app.sel.corner >= 0) m->area.corners[size_t(app.sel.corner)] += d;
    else m->area.translate(d);
    app.edited();
    return;
  }

  if (o) {
    Vec2 nd{d.x * 0.001f, d.y * 0.001f};
    if (tool == Tool::Perspective && app.sel.corner >= 0) o->warp.corners[size_t(app.sel.corner)] += nd;
    else if (tool == Tool::Mesh && app.sel.meshPoint >= 0 && app.sel.meshPoint < int(o->warp.mesh.size()))
      o->warp.mesh[size_t(app.sel.meshPoint)] += nd;
    else return;
    app.edited();
    return;
  }
  if (!l) return;
  Slice &s = l->slices[size_t(std::clamp(app.sel.slice, 0, int(l->slices.size()) - 1))];
  switch (tool) {
  case Tool::Led: break;
  case Tool::Select: keepInPlace(*l, [&] { s.warp.translate(layerDelta(*l, d)); }); break;
  case Tool::Perspective:
    if (app.sel.corner < 0) return;
    keepInPlace(*l, [&] { s.warp.corners[size_t(app.sel.corner)] += layerDelta(*l, d); });
    break;
  case Tool::Mesh: {
    if (app.sel.meshPoint < 0 || app.sel.meshPoint >= int(s.warp.mesh.size())) return;
    // Nudge in canvas pixels, converted back to the mesh's unit space.
    Vec2 cur = s.warp.meshPointDest(app.sel.meshPoint % s.warp.meshCols, app.sel.meshPoint / s.warp.meshCols);
    s.warp.mesh[size_t(app.sel.meshPoint)] = s.warp.unmapPerspective(cur + d);
    break;
  }
  case Tool::Mask: {
    if (app.sel.mask < 0 || app.sel.mask >= int(l->masks.size())) return;
    Mask &m = l->masks[size_t(app.sel.mask)];
    if (app.sel.maskPoint < 0 || app.sel.maskPoint >= int(m.points.size())) return;
    if (del) { // the curve handles go with their point, or the next points get them
      m.ensureTangents();
      m.points.erase(m.points.begin() + app.sel.maskPoint);
      m.tanIn.erase(m.tanIn.begin() + app.sel.maskPoint);
      m.tanOut.erase(m.tanOut.begin() + app.sel.maskPoint);
      app.sel.maskPoint = -1;
    } else {
      m.points[size_t(app.sel.maskPoint)] += d;
    }
    break;
  }
  }
  app.edited();
}

} // namespace lumen
