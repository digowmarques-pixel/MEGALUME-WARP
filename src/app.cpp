#include "app.h"

#include "gl/gl.h"
#include "platform/platform.h"
#include "ui/ui.h"
#include "ui/i18n.h"
#include "core/machine_settings.h"
#include "core/md5.h"
#include "media/upload.h"

#include <GLFW/glfw3.h>
#include <imgui.h>
#include <imgui_internal.h> // ClosePopupsExceptModals
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>

#include <cstdio>
#include <fstream>
#include <algorithm>
#include <filesystem>
#include <map>

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

App::App() : ui_(std::make_unique<Ui>()) { loadLanguage(); }
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
  status(T("Novo projeto"));
}

bool App::openProject(const std::string &path) {
  Project p;
  std::string err;
  if (!loadProject(path, p, &err)) {
    status(T("Erro ao abrir: ") + err, true);
    return false;
  }
  engine.setProject(std::move(p), false);
  sel = {};
  if (!engine.project.layers.empty()) sel.layerId = engine.project.layers.front().id;
  ui_->workspace.viewOutputId = -1;
  ui_->workspace.fitRequested = true;
  history_.reset(serializeProject(engine.project, ""));
  dirty = false;
  // Files moved with the project folder: find them next to it.
  int missing = missingMedia();
  if (missing > 0) {
    int fixed = relinkMissing(platform::toUtf8(platform::toPath(path).parent_path()));
    history_.reset(serializeProject(engine.project, ""));
    dirty = fixed > 0;
    status(fixed == missing ? T("Projeto aberto: ") + std::to_string(fixed) + T(" mídia(s) encontradas em outra pasta")
                            : T("Projeto aberto: ") + std::to_string(missing - fixed) + T(" mídia(s) não encontradas (veja a biblioteca)"),
           fixed < missing);
  } else {
    status(T("Projeto aberto: ") + path);
  }
  checkRecovery();
  return true;
}

// ---------------------------------------------------------------- autosave

// One autosave per project (named after its path), so recovering or closing
// one project never throws away another one's; "untitled" for a project that
// was never saved.
static std::string autosaveBase(const std::string &projectPath) {
  std::string key = projectPath.empty() ? std::string("untitled") : md5Hex(projectPath).substr(0, 12);
  return platform::toUtf8(platform::toPath(platform::settingsDir()) / ("autosave-" + key));
}
static std::string autosaveFile(const std::string &projectPath) { return autosaveBase(projectPath) + ".lumen"; }
static std::string autosaveInfoFile(const std::string &projectPath) { return autosaveBase(projectPath) + ".txt"; }

void App::autosave() {
  // Every minute while there are unsaved changes; the project file itself is
  // never touched.
  double now = glfwGetTime();
  if (!dirty || now - lastAutosave_ < 60) return;
  lastAutosave_ = now;
  std::string base = engine.project.filePath.empty() ? std::string()
                                                     : platform::toUtf8(platform::toPath(engine.project.filePath).parent_path());
  const std::string &path = engine.project.filePath;
  if (platform::writeFile(autosaveFile(path), serializeProject(engine.project, base)))
    platform::writeFile(autosaveInfoFile(path), path);
}

void App::clearAutosave(const std::string &projectPath) {
  std::error_code ec;
  std::filesystem::remove(platform::toPath(autosaveFile(projectPath)), ec);
  std::filesystem::remove(platform::toPath(autosaveInfoFile(projectPath)), ec);
}

void App::checkRecovery() {
  std::error_code ec;
  const std::string &path = engine.project.filePath;
  auto file = platform::toPath(autosaveFile(path));
  if (!std::filesystem::exists(file, ec)) return;
  std::string orig;
  platform::readFile(autosaveInfoFile(path), orig);
  // Only for the project that is open now (or an unsaved one), and only if the
  // autosave is newer than the saved file.
  if (orig != path) return;
  if (!orig.empty()) {
    auto a = std::filesystem::last_write_time(file, ec), b = std::filesystem::last_write_time(platform::toPath(orig), ec);
    if (!ec && a <= b) return;
  }
  recoveryFile_ = autosaveFile(path);
  recoveryInfo_ = orig.empty() ? T("um projeto não salvo") : orig;
}

void App::recover(bool yes) {
  if (yes) {
    std::string text;
    Project p;
    std::string base = engine.project.filePath.empty() ? std::string()
                                                       : platform::toUtf8(platform::toPath(engine.project.filePath).parent_path());
    if (platform::readFile(recoveryFile_, text) && deserializeProject(text, base, p)) {
      p.filePath = engine.project.filePath;
      engine.setProject(std::move(p), false);
      history_.reset(serializeProject(engine.project, ""));
      dirty = true;
      status(T("Alterações recuperadas. Salve para mantê-las."));
    }
  } else {
    clearAutosave();
  }
  recoveryFile_.clear();
}

// ---------------------------------------------------------------- missing media

bool App::mediaMissing(const MediaItem &m) const {
  // The library is drawn every frame: check the disk at most every 2 s.
  double now = glfwGetTime();
  if (now - missingCheckedAt_ > 2) {
    missingCache_.clear();
    missingCheckedAt_ = now;
  }
  auto key = std::make_pair(m.id, m.path);
  auto it = missingCache_.find(key);
  if (it != missingCache_.end()) return it->second;
  return missingCache_[key] = checkMissing(m);
}

bool App::checkMissing(const MediaItem &m) {
  std::string path;
  if (m.type == MediaType::Video || m.type == MediaType::Image || m.type == MediaType::Shader) path = m.path;
  else if (m.type == MediaType::Text) path = m.path;
  if (path.empty()) return false;
  std::error_code ec;
  return !std::filesystem::exists(platform::toPath(path), ec);
}

int App::missingMedia() const {
  int n = 0;
  for (auto &m : engine.project.library) n += mediaMissing(m);
  return n;
}

int App::relinkMissing(const std::string &dir) {
  std::map<std::string, std::string> byName; // file name -> first match
  std::error_code ec;
  int scanned = 0;
  for (auto it = std::filesystem::recursive_directory_iterator(platform::toPath(dir), std::filesystem::directory_options::skip_permission_denied, ec);
       !ec && it != std::filesystem::recursive_directory_iterator() && scanned < 200000; it.increment(ec), ++scanned) {
    if (!it->is_regular_file(ec)) continue;
    std::string n = platform::toUtf8(it->path().filename());
    std::transform(n.begin(), n.end(), n.begin(), ::tolower);
    byName.emplace(n, platform::toUtf8(it->path()));
  }
  int fixed = 0;
  for (auto &m : engine.project.library) {
    if (!checkMissing(m)) continue;
    std::string n = platform::toUtf8(platform::toPath(m.path).filename());
    std::transform(n.begin(), n.end(), n.begin(), ::tolower);
    auto f = byName.find(n);
    if (f == byName.end()) continue;
    m.path = f->second;
    engine.cache.invalidate(m.id);
    ++fixed;
  }
  missingCheckedAt_ = -10;
  if (fixed) edited();
  return fixed;
}

void App::applySync(SyncRole role, const std::string &peer, int port, bool save) {
  engine.setSync(role, peer, port);
  if (!save) return;
  setMachineSetting("syncRole", role == SyncRole::Main ? "main" : role == SyncRole::Backup ? "backup" : "");
  setMachineSetting("syncPeer", peer);
  setMachineSetting("syncPort", port);
}

bool App::saveProject(bool saveAs) {
  std::string path = engine.project.filePath, before = path;
  if (saveAs || path.empty()) {
    path = platform::saveFileDialog("Salvar projeto", kProjectFilter, "show.lumen");
    if (path.empty()) {
      if (!platform::hasNativeDialogs()) status(T("Sem diálogo de arquivos neste sistema"), true);
      return false;
    }
    if (platform::toPath(path).extension().empty()) path += ".lumen";
  }
  std::string err;
  if (!lumen::saveProject(engine.project, path, &err)) {
    status(T("Erro ao salvar: ") + err, true);
    return false;
  }
  dirty = false;
  clearAutosave(before); // "save as": the old name's autosave is saved now too
  clearAutosave(path);
  status(T("Salvo: ") + path);
  return true;
}

void App::unlessUnsaved(std::function<void()> action) {
  if (!dirty) {
    action();
    return;
  }
  afterUnsaved_ = std::move(action);
  askUnsaved_ = true;
}

void App::openDialog() {
  auto files = platform::openFileDialog("Abrir projeto", kProjectFilter, false);
  if (!files.empty()) openProject(files[0]);
}

void App::importDialog() {
  auto files = platform::openFileDialog("Importar mídia", kMediaFilter, true);
  if (!files.empty()) importFiles(files);
  else if (!platform::hasNativeDialogs()) status(T("Arraste os arquivos para a janela para importar"), true);
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
      status(T("Formato não suportado: ") + path, true);
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
    status(std::to_string(count) + T(" mídia(s) importada(s)"));
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

// Keys become /key/<name> inputs (1 = pressed, 0 = released) for the
// mappings, from the control window and from the output windows.
void App::pollKeyboard(bool mainWindowKeys) {
  struct KeyName { int key; const char *name; };
  static std::vector<KeyName> keys = [] {
    std::vector<KeyName> k = {
        {GLFW_KEY_SPACE, "space"},     {GLFW_KEY_ENTER, "enter"},   {GLFW_KEY_ESCAPE, "escape"},
        {GLFW_KEY_TAB, "tab"},         {GLFW_KEY_BACKSPACE, "backspace"},
        {GLFW_KEY_LEFT, "left"},       {GLFW_KEY_RIGHT, "right"},   {GLFW_KEY_UP, "up"},
        {GLFW_KEY_DOWN, "down"},       {GLFW_KEY_PAGE_UP, "pageup"}, {GLFW_KEY_PAGE_DOWN, "pagedown"},
        {GLFW_KEY_HOME, "home"},       {GLFW_KEY_END, "end"}};
    static char letters[26][2], digits[10][2], fkeys[12][4], pad[10][5];
    for (int i = 0; i < 26; ++i) { letters[i][0] = char('a' + i); k.push_back({GLFW_KEY_A + i, letters[i]}); }
    for (int i = 0; i < 10; ++i) { digits[i][0] = char('0' + i); k.push_back({GLFW_KEY_0 + i, digits[i]}); }
    for (int i = 0; i < 12; ++i) { std::snprintf(fkeys[i], 4, "f%d", i + 1); k.push_back({GLFW_KEY_F1 + i, fkeys[i]}); }
    for (int i = 0; i < 10; ++i) { std::snprintf(pad[i], 5, "kp%d", i); k.push_back({GLFW_KEY_KP_0 + i, pad[i]}); }
    return k;
  }();
  keyDown_.resize(keys.size(), false);
  std::vector<GLFWwindow *> wins = outputs.handles();
  if (mainWindowKeys) wins.push_back(window);
  bool ctrl = glfwGetKey(window, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS ||
              glfwGetKey(window, GLFW_KEY_RIGHT_CONTROL) == GLFW_PRESS;
  for (size_t i = 0; i < keys.size(); ++i) {
    bool down = false;
    if (!ctrl) // Ctrl+key are the program's shortcuts
      for (GLFWwindow *w : wins) down = down || glfwGetKey(w, keys[i].key) == GLFW_PRESS;
    if (down == keyDown_[i]) continue;
    keyDown_[i] = down;
    engine.injectInput(OscMessage{std::string("/key/") + keys[i].name, {int32_t(down ? 1 : 0)}, "teclado"});
  }
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
  glfwWindowHint(GLFW_MAXIMIZED, opts.frames > 0 || opts.quitAfter > 0 ? GLFW_FALSE : GLFW_TRUE);
  if (opts.diagnose || opts.checkEffects) glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
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

  if (opts.diagnose) {
    std::string report = diagnosticReport();
    std::fputs(report.c_str(), stdout);
    std::string file = platform::toUtf8(platform::toPath(platform::settingsDir()) / "diagnostico.txt");
    if (platform::writeFile(file, report)) {
      std::printf("\nRelatório salvo em %s\n", file.c_str());
      if (opts.frames == 0) platform::openInShell(file); // the console closes when we exit
    }
    std::fflush(stdout);
    return report.find("[FALHA]") == std::string::npos ? 0 : 2;
  }
  if (opts.checkEffects) {
    int bad = 0, n = 0;
    std::vector<IsfCatalog::Entry> list = engine.isfCatalog.builtins();
    if (!opts.checkEffectsDir.empty()) {
      list.clear();
      std::error_code ec;
      for (auto &f : std::filesystem::recursive_directory_iterator(platform::toPath(opts.checkEffectsDir), ec))
        if (f.path().extension() == ".fs")
          list.push_back({platform::toUtf8(f.path()), platform::toUtf8(f.path().filename()), {}, {}});
    }
    for (auto &e : list) {
      IsfRenderer r;
      std::string err;
      ++n;
      if (!r.load(e.path, &err)) {
        ++bad;
        std::printf("[isf] FALHOU %s: %s\n", e.name.c_str(), err.c_str());
        continue;
      }
      // Run every pass once (catches errors that only show at draw time).
      Texture src;
      std::vector<unsigned char> px(64 * 36 * 4, 128);
      src.upload(px.data(), 64, 36);
      IsfValues v;
      isfDefaults(r.info(), v);
      v["progress"] = {0.5f};
      const Texture *t = r.render(64, 36, 1.0, {{"inputImage", &src}, {"startImage", &src}, {"endImage", &src}}, v, engine.mesh);
      std::printf("[isf] ok %s (%s, %zu passes)%s\n", e.name.c_str(), r.info().transition ? "transição" : r.info().filter ? "efeito" : "gerador",
                  r.info().passes.size(), t ? "" : " SEM IMAGEM");
      if (!t) ++bad;
    }
    std::printf("[isf] %d shaders, %d com erro (pasta %s)\n", n, bad,
                opts.checkEffectsDir.empty() ? builtinEffectsDir().c_str() : opts.checkEffectsDir.c_str());
    std::fflush(stdout);
    return bad == 0 && n > 0 ? 0 : 2;
  }
  if (!opts.projectPath.empty()) {
    if (!openProject(opts.projectPath)) {
      newProject();
      checkRecovery();
    }
  } else {
    newProject();
    checkRecovery(); // a crash while working on a project never saved
  }
  if (!engine.project.layers.empty() && sel.layerId < 0) sel.layerId = engine.project.layers.front().id;
  showLocked = opts.showMode;
  engine.forceSyncUpload = opts.syncUpload;
  if (opts.launchColumn > 0) engine.startColumnOnTakeover = opts.launchColumn - 1;
  {
    // Redundancy: machine settings, or the command line for this run only.
    std::string role = opts.role.empty() ? machineSetting("syncRole") : opts.role;
    std::string peer = opts.syncPeer.empty() ? machineSetting("syncPeer") : opts.syncPeer;
    int port = opts.syncPort > 0 ? opts.syncPort : machineSettingInt("syncPort", 7400);
    applySync(role == "main" ? SyncRole::Main : role == "backup" ? SyncRole::Backup : SyncRole::Off, peer, port, false);
  }
  if (!opts.blend.empty()) {
    BlendLayoutParams bp;
    if (std::sscanf(opts.blend.c_str(), "%dx%d:%f:%dx%d", &bp.cols, &bp.rows, &bp.overlapX, &bp.projW,
                    &bp.projH) >= 3) {
      bp.overlapY = bp.overlapX;
      applyBlendLayout(engine.project, bp, -1, true);
      std::printf("[blend] %dx%d projetores, canvas %dx%d\n", bp.cols, bp.rows, engine.project.canvasW,
                  engine.project.canvasH);
    }
  }
  for (auto &o : engine.project.outputs) {
    if (opts.pattern >= 0 && opts.pattern < int(TestPattern::COUNT)) o.pattern = TestPattern(opts.pattern);
    if (opts.blackLevel >= 0) o.blackLevel = {opts.blackLevel, opts.blackLevel, opts.blackLevel, 1};
  }
  // Installation start-up (project settings), then the command line.
  outputsOpen = opts.openOutputs || engine.project.startOutputs;
  if (opts.outputControls && !engine.project.outputs.empty()) {
    showOutputControls = true;
    sel.outputId = engine.project.outputs.front().id;
  }
  if (opts.launchColumn > 0) engine.launchColumn(opts.launchColumn - 1);
  else if (engine.project.startColumn > 0) engine.launchColumn(engine.project.startColumn - 1);

  double last = glfwGetTime(), fpsTimer = last, started = last;
  bool audioStallDone = false;
  if (!opts.testAudioRecord.empty()) engine.audio.debugRecord(opts.quitAfter > 0 ? opts.quitAfter + 1 : 30);
  int frame = 0, fpsFrames = 0;

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
      if (showLocked) {
        status(T("Modo show: soltar arquivos está desativado (Ctrl+Shift+L destrava)"), true);
      } else if (files.size() == 1 && platform::toPath(files[0]).extension() == ".lumen") {
        std::string path = files[0];
        unlessUnsaved([this, path] { openProject(path); });
      } else {
        importFiles(files);
      }
    }

    // Engine: playback, OSC, then compose the canvas.
    // The first frames log each stage: if something crashes at startup, the
    // last line of lumen.log says where.
    auto step = [&](const char *what) {
      if (frame < 3) std::printf("[frame %d] %s\n", frame, what);
    };
    step("update");
    engine.selectedLayerId = sel.layerId;
    engine.update(dt);
    if (engine.selectedLayerId != sel.layerId && engine.project.layer(engine.selectedLayerId)) {
      sel.layerId = engine.selectedLayerId; // /action/selectLayer
      sel.slice = 0;
    }
    if (engine.requestOutputs >= 0) {
      outputsOpen = engine.requestOutputs == 1;
      engine.requestOutputs = -1;
    }
    step("render");
    engine.render();
    step("ui");

    // UI.
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
    // No Tab focus: in a show, a stray Tab would put the cursor in a text field,
    // where Space types a space instead of giving GO and Delete erases the name.
    ImGui::PushItemFlag(ImGuiItemFlags_NoTabStop, true);

    ImGuiIO &io = ImGui::GetIO();
    if (!io.WantTextInput) {
      bool ctrl = io.KeyCtrl, shift = io.KeyShift;
      if (ctrl && shift && ImGui::IsKeyPressed(ImGuiKey_L, false)) {
        showLocked = !showLocked;
        status(showLocked ? T("Modo show: edição travada (Ctrl+Shift+L destrava)") : T("Edição liberada"));
      }
      if (!ctrl && ImGui::IsKeyPressed(ImGuiKey_Space, false)) shift ? engine.back() : engine.go();
      // Esc closes an open menu (ImGui does it only with keyboard navigation
      // on), and then it must not also stop the show.
      if (!ctrl && ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
        if (popupWasOpen_) ImGui::ClosePopupsExceptModals();
        else engine.stopAll();
      }
      if (!ctrl && ImGui::IsKeyPressed(ImGuiKey_B, false)) engine.blackout = !engine.blackout;
      if (ctrl && ImGui::IsKeyPressed(ImGuiKey_S, false)) saveProject(shift);
      if (!showLocked && ctrl && ImGui::IsKeyPressed(ImGuiKey_O, false)) unlessUnsaved([this] { openDialog(); });
      if (!showLocked && ctrl && ImGui::IsKeyPressed(ImGuiKey_N, false)) unlessUnsaved([this] { newProject(); });
      if (!showLocked && ctrl && ImGui::IsKeyPressed(ImGuiKey_I, false)) importDialog();
      if (ctrl && !shift && ImGui::IsKeyPressed(ImGuiKey_P, false)) outputsOpen = !outputsOpen;
      if (ctrl && shift && ImGui::IsKeyPressed(ImGuiKey_P, false)) ui_->openOutputs();
      if (ctrl && shift && ImGui::IsKeyPressed(ImGuiKey_D, false)) showOutputControls = !showOutputControls;
      if (!showLocked) {
        if (ctrl && ImGui::IsKeyPressed(ImGuiKey_Z)) shift ? redo() : undo();
        if (ctrl && ImGui::IsKeyPressed(ImGuiKey_Y)) redo();
        ui_->workspace.handleShortcuts(*this);
      }
    }
    if (engine.project.keyboardInput) pollKeyboard(!io.WantTextInput);

    ui_->draw(*this);
    // Editing a slice bound to a surface edits the surface (and its other layers).
    engine.project.pushSurfaceEdits();

    if (g_closeRequested) {
      g_closeRequested = false;
      unlessUnsaved([this] { requestQuit = true; });
    }
    if (askUnsaved_) {
      ImGui::OpenPopup("###unsaved");
      askUnsaved_ = false;
    }
    std::string unsavedTitle = std::string(T("Alterações não salvas")) + "###unsaved";
    if (ImGui::BeginPopupModal(unsavedTitle.c_str(), nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
      ImGui::TextUnformatted(T("Existem alterações não salvas."));
      auto run = [this] {
        auto action = std::move(afterUnsaved_);
        afterUnsaved_ = nullptr;
        if (action) action();
      };
      if (ImGui::Button(T("Salvar"))) {
        if (saveProject(false)) run();
        ImGui::CloseCurrentPopup();
      }
      ImGui::SameLine();
      if (ImGui::Button(T("Descartar"))) {
        run();
        ImGui::CloseCurrentPopup();
      }
      ImGui::SameLine();
      if (ImGui::Button(T("Cancelar"))) {
        afterUnsaved_ = nullptr;
        ImGui::CloseCurrentPopup();
      }
      ImGui::EndPopup();
    }

    ImGui::PopItemFlag();
    popupWasOpen_ = ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel);
    step("imgui render");
    ImGui::Render();
    int fbw, fbh;
    glfwGetFramebufferSize(window, &fbw, &fbh);
    glViewport(0, 0, fbw, fbh);
    glClearColor(0.03f, 0.03f, 0.035f, 1); // gaps between the panels
    glClear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

    ++frame;
    if (opts.testAudioStall > 0 && !audioStallDone && glfwGetTime() - started >= opts.testAudioStall) {
      audioStallDone = true;
      engine.audio.debugStall(); // as if the interface were unplugged
    }
    bool lastFrame = (opts.frames > 0 && frame >= opts.frames) || (opts.quitAfter > 0 && glfwGetTime() - started >= opts.quitAfter);
    if (lastFrame) {
      // Summary for automated tests.
      std::printf("[test] audio device: %s\n", engine.audio.deviceName().c_str());
      if (!opts.testAudioRecord.empty() && engine.audio.saveRecording(opts.testAudioRecord))
        std::printf("[test] audio recorded: %s\n", opts.testAudioRecord.c_str());
      std::printf("[test] column=%d\n", engine.activeColumn() + 1);
      if (recoveryPending()) std::printf("[test] recovery offered: %s\n", recoveryInfo().c_str());
      if (engine.syncRole() != SyncRole::Off)
        std::printf("[test] sync: %s | %s\n", engine.syncStatus().c_str(), engine.syncPeerStatus().c_str());
      for (auto &line : engine.oscLog)
        if (!line.empty() && line[0] == '[') std::printf("[test] log %s\n", line.c_str());
      for (auto &l : engine.project.layers)
        if (const auto *bl = engine.blobs(l.id)) {
          std::printf("[test] blobs %s: %zu", l.name.c_str(), bl->size());
          for (auto &b : *bl) std::printf("  #%d (%.2f, %.2f) %.3f", b.id, b.x, b.y, b.area);
          std::printf("\n");
        }
      for (auto &l : engine.project.layers)
        std::printf("[test] layer %s: opacity=%.3f scale=%.3f x=%.1f contrast=%.3f playing=%d\n", l.name.c_str(), l.opacity,
                    l.transform.scale, l.transform.position.x, l.fx.contrast,
                    engine.layerState(l.id) && engine.layerState(l.id)->current ? 1 : 0);
      for (auto &l : engine.project.layers)
        if (const LayerState *st = engine.layerState(l.id); st && st->current)
          std::printf("[test] layer %s: t=%.3f fade=%.2f audio=%d%s hw=%s\n", l.name.c_str(), st->current->time(), st->fade,
                      st->current->hasAudio() ? 1 : 0, st->current->audioPacing() ? " (paces)" : "",
                      st->current->hwaccel().c_str());
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
      if (!opts.screenshotOutputs.empty()) {
        for (size_t i = 0; i < engine.project.outputs.size(); ++i) {
          const OutputConfig &o = engine.project.outputs[i];
          Framebuffer fb;
          fb.resize(int(o.srcW), int(o.srcH));
          outputs.renderPreview(engine.project, o, int(i), engine.canvasTexture(o.canvasId), fb, engine.mesh,
                                engine.time(), engine.blackout);
          fb.readPixels(px);
          writePpm(opts.screenshotOutputs + std::to_string(i + 1) + ".ppm", fb.width(), fb.height(), px, true);
        }
      }
      if (!opts.screenshotOutput.empty() && !engine.project.outputs.empty()) {
        Framebuffer fb;
        const OutputConfig &o = engine.project.outputs.front();
        fb.resize(o.winW, o.winH);
        outputs.renderPreview(engine.project, o, 0, engine.canvasTexture(o.canvasId), fb, engine.mesh,
                              engine.time(), engine.blackout);
        fb.readPixels(px);
        writePpm(opts.screenshotOutput, fb.width(), fb.height(), px, true);
      }
    }

    if (!opts.noOutputs && outputsOpen) {
      step("outputs sync");
      outputs.sync(engine.project);
      step("outputs render");
      OutputControls ctl;
      if (showOutputControls) {
        ctl.outputId = sel.outputId >= 0 ? sel.outputId : ui_->editedOutput();
        ctl.corner = sel.corner;
        ctl.meshPoint = sel.meshPoint;
      }
      outputs.render(engine.project, [this](int id) -> const Texture & { return engine.canvasTexture(id); }, engine.time(), engine.blackout, &ctl);
    } else if (outputs.openCount() > 0) {
      outputs.shutdown(); // closed by the user
    }
    step("swap");
    glfwSwapBuffers(window);

    commitIfNeeded();
    if (opts.frames == 0 && opts.quitAfter <= 0) autosave();
    updateTitle();
    if (lastFrame) break;
  }

  clearAutosave(); // only a crash leaves something to recover
  outputs.shutdown();
  engine.shutdown();
  // GPU objects must die while the context is alive (before glfwTerminate).
  ui_.reset();
  outputsPtr_.reset();
  enginePtr_.reset();
  FrameUploader::releaseShared();
  ImGui_ImplOpenGL3_Shutdown();
  ImGui_ImplGlfw_Shutdown();
  ImGui::DestroyContext();
  glfwDestroyWindow(window);
  glfwTerminate();
  return 0;
}

} // namespace lumen
