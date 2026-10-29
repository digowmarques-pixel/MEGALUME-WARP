// "Saídas" window, modelled on Millumin's Output panel: a layout page (grid of
// projectors, display per projector, soft edge per seam) and a mapping page per
// output (slice of the canvas, warp, masks, colour).
#include "app.h"
#include "engine/outputs.h"
#include "ui/ui.h"

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace lumen {

namespace {

ImTextureID tex(const Texture &t) { return ImTextureID(intptr_t(t.id())); }

// Small "number [-][+]" field like Millumin's steppers.
bool stepper(const char *id, int &v, int lo, int hi) {
  ImGui::PushID(id);
  ImGui::SetNextItemWidth(90);
  bool changed = ImGui::InputInt("##v", &v, 1, 1);
  v = std::clamp(v, lo, hi);
  ImGui::PopID();
  return changed;
}

bool pxField(const char *id, int &v, int hi) {
  ImGui::PushID(id);
  ImGui::SetNextItemWidth(70);
  bool changed = ImGui::DragInt("##px", &v, 1.0f, 0, hi, T("%d px"));
  v = std::clamp(v, 0, hi);
  ImGui::PopID();
  return changed;
}

} // namespace

void Ui::outputsWindow(App &app) {
  if (!showOutputs_) return;
  Project &p = app.engine.project;
  ImGuiViewport *vp = ImGui::GetMainViewport();
  ImGui::SetNextWindowPos({vp->WorkPos.x + vp->WorkSize.x * 0.5f, vp->WorkPos.y + vp->WorkSize.y * 0.5f},
                          ImGuiCond_Appearing, {0.5f, 0.5f});
  ImGui::SetNextWindowSize({vp->WorkSize.x * 0.86f, vp->WorkSize.y * 0.86f}, ImGuiCond_Appearing);
  if (!ImGui::Begin(T("Saídas###outputs"), &showOutputs_, ImGuiWindowFlags_NoCollapse)) {
    ImGui::End();
    return;
  }

  OutputConfig *sel = p.output(outputsSel_);
  if (outputsPage_ == 1 && !sel) outputsPage_ = 0;

  // ---------------------------------------------------------------- left column
  ImGui::BeginChild("##left", {170, -ImGui::GetFrameHeightWithSpacing() * 2.6f}, ImGuiChildFlags_Borders);
  ImGui::TextDisabled("%s", T("Canvas"));
  ImGui::Text("%d × %d", p.canvasW, p.canvasH);
  ImGui::Separator();
  ImGui::TextDisabled("%s", T("Projetores"));
  for (size_t i = 0; i < p.outputs.size(); ++i) {
    auto &o = p.outputs[i];
    char label[160];
    std::snprintf(label, sizeof(label), T("%zu  %s"), i + 1, o.name.c_str());
    if (ImGui::Selectable(label, outputsSel_ == o.id)) outputsSel_ = o.id;
    if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
      outputsSel_ = o.id;
      outputsPage_ = 1;
    }
  }
  ImGui::EndChild();
  ImGui::SameLine();

  // ---------------------------------------------------------------- page
  ImGui::BeginChild("##page", {0, -ImGui::GetFrameHeightWithSpacing() * 2.6f}, ImGuiChildFlags_Borders);
  BlendLayoutParams &L = p.outputLayout;
  // Show every seam with its pixel value (layouts made from a percentage have none yet).
  for (int i = int(L.gapsX.size()); i < L.cols - 1; ++i) L.gapsX.push_back(L.gapX(i));
  for (int i = int(L.gapsY.size()); i < L.rows - 1; ++i) L.gapsY.push_back(L.gapY(i));
  auto apply = [&] {
    L.gapsX.resize(size_t(std::max(0, L.cols - 1)), 0);
    L.gapsY.resize(size_t(std::max(0, L.rows - 1)), 0);
    applyBlendLayout(p, L, -1, true); // keeps each output's display choice
    app.edited();
  };

  if (outputsPage_ == 0) {
    // Layout row: "layout [2] x [1] de [1920] px [1080] px"
    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled("%s", T("layout"));
    ImGui::SameLine();
    bool changed = stepper(T("cols"), L.cols, 1, 16);
    ImGui::SameLine();
    ImGui::TextDisabled("×");
    ImGui::SameLine();
    changed |= stepper(T("rows"), L.rows, 1, 16);
    ImGui::SameLine(0, 18);
    ImGui::TextDisabled("%s", T("de"));
    ImGui::SameLine();
    ImGui::SetNextItemWidth(90);
    changed |= ImGui::InputInt("##w", &L.projW, 0);
    ImGui::SameLine();
    ImGui::TextDisabled("×");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(90);
    changed |= ImGui::InputInt("##h", &L.projH, 0);
    ImGui::SameLine();
    ImGui::TextDisabled("%s", T("px por projetor"));
    L.projW = std::clamp(L.projW, 16, 16384);
    L.projH = std::clamp(L.projH, 16, 16384);
    if (changed || int(p.outputs.size()) != L.cols * L.rows) {
      if (changed) apply();
    }
    ImGui::SameLine(0, 30);
    if (sel && ImGui::Button(T("Mapping  >"))) outputsPage_ = 1;
    if (int(p.outputs.size()) != L.cols * L.rows) {
      ImGui::TextColored({1, 0.7f, 0.3f, 1}, T("O projeto tem %zu saídas e o layout pede %d."), p.outputs.size(), L.cols * L.rows);
      ImGui::SameLine();
      if (ImGui::SmallButton(T("Aplicar layout"))) apply();
    }
    ImGui::Spacing();

    // Grid of projector cards with soft-edge fields on the seams.
    auto mons = OutputManager::monitors();
    ImVec2 avail = ImGui::GetContentRegionAvail();
    // Outer soft edges around the grid (left, right, top, bottom), like Millumin.
    const float marginX = 110, marginY = 40;
    float gapW = 90, gapH = 34;
    float cardW = std::min(300.0f, (avail.x - 2 * marginX - gapW * float(L.cols - 1)) / float(L.cols));
    float cardH = std::min(cardW * 0.62f, (avail.y - 2 * marginY - gapH * float(L.rows - 1) - 20) / float(L.rows));
    cardW = std::max(cardW, 150.0f);
    cardH = std::max(cardH, 110.0f);
    ImVec2 origin = ImGui::GetCursorScreenPos();
    origin = {origin.x + marginX, origin.y + marginY};
    {
      float gridW = float(L.cols) * cardW + float(L.cols - 1) * gapW;
      float gridH = float(L.rows) * cardH + float(L.rows - 1) * gapH;
      auto outer = [&](const char *id, int &v, int hi, ImVec2 at) {
        ImGui::SetCursorScreenPos(at);
        ImGui::TextDisabled("%s", T("soft edge"));
        ImGui::SetCursorScreenPos({at.x, at.y + 18});
        if (pxField(id, v, hi)) apply();
        ImGui::SetItemTooltip("%s", T("Borda externa: a imagem escurece até o preto nesta largura "
                                      "(para emendar com um projetor de outro computador)."));
      };
      outer("eL", L.edgeLeft, L.projW * 9 / 10, {origin.x - marginX + 10, origin.y + gridH * 0.5f - 20});
      outer("eR", L.edgeRight, L.projW * 9 / 10, {origin.x + gridW + 12, origin.y + gridH * 0.5f + (L.rows > 1 ? 30.0f : -20.0f)});
      outer("eT", L.edgeTop, L.projH * 9 / 10, {origin.x + gridW * 0.5f - 35, origin.y - marginY});
      outer("eB", L.edgeBottom, L.projH * 9 / 10, {origin.x + gridW * 0.5f - 35 + (L.cols > 1 ? 0.0f : 0.0f), origin.y + gridH + 2});
    }
    ImDrawList *dl = ImGui::GetWindowDrawList();
    BlendLayout lay = computeBlendLayout(L);
    for (int r = 0; r < L.rows; ++r)
      for (int c = 0; c < L.cols; ++c) {
        size_t idx = size_t(r * L.cols + c);
        if (idx >= p.outputs.size()) continue;
        OutputConfig &o = p.outputs[idx];
        ImVec2 a{origin.x + float(c) * (cardW + gapW), origin.y + float(r) * (cardH + gapH)};
        ImVec2 z{a.x + cardW, a.y + cardH};
        bool selected = outputsSel_ == o.id;
        dl->AddRectFilled(a, z, selected ? IM_COL32(58, 52, 40, 255) : IM_COL32(44, 44, 48, 255), 6);
        dl->AddRect(a, z, selected ? IM_COL32(255, 196, 0, 255) : IM_COL32(70, 70, 76, 255), 6, 0, selected ? 2.0f : 1.0f);
        char num[24];
        std::snprintf(num, sizeof(num), T("%zu"), idx + 1);
        dl->AddText(nullptr, 42, {a.x + cardW * 0.5f - 14, a.y + 14}, IM_COL32(150, 150, 155, 255), num);
        ImGui::PushID(o.id);
        // Click on the card selects it; gear opens its mapping.
        ImGui::SetCursorScreenPos(a);
        ImGui::SetNextItemAllowOverlap(); // the Mapping button on top must get its clicks
        if (ImGui::InvisibleButton("##card", {cardW, cardH * 0.5f})) outputsSel_ = o.id;
        if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
          outputsSel_ = o.id;
          outputsPage_ = 1;
        }
        ImGui::SetCursorScreenPos({a.x + cardW - 84, a.y + 8});
        if (ImGui::Button(T("Mapping"), {76, 0})) {
          outputsSel_ = o.id;
          outputsPage_ = 1;
        }
        ImGui::SetItemTooltip("%s", T("Mapping desta saída"));
        // Display chooser.
        ImGui::SetCursorScreenPos({a.x + 12, a.y + cardH - 40});
        ImGui::SetNextItemWidth(cardW - 24);
        const char *noDisplay = app.outputs.virtualMonitors ? T("(monitor virtual)") : T("(janela)");
        std::string cur = o.monitor < 0 ? noDisplay : T("Tela ") + std::to_string(o.monitor + 1);
        if (o.monitor >= 0 && o.monitor < int(mons.size())) cur += " · " + mons[size_t(o.monitor)].name;
        if (ImGui::BeginCombo("##disp", cur.c_str())) {
          if (ImGui::Selectable(noDisplay, o.monitor < 0)) { o.monitor = -1; app.edited(); }
          for (size_t i = 0; i < mons.size(); ++i) {
            char label[200];
            std::snprintf(label, sizeof(label), T("Tela %zu · %s (%d×%d)"), i + 1, mons[i].name.c_str(), mons[i].w, mons[i].h);
            if (ImGui::Selectable(label, o.monitor == int(i))) { o.monitor = int(i); app.edited(); }
          }
          ImGui::EndCombo();
        }
        if (o.monitor >= 0 && o.monitor < int(mons.size()) &&
            (mons[size_t(o.monitor)].w != L.projW || mons[size_t(o.monitor)].h != L.projH))
          dl->AddText({a.x + 12, a.y + cardH - 62}, IM_COL32(255, 180, 80, 255), T("resolução diferente da tela"));
        else if (idx < lay.projectors.size()) {
          char info[64];
          std::snprintf(info, sizeof(info), T("canvas x %.0f  y %.0f"), lay.projectors[idx].srcX, lay.projectors[idx].srcY);
          dl->AddText({a.x + 12, a.y + cardH - 62}, IM_COL32(130, 130, 135, 255), info);
        }
        ImGui::PopID();

        // Soft edge field on the seam to the right (first row only: the seam is shared by the column).
        if (r == 0 && c + 1 < L.cols && size_t(c) < L.gapsX.size()) {
          ImGui::SetCursorScreenPos({z.x + 8, a.y + cardH * 0.5f - 22});
          ImGui::TextDisabled("%s", T("soft edge"));
          ImGui::SetCursorScreenPos({z.x + 8, a.y + cardH * 0.5f - 2});
          char id[16];
          std::snprintf(id, sizeof(id), T("gx%d"), c);
          if (pxField(id, L.gapsX[size_t(c)], L.projW * 9 / 10)) apply();
        }
        // Seam below (first column only).
        if (c == 0 && r + 1 < L.rows && size_t(r) < L.gapsY.size()) {
          ImGui::SetCursorScreenPos({a.x + 12, z.y + 6});
          ImGui::AlignTextToFramePadding();
          ImGui::TextDisabled("%s", T("soft edge"));
          ImGui::SameLine();
          char id[16];
          std::snprintf(id, sizeof(id), T("gy%d"), r);
          if (pxField(id, L.gapsY[size_t(r)], L.projH * 9 / 10)) apply();
        }
      }
    ImGui::SetCursorScreenPos({origin.x - marginX, origin.y + float(L.rows) * (cardH + gapH) + marginY});
    ImGui::Dummy({1, 1});
    ImGui::TextDisabled("%s", T("O soft edge entre dois projetores é a largura (em px) em que eles se sobrepõem. "
                        "Use o padrão Grade para alinhar e Branco para conferir a emenda."));
  } else if (sel) {
    // ---------------------------------------------------------------- mapping page
    if (ImGui::Button(T("<  Layout"))) outputsPage_ = 0;
    ImGui::SameLine(0, 20);
    ImGui::SetNextItemWidth(220);
    if (ImGui::BeginCombo("##which", sel->name.c_str())) {
      for (auto &o : p.outputs)
        if (ImGui::Selectable(o.name.c_str(), o.id == sel->id)) outputsSel_ = o.id;
      ImGui::EndCombo();
    }
    sel = p.output(outputsSel_);
    ImGui::SameLine(0, 30);
    const char *tabs[] = {T("Slice"), T("Mapping"), T("Máscaras"), T("Cor e blend")};
    for (int t = 0; t < 4; ++t) {
      if (t) ImGui::SameLine(0, 2);
      bool on = mappingTab_ == t;
      if (on) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(1.0f, 0.77f, 0.0f, 0.85f));
      if (ImGui::Button(tabs[t], {110, 0})) mappingTab_ = t;
      if (on) ImGui::PopStyleColor();
    }
    ImGui::Separator();
    if (!sel) {
      ImGui::EndChild();
      ImGui::End();
      return;
    }

    if (mappingTab_ == 0) {
      // Slice: the portion of the canvas this projector shows.
      ImGui::TextDisabled("%s", T("A fatia (slice) é a parte do canvas usada por este projetor. Arraste o retângulo ou os cantos."));
      // Warping (Millumin's): the slice is no longer a rectangle, its points move
      // over the canvas (the input side of this output's mesh).
      bool sliceWarp = !sel->warp.uvMesh.empty() && !sel->warp.uvIsIdentity();
      static bool sliceWarpOn = false;
      static int sliceWarpFor = -1; // per output: another one starts from its own state
      if (sliceWarpFor != sel->id) { sliceWarpFor = sel->id; sliceWarpOn = false; }
      sliceWarpOn = sliceWarpOn || sliceWarp;
      if (ImGui::Checkbox(T("warping##slice"), &sliceWarpOn)) {
        if (!sliceWarpOn) sel->warp.uvMesh.clear(); // back to the rectangle
        app.edited();
      }
      ImGui::SetItemTooltip("%s", T("Deforma a fatia: arraste os pontos sobre a imagem do canvas. "
                                    "Desligar volta ao retângulo."));
      ImVec2 av = ImGui::GetContentRegionAvail();
      av.y -= ImGui::GetFrameHeightWithSpacing() * 1.5f;
      float scw = float(p.canvasWidth(sel->canvasId)), sch = float(p.canvasHeight(sel->canvasId));
      // A window shrunk to nothing must not give a zero-sized (asserting) button.
      float k = std::max(0.01f, std::min(av.x / scw, av.y / sch));
      ImVec2 o0 = ImGui::GetCursorScreenPos();
      ImVec2 o1{o0.x + scw * k, o0.y + sch * k};
      ImDrawList *dl = ImGui::GetWindowDrawList();
      dl->AddImage(tex(app.engine.canvasTexture(sel->canvasId)), o0, o1, {0, 1}, {1, 0});
      dl->AddRect(o0, o1, IM_COL32(90, 90, 95, 255));
      // Other outputs of the same canvas, dimmed.
      for (auto &o : p.outputs) {
        if (o.id == sel->id || o.canvasId != sel->canvasId) continue;
        dl->AddRect({o0.x + o.srcX * k, o0.y + o.srcY * k}, {o0.x + (o.srcX + o.srcW) * k, o0.y + (o.srcY + o.srcH) * k},
                    IM_COL32(90, 160, 255, 90));
        dl->AddText({o0.x + o.srcX * k + 6, o0.y + o.srcY * k + 4}, IM_COL32(150, 190, 255, 200), o.name.c_str());
      }
      ImVec2 ra{o0.x + sel->srcX * k, o0.y + sel->srcY * k};
      ImVec2 rb{o0.x + (sel->srcX + sel->srcW) * k, o0.y + (sel->srcY + sel->srcH) * k};
      dl->AddRectFilled(ra, rb, IM_COL32(255, 196, 0, 40));
      dl->AddRect(ra, rb, IM_COL32(255, 196, 0, 255), 0, 0, 2);
      ImGui::InvisibleButton("##slice", {o1.x - o0.x, o1.y - o0.y});
      static int dragCorner = -2; // -2 none, -1 move, 0 tl, 1 br
      static int dragPoint = -1;  // warping: mesh point being dragged
      ImGuiIO &io = ImGui::GetIO();
      Warp &w = sel->warp;
      auto uvScreen = [&](Vec2 uv) { return ImVec2{ra.x + uv.x * (rb.x - ra.x), ra.y + uv.y * (rb.y - ra.y)}; };
      if (sliceWarpOn) {
        w.ensureUvMesh();
        for (int r = 0; r < w.meshRows; ++r)
          for (int c = 0; c < w.meshCols; ++c) {
            if (c + 1 < w.meshCols) dl->AddLine(uvScreen(w.uvAt(c, r)), uvScreen(w.uvAt(c + 1, r)), IM_COL32(255, 120, 0, 255), 2);
            if (r + 1 < w.meshRows) dl->AddLine(uvScreen(w.uvAt(c, r)), uvScreen(w.uvAt(c, r + 1)), IM_COL32(255, 120, 0, 255), 2);
          }
        for (auto &uv : w.uvMesh) dl->AddCircle(uvScreen(uv), 7, IM_COL32(255, 255, 255, 255), 0, 2);
        if (ImGui::IsItemClicked()) {
          dragPoint = -1;
          float best = 12;
          for (size_t i = 0; i < w.uvMesh.size(); ++i) {
            ImVec2 q = uvScreen(w.uvMesh[i]);
            float d = std::hypot(io.MousePos.x - q.x, io.MousePos.y - q.y);
            if (d < best) { best = d; dragPoint = int(i); }
          }
        }
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) dragPoint = -1;
        if (dragPoint >= 0 && dragPoint < int(w.uvMesh.size()) && ImGui::IsItemActive()) {
          float sw = std::max(1.0f, rb.x - ra.x), sh = std::max(1.0f, rb.y - ra.y);
          w.uvMesh[size_t(dragPoint)] = {std::clamp((io.MousePos.x - ra.x) / sw, 0.0f, 1.0f),
                                         std::clamp((io.MousePos.y - ra.y) / sh, 0.0f, 1.0f)};
          app.edited();
        }
      }
      if (ImGui::IsItemClicked() && dragPoint < 0) {
        auto near = [&](ImVec2 q) { return std::hypot(io.MousePos.x - q.x, io.MousePos.y - q.y) < 12; };
        dragCorner = near(ra) ? 0 : near(rb) ? 1
                   : (io.MousePos.x >= ra.x && io.MousePos.x <= rb.x && io.MousePos.y >= ra.y && io.MousePos.y <= rb.y) ? -1 : -2;
        if (dragCorner == -2) // a click on another projector's slice selects that projector
          for (auto &o : p.outputs) {
            if (o.id == sel->id || o.canvasId != sel->canvasId) continue;
            float cx = (io.MousePos.x - o0.x) / k, cy = (io.MousePos.y - o0.y) / k;
            if (cx >= o.srcX && cx <= o.srcX + o.srcW && cy >= o.srcY && cy <= o.srcY + o.srcH) {
              outputsSel_ = o.id;
              break;
            }
          }
      }
      if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) dragCorner = -2;
      if (dragCorner != -2 && ImGui::IsItemActive() && (io.MouseDelta.x != 0 || io.MouseDelta.y != 0)) {
        float dx = io.MouseDelta.x / k, dy = io.MouseDelta.y / k;
        if (dragCorner == -1) { sel->srcX += dx; sel->srcY += dy; }
        else if (dragCorner == 0) { sel->srcX += dx; sel->srcY += dy; sel->srcW -= dx; sel->srcH -= dy; }
        else { sel->srcW += dx; sel->srcH += dy; }
        sel->srcW = std::max(8.0f, sel->srcW);
        sel->srcH = std::max(8.0f, sel->srcH);
        app.edited();
      }
      for (ImVec2 q : {ra, rb}) dl->AddRectFilled({q.x - 5, q.y - 5}, {q.x + 5, q.y + 5}, IM_COL32(255, 196, 0, 255));
      float src[4] = {sel->srcX, sel->srcY, sel->srcW, sel->srcH};
      ImGui::SetNextItemWidth(360);
      if (ImGui::DragFloat4(T("x  y  largura  altura"), src, 1.0f)) {
        sel->srcX = src[0]; sel->srcY = src[1]; sel->srcW = std::max(1.0f, src[2]); sel->srcH = std::max(1.0f, src[3]);
        app.edited();
      }
      ImGui::SameLine();
      if (ImGui::Button(T("reset"))) apply();
      ImGui::SetItemTooltip("%s", T("Volta a fatia de todas as saídas ao cálculo do layout"));
      ImGui::SameLine();
      if (ImGui::Button(T("blend automático"))) {
        int n = autoBlendOverlaps(p, sel->canvasId);
        app.status(std::to_string(n) + T(" emenda(s) com soft edge calculado pelas sobreposições"));
        app.edited();
      }
      ImGui::SetItemTooltip("%s", T("Projetores fora de grade: as bordas suaves saem das sobreposições das fatias"));
    } else if (mappingTab_ == 1 || mappingTab_ == 2) {
      // Warp (corners / mesh) and masks, edited on the output preview.
      outputsWs_.viewOutputId = sel->id;
      outputsWs_.lockView = true;
      if (mappingTab_ == 1) {
        bool mesh = outputsWs_.tool == Tool::Mesh;
        if (ImGui::RadioButton(T("Cantos (perspectiva)"), !mesh)) outputsWs_.tool = Tool::Perspective;
        ImGui::SameLine();
        if (ImGui::RadioButton(T("Malha (warping)"), mesh)) outputsWs_.tool = Tool::Mesh;
        if (outputsWs_.tool != Tool::Mesh) outputsWs_.tool = Tool::Perspective;
        ImGui::SameLine(0, 20);
        int grid[2] = {sel->warp.meshCols, sel->warp.meshRows};
        ImGui::SetNextItemWidth(140);
        if (ImGui::InputInt2(T("malha"), grid)) {
          sel->warp.resampleMesh(grid[0], grid[1]); // keeps the current warp
          outputsWs_.tool = Tool::Mesh;
          app.edited();
        }
        ImGui::SameLine();
        if (ImGui::Button(T("entrada..."))) openInputMesh(-1, 0, sel->id);
        ImGui::SetItemTooltip("%s", T("Malha de entrada: põe os pontos sobre detalhes do canvas"));
        ImGui::SameLine();
        if (ImGui::Button(T("reset##warp"))) {
          sel->warp.setRect({0, 0}, {1, 1});
          sel->warp.resetMesh(2, 2);
          app.edited();
        }
      } else {
        outputsWs_.tool = Tool::Mask;
        ImGui::BeginChild("##masklist", {260, 0}, ImGuiChildFlags_Borders);
        outputMaskControls(app, sel, outputsWs_);
        ImGui::EndChild();
        ImGui::SameLine();
      }
      ImGui::BeginChild("##ws", {0, 0});
      outputsWs_.draw(app, ImGui::GetContentRegionAvail());
      ImGui::EndChild();
    } else {
      int pat = int(sel->pattern);
      ImGui::SetNextItemWidth(260);
      if (ImGui::Combo(T("Padrão de teste"), &pat, TZ("Nenhum (conteúdo)\0Test card da saída\0Grade de blend (contínua)\0Branco\0Preto\0"))) {
        sel->pattern = TestPattern(pat);
        app.edited();
      }
      softEdgeControls(app, sel);
      blackAndColorControls(app, sel);
    }
  }
  ImGui::EndChild();

  // ---------------------------------------------------------------- bottom bar
  bool open = app.outputsOpen;
  if (open) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.12f, 0.42f, 0.55f, 1));
  if (ImGui::Button(open ? T("Tela cheia: ligada") : T("Tela cheia"), {170, 30})) app.outputsOpen = !open;
  if (open) ImGui::PopStyleColor();
  ImGui::SetItemTooltip("%s", T("Abre as saídas nos projetores (Ctrl+P)"));
  ImGui::SameLine();
  bool card = !p.outputs.empty() && std::all_of(p.outputs.begin(), p.outputs.end(), [](auto &o) { return o.pattern == TestPattern::Card; });
  if (card) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(1.0f, 0.77f, 0.0f, 0.8f));
  if (ImGui::Button(T("Test Card"), {120, 30})) {
    for (auto &o : p.outputs) o.pattern = card ? TestPattern::None : TestPattern::Card;
    app.edited();
  }
  if (card) ImGui::PopStyleColor();
  ImGui::SameLine();
  bool grid = !p.outputs.empty() && std::all_of(p.outputs.begin(), p.outputs.end(), [](auto &o) { return o.pattern == TestPattern::BlendGrid; });
  if (grid) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(1.0f, 0.77f, 0.0f, 0.8f));
  if (ImGui::Button(T("Grade"), {90, 30})) {
    for (auto &o : p.outputs) o.pattern = grid ? TestPattern::None : TestPattern::BlendGrid;
    app.edited();
  }
  if (grid) ImGui::PopStyleColor();
  ImGui::SameLine();
  if (ImGui::Button(T("Identificar"), {110, 30})) app.outputs.identify();
  ImGui::SameLine(ImGui::GetWindowWidth() - 110);
  if (ImGui::Button(T("OK"), {90, 30})) showOutputs_ = false;
  ImGui::End();
}

} // namespace lumen
