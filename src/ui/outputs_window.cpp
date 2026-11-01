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

// The slices of the selected output, listed under it in the left column (like
// Resolume's Advanced Output): Slice 1, Slice 2... and "+ Slice". The selected
// extra slice can be renamed, switched off or removed below the list.
void Ui::outputSliceBar(App &app, OutputConfig &o) {
  ImGui::Indent(14);
  for (int k = 0; k <= int(o.slices.size()); ++k) {
    ImGui::PushID(k);
    std::string nm = k == 0 ? std::string("Slice 1") : o.slices[size_t(k - 1)].name;
    if (k > 0 && !o.slices[size_t(k - 1)].enabled) nm += T(" (desligada)");
    bool on = outputSlice_ == k && outputsPage_ == 1 && mappingTab_ <= 1;
    if (on) ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.25f, 0.75f, 0.55f, 0.55f));
    if (ImGui::Selectable(nm.c_str(), on)) {
      outputSlice_ = k;
      outputsPage_ = 1;
      if (mappingTab_ >= 2) mappingTab_ = 1; // slices are edited in Slice and Mapping
    }
    if (on) ImGui::PopStyleColor();
    // Right click: duplicate, order (extra slices are drawn in list order, over Slice 1), remove.
    if (ImGui::BeginPopupContextItem("##slctx")) {
      if (ImGui::MenuItem(T("Duplicar"))) {
        OutputSlice copy;
        if (k == 0) {
          copy.srcX = o.srcX; copy.srcY = o.srcY; copy.srcW = o.srcW; copy.srcH = o.srcH;
          copy.warp = o.warp; copy.flipX = o.flipX; copy.flipY = o.flipY;
        } else {
          copy = o.slices[size_t(k - 1)];
        }
        copy.name = "Slice " + std::to_string(o.slices.size() + 2);
        copy.enabled = true;
        o.slices.insert(o.slices.begin() + k, copy); // right after the original
        outputSlice_ = k + 1;
        app.edited();
      }
      if (k > 0) {
        if (ImGui::MenuItem(T("Subir"), nullptr, false, k > 1)) {
          std::swap(o.slices[size_t(k - 1)], o.slices[size_t(k - 2)]);
          outputSlice_ = k - 1;
          app.edited();
        }
        if (ImGui::MenuItem(T("Descer"), nullptr, false, k < int(o.slices.size()))) {
          std::swap(o.slices[size_t(k - 1)], o.slices[size_t(k)]);
          outputSlice_ = k + 1;
          app.edited();
        }
        if (ImGui::MenuItem(T("Remover##slctx"))) {
          o.slices.erase(o.slices.begin() + (k - 1));
          outputSlice_ = 0;
          app.edited();
          ImGui::EndPopup();
          ImGui::PopID();
          break;
        }
      }
      ImGui::EndPopup();
    }
    ImGui::PopID();
  }
  if (ImGui::SmallButton(T("+ Slice"))) {
    OutputSlice sl;
    sl.name = "Slice " + std::to_string(o.slices.size() + 2);
    const auto &p = app.engine.project;
    sl.srcW = float(p.canvasWidth(o.canvasId));
    sl.srcH = float(p.canvasHeight(o.canvasId));
    sl.warp.setRect({0.25f, 0.25f}, {0.5f, 0.5f}); // in the middle of the projector, half size
    o.slices.push_back(sl);
    outputSlice_ = int(o.slices.size());
    outputsPage_ = 1;
    if (mappingTab_ >= 2) mappingTab_ = 0;
    app.edited();
  }
  ImGui::SetItemTooltip("%s", T("Mais uma parte do canvas neste projetor, com seu próprio warp (como as slices do Resolume)"));
  ImGui::Unindent(14);
  if (outputSlice_ > 0 && outputSlice_ <= int(o.slices.size())) {
    OutputSlice &sl = o.slices[size_t(outputSlice_ - 1)];
    ImGui::Spacing();
    char nameBuf[64];
    std::snprintf(nameBuf, sizeof(nameBuf), "%s", sl.name.c_str());
    ImGui::SetNextItemWidth(-1);
    if (ImGui::InputText("##slname", nameBuf, sizeof(nameBuf))) {
      sl.name = nameBuf;
      app.edited();
    }
    if (ImGui::Checkbox(T("ligada##slice"), &sl.enabled)) app.edited();
    ImGui::SameLine();
    if (ImGui::SmallButton(T("Remover##slice"))) {
      o.slices.erase(o.slices.begin() + (outputSlice_ - 1));
      outputSlice_ = 0;
      app.edited();
    }
  } else if (!o.slices.empty()) {
    ImGui::TextDisabled("%s", T("Slice 1 leva o soft edge"));
  }
}

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
  ImGui::BeginChild("##left", {190, -ImGui::GetFrameHeightWithSpacing() * 2.6f}, ImGuiChildFlags_Borders);
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
    if (outputsSel_ == o.id) {
      ImGui::PushID(o.id);
      if (outputSliceFor_ != o.id) {
        outputSliceFor_ = o.id;
        outputSlice_ = 0;
      }
      outputSliceBar(app, o);
      ImGui::PopID();
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

    // Several slices on this projector (Resolume's Advanced Output): the bar
    // picks the one the Slice and Mapping tabs edit.
    if (outputSliceFor_ != sel->id) {
      outputSliceFor_ = sel->id;
      outputSlice_ = 0;
    }
    if (mappingTab_ >= 2) outputSlice_ = 0; // masks and colour belong to the whole output
    else if (!sel->slices.empty()) {
      ImGui::TextDisabled(T("Editando: %s"), outputSlice_ == 0 ? "Slice 1"
                                              : sel->slices[size_t(std::min(outputSlice_, int(sel->slices.size())) - 1)].name.c_str());
    }
    outputSlice_ = std::clamp(outputSlice_, 0, int(sel->slices.size()));
    OutputSlice *swapped = outputSlice_ > 0 ? &sel->slices[size_t(outputSlice_ - 1)] : nullptr;
    auto swapSlice = [&] {
      std::swap(sel->srcX, swapped->srcX);
      std::swap(sel->srcY, swapped->srcY);
      std::swap(sel->srcW, swapped->srcW);
      std::swap(sel->srcH, swapped->srcH);
      std::swap(sel->warp, swapped->warp);
      std::swap(sel->flipX, swapped->flipX);
      std::swap(sel->flipY, swapped->flipY);
      sel->editSwap = sel->editSwap ? 0 : outputSlice_;
    };
    if (swapped) swapSlice();

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
      // A margin around the picture, so handles on the canvas edges can be grabbed whole.
      const float M = 18;
      float k = std::max(0.01f, std::min((av.x - 2 * M) / scw, (av.y - 2 * M) / sch));
      ImVec2 c0 = ImGui::GetCursorScreenPos();
      ImVec2 o0{c0.x + M + std::max(0.0f, (av.x - 2 * M - scw * k) * 0.5f), c0.y + M}; // centred in the page
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
      // The other slices of this output, dimmed (the first one is "Slice 1").
      for (size_t si = 0; si < sel->slices.size(); ++si) {
        const OutputSlice &os = sel->slices[si];
        if (int(si) + 1 == outputSlice_) continue; // it is the one being edited (swapped)
        const char *nm = (outputSlice_ > 0 && int(si) == outputSlice_ - 1) ? "Slice 1" : os.name.c_str();
        dl->AddRect({o0.x + os.srcX * k, o0.y + os.srcY * k}, {o0.x + (os.srcX + os.srcW) * k, o0.y + (os.srcY + os.srcH) * k},
                    IM_COL32(120, 230, 180, 120));
        dl->AddText({o0.x + os.srcX * k + 6, o0.y + os.srcY * k + 4}, IM_COL32(150, 240, 200, 210), nm);
      }
      ImVec2 ra{o0.x + sel->srcX * k, o0.y + sel->srcY * k};
      ImVec2 rb{o0.x + (sel->srcX + sel->srcW) * k, o0.y + (sel->srcY + sel->srcH) * k};
      dl->AddRectFilled(ra, rb, IM_COL32(255, 196, 0, 40));
      dl->AddRect(ra, rb, IM_COL32(255, 196, 0, 255), 0, 0, 2);
      ImGui::SetCursorScreenPos({o0.x - M, o0.y - M});
      ImGui::InvisibleButton("##slice", {o1.x - o0.x + 2 * M, o1.y - o0.y + 2 * M});
      static int dragCorner = -2; // -2 none, -1 move, 0..3 corners (tl tr br bl), 4..7 edges (t r b l)
      static int dragPoint = -1;  // warping: mesh point being dragged
      ImGuiIO &io = ImGui::GetIO();
      Warp &w = sel->warp;
      auto uvScreen = [&](Vec2 uv) { return ImVec2{ra.x + uv.x * (rb.x - ra.x), ra.y + uv.y * (rb.y - ra.y)}; };
      // Rectangle handles (squares): corners and edge middles. With warping on,
      // a mesh point on the same spot is the ring around the square (as in
      // Resolume): the square resizes the slice, the ring moves the point.
      const float off = 0.0f;
      const float mx = (ra.x + rb.x) * 0.5f, my = (ra.y + rb.y) * 0.5f;
      const ImVec2 hp[8] = {{ra.x - off, ra.y - off}, {rb.x + off, ra.y - off}, {rb.x + off, rb.y + off},
                            {ra.x - off, rb.y + off}, {mx, ra.y - off}, {rb.x + off, my},
                            {mx, rb.y + off}, {ra.x - off, my}};
      auto dist = [&](ImVec2 q) { return std::hypot(io.MousePos.x - q.x, io.MousePos.y - q.y); };
      const float squareR = 7, ringR = 16;
      // Rings: the warping points; with warping off, the 4 corners (dragging one
      // switches warping on). uv of the corners in uvMesh order is found by value.
      std::vector<Vec2> rings;
      if (sliceWarpOn) {
        w.ensureUvMesh();
        rings = w.uvMesh;
      } else {
        rings = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
      }
      // What is under the mouse: a square (its centre), else a ring, else the inside.
      int hoverHandle = -1, hoverPoint = -1;
      if (ImGui::IsItemHovered() || ImGui::IsItemActive()) {
        float best = squareR + 2;
        for (int h = 0; h < 8; ++h)
          if (float d = dist(hp[h]); d < (h >= 4 ? 12.0f : best)) { best = d; hoverHandle = h; }
        if (hoverHandle < 0) {
          float bestP = ringR + 2;
          for (size_t i = 0; i < rings.size(); ++i)
            if (float d = dist(uvScreen(rings[i])); d < bestP) { bestP = d; hoverPoint = int(i); }
        }
      }
      if (sliceWarpOn) {
        for (int r = 0; r < w.meshRows; ++r)
          for (int c = 0; c < w.meshCols; ++c) {
            if (c + 1 < w.meshCols) dl->AddLine(uvScreen(w.uvAt(c, r)), uvScreen(w.uvAt(c + 1, r)), IM_COL32(255, 120, 0, 255), 2);
            if (r + 1 < w.meshRows) dl->AddLine(uvScreen(w.uvAt(c, r)), uvScreen(w.uvAt(c, r + 1)), IM_COL32(255, 120, 0, 255), 2);
          }
      }
      for (size_t i = 0; i < rings.size(); ++i) {
        bool hot = int(i) == hoverPoint || (sliceWarpOn && int(i) == dragPoint);
        ImVec2 q = uvScreen(rings[i]);
        ImU32 tone = sliceWarpOn ? IM_COL32(255, 140, 40, 255) : IM_COL32(255, 196, 0, 255);
        dl->AddCircleFilled(q, ringR, hot ? IM_COL32(255, 160, 60, 110) : IM_COL32(255, 160, 60, 35));
        dl->AddCircle(q, ringR, hot ? IM_COL32(255, 255, 255, 255) : tone, 0, hot ? 3.0f : 2.0f);
      }
      if (hoverPoint >= 0)
        ImGui::SetItemTooltip("%s", T("Bola: deforma o canto (warping) · quadrado: muda o tamanho"));
      static float startRect[4];
      static ImVec2 startMouse;
      if (ImGui::IsItemClicked()) {
        if (hoverPoint >= 0 && !sliceWarpOn) { // a corner ring: warping starts at that corner
          Vec2 uv = rings[size_t(hoverPoint)];
          sliceWarpOn = true;
          w.ensureUvMesh();
          hoverPoint = -1;
          float bestUv = 1e9f;
          for (size_t i = 0; i < w.uvMesh.size(); ++i) {
            float d = std::fabs(w.uvMesh[i].x - uv.x) + std::fabs(w.uvMesh[i].y - uv.y);
            if (d < bestUv) { bestUv = d; hoverPoint = int(i); }
          }
        }
        dragPoint = hoverPoint;
        dragCorner = hoverHandle >= 0 ? hoverHandle
                   : hoverPoint >= 0 ? -2
                   : (io.MousePos.x >= ra.x && io.MousePos.x <= rb.x && io.MousePos.y >= ra.y && io.MousePos.y <= rb.y) ? -1 : -2;
        startRect[0] = sel->srcX; startRect[1] = sel->srcY; startRect[2] = sel->srcW; startRect[3] = sel->srcH;
        startMouse = io.MousePos;
        if (dragCorner == -2 && dragPoint < 0) // a click on another projector's slice selects that projector
          for (auto &o : p.outputs) {
            if (o.id == sel->id || o.canvasId != sel->canvasId) continue;
            float cx = (io.MousePos.x - o0.x) / k, cy = (io.MousePos.y - o0.y) / k;
            if (cx >= o.srcX && cx <= o.srcX + o.srcW && cy >= o.srcY && cy <= o.srcY + o.srcH) {
              outputsSel_ = o.id;
              break;
            }
          }
      }
      if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) { dragCorner = -2; dragPoint = -1; }
      // Cursor: arrows on a handle or point, a hand inside the slice.
      if (hoverHandle >= 0 || hoverPoint >= 0 || dragCorner >= 0 || dragPoint >= 0)
        ImGui::SetMouseCursor(hoverHandle == 0 || hoverHandle == 2 || dragCorner == 0 || dragCorner == 2 ? ImGuiMouseCursor_ResizeNWSE
                              : hoverHandle == 1 || hoverHandle == 3 || dragCorner == 1 || dragCorner == 3 ? ImGuiMouseCursor_ResizeNESW
                              : hoverHandle == 4 || hoverHandle == 6 || dragCorner == 4 || dragCorner == 6 ? ImGuiMouseCursor_ResizeNS
                              : hoverHandle == 5 || hoverHandle == 7 || dragCorner == 5 || dragCorner == 7 ? ImGuiMouseCursor_ResizeEW
                                                                                                            : ImGuiMouseCursor_ResizeAll);
      else if (ImGui::IsItemHovered() && io.MousePos.x >= ra.x && io.MousePos.x <= rb.x && io.MousePos.y >= ra.y && io.MousePos.y <= rb.y)
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
      if (dragPoint >= 0 && dragPoint < int(w.uvMesh.size()) && ImGui::IsItemActive()) {
        float sw = std::max(1.0f, rb.x - ra.x), sh = std::max(1.0f, rb.y - ra.y);
        w.uvMesh[size_t(dragPoint)] = {std::clamp((io.MousePos.x - ra.x) / sw, 0.0f, 1.0f),
                                       std::clamp((io.MousePos.y - ra.y) / sh, 0.0f, 1.0f)};
        app.edited();
      }
      if (dragCorner != -2 && ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0)) {
        // From where the drag started (no drift), canvas px.
        float dx = (io.MousePos.x - startMouse.x) / k, dy = (io.MousePos.y - startMouse.y) / k;
        float x0 = startRect[0], y0 = startRect[1], x1 = x0 + startRect[2], y1 = y0 + startRect[3];
        // Magnet: canvas edges and centre, the other projectors' slices.
        std::vector<float> tx = {0, scw * 0.5f, scw}, ty = {0, sch * 0.5f, sch};
        for (auto &o : p.outputs)
          if (o.id != sel->id && o.canvasId == sel->canvasId) {
            tx.insert(tx.end(), {o.srcX, o.srcX + o.srcW});
            ty.insert(ty.end(), {o.srcY, o.srcY + o.srcH});
          }
        const bool magnet = workspace.snap != io.KeyCtrl;
        auto snapTo = [&](float v, const std::vector<float> &ts) {
          if (!magnet) return v;
          float bestD = 10 / k, out = v;
          for (float t : ts)
            if (std::fabs(t - v) < bestD) { bestD = std::fabs(t - v); out = t; }
          return out;
        };
        const int h = dragCorner;
        if (h == -1) { // move: the nearest edge on each axis snaps
          float nx0 = x0 + dx, ny0 = y0 + dy, w0 = x1 - x0, h0 = y1 - y0;
          float sx0 = snapTo(nx0, tx), sx1 = snapTo(nx0 + w0, tx);
          nx0 = std::fabs(sx0 - nx0) <= std::fabs(sx1 - (nx0 + w0)) ? sx0 : sx1 - w0;
          float sy0 = snapTo(ny0, ty), sy1 = snapTo(ny0 + h0, ty);
          ny0 = std::fabs(sy0 - ny0) <= std::fabs(sy1 - (ny0 + h0)) ? sy0 : sy1 - h0;
          x0 = nx0; y0 = ny0; x1 = nx0 + w0; y1 = ny0 + h0;
        } else {
          const bool left = h == 0 || h == 3 || h == 7, right = h == 1 || h == 2 || h == 5;
          const bool top = h == 0 || h == 1 || h == 4, bottom = h == 2 || h == 3 || h == 6;
          if (left) x0 = std::min(snapTo(x0 + dx, tx), x1 - 8);
          if (right) x1 = std::max(snapTo(x1 + dx, tx), x0 + 8);
          if (top) y0 = std::min(snapTo(y0 + dy, ty), y1 - 8);
          if (bottom) y1 = std::max(snapTo(y1 + dy, ty), y0 + 8);
        }
        sel->srcX = x0; sel->srcY = y0; sel->srcW = x1 - x0; sel->srcH = y1 - y0;
        app.edited();
      }
      // Arrow keys nudge the slice 1 px (Shift: 10 px) while the mouse is over the picture.
      if (ImGui::IsItemHovered() && !io.WantTextInput) {
        float step = io.KeyShift ? 10.0f : 1.0f, nx = 0, ny = 0;
        if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow)) nx -= step;
        if (ImGui::IsKeyPressed(ImGuiKey_RightArrow)) nx += step;
        if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) ny -= step;
        if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) ny += step;
        if (nx != 0 || ny != 0) {
          sel->srcX += nx;
          sel->srcY += ny;
          app.edited();
        }
      }
      for (int hIdx = 0; hIdx < 8; ++hIdx) {
        bool hot = hIdx == hoverHandle || hIdx == dragCorner;
        float r = hot ? squareR + 1 : squareR - 1;
        ImVec2 q = hp[hIdx];
        dl->AddRectFilled({q.x - r, q.y - r}, {q.x + r, q.y + r}, hot ? IM_COL32(255, 255, 255, 255) : IM_COL32(255, 196, 0, 255), 2);
        dl->AddRect({q.x - r, q.y - r}, {q.x + r, q.y + r}, IM_COL32(20, 20, 20, 255), 2, 0, 1.5f);
      }
      ImGui::SetCursorScreenPos({o0.x - M, o1.y + M + 2});
      float src[4] = {sel->srcX, sel->srcY, sel->srcW, sel->srcH};
      ImGui::SetNextItemWidth(360);
      if (ImGui::DragFloat4(T("x  y  largura  altura"), src, 1.0f)) {
        sel->srcX = src[0]; sel->srcY = src[1]; sel->srcW = std::max(1.0f, src[2]); sel->srcH = std::max(1.0f, src[3]);
        app.edited();
      }
      // Layout and blend work on the projectors' first slices only.
      if (outputSlice_ == 0) {
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
      }
    } else if (mappingTab_ == 1 || mappingTab_ == 2) {
      // Warp (corners / mesh) and masks, edited on the output preview.
      outputsWs_.viewOutputId = sel->id;
      outputsWs_.lockView = true;
      if (mappingTab_ == 1) {
        // Perspective (rings) and mesh (squares) are edited together on the view.
        if (outputsWs_.tool != Tool::Mesh) outputsWs_.tool = Tool::Perspective;
        int grid[2] = {sel->warp.meshCols, sel->warp.meshRows};
        ImGui::SetNextItemWidth(140);
        if (ImGui::InputInt2(T("malha"), grid)) {
          sel->warp.resampleMesh(grid[0], grid[1]); // keeps the current warp
          outputsWs_.tool = Tool::Mesh;
          app.edited();
        }
        ImGui::SameLine();
        if (ImGui::Checkbox(T("Espelhar \xe2\x86\x94"), &sel->flipX)) app.edited(); // ↔
        ImGui::SameLine();
        if (ImGui::Checkbox(T("Espelhar \xe2\x86\x95"), &sel->flipY)) app.edited(); // ↕
        ImGui::SameLine(0, 16);
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
      outputsWs_.snap = workspace.snap; // the toolbar magnet applies here too
      outputsWs_.draw(app, ImGui::GetContentRegionAvail());
      ImGui::EndChild();
    } else {
      // Color and blend next to the output preview: every change is seen at once
      // (the preview is the output as sent to the projector, soft edges included).
      ImGui::BeginChild("##colorblend", {425, 0}, ImGuiChildFlags_Borders);
      int pat = int(sel->pattern);
      ImGui::SetNextItemWidth(220);
      if (ImGui::Combo(T("Padrão de teste"), &pat, TZ("Nenhum (conteúdo)\0Test card da saída\0Grade de blend (contínua)\0Branco\0Preto\0"))) {
        sel->pattern = TestPattern(pat);
        app.edited();
      }
      softEdgeControls(app, sel);
      blackAndColorControls(app, sel);
      ImGui::EndChild();
      ImGui::SameLine();
      outputsWs_.viewOutputId = sel->id;
      outputsWs_.lockView = true;
      // The warp stays adjustable here; the mask tool belongs to its own tab.
      if (outputsWs_.tool != Tool::Mesh) outputsWs_.tool = Tool::Perspective;
      ImGui::BeginChild("##wscolor", {0, 0});
      outputsWs_.snap = workspace.snap; // the toolbar magnet applies here too
      outputsWs_.draw(app, ImGui::GetContentRegionAvail());
      ImGui::EndChild();
    }
    if (swapped) swapSlice(); // the slice goes back to its place
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
