#include "ui/ui.h"

#include "app.h"
#include "core/svg.h"
#include "io/ndi.h"
#include "io/serial.h"
#include "engine/outputs.h"
#include "io/spout.h"
#include "media/decoder.h"
#include "media/upload.h"
#include "platform/platform.h"

#include <GLFW/glfw3.h>
#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <cstring>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <map>

namespace lumen {

namespace {

ImTextureID texId(const Texture &t) { return ImTextureID(intptr_t(t.id())); }

// Colours of the Lumen theme.
// Lumen palette: black and yellow.
const ImVec4 kAccent{1.0f, 0.77f, 0.0f, 1.0f};        // #FFC400
const ImU32 kAccentU32 = IM_COL32(255, 196, 0, 255);
const ImU32 kPlayingU32 = IM_COL32(255, 214, 64, 255); // what is on air
const ImVec4 kOnAccent{0.05f, 0.05f, 0.05f, 1.0f};    // text on yellow

// Yellow button with black text (main actions, active tools).
void pushPrimary() {
  ImGui::PushStyleColor(ImGuiCol_Button, kAccent);
  ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1.0f, 0.84f, 0.25f, 1));
  ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.9f, 0.68f, 0.0f, 1));
  ImGui::PushStyleColor(ImGuiCol_Text, kOnAccent);
}
void popPrimary() { ImGui::PopStyleColor(4); }

bool colorEdit(const char *label, Color &c, bool alpha = false) {
  float v[4] = {c.r, c.g, c.b, c.a};
  bool changed = alpha ? ImGui::ColorEdit4(label, v, ImGuiColorEditFlags_Float)
                       : ImGui::ColorEdit3(label, v, ImGuiColorEditFlags_Float);
  if (changed) c = {v[0], v[1], v[2], v[3]};
  return changed;
}

bool inputText(const char *label, std::string &s) {
  char buf[512];
  std::snprintf(buf, sizeof(buf), "%s", s.c_str());
  if (ImGui::InputText(label, buf, sizeof(buf))) {
    s = buf;
    return true;
  }
  return false;
}

void helpMarker(const char *text) {
  ImGui::SameLine();
  ImGui::TextDisabled("(?)");
  if (ImGui::BeginItemTooltip()) {
    ImGui::PushTextWrapPos(ImGui::GetFontSize() * 30.0f);
    ImGui::TextUnformatted(text);
    ImGui::PopTextWrapPos();
    ImGui::EndTooltip();
  }
}

// Draws a texture fitted inside a box, keeping its aspect ratio.
void fittedImage(const Texture *tex, ImVec2 box, bool flip = false) {
  ImVec2 cursor = ImGui::GetCursorScreenPos();
  ImDrawList *dl = ImGui::GetWindowDrawList();
  dl->AddRectFilled(cursor, {cursor.x + box.x, cursor.y + box.y}, IM_COL32(20, 20, 22, 255));
  if (tex && !tex->empty()) {
    float s = std::min(box.x / float(tex->width()), box.y / float(tex->height()));
    ImVec2 sz{float(tex->width()) * s, float(tex->height()) * s};
    ImVec2 p{cursor.x + (box.x - sz.x) * 0.5f, cursor.y + (box.y - sz.y) * 0.5f};
    dl->AddImage(texId(*tex), p, {p.x + sz.x, p.y + sz.y}, flip ? ImVec2(0, 1) : ImVec2(0, 0),
                 flip ? ImVec2(1, 0) : ImVec2(1, 1));
  }
  ImGui::Dummy(box);
}

bool canvasCombo(const char *label, const Project &p, int &id) {
  bool changed = false;
  if (ImGui::BeginCombo(label, p.canvasLabel(id).c_str())) {
    for (int c : p.canvasIds()) {
      char item[160];
      std::snprintf(item, sizeof(item), T("%s  (%d×%d)##%d"), p.canvasLabel(c).c_str(), p.canvasWidth(c), p.canvasHeight(c), c);
      if (ImGui::Selectable(item, c == id)) { id = c; changed = true; }
    }
    ImGui::EndCombo();
  }
  return changed;
}

// Controls for the inputs of an ISF shader. Returns true when a value changed.
bool isfParamsUi(const IsfInfo &info, IsfValues &values) {
  isfDefaults(info, values);
  bool changed = false;
  for (auto &in : info.inputs) {
    if (in.size() == 0) continue;
    auto &v = values[in.name];
    const char *label = in.label.c_str();
    ImGui::PushID(in.name.c_str());
    float lo = in.min.empty() ? 0.0f : in.min[0], hi = in.max.empty() ? 1.0f : in.max[0];
    if (in.type == "float") {
      changed |= ImGui::SliderFloat(label, &v[0], lo, hi);
    } else if (in.type == "bool") {
      bool b = v[0] > 0.5f;
      if (ImGui::Checkbox(label, &b)) { v[0] = b ? 1.0f : 0.0f; changed = true; }
    } else if (in.type == "event") {
      if (ImGui::Button(label)) { v[0] = 1; changed = true; }
      else if (v[0] != 0) { v[0] = 0; } // one frame pulse
    } else if (in.type == "long") {
      if (!in.values.empty()) {
        int cur = 0;
        for (size_t k = 0; k < in.values.size(); ++k)
          if (in.values[k] == int(std::lround(v[0]))) cur = int(k);
        std::string preview = cur < int(in.labels.size()) ? in.labels[size_t(cur)] : std::to_string(in.values[size_t(cur)]);
        if (ImGui::BeginCombo(label, preview.c_str())) {
          for (size_t k = 0; k < in.values.size(); ++k) {
            std::string item = k < in.labels.size() ? in.labels[k] : std::to_string(in.values[k]);
            if (ImGui::Selectable(item.c_str(), int(k) == cur)) { v[0] = float(in.values[k]); changed = true; }
          }
          ImGui::EndCombo();
        }
      } else {
        int iv = int(std::lround(v[0]));
        if (ImGui::SliderInt(label, &iv, int(lo), int(std::max(hi, lo + 1)))) { v[0] = float(iv); changed = true; }
      }
    } else if (in.type == "color") {
      changed |= ImGui::ColorEdit4(label, v.data(), ImGuiColorEditFlags_AlphaBar);
    } else if (in.type == "point2D") {
      float l2 = in.min.size() >= 2 ? std::min(in.min[0], in.min[1]) : 0.0f;
      float h2 = in.max.size() >= 2 ? std::max(in.max[0], in.max[1]) : 1.0f;
      changed |= ImGui::SliderFloat2(label, v.data(), l2, h2);
    }
    ImGui::PopID();
  }
  return changed;
}

const char *mediaTypeLabel(MediaType t) {
  switch (t) {
  case MediaType::Video: return T("Vídeo");
  case MediaType::Image: return T("Imagem");
  case MediaType::Generator: return T("Gerador");
  case MediaType::Live: return T("Ao vivo");
  case MediaType::Spout: return T("Spout");
  case MediaType::Canvas: return T("Canvas");
  case MediaType::Shader: return T("Shader ISF");
  case MediaType::Text: return T("Texto");
  case MediaType::NDI: return T("NDI");
  }
  return "";
}

} // namespace

void Ui::setup() {
  ImGuiIO &io = ImGui::GetIO();
  // No keyboard navigation: Space must always mean GO, never "press focused button".
  io.ConfigFlags &= ~ImGuiConfigFlags_NavEnableKeyboard;
  io.IniFilename = nullptr; // fixed layout, nothing to persist

  // Prefer a proper system UI font; fall back to the built-in one.
  const char *fonts[] = {"C:/Windows/Fonts/segoeui.ttf",
                         "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
                         "/System/Library/Fonts/SFNS.ttf"};
  bool loaded = false;
  for (const char *f : fonts) {
    std::error_code ec;
    if (std::filesystem::exists(f, ec)) {
      // Latin-1 plus punctuation (– — … ·) and arrows (→) used in the interface.
      static const ImWchar ranges[] = {0x0020, 0x00FF, 0x2000, 0x206F, 0x2190, 0x21FF, 0x2212, 0x2212,
                                       0x25A0, 0x25FF, 0};
      if (io.Fonts->AddFontFromFileTTF(f, 16.0f, nullptr, ranges)) {
        loaded = true;
        // Symbols (▶ ■ ⚙ ...) from Windows' symbol font when the UI font lacks them.
        static const ImWchar symbols[] = {0x2190, 0x21FF, 0x25A0, 0x25FF, 0x2600, 0x26FF, 0};
        if (std::filesystem::exists("C:/Windows/Fonts/seguisym.ttf", ec)) {
          ImFontConfig merge;
          merge.MergeMode = true;
          io.Fonts->AddFontFromFileTTF("C:/Windows/Fonts/seguisym.ttf", 16.0f, &merge, symbols);
        }
        break;
      }
    }
  }
  if (!loaded) io.Fonts->AddFontDefault();

  // A larger weight of the same font for titles and the brand.
  bigFont_ = nullptr;
  for (const char *f : fonts) {
    std::error_code ec;
    if (loaded && std::filesystem::exists(f, ec)) {
      const char *bold = f;
      if (std::filesystem::exists("C:/Windows/Fonts/segoeuib.ttf", ec) && std::string(f).find("segoeui") != std::string::npos)
        bold = "C:/Windows/Fonts/segoeuib.ttf";
      else if (std::filesystem::exists("/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf", ec) &&
               std::string(f).find("DejaVu") != std::string::npos)
        bold = "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf";
      bigFont_ = io.Fonts->AddFontFromFileTTF(bold, 21.0f);
      break;
    }
  }

  ImGui::StyleColorsDark();
  ImGuiStyle &s = ImGui::GetStyle();
  // Rounded, airy and calm: panels are cards on a black background.
  s.WindowRounding = 10;
  s.ChildRounding = 8;
  s.FrameRounding = 6;
  s.PopupRounding = 8;
  s.GrabRounding = 6;
  s.TabRounding = 6;
  s.ScrollbarRounding = 8;
  s.WindowBorderSize = 0;
  s.ChildBorderSize = 1;
  s.PopupBorderSize = 1;
  s.FrameBorderSize = 0;
  s.TabBarOverlineSize = 2;
  s.WindowPadding = {10, 8};
  s.FramePadding = {8, 4};
  s.ItemSpacing = {7, 6};
  s.ItemInnerSpacing = {6, 4};
  s.ScrollbarSize = 11;
  s.GrabMinSize = 5; // a thin yellow marker: the value stays readable
  s.SeparatorTextBorderSize = 1;
  s.SeparatorTextPadding = {12, 4};
  s.WindowTitleAlign = {0.0f, 0.5f};
  s.SelectableTextAlign = {0.0f, 0.5f};

  const ImVec4 black{0.035f, 0.035f, 0.04f, 1}, panel{0.085f, 0.085f, 0.09f, 1}, inset{0.06f, 0.06f, 0.065f, 1};
  const ImVec4 frame{0.14f, 0.14f, 0.15f, 1}, frameHi{0.19f, 0.19f, 0.205f, 1}, line{0.2f, 0.2f, 0.21f, 1};
  const ImVec4 y = kAccent;
  auto ya = [&](float a) { return ImVec4(y.x, y.y, y.z, a); };
  ImVec4 *c = s.Colors;
  c[ImGuiCol_Text] = {0.93f, 0.93f, 0.92f, 1};
  c[ImGuiCol_TextDisabled] = {0.52f, 0.52f, 0.5f, 1};
  c[ImGuiCol_WindowBg] = panel;
  c[ImGuiCol_ChildBg] = inset;
  c[ImGuiCol_PopupBg] = {0.1f, 0.1f, 0.105f, 0.98f};
  c[ImGuiCol_Border] = line;
  c[ImGuiCol_BorderShadow] = {0, 0, 0, 0};
  c[ImGuiCol_MenuBarBg] = black;
  c[ImGuiCol_TitleBg] = c[ImGuiCol_TitleBgActive] = c[ImGuiCol_TitleBgCollapsed] = panel;
  c[ImGuiCol_FrameBg] = frame;
  c[ImGuiCol_FrameBgHovered] = frameHi;
  c[ImGuiCol_FrameBgActive] = {0.23f, 0.22f, 0.18f, 1};
  c[ImGuiCol_Button] = frame;
  c[ImGuiCol_ButtonHovered] = frameHi;
  c[ImGuiCol_ButtonActive] = ya(0.85f);
  c[ImGuiCol_Header] = {0.16f, 0.16f, 0.17f, 1};
  c[ImGuiCol_HeaderHovered] = {0.21f, 0.21f, 0.22f, 1};
  c[ImGuiCol_HeaderActive] = ya(0.35f);
  c[ImGuiCol_CheckMark] = y;
  c[ImGuiCol_SliderGrab] = y;
  c[ImGuiCol_SliderGrabActive] = {1.0f, 0.86f, 0.35f, 1};
  c[ImGuiCol_Separator] = line;
  c[ImGuiCol_SeparatorHovered] = ya(0.6f);
  c[ImGuiCol_SeparatorActive] = y;
  c[ImGuiCol_ResizeGrip] = ya(0.15f);
  c[ImGuiCol_ResizeGripHovered] = ya(0.5f);
  c[ImGuiCol_ResizeGripActive] = y;
  c[ImGuiCol_Tab] = inset;
  c[ImGuiCol_TabHovered] = frameHi;
  c[ImGuiCol_TabSelected] = frame;
  c[ImGuiCol_TabSelectedOverline] = y;
  c[ImGuiCol_TabDimmed] = inset;
  c[ImGuiCol_TabDimmedSelected] = frame;
  c[ImGuiCol_ScrollbarBg] = {0, 0, 0, 0};
  c[ImGuiCol_ScrollbarGrab] = {0.22f, 0.22f, 0.23f, 1};
  c[ImGuiCol_ScrollbarGrabHovered] = {0.3f, 0.3f, 0.31f, 1};
  c[ImGuiCol_ScrollbarGrabActive] = ya(0.7f);
  c[ImGuiCol_TableHeaderBg] = inset;
  c[ImGuiCol_TableBorderStrong] = line;
  c[ImGuiCol_TableBorderLight] = {0.15f, 0.15f, 0.16f, 1};
  c[ImGuiCol_TableRowBg] = {0, 0, 0, 0};
  c[ImGuiCol_TableRowBgAlt] = {1, 1, 1, 0.015f};
  c[ImGuiCol_TextSelectedBg] = ya(0.35f);
  c[ImGuiCol_DragDropTarget] = y;
  c[ImGuiCol_NavHighlight] = y;
  c[ImGuiCol_PlotHistogram] = y;
  c[ImGuiCol_PlotLines] = y;
  c[ImGuiCol_ModalWindowDimBg] = {0, 0, 0, 0.6f};
}

void Ui::draw(App &app) {
  ImGuiViewport *vp = ImGui::GetMainViewport();
  menuBar(app);

  const float toolbarH = 54, statusH = 28, g = 6; // g: gap between the panel cards
  const float libraryW = std::clamp(vp->WorkSize.x * 0.17f, 220.0f, 320.0f);
  const float inspectorW = std::clamp(vp->WorkSize.x * 0.24f, 320.0f, 440.0f);
  const float x0 = vp->WorkPos.x, y0 = vp->WorkPos.y;
  const float W = vp->WorkSize.x, H = vp->WorkSize.y;
  const float bodyY = y0 + toolbarH, bodyH = H - toolbarH - statusH;
  const float centerW = W - libraryW - inspectorW;
  const float dashH = std::clamp(bodyH * 0.38f, 180.0f, 420.0f);

  ImGuiWindowFlags fixed = ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
                           ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoBringToFrontOnFocus;

  ImGui::SetNextWindowPos({x0 + g, y0 + g});
  ImGui::SetNextWindowSize({W - 2 * g, toolbarH - g});
  ImGui::Begin("##toolbar", nullptr, fixed | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoScrollbar);
  toolbar(app);
  ImGui::End();

  ImGui::SetNextWindowPos({x0 + g, bodyY + g});
  ImGui::SetNextWindowSize({libraryW - g, bodyH - g});
  const bool locked = app.showLocked;
  ImGui::Begin(T("Biblioteca"), nullptr, fixed);
  ImGui::BeginDisabled(locked);
  library(app);
  ImGui::EndDisabled();
  ImGui::End();

  ImGui::SetNextWindowPos({x0 + libraryW + g, bodyY + g});
  ImGui::SetNextWindowSize({centerW - g, bodyH - dashH - g});
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {4, 4});
  ImGui::Begin(T("Workspace"), nullptr, fixed | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
  ImGui::BeginDisabled(locked);
  workspace.draw(app, ImGui::GetContentRegionAvail());
  ImGui::EndDisabled();
  ImGui::End();
  ImGui::PopStyleVar();

  ImGui::SetNextWindowPos({x0 + libraryW + g, bodyY + bodyH - dashH + g});
  ImGui::SetNextWindowSize({centerW - g, dashH - g});
  ImGui::Begin("##bottom", nullptr, fixed | ImGuiWindowFlags_NoTitleBar);
  if (ImGui::BeginTabBar("##bottomtabs")) {
    if (ImGui::BeginTabItem(T("Dashboard"), nullptr, bottomTab_ == 0 && forceBottom_ ? ImGuiTabItemFlags_SetSelected : 0)) {
      if (!forceBottom_) bottomTab_ = 0;
      dashboard(app);
      ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem(T("Timeline"), nullptr, bottomTab_ == 1 && forceBottom_ ? ImGuiTabItemFlags_SetSelected : 0)) {
      if (!forceBottom_) bottomTab_ = 1;
      ImGui::BeginDisabled(locked);
      timelineEditor(app);
      ImGui::EndDisabled();
      ImGui::EndTabItem();
    }
    ImGui::EndTabBar();
  }
  forceBottom_ = false;
  ImGui::End();

  ImGui::SetNextWindowPos({x0 + W - inspectorW + g, bodyY + g});
  ImGui::SetNextWindowSize({inspectorW - 2 * g, bodyH - g});
  ImGui::Begin(T("Propriedades"), nullptr, fixed);
  ImGui::BeginDisabled(locked);
  inspector(app);
  ImGui::EndDisabled();
  ImGui::End();

  ImGui::SetNextWindowPos({x0 + g, y0 + H - statusH + 2});
  ImGui::SetNextWindowSize({W - 2 * g, statusH - 2});
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {8, 4});
  ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0)); // on the black background
  ImGui::Begin("##status", nullptr, fixed | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoScrollbar);
  statusBar(app);
  ImGui::End();
  ImGui::PopStyleColor();
  ImGui::PopStyleVar();

  if (!locked) {
    outputsWindow(app);
    inputMeshWindow(app);
  }
  recoveryPopup(app);
  if (showDiag_) {
    ImGui::SetNextWindowSize({640, 560}, ImGuiCond_FirstUseEver);
    if (ImGui::Begin(T("Diagnóstico do sistema"), &showDiag_)) {
      if (ImGui::Button(T("Copiar"))) ImGui::SetClipboardText(diagReport_.c_str());
      ImGui::SameLine();
      if (ImGui::Button(T("Testar de novo"))) diagReport_ = app.diagnosticReport();
      ImGui::SameLine();
      ImGui::TextDisabled("%s", T("Envie este texto junto com qualquer relato de problema."));
      ImGui::InputTextMultiline("##diag", diagReport_.data(), diagReport_.size() + 1, {-1, -1}, ImGuiInputTextFlags_ReadOnly);
    }
    ImGui::End();
  }
  if (showDemo_) ImGui::ShowDemoWindow(&showDemo_);
  if (showHelp_) {
    ImGui::SetNextWindowSize({520, 460}, ImGuiCond_FirstUseEver);
    if (ImGui::Begin(T("Atalhos e ajuda"), &showHelp_)) {
      ImGui::SeparatorText(T("Show"));
      ImGui::BulletText("%s", T("Espaço: GO (próxima coluna)   |   Shift+Espaço: coluna anterior"));
      ImGui::BulletText("%s", T("Esc: parar tudo com fade   |   B: blackout"));
      ImGui::BulletText("%s", T("Clique numa célula: dispara só aquela layer"));
      ImGui::SeparatorText(T("Workspace / mapping"));
      ImGui::BulletText("%s", T("V: selecionar/mover   M: perspectiva (4 cantos)"));
      ImGui::BulletText("%s", T("G: malha (mesh warp)   K: máscara"));
      ImGui::BulletText("%s", T("Tab: próximo canto/ponto   Setas: ajuste fino (Shift = 10px)"));
      ImGui::BulletText("%s", T("Roda do mouse: zoom   Botão do meio / Alt+arrastar: mover a vista"));
      ImGui::BulletText("%s", T("F: enquadrar   Máscara: clique adiciona ponto, botão direito apaga"));
      ImGui::SeparatorText(T("Projeto"));
      ImGui::BulletText("%s", T("Ctrl+N novo, Ctrl+O abrir, Ctrl+S salvar, Ctrl+Shift+S salvar como"));
      ImGui::BulletText("%s", T("Ctrl+Z desfazer, Ctrl+Y / Ctrl+Shift+Z refazer, Ctrl+I importar"));
      ImGui::BulletText("%s", T("Arraste arquivos do Explorer para a janela para importar"));
      ImGui::SeparatorText(T("OSC"));
      ImGui::TextWrapped("%s", T("Envie mensagens para a porta configurada (padrão 8000). "
                         "Exemplos: /lumen/go, /lumen/column/launch 2, /lumen/layer/1/opacity 0.5. "
                         "Veja docs/OSC.md para a lista completa."));
    }
    ImGui::End();
  }
}

void Ui::recoveryPopup(App &app) {
  if (app.recoveryPending() && !ImGui::IsPopupOpen("###recover")) ImGui::OpenPopup("###recover");
  ImGui::SetNextWindowSize({460, 0});
  std::string title = std::string(T("Recuperar alterações")) + "###recover";
  if (ImGui::BeginPopupModal(title.c_str(), nullptr, ImGuiWindowFlags_NoResize)) {
    ImGui::TextWrapped(T("O Lumen não foi fechado corretamente da última vez. Existe um salvamento automático "
                       "mais recente de %s."), app.recoveryInfo().c_str());
    ImGui::Spacing();
    pushPrimary();
    if (ImGui::Button(T("Recuperar"), {140, 30})) {
      app.recover(true);
      ImGui::CloseCurrentPopup();
    }
    popPrimary();
    ImGui::SameLine();
    if (ImGui::Button(T("Descartar"), {140, 30})) {
      app.recover(false);
      ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
  }
}

void Ui::relinkDialog(App &app) {
  std::string dir = platform::folderDialog(T("Pasta onde procurar as mídias"));
  if (dir.empty()) return;
  int before = app.missingMedia();
  int fixed = app.relinkMissing(dir);
  app.status(std::to_string(fixed) + T(" de ") + std::to_string(before) + T(" mídia(s) encontradas"), fixed < before);
}

void Ui::menuBar(App &app) {
  if (!ImGui::BeginMainMenuBar()) return;
  if (ImGui::BeginMenu(T("Arquivo"))) {
    ImGui::BeginDisabled(app.showLocked);
    if (ImGui::MenuItem(T("Novo projeto"), T("Ctrl+N"))) app.newProject();
    if (ImGui::MenuItem(T("Abrir..."), T("Ctrl+O"))) app.openDialog();
    ImGui::EndDisabled();
    if (ImGui::MenuItem(T("Salvar"), T("Ctrl+S"))) app.saveProject(false);
    if (ImGui::MenuItem(T("Salvar como..."), T("Ctrl+Shift+S"))) app.saveProject(true);
    ImGui::Separator();
    ImGui::BeginDisabled(app.showLocked);
    if (ImGui::MenuItem(T("Importar mídia..."), T("Ctrl+I"))) app.importDialog();
    if (ImGui::MenuItem(T("Procurar mídias ausentes..."), nullptr, false, app.missingMedia() > 0)) relinkDialog(app);
    ImGui::EndDisabled();
    ImGui::Separator();
    ImGui::MenuItem(T("Modo show (travar edição)"), T("Ctrl+Shift+L"), &app.showLocked);
    ImGui::Separator();
    if (ImGui::MenuItem(T("Sair"))) app.requestQuit = true;
    ImGui::EndMenu();
  }
  if (ImGui::BeginMenu(T("Editar"), !app.showLocked)) {
    if (ImGui::MenuItem(T("Desfazer"), T("Ctrl+Z"))) app.undo();
    if (ImGui::MenuItem(T("Refazer"), T("Ctrl+Y"))) app.redo();
    ImGui::Separator();
    if (ImGui::MenuItem(T("Nova layer"))) {
      Layer &l = app.engine.project.addLayer("", workspace.viewCanvas);
      app.sel.layerId = l.id;
      app.edited();
    }
    if (ImGui::MenuItem(T("Nova coluna"))) {
      app.engine.project.addColumn();
      app.edited();
    }
    ImGui::EndMenu();
  }
  if (ImGui::BeginMenu(T("Saídas"))) {
    if (ImGui::MenuItem(T("Configurar saídas..."), T("Ctrl+Shift+P"), false, !app.showLocked)) openOutputs();
    if (ImGui::MenuItem(app.outputsOpen ? T("Fechar saídas") : T("Abrir saídas"), T("Ctrl+P"))) app.outputsOpen = !app.outputsOpen;
    ImGui::Separator();
    if (ImGui::MenuItem(T("Adicionar saída"), nullptr, false, !app.showLocked)) {
      OutputConfig &o = app.engine.project.addOutput();
      app.sel.outputId = o.id;
      app.sel.inspector = Inspector::Output;
      app.edited();
    }
    ImGui::Separator();
    auto mons = OutputManager::monitors();
    ImGui::TextDisabled("%s", T("Monitores detectados:"));
    for (size_t i = 0; i < mons.size(); ++i)
      ImGui::TextDisabled(T("  %zu: %s  %dx%d @%dHz"), i + 1, mons[i].name.c_str(), mons[i].w,
                          mons[i].h, mons[i].refresh);
    ImGui::EndMenu();
  }
  if (ImGui::BeginMenu("Idioma / Language")) {
    if (ImGui::MenuItem("Português", nullptr, language() == Language::Portuguese)) {
      setLanguage(Language::Portuguese);
      saveLanguage();
    }
    if (ImGui::MenuItem("English", nullptr, language() == Language::English)) {
      setLanguage(Language::English);
      saveLanguage();
    }
    ImGui::EndMenu();
  }
  if (ImGui::BeginMenu(T("Ajuda"))) {
    if (ImGui::MenuItem(T("Atalhos e ajuda"), "F1")) showHelp_ = true;
    if (ImGui::MenuItem(T("Diagnóstico do sistema..."))) {
      diagReport_ = app.diagnosticReport();
      showDiag_ = true;
    }
    ImGui::MenuItem(T("Demo do ImGui"), nullptr, &showDemo_);
    ImGui::Separator();
    ImGui::TextDisabled(T("Lumen %s"), LUMEN_VERSION);
    ImGui::EndMenu();
  }
  ImGui::EndMainMenuBar();
}

void Ui::toolbar(App &app) {
  Engine &e = app.engine;
  ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 2);
  // Brand: a yellow dot and the name.
  {
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImDrawList *dl = ImGui::GetWindowDrawList();
    dl->AddCircleFilled({p.x + 9, p.y + 15}, 7, kAccentU32);
    dl->AddCircle({p.x + 9, p.y + 15}, 11, IM_COL32(255, 196, 0, 90), 0, 1.5f);
    ImGui::Dummy({24, 30});
    ImGui::SameLine();
    if (bigFont_) ImGui::PushFont(bigFont_);
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + (bigFont_ ? 2 : 6));
    ImGui::TextUnformatted(T("LUMEN"));
    if (bigFont_) ImGui::PopFont();
    ImGui::SameLine(0, 18);
    ImGui::SetCursorPosY(2 + ImGui::GetStyle().WindowPadding.y);
  }

  pushPrimary();
  if (ImGui::Button(T("GO"), {84, 30})) e.go();
  popPrimary();
  ImGui::SetItemTooltip("%s", T("Dispara a próxima coluna (Espaço)"));
  ImGui::SameLine();
  if (ImGui::Button("<", {30, 30})) e.back();
  ImGui::SetItemTooltip("%s", T("Coluna anterior (Shift+Espaço)"));
  ImGui::SameLine();
  if (ImGui::Button(T("Parar"), {60, 30})) e.stopAll();
  ImGui::SetItemTooltip("%s", T("Para tudo com fade (Esc)"));
  ImGui::SameLine();
  if (e.blackout) {
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.75f, 0.15f, 0.15f, 1));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.9f, 0.2f, 0.2f, 1));
  }
  if (ImGui::Button(T("Blackout"), {80, 30})) e.blackout = !e.blackout;
  if (e.blackout) ImGui::PopStyleColor(2);
  ImGui::SetItemTooltip("%s", T("Escurece todas as saídas (B)"));
  ImGui::SameLine();
  bool open = app.outputsOpen;
  if (open) pushPrimary();
  if (ImGui::Button(open ? T("Fechar saídas") : T("Abrir saídas"), {110, 30})) app.outputsOpen = !open;
  if (open) popPrimary();
  ImGui::SameLine();
  if (ImGui::Button(T("Saídas..."), {80, 30})) openOutputs();
  ImGui::SetItemTooltip("%s", T("Layout dos projetores, telas, soft edge e mapping de cada saída"));
  ImGui::SetItemTooltip("%s", T("Abre/fecha as janelas dos projetores (Ctrl+P). O workspace mostra a prévia mesmo fechadas."));
  ImGui::SameLine();
  bool lk = app.showLocked;
  if (lk) pushPrimary();
  if (ImGui::Button(lk ? T("SHOW") : T("Editar"), {64, 30})) app.showLocked = !lk;
  if (lk) popPrimary();
  ImGui::SetItemTooltip("%s", lk ? T("Modo show: edição travada. Clique (ou Ctrl+Shift+L) para editar.")
                           : T("Travar a edição para o show (Ctrl+Shift+L). GO, parar, blackout e masters continuam."));

  ImGui::SameLine(0, 16);
  ImGui::SetNextItemWidth(100);
  ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 5);
  ImGui::SliderFloat(T("Master"), &e.masterOpacity, 0, 1, "%.2f");

  ImGui::SameLine(0, 14);
  ImGui::SetNextItemWidth(80);
  ImGui::BeginDisabled(app.showLocked);
  if (ImGui::DragFloat(T("Fade padrão"), &e.project.defaultTransition, 0.05f, 0, 30, "%.2f s"))
    app.edited();
  ImGui::EndDisabled();

  bool anyManual = std::any_of(e.project.columns.begin(), e.project.columns.end(),
                               [](const Column &c) { return c.manualTransition; });
  if (anyManual) {
    ImGui::SameLine(0, 14);
    ImGui::SetNextItemWidth(90);
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 5);
    bool waiting = e.manualTransitionActive() && !e.manualTransitionArmed();
    if (e.manualTransitionActive()) ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.35f, 0.27f, 0.0f, 1));
    ImGui::SliderFloat("##tfader", &e.transitionFader, 0, 1, waiting ? T("abaixe") : "%.2f");
    if (e.manualTransitionActive()) ImGui::PopStyleColor();
    ImGui::SetItemTooltip("%s", T("Fader de transição manual. Leve a 0 para assumir a transição e suba até 1."));
  }
  ImGui::SameLine(0, 16);
  const char *tools[] = {T("Selecionar"), T("Perspectiva"), T("Malha"), T("Máscara"), T("LED")};
  const char *keys[] = {"V", "M", "G", "K", "L"};
  ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {3, 6});
  ImGui::BeginDisabled(app.showLocked);
  for (int i = 0; i < 5; ++i) {
    bool active = int(workspace.tool) == i;
    if (active) pushPrimary();
    if (ImGui::Button(tools[i], {0, 30})) workspace.tool = Tool(i);
    if (active) popPrimary();
    ImGui::SetItemTooltip(T("Tecla %s"), keys[i]);
    ImGui::SameLine();
  }
  ImGui::EndDisabled();
  ImGui::PopStyleVar();

  ImGui::SameLine(0, 20);
  ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 6);
  int col = e.activeColumn();
  if (col >= 0 && col < int(e.project.columns.size()))
    ImGui::TextColored(kAccent, T("No ar: %d · %s"), col + 1, e.project.columns[size_t(col)].name.c_str());
  else
    ImGui::TextDisabled("%s", T("Nada no ar"));
}

void Ui::library(App &app) {
  Project &p = app.engine.project;
  if (ImGui::Button(T("Importar..."))) app.importDialog();
  ImGui::SameLine();
  if (ImGui::Button(T("+ Gerador"))) ImGui::OpenPopup("gen");
  if (ImGui::BeginPopup("gen")) {
    for (int k = 0; k < int(GeneratorKind::COUNT); ++k)
      if (ImGui::MenuItem(generatorName(GeneratorKind(k)))) {
        MediaItem &m = p.addGenerator(GeneratorKind(k));
        app.sel.mediaId = m.id;
        app.sel.inspector = Inspector::Media;
        app.edited();
      }
    ImGui::SeparatorText(T("Texto"));
    if (ImGui::MenuItem(T("Texto (dinâmico)"))) {
      MediaItem &m = p.addText(T("Texto"));
      app.sel.mediaId = m.id;
      app.sel.inspector = Inspector::Media;
      app.edited();
    }
    ImGui::SetItemTooltip("%s", T("Texto que muda por OSC (/layer:Nome/text \"...\") ou lido de um arquivo .txt"));
    ImGui::SeparatorText(T("Shaders ISF"));
    for (auto &e : app.engine.isfCatalog.builtins()) {
      if (e.info.filter || e.info.transition) continue;
      if (ImGui::MenuItem(e.name.c_str())) {
        MediaItem &m = p.addShader(e.path);
        app.sel.mediaId = m.id;
        app.sel.inspector = Inspector::Media;
        app.edited();
      }
      if (!e.info.description.empty()) ImGui::SetItemTooltip("%s", e.info.description.c_str());
    }
    if (ImGui::MenuItem(T("Abrir arquivo .fs..."))) {
      auto files = platform::openFileDialog("Shader ISF", "ISF|*.fs|Todos|*.*", false);
      if (!files.empty()) {
        MediaItem &m = p.addShader(files[0]);
        app.sel.mediaId = m.id;
        app.sel.inspector = Inspector::Media;
        app.edited();
      }
    }
    ImGui::EndPopup();
  }
  ImGui::SameLine();
  if (ImGui::Button(T("+ Ao vivo"))) {
    captureDevices_ = listCaptureDevices();
    spoutSenders_ = spoutSenders();
    ImGui::OpenPopup("live");
  }
  if (ImGui::BeginPopup("live")) {
    auto add = [&](MediaItem &m) {
      app.sel.mediaId = m.id;
      app.sel.inspector = Inspector::Media;
      app.edited();
      ImGui::CloseCurrentPopup();
    };
    ImGui::SeparatorText(T("Câmeras e placas de captura"));
#ifdef __APPLE__
    // FFmpeg cannot list AVFoundation devices: offer the usual indexes.
    if (ImGui::MenuItem(T("Câmera 1 do Mac (AVFoundation)"))) add(p.addLive(T("Câmera 1"), "0", "avfoundation", "framerate=30"));
    if (ImGui::MenuItem(T("Câmera 2 do Mac (AVFoundation)"))) add(p.addLive(T("Câmera 2"), "1", "avfoundation", "framerate=30"));
#endif
    if (captureDevices_.empty()) ImGui::TextDisabled("%s", T("Nenhum dispositivo encontrado"));
    for (auto &d : captureDevices_)
      if (ImGui::MenuItem(d.name.c_str())) add(p.addLive(d.name, d.url, d.format, ""));
    if (ImGui::SmallButton(T("Procurar de novo"))) captureDevices_ = listCaptureDevices();
    ImGui::SeparatorText(T("Stream de rede"));
    ImGui::SetNextItemWidth(300);
    ImGui::InputTextWithHint("##url", "rtsp://, srt://, udp://, http://...", liveUrl_, sizeof(liveUrl_));
    ImGui::SameLine();
    if (ImGui::Button(T("Adicionar")) && liveUrl_[0]) add(p.addLive("", liveUrl_, "", ""));
    ImGui::SeparatorText(T("NDI (rede)"));
    if (!ndiAvailable()) {
      ImGui::TextDisabled("%s", ndiStatus().c_str());
    } else {
      auto srcs = ndiSources();
      if (srcs.empty()) ImGui::TextDisabled("%s", T("Procurando fontes NDI na rede..."));
      for (auto &n : srcs)
        if (ImGui::MenuItem(n.c_str())) add(p.addNdi(n));
    }
    ImGui::SeparatorText(T("Spout (Windows)"));
    if (!spoutAvailable()) ImGui::TextDisabled("%s", T("Disponível só no Windows (no Mac o equivalente é o Syphon)"));
    else {
      if (ImGui::MenuItem(T("Sender ativo (qualquer um)"))) add(p.addSpout(""));
      for (auto &n : spoutSenders_)
        if (ImGui::MenuItem(n.c_str())) add(p.addSpout(n));
    }
    ImGui::SeparatorText(T("Canvas (video routing)"));
    for (int c : p.canvasIds()) {
      char label[160];
      std::snprintf(label, sizeof(label), T("%s (%d×%d)##cvm%d"), p.canvasLabel(c).c_str(), p.canvasWidth(c), p.canvasHeight(c), c);
      if (ImGui::MenuItem(label)) add(p.addCanvasMedia(c));
    }
    ImGui::TextDisabled("%s", T("Mostra um canvas dentro de outro (posição, escala e mapping da layer)."));
    ImGui::SeparatorText(T("Teste"));
    if (ImGui::MenuItem(T("Sinal de teste ao vivo (lavfi)")))
      add(p.addLive(T("Sinal de teste"), T("testsrc2=size=1280x720:rate=30"), "lavfi", ""));
    ImGui::EndPopup();
  }
  if (int missing = app.missingMedia()) {
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.55f, 0.12f, 0.12f, 1));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.7f, 0.16f, 0.16f, 1));
    char label[64];
    std::snprintf(label, sizeof(label), T("%d mídia(s) ausente(s): procurar..."), missing);
    if (ImGui::Button(label, {-1, 0})) relinkDialog(app);
    ImGui::PopStyleColor(2);
  }
  ImGui::SetNextItemWidth(-1);
  ImGui::InputTextWithHint("##filter", T("Buscar mídia..."), filterText_, sizeof(filterText_));
  ImGui::Separator();

  ImGui::BeginChild("##items");
  std::string filter = filterText_;
  std::transform(filter.begin(), filter.end(), filter.begin(), [](unsigned char ch) { return char(std::tolower(ch)); });
  int removeId = -1;
  for (auto &m : p.library) {
    std::string lname = m.name;
    std::transform(lname.begin(), lname.end(), lname.begin(), [](unsigned char ch) { return char(std::tolower(ch)); });
    if (!filter.empty() && lname.find(filter) == std::string::npos) continue;

    ImGui::PushID(m.id);
    bool selected = app.sel.mediaId == m.id && app.sel.inspector == Inspector::Media;
    ImVec2 start = ImGui::GetCursorScreenPos();
    if (ImGui::Selectable("##item", selected, 0, {0, 48})) {
      app.sel.mediaId = m.id;
      app.sel.inspector = Inspector::Media;
    }
    if (ImGui::BeginDragDropSource()) {
      ImGui::SetDragDropPayload("LUMEN_MEDIA", &m.id, sizeof(int));
      ImGui::Text("%s", m.name.c_str());
      ImGui::EndDragDropSource();
    }
    if (ImGui::BeginPopupContextItem()) {
      if (ImGui::MenuItem(T("Remover da biblioteca"))) removeId = m.id;
      ImGui::EndPopup();
    }
    const Texture *thumb = app.engine.cache.thumbnail(m, app.engine.generators, app.engine.mesh);
    ImDrawList *dl = ImGui::GetWindowDrawList();
    ImVec2 tl{start.x + 2, start.y + 2}, br{start.x + 78, start.y + 46};
    dl->AddRectFilled(tl, br, IM_COL32(15, 15, 15, 255), 5);
    if (m.type == MediaType::Canvas) // live picture of the canvas (bottom-up texture)
      dl->AddImageRounded(texId(app.engine.canvasTexture(m.canvasRef)), tl, br, {0, 1}, {1, 0}, IM_COL32_WHITE, 5);
    else if (thumb && !thumb->empty()) dl->AddImageRounded(texId(*thumb), tl, br, {0, 0}, {1, 1}, IM_COL32_WHITE, 5);
    else if (m.type == MediaType::Live || m.type == MediaType::Spout || m.type == MediaType::NDI)
      dl->AddText({tl.x + 6, tl.y + 14}, IM_COL32(150, 150, 150, 255), T("parado"));
    else dl->AddText({tl.x + 6, tl.y + 14}, IM_COL32(200, 80, 80, 255), T("erro"));
    dl->AddText({start.x + 86, start.y + 6}, IM_COL32(230, 230, 230, 255), m.name.c_str());
    if (app.mediaMissing(m)) {
      dl->AddRect(tl, br, IM_COL32(230, 60, 60, 255), 5, 0, 2);
      dl->AddText({start.x + 86, start.y + 25}, IM_COL32(230, 90, 90, 255), T("arquivo não encontrado"));
      ImGui::SetItemTooltip(T("%s\nUse Arquivo > Procurar mídias ausentes ou troque o arquivo nas propriedades."), m.path.c_str());
    } else {
      dl->AddText({start.x + 86, start.y + 25}, IM_COL32(140, 140, 140, 255), mediaTypeLabel(m.type));
    }
    ImGui::PopID();
  }
  if (p.library.empty())
    ImGui::TextWrapped("%s", T("Arraste vídeos ou imagens para esta janela, ou clique em Importar. "
                       "Geradores (test card, grade...) ajudam a alinhar projetores."));
  ImGui::EndChild();

  if (removeId >= 0) {
    p.removeMedia(removeId);
    app.engine.cache.invalidate(removeId);
    if (app.sel.mediaId == removeId) app.sel.mediaId = -1;
    app.edited();
  }
}

void Ui::dashboard(App &app) {
  Engine &e = app.engine;
  Project &p = e.project;
  const bool locked = app.showLocked; // show mode: launching only, no editing

  ImGui::BeginDisabled(locked);
  if (ImGui::Button(T("+ Layer"))) {
    Layer &l = p.addLayer("", workspace.viewCanvas);
    app.sel.layerId = l.id;
    app.sel.slice = 0;
    app.edited();
  }
  ImGui::SameLine();
  if (ImGui::Button(T("+ Coluna"))) {
    p.addColumn();
    app.edited();
  }
  ImGui::EndDisabled();
  ImGui::SameLine();
  ImGui::TextDisabled("%s", T("Arraste mídias para as células. Clique numa célula para disparar."));
  ImGui::SameLine(std::max(ImGui::GetCursorPosX() + 10, ImGui::GetWindowWidth() - 150));
  ImGui::SetNextItemWidth(130);
  ImGui::Combo("##zoom", &dashZoom_, TZ("pequeno\0médio\0grande\0"));

  const float zw[] = {86, 116, 160}, zh[] = {50, 74, 104};
  const float cellW = zw[std::clamp(dashZoom_, 0, 2)], cellH = zh[std::clamp(dashZoom_, 0, 2)], nameW = 150;
  int ncols = int(p.columns.size());
  ImGuiTableFlags flags = ImGuiTableFlags_ScrollX | ImGuiTableFlags_ScrollY |
                          ImGuiTableFlags_BordersInner | ImGuiTableFlags_RowBg;
  if (!ImGui::BeginTable("##dash", ncols + 2, flags)) return;
  ImGui::TableSetupScrollFreeze(1, 1);
  ImGui::TableSetupColumn("##layers", ImGuiTableColumnFlags_WidthFixed, nameW);
  for (int c = 0; c < ncols; ++c)
    ImGui::TableSetupColumn(p.columns[size_t(c)].name.c_str(), ImGuiTableColumnFlags_WidthFixed, cellW);
  ImGui::TableSetupColumn("##stop", ImGuiTableColumnFlags_WidthFixed, 40);

  // Header row: one launch button per column.
  ImGui::TableNextRow(ImGuiTableRowFlags_Headers);
  ImGui::TableSetColumnIndex(0);
  ImGui::TextDisabled("%s", T("Layers \\ Cues"));
  int removeColumn = -1, moveColumn = -1, moveDir = 0;
  for (int c = 0; c < ncols; ++c) {
    ImGui::TableSetColumnIndex(c + 1);
    ImGui::PushID(p.columns[size_t(c)].id);
    Column &colRef = p.columns[size_t(c)];
    bool active = e.activeColumn() == c;
    if (active) pushPrimary();
    char label[96];
    std::snprintf(label, sizeof(label), T("%02d  \xe2\x96\xb6  %s"), c + 1, colRef.name.c_str()); // ▶
    if (ImGui::Button(label, {cellW - 4, 0})) e.launchColumn(c);
    if (active) popPrimary();
    if (colRef.color[0] > 0 || colRef.color[1] > 0 || colRef.color[2] > 0) {
      ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
      ImGui::GetWindowDrawList()->AddRectFilled({a.x, b.y - 3}, {b.x, b.y},
          ImGui::GetColorU32(ImVec4(colRef.color[0], colRef.color[1], colRef.color[2], 1)));
    }
    if (!colRef.notes.empty()) ImGui::SetItemTooltip("%s", colRef.notes.c_str());
    // Transition line (click: column window), like Millumin's "no transition ⚙".
    {
      float dur = colRef.transition < 0 ? p.defaultTransition : colRef.transition;
      char tl[64];
      if (colRef.transitionType == TransitionType::Cut || dur <= 0) std::snprintf(tl, sizeof(tl), "%s", T("corte"));
      else std::snprintf(tl, sizeof(tl), T("%s %.1fs"), colRef.transitionType == TransitionType::ThroughBlack ? T("pelo preto") : T("crossfade"), dur);
      // Auto-go: countdown on the live column, wait time on the others.
      if (colRef.autoGo >= 0) {
        size_t n = std::strlen(tl);
        float left = active ? e.autoGoRemaining() : -1;
        if (left >= 0) std::snprintf(tl + n, sizeof(tl) - n, T("  \xe2\x8f\xb1%.1f"), left); // ⏱
        else std::snprintf(tl + n, sizeof(tl) - n, T("  \xe2\x8f\xb1%.0fs"), colRef.autoGo);
      }
      ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
      ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
      if (ImGui::SmallButton(tl) && !locked) ImGui::OpenPopup("colpop");
      ImGui::PopStyleColor(2);
    }
    bool popupOpen = !locked && ImGui::BeginPopup("colpop");
    if (!popupOpen && !locked) popupOpen = ImGui::BeginPopupContextItem("colpop");
    if (popupOpen) {
      ImGui::SeparatorText(T("Coluna"));
      ImGui::SetNextItemWidth(220);
      if (inputText(T("Título"), colRef.name)) app.edited();
      {
        char notes[1024];
        std::snprintf(notes, sizeof(notes), "%s", colRef.notes.c_str());
        if (ImGui::InputTextMultiline(T("Notas"), notes, sizeof(notes), {220, 54})) {
          colRef.notes = notes;
          app.edited();
        }
      }
      ImGui::SeparatorText(T("Transição"));
      int tt = int(colRef.transitionType);
      ImGui::SetNextItemWidth(160);
      if (ImGui::Combo(T("Tipo"), &tt, TZ("Crossfade\0Corte (sem transição)\0Fade pelo preto\0Shader (ISF)\0"))) {
        colRef.transitionType = TransitionType(tt);
        if (colRef.transitionType == TransitionType::Shader && colRef.transitionShader.empty())
          for (auto &x : e.isfCatalog.builtins())
            if (x.info.transition) { colRef.transitionShader = x.path; break; }
        app.edited();
      }
      if (colRef.transitionType == TransitionType::Shader) {
        std::string cur = colRef.transitionShader.empty() ? T("(escolha)") : platform::toUtf8(platform::toPath(colRef.transitionShader).stem());
        if (cur.rfind("T_", 0) == 0) cur = cur.substr(2);
        ImGui::SetNextItemWidth(160);
        if (ImGui::BeginCombo(T("Shader"), cur.c_str())) {
          for (auto &x : e.isfCatalog.builtins()) {
            if (!x.info.transition) continue;
            std::string n = x.name.rfind("T_", 0) == 0 ? x.name.substr(2) : x.name;
            if (ImGui::Selectable(n.c_str(), x.path == colRef.transitionShader)) { colRef.transitionShader = x.path; app.edited(); }
            if (!x.info.description.empty()) ImGui::SetItemTooltip("%s", x.info.description.c_str());
          }
          if (ImGui::Selectable(T("Abrir arquivo .fs..."))) {
            auto files = platform::openFileDialog("Transição ISF", "ISF|*.fs|Todos|*.*", false);
            if (!files.empty()) { colRef.transitionShader = files[0]; app.edited(); }
          }
          ImGui::EndCombo();
        }
      }
      if (ImGui::Checkbox(T("Manual (fader)"), &colRef.manualTransition)) app.edited();
      helpMarker(T("A transição anda com o fader de transição (barra de cima, MIDI ou OSC "
                 "/lumen/transition). Para assumir, leve o fader a 0 e depois suba até 1."));
      {
        const MediaItem *sm = p.media(colRef.stingerMediaId);
        ImGui::SetNextItemWidth(160);
        if (ImGui::BeginCombo(T("Stinger"), sm ? sm->name.c_str() : T("(nenhum)"))) {
          if (ImGui::Selectable(T("(nenhum)"), !sm)) { colRef.stingerMediaId = -1; app.edited(); }
          for (auto &m : p.library)
            if (m.type == MediaType::Video && ImGui::Selectable(m.name.c_str(), m.id == colRef.stingerMediaId)) {
              colRef.stingerMediaId = m.id;
              app.edited();
            }
          ImGui::EndCombo();
        }
        helpMarker(T("Vídeo (com transparência, ex. HAP Alpha ou ProRes 4444) tocado por cima de tudo; "
                   "o cue troca com um corte no meio dele, escondido pela animação."));
        if (sm) {
          ImGui::SetNextItemWidth(160);
          if (ImGui::SliderFloat(T("Troca em"), &colRef.stingerCut, 0, 1, T("%.2f do vídeo"))) app.edited();
        }
      }
      float &tr = colRef.transition;
      bool custom = tr >= 0;
      if (ImGui::Checkbox(T("Duração própria"), &custom)) {
        tr = custom ? p.defaultTransition : -1;
        app.edited();
      }
      if (custom) {
        ImGui::SetNextItemWidth(160);
        if (ImGui::DragFloat(T("Duração (s)"), &tr, 0.05f, 0, 60, "%.2f s")) app.edited();
      } else {
        ImGui::SameLine();
        ImGui::TextDisabled(T("(padrão %.2f s)"), p.defaultTransition);
      }
      ImGui::SeparatorText(T("Auto-GO"));
      {
        bool on = colRef.autoGo >= 0;
        if (ImGui::Checkbox(T("Disparar outra coluna sozinho"), &on)) {
          colRef.autoGo = on ? 10.0f : -1.0f;
          app.edited();
        }
        helpMarker(T("Para instalações: depois do tempo de espera, dispara a próxima coluna "
                   "(na última, volta para a primeira) ou uma coluna escolhida."));
        if (on) {
          ImGui::SetNextItemWidth(160);
          if (ImGui::DragFloat(T("Espera (s)"), &colRef.autoGo, 0.1f, 0, 36000, "%.1f s")) app.edited();
          if (ImGui::Checkbox(T("Contar depois da transição"), &colRef.autoGoAfterTransition)) app.edited();
          ImGui::SetNextItemWidth(160);
          if (ImGui::InputInt(T("Coluna (0 = próxima)"), &colRef.autoGoTarget)) {
            colRef.autoGoTarget = std::clamp(colRef.autoGoTarget, 0, ncols);
            app.edited();
          }
        }
      }
      ImGui::SeparatorText(T("Timeline"));
      {
        const Timeline *tl = p.timeline(colRef.timelineId);
        ImGui::SetNextItemWidth(180);
        if (ImGui::BeginCombo(T("Ao disparar, tocar"), tl ? tl->name.c_str() : T("(nenhuma)"))) {
          if (ImGui::Selectable(T("(nenhuma)"), !tl)) { colRef.timelineId = -1; app.edited(); }
          for (auto &t : p.timelines)
            if (ImGui::Selectable(t.name.c_str(), t.id == colRef.timelineId)) { colRef.timelineId = t.id; app.edited(); }
          if (ImGui::Selectable(T("+ Nova timeline"))) {
            Timeline &t = p.addTimeline();
            colRef.timelineId = t.id;
            showTimeline(t.id);
            app.edited();
          }
          ImGui::EndCombo();
        }
        helpMarker(T("A timeline começa quando a coluna é disparada e para quando outra coluna "
                   "(sem ela) entra. Se a próxima coluna usa a mesma timeline, ela continua."));
      }
      ImGui::SeparatorText(T("Comandos para equipamentos"));
      {
        std::string text;
        for (auto &line : colRef.commands) text += line + "\n";
        char buf[2048];
        std::snprintf(buf, sizeof(buf), "%s", text.c_str());
        if (ImGui::InputTextMultiline("##cmds", buf, sizeof(buf), {320, 70})) {
          colRef.commands.clear();
          std::string cur;
          for (const char *q = buf; *q; ++q) {
            if (*q == '\n') { if (!cur.empty()) colRef.commands.push_back(cur); cur.clear(); }
            else cur += *q;
          }
          if (!cur.empty()) colRef.commands.push_back(cur);
          app.edited();
        }
        ImGui::TextDisabled("%s", T("Ex.: pjlink Sala power on · wait 2 · midi note 1 60 127 · dmx 0 5 255"));
        helpMarker(T("Um por linha, em ordem. Veja docs/DISPOSITIVOS.md:\n"
                   "pjlink <projetor> power on|off, shutter close|open, input 31\n"
                   "midi note|noteoff|cc|pc <canal> ...  (midi@nome para escolher a porta)\n"
                   "dmx [artnet|sacn] <universo> <canal>[-<último>] <valor>\n"
                   "tcp|udp <ip>:<porta> <texto>   serial COM3@9600 <texto>\n"
                   "osc <ip>:<porta> /endereco args   wait <segundos>"));
      }
      ImGui::SeparatorText(T("Timecode"));
      {
        bool on = colRef.timecodeCue >= 0;
        if (ImGui::Checkbox(T("Disparar no timecode"), &on)) { colRef.timecodeCue = on ? 0.0f : -1.0f; app.edited(); }
        if (on) {
          int hmsf[4];
          float t = colRef.timecodeCue;
          hmsf[0] = int(t / 3600); hmsf[1] = int(std::fmod(t, 3600) / 60); hmsf[2] = int(std::fmod(t, 60));
          hmsf[3] = int(std::lround((t - std::floor(t)) * 25.0f));
          ImGui::SetNextItemWidth(180);
          if (ImGui::InputInt4(T("hh mm ss qq"), hmsf)) {
            colRef.timecodeCue = float(std::max(0, hmsf[0]) * 3600 + std::clamp(hmsf[1], 0, 59) * 60 + std::clamp(hmsf[2], 0, 59)) +
                                 float(std::clamp(hmsf[3], 0, 29)) / 25.0f;
            app.edited();
          }
          helpMarker(T("A coluna dispara quando o timecode recebido (MTC ou LTC, em Interação) passa por este valor."));
        }
      }
      ImGui::SeparatorText(T("Estados (animar parâmetros)"));
      {
        helpMarker(T("Grava o valor atual de parâmetros de uma layer nesta coluna. Ao disparar a "
                   "coluna, eles vão até esse valor durante a transição. Colunas sem valor "
                   "herdam o da coluna anterior."));
        Layer *sl = p.layer(app.sel.layerId);
        static const MappingTarget recordable[] = {
            MappingTarget::Opacity,    MappingTarget::PositionX,  MappingTarget::PositionY,
            MappingTarget::Scale,      MappingTarget::Rotation,   MappingTarget::Brightness,
            MappingTarget::Contrast,   MappingTarget::Saturation, MappingTarget::Hue,
            MappingTarget::Volume};
        if (sl) {
          ImGui::Text(T("Layer selecionada: %s"), sl->name.c_str());
          if (ImGui::Button(T("Gravar opacidade + transformação"))) {
            for (auto t : {MappingTarget::Opacity, MappingTarget::PositionX, MappingTarget::PositionY,
                           MappingTarget::Scale, MappingTarget::Rotation})
              e.storeState(c, sl->id, t);
            app.edited();
          }
          ImGui::SameLine();
          if (ImGui::Button(T("Gravar..."))) ImGui::OpenPopup("statepick");
          if (ImGui::BeginPopup("statepick")) {
            for (auto t : recordable)
              if (ImGui::MenuItem(mappingTargetName(t))) { e.storeState(c, sl->id, t); app.edited(); }
            ImGui::EndPopup();
          }
        } else {
          ImGui::TextDisabled("%s", T("Selecione uma layer para gravar estados."));
        }
        int removeState = -1;
        for (size_t si = 0; si < colRef.states.size(); ++si) {
          ColumnState &cs = colRef.states[si];
          const Layer *L = p.layer(cs.layerId);
          ImGui::PushID(int(si));
          if (ImGui::SmallButton("x")) removeState = int(si);
          ImGui::SameLine();
          ImGui::SetNextItemWidth(110);
          char lbl[128];
          std::snprintf(lbl, sizeof(lbl), "%s · %s", L ? L->name.c_str() : "?", mappingTargetName(MappingTarget(cs.param)));
          if (ImGui::DragFloat(lbl, &cs.value, 0.01f)) app.edited();
          ImGui::PopID();
        }
        if (removeState >= 0) {
          colRef.states.erase(colRef.states.begin() + removeState);
          app.edited();
        }
      }
      ImGui::SeparatorText(T("Organizar"));
      if (ImGui::ColorEdit3(T("Cor"), colRef.color, ImGuiColorEditFlags_NoInputs)) app.edited();
      if (ImGui::MenuItem(T("Mover para a esquerda"), nullptr, false, c > 0)) { moveColumn = c; moveDir = -1; }
      if (ImGui::MenuItem(T("Mover para a direita"), nullptr, false, c + 1 < ncols)) { moveColumn = c; moveDir = 1; }
      ImGui::SeparatorText(T("Ações OSC ao disparar"));
      {
        std::string text;
        for (auto &line : p.columns[size_t(c)].oscActions) text += line + "\n";
        char buf[2048];
        std::snprintf(buf, sizeof(buf), "%s", text.c_str());
        if (ImGui::InputTextMultiline("##osc", buf, sizeof(buf), {320, 80})) {
          auto &acts = p.columns[size_t(c)].oscActions;
          acts.clear();
          std::string cur;
          for (const char *q = buf; *q; ++q) {
            if (*q == '\n') { if (!cur.empty()) acts.push_back(cur); cur.clear(); }
            else cur += *q;
          }
          if (!cur.empty()) acts.push_back(cur);
          app.edited();
        }
        ImGui::TextDisabled("%s", T("Uma por linha, ex.: /luz/cena 3   (vai para as saídas OSC)"));
      }
      if (ImGui::MenuItem(T("Remover coluna"))) removeColumn = c;
      ImGui::EndPopup();
    }
    ImGui::PopID();
  }
  // Stop column: stops everything, and per layer below.
  ImGui::TableSetColumnIndex(ncols + 1);
  if (ImGui::Button("\xe2\x96\xa0##stopall", {32, 0})) e.stopAll(); // ■
  ImGui::SetItemTooltip("%s", T("Parar tudo (Esc)"));

  int removeLayer = -1, moveLayer = -1, moveLayerDir = 0;
  for (size_t li = 0; li < p.layers.size(); ++li) {
    Layer &l = p.layers[li];
    ImGui::TableNextRow(0, cellH);
    ImGui::TableSetColumnIndex(0);
    ImGui::PushID(l.id);
    bool selected = app.sel.layerId == l.id;
    if (ImGui::Checkbox("##vis", &l.visible)) app.edited();
    ImGui::SetItemTooltip("%s", T("Visível"));
    ImGui::SameLine();
    std::string rowName = p.canvases.empty() ? l.name : l.name + T("  \xc2\xb7 ") + p.canvasLabel(l.canvasId); // ·
    if (ImGui::Selectable(rowName.c_str(), selected, 0, {nameW - 36, 0})) {
      app.sel.layerId = l.id;
      app.sel.slice = 0;
      app.sel.inspector = Inspector::Layer;
      if (workspace.viewOutputId < 0) workspace.viewCanvas = l.canvasId;
    }
    if (!locked && ImGui::BeginPopupContextItem()) {
      ImGui::SetNextItemWidth(160);
      if (inputText(T("Nome"), l.name)) app.edited();
      if (ImGui::MenuItem(T("Subir"), nullptr, false, li > 0)) { moveLayer = int(li); moveLayerDir = -1; }
      if (ImGui::MenuItem(T("Descer"), nullptr, false, li + 1 < p.layers.size())) { moveLayer = int(li); moveLayerDir = 1; }
      if (ImGui::MenuItem(T("Parar layer"))) e.stopLayer(l.id);
      if (ImGui::MenuItem(T("Remover layer"))) removeLayer = l.id;
      ImGui::EndPopup();
    }
    ImGui::SetNextItemWidth(nameW - 12);
    if (ImGui::SliderFloat("##op", &l.opacity, 0, 1, "%.2f")) app.edited();
    const LayerState *st = e.layerState(l.id);

    for (int c = 0; c < ncols; ++c) {
      ImGui::TableSetColumnIndex(c + 1);
      Column &col = p.columns[size_t(c)];
      ImGui::PushID(col.id);
      auto it = col.cells.find(l.id);
      Cell *cell = (it != col.cells.end() && it->second.mediaId >= 0) ? &it->second : nullptr;
      MediaItem *m = cell ? p.media(cell->mediaId) : nullptr;

      ImVec2 pos = ImGui::GetCursorScreenPos();
      ImVec2 size{cellW - 6, cellH - 6};
      ImGui::InvisibleButton("##cell", size);
      bool hovered = ImGui::IsItemHovered();
      if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
        app.sel.cellColumn = c;
        app.sel.cellLayer = l.id;
        app.sel.layerId = l.id;
        if (m) app.sel.inspector = Inspector::Cell;
        e.launchCell(c, l.id);
      }
      if (cell && !locked && ImGui::BeginDragDropSource()) {
        int payload[2] = {c, l.id};
        ImGui::SetDragDropPayload("LUMEN_CELL", payload, sizeof(payload));
        ImGui::Text(T("%s  (Ctrl = copiar)"), m ? m->name.c_str() : "");
        ImGui::EndDragDropSource();
      }
      if (!locked && ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload *pl = ImGui::AcceptDragDropPayload("LUMEN_MEDIA")) {
          Cell nc;
          nc.mediaId = *static_cast<const int *>(pl->Data);
          col.cells[l.id] = nc;
          app.edited();
        }
        if (const ImGuiPayload *pl = ImGui::AcceptDragDropPayload("LUMEN_CELL")) {
          const int *src = static_cast<const int *>(pl->Data);
          if (src[0] != c || src[1] != l.id) {
            Column &sc = p.columns[size_t(src[0])];
            Cell copy = sc.cells[src[1]];
            col.cells[l.id] = copy;
            if (!ImGui::GetIO().KeyCtrl) sc.cells.erase(src[1]);
            app.edited();
          }
        }
        ImGui::EndDragDropTarget();
      }
      if (cell && !locked && ImGui::BeginPopupContextItem("##cellctx")) {
        ImGui::TextDisabled("%s", T("No fim da mídia"));
        for (int em = 0; em < int(EndMode::COUNT); ++em) {
          const char *names[] = {T("Loop"), T("Congelar no último quadro"), T("Parar"),
                                        T("Disparar a próxima coluna")};
          if (ImGui::MenuItem(names[em], nullptr, int(cell->endMode) == em)) {
            cell->setEndMode(EndMode(em));
            app.edited();
          }
        }
        ImGui::Separator();
        if (ImGui::MenuItem(T("Limpar célula"))) {
          col.cells.erase(l.id);
          cell = nullptr;
          m = nullptr;
          app.edited();
        }
        ImGui::EndPopup();
      }

      ImDrawList *dl = ImGui::GetWindowDrawList();
      ImVec2 br{pos.x + size.x, pos.y + size.y};
      bool playing = st && st->current && st->columnId == col.id && m && st->current->mediaId() == m->id;
      dl->AddRectFilled(pos, br, hovered ? IM_COL32(44, 44, 47, 255) : IM_COL32(28, 28, 30, 255), 7);
      if (m) {
        const Texture *thumb = e.cache.thumbnail(*m, e.generators, e.mesh);
        if (thumb && !thumb->empty()) {
          ImVec2 ttl{pos.x + 3, pos.y + 3}, tbr{br.x - 3, br.y - 20};
          dl->AddImageRounded(texId(*thumb), ttl, tbr, {0, 0}, {1, 1}, IM_COL32_WHITE, 5);
        }
        dl->PushClipRect(pos, br, true);
        dl->AddText({pos.x + 5, br.y - 18}, IM_COL32(220, 220, 220, 255), m->name.c_str());
        dl->PopClipRect();
        static const char *endMark[] = {"", "1x", "\xe2\x96\xa0", "\xe2\x86\x92"}; // ■ →
        if (cell->endMode != EndMode::Loop)
          dl->AddText({br.x - 16, br.y - 18}, IM_COL32(160, 160, 160, 255), endMark[int(cell->endMode)]);
      } else {
        dl->AddText({pos.x + size.x * 0.5f - 4, pos.y + size.y * 0.5f - 8},
                    IM_COL32(70, 70, 75, 255), "+");
      }
      // ◆: this column stores parameter states of this layer.
      if (std::any_of(col.states.begin(), col.states.end(), [&](auto &x) { return x.layerId == l.id; }))
        dl->AddText({pos.x + 4, pos.y + 2}, kAccentU32, "\xe2\x97\x86");
      if (playing) {
        dl->AddRect(pos, br, kPlayingU32, 7, 0, 2.5f);
        double dur = st->current->duration();
        if (dur > 0) {
          float t = float(std::fmod(st->current->time(), dur) / dur);
          dl->AddRectFilled({pos.x + 3, br.y - 3}, {pos.x + 3 + (size.x - 6) * t, br.y - 1}, kPlayingU32);
        }
      } else if (app.sel.cellColumn == c && app.sel.cellLayer == l.id) {
        dl->AddRect(pos, br, IM_COL32(235, 235, 235, 200), 7, 0, 1.5f);
      }
      ImGui::PopID();
    }
    ImGui::TableSetColumnIndex(ncols + 1);
    if (st && (st->current || st->previous)) {
      if (ImGui::Button("\xe2\x96\xa0##stopl", {32, cellH - 10})) e.stopLayer(l.id); // ■
      ImGui::SetItemTooltip("%s", T("Parar esta layer"));
    }
    ImGui::PopID();
  }
  ImGui::EndTable();

  if (removeColumn >= 0) {
    p.columns.erase(p.columns.begin() + removeColumn);
    app.edited();
  }
  if (moveColumn >= 0) {
    std::swap(p.columns[size_t(moveColumn)], p.columns[size_t(moveColumn + moveDir)]);
    app.edited();
  }
  if (removeLayer >= 0) {
    p.removeLayer(removeLayer);
    if (app.sel.layerId == removeLayer) app.sel.layerId = -1;
    app.edited();
  }
  if (moveLayer >= 0) {
    std::swap(p.layers[size_t(moveLayer)], p.layers[size_t(moveLayer + moveLayerDir)]);
    app.edited();
  }
}

void Ui::inspector(App &app) {
  struct Tab { const char *name; Inspector id; };
  const Tab tabs[] = {{T("Layer"), Inspector::Layer},   {T("Célula"), Inspector::Cell},
                      {T("Mídia"), Inspector::Media},   {T("Saídas"), Inspector::Output}, {T("LED"), Inspector::Led},
                      {T("Interação"), Inspector::Interaction}, {T("Projeto"), Inspector::Project}};
  static Inspector shown = Inspector::Layer;
  bool forced = shown != app.sel.inspector;
  if (ImGui::BeginTabBar("##insp")) {
    for (auto &t : tabs) {
      ImGuiTabItemFlags f = (forced && app.sel.inspector == t.id) ? ImGuiTabItemFlags_SetSelected : 0;
      if (ImGui::BeginTabItem(t.name, nullptr, f)) {
        if (!forced) app.sel.inspector = t.id;
        ImGui::BeginChild("##inspbody");
        switch (t.id) {
        case Inspector::Layer: layerInspector(app); break;
        case Inspector::Cell: cellInspector(app); break;
        case Inspector::Media: mediaInspector(app); break;
        case Inspector::Output: outputInspector(app); break;
        case Inspector::Interaction: interactionInspector(app); break;
        case Inspector::Project: projectInspector(app); break;
        case Inspector::Led: ledInspector(app); break;
        }
        ImGui::EndChild();
        ImGui::EndTabItem();
      }
    }
    ImGui::EndTabBar();
  }
  shown = app.sel.inspector;
}

// Mask list and properties, shared by layer masks (canvas px, unit = 1) and
// output masks (unit = output size in px, points stored 0..1).
void Ui::maskList(App &app, std::vector<Mask> &masks, int &sel, int &selPoint, Vec2 unit, Vec2 area,
                  bool outputSpace) {
  int remove = -1, move = -1, dir = 0;
  for (size_t i = 0; i < masks.size(); ++i) {
    Mask &m = masks[i];
    ImGui::PushID(int(i) + 9100);
    if (ImGui::Checkbox("##en", &m.enabled)) app.edited();
    ImGui::SameLine();
    bool valid = m.points.size() >= 3;
    if (!valid) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.77f, 0.0f, 1)); // like Millumin: orange = no shape yet
    char label[160];
    std::snprintf(label, sizeof(label), "%s  %s%s", m.name.c_str(), m.subtract ? T("(esconde)") : T("(mostra)"),
                  m.inverted ? T(" inv.") : "");
    if (ImGui::Selectable(label, sel == int(i))) { sel = int(i); selPoint = -1; }
    if (!valid) ImGui::PopStyleColor();
    if (ImGui::BeginPopupContextItem()) {
      if (ImGui::MenuItem(T("Subir"), nullptr, false, i > 0)) { move = int(i); dir = -1; }
      if (ImGui::MenuItem(T("Descer"), nullptr, false, i + 1 < masks.size())) { move = int(i); dir = 1; }
      if (ImGui::MenuItem(T("Duplicar"))) {
        Mask c = m;
        c.name += T(" cópia");
        masks.insert(masks.begin() + int(i) + 1, c);
        app.edited();
        ImGui::EndPopup();
        ImGui::PopID();
        break;
      }
      if (ImGui::MenuItem(T("Remover"))) remove = int(i);
      ImGui::EndPopup();
    }
    ImGui::PopID();
  }
  if (remove >= 0) {
    masks.erase(masks.begin() + remove);
    sel = -1;
    app.edited();
  }
  if (move >= 0) {
    std::swap(masks[size_t(move)], masks[size_t(move + dir)]);
    sel = move + dir;
    app.edited();
  }
  ImGui::TextDisabled("%s", T("Aplicadas de cima para baixo (botão direito: subir / descer)."));

  auto add = [&](Mask m) {
    m.name = T("Máscara ") + std::to_string(masks.size() + 1);
    if (outputSpace) { m.subtract = true; m.feather = 20; }
    masks.push_back(m);
    sel = int(masks.size()) - 1;
    selPoint = -1;
    workspace.tool = Tool::Mask;
    app.edited();
  };
  if (ImGui::Button(T("+ Máscara"))) add(Mask{});
  ImGui::SameLine();
  if (ImGui::Button(T("Forma..."))) ImGui::OpenPopup("##shape");
  if (ImGui::BeginPopup("##shape")) {
    // Centred, a third of the area (canvas or output).
    Vec2 c{area.x * 0.5f, area.y * 0.5f};
    float r = std::min(area.x * unit.x, area.y * unit.y) / 6.0f; // pixels
    float rx = r / unit.x, ry = r / unit.y;
    Mask m;
    if (ImGui::MenuItem(T("Quadrado"))) {
      m.points = {{c.x - rx, c.y - ry}, {c.x + rx, c.y - ry}, {c.x + rx, c.y + ry}, {c.x - rx, c.y + ry}};
      add(m);
    }
    if (ImGui::MenuItem(T("Círculo"))) {
      const float k = 0.5522847f;
      m.points = {{c.x + rx, c.y}, {c.x, c.y + ry}, {c.x - rx, c.y}, {c.x, c.y - ry}};
      m.tanIn = {{0, -ry * k}, {rx * k, 0}, {0, ry * k}, {-rx * k, 0}};
      m.tanOut = {{0, ry * k}, {-rx * k, 0}, {0, -ry * k}, {rx * k, 0}};
      add(m);
    }
    if (ImGui::MenuItem(T("Triângulo"))) {
      m.points = {{c.x, c.y - ry}, {c.x + rx, c.y + ry}, {c.x - rx, c.y + ry}};
      add(m);
    }
    ImGui::EndPopup();
  }
  ImGui::SameLine();
  if (ImGui::Button(T("Importar SVG"))) {
    auto files = platform::openFileDialog("Importar máscaras (SVG)", "SVG|*.svg|Todos|*.*", false);
    std::string text, err;
    if (!files.empty() && platform::readFile(files[0], text)) {
      auto imported = masksFromSvg(text, area.x * unit.x, area.y * unit.y, &err);
      for (auto &m : imported) {
        for (auto &v : m.points) v = {v.x / unit.x, v.y / unit.y};
        for (auto &v : m.tanIn) v = {v.x / unit.x, v.y / unit.y};
        for (auto &v : m.tanOut) v = {v.x / unit.x, v.y / unit.y};
        if (outputSpace) m.subtract = true;
        masks.push_back(m);
      }
      if (!imported.empty()) {
        sel = int(masks.size()) - 1;
        app.edited();
        app.status(std::to_string(imported.size()) + T(" máscara(s) importada(s) do SVG"));
      } else {
        app.status(T("SVG: ") + err, true);
      }
    }
  }
  ImGui::SameLine();
  ImGui::BeginDisabled(masks.empty());
  if (ImGui::Button(T("Exportar SVG"))) {
    std::string path = platform::saveFileDialog(T("Exportar máscaras (SVG)"), T("SVG|*.svg"), "mascaras.svg");
    if (!path.empty()) {
      std::vector<Mask> scaled = masks;
      for (auto &m : scaled) {
        for (auto &v : m.points) v = {v.x * unit.x, v.y * unit.y};
        for (auto &v : m.tanIn) v = {v.x * unit.x, v.y * unit.y};
        for (auto &v : m.tanOut) v = {v.x * unit.x, v.y * unit.y};
      }
      if (platform::writeFile(path, masksToSvg(scaled, area.x * unit.x, area.y * unit.y))) app.status(T("Máscaras exportadas: ") + path);
      else app.status(T("Não foi possível salvar ") + path, true);
    }
  }
  ImGui::EndDisabled();
  helpMarker(T("SVG: desenhe as formas no Illustrator / Inkscape sobre uma foto do cenário no tamanho "
             "do canvas (ou do projetor) e importe. Caminhos, polígonos, retângulos, círculos e "
             "curvas viram máscaras."));

  if (sel < 0 || sel >= int(masks.size())) return;
  Mask &m = masks[size_t(sel)];
  ImGui::SeparatorText(m.name.c_str());
  ImGui::PushID("maskprops");
  if (inputText(T("Nome"), m.name)) app.edited();
  int mode = m.subtract ? 1 : 0;
  if (ImGui::Combo(T("Modo"), &mode, TZ("Mostrar pixels\0Esconder pixels\0"))) { m.subtract = mode == 1; app.edited(); }
  ImGui::SameLine();
  if (ImGui::Checkbox(T("Invertida"), &m.inverted)) app.edited();
  helpMarker(T("Invertida: age fora da forma em vez de dentro."));
  if (ImGui::SliderFloat(T("Opacidade"), &m.opacity, 0, 1)) app.edited();
  if (ImGui::SliderFloat(T("Suavizar borda (px)"), &m.feather, 0, 300)) app.edited();
  if (ImGui::DragFloat(T("Escala"), &m.scale, 0.005f, 0.001f, 20, "%.3f")) { m.scale = std::max(0.001f, m.scale); app.edited(); }
  ImGui::Text(T("%zu pontos"), m.points.size());
  ImGui::SameLine();
  if (ImGui::SmallButton(T("Tudo curvo"))) {
    m.ensureTangents();
    size_t n = m.points.size();
    for (size_t i = 0; i < n && n >= 3; ++i) {
      Vec2 d = (m.points[(i + 1) % n] - m.points[(i + n - 1) % n]) * (1.0f / 6.0f);
      m.tanIn[i] = d * -1.0f;
      m.tanOut[i] = d;
    }
    app.edited();
  }
  ImGui::SameLine();
  if (ImGui::SmallButton(T("Tudo reto"))) { m.tanIn.clear(); m.tanOut.clear(); app.edited(); }
  ImGui::SameLine();
  if (ImGui::SmallButton(T("Limpar pontos"))) { m.points.clear(); m.tanIn.clear(); m.tanOut.clear(); selPoint = -1; app.edited(); }
  ImGui::TextDisabled("%s", T("Alt+clique num ponto: curva / canto. Arraste as alças (Shift: só uma)."));
  ImGui::PopID();
}

static std::string timeLabel(float t) {
  char b[32];
  int m = int(t) / 60;
  std::snprintf(b, sizeof(b), "%d:%05.2f", m, t - float(m * 60));
  return b;
}

// Parameters a track can animate for a layer (plus its effects' inputs).
static std::vector<std::pair<std::string, std::string>> trackParams(Engine &e, const Layer *l) {
  std::vector<std::pair<std::string, std::string>> out;
  if (!l) {
    out = {{"master", T("Master (opacidade)")}, {"masterVolume", T("Volume master")}, {"dmxMaster", T("DMX master")},
           {"transition", T("Fader de transição")}};
    return out;
  }
  out = {{"opacity", T("Opacidade")}, {"x", T("Posição X")},  {"y", T("Posição Y")},     {"scale", T("Escala")},
         {"rotation", T("Rotação")},  {"brightness", T("Brilho")}, {"contrast", T("Contraste")}, {"saturation", T("Saturação")},
         {"hue", T("Matiz")},         {"volume", T("Volume")},  {"speed", T("Velocidade")}};
  for (size_t i = 0; i < l->effects.size(); ++i) {
    const IsfInfo *info = e.isfCatalog.info(l->effects[i].path);
    if (!info) continue;
    std::string pre = "effect" + std::to_string(i + 1) + "/";
    std::string fxName = l->effects[i].name.empty() ? T("efeito") : l->effects[i].name;
    out.push_back({pre + T("enabled"), fxName + T(" · ligado")});
    for (auto &in : info->inputs) {
      int n = in.size();
      if (n == 0 || in.name == "progress") continue;
      if (n == 1) out.push_back({pre + in.name, fxName + " · " + in.label});
      else
        for (int k = 0; k < n; ++k)
          out.push_back({pre + in.name + "." + std::to_string(k), fxName + " · " + in.label + " [" + std::to_string(k) + "]"});
    }
  }
  return out;
}

void Ui::timelineEditor(App &app) {
  Engine &e = app.engine;
  Project &p = e.project;
  Timeline *tl = p.timeline(timelineSel_);
  if (!tl && !p.timelines.empty()) {
    tl = &p.timelines.front();
    timelineSel_ = tl->id;
  }
  // ---- header
  ImGui::SetNextItemWidth(170);
  if (ImGui::BeginCombo("##tlsel", tl ? tl->name.c_str() : T("(nenhuma timeline)"))) {
    for (auto &t : p.timelines)
      if (ImGui::Selectable((t.name + "##" + std::to_string(t.id)).c_str(), tl && t.id == tl->id)) timelineSel_ = t.id;
    ImGui::EndCombo();
  }
  ImGui::SameLine();
  if (ImGui::Button(T("+ Timeline"))) {
    Timeline &t = p.addTimeline();
    timelineSel_ = t.id;
    app.edited();
    return;
  }
  if (!tl) {
    ImGui::TextWrapped("%s", T("Uma timeline anima parâmetros no tempo com keyframes (opacidade, posição, efeitos, "
                       "masters) e dispara comandos em marcadores. Ligue-a a uma coluna (menu da coluna → "
                       "Timeline) ou toque por OSC: /lumen/timeline/1/play."));
    return;
  }
  ImGui::SameLine();
  ImGui::SetNextItemWidth(120);
  if (inputText("##tlname", tl->name)) app.edited();
  ImGui::SameLine();
  ImGui::SetNextItemWidth(80);
  if (ImGui::DragFloat(T("duração"), &tl->duration, 0.1f, 0.1f, 36000, "%.1f s")) { tl->duration = std::max(0.1f, tl->duration); app.edited(); }
  ImGui::SameLine();
  if (ImGui::Checkbox(T("loop"), &tl->loop)) app.edited();
  if (tl->loop) {
    ImGui::SameLine();
    ImGui::SetNextItemWidth(70);
    if (ImGui::DragFloat("##ls", &tl->loopStart, 0.05f, 0, tl->duration, T("de %.1f s"))) app.edited();
    ImGui::SetItemTooltip("%s", T("Início do loop: 0 até aqui toca uma vez (introdução), depois repete daqui até o fim."));
  } else {
    ImGui::SameLine();
    if (ImGui::Checkbox(T("GO no fim"), &tl->goAtEnd)) app.edited();
  }
  ImGui::SameLine();
  if (ImGui::Checkbox(T("timecode"), &tl->followTimecode)) app.edited();
  ImGui::SetItemTooltip("%s", T("Segue o timecode recebido (MTC/LTC) em vez do próprio relógio."));
  if (tl->followTimecode) {
    ImGui::SameLine();
    ImGui::SetNextItemWidth(80);
    if (ImGui::DragFloat("##tco", &tl->timecodeOffset, 0.04f, 0, 360000, T("início %.2f s"))) app.edited();
    ImGui::SetItemTooltip("%s", T("Timecode (em segundos) em que esta timeline está no tempo 0."));
  }
  const TimelineRun *run = e.timelineRun(tl->id);
  float now = run ? float(run->time) : 0.0f;
  bool playing = run && run->playing;
  ImGui::SameLine(0, 14);
  if (playing) {
    pushPrimary();
    if (ImGui::Button(T("Pausar"))) e.pauseTimeline(tl->id);
    popPrimary();
  } else if (ImGui::Button(T("Tocar"))) {
    e.playTimeline(tl->id, !run || now >= tl->duration);
  }
  ImGui::SameLine();
  if (ImGui::Button(T("Parar##tl"))) { e.stopTimeline(tl->id); e.applyTimeline(*tl, 0); }
  ImGui::SameLine();
  ImGui::Text("%s / %s", timeLabel(now).c_str(), timeLabel(tl->duration).c_str());
  ImGui::SameLine();
  if (ImGui::SmallButton("...")) ImGui::OpenPopup("##tlmenu");
  if (ImGui::BeginPopup("##tlmenu")) {
    if (ImGui::MenuItem(T("Duplicar timeline"))) {
      Timeline copy = *tl;
      copy.id = p.newId();
      copy.name += T(" cópia");
      p.timelines.push_back(copy);
      timelineSel_ = copy.id;
      app.edited();
    }
    if (ImGui::MenuItem(T("Gravar o valor atual de todas as faixas aqui"))) {
      for (auto &tr : tl->tracks) {
        float v = e.getNamedParam(tr.layerId, tr.param);
        auto it = std::find_if(tr.keys.begin(), tr.keys.end(), [&](const Keyframe &k) { return std::fabs(k.time - now) < 0.02f; });
        if (it != tr.keys.end()) it->value = v;
        else tr.keys.push_back({now, v, Ease::InOut});
        std::sort(tr.keys.begin(), tr.keys.end(), [](auto &a, auto &b) { return a.time < b.time; });
      }
      app.edited();
    }
    if (ImGui::MenuItem(T("Remover timeline"))) {
      int id = tl->id;
      e.stopTimeline(id);
      p.timelines.erase(std::remove_if(p.timelines.begin(), p.timelines.end(), [&](auto &t) { return t.id == id; }), p.timelines.end());
      for (auto &c : p.columns)
        if (c.timelineId == id) c.timelineId = -1;
      timelineSel_ = -1;
      app.edited();
      ImGui::EndPopup();
      return;
    }
    ImGui::EndPopup();
  }

  // ---- lanes
  const float labelW = 220, rulerH = 22, flagH = 24, laneH = 38;
  ImVec2 avail = ImGui::GetContentRegionAvail();
  ImGui::BeginChild("##tlarea", {avail.x, avail.y}, ImGuiChildFlags_Borders, ImGuiWindowFlags_NoScrollWithMouse);
  ImDrawList *dl = ImGui::GetWindowDrawList();
  ImVec2 o = ImGui::GetCursorScreenPos();
  float W = ImGui::GetContentRegionAvail().x - labelW;
  if (W < 50) { ImGui::EndChild(); return; }
  ImGuiIO &io = ImGui::GetIO();
  // View: seconds per pixel; wheel zooms around the mouse, Shift+wheel pans.
  if (timelineView_ <= 0 || tlViewFor_ != tl->id) {
    timelineZoom_ = W / std::max(1.0f, tl->duration);
    timelineView_ = 0.001f;
    tlStart_ = 0;
    tlViewFor_ = tl->id;
  }
  float x0 = o.x + labelW;
  auto tx = [&](float t) { return x0 + (t - tlStart_) * timelineZoom_; };
  auto xt = [&](float x) { return tlStart_ + (x - x0) / timelineZoom_; };
  bool inArea = ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows) && io.MousePos.x > x0;
  if (inArea && io.MouseWheel != 0) {
    if (io.KeyShift) {
      tlStart_ -= io.MouseWheel * 60.0f / timelineZoom_;
    } else {
      float at = xt(io.MousePos.x);
      timelineZoom_ = std::clamp(timelineZoom_ * std::pow(1.2f, io.MouseWheel), 2.0f, 2000.0f);
      tlStart_ = at - (io.MousePos.x - x0) / timelineZoom_;
    }
  }
  tlStart_ = std::clamp(tlStart_, -0.5f, std::max(0.0f, tl->duration - 0.5f));
  float tEnd = xt(x0 + W);

  // Ruler.
  dl->AddRectFilled({x0, o.y}, {x0 + W, o.y + rulerH}, IM_COL32(24, 24, 26, 255), 4);
  float step = 0.1f;
  for (float c : {0.1f, 0.25f, 0.5f, 1.0f, 2.0f, 5.0f, 10.0f, 15.0f, 30.0f, 60.0f, 120.0f, 300.0f, 600.0f})
    if (c * timelineZoom_ >= 60) { step = c; break; } else step = c;
  for (float t = std::floor(std::max(0.0f, tlStart_) / step) * step; t <= std::min(tEnd, tl->duration) + 1e-3f; t += step) {
    float x = tx(t);
    dl->AddLine({x, o.y + rulerH - 7}, {x, o.y + rulerH}, IM_COL32(120, 120, 120, 255));
    dl->AddText({x + 3, o.y + 3}, IM_COL32(150, 150, 150, 255), timeLabel(t).c_str());
  }
  ImGui::SetCursorScreenPos({x0, o.y});
  ImGui::InvisibleButton("##ruler", {W, rulerH});
  if (ImGui::IsItemActive()) e.seekTimeline(tl->id, std::clamp(xt(io.MousePos.x), 0.0f, tl->duration));
  ImGui::SetCursorScreenPos(o);
  ImGui::TextDisabled("%s", T("roda: zoom · Shift+roda: rolar"));

  // Flags lane.
  float fy = o.y + rulerH + 2;
  ImGui::SetCursorScreenPos({o.x, fy + 3});
  ImGui::TextUnformatted(T("Marcadores"));
  ImGui::SetItemTooltip("%s", T("Duplo clique na faixa cria um marcador: um comando disparado quando a timeline passa por ele."));
  dl->AddRectFilled({x0, fy}, {x0 + W, fy + flagH}, IM_COL32(20, 20, 22, 255), 4);
  ImGui::SetCursorScreenPos({x0, fy});
  ImGui::InvisibleButton("##flags", {W, flagH});
  bool flagsHovered = ImGui::IsItemHovered();
  int hitFlag = -1;
  for (size_t i = 0; i < tl->flags.size(); ++i) {
    float x = tx(tl->flags[i].time);
    if (x < x0 - 5 || x > x0 + W + 5) continue;
    ImVec2 a{x, fy + 3}, b{x - 6, fy + flagH - 4}, c{x + 6, fy + flagH - 4};
    dl->AddTriangleFilled(a, b, c, kAccentU32);
    dl->AddText({x + 8, fy + 4}, IM_COL32(220, 220, 220, 255), tl->flags[i].name.empty() ? tl->flags[i].command.c_str() : tl->flags[i].name.c_str());
    if (std::fabs(io.MousePos.x - x) < 7 && io.MousePos.y >= fy && io.MousePos.y <= fy + flagH) hitFlag = int(i);
  }
  if (flagsHovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && hitFlag < 0) {
    tl->flags.push_back({std::clamp(xt(io.MousePos.x), 0.0f, tl->duration), T("Marcador"), "/lumen/go"});
    tlFlag_ = int(tl->flags.size()) - 1;
    ImGui::OpenPopup("##flagpop");
    app.edited();
  } else if (flagsHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && hitFlag >= 0) {
    tlFlag_ = hitFlag;
    tlDragFlag_ = true;
  } else if (flagsHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right) && hitFlag >= 0) {
    tlFlag_ = hitFlag;
    ImGui::OpenPopup("##flagpop");
  }
  if (tlDragFlag_ && ImGui::IsMouseDown(ImGuiMouseButton_Left) && tlFlag_ >= 0 && tlFlag_ < int(tl->flags.size())) {
    if (io.MouseDelta.x != 0) {
      tl->flags[size_t(tlFlag_)].time = std::clamp(xt(io.MousePos.x), 0.0f, tl->duration);
      app.edited();
    }
  } else {
    tlDragFlag_ = false;
  }
  if (ImGui::BeginPopup("##flagpop")) {
    if (tlFlag_ >= 0 && tlFlag_ < int(tl->flags.size())) {
      TimelineFlag &f = tl->flags[size_t(tlFlag_)];
      ImGui::SetNextItemWidth(200);
      if (inputText(T("Nome"), f.name)) app.edited();
      ImGui::SetNextItemWidth(300);
      if (inputText(T("Comando"), f.command)) app.edited();
      ImGui::TextDisabled("%s", T("Ex.: /lumen/go · /lumen/column/launch 3 · pjlink Sala shutter close · midi note 1 60 127"));
      ImGui::SetNextItemWidth(120);
      if (ImGui::DragFloat(T("Tempo"), &f.time, 0.01f, 0, tl->duration, "%.2f s")) app.edited();
      if (ImGui::Button(T("Testar agora"))) e.runCommand(f.command);
      ImGui::SameLine();
      if (ImGui::Button(T("Remover"))) {
        tl->flags.erase(tl->flags.begin() + tlFlag_);
        tlFlag_ = -1;
        app.edited();
        ImGui::CloseCurrentPopup();
      }
    }
    ImGui::EndPopup();
  }

  // Tracks.
  float y = fy + flagH + 4;
  int removeTrack = -1;
  bool changed = false;
  for (size_t ti = 0; ti < tl->tracks.size(); ++ti) {
    TimelineTrack &tr = tl->tracks[ti];
    ImGui::PushID(int(ti) + 4400);
    const Layer *l = p.layer(tr.layerId);
    std::string label = (tr.layerId < 0 ? std::string(T("Master")) : l ? l->name : std::string("?")) + " · ";
    std::string pname = tr.param;
    for (auto &[k, v] : trackParams(e, l))
      if (k == tr.param) pname = v;
    label += pname;
    ImGui::SetCursorScreenPos({o.x, y + 2});
    if (ImGui::Checkbox("##en", &tr.enabled)) app.edited();
    ImGui::SameLine();
    ImGui::PushClipRect({o.x, y}, {o.x + labelW - 56, y + laneH}, true);
    ImGui::TextUnformatted(label.c_str());
    ImGui::PopClipRect();
    ImGui::SetCursorScreenPos({o.x + labelW - 54, y + 2});
    if (ImGui::SmallButton("\xe2\x97\x86+")) { // ◆+ key with the current value at the playhead
      float v = e.getNamedParam(tr.layerId, tr.param);
      auto it = std::find_if(tr.keys.begin(), tr.keys.end(), [&](const Keyframe &k) { return std::fabs(k.time - now) < 0.02f; });
      if (it != tr.keys.end()) it->value = v;
      else tr.keys.push_back({now, v, Ease::InOut});
      std::sort(tr.keys.begin(), tr.keys.end(), [](auto &a, auto &b) { return a.time < b.time; });
      app.edited();
    }
    ImGui::SetItemTooltip("%s", T("Grava o valor atual do parâmetro no tempo do cursor"));
    ImGui::SameLine();
    if (ImGui::SmallButton("x")) removeTrack = int(ti);
    ImGui::SetCursorScreenPos({o.x + 26, y + 20});
    ImGui::TextDisabled("%s", tr.keys.empty() ? T("sem chaves") : (std::to_string(tr.keys.size()) + T(" chaves")).c_str());

    // Lane with the curve.
    dl->AddRectFilled({x0, y}, {x0 + W, y + laneH}, IM_COL32(18, 18, 20, 255), 5);
    float vmin = 0, vmax = 1;
    if (!tr.keys.empty()) {
      vmin = vmax = tr.keys[0].value;
      for (auto &k : tr.keys) { vmin = std::min(vmin, k.value); vmax = std::max(vmax, k.value); }
    }
    if (tlDragTrack_ == int(ti)) { vmin = tlDragMin_; vmax = tlDragMax_; } // stable while dragging
    if (vmax - vmin < 1e-4f) { vmin -= 0.5f; vmax += 0.5f; }
    auto vy = [&](float v) { return y + laneH - 5 - (v - vmin) / (vmax - vmin) * (laneH - 10); };
    auto yv = [&](float py) { return vmin + (y + laneH - 5 - py) / (laneH - 10) * (vmax - vmin); };
    if (!tr.keys.empty()) {
      std::vector<ImVec2> pts;
      for (float px = 0; px <= W; px += 3) {
        float t = xt(x0 + px);
        if (t < 0 || t > tl->duration) continue;
        pts.push_back({x0 + px, vy(tr.valueAt(t))});
      }
      if (pts.size() > 1) dl->AddPolyline(pts.data(), int(pts.size()), IM_COL32(255, 196, 0, tr.enabled ? 170 : 60), 0, 1.5f);
    }
    ImGui::SetCursorScreenPos({x0, y});
    ImGui::InvisibleButton("##lane", {W, laneH});
    bool laneHovered = ImGui::IsItemHovered();
    int hitKey = -1;
    for (size_t k = 0; k < tr.keys.size(); ++k) {
      ImVec2 c{tx(tr.keys[k].time), vy(tr.keys[k].value)};
      if (c.x < x0 - 6 || c.x > x0 + W + 6) continue;
      bool sel = tlKeyTrack_ == int(ti) && tlKey_ == int(k);
      float r = sel ? 6.5f : 5.0f;
      ImVec2 d[4] = {{c.x, c.y - r}, {c.x + r, c.y}, {c.x, c.y + r}, {c.x - r, c.y}};
      dl->AddConvexPolyFilled(d, 4, sel ? IM_COL32(255, 255, 255, 255) : kAccentU32);
      dl->AddPolyline(d, 4, IM_COL32(10, 10, 10, 255), ImDrawFlags_Closed, 1.0f);
      if (std::hypot(io.MousePos.x - c.x, io.MousePos.y - c.y) < 8) hitKey = int(k);
    }
    if (laneHovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && hitKey < 0) {
      float t = std::clamp(xt(io.MousePos.x), 0.0f, tl->duration);
      float v = tr.keys.empty() ? e.getNamedParam(tr.layerId, tr.param) : yv(io.MousePos.y);
      tr.keys.push_back({t, v, Ease::InOut});
      std::sort(tr.keys.begin(), tr.keys.end(), [](auto &a, auto &b) { return a.time < b.time; });
      app.edited();
      changed = true;
    } else if (laneHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && hitKey >= 0) {
      tlKeyTrack_ = int(ti);
      tlKey_ = hitKey;
      tlDragTrack_ = int(ti);
      tlDragMin_ = vmin;
      tlDragMax_ = vmax;
    } else if (laneHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right) && hitKey >= 0) {
      tlKeyTrack_ = int(ti);
      tlKey_ = hitKey;
      ImGui::OpenPopup("##keypop");
    }
    if (tlDragTrack_ == int(ti) && ImGui::IsMouseDown(ImGuiMouseButton_Left) && tlKey_ >= 0 && tlKey_ < int(tr.keys.size())) {
      if (io.MouseDelta.x != 0 || io.MouseDelta.y != 0) {
        Keyframe &kf = tr.keys[size_t(tlKey_)];
        kf.time = std::clamp(xt(io.MousePos.x), 0.0f, tl->duration);
        if (!io.KeyShift) kf.value = yv(io.MousePos.y); // Shift: only in time
        float t = kf.time;
        std::sort(tr.keys.begin(), tr.keys.end(), [](auto &a, auto &b) { return a.time < b.time; });
        for (size_t k = 0; k < tr.keys.size(); ++k)
          if (tr.keys[k].time == t) tlKey_ = int(k);
        app.edited();
        changed = true;
      }
    } else if (tlDragTrack_ == int(ti)) {
      tlDragTrack_ = -1;
    }
    if (ImGui::BeginPopup("##keypop")) {
      if (tlKeyTrack_ == int(ti) && tlKey_ >= 0 && tlKey_ < int(tr.keys.size())) {
        Keyframe &kf = tr.keys[size_t(tlKey_)];
        ImGui::SetNextItemWidth(120);
        if (ImGui::DragFloat(T("Valor"), &kf.value, 0.01f)) { app.edited(); changed = true; }
        ImGui::SetNextItemWidth(120);
        if (ImGui::DragFloat(T("Tempo"), &kf.time, 0.01f, 0, tl->duration, "%.2f s")) {
          std::sort(tr.keys.begin(), tr.keys.end(), [](auto &a, auto &b) { return a.time < b.time; });
          app.edited();
          changed = true;
        }
        ImGui::TextDisabled("%s", T("Curva até a próxima chave"));
        for (int ez = 0; ez < int(Ease::COUNT); ++ez)
          if (ImGui::RadioButton(easeName(Ease(ez)), kf.ease == Ease(ez))) { kf.ease = Ease(ez); app.edited(); changed = true; }
        if (ImGui::Button(T("Remover chave"))) {
          tr.keys.erase(tr.keys.begin() + tlKey_);
          tlKey_ = -1;
          app.edited();
          changed = true;
          ImGui::CloseCurrentPopup();
        }
      }
      ImGui::EndPopup();
    }
    ImGui::PopID();
    y += laneH + 3;
  }
  if (removeTrack >= 0) {
    tl->tracks.erase(tl->tracks.begin() + removeTrack);
    app.edited();
  }
  // Loop region and playhead over all lanes.
  if (tl->loop) dl->AddRectFilled({tx(tl->loopStart), fy}, {tx(tl->duration), y}, IM_COL32(255, 196, 0, 18));
  dl->AddRectFilled({tx(tl->duration), o.y}, {x0 + W, y}, IM_COL32(0, 0, 0, 120));
  float px = tx(now);
  if (px >= x0 && px <= x0 + W) {
    dl->AddLine({px, o.y}, {px, y}, kAccentU32, 2);
    dl->AddTriangleFilled({px - 6, o.y}, {px + 6, o.y}, {px, o.y + 8}, kAccentU32);
  }
  // Editing a stopped timeline shows the result right away.
  if (changed && !playing) e.applyTimeline(*tl, now);

  // Add a track.
  ImGui::SetCursorScreenPos({o.x, y + 4});
  if (ImGui::Button(T("+ Faixa"))) ImGui::OpenPopup("##addtrack");
  if (ImGui::BeginPopup("##addtrack")) {
    auto menu = [&](const Layer *l, const char *title) {
      if (!ImGui::BeginMenu(title)) return;
      for (auto &[k, v] : trackParams(e, l))
        if (ImGui::MenuItem(v.c_str())) {
          TimelineTrack tr;
          tr.layerId = l ? l->id : -1;
          tr.param = k;
          float cur = e.getNamedParam(tr.layerId, k);
          tr.keys.push_back({0, cur, Ease::InOut});
          tl->tracks.push_back(tr);
          app.edited();
        }
      ImGui::EndMenu();
    };
    for (auto &l : p.layers) menu(&l, (l.name + "##" + std::to_string(l.id)).c_str());
    menu(nullptr, T("Masters"));
    ImGui::EndPopup();
  }
  ImGui::SameLine();
  ImGui::TextDisabled("%s", T("Duplo clique na faixa: nova chave · arraste: tempo e valor (Shift: só tempo) · botão direito: curva"));
  ImGui::Dummy({1, 8});
  ImGui::EndChild();
}

void Ui::effectsUi(App &app, Layer &l) {
  Engine &e = app.engine;
  int remove = -1, move = -1, dir = 0;
  for (size_t i = 0; i < l.effects.size(); ++i) {
    Effect &fx = l.effects[i];
    ImGui::PushID(int(i) + 7700);
    if (ImGui::Checkbox("##en", &fx.enabled)) app.edited();
    ImGui::SameLine();
    std::string title = (fx.name.empty() ? std::string(T("efeito")) : fx.name) + "###fx";
    bool open = ImGui::TreeNodeEx(title.c_str(), ImGuiTreeNodeFlags_DefaultOpen);
    if (ImGui::BeginPopupContextItem()) {
      if (ImGui::MenuItem(T("Subir"), nullptr, false, i > 0)) { move = int(i); dir = -1; }
      if (ImGui::MenuItem(T("Descer"), nullptr, false, i + 1 < l.effects.size())) { move = int(i); dir = 1; }
      if (ImGui::MenuItem(T("Valores padrão"))) { fx.params.clear(); app.edited(); }
      if (ImGui::MenuItem(T("Remover"))) remove = int(i);
      ImGui::EndPopup();
    }
    ImGui::SameLine(ImGui::GetContentRegionAvail().x - 4);
    if (ImGui::SmallButton("x")) remove = int(i);
    if (open) {
      const IsfInfo *info = e.isfCatalog.info(fx.path);
      if (!info) {
        ImGui::TextColored({1, 0.4f, 0.4f, 1}, "%s", e.isfCatalog.error(fx.path).c_str());
      } else {
        if (isfParamsUi(*info, fx.params)) app.edited();
        if (fx.isBlobTracker()) blobUi(app, l);
      }
      ImGui::TreePop();
    }
    ImGui::PopID();
  }
  if (remove >= 0) { l.effects.erase(l.effects.begin() + remove); app.edited(); }
  if (move >= 0) { std::swap(l.effects[size_t(move)], l.effects[size_t(move + dir)]); app.edited(); }

  if (ImGui::Button(T("+ Efeito"))) ImGui::OpenPopup("##addfx");
  if (ImGui::BeginPopup("##addfx")) {
    auto add = [&](const std::string &path, const std::string &name) {
      Effect fx;
      fx.path = path;
      fx.name = name;
      if (const IsfInfo *info = e.isfCatalog.info(path)) isfDefaults(*info, fx.params);
      l.effects.push_back(fx);
      app.edited();
    };
    // Built-ins by category.
    std::map<std::string, std::vector<const IsfCatalog::Entry *>> byCat;
    for (auto &x : e.isfCatalog.builtins())
      if (x.info.filter) byCat[x.info.categories.empty() ? T("Outros") : x.info.categories.front()].push_back(&x);
    for (auto &[cat, list] : byCat) {
      if (!ImGui::BeginMenu(cat.c_str())) continue;
      for (auto *x : list) {
        if (ImGui::MenuItem(x->name.c_str())) add(x->path, x->name);
        if (!x->info.description.empty()) ImGui::SetItemTooltip("%s", x->info.description.c_str());
      }
      ImGui::EndMenu();
    }
    ImGui::Separator();
    if (ImGui::MenuItem(T("Rastreamento de blobs (câmera)"))) add(kBlobTrackerId, T("Blob tracker"));
    if (ImGui::MenuItem(T("Abrir arquivo ISF (.fs)..."))) {
      auto files = platform::openFileDialog("Efeito ISF", "ISF|*.fs|Todos|*.*", false);
      if (!files.empty()) add(files[0], platform::toUtf8(platform::toPath(files[0]).stem()));
    }
    if (ImGui::MenuItem(T("Recarregar a pasta de efeitos"))) e.isfCatalog.rescan();
    ImGui::EndPopup();
  }
  helpMarker(T("Efeitos são shaders ISF (o mesmo formato do Millumin, VDMX, Resolume e MadMapper), "
             "aplicados em ordem sobre a mídia da layer. Baixe mais em editor.isf.video ou coloque "
             "arquivos .fs na pasta effects ao lado do lumen.exe. Via OSC: /layer:Nome/effect1/<parâmetro>."));
}

void Ui::blobUi(App &app, Layer &l) {
  Engine &e = app.engine;
  if (ImGui::Button(T("Gravar fundo"))) e.learnBlobBackground(l.id);
  ImGui::SameLine();
  const BlobTracker *t = e.blobTracker(l.id);
  ImGui::TextDisabled("%s", t && t->hasBackground() ? T("(comparando com o fundo)") : T("(limiar fixo)"));
  helpMarker(T("Gravar fundo: com a cena vazia, clique. Depois, tudo o que mudar (pessoas, objetos) "
             "vira blob, mesmo com iluminação irregular. A câmera precisa estar parada."));
  const std::vector<Blob> *blobs = e.blobs(l.id);
  // Preview: black & white image with the blobs.
  float w = ImGui::GetContentRegionAvail().x, h = w * 9.0f / 16.0f;
  if (t && t->width() > 0) h = w * float(t->height()) / float(t->width());
  ImVec2 o = ImGui::GetCursorScreenPos();
  ImDrawList *dl = ImGui::GetWindowDrawList();
  dl->AddRectFilled(o, {o.x + w, o.y + h}, IM_COL32(20, 20, 22, 255));
  if (t && t->width() > 0) {
    // A coarse rendering of the mask (one rectangle per cell of 4x4 pixels).
    int tw = t->width(), th = t->height(), step = std::max(1, tw / 80);
    const auto &mask = t->mask();
    for (int y = 0; y < th; y += step)
      for (int x = 0; x < tw; x += step)
        if (mask[size_t(y * tw + x)])
          dl->AddRectFilled({o.x + float(x) / tw * w, o.y + float(y) / th * h},
                            {o.x + float(x + step) / tw * w, o.y + float(y + step) / th * h}, IM_COL32(200, 200, 200, 255));
  }
  if (blobs)
    for (auto &b : *blobs) {
      ImVec2 a{o.x + (b.x - b.w / 2) * w, o.y + (b.y - b.h / 2) * h}, c{o.x + (b.x + b.w / 2) * w, o.y + (b.y + b.h / 2) * h};
      dl->AddRect(a, c, IM_COL32(255, 196, 0, 255), 0, 0, 2);
      char id[16];
      std::snprintf(id, sizeof(id), "%d", b.id);
      dl->AddText({a.x + 2, a.y + 1}, IM_COL32(255, 196, 0, 255), id);
    }
  ImGui::Dummy({w, h});
  if (!t) ImGui::TextDisabled("%s", T("Dispare uma câmera nesta layer para ver os blobs."));
  ImGui::TextDisabled(T("%d blob(s) · endereços /blob/%s/1 x y l a, /blob/%s/1/x ..."), blobs ? int(blobs->size()) : 0,
                      l.name.c_str(), l.name.c_str());
  // Shortcuts: follow blob 1 with a layer.
  static int target = -1;
  Project &p = e.project;
  Layer *tl = p.layer(target);
  ImGui::SetNextItemWidth(160);
  if (ImGui::BeginCombo("##follow", tl ? tl->name.c_str() : T("(layer)"))) {
    for (auto &x : p.layers)
      if (ImGui::Selectable(x.name.c_str(), x.id == target)) target = x.id;
    ImGui::EndCombo();
  }
  ImGui::SameLine();
  if (ImGui::Button(T("segue o blob 1")) && tl) {
    // Position offsets in canvas pixels: blob 0..1 -> -W/2..W/2 around the layer.
    float cw = float(p.canvasWidth(tl->canvasId)), ch = float(p.canvasHeight(tl->canvasId));
    InputMapping mx, my;
    mx.address = "/blob/" + l.name + "/1/x"; mx.target = MappingTarget::PositionX; mx.layerId = tl->id;
    mx.outMin = -cw / 2; mx.outMax = cw / 2; mx.smoothing = 0.5f;
    my = mx;
    my.address = "/blob/" + l.name + "/1/y"; my.target = MappingTarget::PositionY;
    my.outMin = -ch / 2; my.outMax = ch / 2;
    p.mappings.push_back(mx);
    p.mappings.push_back(my);
    app.status(T("Mapeamentos criados em Interação: o blob 1 move a layer ") + tl->name);
    app.edited();
  }
  if (ImGui::Button(T("Presença liga a layer")) && tl) {
    InputMapping m;
    m.address = "/blob/" + l.name + "/count";
    m.inMin = 0; m.inMax = 1;
    m.target = MappingTarget::Opacity; m.layerId = tl->id;
    m.transformer = Transformer::Switch;
    m.smoothing = 0.9f;
    p.mappings.push_back(m);
    app.status(T("Mapeamento criado: a layer ") + tl->name + T(" aparece quando alguém é detectado"));
    app.edited();
  }
}

void Ui::openInputMesh(int layerId, int slice, int outputId) {
  inputMesh_ = {true, layerId, slice, outputId};
}

// Input side of a mesh warp: the control points over the content itself.
void Ui::inputMeshWindow(App &app) {
  if (!inputMesh_.open) return;
  Project &p = app.engine.project;
  Warp *w = nullptr;
  const Texture *tex = nullptr;
  bool flip = false;
  float u0 = 0, v0 = 0, u1 = 1, v1 = 1;
  std::string title;
  if (OutputConfig *o = p.output(inputMesh_.outputId)) {
    w = &o->warp;
    tex = &app.engine.canvas().texture();
    flip = true; // the canvas texture is bottom-up
    float cw = float(p.canvasWidth(o->canvasId)), ch = float(p.canvasHeight(o->canvasId));
    u0 = o->srcX / cw; v0 = o->srcY / ch; u1 = (o->srcX + o->srcW) / cw; v1 = (o->srcY + o->srcH) / ch;
    title = T("Saída ") + o->name;
  } else if (Layer *l = p.layer(inputMesh_.layerId)) {
    if (inputMesh_.slice >= 0 && inputMesh_.slice < int(l->slices.size())) {
      Slice &s = l->slices[size_t(inputMesh_.slice)];
      w = &s.warp;
      u0 = s.u0; v0 = s.v0; u1 = s.u1; v1 = s.v1;
      if (const LayerState *st = app.engine.layerState(l->id)) tex = st->currentTex;
      title = l->name + " / " + s.name;
    }
  }
  if (!w) { inputMesh_.open = false; return; }
  ImGui::SetNextWindowSize({720, 520}, ImGuiCond_FirstUseEver);
  if (!ImGui::Begin((T("Malha de entrada — ") + title + "###inputmesh").c_str(), &inputMesh_.open)) {
    ImGui::End();
    return;
  }
  int grid[2] = {w->meshCols, w->meshRows};
  ImGui::SetNextItemWidth(140);
  if (ImGui::InputInt2(T("Divisão"), grid)) { w->resampleMesh(grid[0], grid[1]); app.edited(); }
  ImGui::SameLine();
  if (ImGui::Button(T("Grade regular"))) { w->uvMesh.clear(); app.edited(); }
  ImGui::SameLine();
  ImGui::TextDisabled("%s", T("Arraste os pontos para cima dos detalhes da imagem · duplo clique adiciona linha e coluna"));
  ImVec2 avail = ImGui::GetContentRegionAvail();
  float aspect = tex && !tex->empty() ? float(tex->width()) * (u1 - u0) / std::max(1.0f, float(tex->height()) * (v1 - v0)) : 16.0f / 9.0f;
  float bw = avail.x, bh = bw / aspect;
  if (bh > avail.y) { bh = avail.y; bw = bh * aspect; }
  ImVec2 o = ImGui::GetCursorScreenPos();
  ImGui::InvisibleButton("##inmesh", {std::max(bw, 10.0f), std::max(bh, 10.0f)});
  ImDrawList *dl = ImGui::GetWindowDrawList();
  dl->AddRectFilled(o, {o.x + bw, o.y + bh}, IM_COL32(20, 20, 22, 255));
  if (tex && !tex->empty()) {
    ImVec2 a = flip ? ImVec2(u0, 1 - v0) : ImVec2(u0, v0), b = flip ? ImVec2(u1, 1 - v1) : ImVec2(u1, v1);
    dl->AddImage(texId(*tex), o, {o.x + bw, o.y + bh}, a, b);
  } else {
    dl->AddText({o.x + 10, o.y + 10}, IM_COL32(200, 200, 200, 255), T("Dispare uma mídia nesta layer para ver a imagem."));
  }
  w->ensureUvMesh();
  auto toScreen = [&](Vec2 uv) { return ImVec2{o.x + uv.x * bw, o.y + uv.y * bh}; };
  for (int r = 0; r < w->meshRows; ++r)
    for (int c = 0; c < w->meshCols; ++c) {
      if (c + 1 < w->meshCols) dl->AddLine(toScreen(w->uvAt(c, r)), toScreen(w->uvAt(c + 1, r)), IM_COL32(255, 196, 0, 150));
      if (r + 1 < w->meshRows) dl->AddLine(toScreen(w->uvAt(c, r)), toScreen(w->uvAt(c, r + 1)), IM_COL32(255, 196, 0, 150));
    }
  ImGuiIO &io = ImGui::GetIO();
  Vec2 mouse{std::clamp((io.MousePos.x - o.x) / bw, 0.0f, 1.0f), std::clamp((io.MousePos.y - o.y) / bh, 0.0f, 1.0f)};
  if (ImGui::IsItemClicked()) {
    inputMesh_.point = -1;
    float best = 10;
    for (int i = 0; i < w->meshCols * w->meshRows; ++i) {
      ImVec2 sp = toScreen(w->uvMesh[size_t(i)]);
      float d = std::hypot(sp.x - io.MousePos.x, sp.y - io.MousePos.y);
      if (d < best) { best = d; inputMesh_.point = i; }
    }
  }
  if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
    // The clicked content point becomes a new row / column of the grid.
    Vec2 uv;
    Warp unit; // grid parameter of a content point: invert the input grid
    unit.corners = {Vec2{0, 0}, Vec2{1, 0}, Vec2{1, 1}, Vec2{0, 1}};
    unit.meshCols = w->meshCols;
    unit.meshRows = w->meshRows;
    unit.mesh = w->uvMesh;
    if (unit.unmap(mouse, uv) && w->insertLines(uv.x, uv.y)) app.edited();
    inputMesh_.point = -1;
  }
  if (inputMesh_.point >= 0 && inputMesh_.point < int(w->uvMesh.size()) && ImGui::IsItemActive() &&
      ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0)) {
    w->uvMesh[size_t(inputMesh_.point)] = mouse;
    app.edited();
  }
  for (int i = 0; i < w->meshCols * w->meshRows; ++i) {
    ImVec2 sp = toScreen(w->uvMesh[size_t(i)]);
    bool selPt = i == inputMesh_.point;
    dl->AddCircleFilled(sp, selPt ? 6.0f : 4.5f, selPt ? IM_COL32(255, 255, 255, 255) : IM_COL32(20, 20, 20, 230));
    dl->AddCircle(sp, selPt ? 6.0f : 4.5f, IM_COL32(255, 196, 0, 255), 0, 2);
  }
  if (w->uvIsIdentity()) w->uvMesh.clear();
  ImGui::End();
}

void Ui::layerInspector(App &app) {
  Project &p = app.engine.project;
  Layer *l = p.layer(app.sel.layerId);
  if (!l) {
    ImGui::TextWrapped("%s", T("Selecione uma layer no dashboard."));
    return;
  }
  ImGui::PushID(l->id);
  const float CW = float(p.canvasWidth(l->canvasId)), CH = float(p.canvasHeight(l->canvasId));
  if (inputText(T("Nome"), l->name)) app.edited();
  if (p.canvases.size() > 0) {
    int cid = l->canvasId;
    if (canvasCombo(T("Canvas"), p, cid)) {
      // Keep the mapping proportional to the new canvas.
      float sx = float(p.canvasWidth(cid)) / CW, sy = float(p.canvasHeight(cid)) / CH;
      for (auto &sl : l->slices)
        for (auto &c : sl.warp.corners) c = {c.x * sx, c.y * sy};
      l->canvasId = cid;
      workspace.viewCanvas = cid;
      workspace.viewOutputId = -1;
      app.edited();
    }
  }
  if (ImGui::Checkbox(T("Visível"), &l->visible)) app.edited();
  if (ImGui::SliderFloat(T("Opacidade"), &l->opacity, 0, 1)) app.edited();
  if (ImGui::SliderFloat(T("Volume"), &l->volume, 0, 2, "%.2f")) app.edited();
  ImGui::SameLine();
  if (ImGui::Checkbox(T("Mudo"), &l->mute)) app.edited();
  if (const LayerState *st = app.engine.layerState(l->id); st && st->current) {
    if (st->current->hasAudio()) {
      ImGui::ProgressBar(std::min(1.0f, st->current->audioPeak()), {-1, 6}, "");
    } else if (st->current->speed != 1.0f) {
      ImGui::TextDisabled("%s", T("Áudio desligado: velocidade diferente de 1x"));
    }
    std::string hw = st->current->hwaccel();
    if (!hw.empty()) ImGui::TextDisabled(T("Decodificação por hardware: %s"), hw.c_str());
  }
  int blend = int(l->blend);
  if (ImGui::Combo(T("Blend"), &blend, TZ("Normal\0Adição\0Multiplicar\0Tela (screen)\0"
                                    "Matte alpha (máscara da layer de baixo)\0Matte alpha invertido\0"
                                    "Matte luma (claro mostra)\0Matte luma invertido\0"))) {
    l->blend = BlendMode(blend);
    app.edited();
  }

  if (ImGui::CollapsingHeader(T("Transformação"), ImGuiTreeNodeFlags_DefaultOpen)) {
    float pos[2] = {l->transform.position.x, l->transform.position.y};
    if (ImGui::DragFloat2(T("Posição (px)"), pos, 1.0f)) {
      l->transform.position = {pos[0], pos[1]};
      app.edited();
    }
    if (ImGui::DragFloat(T("Escala"), &l->transform.scale, 0.005f, 0.01f, 20)) app.edited();
    if (ImGui::DragFloat(T("Rotação (°)"), &l->transform.rotation, 0.25f, -360, 360)) app.edited();
    if (ImGui::Button(T("Zerar transformação"))) {
      l->transform = {};
      app.edited();
    }
    helpMarker(T("A transformação move todas as fatias juntas. Útil para animar via OSC "
               "sem perder o mapeamento."));
  }

  if (ImGui::CollapsingHeader(T("Cor / efeitos"), ImGuiTreeNodeFlags_DefaultOpen)) {
    if (ImGui::SliderFloat(T("Brilho"), &l->fx.brightness, -1, 1)) app.edited();
    if (ImGui::SliderFloat(T("Contraste"), &l->fx.contrast, 0, 2)) app.edited();
    if (ImGui::SliderFloat(T("Saturação"), &l->fx.saturation, 0, 2)) app.edited();
    if (ImGui::SliderFloat(T("Matiz (°)"), &l->fx.hue, -180, 180)) app.edited();
    if (ImGui::Checkbox(T("Inverter"), &l->fx.invert)) app.edited();
    if (colorEdit(T("Tinta"), l->fx.tint)) app.edited();
    if (ImGui::Button(T("Zerar efeitos"))) {
      l->fx = {};
      app.edited();
    }
  }
  if (isMatte(l->blend))
    ImGui::TextColored({1.0f, 0.77f, 0.0f, 1}, "%s", T("Matte: esta layer não aparece; ela recorta a layer logo abaixo."));

  if (ImGui::CollapsingHeader(T("Efeitos (shaders ISF)"), ImGuiTreeNodeFlags_DefaultOpen)) effectsUi(app, *l);

  if (ImGui::CollapsingHeader(T("Fatias (slices) / mapping"), ImGuiTreeNodeFlags_DefaultOpen)) {
    ImGui::TextWrapped("%s", T("Cada fatia recorta uma parte da mídia e a projeta numa superfície. "
                       "Use as ferramentas Perspectiva (M) e Malha (G) no workspace."));
    int &si = app.sel.slice;
    si = std::clamp(si, 0, int(l->slices.size()) - 1);
    for (size_t i = 0; i < l->slices.size(); ++i) {
      ImGui::PushID(int(i));
      if (ImGui::Checkbox("##en", &l->slices[i].enabled)) app.edited();
      ImGui::SameLine();
      if (ImGui::Selectable(l->slices[i].name.c_str(), si == int(i))) si = int(i);
      ImGui::PopID();
    }
    if (ImGui::Button(T("+ Fatia"))) {
      Slice s;
      s.name = T("Fatia ") + std::to_string(l->slices.size() + 1);
      float w = CW * 0.3f, h = CH * 0.3f;
      s.warp.setRect({CW * 0.35f, CH * 0.35f}, {w, h});
      l->slices.push_back(s);
      si = int(l->slices.size()) - 1;
      app.edited();
    }
    ImGui::SameLine();
    if (ImGui::Button(T("Duplicar")) && si < int(l->slices.size())) {
      Slice s = l->slices[size_t(si)];
      s.name += T(" cópia");
      s.warp.translate({30, 30});
      l->slices.push_back(s);
      si = int(l->slices.size()) - 1;
      app.edited();
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(l->slices.size() <= 1);
    if (ImGui::Button(T("Remover"))) {
      l->slices.erase(l->slices.begin() + si);
      si = std::max(0, si - 1);
      app.edited();
    }
    ImGui::EndDisabled();

    if (si < int(l->slices.size())) {
      Slice &s = l->slices[size_t(si)];
      ImGui::SeparatorText(s.name.c_str());
      if (inputText(T("Nome da fatia"), s.name)) app.edited();
      // Surface: a mapping shared with other layers.
      {
        Surface *bound = p.surface(s.surfaceId);
        if (ImGui::BeginCombo(T("Surface"), bound ? bound->name.c_str() : T("(mapping próprio)"))) {
          if (ImGui::Selectable(T("(mapping próprio)"), !bound)) { s.surfaceId = -1; app.edited(); }
          for (auto &sf : p.surfaces) {
            ImGui::PushID(sf.id);
            ImGui::ColorButton("##c", ImVec4(sf.color.r, sf.color.g, sf.color.b, 1),
                               ImGuiColorEditFlags_NoTooltip, {12, 12});
            ImGui::SameLine();
            if (ImGui::Selectable(sf.name.c_str(), bound == &sf)) {
              s.surfaceId = sf.id;
              p.resolveSurfaces();
              app.edited();
            }
            ImGui::PopID();
          }
          ImGui::Separator();
          if (ImGui::Selectable(T("+ Nova surface com este mapping"))) {
            s.surfaceId = p.addSurface(s.warp).id;
            app.edited();
          }
          ImGui::EndCombo();
        }
        helpMarker(T("Surface = mapping compartilhado. Várias layers ligadas à mesma surface "
                   "seguem o mesmo mapping: quando o cenário ou o projetor muda de lugar, "
                   "ajuste uma vez e todas acompanham."));
        if (bound) {
          ImGui::SameLine();
          if (ImGui::SmallButton(T("Soltar"))) { s.surfaceId = -1; app.edited(); }
          int users = 0;
          for (auto &L : p.layers)
            for (auto &S : L.slices) users += S.surfaceId == bound->id;
          ImGui::PushID("surf");
          ImGui::SetNextItemWidth(140);
          if (inputText("##sname", bound->name)) app.edited();
          ImGui::SameLine();
          if (colorEdit("##scol", bound->color)) app.edited();
          ImGui::SameLine();
          ImGui::TextDisabled(T("%d fatia(s)"), users);
          ImGui::SameLine();
          if (ImGui::SmallButton(T("Apagar surface"))) {
            int id = bound->id;
            p.surfaces.erase(std::remove_if(p.surfaces.begin(), p.surfaces.end(),
                                            [&](auto &x) { return x.id == id; }), p.surfaces.end());
            p.resolveSurfaces(); // bound slices keep the last mapping
            app.edited();
          }
          ImGui::PopID();
        }
      }
      float crop[4] = {s.u0, s.v0, s.u1, s.v1};
      if (ImGui::DragFloat4(T("Recorte UV"), crop, 0.002f, 0, 1)) {
        s.u0 = crop[0]; s.v0 = crop[1]; s.u1 = crop[2]; s.v1 = crop[3];
        app.edited();
      }
      helpMarker(T("Parte da mídia usada por esta fatia: u0 v0 u1 v1 (0 a 1)."));
      for (int c = 0; c < 4; ++c) {
        const char *names[] = {T("Canto sup. esq."), T("Canto sup. dir."), T("Canto inf. dir."), T("Canto inf. esq.")};
        float v[2] = {s.warp.corners[size_t(c)].x, s.warp.corners[size_t(c)].y};
        if (ImGui::DragFloat2(names[c], v, 0.5f)) {
          s.warp.corners[size_t(c)] = {v[0], v[1]};
          app.edited();
        }
      }
      int grid[2] = {s.warp.meshCols, s.warp.meshRows};
      if (ImGui::InputInt2(T("Malha (col × lin)"), grid)) {
        s.warp.resampleMesh(grid[0], grid[1]); // keeps the current warp
        app.edited();
      }
      helpMarker(T("Mudar a divisão mantém a deformação atual. No workspace (ferramenta Malha), "
                 "duplo clique adiciona uma linha e uma coluna de pontos passando pelo clique."));
      if (ImGui::Button(T("Malha de entrada..."))) openInputMesh(l->id, si, -1);
      ImGui::SameLine();
      ImGui::TextDisabled("%s", s.warp.uvIsIdentity() ? T("(regular)") : T("(ajustada)"));
      helpMarker(T("Lado da mídia da malha: coloque cada ponto sobre um detalhe da imagem (janelas "
                 "de uma fachada, quinas). Depois, na ferramenta Malha, leve os pontos até o "
                 "mesmo detalhe no prédio real."));
      if (ImGui::Button(T("Retângulo da mídia"))) {
        // Fit the slice to the media aspect ratio, centred on the canvas.
        float aspect = 16.0f / 9.0f;
        if (const LayerState *st = app.engine.layerState(l->id); st && st->currentTex)
          aspect = float(st->currentTex->width()) / float(std::max(1, st->currentTex->height()));
        aspect *= (s.u1 - s.u0) / std::max(1e-3f, s.v1 - s.v0);
        float h = CH * 0.8f, w = h * aspect;
        if (w > CW * 0.95f) { w = CW * 0.95f; h = w / aspect; }
        s.warp.setRect({(CW - w) * 0.5f, (CH - h) * 0.5f}, {w, h});
        app.edited();
      }
      ImGui::SameLine();
      if (ImGui::Button(T("Tela cheia"))) {
        s.warp.setRect({0, 0}, {CW, CH});
        app.edited();
      }
      ImGui::SameLine();
      if (ImGui::Button(T("Zerar malha"))) {
        s.warp.resetMesh(s.warp.meshCols, s.warp.meshRows);
        app.edited();
      }
    }
  }

  if (ImGui::CollapsingHeader(T("Máscaras"), ImGuiTreeNodeFlags_DefaultOpen)) {
    ImGui::TextWrapped("%s", T("Ferramenta Máscara (K): clique no workspace para adicionar pontos."));
    maskList(app, l->masks, app.sel.mask, app.sel.maskPoint, {1, 1}, {CW, CH}, false);
  }
  ImGui::PopID();
}

void Ui::cellInspector(App &app) {
  Project &p = app.engine.project;
  int c = app.sel.cellColumn;
  if (c < 0 || c >= int(p.columns.size())) {
    ImGui::TextWrapped("%s", T("Clique numa célula do dashboard."));
    return;
  }
  Column &col = p.columns[size_t(c)];
  auto it = col.cells.find(app.sel.cellLayer);
  Layer *l = p.layer(app.sel.cellLayer);
  if (it == col.cells.end() || it->second.mediaId < 0 || !l) {
    ImGui::TextWrapped("%s", T("Célula vazia. Arraste uma mídia da biblioteca para ela."));
    return;
  }
  Cell &cell = it->second;
  MediaItem *m = p.media(cell.mediaId);
  ImGui::Text(T("Coluna %d · %s"), c + 1, col.name.c_str());
  ImGui::Text(T("Layer: %s"), l->name.c_str());
  ImGui::Text(T("Mídia: %s"), m ? m->name.c_str() : T("(removida)"));
  ImGui::Separator();
  {
    int em = int(cell.endMode);
    if (ImGui::Combo(T("No fim"), &em, TZ("Loop\0Congelar no último quadro\0Parar\0Disparar a próxima coluna\0"))) {
      cell.setEndMode(EndMode(em));
      app.edited();
    }
    helpMarker(T("Parar: a mídia some com a transição da coluna. Próxima coluna: o show segue "
               "sozinho quando o vídeo acaba (auto-follow), bom para instalações."));
  }
  if (ImGui::DragFloat(T("Velocidade"), &cell.speed, 0.01f, 0, 8, T("%.2fx"))) app.edited();
  if (ImGui::Button(T("Disparar"))) app.engine.launchCell(c, l->id);
  ImGui::SameLine();
  if (ImGui::Button(T("Parar layer"))) app.engine.stopLayer(l->id);
  ImGui::SameLine();
  if (ImGui::Button(T("Limpar célula"))) {
    col.cells.erase(it);
    app.edited();
  }
}

void Ui::mediaInspector(App &app) {
  Project &p = app.engine.project;
  MediaItem *m = p.media(app.sel.mediaId);
  if (!m) {
    ImGui::TextWrapped("%s", T("Selecione um item da biblioteca."));
    return;
  }
  ImGui::PushID(m->id);
  const Texture *thumb = app.engine.cache.thumbnail(*m, app.engine.generators, app.engine.mesh);
  float w = ImGui::GetContentRegionAvail().x;
  fittedImage(thumb, {w, w * 9.0f / 16.0f});
  if (inputText(T("Nome"), m->name)) app.edited();
  ImGui::Text(T("Tipo: %s"), mediaTypeLabel(m->type));

  if (m->type == MediaType::Generator) {
    int kind = int(m->generator);
    std::string items;
    for (int k = 0; k < int(GeneratorKind::COUNT); ++k) {
      items += generatorName(GeneratorKind(k));
      items.push_back('\0');
    }
    if (ImGui::Combo(T("Gerador"), &kind, items.c_str())) {
      m->generator = GeneratorKind(kind);
      app.edited();
    }
    int size[2] = {m->width, m->height};
    if (ImGui::InputInt2(T("Resolução"), size)) {
      m->width = std::clamp(size[0], 16, 8192);
      m->height = std::clamp(size[1], 16, 8192);
      app.edited();
    }
    if (colorEdit(T("Cor 1"), m->color1)) app.edited();
    if (colorEdit(T("Cor 2"), m->color2)) app.edited();
    const char *p1 = T("Parâmetro 1"), *p2 = T("Parâmetro 2");
    float lo1 = 0, hi1 = 10, lo2 = 0, hi2 = 10;
    switch (m->generator) {
    case GeneratorKind::Gradient: p1 = T("Ângulo (°)"); lo1 = -180; hi1 = 180; break;
    case GeneratorKind::Grid: p1 = T("Tamanho da célula"); lo1 = 4; hi1 = 512; p2 = T("Espessura"); lo2 = 0.5f; hi2 = 32; break;
    case GeneratorKind::Plasma: p1 = T("Velocidade"); p2 = T("Escala"); lo2 = 0.1f; hi2 = 20; break;
    case GeneratorKind::Stripes: p1 = T("Velocidade"); lo1 = -5; hi1 = 5; p2 = T("Quantidade"); lo2 = 1; hi2 = 100; break;
    default: break;
    }
    if (ImGui::SliderFloat(p1, &m->param1, lo1, hi1)) app.edited();
    if (ImGui::SliderFloat(p2, &m->param2, lo2, hi2)) app.edited();
  } else if (m->type == MediaType::Live) {
    bool changed = inputText(T("URL / dispositivo"), m->path);
    changed |= inputText(T("Formato"), m->liveFormat);
    helpMarker(T("dshow (Windows), v4l2 (Linux), lavfi (teste) ou vazio para streams de rede."));
    changed |= inputText(T("Opções"), m->liveOptions);
    helpMarker(T("Opções do FFmpeg separadas por ':', ex.: video_size=1920x1080:framerate=30 "
               "ou rtsp_transport=tcp"));
    if (changed) {
      app.engine.cache.invalidate(m->id);
      app.edited();
    }
    ImGui::TextDisabled("%s", T("A entrada abre quando alguma layer a dispara e fecha quando ninguém a usa."));
  } else if (m->type == MediaType::Text) {
    char buf[4096];
    std::snprintf(buf, sizeof(buf), "%s", m->text.c_str());
    ImGui::BeginDisabled(!m->path.empty());
    if (ImGui::InputTextMultiline(T("Texto"), buf, sizeof(buf), {-1, 90})) { m->text = buf; app.edited(); }
    ImGui::EndDisabled();
    ImGui::TextDisabled("%s", T("OSC: /layer:<layer>/text \"novo texto\""));
    if (ImGui::Button(m->path.empty() ? T("Ler de um arquivo .txt...") : T("Trocar arquivo..."))) {
      auto f = platform::openFileDialog("Arquivo de texto", "Texto|*.txt|Todos|*.*", false);
      if (!f.empty()) { m->path = f[0]; app.edited(); }
    }
    if (!m->path.empty()) {
      ImGui::SameLine();
      if (ImGui::SmallButton(T("Usar o texto acima"))) { m->path.clear(); app.edited(); }
      ImGui::TextWrapped(T("Arquivo: %s (lido de novo quando muda)"), m->path.c_str());
    }
    if (ImGui::DragFloat(T("Tamanho da letra"), &m->fontSize, 0.5f, 4, 1000, T("%.0f px"))) app.edited();
    {
      std::string fn = m->font.empty() ? T("Fonte do sistema") : platform::toUtf8(platform::toPath(m->font).filename());
      if (ImGui::Button((T("Fonte: ") + fn).c_str())) {
        auto f = platform::openFileDialog("Fonte", "Fontes|*.ttf;*.otf|Todos|*.*", false);
        if (!f.empty()) { m->font = f[0]; app.edited(); }
      }
      if (!m->font.empty()) { ImGui::SameLine(); if (ImGui::SmallButton(T("padrão"))) { m->font.clear(); app.edited(); } }
    }
    if (ImGui::Combo(T("Alinhamento"), &m->align, TZ("Esquerda\0Centro\0Direita\0"))) app.edited();
    if (ImGui::Combo(T("Vertical"), &m->valign, TZ("Topo\0Meio\0Base\0"))) app.edited();
    if (ImGui::DragFloat(T("Entrelinha"), &m->lineSpacing, 0.01f, 0.5f, 3)) app.edited();
    if (colorEdit(T("Cor"), m->color1, true)) app.edited();
    if (colorEdit(T("Fundo"), m->color2, true)) app.edited();
    int sz[2] = {m->width, m->height};
    if (ImGui::InputInt2(T("Tamanho (px)"), sz)) { m->width = std::clamp(sz[0], 16, 8192); m->height = std::clamp(sz[1], 16, 8192); app.edited(); }
  } else if (m->type == MediaType::NDI) {
    if (inputText(T("Fonte NDI"), m->path)) { app.engine.cache.invalidate(m->id); app.edited(); }
    if (!ndiAvailable()) ImGui::TextColored({1, 0.6f, 0.3f, 1}, "%s", ndiStatus().c_str());
    else
      for (auto &n : ndiSources())
        if (ImGui::Selectable(n.c_str(), n == m->path)) { m->path = n; m->name = T("NDI: ") + n; app.engine.cache.invalidate(m->id); app.edited(); }
  } else if (m->type == MediaType::Shader) {
    ImGui::TextWrapped(T("Arquivo: %s"), m->path.c_str());
    int sz[2] = {m->width, m->height};
    if (ImGui::InputInt2(T("Tamanho (px)"), sz)) {
      m->width = std::clamp(sz[0], 16, 8192);
      m->height = std::clamp(sz[1], 16, 8192);
      app.edited();
    }
    if (const IsfInfo *info = app.engine.isfCatalog.info(m->path)) {
      if (!info->description.empty()) ImGui::TextDisabled("%s", info->description.c_str());
      if (isfParamsUi(*info, m->shaderParams)) app.edited();
      if (ImGui::SmallButton(T("Valores padrão"))) { m->shaderParams.clear(); app.edited(); }
    } else {
      ImGui::TextColored({1, 0.4f, 0.4f, 1}, "%s", app.engine.isfCatalog.error(m->path).c_str());
    }
    ImGui::TextDisabled("%s", T("Edite o arquivo .fs em qualquer editor: o Lumen recarrega sozinho."));
  } else if (m->type == MediaType::Canvas) {
    int cid = m->canvasRef;
    if (canvasCombo(T("Canvas mostrado"), p, cid)) {
      m->canvasRef = cid;
      m->name = T("Canvas: ") + p.canvasLabel(cid);
      app.edited();
    }
    helpMarker(T("Coloque esta mídia numa layer de outro canvas: posição, escala, rotação e mapping "
               "da layer posicionam o canvas inteiro. Um canvas que mostra a si mesmo cria um eco."));
  } else if (m->type == MediaType::Spout) {
    if (inputText(T("Sender"), m->path)) {
      app.engine.cache.invalidate(m->id);
      app.edited();
    }
    helpMarker(T("Nome do sender Spout. Vazio = o sender ativo."));
    if (!spoutAvailable()) ImGui::TextColored({1, 0.6f, 0.3f, 1}, "%s", T("Spout só funciona no Windows."));
  } else {
    ImGui::TextWrapped(T("Arquivo: %s"), m->path.c_str());
    if (const MediaInfo *info = app.engine.cache.info(*m)) {
      ImGui::Text(T("Resolução: %d × %d"), info->width, info->height);
      if (m->type == MediaType::Video) {
        ImGui::Text(T("Duração: %.2f s   ·   %.2f fps"), info->duration, info->fps);
        ImGui::Text(T("Codec: %s%s"), info->codec.c_str(), info->hasAudio ? T("   (com áudio)") : "");
        if (info->codec == "hap" && !FrameUploader::supportsS3tc())
          ImGui::TextDisabled("%s", T("Esta GPU não aceita HAP comprimido: decodificando na CPU."));
      }
    } else {
      ImGui::TextColored({1, 0.4f, 0.4f, 1}, "%s", T("Não foi possível abrir este arquivo."));
    }
    if (ImGui::Button(T("Trocar arquivo..."))) {
      auto files = platform::openFileDialog("Trocar arquivo", "Mídia|*.mp4;*.mov;*.mkv;*.avi;*.webm;*.png;*.jpg;*.jpeg|Todos|*.*", false);
      if (!files.empty()) {
        m->path = files[0];
        m->type = isImagePath(m->path) ? MediaType::Image : MediaType::Video;
        app.engine.cache.invalidate(m->id);
        app.edited();
      }
    }
  }
  ImGui::PopID();
}

void Ui::outputInspector(App &app) {
  Project &p = app.engine.project;
  if (ImGui::CollapsingHeader(T("Assistente de blend (vários projetores)"))) {
    static BlendLayoutParams bp;
    static int firstMonitor = 1; // monitor 2: the control screen is usually monitor 1
    static bool scale = true;
    ImGui::TextWrapped("%s", T("Monta as saídas, as regiões do canvas e as bordas suaves de uma grade de "
                       "projetores sobrepostos."));
    ImGui::SetNextItemWidth(120);
    ImGui::InputInt(T("Colunas"), &bp.cols);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(120);
    ImGui::InputInt(T("Linhas"), &bp.rows);
    bp.cols = std::clamp(bp.cols, 1, 16);
    bp.rows = std::clamp(bp.rows, 1, 16);
    int res[2] = {bp.projW, bp.projH};
    if (ImGui::InputInt2(T("Resolução do projetor"), res)) {
      bp.projW = std::clamp(res[0], 16, 16384);
      bp.projH = std::clamp(res[1], 16, 16384);
    }
    float ov[2] = {bp.overlapX * 100, bp.overlapY * 100};
    if (ImGui::DragFloat2(T("Sobreposição % (horiz/vert)"), ov, 0.1f, 0, 50, "%.1f%%")) {
      bp.overlapX = ov[0] / 100;
      bp.overlapY = ov[1] / 100;
    }
    helpMarker(T("Quanto da largura de cada projetor se sobrepõe ao vizinho. 10–20% é o comum."));
    auto mons = OutputManager::monitors();
    std::string mlabel = firstMonitor < 0 ? T("Janelas (teste)") : T("A partir do monitor ") + std::to_string(firstMonitor + 1);
    if (ImGui::BeginCombo(T("Monitores"), mlabel.c_str())) {
      if (ImGui::Selectable(T("Janelas (teste)"), firstMonitor < 0)) firstMonitor = -1;
      for (size_t i = 0; i < mons.size(); ++i) {
        std::string l = T("A partir do monitor ") + std::to_string(i + 1) + " (" + mons[i].name + ")";
        if (ImGui::Selectable(l.c_str(), firstMonitor == int(i))) firstMonitor = int(i);
      }
      ImGui::EndCombo();
    }
    ImGui::Checkbox(T("Escalar o mapping atual para o novo canvas"), &scale);
    BlendLayout preview = computeBlendLayout(bp);
    ImGui::Text(T("Canvas: %d × %d  ·  sobreposição: %d × %d px  ·  %d saídas"), preview.canvasW,
                preview.canvasH, preview.overlapPxX, preview.overlapPxY, int(preview.projectors.size()));
    // Small diagram of the layout.
    {
      ImDrawList *dl = ImGui::GetWindowDrawList();
      ImVec2 o0 = ImGui::GetCursorScreenPos();
      float W = ImGui::GetContentRegionAvail().x, k = W / float(std::max(1, preview.canvasW));
      float H = float(preview.canvasH) * k;
      dl->AddRect(o0, {o0.x + W, o0.y + H}, IM_COL32(120, 120, 120, 255));
      for (size_t i = 0; i < preview.projectors.size(); ++i) {
        auto &b = preview.projectors[i];
        ImU32 col = IM_COL32(90 + int(i * 53) % 160, 160, 255 - int(i * 37) % 120, 70);
        ImVec2 a{o0.x + b.srcX * k, o0.y + b.srcY * k}, z{o0.x + (b.srcX + b.srcW) * k, o0.y + (b.srcY + b.srcH) * k};
        dl->AddRectFilled(a, z, col);
        dl->AddRect(a, z, IM_COL32(255, 255, 255, 160));
        char t[24];
        std::snprintf(t, sizeof(t), T("%zu"), i + 1);
        dl->AddText({a.x + 4, a.y + 2}, IM_COL32(255, 255, 255, 230), t);
      }
      ImGui::Dummy({W, H + 4});
    }
    if (ImGui::Button(T("Aplicar"))) {
      applyBlendLayout(p, bp, firstMonitor, scale);
      if (!p.outputs.empty()) app.sel.outputId = p.outputs.front().id;
      app.edited();
      app.status(T("Blend aplicado: ") + std::to_string(p.outputs.size()) + T(" saídas"));
    }
    ImGui::SameLine();
    ImGui::TextDisabled("%s", T("Depois: padrão \"Grade de blend\" para alinhar"));
  }
  if (ImGui::Button(T("Identificar saídas"))) app.outputs.identify();
  ImGui::SetItemTooltip("%s", T("Mostra o número de cada saída por alguns segundos"));
  ImGui::SameLine();
  if (ImGui::Button(T("Grade em todas"))) {
    bool any = std::any_of(p.outputs.begin(), p.outputs.end(), [](auto &x) { return x.pattern == TestPattern::BlendGrid; });
    for (auto &x : p.outputs) x.pattern = any ? TestPattern::None : TestPattern::BlendGrid;
    app.edited();
  }
  ImGui::SetItemTooltip("%s", T("Liga/desliga a grade de alinhamento em todos os projetores"));
  ImGui::Separator();
  for (auto &o : p.outputs) {
    ImGui::PushID(o.id);
    if (ImGui::Selectable(o.name.c_str(), app.sel.outputId == o.id)) app.sel.outputId = o.id;
    ImGui::PopID();
  }
  if (ImGui::Button(T("+ Saída"))) {
    OutputConfig &o = p.addOutput();
    app.sel.outputId = o.id;
    app.edited();
  }
  OutputConfig *o = p.output(app.sel.outputId);
  if (!o && !p.outputs.empty()) {
    o = &p.outputs.front();
    app.sel.outputId = o->id;
  }
  if (!o) return;
  ImGui::SameLine();
  if (ImGui::Button(T("Remover saída"))) {
    int id = o->id;
    p.outputs.erase(std::remove_if(p.outputs.begin(), p.outputs.end(), [&](auto &x) { return x.id == id; }),
                    p.outputs.end());
    if (workspace.viewOutputId == id) workspace.viewOutputId = -1;
    app.sel.outputId = -1;
    app.edited();
    return;
  }

  ImGui::PushID(o->id);
  ImGui::SeparatorText(o->name.c_str());
  if (inputText(T("Nome"), o->name)) app.edited();
  if (ImGui::Checkbox(T("Ativa"), &o->enabled)) app.edited();
  int pat = int(o->pattern);
  ImGui::SetNextItemWidth(200);
  if (ImGui::Combo(T("Padrão de teste"), &pat, TZ("Nenhum (conteúdo)\0Test card da saída\0Grade de blend (contínua)\0Branco\0Preto\0"))) {
    o->pattern = TestPattern(pat);
    app.edited();
  }
  helpMarker(T("Test card: foco e identificação. Grade de blend: desenhada no canvas, continua de um "
             "projetor para o outro (alinhe até as linhas coincidirem). Branco: ajuste as bordas "
             "até a emenda sumir. Preto: ajuste o nível de preto."));

  auto mons = OutputManager::monitors();
  std::string current = o->monitor < 0 ? T("Janela (teste)") : T("Monitor ") + std::to_string(o->monitor + 1);
  if (ImGui::BeginCombo(T("Destino"), current.c_str())) {
    if (ImGui::Selectable(T("Janela (teste)"), o->monitor < 0)) { o->monitor = -1; app.edited(); }
    for (size_t i = 0; i < mons.size(); ++i) {
      char label[200];
      std::snprintf(label, sizeof(label), T("Monitor %zu: %s (%dx%d)"), i + 1, mons[i].name.c_str(), mons[i].w, mons[i].h);
      if (ImGui::Selectable(label, o->monitor == int(i))) { o->monitor = int(i); app.edited(); }
    }
    ImGui::EndCombo();
  }
  helpMarker(T("Em um monitor a saída ocupa a tela inteira, sem bordas. Desative o espelhamento "
             "de telas no Windows (Win+P → Estender)."));
  if (p.canvases.size() > 0) {
    int cid = o->canvasId;
    if (canvasCombo(T("Canvas"), p, cid)) {
      o->canvasId = cid;
      o->srcX = o->srcY = 0;
      o->srcW = float(p.canvasWidth(cid));
      o->srcH = float(p.canvasHeight(cid));
      app.edited();
    }
    helpMarker(T("Qual canvas esta saída mostra. Ex.: um canvas 3840×2160 para o processador de LED "
               "e o canvas principal para os projetores."));
  }
  ImGui::SeparatorText(T("NDI desta saída"));
  {
    bool on = !o->ndiName.empty();
    if (ImGui::Checkbox(T("Enviar por NDI"), &on)) { o->ndiName = on ? T("Lumen ") + o->name : ""; app.edited(); }
    if (on) {
      ImGui::SetNextItemWidth(200);
      if (inputText(T("Nome NDI"), o->ndiName)) app.edited();
      if (ImGui::Checkbox(T("Sem janela (só Spout / NDI)##ndi"), &o->windowless)) app.edited();
      if (!ndiAvailable()) ImGui::TextColored({1, 0.6f, 0.3f, 1}, "%s", ndiStatus().c_str());
      std::string st = app.outputs.spoutStatus(o->id);
      if (!st.empty()) ImGui::TextDisabled("%s", st.c_str());
    }
    helpMarker(T("Envia a imagem final desta saída pela rede (NDI): para outro computador, um "
               "processador de LED, uma mesa de corte ou OBS. Precisa do NDI Runtime (NDI Tools)."));
  }
  ImGui::SeparatorText(T("Spout desta saída"));
  {
    bool on = !o->spoutName.empty();
    if (ImGui::Checkbox(T("Enviar por Spout"), &on)) {
      o->spoutName = on ? T("Lumen ") + o->name : "";
      if (!on) o->windowless = false;
      app.edited();
    }
    helpMarker(T("Envia a imagem final desta saída (com warp, blend e cor) para outro programa ou "
               "para um processador de LED / NDI (via Spout to NDI). Funciona quando as saídas estão abertas."));
    if (on) {
      ImGui::SetNextItemWidth(200);
      if (inputText(T("Nome do Spout"), o->spoutName)) app.edited();
      if (ImGui::Checkbox(T("Sem janela (só Spout)"), &o->windowless)) app.edited();
      if (o->windowless) {
        int sz[2] = {o->winW, o->winH};
        if (ImGui::InputInt2(T("Tamanho (px)"), sz)) {
          o->winW = std::max(16, sz[0]);
          o->winH = std::max(16, sz[1]);
          app.edited();
        }
      }
      std::string st = app.outputs.spoutStatus(o->id);
      if (!st.empty()) ImGui::TextDisabled("%s", st.c_str());
    }
  }
  if (o->monitor < 0) {
    int sz[2] = {o->winW, o->winH};
    if (ImGui::InputInt2(T("Tamanho da janela"), sz)) {
      o->winW = std::max(64, sz[0]);
      o->winH = std::max(64, sz[1]);
      app.edited();
    }
  }

  ImGui::SeparatorText(T("Região do canvas"));
  float src[4] = {o->srcX, o->srcY, o->srcW, o->srcH};
  if (ImGui::DragFloat4(T("x y larg alt"), src, 1.0f)) {
    o->srcX = src[0]; o->srcY = src[1];
    o->srcW = std::max(1.0f, src[2]); o->srcH = std::max(1.0f, src[3]);
    app.edited();
  }
  const float OW = float(p.canvasWidth(o->canvasId)), OH = float(p.canvasHeight(o->canvasId));
  if (ImGui::Button(T("Canvas inteiro"))) {
    o->srcX = o->srcY = 0;
    o->srcW = OW;
    o->srcH = OH;
    app.edited();
  }
  ImGui::SameLine();
  if (ImGui::Button(T("Metade esq."))) {
    o->srcX = 0; o->srcY = 0; o->srcW = OW * 0.5f; o->srcH = OH;
    app.edited();
  }
  ImGui::SameLine();
  if (ImGui::Button(T("Metade dir."))) {
    o->srcX = OW * 0.5f; o->srcY = 0; o->srcW = OW * 0.5f; o->srcH = OH;
    app.edited();
  }
  if (ImGui::Button(T("Blend automático pelas sobreposições"))) {
    int n = autoBlendOverlaps(p, o->canvasId);
    app.status(std::to_string(n) + T(" emenda(s) com soft edge calculado pelas regiões das saídas"));
    app.edited();
  }
  helpMarker(T("Para projetores fora de uma grade (tamanhos ou posições diferentes): ajuste a região "
             "de cada saída no canvas e o Lumen calcula as bordas suaves de cada sobreposição."));

  ImGui::SeparatorText(T("Warp da saída"));
  bool viewing = workspace.viewOutputId == o->id;
  if (ImGui::Checkbox(T("Editar no workspace"), &viewing)) workspace.viewOutputId = viewing ? o->id : -1;
  helpMarker(T("Mostra esta saída no workspace. Use Perspectiva (M) ou Malha (G) para corrigir "
             "a geometria do projetor inteiro (keystone / superfícies curvas)."));
  int grid[2] = {o->warp.meshCols, o->warp.meshRows};
  if (ImGui::InputInt2(T("Malha (col × lin)"), grid)) {
    o->warp.resampleMesh(grid[0], grid[1]); // keeps the current warp
    app.edited();
  }
  ImGui::SameLine();
  if (ImGui::Button(T("Malha de entrada..."))) openInputMesh(-1, 0, o->id);
  if (ImGui::Button(T("Zerar warp"))) {
    o->warp.setRect({0, 0}, {1, 1});
    o->warp.resetMesh(2, 2);
    app.edited();
  }

  softEdgeControls(app, o);
  blackAndColorControls(app, o);
  outputMaskControls(app, o, workspace);
  ImGui::PopID();
}

void Ui::softEdgeControls(App &app, OutputConfig *o) {
  ImGui::SeparatorText(T("Soft edge (blend entre projetores)"));
  bool se = false;
  se |= ImGui::SliderFloat(T("Esquerda"), &o->softEdge.left, 0, 0.5f);
  se |= ImGui::SliderFloat(T("Direita"), &o->softEdge.right, 0, 0.5f);
  se |= ImGui::SliderFloat(T("Topo"), &o->softEdge.top, 0, 0.5f);
  se |= ImGui::SliderFloat(T("Base"), &o->softEdge.bottom, 0, 0.5f);
  se |= ImGui::SliderFloat(T("Gamma"), &o->softEdge.gamma, 0.5f, 4);
  se |= ImGui::SliderFloat(T("Curva"), &o->softEdge.power, 0.5f, 6);
  if (se) app.edited();
  helpMarker(T("Para dois projetores lado a lado com sobreposição: cada um mostra uma região do "
             "canvas que inclui a faixa sobreposta, e a borda é suavizada com a mesma largura. "
             "O Assistente de blend calcula tudo isso."));

}

void Ui::blackAndColorControls(App &app, OutputConfig *o) {
  ImGui::SeparatorText(T("Nível de preto"));
  {
    float bl[3] = {o->blackLevel.r, o->blackLevel.g, o->blackLevel.b};
    static std::map<int, bool> perChannel; // UI state only
    bool linked = !perChannel[o->id] && bl[0] == bl[1] && bl[1] == bl[2];
    if (linked) {
      if (ImGui::SliderFloat(T("Compensação"), &bl[0], 0, 0.1f, "%.4f", ImGuiSliderFlags_Logarithmic)) {
        o->blackLevel = {bl[0], bl[0], bl[0], 1};
        app.edited();
      }
    } else if (ImGui::SliderFloat3(T("Compensação RGB"), bl, 0, 0.1f, "%.4f", ImGuiSliderFlags_Logarithmic)) {
      o->blackLevel = {bl[0], bl[1], bl[2], 1};
      app.edited();
    }
    if (ImGui::SmallButton(linked ? T("Ajustar por cor") : T("Unificar"))) {
      perChannel[o->id] = linked;
      if (!linked) {
        o->blackLevel = {bl[0], bl[0], bl[0], 1};
        app.edited();
      }
    }
    helpMarker(T("Projetores não mostram preto puro: na sobreposição os dois \"pretos\" somam e aparece "
               "uma faixa cinza nas cenas escuras. Use o padrão Preto e aumente até o resto da imagem "
               "ficar igual à faixa. Valores típicos: 0.002 a 0.02."));
  }

  ImGui::SeparatorText(T("Cor do projetor"));
  {
    bool c = false;
    c |= ImGui::SliderFloat(T("Brilho##out"), &o->color.brightness, 0, 2);
    c |= ImGui::SliderFloat(T("Contraste##out"), &o->color.contrast, 0, 2);
    c |= ImGui::SliderFloat(T("Gamma##out"), &o->color.gamma, 0.3f, 3);
    float g[3] = {o->color.gain.r, o->color.gain.g, o->color.gain.b};
    if (ImGui::SliderFloat3(T("Ganho R G B"), g, 0, 2)) {
      o->color.gain = {g[0], g[1], g[2], 1};
      c = true;
    }
    if (ImGui::SmallButton(T("Zerar cor"))) {
      o->color = {};
      c = true;
    }
    if (c) app.edited();
    helpMarker(T("Iguale projetores diferentes (ou com lâmpadas de idades diferentes): mostre Branco em "
               "todos e reduza o ganho do canal que estiver sobrando."));
  }

  ImGui::SeparatorText(T("Sincronia"));
  if (ImGui::SliderInt(T("Atraso (quadros)"), &o->delayFrames, 0, 8)) app.edited();
  helpMarker(T("Projetores de modelos diferentes têm latências diferentes. Atrase os mais rápidos "
             "até a imagem andar junta na emenda (filme em câmera lenta com o celular para conferir)."));
  if (ImGui::Checkbox(T("Mostrar controles no projetor (Ctrl+Shift+D)"), &app.showOutputControls)) {}
  helpMarker(T("Desenha os cantos e a malha da saída selecionada na própria projeção, para ajustar "
             "olhando a parede."));

}

void Ui::outputMaskControls(App &app, OutputConfig *o, Workspace &ws) {
  ImGui::SeparatorText(T("Máscaras da saída"));
  ImGui::TextWrapped("%s", T("Recortam a imagem deste projetor (obstáculos, bordas irregulares, cúpulas). "
                     "Edite no workspace com a saída em vista e a ferramenta Máscara (K)."));
  {
    float w = float(o->winW), h = float(o->winH);
    auto mons = OutputManager::monitors();
    if (o->monitor >= 0 && o->monitor < int(mons.size())) {
      w = float(mons[size_t(o->monitor)].w);
      h = float(mons[size_t(o->monitor)].h);
    }
    size_t before = o->masks.size();
    maskList(app, o->masks, app.sel.outMask, app.sel.outMaskPoint, {w, h}, {1, 1}, true);
    if (o->masks.size() > before) { // edit the new mask on this output right away
      ws.viewOutputId = o->id;
      ws.tool = Tool::Mask;
    }
  }
}

void Ui::interactionInspector(App &app) {
  Engine &e = app.engine;
  Project &p = e.project;
  ImGui::SeparatorText(T("Entrada OSC"));
  bool changed = ImGui::Checkbox(T("Ativar OSC"), &p.oscEnabled);
  ImGui::SetNextItemWidth(120);
  changed |= ImGui::InputInt(T("Porta UDP"), &p.oscPort);
  if (changed) {
    p.oscPort = std::clamp(p.oscPort, 1, 65535);
    e.restartOsc();
    app.edited();
  }
  if (e.oscServer().running())
    ImGui::TextColored({0.4f, 0.9f, 0.5f, 1}, T("Escutando na porta %d"), e.oscServer().port());
  else if (!e.oscError().empty())
    ImGui::TextColored({1, 0.4f, 0.4f, 1}, T("Erro: %s"), e.oscError().c_str());
  else
    ImGui::TextDisabled("%s", T("Desativado"));

  ImGui::SeparatorText(T("Saída OSC (feedback e ações)"));
  ImGui::TextWrapped("%s", T("Destinos que recebem /lumen/status/column ao trocar de cue e as ações OSC das "
                     "colunas (botão direito no cabeçalho da coluna)."));
  int removeTarget = -1;
  for (size_t i = 0; i < p.oscTargets.size(); ++i) {
    OscTarget &t = p.oscTargets[i];
    ImGui::PushID(5000 + int(i));
    if (ImGui::Checkbox("##en", &t.enabled)) app.edited();
    ImGui::SameLine();
    ImGui::SetNextItemWidth(140);
    if (inputText("##host", t.host)) app.edited();
    ImGui::SameLine();
    ImGui::SetNextItemWidth(90);
    if (ImGui::InputInt("##port", &t.port, 0)) { t.port = std::clamp(t.port, 1, 65535); app.edited(); }
    ImGui::SameLine();
    if (ImGui::Checkbox(T("Status"), &t.feedback)) app.edited();
    ImGui::SameLine();
    if (ImGui::SmallButton("x")) removeTarget = int(i);
    ImGui::PopID();
  }
  if (removeTarget >= 0) {
    p.oscTargets.erase(p.oscTargets.begin() + removeTarget);
    app.edited();
  }
  if (ImGui::Button(T("+ Destino OSC"))) {
    p.oscTargets.push_back({});
    app.edited();
  }
  if (e.oscSent() > 0) {
    ImGui::SameLine();
    ImGui::TextDisabled(T("%d mensagens enviadas"), e.oscSent());
  }

  ImGui::SeparatorText(T("MIDI"));
  if (!e.midi().available()) {
    ImGui::TextDisabled("%s", T("Compilado sem suporte a MIDI"));
  } else {
    if (ImGui::Checkbox(T("Ativar MIDI"), &p.midiEnabled)) {
      e.restartMidi();
      app.edited();
    }
    auto ports = e.midi().ports();
    if (ports.empty()) ImGui::TextDisabled("%s", T("Nenhum controlador conectado (procura a cada 3 s)"));
    for (auto &port : ports) ImGui::BulletText("%s", port.c_str());
    ImGui::TextDisabled("%s", T("MIDI chega como /midi/cc/<canal>/<n>, /midi/note/<canal>/<nota>: use \"Aprender\"."));
  }

  ImGui::SeparatorText(T("Timecode"));
  {
    TimecodeConfig &tc = p.timecode;
    int src = int(tc.source);
    if (ImGui::Combo(T("Receber"), &src, TZ("Desligado\0MTC (MIDI Time Code)\0LTC (áudio)\0"))) {
      tc.source = TimecodeSource(src);
      app.edited();
    }
    if (tc.source == TimecodeSource::LTC) {
      static std::vector<std::string> caps;
      static double capsAt = -10;
      if (ImGui::GetTime() - capsAt > 5) { caps = LtcCapture::devices(); capsAt = ImGui::GetTime(); }
      if (ImGui::BeginCombo(T("Entrada de áudio"), tc.ltcDevice.empty() ? T("Padrão do sistema") : tc.ltcDevice.c_str())) {
        if (ImGui::Selectable(T("Padrão do sistema"), tc.ltcDevice.empty())) { tc.ltcDevice.clear(); app.edited(); }
        for (auto &d : caps)
          if (ImGui::Selectable(d.c_str(), d == tc.ltcDevice)) { tc.ltcDevice = d; app.edited(); }
        ImGui::EndCombo();
      }
      ImGui::SetNextItemWidth(100);
      if (ImGui::InputInt(T("Canal (0 = esquerdo)"), &tc.ltcChannel)) { tc.ltcChannel = std::clamp(tc.ltcChannel, 0, 7); app.edited(); }
      ImGui::ProgressBar(std::min(1.0f, e.ltcInput().level()), {-1, 5}, "");
    }
    if (tc.source != TimecodeSource::Off) {
      ImGui::SetNextItemWidth(100);
      if (ImGui::DragFloat(T("Roda livre (s)"), &tc.freewheel, 0.05f, 0, 10)) app.edited();
      helpMarker(T("Se o sinal cair, o relógio continua sozinho por este tempo (evita parar o show "
                 "num cabo com mau contato)."));
      if (e.timecodeValid())
        ImGui::TextColored(kAccent, T("%s  (%.2f fps)"), Timecode::fromSeconds(e.timecodeSeconds(), e.timecodeFps()).str().c_str(),
                           e.timecodeFps());
      else
        ImGui::TextDisabled("%s", T("Sem sinal"));
      if (!e.timecodeError().empty()) ImGui::TextColored({1, 0.4f, 0.4f, 1}, "%s", e.timecodeError().c_str());
      ImGui::TextDisabled("%s", T("Use: timeline → \"timecode\", ou coluna → Disparar no timecode."));
    }
    if (ImGui::Checkbox(T("Gerar timecode a partir de uma timeline"), &tc.generate)) app.edited();
    if (tc.generate) {
      const Timeline *gt = p.timeline(tc.genTimelineId);
      if (ImGui::BeginCombo(T("Timeline##gen"), gt ? gt->name.c_str() : T("(escolha)"))) {
        for (auto &t : p.timelines)
          if (ImGui::Selectable(t.name.c_str(), t.id == tc.genTimelineId)) { tc.genTimelineId = t.id; app.edited(); }
        ImGui::EndCombo();
      }
      static char start[32] = "";
      std::string cur = Timecode::fromSeconds(tc.genStart, tc.genFps).str();
      if (!ImGui::IsAnyItemActive()) std::snprintf(start, sizeof(start), "%s", cur.c_str());
      ImGui::SetNextItemWidth(120);
      if (ImGui::InputText(T("Início"), start, sizeof(start), ImGuiInputTextFlags_EnterReturnsTrue)) {
        double v;
        if (parseTimecode(start, tc.genFps, v)) { tc.genStart = v; app.edited(); }
      }
      int fpsI = tc.genFps < 24.5 ? 0 : tc.genFps < 27 ? 1 : tc.genFps < 29.99 ? 2 : 3;
      ImGui::SetNextItemWidth(120);
      if (ImGui::Combo(T("fps##gen"), &fpsI, TZ("24\00025\00029.97 drop\00030\0"))) {
        static const double f[] = {24, 25, 29.97, 30};
        tc.genFps = f[fpsI];
        app.edited();
      }
      if (ImGui::Checkbox(T("MTC na porta MIDI"), &tc.genMtc)) app.edited();
      if (tc.genMtc) {
        ImGui::SameLine();
        ImGui::SetNextItemWidth(120);
        if (inputText("##mtcport", tc.genMtcPort)) app.edited();
        ImGui::SetItemTooltip("%s", T("Parte do nome da porta MIDI (vazio = a primeira)"));
      }
      if (ImGui::Checkbox(T("LTC no áudio"), &tc.genLtc)) app.edited();
      if (tc.genLtc) {
        ImGui::SameLine();
        ImGui::SetNextItemWidth(100);
        if (ImGui::Combo("##ltcch", &tc.genLtcChannel, TZ("esquerdo\0direito\0os dois\0"))) app.edited();
      }
      if (e.generatingTimecode())
        ImGui::TextColored(kAccent, T("Gerando %s"), Timecode::fromSeconds(e.generatedTimecode(), tc.genFps).str().c_str());
      else
        ImGui::TextDisabled("%s", T("Gera enquanto a timeline estiver tocando."));
    }
  }

  ImGui::SeparatorText(T("Serial (Arduino)"));
  {
    auto &si = p.serialIn;
    bool ch = ImGui::Checkbox(T("Receber da porta serial"), &si.enabled);
    static std::vector<std::string> ports;
    static double portsAt = -10;
    if (ImGui::GetTime() - portsAt > 3) { ports = serialPorts(); portsAt = ImGui::GetTime(); }
    ImGui::SetNextItemWidth(140);
    if (ImGui::BeginCombo(T("Porta"), si.port.empty() ? T("(escolha)") : si.port.c_str())) {
      for (auto &pt : ports)
        if (ImGui::Selectable(pt.c_str(), pt == si.port)) { si.port = pt; ch = true; }
      if (ports.empty()) ImGui::TextDisabled("%s", T("Nenhuma porta encontrada"));
      ImGui::EndCombo();
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(90);
    ch |= ImGui::InputInt(T("baud"), &si.baud, 0);
    if (ch) app.edited();
    if (e.serialInput().running()) ImGui::TextColored(kAccent, T("Lendo %s"), e.serialInput().port().c_str());
    else if (!e.serialError().empty()) ImGui::TextColored({1, 0.4f, 0.4f, 1}, "%s", e.serialError().c_str());
    helpMarker(T("Cada linha que o Arduino envia vira uma entrada: \"A0 512\" chega como /serial/A0 512, "
               "\"botao 1\" como /serial/botao 1, \"12 34\" como /serial 12 34. Use num mapeamento "
               "(Aprender funciona). Exemplo de código em docs/DISPOSITIVOS.md."));
  }

  ImGui::SeparatorText(T("Projetores (PJLink)"));
  {
    int removePj = -1;
    for (size_t i = 0; i < p.projectors.size(); ++i) {
      PjlinkProjector &pj = p.projectors[i];
      ImGui::PushID(int(i) + 8800);
      ImGui::SetNextItemWidth(100);
      if (inputText("##n", pj.name)) app.edited();
      ImGui::SameLine();
      ImGui::SetNextItemWidth(110);
      if (inputText("##h", pj.host)) app.edited();
      ImGui::SameLine();
      ImGui::SetNextItemWidth(70);
      if (ImGui::InputTextWithHint("##pw", T("senha"), pj.password.data(), pj.password.capacity() + 1,
                                   ImGuiInputTextFlags_Password | ImGuiInputTextFlags_CallbackResize,
                                   [](ImGuiInputTextCallbackData *d) {
                                     if (d->EventFlag == ImGuiInputTextFlags_CallbackResize) {
                                       auto *str = static_cast<std::string *>(d->UserData);
                                       str->resize(size_t(d->BufTextLen));
                                       d->Buf = str->data();
                                     }
                                     return 0;
                                   }, &pj.password))
        app.edited();
      ImGui::SameLine();
      if (ImGui::SmallButton("x")) removePj = int(i);
      if (ImGui::SmallButton(T("Ligar"))) e.runCommand("pjlink " + pj.host + " power on");
      ImGui::SameLine();
      if (ImGui::SmallButton(T("Desligar"))) e.runCommand("pjlink " + pj.host + " power off");
      ImGui::SameLine();
      if (ImGui::SmallButton(T("Fechar obturador"))) e.runCommand("pjlink " + pj.host + " shutter close");
      ImGui::SameLine();
      if (ImGui::SmallButton(T("Abrir"))) e.runCommand("pjlink " + pj.host + " shutter open");
      ImGui::SameLine();
      if (ImGui::SmallButton("?")) e.runCommand("pjlink " + pj.host + " status");
      std::string st = e.devices.projectorStatus(pj.host);
      if (!st.empty()) { ImGui::SameLine(); ImGui::TextDisabled("%s", st.c_str()); }
      ImGui::PopID();
    }
    if (removePj >= 0) { p.projectors.erase(p.projectors.begin() + removePj); app.edited(); }
    if (ImGui::Button(T("+ Projetor"))) {
      PjlinkProjector pj;
      pj.name = T("Projetor ") + std::to_string(p.projectors.size() + 1);
      pj.host = "192.168.0.100";
      p.projectors.push_back(pj);
      app.edited();
    }
    helpMarker(T("Projetores com PJLink na rede (Epson, Panasonic, NEC, Sony, Optoma, Christie...). "
               "Nas colunas, na agenda ou nos marcadores da timeline use: pjlink <nome> power on, "
               "pjlink <nome> shutter close... Ex.: a agenda liga os projetores às 9:00."));
  }

  ImGui::SeparatorText(T("Testar um comando"));
  {
    static char cmd[256] = "midi note 1 60 127";
    ImGui::SetNextItemWidth(260);
    bool go = ImGui::InputText("##cmd", cmd, sizeof(cmd), ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::SameLine();
    if (ImGui::Button(T("Enviar")) || go) e.runCommand(cmd);
    static std::vector<std::string> outs;
    static double outsAt = -10;
    if (ImGui::GetTime() - outsAt > 3) { outs = e.devices.midiOutPorts(); outsAt = ImGui::GetTime(); }
    ImGui::TextDisabled(T("Saídas MIDI: %s"), outs.empty() ? T("nenhuma") : outs.front().c_str());
    if (outs.size() > 1) ImGui::SetItemTooltip(T("%zu portas; use midi@nome para escolher"), outs.size());
  }

  ImGui::SeparatorText(T("Teclado"));
  if (ImGui::Checkbox(T("Teclas como entrada (/key/<tecla>)"), &p.keyboardInput)) app.edited();
  helpMarker(T("Cada tecla vira /key/a, /key/1, /key/f1, /key/left... com valor 1 ao apertar e 0 ao "
             "soltar, também com as janelas de saída em foco. Use num mapeamento: ex. a tecla A "
             "dispara a coluna 3. Espaço, Esc e B continuam com as funções do programa."));

  ImGui::SeparatorText(T("DMX de entrada (mesa de luz)"));
  {
    DmxInputConfig &d = p.dmxInput;
    bool ch = ImGui::Checkbox(T("Receber DMX"), &d.enabled);
    int proto = int(d.protocol);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(110);
    ch |= ImGui::Combo("##dmxproto", &proto, "Art-Net\0sACN\0");
    d.protocol = DmxProtocol(proto);
    ImGui::SetNextItemWidth(90);
    ch |= ImGui::InputInt(T("Universo inicial"), &d.universe);
    ImGui::SetNextItemWidth(90);
    ch |= ImGui::InputInt(T("Quantidade"), &d.count);
    d.universe = std::clamp(d.universe, 0, 32767);
    d.count = std::clamp(d.count, 1, 64);
    if (ch) { e.restartDmxInput(); app.edited(); }
    if (e.dmxInput().running())
      ImGui::TextColored({0.4f, 0.9f, 0.5f, 1}, T("Recebendo (%d pacotes). Canais chegam como %s/<universo>/<canal> (0-255)."),
                         e.dmxInput().packets(), d.protocol == DmxProtocol::ArtNet ? "/artnet" : "/sacn");
    else if (!e.dmxInputError().empty())
      ImGui::TextColored({1, 0.4f, 0.4f, 1}, T("Erro: %s"), e.dmxInputError().c_str());
    helpMarker(T("Uma mesa de luz (grandMA, Chamsys, Hog...) controla o Lumen: ligue um canal à "
               "opacidade de uma layer, ou dispare colunas com um canal (valor acima de 127)."));
  }

  ImGui::SeparatorText(T("Mapeamentos (sensor → parâmetro)"));
  ImGui::TextWrapped("%s", T("Ligue qualquer mensagem OSC (TouchOSC, TouchDesigner, Arduino, sensores...) "
                     "a um parâmetro. Clique em \"Aprender\" e envie a mensagem."));
  int remove = -1;
  for (size_t i = 0; i < p.mappings.size(); ++i) {
    InputMapping &m = p.mappings[i];
    ImGui::PushID(int(i));
    char header[256];
    std::snprintf(header, sizeof(header), "%s → %s###map", m.address.c_str(), mappingTargetName(m.target));
    if (ImGui::CollapsingHeader(header, ImGuiTreeNodeFlags_DefaultOpen)) {
      if (ImGui::Checkbox(T("Ativo"), &m.enabled)) app.edited();
      ImGui::SameLine();
      bool learning = e.learnMapping == int(i);
      if (learning) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.9f, 0.3f, 0.3f, 1));
      if (ImGui::Button(learning ? T("Aguardando mensagem...") : T("Aprender")))
        e.learnMapping = learning ? -1 : int(i);
      if (learning) ImGui::PopStyleColor();
      ImGui::SameLine();
      if (ImGui::Button(T("Remover"))) remove = int(i);
      if (inputText(T("Endereço"), m.address)) app.edited();
      helpMarker(T("Aceita curingas OSC: /sensor/* , /pad/[1-4]"));
      if (ImGui::InputInt(T("Argumento #"), &m.argIndex)) { m.argIndex = std::max(0, m.argIndex); app.edited(); }
      if (ImGui::Button(T("Calibrar"))) { calibrating_ = int(i); calMin_ = 1e30f; calMax_ = -1e30f; }
      helpMarker(T("Mexa o sensor ou fader até os extremos: a faixa de entrada é ajustada sozinha. "
                 "Clique de novo para terminar."));
      if (calibrating_ == int(i)) {
        for (auto &msg : e.frameInputs())
          if (oscMatch(m.address.c_str(), msg.address.c_str()) && size_t(m.argIndex) < msg.args.size()) {
            float v = msg.number(size_t(m.argIndex));
            calMin_ = std::min(calMin_, v);
            calMax_ = std::max(calMax_, v);
          }
        ImGui::SameLine();
        if (calMax_ >= calMin_) ImGui::Text("%.2f .. %.2f", calMin_, calMax_);
        else ImGui::TextDisabled("%s", T("aguardando valores..."));
        ImGui::SameLine();
        if (ImGui::Button(T("Concluir"))) {
          if (calMax_ > calMin_) { m.inMin = calMin_; m.inMax = calMax_; app.edited(); }
          calibrating_ = -1;
        }
      }
      float in[2] = {m.inMin, m.inMax};
      if (ImGui::DragFloat2(T("Entrada min/max"), in, 0.01f)) { m.inMin = in[0]; m.inMax = in[1]; app.edited(); }
      int t = int(m.target);
      std::string items;
      for (int k = 0; k < int(MappingTarget::COUNT); ++k) {
        items += mappingTargetName(MappingTarget(k));
        items.push_back('\0');
      }
      if (ImGui::Combo(T("Alvo"), &t, items.c_str())) { m.target = MappingTarget(t); app.edited(); }
      if (!mappingTargetIsTrigger(m.target)) {
        int tr = int(m.transformer);
        if (ImGui::Combo(T("Transformação"), &tr, TZ("Linear (segue a entrada)\0Chave (alto = máx, baixo = mín)\0"
                                                  "Alternar (cada toque troca mín/máx)\0Acumular (cada toque soma o passo)\0"))) {
          m.transformer = Transformer(tr);
          app.edited();
        }
        if (m.transformer == Transformer::Accumulator && ImGui::DragFloat(T("Passo"), &m.step, 0.01f)) app.edited();
      }
      int cols[2] = {m.firstColumn, m.lastColumn};
      if (ImGui::InputInt2(T("Só nas colunas (de/até)"), cols)) {
        m.firstColumn = std::max(0, cols[0]);
        m.lastColumn = std::max(0, cols[1]);
        app.edited();
      }
      helpMarker(T("0 0 = sempre ativo. Ex.: 3 7 = o sensor só age enquanto uma coluna de 3 a 7 "
                 "estiver no ar."));
      if (mappingTargetNeedsLayer(m.target)) {
        Layer *l = p.layer(m.layerId);
        if (ImGui::BeginCombo(T("Layer"), l ? l->name.c_str() : T("(escolha)"))) {
          for (auto &ly : p.layers)
            if (ImGui::Selectable(ly.name.c_str(), ly.id == m.layerId)) { m.layerId = ly.id; app.edited(); }
          ImGui::EndCombo();
        }
        float out[2] = {m.outMin, m.outMax};
        if (ImGui::DragFloat2(T("Saída min/max"), out, 0.01f)) { m.outMin = out[0]; m.outMax = out[1]; app.edited(); }
        if (ImGui::SliderFloat(T("Suavização"), &m.smoothing, 0, 0.99f)) app.edited();
      } else if (!mappingTargetIsTrigger(m.target)) {
        float out[2] = {m.outMin, m.outMax};
        if (ImGui::DragFloat2(T("Saída min/max"), out, 0.01f)) { m.outMin = out[0]; m.outMax = out[1]; app.edited(); }
        if (ImGui::SliderFloat(T("Suavização"), &m.smoothing, 0, 0.99f)) app.edited();
      } else if (m.target == MappingTarget::LaunchColumn) {
        int colN = int(m.outMin);
        if (ImGui::InputInt(T("Coluna (1..n)"), &colN)) { m.outMin = float(std::max(1, colN)); app.edited(); }
        ImGui::TextDisabled("%s", T("Dispara quando o valor passa da metade da faixa."));
      } else {
        ImGui::TextDisabled("%s", T("Dispara quando o valor passa da metade da faixa."));
      }
    }
    ImGui::PopID();
  }
  if (remove >= 0) {
    p.mappings.erase(p.mappings.begin() + remove);
    if (e.learnMapping == remove) e.learnMapping = -1;
    app.edited();
  }
  if (ImGui::Button(T("+ Mapeamento"))) {
    InputMapping m;
    m.layerId = app.sel.layerId >= 0 ? app.sel.layerId : (p.layers.empty() ? -1 : p.layers.front().id);
    p.mappings.push_back(m);
    app.edited();
  }

  ImGui::SeparatorText(T("Agenda (instalações)"));
  {
    ImGui::TextWrapped("%s", T("Ações em horários fixos, ex.: liga a coluna 1 às 9:00 e faz blackout às 22:00."));
    int removeEntry = -1;
    for (size_t i = 0; i < p.schedule.size(); ++i) {
      ScheduleEntry &se = p.schedule[i];
      ImGui::PushID(7000 + int(i));
      if (ImGui::Checkbox("##en", &se.enabled)) app.edited();
      ImGui::SameLine();
      int hm[2] = {se.hour, se.minute};
      ImGui::SetNextItemWidth(80);
      if (ImGui::InputInt2("##hm", hm)) {
        se.hour = std::clamp(hm[0], 0, 23);
        se.minute = std::clamp(hm[1], 0, 59);
        app.edited();
      }
      ImGui::SameLine();
      int act = int(se.action);
      ImGui::SetNextItemWidth(130);
      if (ImGui::Combo("##act", &act, TZ("Disparar coluna\0GO (próxima)\0Parar tudo\0Blackout liga\0Blackout desliga\0Comando\0"))) {
        se.action = ScheduleAction(act);
        app.edited();
      }
      if (se.action == ScheduleAction::LaunchColumn) {
        ImGui::SameLine();
        ImGui::SetNextItemWidth(70);
        if (ImGui::InputInt("##col", &se.column)) { se.column = std::max(1, se.column); app.edited(); }
      } else if (se.action == ScheduleAction::Command) {
        ImGui::SetNextItemWidth(260);
        if (inputText("##scmd", se.command)) app.edited();
        ImGui::SetItemTooltip("%s", T("Ex.: pjlink Sala power on"));
      }
      ImGui::SameLine();
      if (ImGui::SmallButton("x")) removeEntry = int(i);
      const char *dn[] = {"D", "S", "T", "Q", "Q", "S", "S"};
      if (language() == Language::English) {
        static const char *en[] = {"S", "M", "T", "W", "T", "F", "S"};
        std::copy(en, en + 7, dn);
      }
      for (int d = 0; d < 7; ++d) {
        ImGui::PushID(d);
        bool on = se.days & (1 << d);
        if (d) ImGui::SameLine();
        if (ImGui::Checkbox(dn[d], &on)) {
          se.days = on ? (se.days | (1 << d)) : (se.days & ~(1 << d));
          app.edited();
        }
        ImGui::PopID();
      }
      ImGui::PopID();
    }
    if (removeEntry >= 0) {
      p.schedule.erase(p.schedule.begin() + removeEntry);
      app.edited();
    }
    if (ImGui::Button(T("+ Horário"))) {
      p.schedule.push_back({});
      app.edited();
    }
  }

  ImGui::SeparatorText(T("Monitor OSC"));
  if (ImGui::SmallButton(T("Limpar"))) e.oscLog.clear();
  ImGui::BeginChild("##osclog", {0, 180}, ImGuiChildFlags_Borders);
  for (auto &line : e.oscLog) ImGui::TextUnformatted(line.c_str());
  if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 4) ImGui::SetScrollHereY(1.0f);
  ImGui::EndChild();
  ImGui::TextDisabled("%s", T("Linhas com \"?\" não correspondem a comandos /lumen/..."));
}

void Ui::ledInspector(App &app) {
  Engine &e = app.engine;
  Project &p = e.project;

  ImGui::SeparatorText(T("Controladores (Art-Net / sACN)"));
  for (auto &d : p.dmxDevices) {
    ImGui::PushID(d.id);
    if (ImGui::Checkbox("##en", &d.enabled)) app.edited();
    ImGui::SameLine();
    char label[200];
    std::snprintf(label, sizeof(label), "%s  (%s %s)", d.name.c_str(),
                  d.protocol == DmxProtocol::ArtNet ? T("Art-Net") : T("sACN"),
                  d.host.empty() ? (d.protocol == DmxProtocol::ArtNet ? T("broadcast") : T("multicast")) : d.host.c_str());
    if (ImGui::Selectable(label, app.sel.dmxDeviceId == d.id)) app.sel.dmxDeviceId = d.id;
    ImGui::PopID();
  }
  if (ImGui::Button(T("+ Art-Net"))) {
    app.sel.dmxDeviceId = p.addDmxDevice(DmxProtocol::ArtNet).id;
    app.edited();
  }
  ImGui::SameLine();
  if (ImGui::Button(T("+ sACN"))) {
    app.sel.dmxDeviceId = p.addDmxDevice(DmxProtocol::SACN).id;
    app.edited();
  }
  if (DmxDevice *d = p.dmxDevice(app.sel.dmxDeviceId)) {
    ImGui::SameLine();
    if (ImGui::Button(T("Remover controlador"))) {
      int id = d->id;
      p.dmxDevices.erase(std::remove_if(p.dmxDevices.begin(), p.dmxDevices.end(), [&](auto &x) { return x.id == id; }),
                         p.dmxDevices.end());
      app.sel.dmxDeviceId = -1;
      app.edited();
    } else {
      ImGui::PushID(d->id);
      if (inputText(T("Nome##dev"), d->name)) app.edited();
      int proto = int(d->protocol);
      if (ImGui::Combo(T("Protocolo"), &proto, TZ("Art-Net\0sACN (E1.31)\0"))) { d->protocol = DmxProtocol(proto); app.edited(); }
      if (inputText(T("IP do controlador"), d->host)) app.edited();
      helpMarker(T("Vazio = broadcast (Art-Net) ou multicast (sACN). Prefira o IP do controlador "
                 "(unicast): menos tráfego na rede. Ex.: 2.0.0.10 ou 192.168.1.50"));
      if (ImGui::SliderInt(T("Taxa (pacotes/s)"), &d->rate, 1, 60)) app.edited();
      ImGui::PopID();
    }
  }

  ImGui::SeparatorText(T("Fitas / matrizes de LED"));
  int removeId = -1;
  for (auto &m : p.ledMappings) {
    ImGui::PushID(m.id);
    if (ImGui::Checkbox("##en", &m.enabled)) app.edited();
    ImGui::SetItemTooltip("%s", T("Desligada não envia nada"));
    ImGui::SameLine();
    if (ImGui::Checkbox("##lock", &m.locked)) app.edited();
    ImGui::SetItemTooltip("%s", T("Travada: continua enviando, mas não dá para mover no workspace"));
    ImGui::SameLine();
    char label[200];
    std::snprintf(label, sizeof(label), T("%s  (%d×%d %s)"), m.name.c_str(), m.cols, m.rows, ledFormatName(m.format));
    if (ImGui::Selectable(label, app.sel.ledId == m.id)) {
      app.sel.ledId = m.id;
      workspace.tool = Tool::Led;
    }
    if (ImGui::BeginPopupContextItem()) {
      if (ImGui::MenuItem(T("Remover"))) removeId = m.id;
      ImGui::EndPopup();
    }
    ImGui::PopID();
  }
  if (removeId >= 0) {
    p.ledMappings.erase(std::remove_if(p.ledMappings.begin(), p.ledMappings.end(), [&](auto &x) { return x.id == removeId; }),
                        p.ledMappings.end());
    app.edited();
  }
  if (ImGui::Button(T("+ Fita de LED"))) {
    if (p.dmxDevices.empty()) p.addDmxDevice(DmxProtocol::ArtNet);
    LedMapping &nm = p.addLedMapping();
    if (workspace.viewOutputId < 0 && workspace.viewCanvas != 0) {
      float sx = float(p.canvasWidth(workspace.viewCanvas)) / float(p.canvasW);
      float sy = float(p.canvasHeight(workspace.viewCanvas)) / float(p.canvasH);
      for (auto &c : nm.area.corners) c = {c.x * sx, c.y * sy};
      nm.canvasId = workspace.viewCanvas;
    }
    app.sel.ledId = nm.id;
    workspace.tool = Tool::Led;
    app.edited();
  }
  ImGui::SameLine();
  ImGui::TextDisabled("%s", T("Ferramenta LED (L) para posicionar"));

  if (LedMapping *m = p.ledMapping(app.sel.ledId)) {
    ImGui::PushID(m->id);
    ImGui::SeparatorText(m->name.c_str());
    if (inputText(T("Nome"), m->name)) app.edited();
    if (!p.canvases.empty()) {
      int cid = m->canvasId;
      if (canvasCombo(T("Canvas"), p, cid)) {
        m->canvasId = cid;
        workspace.viewCanvas = cid;
        workspace.viewOutputId = -1;
        app.edited();
      }
    }
    DmxDevice *dev = p.dmxDevice(m->deviceId);
    if (ImGui::BeginCombo(T("Controlador"), dev ? dev->name.c_str() : T("(nenhum)"))) {
      for (auto &d : p.dmxDevices)
        if (ImGui::Selectable(d.name.c_str(), d.id == m->deviceId)) { m->deviceId = d.id; app.edited(); }
      ImGui::EndCombo();
    }
    int size[2] = {m->cols, m->rows};
    if (ImGui::InputInt2(T("LEDs (col × lin)"), size)) {
      m->cols = std::clamp(size[0], 1, 4096);
      m->rows = std::clamp(size[1], 1, 4096);
      app.edited();
    }
    helpMarker(T("Quantidade de LEDs. Fita de 60 LEDs: 60 × 1. Matriz 16×16: 16 × 16."));
    int fmt = int(m->format);
    if (ImGui::Combo(T("Formato"), &fmt, TZ("RGB\0BGR\0GRB (WS2812)\0RGBW\0Mono\0"))) { m->format = LedFormat(fmt); app.edited(); }
    helpMarker(T("Se o vermelho sair azul, use BGR. Fitas WS2812/SK6812 costumam ser GRB."));
    ImGui::SameLine();
    if (ImGui::Checkbox(T("16 bits"), &m->wide)) app.edited();
    if (ImGui::Checkbox(T("Zigzag"), &m->zigzag)) app.edited();
    ImGui::SetItemTooltip("%s", T("As linhas vão e voltam (matriz feita com uma fita dobrada)"));
    ImGui::SameLine();
    if (ImGui::Checkbox(T("Vertical"), &m->vertical)) app.edited();
    ImGui::SetItemTooltip("%s", T("A fiação segue as colunas em vez das linhas"));
    ImGui::SameLine();
    if (ImGui::Checkbox(T("Inverter X"), &m->flipX)) app.edited();
    ImGui::SameLine();
    if (ImGui::Checkbox(T("Inverter Y"), &m->flipY)) app.edited();

    int uni = m->universe, ch = m->channel;
    if (ImGui::InputInt(T("Universo"), &uni)) { m->universe = std::clamp(uni, 0, 32767); app.edited(); }
    helpMarker(T("Art-Net: o primeiro universo é 0. sACN: o primeiro é 1. Erro clássico quando a fita não acende."));
    if (ImGui::InputInt(T("Canal inicial"), &ch)) { m->channel = std::clamp(ch, 1, 512); app.edited(); }
    if (ImGui::SliderFloat(T("Curva (gamma)"), &m->gamma, 0.5f, 4.0f, "%.2f")) app.edited();
    helpMarker(T("LEDs não são lineares: 2.2 deixa os tons médios mais naturais. 1 = sem correção."));
    if (ImGui::SliderFloat(T("Brilho"), &m->brightness, 0, 1)) app.edited();

    // Where the strip lands in DMX.
    auto slots = computeLedLayout(m->layout());
    int cpp = ledChannelsPerPixel(m->format, m->wide);
    if (!slots.empty())
      ImGui::TextWrapped(T("%zu LEDs · %d canais por LED · universo %d canal %d → universo %d canal %d"),
                         slots.size(), cpp, slots.front().universe, slots.front().channel + 1,
                         slots.back().universe, slots.back().channel + cpp);
    if (dev && dev->protocol == DmxProtocol::SACN && m->universe == 0)
      ImGui::TextColored({1, 0.6f, 0.3f, 1}, "%s", T("sACN não tem universo 0: use 1."));

    if (ImGui::Button(T("Ocupar o canvas"))) {
      m->area.setRect({0, 0}, {float(p.canvasWidth(m->canvasId)), float(p.canvasHeight(m->canvasId))});
      app.edited();
    }
    ImGui::SameLine();
    if (ImGui::Button(T("Faixa horizontal"))) {
      m->area.setRect({float(p.canvasWidth(m->canvasId)) * 0.05f, float(p.canvasHeight(m->canvasId)) * 0.47f}, {float(p.canvasWidth(m->canvasId)) * 0.9f, float(p.canvasHeight(m->canvasId)) * 0.06f});
      app.edited();
    }
    ImGui::PopID();
  }

  ImGui::SeparatorText(T("Geral"));
  if (ImGui::SliderFloat(T("DMX master"), &p.dmxMaster, 0, 1)) app.edited();
  ImGui::SetItemTooltip("%s", T("Intensidade de todos os LEDs (0 = blackout das luzes)"));
  for (auto &c : e.leds.conflicts()) ImGui::TextColored({1, 0.5f, 0.4f, 1}, T("Conflito: %s"), c.c_str());
  ImGui::TextDisabled(T("%d pacotes enviados"), e.leds.packetsSent());

  ImGui::SeparatorText(T("Monitor DMX"));
  const auto &unis = e.leds.universes();
  if (unis.empty()) {
    ImGui::TextDisabled("%s", T("Nada sendo enviado"));
    return;
  }
  static UniverseKey shownKey{-1, -1};
  if (!unis.count(shownKey)) shownKey = unis.begin()->first;
  auto keyName = [&](const UniverseKey &k) {
    const DmxDevice *d = p.dmxDevice(k.deviceId);
    return (d ? d->name : std::string("?")) + T(" · universo ") + std::to_string(k.universe);
  };
  if (ImGui::BeginCombo(T("Universo##mon"), keyName(shownKey).c_str())) {
    for (auto &[k, data] : unis)
      if (ImGui::Selectable(keyName(k).c_str(), !(k < shownKey) && !(shownKey < k))) shownKey = k;
    ImGui::EndCombo();
  }
  const DmxUniverse &data = unis.at(shownKey);
  ImDrawList *dl = ImGui::GetWindowDrawList();
  ImVec2 o = ImGui::GetCursorScreenPos();
  float cw = std::max(6.0f, (ImGui::GetContentRegionAvail().x - 4) / 32.0f), chh = cw;
  for (int i = 0; i < 512; ++i) {
    ImVec2 a{o.x + float(i % 32) * cw, o.y + float(i / 32) * chh};
    int v = data[size_t(i)];
    dl->AddRectFilled(a, {a.x + cw - 1, a.y + chh - 1}, IM_COL32(v, v, v, 255));
  }
  ImGui::Dummy({cw * 32, chh * 16});
  if (ImGui::IsItemHovered()) {
    ImVec2 mp = ImGui::GetIO().MousePos;
    int idx = int((mp.x - o.x) / cw) + int((mp.y - o.y) / chh) * 32;
    if (idx >= 0 && idx < 512) ImGui::SetTooltip(T("Canal %d = %d"), idx + 1, data[size_t(idx)]);
  }
  ImGui::TextDisabled("%s", T("Cada quadrado é um canal (1–512); passe o mouse para ver o valor."));
}

void Ui::projectInspector(App &app) {
  Project &p = app.engine.project;
  ImGui::SeparatorText(T("Canvas principal"));
  if (inputText(T("Nome##cv0"), p.canvasName)) app.edited();
  int size[2] = {p.canvasW, p.canvasH};
  if (ImGui::InputInt2(T("Resolução"), size, ImGuiInputTextFlags_EnterReturnsTrue)) {
    p.canvasW = std::clamp(size[0], 16, 16384);
    p.canvasH = std::clamp(size[1], 16, 16384);
    app.edited();
  }
  helpMarker(T("Espaço de trabalho onde as layers são compostas. As saídas recortam regiões dele. "
             "Pressione Enter para aplicar."));
  const char *presets[] = {"1280×720", "1920×1080", "2560×1440", T("3840×1080 (2 proj.)"), "3840×2160"};
  const int pw[] = {1280, 1920, 2560, 3840, 3840}, ph[] = {720, 1080, 1440, 1080, 2160};
  for (int i = 0; i < 5; ++i) {
    if (i) ImGui::SameLine();
    if (ImGui::SmallButton(presets[i])) {
      p.canvasW = pw[i];
      p.canvasH = ph[i];
      app.edited();
    }
  }
  if (colorEdit(T("Fundo"), p.background)) app.edited();

  ImGui::SeparatorText(T("Outros canvases (video routing)"));
  ImGui::TextWrapped("%s", T("Cada canvas tem as suas layers. Uma saída escolhe qual canvas mostra, e um "
                     "canvas pode virar mídia dentro de outro (Biblioteca → + Ao vivo → Canvas). "
                     "Ex.: vários canvases montados num único sinal 4K para o processador de LED."));
  int removeCanvas = -1;
  for (auto &c : p.canvases) {
    ImGui::PushID(c.id);
    ImGui::SetNextItemWidth(140);
    if (inputText("##name", c.name)) app.edited();
    ImGui::SameLine();
    int sz[2] = {c.width, c.height};
    ImGui::SetNextItemWidth(130);
    if (ImGui::InputInt2("##size", sz, ImGuiInputTextFlags_EnterReturnsTrue)) {
      c.width = std::clamp(sz[0], 16, 16384);
      c.height = std::clamp(sz[1], 16, 16384);
      app.edited();
    }
    ImGui::SameLine();
    if (colorEdit("##bg", c.background)) app.edited();
    ImGui::SameLine();
    if (ImGui::SmallButton(T("Ver"))) { workspace.viewCanvas = c.id; workspace.viewOutputId = -1; }
    ImGui::SameLine();
    if (ImGui::SmallButton("x")) removeCanvas = c.id;
    ImGui::PopID();
  }
  if (removeCanvas >= 0) ImGui::OpenPopup("Remover canvas?");
  static int pendingRemove = -1;
  if (removeCanvas >= 0) pendingRemove = removeCanvas;
  if (ImGui::BeginPopupModal("Remover canvas?", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
    ImGui::Text("%s", T("As layers deste canvas serão apagadas. As saídas voltam para o canvas principal."));
    if (ImGui::Button(T("Remover"))) {
      p.removeCanvas(pendingRemove);
      if (workspace.viewCanvas == pendingRemove) workspace.viewCanvas = 0;
      app.edited();
      ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button(T("Cancelar"))) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
  }
  if (ImGui::Button(T("+ Canvas"))) {
    CanvasConfig &c = p.addCanvas(1920, 1080);
    Layer &l = p.addLayer(c.name + T(" layer"), c.id);
    app.sel.layerId = l.id;
    workspace.viewCanvas = c.id;
    workspace.viewOutputId = -1;
    app.edited();
  }
  ImGui::SeparatorText(T("Show"));
  if (ImGui::DragFloat(T("Fade padrão (s)"), &p.defaultTransition, 0.05f, 0, 60)) app.edited();

  ImGui::SeparatorText(T("Ao abrir o projeto (instalação)"));
  if (ImGui::Checkbox(T("Abrir as saídas"), &p.startOutputs)) app.edited();
  ImGui::SetNextItemWidth(100);
  if (ImGui::InputInt(T("Disparar a coluna (0 = nenhuma)"), &p.startColumn)) {
    p.startColumn = std::max(0, p.startColumn);
    app.edited();
  }
  helpMarker(T("Para uma instalação que liga sozinha: coloque um atalho do Lumen com o projeto "
             "(Lumen.exe show.lumen) na pasta Inicializar do Windows (Win+R, shell:startup)."));

  ImGui::SeparatorText(T("Áudio"));
  Engine &e = app.engine;
  if (ImGui::BeginCombo(T("Saída de áudio"), p.audioDevice.empty() ? T("Padrão do sistema") : p.audioDevice.c_str())) {
    if (ImGui::Selectable(T("Padrão do sistema"), p.audioDevice.empty())) { p.audioDevice.clear(); app.edited(); }
    for (auto &d : AudioEngine::devices())
      if (ImGui::Selectable(d.c_str(), d == p.audioDevice)) { p.audioDevice = d; app.edited(); }
    ImGui::EndCombo();
  }
  ImGui::TextDisabled(T("Em uso: %s"), e.audio.deviceName().c_str());
  if (!e.audio.error().empty()) ImGui::TextColored({1, 0.5f, 0.4f, 1}, "%s", e.audio.error().c_str());
  if (ImGui::SliderFloat(T("Volume master"), &p.masterVolume, 0, 2, "%.2f")) app.edited();
  ImGui::ProgressBar(std::min(1.0f, e.audio.peak(0)), {-1, 5}, "");
  ImGui::ProgressBar(std::min(1.0f, e.audio.peak(1)), {-1, 5}, "");

  ImGui::SeparatorText(T("Vídeo"));
  if (ImGui::Checkbox(T("Decodificação por hardware"), &p.hardwareDecode)) app.edited();
  helpMarker(T("Usa a GPU (D3D11VA/DXVA2 no Windows) para H.264/HEVC/VP9/AV1. Vale para mídias "
             "disparadas depois da mudança. HAP sempre vai direto para a GPU."));
  ImGui::TextDisabled(T("HAP comprimido na GPU: %s"), FrameUploader::supportsS3tc() ? T("sim") : T("não suportado"));

  ImGui::SeparatorText(T("Spout (enviar o canvas)"));
  if (!spoutAvailable()) {
    ImGui::TextDisabled("%s", T("Disponível só no Windows"));
  } else {
    if (ImGui::Checkbox(T("Enviar canvas por Spout"), &p.spoutSend)) app.edited();
    if (inputText(T("Nome do sender"), p.spoutName)) app.edited();
    if (!e.spoutStatus().empty()) ImGui::TextDisabled("%s", e.spoutStatus().c_str());
  }
  ImGui::SeparatorText(T("NDI (enviar o canvas)"));
  if (ImGui::Checkbox(T("Enviar canvas por NDI"), &p.ndiSend)) app.edited();
  if (p.ndiSend) {
    if (inputText(T("Nome NDI##canvas"), p.ndiName)) app.edited();
    if (!e.ndiStatus().empty()) ImGui::TextDisabled("%s", e.ndiStatus().c_str());
  }
  ImGui::SeparatorText(T("Arquivo"));
  ImGui::TextWrapped("%s", p.filePath.empty() ? T("(projeto não salvo)") : p.filePath.c_str());
  ImGui::Text(T("%zu mídias · %zu layers · %zu colunas · %zu saídas"), p.library.size(),
              p.layers.size(), p.columns.size(), p.outputs.size());
}

void Ui::statusBar(App &app) {
  ImGui::Text(T("%.0f fps"), app.fps());
  ImGui::SameLine(0, 20);
  const Project &p = app.engine.project;
  ImGui::TextDisabled(T("Canvas %d×%d"), p.canvasW, p.canvasH);
  ImGui::SameLine(0, 20);
  ImGui::TextDisabled(T("Saídas abertas: %d (%.1f ms)"), app.outputs.openCount(), app.outputs.renderMs());
  ImGui::SameLine(0, 20);
  if (app.engine.oscServer().running()) ImGui::TextDisabled(T("OSC :%d"), app.engine.oscServer().port());
  else ImGui::TextDisabled("%s", T("OSC desligado"));
  if (!app.statusText().empty() && app.statusAge() < 6) {
    ImGui::SameLine(0, 30);
    if (app.statusIsError()) ImGui::TextColored({1, 0.45f, 0.45f, 1}, "%s", app.statusText().c_str());
    else ImGui::TextColored(kAccent, "%s", app.statusText().c_str());
  }
}

} // namespace lumen
