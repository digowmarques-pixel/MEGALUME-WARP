// "Saídas" window, modelled on Millumin's Output panel: a layout page (grid of
// projectors, display per projector, soft edge per seam) and a mapping page per
// output (slice of the canvas, warp, masks, colour).
#include "app.h"
#include "engine/outputs.h"
#include "ui/ui.h"
#include "platform/platform.h"

#include <imgui.h>

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <cmath>
#include <cstdio>

namespace lumen {

namespace {

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
  // Topmost first, as drawn (Resolume's order): the last slice is on top.
  for (int k = int(o.slices.size()); k >= 0; --k) {
    ImGui::PushID(k);
    bool on = k == 0 || o.slices[size_t(k - 1)].enabled;
    ImGui::BeginDisabled(k == 0); // Slice 1 is always on
    if (ImGui::Checkbox("##on", &on) && k > 0) {
      o.slices[size_t(k - 1)].enabled = on;
      app.edited();
    }
    ImGui::EndDisabled();
    ImGui::SameLine(0, 4);
    const char *nm = k == 0 ? "Slice 1" : o.slices[size_t(k - 1)].name.c_str();
    const bool selected = outputSlice_ == k && outputsPage_ == 1 && mappingTab_ <= 1;
    if (selected) ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.30f, 0.85f, 0.70f, 0.55f));
    float xw = (selected && k > 0) ? 26.0f : 0.0f;
    if (ImGui::Selectable(nm, selected, 0, {ImGui::GetContentRegionAvail().x - xw, 0})) {
      outputSlice_ = k;
      outputsPage_ = 1;
      if (mappingTab_ >= 2) mappingTab_ = 1; // slices are edited in the input and output tabs
    }
    if (selected) ImGui::PopStyleColor();
    bool removed = false;
    if (ImGui::BeginPopupContextItem("##slctx")) {
      if (ImGui::MenuItem(T("Duplicar"))) {
        OutputSlice copy;
        if (k == 0) {
          copy.srcX = o.srcX; copy.srcY = o.srcY; copy.srcW = o.srcW; copy.srcH = o.srcH;
          copy.warp = o.warp; copy.flipX = o.flipX; copy.flipY = o.flipY;
          copy.inRotation = o.inRotation; copy.outRotation = o.outRotation; copy.tone = o.tone;
        } else {
          copy = o.slices[size_t(k - 1)];
        }
        copy.name = "Slice " + std::to_string(o.slices.size() + 2);
        copy.enabled = true;
        o.slices.insert(o.slices.begin() + k, copy); // right above the original
        outputSlice_ = k + 1;
        app.edited();
      }
      if (ImGui::MenuItem(T("Igualar à saída (Match Output Shape)"))) matchOutputShape(app, o, k);
      if (k > 0) {
        if (ImGui::MenuItem(T("Subir"), nullptr, false, k < int(o.slices.size()))) { // drawn later = on top
          std::swap(o.slices[size_t(k - 1)], o.slices[size_t(k)]);
          outputSlice_ = k + 1;
          app.edited();
        }
        if (ImGui::MenuItem(T("Descer"), nullptr, false, k > 1)) {
          std::swap(o.slices[size_t(k - 1)], o.slices[size_t(k - 2)]);
          outputSlice_ = k - 1;
          app.edited();
        }
        if (ImGui::MenuItem(T("Remover##slctx"))) removed = true;
      }
      ImGui::EndPopup();
    }
    if (selected && k > 0) {
      ImGui::SameLine(0, 2);
      if (ImGui::SmallButton("x")) removed = true;
      ImGui::SetItemTooltip("%s", T("Remover slice"));
    }
    ImGui::PopID();
    if (removed) {
      o.slices.erase(o.slices.begin() + (k - 1));
      outputSlice_ = std::min(k - 1, int(o.slices.size()));
      app.edited();
      break;
    }
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
  ImGui::BeginChild("##left", {220, -ImGui::GetFrameHeightWithSpacing() * 2.6f}, ImGuiChildFlags_Borders);
  // Presets (Resolume's screen setups): the outputs in a file of their own,
  // in <settings>/presets/saidas, to reuse a venue's setup with any project.
  {
    namespace fs = std::filesystem;
    const fs::path dir = platform::toPath(platform::settingsDir()) / "presets" / "saidas";
    static std::string presetName = T("Presets");
    static char newName[64] = "";
    bool openSave = false;
    ImGui::SetNextItemWidth(-1);
    if (ImGui::BeginCombo("##presets", presetName.c_str())) {
      std::error_code ec;
      std::vector<fs::path> files;
      for (auto &f : fs::directory_iterator(dir, ec))
        if (f.path().extension() == ".lumenout") files.push_back(f.path());
      std::sort(files.begin(), files.end());
      if (files.empty()) ImGui::TextDisabled("%s", T("(nenhum preset salvo)"));
      for (auto &f : files) {
        std::string nm = platform::toUtf8(f.stem());
        if (ImGui::Selectable(nm.c_str(), nm == presetName)) {
          std::string text, err;
          if (platform::readFile(platform::toUtf8(f), text) &&
              loadOutputSetup(text, platform::toUtf8(dir), p, &err)) {
            presetName = nm;
            outputsSel_ = p.outputs.empty() ? -1 : p.outputs[0].id;
            outputSlice_ = 0;
            app.edited();
            app.status(T("Preset de saída carregado: ") + nm);
          } else {
            app.status(T("Não foi possível carregar o preset: ") + err, true);
          }
        }
      }
      ImGui::Separator();
      if (ImGui::Selectable(T("Salvar como..."))) openSave = true;
      if (ImGui::Selectable(T("Carregar arquivo..."))) {
        auto picked = platform::openFileDialog(T("Preset de saída"), "Preset de saída|*.lumenout", false);
        std::string text, err;
        if (!picked.empty() && platform::readFile(picked[0], text) &&
            loadOutputSetup(text, platform::toUtf8(platform::toPath(picked[0]).parent_path()), p, &err)) {
          presetName = platform::toUtf8(platform::toPath(picked[0]).stem());
          outputsSel_ = p.outputs.empty() ? -1 : p.outputs[0].id;
          outputSlice_ = 0;
          app.edited();
        } else if (!picked.empty()) {
          app.status(T("Não foi possível carregar o preset: ") + err, true);
        }
      }
      if (ImGui::Selectable(T("Abrir pasta dos presets"))) {
        fs::create_directories(dir, ec);
        platform::openInShell(platform::toUtf8(dir));
      }
      ImGui::EndCombo();
    }
    ImGui::SetItemTooltip("%s", T("Presets de saída: as telas e os slices num arquivo próprio, para usar em qualquer projeto"));
    if (openSave) {
      std::snprintf(newName, sizeof(newName), "%s", presetName == T("Presets") ? "" : presetName.c_str());
      ImGui::OpenPopup("##savepreset");
    }
    if (ImGui::BeginPopup("##savepreset")) {
      ImGui::TextUnformatted(T("Nome do preset"));
      ImGui::SetNextItemWidth(220);
      if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
      bool enter = ImGui::InputText("##pname", newName, sizeof(newName), ImGuiInputTextFlags_EnterReturnsTrue);
      if ((enter || ImGui::Button(T("Salvar"))) && newName[0]) {
        std::string nm = newName;
        for (char &c : nm)
          if (std::strchr("\\/:*?\"<>|", c)) c = '_'; // not allowed in file names
        std::error_code ec;
        fs::create_directories(dir, ec);
        std::string file = platform::toUtf8(dir / platform::toPath(nm + ".lumenout"));
        if (platform::writeFile(file, serializeOutputSetup(p, platform::toUtf8(dir)))) {
          presetName = nm;
          app.status(T("Preset de saída salvo: ") + nm);
        } else {
          app.status(T("Não foi possível salvar o preset"), true);
        }
        ImGui::CloseCurrentPopup();
      }
      ImGui::EndPopup();
    }
  }
  // "+" (Resolume's): a slice on the selected projector, or an output mask.
  if (ImGui::Button(T("+  \xe2\x96\xbe##add"), {60, 0})) ImGui::OpenPopup("##addmenu"); // ▾
  if (ImGui::BeginPopup("##addmenu")) {
    OutputConfig *cur = p.output(outputsSel_);
    if (ImGui::MenuItem(T("Slice"), nullptr, false, cur != nullptr)) {
      OutputSlice sl;
      sl.name = "Slice " + std::to_string(cur->slices.size() + 2);
      sl.srcW = float(p.canvasWidth(cur->canvasId));
      sl.srcH = float(p.canvasHeight(cur->canvasId));
      sl.warp.setRect({0.25f, 0.25f}, {0.5f, 0.5f}); // in the middle of the projector, half size
      cur->slices.push_back(sl);
      outputSliceFor_ = cur->id;
      outputSlice_ = int(cur->slices.size());
      outputsPage_ = 1;
      if (mappingTab_ >= 2) mappingTab_ = 0;
      app.edited();
    }
    if (ImGui::MenuItem(T("Máscara de saída"), nullptr, false, cur != nullptr)) {
      outputsPage_ = 1;
      mappingTab_ = 2;
    }
    ImGui::EndPopup();
  }
  ImGui::TextDisabled(T("Canvas %d × %d"), p.canvasW, p.canvasH);
  ImGui::Separator();
  auto mons = OutputManager::monitors();
  for (size_t i = 0; i < p.outputs.size(); ++i) {
    auto &o = p.outputs[i];
    ImGui::PushID(o.id);
    const bool selOut = outputsSel_ == o.id;
    char label[160];
    std::snprintf(label, sizeof(label), "%s  %s", selOut ? "\xe2\x96\xbe" : "\xe2\x96\xb8", o.name.c_str()); // ▾ ▸
    // Resolume's screen toggle: off = the screen shows black (its window stays open).
    bool live = !o.muted;
    if (ImGui::Checkbox("##scron", &live)) {
      o.muted = !live;
      app.edited();
    }
    ImGui::SetItemTooltip("%s", T("Liga/desliga a imagem desta tela (desligada = preto)"));
    ImGui::SameLine(0, 4);
    // The projector row selects the screen itself (its properties on the right).
    bool screenOn = selOut && outputsPage_ == 1 && outputSlice_ < 0 && mappingTab_ <= 1;
    if (ImGui::Selectable(label, screenOn || (selOut && outputsPage_ == 0))) {
      outputsSel_ = o.id;
      outputSliceFor_ = o.id;
      outputSlice_ = -1;
      if (outputsPage_ == 1 && mappingTab_ >= 2) mappingTab_ = 1;
    }
    if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
      outputsSel_ = o.id;
      outputsPage_ = 1;
    }
    if (selOut) {
      if (outputSliceFor_ != o.id) {
        outputSliceFor_ = o.id;
        outputSlice_ = 0;
      }
      int w = o.winW, h = o.winH;
      std::string dev = o.monitor < 0 ? T("janela") : T("Tela ") + std::to_string(o.monitor + 1);
      if (o.monitor >= 0 && o.monitor < int(mons.size())) { w = mons[size_t(o.monitor)].w; h = mons[size_t(o.monitor)].h; }
      ImGui::Indent(14);
      ImGui::TextDisabled("%s (%d×%d)", dev.c_str(), w, h);
      outputSliceBar(app, o);
      ImGui::Unindent(14);
    }
    ImGui::PopID();
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
    const char *tabs[] = {T("Seleção de entrada"), T("Transformação de saída"), T("Máscaras"), T("Cor e blend")};
    for (int t = 0; t < 4; ++t) {
      if (t) ImGui::SameLine(0, 2);
      bool on = mappingTab_ == t;
      if (on) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(1.0f, 0.77f, 0.0f, 0.85f));
      if (ImGui::Button(tabs[t], {std::max(110.0f, ImGui::CalcTextSize(tabs[t]).x + 24), 0})) mappingTab_ = t;
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
    outputSlice_ = std::clamp(outputSlice_, -1, int(sel->slices.size()));

    if (mappingTab_ <= 1) {
      // Input Selection / Output Transformation, as in Resolume's Advanced Output.
      sliceEditor(app, *sel, mappingTab_ == 1);
    } else if (mappingTab_ == 2) {
      // Masks, edited on the output preview.
      outputsWs_.viewOutputId = sel->id;
      outputsWs_.lockView = true;
      {
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
