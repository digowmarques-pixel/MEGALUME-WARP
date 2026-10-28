#include "app.h"

#include "gl/gl.h"
#include "platform/platform.h"
#include "ui/ui.h"

#include <GLFW/glfw3.h>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>

#include <cstdio>
#include <fstream>
#include <algorithm>
#include <filesystem>

namespace lumen {

namespace {

std::vector<std::string> g_droppedFiles;
bool g_closeRequested = false;

void onDrop(GLFWwindow *, int count, const char **paths) {
  for (int i = 0; i < count; ++i) g_droppedFiles.emplace_back(paths[i]);
}

void onClose(GLFWwindow *w) {
  g_closeRequested = true;
  glfwSetWindowShouldClose(w, GLFW_FALSE); // the app decides (unsaved changes)
}

void onGlfwError(int code, const char *desc) {
  std::fprintf(stderr, "[glfw] error %d: %s\n", code, desc);
}

const char *kProjectFilter = "Projeto Lumen|*.lumen|Todos os arquivos|*.*";
const char *kMediaFilter =
    "Vídeos e imagens|*.mp4;*.mov;*.m4v;*.mkv;*.avi;*.webm;*.wmv;*.mpg;*.gif;*.png;*.jpg;*.jpeg;*.bmp;*.tif;*.tiff;*.webp|"
    "Todos os arquivos|*.*";

} // namespace

// ------------------------------------------------------------------ history

void History::reset(const std::string &s) {
  stack_ = {s};
  index_ = 0;
}

bool History::push(const std::string &s) {
  if (index_ >= 0 && stack_[size_t(index_)] == s) return false;
  stack_.resize(size_t(index_ + 1));
  stack_.push_back(s);
  if (stack_.size() > 200) stack_.erase(stack_.begin());
  index_ = int(stack_.size()) - 1;
  return true;
}

// ------------------------------------------------------------------ app

App::App() : ui_(std::make_unique<Ui>()) {}
App::~App() = default;

void App::status(const std::string &msg, bool error) {
  status_ = msg;
  statusError_ = error;
  statusTime_ = glfwGetTime();
  std::fprintf(error ? stderr : stdout, "%s\n", msg.c_str());
}

double App::statusAge() const { return glfwGetTime() - statusTime_; }

static Project starterProject() {
  Project p = Project::makeDefault();
  int cardId = p.addGenerator(GeneratorKind::TestCard).id;
  p.addGenerator(GeneratorKind::Grid);
  p.addGenerator(GeneratorKind::Plasma);
  Cell c;
  c.mediaId = cardId;
  p.columns[0].cells[p.layers[0].id] = c;
  return p;
}

void App::newProject() {
  engine.setProject(starterProject(), false);
  sel = {};
  history_.reset(serializeProject(engine.project, ""));
  dirty = false;
  status("Novo projeto");
}

bool App::openProject(const std::string &path) {
  Project p;
  std::string err;
  if (!loadProject(path, p, &err)) {
    status("Erro ao abrir: " + err, true);
    return false;
  }
  engine.setProject(std::move(p), false);
  sel = {};
  if (!engine.project.layers.empty()) sel.layerId = engine.project.layers.front().id;
  ui_->workspace.viewOutputId = -1;
  ui_->workspace.fitRequested = true;
  history_.reset(serializeProject(engine.project, ""));
  dirty = false;
  status("Projeto aberto: " + path);
  return true;
}

bool App::saveProject(bool saveAs) {
  std::string path = engine.project.filePath;
  if (saveAs || path.empty()) {
    path = platform::saveFileDialog("Salvar projeto", kProjectFilter, "show.lumen");
    if (path.empty()) {
      if (!platform::hasNativeDialogs()) status("Sem diálogo de arquivos neste sistema", true);
      return false;
    }
    if (platform::toPath(path).extension().empty()) path += ".lumen";
  }
  std::string err;
  if (!lumen::saveProject(engine.project, path, &err)) {
    status("Erro ao salvar: " + err, true);
    return false;
  }
  dirty = false;
  status("Salvo: " + path);
  return true;
}

void App::openDialog() {
  auto files = platform::openFileDialog("Abrir projeto", kProjectFilter, false);
  if (!files.empty()) openProject(files[0]);
}

void App::importDialog() {
  auto files = platform::openFileDialog("Importar mídia", kMediaFilter, true);
  if (!files.empty()) importFiles(files);
  else if (!platform::hasNativeDialogs()) status("Arraste os arquivos para a janela para importar", true);
}

void App::importFiles(const std::vector<std::string> &paths, int column, int layerId) {
  int count = 0;
  for (auto &path : paths) {
    std::error_code ec;
    if (std::filesystem::is_directory(platform::toPath(path), ec)) {
      std::vector<std::string> inner;
      for (auto &entry : std::filesystem::directory_iterator(platform::toPath(path), ec))
        if (entry.is_regular_file() && isMediaPath(platform::toUtf8(entry.path())))
          inner.push_back(platform::toUtf8(entry.path()));
      std::sort(inner.begin(), inner.end());
      importFiles(inner, column, layerId);
      continue;
    }
    if (!isMediaPath(path)) {
      status("Formato não suportado: " + path, true);
      continue;
    }
    MediaItem &m = engine.project.addMedia(path);
    sel.mediaId = m.id;
    ++count;
    if (column >= 0 && column < int(engine.project.columns.size()) && layerId >= 0) {
      Cell c;
      c.mediaId = m.id;
      engine.project.columns[size_t(column)].cells[layerId] = c;
    }
  }
  if (count) {
    sel.inspector = Inspector::Media;
    edited();
    status(std::to_string(count) + " mídia(s) importada(s)");
  }
}

void App::undo() {
  if (!history_.canUndo()) return;
  Project p;
  std::string path = engine.project.filePath;
  if (deserializeProject(history_.undo(), "", p)) {
    p.filePath = path;
    engine.setProject(std::move(p), true);
    dirty = true;
  }
}

void App::redo() {
  if (!history_.canRedo()) return;
  Project p;
  std::string path = engine.project.filePath;
  if (deserializeProject(history_.redo(), "", p)) {
    p.filePath = path;
    engine.setProject(std::move(p), true);
    dirty = true;
  }
}

void App::commitIfNeeded() {
  // Wait until the user lets go of sliders / handles so a drag is one undo step.
  if (!pendingCommit_ || ImGui::IsAnyItemActive() || ImGui::IsMouseDown(ImGuiMouseButton_Left)) return;
  pendingCommit_ = false;
  history_.push(serializeProject(engine.project, ""));
  // Keep runtime state that depends on counts in sync.
  if (engine.learnMapping >= int(engine.project.mappings.size())) engine.learnMapping = -1;
}

void App::updateTitle() {
  std::string name = engine.project.filePath.empty()
                         ? "Sem título"
                         : platform::toUtf8(platform::toPath(engine.project.filePath).filename());
  std::string title = "Lumen — " + name + (dirty ? " *" : "");
  if (title != lastTitle_) {
    glfwSetWindowTitle(window, title.c_str());
    lastTitle_ = title;
  }
}

bool App::writePpm(const std::string &path, int w, int h, const std::vector<unsigned char> &rgba,
                   bool flipY) {
  std::ofstream f(platform::toPath(path), std::ios::binary);
  if (!f) return false;
  f << "P6\n" << w << " " << h << "\n255\n";
  for (int y = 0; y < h; ++y) {
    int row = flipY ? h - 1 - y : y;
    for (int x = 0; x < w; ++x) f.write(reinterpret_cast<const char *>(&rgba[size_t((row * w + x) * 4)]), 3);
  }
  return bool(f);
}

int App::run(const Options &opts) {
  std::printf("Lumen %s iniciando\n", LUMEN_VERSION);
  glfwSetErrorCallback(onGlfwError);
  if (!glfwInit()) {
    platform::fatalError("Não foi possível iniciar a janela (GLFW).");
    return 1;
  }
  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
  glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
  glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
  glfwWindowHint(GLFW_MAXIMIZED, opts.frames > 0 ? GLFW_FALSE : GLFW_TRUE);
  window = glfwCreateWindow(opts.width, opts.height, "Lumen", nullptr, nullptr);
  if (!window) {
    const char *glfwDesc = nullptr;
    glfwGetError(&glfwDesc);
    platform::fatalError(std::string("Esta placa de vídeo não ofereceu OpenGL 3.3.\n\n") +
                         (glfwDesc ? std::string("Detalhe: ") + glfwDesc + "\n\n" : std::string()) +
                         "Atualize o driver da placa de vídeo (NVIDIA, AMD ou Intel). Se estiver usando "
                         "Área de Trabalho Remota ou máquina virtual, rode direto no computador.");
    glfwTerminate();
    return 1;
  }
  glfwMakeContextCurrent(window);
  glfwSwapInterval(1);
  if (!gl::load()) {
    platform::fatalError("O driver de vídeo não tem todas as funções OpenGL 3.3 necessárias.");
    return 1;
  }
  std::printf("[gl] %s | %s\n", reinterpret_cast<const char *>(glGetString(GL_RENDERER)),
              reinterpret_cast<const char *>(glGetString(GL_VERSION)));
  glfwSetDropCallback(window, onDrop);
  glfwSetWindowCloseCallback(window, onClose);

  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ui_->setup();
  ImGui_ImplGlfw_InitForOpenGL(window, true);
  ImGui_ImplOpenGL3_Init("#version 330");

  std::printf("[init] interface pronta\n");
  if (!engine.init() || !outputs.init(window)) {
    platform::fatalError("Falha ao compilar os shaders do renderizador (driver de vídeo).");
    return 1;
  }

  if (!opts.projectPath.empty()) {
    if (!openProject(opts.projectPath)) newProject();
  } else {
    newProject();
  }
  if (!engine.project.layers.empty() && sel.layerId < 0) sel.layerId = engine.project.layers.front().id;
  if (opts.launchColumn > 0) engine.launchColumn(opts.launchColumn - 1);

  double last = glfwGetTime(), fpsTimer = last;
  int frame = 0, fpsFrames = 0;
  bool showCloseDialog = false;

  while (!requestQuit) {
    glfwPollEvents();
    double now = glfwGetTime();
    double dt = std::clamp(now - last, 0.0, 0.1);
    if (opts.frames > 0) dt = 1.0 / 60.0; // deterministic for automated runs
    last = now;
    ++fpsFrames;
    if (now - fpsTimer >= 0.5) {
      fps_ = float(fpsFrames / (now - fpsTimer));
      fpsFrames = 0;
      fpsTimer = now;
    }

    if (!g_droppedFiles.empty()) {
      auto files = std::move(g_droppedFiles);
      g_droppedFiles.clear();
      if (files.size() == 1 && platform::toPath(files[0]).extension() == ".lumen") openProject(files[0]);
      else importFiles(files);
    }

    // Engine: playback, OSC, then compose the canvas.
    // The first frames log each stage: if something crashes at startup, the
    // last line of lumen.log says where.
    auto step = [&](const char *what) {
      if (frame < 3) std::printf("[frame %d] %s\n", frame, what);
    };
    step("update");
    engine.update(dt);
    step("render");
    engine.render();
    step("ui");

    // UI.
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    ImGuiIO &io = ImGui::GetIO();
    if (!io.WantTextInput) {
      bool ctrl = io.KeyCtrl, shift = io.KeyShift;
      if (!ctrl && ImGui::IsKeyPressed(ImGuiKey_Space, false)) shift ? engine.back() : engine.go();
      if (!ctrl && ImGui::IsKeyPressed(ImGuiKey_Escape, false)) engine.stopAll();
      if (!ctrl && ImGui::IsKeyPressed(ImGuiKey_B, false)) engine.blackout = !engine.blackout;
      if (ctrl && ImGui::IsKeyPressed(ImGuiKey_S, false)) saveProject(shift);
      if (ctrl && ImGui::IsKeyPressed(ImGuiKey_O, false)) openDialog();
      if (ctrl && ImGui::IsKeyPressed(ImGuiKey_N, false)) newProject();
      if (ctrl && ImGui::IsKeyPressed(ImGuiKey_I, false)) importDialog();
      if (ctrl && ImGui::IsKeyPressed(ImGuiKey_Z)) shift ? redo() : undo();
      if (ctrl && ImGui::IsKeyPressed(ImGuiKey_Y)) redo();
      ui_->workspace.handleShortcuts(*this);
    }

    ui_->draw(*this);

    if (g_closeRequested) {
      g_closeRequested = false;
      if (dirty) showCloseDialog = true;
      else requestQuit = true;
    }
    if (showCloseDialog) {
      ImGui::OpenPopup("Sair do Lumen");
      showCloseDialog = false;
    }
    if (ImGui::BeginPopupModal("Sair do Lumen", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
      ImGui::Text("Existem alterações não salvas.");
      if (ImGui::Button("Salvar e sair")) {
        if (saveProject(false)) requestQuit = true;
        ImGui::CloseCurrentPopup();
      }
      ImGui::SameLine();
      if (ImGui::Button("Sair sem salvar")) requestQuit = true;
      ImGui::SameLine();
      if (ImGui::Button("Cancelar")) ImGui::CloseCurrentPopup();
      ImGui::EndPopup();
    }

    step("imgui render");
    ImGui::Render();
    int fbw, fbh;
    glfwGetFramebufferSize(window, &fbw, &fbh);
    glViewport(0, 0, fbw, fbh);
    glClearColor(0.08f, 0.08f, 0.09f, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

    ++frame;
    bool lastFrame = opts.frames > 0 && frame >= opts.frames;
    if (lastFrame) {
      // Summary for automated tests.
      std::printf("[test] audio device: %s\n", engine.audio.deviceName().c_str());
      for (auto &l : engine.project.layers)
        if (const LayerState *st = engine.layerState(l.id); st && st->current)
          std::printf("[test] layer %s: t=%.3f audio=%d hw=%s\n", l.name.c_str(), st->current->time(),
                      st->current->hasAudio() ? 1 : 0, st->current->hwaccel().c_str());
      std::fflush(stdout);
      std::vector<unsigned char> px;
      if (!opts.screenshotCanvas.empty()) {
        engine.canvas().readPixels(px);
        writePpm(opts.screenshotCanvas, engine.canvas().width(), engine.canvas().height(), px, true);
      }
      if (!opts.screenshotUi.empty()) {
        px.resize(size_t(fbw) * size_t(fbh) * 4);
        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        glReadPixels(0, 0, fbw, fbh, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
        writePpm(opts.screenshotUi, fbw, fbh, px, true);
      }
      if (!opts.screenshotOutput.empty() && !engine.project.outputs.empty()) {
        Framebuffer fb;
        const OutputConfig &o = engine.project.outputs.front();
        fb.resize(o.winW, o.winH);
        outputs.renderPreview(engine.project, o, 0, engine.canvas().texture(), fb, engine.mesh,
                              engine.time(), engine.blackout);
        fb.readPixels(px);
        writePpm(opts.screenshotOutput, fb.width(), fb.height(), px, true);
      }
    }

    if (!opts.noOutputs) {
      step("outputs sync");
      outputs.sync(engine.project);
      step("outputs render");
      outputs.render(engine.project, engine.canvas().texture(), engine.time(), engine.blackout);
    }
    step("swap");
    glfwSwapBuffers(window);

    commitIfNeeded();
    updateTitle();
    if (lastFrame) break;
  }

  outputs.shutdown();
  engine.shutdown();
  ImGui_ImplOpenGL3_Shutdown();
  ImGui_ImplGlfw_Shutdown();
  ImGui::DestroyContext();
  glfwDestroyWindow(window);
  glfwTerminate();
  return 0;
}

} // namespace lumen
