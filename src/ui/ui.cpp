#include "ui/ui.h"

#include "app.h"
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
const ImVec4 kAccent{0.98f, 0.62f, 0.18f, 1.0f};
const ImU32 kAccentU32 = IM_COL32(250, 158, 46, 255);
const ImU32 kPlayingU32 = IM_COL32(80, 200, 120, 255);

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

const char *mediaTypeLabel(MediaType t) {
  switch (t) {
  case MediaType::Video: return "Vídeo";
  case MediaType::Image: return "Imagem";
  case MediaType::Generator: return "Gerador";
  case MediaType::Live: return "Ao vivo";
  case MediaType::Spout: return "Spout";
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

  ImGui::StyleColorsDark();
  ImGuiStyle &s = ImGui::GetStyle();
  s.WindowRounding = 0;
  s.FrameRounding = 3;
  s.GrabRounding = 3;
  s.TabRounding = 3;
  s.WindowBorderSize = 1;
  s.FramePadding = {6, 4};
  s.ItemSpacing = {6, 5};
  ImVec4 *c = s.Colors;
  c[ImGuiCol_WindowBg] = {0.11f, 0.11f, 0.12f, 1};
  c[ImGuiCol_ChildBg] = {0.09f, 0.09f, 0.10f, 1};
  c[ImGuiCol_TitleBg] = c[ImGuiCol_TitleBgActive] = {0.14f, 0.14f, 0.15f, 1};
  c[ImGuiCol_Header] = {0.98f, 0.62f, 0.18f, 0.35f};
  c[ImGuiCol_HeaderHovered] = {0.98f, 0.62f, 0.18f, 0.55f};
  c[ImGuiCol_HeaderActive] = {0.98f, 0.62f, 0.18f, 0.8f};
  c[ImGuiCol_Button] = {0.22f, 0.22f, 0.24f, 1};
  c[ImGuiCol_ButtonHovered] = {0.32f, 0.32f, 0.35f, 1};
  c[ImGuiCol_ButtonActive] = {0.98f, 0.62f, 0.18f, 0.9f};
  c[ImGuiCol_FrameBg] = {0.17f, 0.17f, 0.19f, 1};
  c[ImGuiCol_FrameBgHovered] = {0.23f, 0.23f, 0.26f, 1};
  c[ImGuiCol_SliderGrab] = c[ImGuiCol_CheckMark] = kAccent;
  c[ImGuiCol_SliderGrabActive] = {1.0f, 0.75f, 0.35f, 1};
  c[ImGuiCol_Tab] = {0.16f, 0.16f, 0.18f, 1};
  c[ImGuiCol_TabHovered] = {0.98f, 0.62f, 0.18f, 0.6f};
  c[ImGuiCol_TabSelected] = {0.98f, 0.62f, 0.18f, 0.45f};
}

void Ui::draw(App &app) {
  ImGuiViewport *vp = ImGui::GetMainViewport();
  menuBar(app);

  const float toolbarH = 46, statusH = 26;
  const float libraryW = std::clamp(vp->WorkSize.x * 0.17f, 220.0f, 320.0f);
  const float inspectorW = std::clamp(vp->WorkSize.x * 0.24f, 320.0f, 440.0f);
  const float x0 = vp->WorkPos.x, y0 = vp->WorkPos.y;
  const float W = vp->WorkSize.x, H = vp->WorkSize.y;
  const float bodyY = y0 + toolbarH, bodyH = H - toolbarH - statusH;
  const float centerW = W - libraryW - inspectorW;
  const float dashH = std::clamp(bodyH * 0.38f, 180.0f, 420.0f);

  ImGuiWindowFlags fixed = ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
                           ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoBringToFrontOnFocus;

  ImGui::SetNextWindowPos({x0, y0});
  ImGui::SetNextWindowSize({W, toolbarH});
  ImGui::Begin("##toolbar", nullptr, fixed | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoScrollbar);
  toolbar(app);
  ImGui::End();

  ImGui::SetNextWindowPos({x0, bodyY});
  ImGui::SetNextWindowSize({libraryW, bodyH});
  ImGui::Begin("Biblioteca", nullptr, fixed);
  library(app);
  ImGui::End();

  ImGui::SetNextWindowPos({x0 + libraryW, bodyY});
  ImGui::SetNextWindowSize({centerW, bodyH - dashH});
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {4, 4});
  ImGui::Begin("Workspace", nullptr, fixed | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
  workspace.draw(app, ImGui::GetContentRegionAvail());
  ImGui::End();
  ImGui::PopStyleVar();

  ImGui::SetNextWindowPos({x0 + libraryW, bodyY + bodyH - dashH});
  ImGui::SetNextWindowSize({centerW, dashH});
  ImGui::Begin("Dashboard", nullptr, fixed);
  dashboard(app);
  ImGui::End();

  ImGui::SetNextWindowPos({x0 + W - inspectorW, bodyY});
  ImGui::SetNextWindowSize({inspectorW, bodyH});
  ImGui::Begin("Propriedades", nullptr, fixed);
  inspector(app);
  ImGui::End();

  ImGui::SetNextWindowPos({x0, y0 + H - statusH});
  ImGui::SetNextWindowSize({W, statusH});
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {8, 4});
  ImGui::Begin("##status", nullptr, fixed | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoScrollbar);
  statusBar(app);
  ImGui::End();
  ImGui::PopStyleVar();

  outputsWindow(app);
  if (showDemo_) ImGui::ShowDemoWindow(&showDemo_);
  if (showHelp_) {
    ImGui::SetNextWindowSize({520, 460}, ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Atalhos e ajuda", &showHelp_)) {
      ImGui::SeparatorText("Show");
      ImGui::BulletText("Espaço: GO (próxima coluna)   |   Shift+Espaço: coluna anterior");
      ImGui::BulletText("Esc: parar tudo com fade   |   B: blackout");
      ImGui::BulletText("Clique numa célula: dispara só aquela layer");
      ImGui::SeparatorText("Workspace / mapping");
      ImGui::BulletText("V: selecionar/mover   M: perspectiva (4 cantos)");
      ImGui::BulletText("G: malha (mesh warp)   K: máscara");
      ImGui::BulletText("Tab: próximo canto/ponto   Setas: ajuste fino (Shift = 10px)");
      ImGui::BulletText("Roda do mouse: zoom   Botão do meio / Alt+arrastar: mover a vista");
      ImGui::BulletText("F: enquadrar   Máscara: clique adiciona ponto, botão direito apaga");
      ImGui::SeparatorText("Projeto");
      ImGui::BulletText("Ctrl+N novo, Ctrl+O abrir, Ctrl+S salvar, Ctrl+Shift+S salvar como");
      ImGui::BulletText("Ctrl+Z desfazer, Ctrl+Y / Ctrl+Shift+Z refazer, Ctrl+I importar");
      ImGui::BulletText("Arraste arquivos do Explorer para a janela para importar");
      ImGui::SeparatorText("OSC");
      ImGui::TextWrapped("Envie mensagens para a porta configurada (padrão 8000). "
                         "Exemplos: /lumen/go, /lumen/column/launch 2, /lumen/layer/1/opacity 0.5. "
                         "Veja docs/OSC.md para a lista completa.");
    }
    ImGui::End();
  }
}

void Ui::menuBar(App &app) {
  if (!ImGui::BeginMainMenuBar()) return;
  if (ImGui::BeginMenu("Arquivo")) {
    if (ImGui::MenuItem("Novo projeto", "Ctrl+N")) app.newProject();
    if (ImGui::MenuItem("Abrir...", "Ctrl+O")) app.openDialog();
    if (ImGui::MenuItem("Salvar", "Ctrl+S")) app.saveProject(false);
    if (ImGui::MenuItem("Salvar como...", "Ctrl+Shift+S")) app.saveProject(true);
    ImGui::Separator();
    if (ImGui::MenuItem("Importar mídia...", "Ctrl+I")) app.importDialog();
    ImGui::Separator();
    if (ImGui::MenuItem("Sair")) app.requestQuit = true;
    ImGui::EndMenu();
  }
  if (ImGui::BeginMenu("Editar")) {
    if (ImGui::MenuItem("Desfazer", "Ctrl+Z")) app.undo();
    if (ImGui::MenuItem("Refazer", "Ctrl+Y")) app.redo();
    ImGui::Separator();
    if (ImGui::MenuItem("Nova layer")) {
      Layer &l = app.engine.project.addLayer();
      app.sel.layerId = l.id;
      app.edited();
    }
    if (ImGui::MenuItem("Nova coluna")) {
      app.engine.project.addColumn();
      app.edited();
    }
    ImGui::EndMenu();
  }
  if (ImGui::BeginMenu("Saídas")) {
    if (ImGui::MenuItem("Configurar saídas...", "Ctrl+Shift+P")) openOutputs();
    if (ImGui::MenuItem(app.outputsOpen ? "Fechar saídas" : "Abrir saídas", "Ctrl+P")) app.outputsOpen = !app.outputsOpen;
    ImGui::Separator();
    if (ImGui::MenuItem("Adicionar saída")) {
      OutputConfig &o = app.engine.project.addOutput();
      app.sel.outputId = o.id;
      app.sel.inspector = Inspector::Output;
      app.edited();
    }
    ImGui::Separator();
    auto mons = OutputManager::monitors();
    ImGui::TextDisabled("Monitores detectados:");
    for (size_t i = 0; i < mons.size(); ++i)
      ImGui::TextDisabled("  %zu: %s  %dx%d @%dHz", i + 1, mons[i].name.c_str(), mons[i].w,
                          mons[i].h, mons[i].refresh);
    ImGui::EndMenu();
  }
  if (ImGui::BeginMenu("Ajuda")) {
    if (ImGui::MenuItem("Atalhos e ajuda", "F1")) showHelp_ = true;
    ImGui::MenuItem("Demo do ImGui", nullptr, &showDemo_);
    ImGui::Separator();
    ImGui::TextDisabled("Lumen %s", LUMEN_VERSION);
    ImGui::EndMenu();
  }
  ImGui::EndMainMenuBar();
}

void Ui::toolbar(App &app) {
  Engine &e = app.engine;
  ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 2);

  ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.15f, 0.55f, 0.3f, 1));
  ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.2f, 0.7f, 0.4f, 1));
  if (ImGui::Button("  GO  ", {80, 30})) e.go();
  ImGui::PopStyleColor(2);
  ImGui::SetItemTooltip("Dispara a próxima coluna (Espaço)");
  ImGui::SameLine();
  if (ImGui::Button("<", {30, 30})) e.back();
  ImGui::SetItemTooltip("Coluna anterior (Shift+Espaço)");
  ImGui::SameLine();
  if (ImGui::Button("Parar", {60, 30})) e.stopAll();
  ImGui::SetItemTooltip("Para tudo com fade (Esc)");
  ImGui::SameLine();
  if (e.blackout) {
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.75f, 0.15f, 0.15f, 1));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.9f, 0.2f, 0.2f, 1));
  }
  if (ImGui::Button("Blackout", {80, 30})) e.blackout = !e.blackout;
  if (e.blackout) ImGui::PopStyleColor(2);
  ImGui::SetItemTooltip("Escurece todas as saídas (B)");
  ImGui::SameLine();
  bool open = app.outputsOpen;
  if (open) {
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.45f, 0.8f, 1));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.3f, 0.55f, 0.9f, 1));
  }
  if (ImGui::Button(open ? "Fechar saídas" : "Abrir saídas", {110, 30})) app.outputsOpen = !open;
  if (open) ImGui::PopStyleColor(2);
  ImGui::SameLine();
  if (ImGui::Button("Saídas...", {80, 30})) openOutputs();
  ImGui::SetItemTooltip("Layout dos projetores, telas, soft edge e mapping de cada saída");
  ImGui::SetItemTooltip("Abre/fecha as janelas dos projetores (Ctrl+P). O workspace mostra a prévia mesmo fechadas.");

  ImGui::SameLine(0, 20);
  ImGui::SetNextItemWidth(120);
  ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 5);
  ImGui::SliderFloat("Master", &e.masterOpacity, 0, 1, "%.2f");

  ImGui::SameLine(0, 20);
  ImGui::SetNextItemWidth(110);
  if (ImGui::DragFloat("Fade padrão", &e.project.defaultTransition, 0.05f, 0, 30, "%.2f s"))
    app.edited();

  ImGui::SameLine(0, 24);
  const char *tools[] = {"Selecionar (V)", "Perspectiva (M)", "Malha (G)", "Máscara (K)", "LED (L)"};
  for (int i = 0; i < 5; ++i) {
    bool active = int(workspace.tool) == i;
    if (active) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.98f, 0.62f, 0.18f, 0.7f));
    if (ImGui::Button(tools[i], {0, 30})) workspace.tool = Tool(i);
    if (active) ImGui::PopStyleColor();
    ImGui::SameLine();
  }

  ImGui::SameLine(0, 20);
  ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 6);
  int col = e.activeColumn();
  if (col >= 0 && col < int(e.project.columns.size()))
    ImGui::TextColored(kAccent, "No ar: %d · %s", col + 1, e.project.columns[size_t(col)].name.c_str());
  else
    ImGui::TextDisabled("Nada no ar");
}

void Ui::library(App &app) {
  Project &p = app.engine.project;
  if (ImGui::Button("Importar...")) app.importDialog();
  ImGui::SameLine();
  if (ImGui::Button("+ Gerador")) ImGui::OpenPopup("gen");
  if (ImGui::BeginPopup("gen")) {
    for (int k = 0; k < int(GeneratorKind::COUNT); ++k)
      if (ImGui::MenuItem(generatorName(GeneratorKind(k)))) {
        MediaItem &m = p.addGenerator(GeneratorKind(k));
        app.sel.mediaId = m.id;
        app.sel.inspector = Inspector::Media;
        app.edited();
      }
    ImGui::EndPopup();
  }
  ImGui::SameLine();
  if (ImGui::Button("+ Ao vivo")) {
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
    ImGui::SeparatorText("Câmeras e placas de captura");
#ifdef __APPLE__
    // FFmpeg cannot list AVFoundation devices: offer the usual indexes.
    if (ImGui::MenuItem("Câmera 1 do Mac (AVFoundation)")) add(p.addLive("Câmera 1", "0", "avfoundation", "framerate=30"));
    if (ImGui::MenuItem("Câmera 2 do Mac (AVFoundation)")) add(p.addLive("Câmera 2", "1", "avfoundation", "framerate=30"));
#endif
    if (captureDevices_.empty()) ImGui::TextDisabled("Nenhum dispositivo encontrado");
    for (auto &d : captureDevices_)
      if (ImGui::MenuItem(d.name.c_str())) add(p.addLive(d.name, d.url, d.format, ""));
    if (ImGui::SmallButton("Procurar de novo")) captureDevices_ = listCaptureDevices();
    ImGui::SeparatorText("Stream de rede");
    ImGui::SetNextItemWidth(300);
    ImGui::InputTextWithHint("##url", "rtsp://, srt://, udp://, http://...", liveUrl_, sizeof(liveUrl_));
    ImGui::SameLine();
    if (ImGui::Button("Adicionar") && liveUrl_[0]) add(p.addLive("", liveUrl_, "", ""));
    ImGui::SeparatorText("Spout (Windows)");
    if (!spoutAvailable()) ImGui::TextDisabled("Disponível só no Windows (no Mac o equivalente é o Syphon)");
    else {
      if (ImGui::MenuItem("Sender ativo (qualquer um)")) add(p.addSpout(""));
      for (auto &n : spoutSenders_)
        if (ImGui::MenuItem(n.c_str())) add(p.addSpout(n));
    }
    ImGui::SeparatorText("Teste");
    if (ImGui::MenuItem("Sinal de teste ao vivo (lavfi)"))
      add(p.addLive("Sinal de teste", "testsrc2=size=1280x720:rate=30", "lavfi", ""));
    ImGui::EndPopup();
  }
  ImGui::SetNextItemWidth(-1);
  ImGui::InputTextWithHint("##filter", "Buscar mídia...", filterText_, sizeof(filterText_));
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
      if (ImGui::MenuItem("Remover da biblioteca")) removeId = m.id;
      ImGui::EndPopup();
    }
    const Texture *thumb = app.engine.cache.thumbnail(m, app.engine.generators, app.engine.mesh);
    ImDrawList *dl = ImGui::GetWindowDrawList();
    ImVec2 tl{start.x + 2, start.y + 2}, br{start.x + 78, start.y + 46};
    dl->AddRectFilled(tl, br, IM_COL32(15, 15, 15, 255));
    if (thumb && !thumb->empty()) dl->AddImage(texId(*thumb), tl, br);
    else dl->AddText({tl.x + 6, tl.y + 14}, IM_COL32(200, 80, 80, 255), "erro");
    dl->AddText({start.x + 86, start.y + 6}, IM_COL32(230, 230, 230, 255), m.name.c_str());
    dl->AddText({start.x + 86, start.y + 25}, IM_COL32(140, 140, 140, 255), mediaTypeLabel(m.type));
    ImGui::PopID();
  }
  if (p.library.empty())
    ImGui::TextWrapped("Arraste vídeos ou imagens para esta janela, ou clique em Importar. "
                       "Geradores (test card, grade...) ajudam a alinhar projetores.");
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

  if (ImGui::Button("+ Layer")) {
    Layer &l = p.addLayer();
    app.sel.layerId = l.id;
    app.sel.slice = 0;
    app.edited();
  }
  ImGui::SameLine();
  if (ImGui::Button("+ Coluna")) {
    p.addColumn();
    app.edited();
  }
  ImGui::SameLine();
  ImGui::TextDisabled("Arraste mídias para as células. Clique numa célula para disparar.");
  ImGui::SameLine(std::max(ImGui::GetCursorPosX() + 10, ImGui::GetWindowWidth() - 150));
  ImGui::SetNextItemWidth(130);
  ImGui::Combo("##zoom", &dashZoom_, "pequeno\0médio\0grande\0");

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
  ImGui::TextDisabled("Layers \\ Cues");
  int removeColumn = -1, moveColumn = -1, moveDir = 0;
  for (int c = 0; c < ncols; ++c) {
    ImGui::TableSetColumnIndex(c + 1);
    ImGui::PushID(p.columns[size_t(c)].id);
    Column &colRef = p.columns[size_t(c)];
    bool active = e.activeColumn() == c;
    if (active) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.98f, 0.62f, 0.18f, 0.85f));
    char label[96];
    std::snprintf(label, sizeof(label), "%02d  \xe2\x96\xb6  %s", c + 1, colRef.name.c_str()); // ▶
    if (ImGui::Button(label, {cellW - 4, 0})) e.launchColumn(c);
    if (active) ImGui::PopStyleColor();
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
      if (colRef.transitionType == TransitionType::Cut || dur <= 0) std::snprintf(tl, sizeof(tl), "corte");
      else std::snprintf(tl, sizeof(tl), "%s %.1fs", colRef.transitionType == TransitionType::ThroughBlack ? "pelo preto" : "crossfade", dur);
      // Auto-go: countdown on the live column, wait time on the others.
      if (colRef.autoGo >= 0) {
        size_t n = std::strlen(tl);
        float left = active ? e.autoGoRemaining() : -1;
        if (left >= 0) std::snprintf(tl + n, sizeof(tl) - n, "  \xe2\x8f\xb1%.1f", left); // ⏱
        else std::snprintf(tl + n, sizeof(tl) - n, "  \xe2\x8f\xb1%.0fs", colRef.autoGo);
      }
      ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
      ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
      if (ImGui::SmallButton(tl)) ImGui::OpenPopup("colpop");
      ImGui::PopStyleColor(2);
    }
    bool popupOpen = ImGui::BeginPopup("colpop");
    if (!popupOpen) popupOpen = ImGui::BeginPopupContextItem("colpop");
    if (popupOpen) {
      ImGui::SeparatorText("Coluna");
      ImGui::SetNextItemWidth(220);
      if (inputText("Título", colRef.name)) app.edited();
      {
        char notes[1024];
        std::snprintf(notes, sizeof(notes), "%s", colRef.notes.c_str());
        if (ImGui::InputTextMultiline("Notas", notes, sizeof(notes), {220, 54})) {
          colRef.notes = notes;
          app.edited();
        }
      }
      ImGui::SeparatorText("Transição");
      int tt = int(colRef.transitionType);
      ImGui::SetNextItemWidth(160);
      if (ImGui::Combo("Tipo", &tt, "Crossfade\0Corte (sem transição)\0Fade pelo preto\0")) {
        colRef.transitionType = TransitionType(tt);
        app.edited();
      }
      float &tr = colRef.transition;
      bool custom = tr >= 0;
      if (ImGui::Checkbox("Duração própria", &custom)) {
        tr = custom ? p.defaultTransition : -1;
        app.edited();
      }
      if (custom) {
        ImGui::SetNextItemWidth(160);
        if (ImGui::DragFloat("Duração (s)", &tr, 0.05f, 0, 60, "%.2f s")) app.edited();
      } else {
        ImGui::SameLine();
        ImGui::TextDisabled("(padrão %.2f s)", p.defaultTransition);
      }
      ImGui::SeparatorText("Auto-GO");
      {
        bool on = colRef.autoGo >= 0;
        if (ImGui::Checkbox("Disparar outra coluna sozinho", &on)) {
          colRef.autoGo = on ? 10.0f : -1.0f;
          app.edited();
        }
        helpMarker("Para instalações: depois do tempo de espera, dispara a próxima coluna "
                   "(na última, volta para a primeira) ou uma coluna escolhida.");
        if (on) {
          ImGui::SetNextItemWidth(160);
          if (ImGui::DragFloat("Espera (s)", &colRef.autoGo, 0.1f, 0, 36000, "%.1f s")) app.edited();
          if (ImGui::Checkbox("Contar depois da transição", &colRef.autoGoAfterTransition)) app.edited();
          ImGui::SetNextItemWidth(160);
          if (ImGui::InputInt("Coluna (0 = próxima)", &colRef.autoGoTarget)) {
            colRef.autoGoTarget = std::clamp(colRef.autoGoTarget, 0, ncols);
            app.edited();
          }
        }
      }
      ImGui::SeparatorText("Estados (animar parâmetros)");
      {
        helpMarker("Grava o valor atual de parâmetros de uma layer nesta coluna. Ao disparar a "
                   "coluna, eles vão até esse valor durante a transição. Colunas sem valor "
                   "herdam o da coluna anterior.");
        Layer *sl = p.layer(app.sel.layerId);
        static const MappingTarget recordable[] = {
            MappingTarget::Opacity,    MappingTarget::PositionX,  MappingTarget::PositionY,
            MappingTarget::Scale,      MappingTarget::Rotation,   MappingTarget::Brightness,
            MappingTarget::Contrast,   MappingTarget::Saturation, MappingTarget::Hue,
            MappingTarget::Volume};
        if (sl) {
          ImGui::Text("Layer selecionada: %s", sl->name.c_str());
          if (ImGui::Button("Gravar opacidade + transformação")) {
            for (auto t : {MappingTarget::Opacity, MappingTarget::PositionX, MappingTarget::PositionY,
                           MappingTarget::Scale, MappingTarget::Rotation})
              e.storeState(c, sl->id, t);
            app.edited();
          }
          ImGui::SameLine();
          if (ImGui::Button("Gravar...")) ImGui::OpenPopup("statepick");
          if (ImGui::BeginPopup("statepick")) {
            for (auto t : recordable)
              if (ImGui::MenuItem(mappingTargetName(t))) { e.storeState(c, sl->id, t); app.edited(); }
            ImGui::EndPopup();
          }
        } else {
          ImGui::TextDisabled("Selecione uma layer para gravar estados.");
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
      ImGui::SeparatorText("Organizar");
      if (ImGui::ColorEdit3("Cor", colRef.color, ImGuiColorEditFlags_NoInputs)) app.edited();
      if (ImGui::MenuItem("Mover para a esquerda", nullptr, false, c > 0)) { moveColumn = c; moveDir = -1; }
      if (ImGui::MenuItem("Mover para a direita", nullptr, false, c + 1 < ncols)) { moveColumn = c; moveDir = 1; }
      ImGui::SeparatorText("Ações OSC ao disparar");
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
        ImGui::TextDisabled("Uma por linha, ex.: /luz/cena 3   (vai para as saídas OSC)");
      }
      if (ImGui::MenuItem("Remover coluna")) removeColumn = c;
      ImGui::EndPopup();
    }
    ImGui::PopID();
  }
  // Stop column: stops everything, and per layer below.
  ImGui::TableSetColumnIndex(ncols + 1);
  if (ImGui::Button("\xe2\x96\xa0##stopall", {32, 0})) e.stopAll(); // ■
  ImGui::SetItemTooltip("Parar tudo (Esc)");

  int removeLayer = -1, moveLayer = -1, moveLayerDir = 0;
  for (size_t li = 0; li < p.layers.size(); ++li) {
    Layer &l = p.layers[li];
    ImGui::TableNextRow(0, cellH);
    ImGui::TableSetColumnIndex(0);
    ImGui::PushID(l.id);
    bool selected = app.sel.layerId == l.id;
    if (ImGui::Checkbox("##vis", &l.visible)) app.edited();
    ImGui::SetItemTooltip("Visível");
    ImGui::SameLine();
    if (ImGui::Selectable(l.name.c_str(), selected, 0, {nameW - 36, 0})) {
      app.sel.layerId = l.id;
      app.sel.slice = 0;
      app.sel.inspector = Inspector::Layer;
    }
    if (ImGui::BeginPopupContextItem()) {
      ImGui::SetNextItemWidth(160);
      if (inputText("Nome", l.name)) app.edited();
      if (ImGui::MenuItem("Subir", nullptr, false, li > 0)) { moveLayer = int(li); moveLayerDir = -1; }
      if (ImGui::MenuItem("Descer", nullptr, false, li + 1 < p.layers.size())) { moveLayer = int(li); moveLayerDir = 1; }
      if (ImGui::MenuItem("Parar layer")) e.stopLayer(l.id);
      if (ImGui::MenuItem("Remover layer")) removeLayer = l.id;
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
      if (cell && ImGui::BeginDragDropSource()) {
        int payload[2] = {c, l.id};
        ImGui::SetDragDropPayload("LUMEN_CELL", payload, sizeof(payload));
        ImGui::Text("%s  (Ctrl = copiar)", m ? m->name.c_str() : "");
        ImGui::EndDragDropSource();
      }
      if (ImGui::BeginDragDropTarget()) {
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
      if (cell && ImGui::BeginPopupContextItem("##cellctx")) {
        ImGui::TextDisabled("No fim da mídia");
        for (int em = 0; em < int(EndMode::COUNT); ++em) {
          static const char *names[] = {"Loop", "Congelar no último quadro", "Parar",
                                        "Disparar a próxima coluna"};
          if (ImGui::MenuItem(names[em], nullptr, int(cell->endMode) == em)) {
            cell->setEndMode(EndMode(em));
            app.edited();
          }
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Limpar célula")) {
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
      dl->AddRectFilled(pos, br, hovered ? IM_COL32(48, 48, 52, 255) : IM_COL32(30, 30, 33, 255), 4);
      if (m) {
        const Texture *thumb = e.cache.thumbnail(*m, e.generators, e.mesh);
        if (thumb && !thumb->empty()) {
          ImVec2 ttl{pos.x + 3, pos.y + 3}, tbr{br.x - 3, br.y - 20};
          dl->AddImage(texId(*thumb), ttl, tbr);
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
        dl->AddRect(pos, br, kPlayingU32, 4, 0, 2.5f);
        double dur = st->current->duration();
        if (dur > 0) {
          float t = float(std::fmod(st->current->time(), dur) / dur);
          dl->AddRectFilled({pos.x + 3, br.y - 3}, {pos.x + 3 + (size.x - 6) * t, br.y - 1}, kPlayingU32);
        }
      } else if (app.sel.cellColumn == c && app.sel.cellLayer == l.id) {
        dl->AddRect(pos, br, kAccentU32, 4, 0, 1.5f);
      }
      ImGui::PopID();
    }
    ImGui::TableSetColumnIndex(ncols + 1);
    if (st && (st->current || st->previous)) {
      if (ImGui::Button("\xe2\x96\xa0##stopl", {32, cellH - 10})) e.stopLayer(l.id); // ■
      ImGui::SetItemTooltip("Parar esta layer");
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
  const Tab tabs[] = {{"Layer", Inspector::Layer},   {"Célula", Inspector::Cell},
                      {"Mídia", Inspector::Media},   {"Saídas", Inspector::Output}, {"LED", Inspector::Led},
                      {"Interação", Inspector::Interaction}, {"Projeto", Inspector::Project}};
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

void Ui::layerInspector(App &app) {
  Project &p = app.engine.project;
  Layer *l = p.layer(app.sel.layerId);
  if (!l) {
    ImGui::TextWrapped("Selecione uma layer no dashboard.");
    return;
  }
  ImGui::PushID(l->id);
  if (inputText("Nome", l->name)) app.edited();
  if (ImGui::Checkbox("Visível", &l->visible)) app.edited();
  if (ImGui::SliderFloat("Opacidade", &l->opacity, 0, 1)) app.edited();
  if (ImGui::SliderFloat("Volume", &l->volume, 0, 2, "%.2f")) app.edited();
  ImGui::SameLine();
  if (ImGui::Checkbox("Mudo", &l->mute)) app.edited();
  if (const LayerState *st = app.engine.layerState(l->id); st && st->current) {
    if (st->current->hasAudio()) {
      ImGui::ProgressBar(std::min(1.0f, st->current->audioPeak()), {-1, 6}, "");
    } else if (st->current->speed != 1.0f) {
      ImGui::TextDisabled("Áudio desligado: velocidade diferente de 1x");
    }
    std::string hw = st->current->hwaccel();
    if (!hw.empty()) ImGui::TextDisabled("Decodificação por hardware: %s", hw.c_str());
  }
  int blend = int(l->blend);
  if (ImGui::Combo("Blend", &blend, "Normal\0Adição\0Multiplicar\0Tela (screen)\0")) {
    l->blend = BlendMode(blend);
    app.edited();
  }

  if (ImGui::CollapsingHeader("Transformação", ImGuiTreeNodeFlags_DefaultOpen)) {
    float pos[2] = {l->transform.position.x, l->transform.position.y};
    if (ImGui::DragFloat2("Posição (px)", pos, 1.0f)) {
      l->transform.position = {pos[0], pos[1]};
      app.edited();
    }
    if (ImGui::DragFloat("Escala", &l->transform.scale, 0.005f, 0.01f, 20)) app.edited();
    if (ImGui::DragFloat("Rotação (°)", &l->transform.rotation, 0.25f, -360, 360)) app.edited();
    if (ImGui::Button("Zerar transformação")) {
      l->transform = {};
      app.edited();
    }
    helpMarker("A transformação move todas as fatias juntas. Útil para animar via OSC "
               "sem perder o mapeamento.");
  }

  if (ImGui::CollapsingHeader("Cor / efeitos", ImGuiTreeNodeFlags_DefaultOpen)) {
    if (ImGui::SliderFloat("Brilho", &l->fx.brightness, -1, 1)) app.edited();
    if (ImGui::SliderFloat("Contraste", &l->fx.contrast, 0, 2)) app.edited();
    if (ImGui::SliderFloat("Saturação", &l->fx.saturation, 0, 2)) app.edited();
    if (ImGui::SliderFloat("Matiz (°)", &l->fx.hue, -180, 180)) app.edited();
    if (ImGui::Checkbox("Inverter", &l->fx.invert)) app.edited();
    if (colorEdit("Tinta", l->fx.tint)) app.edited();
    if (ImGui::Button("Zerar efeitos")) {
      l->fx = {};
      app.edited();
    }
  }

  if (ImGui::CollapsingHeader("Fatias (slices) / mapping", ImGuiTreeNodeFlags_DefaultOpen)) {
    ImGui::TextWrapped("Cada fatia recorta uma parte da mídia e a projeta numa superfície. "
                       "Use as ferramentas Perspectiva (M) e Malha (G) no workspace.");
    int &si = app.sel.slice;
    si = std::clamp(si, 0, int(l->slices.size()) - 1);
    for (size_t i = 0; i < l->slices.size(); ++i) {
      ImGui::PushID(int(i));
      if (ImGui::Checkbox("##en", &l->slices[i].enabled)) app.edited();
      ImGui::SameLine();
      if (ImGui::Selectable(l->slices[i].name.c_str(), si == int(i))) si = int(i);
      ImGui::PopID();
    }
    if (ImGui::Button("+ Fatia")) {
      Slice s;
      s.name = "Fatia " + std::to_string(l->slices.size() + 1);
      float w = float(p.canvasW) * 0.3f, h = float(p.canvasH) * 0.3f;
      s.warp.setRect({float(p.canvasW) * 0.35f, float(p.canvasH) * 0.35f}, {w, h});
      l->slices.push_back(s);
      si = int(l->slices.size()) - 1;
      app.edited();
    }
    ImGui::SameLine();
    if (ImGui::Button("Duplicar") && si < int(l->slices.size())) {
      Slice s = l->slices[size_t(si)];
      s.name += " cópia";
      s.warp.translate({30, 30});
      l->slices.push_back(s);
      si = int(l->slices.size()) - 1;
      app.edited();
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(l->slices.size() <= 1);
    if (ImGui::Button("Remover")) {
      l->slices.erase(l->slices.begin() + si);
      si = std::max(0, si - 1);
      app.edited();
    }
    ImGui::EndDisabled();

    if (si < int(l->slices.size())) {
      Slice &s = l->slices[size_t(si)];
      ImGui::SeparatorText(s.name.c_str());
      if (inputText("Nome da fatia", s.name)) app.edited();
      // Surface: a mapping shared with other layers.
      {
        Surface *bound = p.surface(s.surfaceId);
        if (ImGui::BeginCombo("Surface", bound ? bound->name.c_str() : "(mapping próprio)")) {
          if (ImGui::Selectable("(mapping próprio)", !bound)) { s.surfaceId = -1; app.edited(); }
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
          if (ImGui::Selectable("+ Nova surface com este mapping")) {
            s.surfaceId = p.addSurface(s.warp).id;
            app.edited();
          }
          ImGui::EndCombo();
        }
        helpMarker("Surface = mapping compartilhado. Várias layers ligadas à mesma surface "
                   "seguem o mesmo mapping: quando o cenário ou o projetor muda de lugar, "
                   "ajuste uma vez e todas acompanham.");
        if (bound) {
          ImGui::SameLine();
          if (ImGui::SmallButton("Soltar")) { s.surfaceId = -1; app.edited(); }
          int users = 0;
          for (auto &L : p.layers)
            for (auto &S : L.slices) users += S.surfaceId == bound->id;
          ImGui::PushID("surf");
          ImGui::SetNextItemWidth(140);
          if (inputText("##sname", bound->name)) app.edited();
          ImGui::SameLine();
          if (colorEdit("##scol", bound->color)) app.edited();
          ImGui::SameLine();
          ImGui::TextDisabled("%d fatia(s)", users);
          ImGui::SameLine();
          if (ImGui::SmallButton("Apagar surface")) {
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
      if (ImGui::DragFloat4("Recorte UV", crop, 0.002f, 0, 1)) {
        s.u0 = crop[0]; s.v0 = crop[1]; s.u1 = crop[2]; s.v1 = crop[3];
        app.edited();
      }
      helpMarker("Parte da mídia usada por esta fatia: u0 v0 u1 v1 (0 a 1).");
      for (int c = 0; c < 4; ++c) {
        const char *names[] = {"Canto sup. esq.", "Canto sup. dir.", "Canto inf. dir.", "Canto inf. esq."};
        float v[2] = {s.warp.corners[size_t(c)].x, s.warp.corners[size_t(c)].y};
        if (ImGui::DragFloat2(names[c], v, 0.5f)) {
          s.warp.corners[size_t(c)] = {v[0], v[1]};
          app.edited();
        }
      }
      int grid[2] = {s.warp.meshCols, s.warp.meshRows};
      if (ImGui::InputInt2("Malha (col × lin)", grid)) {
        grid[0] = std::clamp(grid[0], 2, 33);
        grid[1] = std::clamp(grid[1], 2, 33);
        s.warp.resetMesh(grid[0], grid[1]);
        app.edited();
      }
      if (ImGui::Button("Retângulo da mídia")) {
        // Fit the slice to the media aspect ratio, centred on the canvas.
        float aspect = 16.0f / 9.0f;
        if (const LayerState *st = app.engine.layerState(l->id); st && st->currentTex)
          aspect = float(st->currentTex->width()) / float(std::max(1, st->currentTex->height()));
        aspect *= (s.u1 - s.u0) / std::max(1e-3f, s.v1 - s.v0);
        float h = float(p.canvasH) * 0.8f, w = h * aspect;
        if (w > float(p.canvasW) * 0.95f) { w = float(p.canvasW) * 0.95f; h = w / aspect; }
        s.warp.setRect({(float(p.canvasW) - w) * 0.5f, (float(p.canvasH) - h) * 0.5f}, {w, h});
        app.edited();
      }
      ImGui::SameLine();
      if (ImGui::Button("Tela cheia")) {
        s.warp.setRect({0, 0}, {float(p.canvasW), float(p.canvasH)});
        app.edited();
      }
      ImGui::SameLine();
      if (ImGui::Button("Zerar malha")) {
        s.warp.resetMesh(s.warp.meshCols, s.warp.meshRows);
        app.edited();
      }
    }
  }

  if (ImGui::CollapsingHeader("Máscaras", ImGuiTreeNodeFlags_DefaultOpen)) {
    ImGui::TextWrapped("Ferramenta Máscara (K): clique no workspace para adicionar pontos.");
    int &mi = app.sel.mask;
    for (size_t i = 0; i < l->masks.size(); ++i) {
      ImGui::PushID(1000 + int(i));
      if (ImGui::Checkbox("##en", &l->masks[i].enabled)) app.edited();
      ImGui::SameLine();
      if (ImGui::Selectable(l->masks[i].name.c_str(), mi == int(i))) {
        mi = int(i);
        app.sel.maskPoint = -1;
      }
      ImGui::PopID();
    }
    if (ImGui::Button("+ Máscara")) {
      Mask m;
      m.name = "Máscara " + std::to_string(l->masks.size() + 1);
      l->masks.push_back(m);
      mi = int(l->masks.size()) - 1;
      app.sel.maskPoint = -1;
      workspace.tool = Tool::Mask;
      app.edited();
    }
    if (mi >= 0 && mi < int(l->masks.size())) {
      Mask &m = l->masks[size_t(mi)];
      ImGui::SameLine();
      if (ImGui::Button("Remover máscara")) {
        l->masks.erase(l->masks.begin() + mi);
        mi = -1;
        app.edited();
      } else {
        ImGui::SeparatorText(m.name.c_str());
        if (inputText("Nome da máscara", m.name)) app.edited();
        int mode = m.subtract ? 1 : 0;
        if (ImGui::Combo("Modo", &mode, "Revelar dentro\0Esconder dentro\0")) {
          m.subtract = mode == 1;
          app.edited();
        }
        if (ImGui::SliderFloat("Suavizar borda (px)", &m.feather, 0, 200)) app.edited();
        ImGui::Text("%zu pontos", m.points.size());
        ImGui::SameLine();
        if (ImGui::SmallButton("Limpar pontos")) {
          m.points.clear();
          app.edited();
        }
      }
    }
  }
  ImGui::PopID();
}

void Ui::cellInspector(App &app) {
  Project &p = app.engine.project;
  int c = app.sel.cellColumn;
  if (c < 0 || c >= int(p.columns.size())) {
    ImGui::TextWrapped("Clique numa célula do dashboard.");
    return;
  }
  Column &col = p.columns[size_t(c)];
  auto it = col.cells.find(app.sel.cellLayer);
  Layer *l = p.layer(app.sel.cellLayer);
  if (it == col.cells.end() || it->second.mediaId < 0 || !l) {
    ImGui::TextWrapped("Célula vazia. Arraste uma mídia da biblioteca para ela.");
    return;
  }
  Cell &cell = it->second;
  MediaItem *m = p.media(cell.mediaId);
  ImGui::Text("Coluna %d · %s", c + 1, col.name.c_str());
  ImGui::Text("Layer: %s", l->name.c_str());
  ImGui::Text("Mídia: %s", m ? m->name.c_str() : "(removida)");
  ImGui::Separator();
  {
    int em = int(cell.endMode);
    if (ImGui::Combo("No fim", &em, "Loop\0Congelar no último quadro\0Parar\0Disparar a próxima coluna\0")) {
      cell.setEndMode(EndMode(em));
      app.edited();
    }
    helpMarker("Parar: a mídia some com a transição da coluna. Próxima coluna: o show segue "
               "sozinho quando o vídeo acaba (auto-follow), bom para instalações.");
  }
  if (ImGui::DragFloat("Velocidade", &cell.speed, 0.01f, 0, 8, "%.2fx")) app.edited();
  if (ImGui::Button("Disparar")) app.engine.launchCell(c, l->id);
  ImGui::SameLine();
  if (ImGui::Button("Parar layer")) app.engine.stopLayer(l->id);
  ImGui::SameLine();
  if (ImGui::Button("Limpar célula")) {
    col.cells.erase(it);
    app.edited();
  }
}

void Ui::mediaInspector(App &app) {
  Project &p = app.engine.project;
  MediaItem *m = p.media(app.sel.mediaId);
  if (!m) {
    ImGui::TextWrapped("Selecione um item da biblioteca.");
    return;
  }
  ImGui::PushID(m->id);
  const Texture *thumb = app.engine.cache.thumbnail(*m, app.engine.generators, app.engine.mesh);
  float w = ImGui::GetContentRegionAvail().x;
  fittedImage(thumb, {w, w * 9.0f / 16.0f});
  if (inputText("Nome", m->name)) app.edited();
  ImGui::Text("Tipo: %s", mediaTypeLabel(m->type));

  if (m->type == MediaType::Generator) {
    int kind = int(m->generator);
    std::string items;
    for (int k = 0; k < int(GeneratorKind::COUNT); ++k) {
      items += generatorName(GeneratorKind(k));
      items.push_back('\0');
    }
    if (ImGui::Combo("Gerador", &kind, items.c_str())) {
      m->generator = GeneratorKind(kind);
      app.edited();
    }
    int size[2] = {m->width, m->height};
    if (ImGui::InputInt2("Resolução", size)) {
      m->width = std::clamp(size[0], 16, 8192);
      m->height = std::clamp(size[1], 16, 8192);
      app.edited();
    }
    if (colorEdit("Cor 1", m->color1)) app.edited();
    if (colorEdit("Cor 2", m->color2)) app.edited();
    const char *p1 = "Parâmetro 1", *p2 = "Parâmetro 2";
    float lo1 = 0, hi1 = 10, lo2 = 0, hi2 = 10;
    switch (m->generator) {
    case GeneratorKind::Gradient: p1 = "Ângulo (°)"; lo1 = -180; hi1 = 180; break;
    case GeneratorKind::Grid: p1 = "Tamanho da célula"; lo1 = 4; hi1 = 512; p2 = "Espessura"; lo2 = 0.5f; hi2 = 32; break;
    case GeneratorKind::Plasma: p1 = "Velocidade"; p2 = "Escala"; lo2 = 0.1f; hi2 = 20; break;
    case GeneratorKind::Stripes: p1 = "Velocidade"; lo1 = -5; hi1 = 5; p2 = "Quantidade"; lo2 = 1; hi2 = 100; break;
    default: break;
    }
    if (ImGui::SliderFloat(p1, &m->param1, lo1, hi1)) app.edited();
    if (ImGui::SliderFloat(p2, &m->param2, lo2, hi2)) app.edited();
  } else if (m->type == MediaType::Live) {
    bool changed = inputText("URL / dispositivo", m->path);
    changed |= inputText("Formato", m->liveFormat);
    helpMarker("dshow (Windows), v4l2 (Linux), lavfi (teste) ou vazio para streams de rede.");
    changed |= inputText("Opções", m->liveOptions);
    helpMarker("Opções do FFmpeg separadas por ':', ex.: video_size=1920x1080:framerate=30 "
               "ou rtsp_transport=tcp");
    if (changed) {
      app.engine.cache.invalidate(m->id);
      app.edited();
    }
    ImGui::TextDisabled("A entrada abre quando alguma layer a dispara e fecha quando ninguém a usa.");
  } else if (m->type == MediaType::Spout) {
    if (inputText("Sender", m->path)) {
      app.engine.cache.invalidate(m->id);
      app.edited();
    }
    helpMarker("Nome do sender Spout. Vazio = o sender ativo.");
    if (!spoutAvailable()) ImGui::TextColored({1, 0.6f, 0.3f, 1}, "Spout só funciona no Windows.");
  } else {
    ImGui::TextWrapped("Arquivo: %s", m->path.c_str());
    if (const MediaInfo *info = app.engine.cache.info(*m)) {
      ImGui::Text("Resolução: %d × %d", info->width, info->height);
      if (m->type == MediaType::Video) {
        ImGui::Text("Duração: %.2f s   ·   %.2f fps", info->duration, info->fps);
        ImGui::Text("Codec: %s%s", info->codec.c_str(), info->hasAudio ? "   (com áudio)" : "");
        if (info->codec == "hap" && !FrameUploader::supportsS3tc())
          ImGui::TextDisabled("Esta GPU não aceita HAP comprimido: decodificando na CPU.");
      }
    } else {
      ImGui::TextColored({1, 0.4f, 0.4f, 1}, "Não foi possível abrir este arquivo.");
    }
    if (ImGui::Button("Trocar arquivo...")) {
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
  if (ImGui::CollapsingHeader("Assistente de blend (vários projetores)")) {
    static BlendLayoutParams bp;
    static int firstMonitor = 1; // monitor 2: the control screen is usually monitor 1
    static bool scale = true;
    ImGui::TextWrapped("Monta as saídas, as regiões do canvas e as bordas suaves de uma grade de "
                       "projetores sobrepostos.");
    ImGui::SetNextItemWidth(120);
    ImGui::InputInt("Colunas", &bp.cols);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(120);
    ImGui::InputInt("Linhas", &bp.rows);
    bp.cols = std::clamp(bp.cols, 1, 16);
    bp.rows = std::clamp(bp.rows, 1, 16);
    int res[2] = {bp.projW, bp.projH};
    if (ImGui::InputInt2("Resolução do projetor", res)) {
      bp.projW = std::clamp(res[0], 16, 16384);
      bp.projH = std::clamp(res[1], 16, 16384);
    }
    float ov[2] = {bp.overlapX * 100, bp.overlapY * 100};
    if (ImGui::DragFloat2("Sobreposição % (horiz/vert)", ov, 0.1f, 0, 50, "%.1f%%")) {
      bp.overlapX = ov[0] / 100;
      bp.overlapY = ov[1] / 100;
    }
    helpMarker("Quanto da largura de cada projetor se sobrepõe ao vizinho. 10–20% é o comum.");
    auto mons = OutputManager::monitors();
    std::string mlabel = firstMonitor < 0 ? "Janelas (teste)" : "A partir do monitor " + std::to_string(firstMonitor + 1);
    if (ImGui::BeginCombo("Monitores", mlabel.c_str())) {
      if (ImGui::Selectable("Janelas (teste)", firstMonitor < 0)) firstMonitor = -1;
      for (size_t i = 0; i < mons.size(); ++i) {
        std::string l = "A partir do monitor " + std::to_string(i + 1) + " (" + mons[i].name + ")";
        if (ImGui::Selectable(l.c_str(), firstMonitor == int(i))) firstMonitor = int(i);
      }
      ImGui::EndCombo();
    }
    ImGui::Checkbox("Escalar o mapping atual para o novo canvas", &scale);
    BlendLayout preview = computeBlendLayout(bp);
    ImGui::Text("Canvas: %d × %d  ·  sobreposição: %d × %d px  ·  %d saídas", preview.canvasW,
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
        std::snprintf(t, sizeof(t), "%zu", i + 1);
        dl->AddText({a.x + 4, a.y + 2}, IM_COL32(255, 255, 255, 230), t);
      }
      ImGui::Dummy({W, H + 4});
    }
    if (ImGui::Button("Aplicar")) {
      applyBlendLayout(p, bp, firstMonitor, scale);
      if (!p.outputs.empty()) app.sel.outputId = p.outputs.front().id;
      app.edited();
      app.status("Blend aplicado: " + std::to_string(p.outputs.size()) + " saídas");
    }
    ImGui::SameLine();
    ImGui::TextDisabled("Depois: padrão \"Grade de blend\" para alinhar");
  }
  if (ImGui::Button("Identificar saídas")) app.outputs.identify();
  ImGui::SetItemTooltip("Mostra o número de cada saída por alguns segundos");
  ImGui::SameLine();
  if (ImGui::Button("Grade em todas")) {
    bool any = std::any_of(p.outputs.begin(), p.outputs.end(), [](auto &x) { return x.pattern == TestPattern::BlendGrid; });
    for (auto &x : p.outputs) x.pattern = any ? TestPattern::None : TestPattern::BlendGrid;
    app.edited();
  }
  ImGui::SetItemTooltip("Liga/desliga a grade de alinhamento em todos os projetores");
  ImGui::Separator();
  for (auto &o : p.outputs) {
    ImGui::PushID(o.id);
    if (ImGui::Selectable(o.name.c_str(), app.sel.outputId == o.id)) app.sel.outputId = o.id;
    ImGui::PopID();
  }
  if (ImGui::Button("+ Saída")) {
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
  if (ImGui::Button("Remover saída")) {
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
  if (inputText("Nome", o->name)) app.edited();
  if (ImGui::Checkbox("Ativa", &o->enabled)) app.edited();
  int pat = int(o->pattern);
  ImGui::SetNextItemWidth(200);
  if (ImGui::Combo("Padrão de teste", &pat, "Nenhum (conteúdo)\0Test card da saída\0Grade de blend (contínua)\0Branco\0Preto\0")) {
    o->pattern = TestPattern(pat);
    app.edited();
  }
  helpMarker("Test card: foco e identificação. Grade de blend: desenhada no canvas, continua de um "
             "projetor para o outro (alinhe até as linhas coincidirem). Branco: ajuste as bordas "
             "até a emenda sumir. Preto: ajuste o nível de preto.");

  auto mons = OutputManager::monitors();
  std::string current = o->monitor < 0 ? "Janela (teste)" : "Monitor " + std::to_string(o->monitor + 1);
  if (ImGui::BeginCombo("Destino", current.c_str())) {
    if (ImGui::Selectable("Janela (teste)", o->monitor < 0)) { o->monitor = -1; app.edited(); }
    for (size_t i = 0; i < mons.size(); ++i) {
      char label[200];
      std::snprintf(label, sizeof(label), "Monitor %zu: %s (%dx%d)", i + 1, mons[i].name.c_str(), mons[i].w, mons[i].h);
      if (ImGui::Selectable(label, o->monitor == int(i))) { o->monitor = int(i); app.edited(); }
    }
    ImGui::EndCombo();
  }
  helpMarker("Em um monitor a saída ocupa a tela inteira, sem bordas. Desative o espelhamento "
             "de telas no Windows (Win+P → Estender).");
  if (o->monitor < 0) {
    int sz[2] = {o->winW, o->winH};
    if (ImGui::InputInt2("Tamanho da janela", sz)) {
      o->winW = std::max(64, sz[0]);
      o->winH = std::max(64, sz[1]);
      app.edited();
    }
  }

  ImGui::SeparatorText("Região do canvas");
  float src[4] = {o->srcX, o->srcY, o->srcW, o->srcH};
  if (ImGui::DragFloat4("x y larg alt", src, 1.0f)) {
    o->srcX = src[0]; o->srcY = src[1];
    o->srcW = std::max(1.0f, src[2]); o->srcH = std::max(1.0f, src[3]);
    app.edited();
  }
  if (ImGui::Button("Canvas inteiro")) {
    o->srcX = o->srcY = 0;
    o->srcW = float(p.canvasW);
    o->srcH = float(p.canvasH);
    app.edited();
  }
  ImGui::SameLine();
  if (ImGui::Button("Metade esq.")) {
    o->srcX = 0; o->srcY = 0; o->srcW = float(p.canvasW) * 0.5f; o->srcH = float(p.canvasH);
    app.edited();
  }
  ImGui::SameLine();
  if (ImGui::Button("Metade dir.")) {
    o->srcX = float(p.canvasW) * 0.5f; o->srcY = 0; o->srcW = float(p.canvasW) * 0.5f; o->srcH = float(p.canvasH);
    app.edited();
  }

  ImGui::SeparatorText("Warp da saída");
  bool viewing = workspace.viewOutputId == o->id;
  if (ImGui::Checkbox("Editar no workspace", &viewing)) workspace.viewOutputId = viewing ? o->id : -1;
  helpMarker("Mostra esta saída no workspace. Use Perspectiva (M) ou Malha (G) para corrigir "
             "a geometria do projetor inteiro (keystone / superfícies curvas).");
  int grid[2] = {o->warp.meshCols, o->warp.meshRows};
  if (ImGui::InputInt2("Malha (col × lin)", grid)) {
    o->warp.resetMesh(std::clamp(grid[0], 2, 33), std::clamp(grid[1], 2, 33));
    app.edited();
  }
  if (ImGui::Button("Zerar warp")) {
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
  ImGui::SeparatorText("Soft edge (blend entre projetores)");
  bool se = false;
  se |= ImGui::SliderFloat("Esquerda", &o->softEdge.left, 0, 0.5f);
  se |= ImGui::SliderFloat("Direita", &o->softEdge.right, 0, 0.5f);
  se |= ImGui::SliderFloat("Topo", &o->softEdge.top, 0, 0.5f);
  se |= ImGui::SliderFloat("Base", &o->softEdge.bottom, 0, 0.5f);
  se |= ImGui::SliderFloat("Gamma", &o->softEdge.gamma, 0.5f, 4);
  se |= ImGui::SliderFloat("Curva", &o->softEdge.power, 0.5f, 6);
  if (se) app.edited();
  helpMarker("Para dois projetores lado a lado com sobreposição: cada um mostra uma região do "
             "canvas que inclui a faixa sobreposta, e a borda é suavizada com a mesma largura. "
             "O Assistente de blend calcula tudo isso.");

}

void Ui::blackAndColorControls(App &app, OutputConfig *o) {
  ImGui::SeparatorText("Nível de preto");
  {
    float bl[3] = {o->blackLevel.r, o->blackLevel.g, o->blackLevel.b};
    static std::map<int, bool> perChannel; // UI state only
    bool linked = !perChannel[o->id] && bl[0] == bl[1] && bl[1] == bl[2];
    if (linked) {
      if (ImGui::SliderFloat("Compensação", &bl[0], 0, 0.1f, "%.4f", ImGuiSliderFlags_Logarithmic)) {
        o->blackLevel = {bl[0], bl[0], bl[0], 1};
        app.edited();
      }
    } else if (ImGui::SliderFloat3("Compensação RGB", bl, 0, 0.1f, "%.4f", ImGuiSliderFlags_Logarithmic)) {
      o->blackLevel = {bl[0], bl[1], bl[2], 1};
      app.edited();
    }
    if (ImGui::SmallButton(linked ? "Ajustar por cor" : "Unificar")) {
      perChannel[o->id] = linked;
      if (!linked) {
        o->blackLevel = {bl[0], bl[0], bl[0], 1};
        app.edited();
      }
    }
    helpMarker("Projetores não mostram preto puro: na sobreposição os dois \"pretos\" somam e aparece "
               "uma faixa cinza nas cenas escuras. Use o padrão Preto e aumente até o resto da imagem "
               "ficar igual à faixa. Valores típicos: 0.002 a 0.02.");
  }

  ImGui::SeparatorText("Cor do projetor");
  {
    bool c = false;
    c |= ImGui::SliderFloat("Brilho##out", &o->color.brightness, 0, 2);
    c |= ImGui::SliderFloat("Contraste##out", &o->color.contrast, 0, 2);
    c |= ImGui::SliderFloat("Gamma##out", &o->color.gamma, 0.3f, 3);
    float g[3] = {o->color.gain.r, o->color.gain.g, o->color.gain.b};
    if (ImGui::SliderFloat3("Ganho R G B", g, 0, 2)) {
      o->color.gain = {g[0], g[1], g[2], 1};
      c = true;
    }
    if (ImGui::SmallButton("Zerar cor")) {
      o->color = {};
      c = true;
    }
    if (c) app.edited();
    helpMarker("Iguale projetores diferentes (ou com lâmpadas de idades diferentes): mostre Branco em "
               "todos e reduza o ganho do canal que estiver sobrando.");
  }

  ImGui::SeparatorText("Sincronia");
  if (ImGui::SliderInt("Atraso (quadros)", &o->delayFrames, 0, 8)) app.edited();
  helpMarker("Projetores de modelos diferentes têm latências diferentes. Atrase os mais rápidos "
             "até a imagem andar junta na emenda (filme em câmera lenta com o celular para conferir).");
  if (ImGui::Checkbox("Mostrar controles no projetor (Ctrl+Shift+D)", &app.showOutputControls)) {}
  helpMarker("Desenha os cantos e a malha da saída selecionada na própria projeção, para ajustar "
             "olhando a parede.");

}

void Ui::outputMaskControls(App &app, OutputConfig *o, Workspace &ws) {
  ImGui::SeparatorText("Máscaras da saída");
  ImGui::TextWrapped("Recortam a imagem deste projetor (obstáculos, bordas irregulares, cúpulas). "
                     "Edite no workspace com a saída em vista e a ferramenta Máscara (K).");
  {
    int removeMask = -1;
    for (size_t i = 0; i < o->masks.size(); ++i) {
      ImGui::PushID(3000 + int(i));
      Mask &m = o->masks[i];
      if (ImGui::Checkbox("##en", &m.enabled)) app.edited();
      ImGui::SameLine();
      if (ImGui::Selectable(m.name.c_str(), app.sel.outMask == int(i))) app.sel.outMask = int(i);
      if (ImGui::BeginPopupContextItem()) {
        if (ImGui::MenuItem("Remover")) removeMask = int(i);
        ImGui::EndPopup();
      }
      ImGui::PopID();
    }
    if (removeMask >= 0) {
      o->masks.erase(o->masks.begin() + removeMask);
      app.sel.outMask = -1;
      app.edited();
    }
    if (ImGui::Button("+ Máscara de saída")) {
      Mask m;
      m.name = "Máscara " + std::to_string(o->masks.size() + 1);
      m.subtract = true;
      m.feather = 20;
      o->masks.push_back(m);
      app.sel.outMask = int(o->masks.size()) - 1;
      ws.viewOutputId = o->id;
      ws.tool = Tool::Mask;
      app.edited();
    }
    if (app.sel.outMask >= 0 && app.sel.outMask < int(o->masks.size())) {
      Mask &m = o->masks[size_t(app.sel.outMask)];
      int mode = m.subtract ? 1 : 0;
      if (ImGui::Combo("Modo##om", &mode, "Revelar dentro\0Esconder dentro\0")) { m.subtract = mode == 1; app.edited(); }
      if (ImGui::SliderFloat("Suavizar (px)##om", &m.feather, 0, 300)) app.edited();
      ImGui::Text("%zu pontos", m.points.size());
    }
  }
}

void Ui::interactionInspector(App &app) {
  Engine &e = app.engine;
  Project &p = e.project;
  ImGui::SeparatorText("Entrada OSC");
  bool changed = ImGui::Checkbox("Ativar OSC", &p.oscEnabled);
  ImGui::SetNextItemWidth(120);
  changed |= ImGui::InputInt("Porta UDP", &p.oscPort);
  if (changed) {
    p.oscPort = std::clamp(p.oscPort, 1, 65535);
    e.restartOsc();
    app.edited();
  }
  if (e.oscServer().running())
    ImGui::TextColored({0.4f, 0.9f, 0.5f, 1}, "Escutando na porta %d", e.oscServer().port());
  else if (!e.oscError().empty())
    ImGui::TextColored({1, 0.4f, 0.4f, 1}, "Erro: %s", e.oscError().c_str());
  else
    ImGui::TextDisabled("Desativado");

  ImGui::SeparatorText("Saída OSC (feedback e ações)");
  ImGui::TextWrapped("Destinos que recebem /lumen/status/column ao trocar de cue e as ações OSC das "
                     "colunas (botão direito no cabeçalho da coluna).");
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
    if (ImGui::Checkbox("Status", &t.feedback)) app.edited();
    ImGui::SameLine();
    if (ImGui::SmallButton("x")) removeTarget = int(i);
    ImGui::PopID();
  }
  if (removeTarget >= 0) {
    p.oscTargets.erase(p.oscTargets.begin() + removeTarget);
    app.edited();
  }
  if (ImGui::Button("+ Destino OSC")) {
    p.oscTargets.push_back({});
    app.edited();
  }
  if (e.oscSent() > 0) {
    ImGui::SameLine();
    ImGui::TextDisabled("%d mensagens enviadas", e.oscSent());
  }

  ImGui::SeparatorText("MIDI");
  if (!e.midi().available()) {
    ImGui::TextDisabled("Compilado sem suporte a MIDI");
  } else {
    if (ImGui::Checkbox("Ativar MIDI", &p.midiEnabled)) {
      e.restartMidi();
      app.edited();
    }
    auto ports = e.midi().ports();
    if (ports.empty()) ImGui::TextDisabled("Nenhum controlador conectado (procura a cada 3 s)");
    for (auto &port : ports) ImGui::BulletText("%s", port.c_str());
    ImGui::TextDisabled("MIDI chega como /midi/cc/<canal>/<n>, /midi/note/<canal>/<nota>: use \"Aprender\".");
  }

  ImGui::SeparatorText("Teclado");
  if (ImGui::Checkbox("Teclas como entrada (/key/<tecla>)", &p.keyboardInput)) app.edited();
  helpMarker("Cada tecla vira /key/a, /key/1, /key/f1, /key/left... com valor 1 ao apertar e 0 ao "
             "soltar, também com as janelas de saída em foco. Use num mapeamento: ex. a tecla A "
             "dispara a coluna 3. Espaço, Esc e B continuam com as funções do programa.");

  ImGui::SeparatorText("DMX de entrada (mesa de luz)");
  {
    DmxInputConfig &d = p.dmxInput;
    bool ch = ImGui::Checkbox("Receber DMX", &d.enabled);
    int proto = int(d.protocol);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(110);
    ch |= ImGui::Combo("##dmxproto", &proto, "Art-Net\0sACN\0");
    d.protocol = DmxProtocol(proto);
    ImGui::SetNextItemWidth(90);
    ch |= ImGui::InputInt("Universo inicial", &d.universe);
    ImGui::SetNextItemWidth(90);
    ch |= ImGui::InputInt("Quantidade", &d.count);
    d.universe = std::clamp(d.universe, 0, 32767);
    d.count = std::clamp(d.count, 1, 64);
    if (ch) { e.restartDmxInput(); app.edited(); }
    if (e.dmxInput().running())
      ImGui::TextColored({0.4f, 0.9f, 0.5f, 1}, "Recebendo (%d pacotes). Canais chegam como %s/<universo>/<canal> (0-255).",
                         e.dmxInput().packets(), d.protocol == DmxProtocol::ArtNet ? "/artnet" : "/sacn");
    else if (!e.dmxInputError().empty())
      ImGui::TextColored({1, 0.4f, 0.4f, 1}, "Erro: %s", e.dmxInputError().c_str());
    helpMarker("Uma mesa de luz (grandMA, Chamsys, Hog...) controla o Lumen: ligue um canal à "
               "opacidade de uma layer, ou dispare colunas com um canal (valor acima de 127).");
  }

  ImGui::SeparatorText("Mapeamentos (sensor → parâmetro)");
  ImGui::TextWrapped("Ligue qualquer mensagem OSC (TouchOSC, TouchDesigner, Arduino, sensores...) "
                     "a um parâmetro. Clique em \"Aprender\" e envie a mensagem.");
  int remove = -1;
  for (size_t i = 0; i < p.mappings.size(); ++i) {
    InputMapping &m = p.mappings[i];
    ImGui::PushID(int(i));
    char header[256];
    std::snprintf(header, sizeof(header), "%s → %s###map", m.address.c_str(), mappingTargetName(m.target));
    if (ImGui::CollapsingHeader(header, ImGuiTreeNodeFlags_DefaultOpen)) {
      if (ImGui::Checkbox("Ativo", &m.enabled)) app.edited();
      ImGui::SameLine();
      bool learning = e.learnMapping == int(i);
      if (learning) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.9f, 0.3f, 0.3f, 1));
      if (ImGui::Button(learning ? "Aguardando mensagem..." : "Aprender"))
        e.learnMapping = learning ? -1 : int(i);
      if (learning) ImGui::PopStyleColor();
      ImGui::SameLine();
      if (ImGui::Button("Remover")) remove = int(i);
      if (inputText("Endereço", m.address)) app.edited();
      helpMarker("Aceita curingas OSC: /sensor/* , /pad/[1-4]");
      if (ImGui::InputInt("Argumento #", &m.argIndex)) { m.argIndex = std::max(0, m.argIndex); app.edited(); }
      if (ImGui::Button("Calibrar")) { calibrating_ = int(i); calMin_ = 1e30f; calMax_ = -1e30f; }
      helpMarker("Mexa o sensor ou fader até os extremos: a faixa de entrada é ajustada sozinha. "
                 "Clique de novo para terminar.");
      if (calibrating_ == int(i)) {
        for (auto &msg : e.frameInputs())
          if (oscMatch(m.address.c_str(), msg.address.c_str()) && size_t(m.argIndex) < msg.args.size()) {
            float v = msg.number(size_t(m.argIndex));
            calMin_ = std::min(calMin_, v);
            calMax_ = std::max(calMax_, v);
          }
        ImGui::SameLine();
        if (calMax_ >= calMin_) ImGui::Text("%.2f .. %.2f", calMin_, calMax_);
        else ImGui::TextDisabled("aguardando valores...");
        ImGui::SameLine();
        if (ImGui::Button("Concluir")) {
          if (calMax_ > calMin_) { m.inMin = calMin_; m.inMax = calMax_; app.edited(); }
          calibrating_ = -1;
        }
      }
      float in[2] = {m.inMin, m.inMax};
      if (ImGui::DragFloat2("Entrada min/max", in, 0.01f)) { m.inMin = in[0]; m.inMax = in[1]; app.edited(); }
      int t = int(m.target);
      std::string items;
      for (int k = 0; k < int(MappingTarget::COUNT); ++k) {
        items += mappingTargetName(MappingTarget(k));
        items.push_back('\0');
      }
      if (ImGui::Combo("Alvo", &t, items.c_str())) { m.target = MappingTarget(t); app.edited(); }
      if (!mappingTargetIsTrigger(m.target)) {
        int tr = int(m.transformer);
        if (ImGui::Combo("Transformação", &tr, "Linear (segue a entrada)\0Chave (alto = máx, baixo = mín)\0"
                                                  "Alternar (cada toque troca mín/máx)\0Acumular (cada toque soma o passo)\0")) {
          m.transformer = Transformer(tr);
          app.edited();
        }
        if (m.transformer == Transformer::Accumulator && ImGui::DragFloat("Passo", &m.step, 0.01f)) app.edited();
      }
      int cols[2] = {m.firstColumn, m.lastColumn};
      if (ImGui::InputInt2("Só nas colunas (de/até)", cols)) {
        m.firstColumn = std::max(0, cols[0]);
        m.lastColumn = std::max(0, cols[1]);
        app.edited();
      }
      helpMarker("0 0 = sempre ativo. Ex.: 3 7 = o sensor só age enquanto uma coluna de 3 a 7 "
                 "estiver no ar.");
      if (mappingTargetNeedsLayer(m.target)) {
        Layer *l = p.layer(m.layerId);
        if (ImGui::BeginCombo("Layer", l ? l->name.c_str() : "(escolha)")) {
          for (auto &ly : p.layers)
            if (ImGui::Selectable(ly.name.c_str(), ly.id == m.layerId)) { m.layerId = ly.id; app.edited(); }
          ImGui::EndCombo();
        }
        float out[2] = {m.outMin, m.outMax};
        if (ImGui::DragFloat2("Saída min/max", out, 0.01f)) { m.outMin = out[0]; m.outMax = out[1]; app.edited(); }
        if (ImGui::SliderFloat("Suavização", &m.smoothing, 0, 0.99f)) app.edited();
      } else if (!mappingTargetIsTrigger(m.target)) {
        float out[2] = {m.outMin, m.outMax};
        if (ImGui::DragFloat2("Saída min/max", out, 0.01f)) { m.outMin = out[0]; m.outMax = out[1]; app.edited(); }
        if (ImGui::SliderFloat("Suavização", &m.smoothing, 0, 0.99f)) app.edited();
      } else if (m.target == MappingTarget::LaunchColumn) {
        int colN = int(m.outMin);
        if (ImGui::InputInt("Coluna (1..n)", &colN)) { m.outMin = float(std::max(1, colN)); app.edited(); }
        ImGui::TextDisabled("Dispara quando o valor passa da metade da faixa.");
      } else {
        ImGui::TextDisabled("Dispara quando o valor passa da metade da faixa.");
      }
    }
    ImGui::PopID();
  }
  if (remove >= 0) {
    p.mappings.erase(p.mappings.begin() + remove);
    if (e.learnMapping == remove) e.learnMapping = -1;
    app.edited();
  }
  if (ImGui::Button("+ Mapeamento")) {
    InputMapping m;
    m.layerId = app.sel.layerId >= 0 ? app.sel.layerId : (p.layers.empty() ? -1 : p.layers.front().id);
    p.mappings.push_back(m);
    app.edited();
  }

  ImGui::SeparatorText("Agenda (instalações)");
  {
    ImGui::TextWrapped("Ações em horários fixos, ex.: liga a coluna 1 às 9:00 e faz blackout às 22:00.");
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
      if (ImGui::Combo("##act", &act, "Disparar coluna\0GO (próxima)\0Parar tudo\0Blackout liga\0Blackout desliga\0")) {
        se.action = ScheduleAction(act);
        app.edited();
      }
      if (se.action == ScheduleAction::LaunchColumn) {
        ImGui::SameLine();
        ImGui::SetNextItemWidth(70);
        if (ImGui::InputInt("##col", &se.column)) { se.column = std::max(1, se.column); app.edited(); }
      }
      ImGui::SameLine();
      if (ImGui::SmallButton("x")) removeEntry = int(i);
      static const char *dn[] = {"D", "S", "T", "Q", "Q", "S", "S"};
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
    if (ImGui::Button("+ Horário")) {
      p.schedule.push_back({});
      app.edited();
    }
  }

  ImGui::SeparatorText("Monitor OSC");
  if (ImGui::SmallButton("Limpar")) e.oscLog.clear();
  ImGui::BeginChild("##osclog", {0, 180}, ImGuiChildFlags_Borders);
  for (auto &line : e.oscLog) ImGui::TextUnformatted(line.c_str());
  if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 4) ImGui::SetScrollHereY(1.0f);
  ImGui::EndChild();
  ImGui::TextDisabled("Linhas com \"?\" não correspondem a comandos /lumen/...");
}

void Ui::ledInspector(App &app) {
  Engine &e = app.engine;
  Project &p = e.project;

  ImGui::SeparatorText("Controladores (Art-Net / sACN)");
  for (auto &d : p.dmxDevices) {
    ImGui::PushID(d.id);
    if (ImGui::Checkbox("##en", &d.enabled)) app.edited();
    ImGui::SameLine();
    char label[200];
    std::snprintf(label, sizeof(label), "%s  (%s %s)", d.name.c_str(),
                  d.protocol == DmxProtocol::ArtNet ? "Art-Net" : "sACN",
                  d.host.empty() ? (d.protocol == DmxProtocol::ArtNet ? "broadcast" : "multicast") : d.host.c_str());
    if (ImGui::Selectable(label, app.sel.dmxDeviceId == d.id)) app.sel.dmxDeviceId = d.id;
    ImGui::PopID();
  }
  if (ImGui::Button("+ Art-Net")) {
    app.sel.dmxDeviceId = p.addDmxDevice(DmxProtocol::ArtNet).id;
    app.edited();
  }
  ImGui::SameLine();
  if (ImGui::Button("+ sACN")) {
    app.sel.dmxDeviceId = p.addDmxDevice(DmxProtocol::SACN).id;
    app.edited();
  }
  if (DmxDevice *d = p.dmxDevice(app.sel.dmxDeviceId)) {
    ImGui::SameLine();
    if (ImGui::Button("Remover controlador")) {
      int id = d->id;
      p.dmxDevices.erase(std::remove_if(p.dmxDevices.begin(), p.dmxDevices.end(), [&](auto &x) { return x.id == id; }),
                         p.dmxDevices.end());
      app.sel.dmxDeviceId = -1;
      app.edited();
    } else {
      ImGui::PushID(d->id);
      if (inputText("Nome##dev", d->name)) app.edited();
      int proto = int(d->protocol);
      if (ImGui::Combo("Protocolo", &proto, "Art-Net\0sACN (E1.31)\0")) { d->protocol = DmxProtocol(proto); app.edited(); }
      if (inputText("IP do controlador", d->host)) app.edited();
      helpMarker("Vazio = broadcast (Art-Net) ou multicast (sACN). Prefira o IP do controlador "
                 "(unicast): menos tráfego na rede. Ex.: 2.0.0.10 ou 192.168.1.50");
      if (ImGui::SliderInt("Taxa (pacotes/s)", &d->rate, 1, 60)) app.edited();
      ImGui::PopID();
    }
  }

  ImGui::SeparatorText("Fitas / matrizes de LED");
  int removeId = -1;
  for (auto &m : p.ledMappings) {
    ImGui::PushID(m.id);
    if (ImGui::Checkbox("##en", &m.enabled)) app.edited();
    ImGui::SetItemTooltip("Desligada não envia nada");
    ImGui::SameLine();
    if (ImGui::Checkbox("##lock", &m.locked)) app.edited();
    ImGui::SetItemTooltip("Travada: continua enviando, mas não dá para mover no workspace");
    ImGui::SameLine();
    char label[200];
    std::snprintf(label, sizeof(label), "%s  (%d×%d %s)", m.name.c_str(), m.cols, m.rows, ledFormatName(m.format));
    if (ImGui::Selectable(label, app.sel.ledId == m.id)) {
      app.sel.ledId = m.id;
      workspace.tool = Tool::Led;
    }
    if (ImGui::BeginPopupContextItem()) {
      if (ImGui::MenuItem("Remover")) removeId = m.id;
      ImGui::EndPopup();
    }
    ImGui::PopID();
  }
  if (removeId >= 0) {
    p.ledMappings.erase(std::remove_if(p.ledMappings.begin(), p.ledMappings.end(), [&](auto &x) { return x.id == removeId; }),
                        p.ledMappings.end());
    app.edited();
  }
  if (ImGui::Button("+ Fita de LED")) {
    if (p.dmxDevices.empty()) p.addDmxDevice(DmxProtocol::ArtNet);
    app.sel.ledId = p.addLedMapping().id;
    workspace.tool = Tool::Led;
    app.edited();
  }
  ImGui::SameLine();
  ImGui::TextDisabled("Ferramenta LED (L) para posicionar");

  if (LedMapping *m = p.ledMapping(app.sel.ledId)) {
    ImGui::PushID(m->id);
    ImGui::SeparatorText(m->name.c_str());
    if (inputText("Nome", m->name)) app.edited();
    DmxDevice *dev = p.dmxDevice(m->deviceId);
    if (ImGui::BeginCombo("Controlador", dev ? dev->name.c_str() : "(nenhum)")) {
      for (auto &d : p.dmxDevices)
        if (ImGui::Selectable(d.name.c_str(), d.id == m->deviceId)) { m->deviceId = d.id; app.edited(); }
      ImGui::EndCombo();
    }
    int size[2] = {m->cols, m->rows};
    if (ImGui::InputInt2("LEDs (col × lin)", size)) {
      m->cols = std::clamp(size[0], 1, 4096);
      m->rows = std::clamp(size[1], 1, 4096);
      app.edited();
    }
    helpMarker("Quantidade de LEDs. Fita de 60 LEDs: 60 × 1. Matriz 16×16: 16 × 16.");
    int fmt = int(m->format);
    if (ImGui::Combo("Formato", &fmt, "RGB\0BGR\0GRB (WS2812)\0RGBW\0Mono\0")) { m->format = LedFormat(fmt); app.edited(); }
    helpMarker("Se o vermelho sair azul, use BGR. Fitas WS2812/SK6812 costumam ser GRB.");
    ImGui::SameLine();
    if (ImGui::Checkbox("16 bits", &m->wide)) app.edited();
    if (ImGui::Checkbox("Zigzag", &m->zigzag)) app.edited();
    ImGui::SetItemTooltip("As linhas vão e voltam (matriz feita com uma fita dobrada)");
    ImGui::SameLine();
    if (ImGui::Checkbox("Vertical", &m->vertical)) app.edited();
    ImGui::SetItemTooltip("A fiação segue as colunas em vez das linhas");
    ImGui::SameLine();
    if (ImGui::Checkbox("Inverter X", &m->flipX)) app.edited();
    ImGui::SameLine();
    if (ImGui::Checkbox("Inverter Y", &m->flipY)) app.edited();

    int uni = m->universe, ch = m->channel;
    if (ImGui::InputInt("Universo", &uni)) { m->universe = std::clamp(uni, 0, 32767); app.edited(); }
    helpMarker("Art-Net: o primeiro universo é 0. sACN: o primeiro é 1. Erro clássico quando a fita não acende.");
    if (ImGui::InputInt("Canal inicial", &ch)) { m->channel = std::clamp(ch, 1, 512); app.edited(); }
    if (ImGui::SliderFloat("Curva (gamma)", &m->gamma, 0.5f, 4.0f, "%.2f")) app.edited();
    helpMarker("LEDs não são lineares: 2.2 deixa os tons médios mais naturais. 1 = sem correção.");
    if (ImGui::SliderFloat("Brilho", &m->brightness, 0, 1)) app.edited();

    // Where the strip lands in DMX.
    auto slots = computeLedLayout(m->layout());
    int cpp = ledChannelsPerPixel(m->format, m->wide);
    if (!slots.empty())
      ImGui::TextWrapped("%zu LEDs · %d canais por LED · universo %d canal %d → universo %d canal %d",
                         slots.size(), cpp, slots.front().universe, slots.front().channel + 1,
                         slots.back().universe, slots.back().channel + cpp);
    if (dev && dev->protocol == DmxProtocol::SACN && m->universe == 0)
      ImGui::TextColored({1, 0.6f, 0.3f, 1}, "sACN não tem universo 0: use 1.");

    if (ImGui::Button("Ocupar o canvas")) {
      m->area.setRect({0, 0}, {float(p.canvasW), float(p.canvasH)});
      app.edited();
    }
    ImGui::SameLine();
    if (ImGui::Button("Faixa horizontal")) {
      m->area.setRect({float(p.canvasW) * 0.05f, float(p.canvasH) * 0.47f}, {float(p.canvasW) * 0.9f, float(p.canvasH) * 0.06f});
      app.edited();
    }
    ImGui::PopID();
  }

  ImGui::SeparatorText("Geral");
  if (ImGui::SliderFloat("DMX master", &p.dmxMaster, 0, 1)) app.edited();
  ImGui::SetItemTooltip("Intensidade de todos os LEDs (0 = blackout das luzes)");
  for (auto &c : e.leds.conflicts()) ImGui::TextColored({1, 0.5f, 0.4f, 1}, "Conflito: %s", c.c_str());
  ImGui::TextDisabled("%d pacotes enviados", e.leds.packetsSent());

  ImGui::SeparatorText("Monitor DMX");
  const auto &unis = e.leds.universes();
  if (unis.empty()) {
    ImGui::TextDisabled("Nada sendo enviado");
    return;
  }
  static UniverseKey shownKey{-1, -1};
  if (!unis.count(shownKey)) shownKey = unis.begin()->first;
  auto keyName = [&](const UniverseKey &k) {
    const DmxDevice *d = p.dmxDevice(k.deviceId);
    return (d ? d->name : std::string("?")) + " · universo " + std::to_string(k.universe);
  };
  if (ImGui::BeginCombo("Universo##mon", keyName(shownKey).c_str())) {
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
    if (idx >= 0 && idx < 512) ImGui::SetTooltip("Canal %d = %d", idx + 1, data[size_t(idx)]);
  }
  ImGui::TextDisabled("Cada quadrado é um canal (1–512); passe o mouse para ver o valor.");
}

void Ui::projectInspector(App &app) {
  Project &p = app.engine.project;
  ImGui::SeparatorText("Canvas");
  int size[2] = {p.canvasW, p.canvasH};
  if (ImGui::InputInt2("Resolução", size, ImGuiInputTextFlags_EnterReturnsTrue)) {
    p.canvasW = std::clamp(size[0], 16, 16384);
    p.canvasH = std::clamp(size[1], 16, 16384);
    app.edited();
  }
  helpMarker("Espaço de trabalho onde as layers são compostas. As saídas recortam regiões dele. "
             "Pressione Enter para aplicar.");
  const char *presets[] = {"1280×720", "1920×1080", "2560×1440", "3840×1080 (2 proj.)", "3840×2160"};
  const int pw[] = {1280, 1920, 2560, 3840, 3840}, ph[] = {720, 1080, 1440, 1080, 2160};
  for (int i = 0; i < 5; ++i) {
    if (i) ImGui::SameLine();
    if (ImGui::SmallButton(presets[i])) {
      p.canvasW = pw[i];
      p.canvasH = ph[i];
      app.edited();
    }
  }
  if (colorEdit("Fundo", p.background)) app.edited();
  ImGui::SeparatorText("Show");
  if (ImGui::DragFloat("Fade padrão (s)", &p.defaultTransition, 0.05f, 0, 60)) app.edited();

  ImGui::SeparatorText("Ao abrir o projeto (instalação)");
  if (ImGui::Checkbox("Abrir as saídas", &p.startOutputs)) app.edited();
  ImGui::SetNextItemWidth(100);
  if (ImGui::InputInt("Disparar a coluna (0 = nenhuma)", &p.startColumn)) {
    p.startColumn = std::max(0, p.startColumn);
    app.edited();
  }
  helpMarker("Para uma instalação que liga sozinha: coloque um atalho do Lumen com o projeto "
             "(Lumen.exe show.lumen) na pasta Inicializar do Windows (Win+R, shell:startup).");

  ImGui::SeparatorText("Áudio");
  Engine &e = app.engine;
  if (ImGui::BeginCombo("Saída de áudio", p.audioDevice.empty() ? "Padrão do sistema" : p.audioDevice.c_str())) {
    if (ImGui::Selectable("Padrão do sistema", p.audioDevice.empty())) { p.audioDevice.clear(); app.edited(); }
    for (auto &d : AudioEngine::devices())
      if (ImGui::Selectable(d.c_str(), d == p.audioDevice)) { p.audioDevice = d; app.edited(); }
    ImGui::EndCombo();
  }
  ImGui::TextDisabled("Em uso: %s", e.audio.deviceName().c_str());
  if (!e.audio.error().empty()) ImGui::TextColored({1, 0.5f, 0.4f, 1}, "%s", e.audio.error().c_str());
  if (ImGui::SliderFloat("Volume master", &p.masterVolume, 0, 2, "%.2f")) app.edited();
  ImGui::ProgressBar(std::min(1.0f, e.audio.peak(0)), {-1, 5}, "");
  ImGui::ProgressBar(std::min(1.0f, e.audio.peak(1)), {-1, 5}, "");

  ImGui::SeparatorText("Vídeo");
  if (ImGui::Checkbox("Decodificação por hardware", &p.hardwareDecode)) app.edited();
  helpMarker("Usa a GPU (D3D11VA/DXVA2 no Windows) para H.264/HEVC/VP9/AV1. Vale para mídias "
             "disparadas depois da mudança. HAP sempre vai direto para a GPU.");
  ImGui::TextDisabled("HAP comprimido na GPU: %s", FrameUploader::supportsS3tc() ? "sim" : "não suportado");

  ImGui::SeparatorText("Spout (enviar o canvas)");
  if (!spoutAvailable()) {
    ImGui::TextDisabled("Disponível só no Windows");
  } else {
    if (ImGui::Checkbox("Enviar canvas por Spout", &p.spoutSend)) app.edited();
    if (inputText("Nome do sender", p.spoutName)) app.edited();
    if (!e.spoutStatus().empty()) ImGui::TextDisabled("%s", e.spoutStatus().c_str());
  }
  ImGui::SeparatorText("Arquivo");
  ImGui::TextWrapped("%s", p.filePath.empty() ? "(projeto não salvo)" : p.filePath.c_str());
  ImGui::Text("%zu mídias · %zu layers · %zu colunas · %zu saídas", p.library.size(),
              p.layers.size(), p.columns.size(), p.outputs.size());
}

void Ui::statusBar(App &app) {
  ImGui::Text("%.0f fps", app.fps());
  ImGui::SameLine(0, 20);
  const Project &p = app.engine.project;
  ImGui::TextDisabled("Canvas %d×%d", p.canvasW, p.canvasH);
  ImGui::SameLine(0, 20);
  ImGui::TextDisabled("Saídas abertas: %d (%.1f ms)", app.outputs.openCount(), app.outputs.renderMs());
  ImGui::SameLine(0, 20);
  if (app.engine.oscServer().running()) ImGui::TextDisabled("OSC :%d", app.engine.oscServer().port());
  else ImGui::TextDisabled("OSC desligado");
  if (!app.statusText().empty() && app.statusAge() < 6) {
    ImGui::SameLine(0, 30);
    if (app.statusIsError()) ImGui::TextColored({1, 0.45f, 0.45f, 1}, "%s", app.statusText().c_str());
    else ImGui::TextColored(kAccent, "%s", app.statusText().c_str());
  }
}

} // namespace lumen
