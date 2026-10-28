#include "engine/outputs.h"

#include <GLFW/glfw3.h>

#ifdef _WIN32
#include <windows.h>
#endif

#include <algorithm>
#include <cmath>

namespace lumen {

static const char *kOutputFS = R"(#version 330 core
in vec2 vUV;              // 0..1 across the output warp (content space)
out vec4 fragColor;
uniform sampler2D uCanvas;
uniform sampler2D uMask;
uniform int uUseMask;
uniform vec4 uRect;       // canvas region, normalised: x y w h (y down)
uniform vec2 uCanvasPx;   // canvas size in pixels
uniform vec4 uSoft;       // left right top bottom (fraction)
uniform float uGamma;     // display gamma of the projector
uniform float uPower;
uniform int uPattern;     // 0 none, 1 card, 2 blend grid, 3 white, 4 black
uniform int uIndex;
uniform vec2 uRes;
uniform float uTime;
uniform float uBlack;     // blackout
uniform vec4 uColorAdj;   // brightness, contrast, gamma, -
uniform vec3 uGain;
uniform vec3 uBlackLevel; // projector black leak (linear)

float ramp(float x, float width) {
  if (width <= 0.0) return 1.0;
  float t = clamp(x / width, 0.0, 1.0);
  // Edge-blend curve: symmetric (c(t) + c(1-t) = 1), then gamma compensation
  // so the *light* of two projectors adds up to exactly one.
  float c = t < 0.5 ? 0.5 * pow(2.0 * t, uPower) : 1.0 - 0.5 * pow(2.0 * (1.0 - t), uPower);
  return pow(c, 1.0 / uGamma);
}

// 1 inside an overlap zone, 0 outside, with a few pixels of feather.
float zone(float x, float width, float px) {
  if (width <= 0.0) return 0.0;
  return 1.0 - smoothstep(width - px, width + px, x);
}

float line(float d, float w) { return 1.0 - smoothstep(0.0, w, abs(d)); }

vec3 accentColor() {
  vec3 hues[6] = vec3[6](vec3(1,0.3,0.3), vec3(0.3,1,0.3), vec3(0.3,0.5,1), vec3(1,1,0.3), vec3(1,0.3,1), vec3(0.3,1,1));
  return hues[uIndex % 6];
}

vec3 testCard(vec2 uv) {
  vec3 accent = accentColor();
  vec2 px = uv * uRes;
  float cell = uRes.y / 10.0;
  vec2 g = mod(px, cell);
  float grid = max(line(g.x, 1.2), line(g.y, 1.2));
  vec3 col = mix(vec3(0.05), accent * 0.8, grid);
  vec2 c = px - uRes * 0.5;
  col = mix(col, vec3(1), line(length(c) - uRes.y * 0.4, 2.0));
  col = mix(col, vec3(1), max(line(c.x, 1.5), line(c.y, 1.5)));
  float b = min(min(px.x, uRes.x - px.x), min(px.y, uRes.y - px.y));
  col = mix(col, accent, 1.0 - smoothstep(6.0, 8.0, b));
  // Output number as a row of dots at the centre.
  for (int i = 0; i <= uIndex; ++i) {
    vec2 d = c - vec2((float(i) - float(uIndex) * 0.5) * 40.0, -uRes.y * 0.18);
    col = mix(col, accent, 1.0 - smoothstep(12.0, 14.0, length(d)));
  }
  return col;
}

// Drawn in *canvas* coordinates, so lines continue across projectors: if the
// projectors are aligned, the grids overlap perfectly in the blend zones.
vec3 blendGrid(vec2 cuv) {
  vec2 p = cuv * uCanvasPx;
  float lw = max(1.0, uCanvasPx.y / 700.0);
  vec2 g = abs(mod(p + 50.0, 100.0) - 50.0);
  vec2 G = abs(mod(p + 250.0, 500.0) - 250.0);
  vec3 col = vec3(0.02) + accentColor() * 0.12; // faint tint shows who lights what
  col = mix(col, vec3(0.55), max(line(g.x, lw), line(g.y, lw)));
  col = mix(col, vec3(1.0), max(line(G.x, lw * 2.0), line(G.y, lw * 2.0)));
  vec2 c = p - uCanvasPx * 0.5;
  col = mix(col, vec3(1.0, 0.85, 0.2), line(length(c) - uCanvasPx.y * 0.45, lw * 2.0));
  float diag1 = (p.x / uCanvasPx.x - p.y / uCanvasPx.y) * uCanvasPx.y;
  float diag2 = (p.x / uCanvasPx.x + p.y / uCanvasPx.y - 1.0) * uCanvasPx.y;
  col = mix(col, vec3(0.3, 0.9, 1.0), max(line(diag1, lw * 1.5), line(diag2, lw * 1.5)));
  return col;
}

void main() {
  vec2 cuv = uRect.xy + vUV * uRect.zw;
  vec3 col;
  if (uPattern == 1) col = testCard(vUV);
  else if (uPattern == 2) col = blendGrid(cuv);
  else if (uPattern == 3) col = vec3(1.0);
  else if (uPattern == 4) col = vec3(0.0);
  else col = texture(uCanvas, vec2(cuv.x, 1.0 - cuv.y)).rgb;

  // Per-projector colour matching.
  col = clamp((col - 0.5) * uColorAdj.y + 0.5, 0.0, 1.0) * uColorAdj.x * uGain;
  col = pow(max(col, vec3(0.0)), vec3(1.0 / max(uColorAdj.z, 0.05)));

  float s = ramp(vUV.x, uSoft.x) * ramp(1.0 - vUV.x, uSoft.y) *
            ramp(vUV.y, uSoft.z) * ramp(1.0 - vUV.y, uSoft.w);
  col *= s * (1.0 - uBlack);

  // Black level compensation: inside an overlap N projectors add their black
  // leak; everywhere else is lifted so the whole image has the same black.
  if (any(greaterThan(uBlackLevel, vec3(0.0)))) {
    // The neighbour's black starts at its physical edge, which is sharp: the
    // lift must switch just as sharply (sub-pixel smoothing only).
    vec2 fpx = 0.5 / uRes;
    float hz = max(zone(vUV.x, uSoft.x, fpx.x), zone(1.0 - vUV.x, uSoft.y, fpx.x));
    float vz = max(zone(vUV.y, uSoft.z, fpx.y), zone(1.0 - vUV.y, uSoft.w, fpx.y));
    float n = (1.0 + hz) * (1.0 + vz);
    float nmax = ((uSoft.x > 0.0 || uSoft.y > 0.0) ? 2.0 : 1.0) * ((uSoft.z > 0.0 || uSoft.w > 0.0) ? 2.0 : 1.0);
    vec3 lift = uBlackLevel * (nmax - n) / n * (1.0 - uBlack);
    col = pow(pow(col, vec3(uGamma)) + lift, vec3(1.0 / uGamma));
  }

  if (uUseMask != 0) col *= texture(uMask, gl_FragCoord.xy / uRes).r;
  fragColor = vec4(col, 1.0);
}
)";

static const char *kSolidFS = R"(#version 330 core
in vec2 vUV;
out vec4 fragColor;
uniform vec4 uColor;
void main() { fragColor = uColor; }
)";

static const char *kCopyFS = R"(#version 330 core
in vec2 vUV;
out vec4 fragColor;
uniform sampler2D uTex;
void main() { fragColor = texture(uTex, vUV); }
)";

std::vector<MonitorInfo> OutputManager::monitors() {
  std::vector<MonitorInfo> out;
  int count = 0;
  GLFWmonitor **mons = glfwGetMonitors(&count);
  for (int i = 0; i < count; ++i) {
    MonitorInfo m;
    m.name = glfwGetMonitorName(mons[i]) ? glfwGetMonitorName(mons[i]) : "Monitor";
    glfwGetMonitorPos(mons[i], &m.x, &m.y);
    if (const GLFWvidmode *mode = glfwGetVideoMode(mons[i])) {
      m.w = mode->width;
      m.h = mode->height;
      m.refresh = mode->refreshRate;
    }
    out.push_back(m);
  }
  return out;
}

#ifdef _WIN32
// DwmFlush paces presentation to the desktop compositor (one vblank) without
// linking dwmapi: every output can then swap without blocking.
static void dwmFlush() {
  using Fn = long(__stdcall *)();
  static Fn fn = [] {
    HMODULE m = LoadLibraryW(L"dwmapi.dll");
    return m ? reinterpret_cast<Fn>(reinterpret_cast<void *>(GetProcAddress(m, "DwmFlush"))) : nullptr;
  }();
  if (fn) fn();
}
#endif

bool OutputManager::init(GLFWwindow *mainWindow) {
  main_ = mainWindow;
  return shader_.build(kPixelVertexShader, kOutputFS) && solid_.build(kPixelVertexShader, kSolidFS) &&
         copy_.build(kPixelVertexShader, kCopyFS) && masks_.init();
}

void OutputManager::identify(double seconds) { identifyUntil_ = glfwGetTime() + seconds; }

bool OutputManager::identifying() const { return glfwGetTime() < identifyUntil_; }

void OutputManager::destroy(Window &w) {
  if (!w.handle) return;
  glfwMakeContextCurrent(w.handle);
  w.mesh.reset(); // VAO belongs to this context
  glfwMakeContextCurrent(main_);
  glfwDestroyWindow(w.handle);
  w.handle = nullptr;
  swapDirty_ = true;
}

void OutputManager::shutdown() {
  for (auto &w : windows_) destroy(*w);
  windows_.clear();
  history_.clear();
  spout_.clear();
}

void OutputManager::pushHistory(const Project &p, const CanvasLookup &canvases) {
  std::map<int, int> maxDelay; // per canvas
  for (auto &o : p.outputs)
    if (o.enabled && o.delayFrames > 0)
      maxDelay[o.canvasId] = std::max(maxDelay[o.canvasId], std::clamp(o.delayFrames, 0, 8));
  for (auto it = history_.begin(); it != history_.end();)
    it = maxDelay.count(it->first) ? std::next(it) : history_.erase(it);
  for (auto &[canvasId, delay] : maxDelay) {
    History &h = history_[canvasId];
    const Texture &canvas = canvases(canvasId);
    size_t n = size_t(delay) + 1;
    while (h.frames.size() < n) h.frames.push_back(std::make_unique<Framebuffer>());
    h.head = (h.head + 1) % int(h.frames.size());
    Framebuffer &fb = *h.frames[size_t(h.head)];
    if (fb.width() != canvas.width() || fb.height() != canvas.height()) fb.resize(canvas.width(), canvas.height());
    fb.bind();
    glDisable(GL_BLEND);
    copy_.use();
    copy_.set("uTarget", float(fb.width()), float(fb.height()));
    copy_.set("uTex", 0);
    canvas.bind(0);
    // Same orientation as the canvas (bottom-up).
    mainMesh_.drawQuad(0, 0, float(fb.width()), float(fb.height()), 0, 1, 1, 0);
    Framebuffer::unbind();
    h.count = std::min(h.count + 1, int(h.frames.size()));
  }
}

const Texture &OutputManager::delayedCanvas(int canvasId, const Texture &canvas, int delay) {
  auto it = history_.find(canvasId);
  if (delay <= 0 || it == history_.end() || it->second.head < 0) return canvas;
  History &h = it->second;
  delay = std::min(delay, h.count - 1);
  int n = int(h.frames.size());
  return h.frames[size_t(((h.head - delay) % n + n) % n)]->texture();
}

std::string OutputManager::spoutStatus(int outputId) const {
  auto it = spoutStatus_.find(outputId);
  return it == spoutStatus_.end() ? std::string() : it->second;
}

void OutputManager::sendSpout(const Project &p, const CanvasLookup &canvases, double time, bool blackout) {
  spoutStatus_.clear();
  for (auto it = spout_.begin(); it != spout_.end();) {
    const OutputConfig *o = p.output(it->first);
    if (!o || !o->enabled || (o->spoutName.empty() && o->ndiName.empty())) it = spout_.erase(it);
    else ++it;
  }
  auto mons = monitors();
  for (size_t i = 0; i < p.outputs.size(); ++i) {
    const OutputConfig &o = p.outputs[i];
    if (!o.enabled || (o.spoutName.empty() && o.ndiName.empty())) continue;
    bool wantSpout = !o.spoutName.empty() && spoutAvailable();
    bool wantNdi = !o.ndiName.empty() && ndiAvailable();
    std::string status;
    if (!o.spoutName.empty() && !spoutAvailable()) status = "Spout só existe no Windows. ";
    if (!o.ndiName.empty() && !ndiAvailable()) status += ndiStatus() + ". ";
    if (!wantSpout && !wantNdi) { spoutStatus_[o.id] = status; continue; }
    int w = std::max(16, o.winW), h = std::max(16, o.winH);
    if (!o.windowless && o.monitor >= 0 && o.monitor < int(mons.size())) {
      w = mons[size_t(o.monitor)].w;
      h = mons[size_t(o.monitor)].h;
    }
    auto &t = spout_[o.id];
    if (!t) t = std::make_unique<SpoutTarget>();
    t->fb.resize(w, h);
    const Texture *mask = outputMask(o, w, h, true, mainMesh_);
    t->fb.bind();
    draw(p, o, int(i), delayedCanvas(o.canvasId, canvases(o.canvasId), o.delayFrames), mask, mainMesh_, w, h,
         time, blackout);
    Framebuffer::unbind();
    if (wantSpout) {
      bool ok = t->out.send(t->fb.texture().id(), w, h, o.spoutName);
      status += ok ? "Spout \"" + o.spoutName + "\" " + std::to_string(w) + "×" + std::to_string(h) + ". " : "erro no Spout. ";
    }
    if (wantNdi) { // NDI wants rows top-down
      t->fb.readPixels(t->px);
      t->flip.resize(t->px.size());
      size_t row = size_t(w) * 4;
      for (int y = 0; y < h; ++y) std::copy_n(&t->px[size_t(h - 1 - y) * row], row, &t->flip[size_t(y) * row]);
      bool ok = t->ndi.send(o.ndiName, t->flip.data(), w, h);
      status += ok ? "NDI \"" + o.ndiName + "\"." : "erro no NDI.";
    }
    spoutStatus_[o.id] = status;
  }
}

void OutputManager::drawControls(const OutputConfig &o, const OutputControls &c, DynamicMesh &mesh,
                                 int w, int h) {
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  solid_.use();
  solid_.set("uTarget", float(w), float(h));
  Warp warp = o.warp;
  for (auto &v : warp.corners) v = {v.x * float(w), v.y * float(h)};
  std::vector<float> v;
  auto line = [&](Vec2 a, Vec2 b, float t) {
    float dx = b.x - a.x, dy = b.y - a.y, len = std::sqrt(dx * dx + dy * dy);
    if (len < 1e-3f) return;
    float nx = -dy / len * t * 0.5f, ny = dx / len * t * 0.5f;
    float q[] = {a.x + nx, a.y + ny, b.x + nx, b.y + ny, b.x - nx, b.y - ny, a.x - nx, a.y - ny};
    int idx[] = {0, 1, 2, 0, 2, 3};
    for (int i : idx) v.insert(v.end(), {q[i * 2], q[i * 2 + 1], 0, 0});
  };
  auto box = [&](Vec2 p, float r, float t) {
    line({p.x - r, p.y - r}, {p.x + r, p.y - r}, t);
    line({p.x + r, p.y - r}, {p.x + r, p.y + r}, t);
    line({p.x + r, p.y + r}, {p.x - r, p.y + r}, t);
    line({p.x - r, p.y + r}, {p.x - r, p.y - r}, t);
  };
  auto flush = [&](float r, float g, float b, float a) {
    if (v.empty()) return;
    solid_.set("uColor", r, g, b, a);
    mesh.draw(v);
    v.clear();
  };
  // Mesh lines (the grid as it lands on the wall).
  int cols = warp.meshCols, rows = warp.meshRows;
  for (int r = 0; r < rows; ++r)
    for (int cc = 0; cc + 1 < cols; ++cc) line(warp.meshPointDest(cc, r), warp.meshPointDest(cc + 1, r), 2);
  for (int cc = 0; cc < cols; ++cc)
    for (int r = 0; r + 1 < rows; ++r) line(warp.meshPointDest(cc, r), warp.meshPointDest(cc, r + 1), 2);
  flush(1.0f, 0.77f, 0.0f, 0.8f);
  for (int i = 0; i < cols * rows; ++i)
    if (warp.hasMesh()) box(warp.meshPointDest(i % cols, i / cols), 5, 2);
  for (auto &cn : warp.corners) box(cn, 14, 3);
  flush(1, 1, 1, 0.9f);
  // Selected handle.
  if (c.corner >= 0 && c.corner < 4) box(warp.corners[size_t(c.corner)], 20, 5);
  else if (c.meshPoint >= 0 && c.meshPoint < cols * rows) box(warp.meshPointDest(c.meshPoint % cols, c.meshPoint / cols), 10, 4);
  flush(1.0f, 0.77f, 0.0f, 1);
  glDisable(GL_BLEND);
}

bool OutputManager::windowFor(int outputId) const {
  return std::any_of(windows_.begin(), windows_.end(),
                     [&](auto &w) { return w->outputId == outputId; });
}

std::vector<GLFWwindow *> OutputManager::handles() const {
  std::vector<GLFWwindow *> out;
  for (auto &w : windows_) out.push_back(w->handle);
  return out;
}

void OutputManager::sync(Project &p) {
  auto mons = monitors();

  // Close windows whose output was removed or disabled.
  for (auto it = windows_.begin(); it != windows_.end();) {
    OutputConfig *o = p.output((*it)->outputId);
    if (!o || !o->enabled || o->windowless) {
      destroy(**it);
      it = windows_.erase(it);
    } else {
      ++it;
    }
  }

  for (auto &o : p.outputs) {
    if (!o.enabled || o.windowless) continue;
    auto it = std::find_if(windows_.begin(), windows_.end(),
                           [&](auto &w) { return w->outputId == o.id; });
    Window *w = it == windows_.end() ? nullptr : it->get();

    bool onMonitor = o.monitor >= 0 && o.monitor < int(mons.size());
    int monitor = onMonitor ? o.monitor : -1;
    int x = onMonitor ? mons[size_t(monitor)].x : o.winX;
    int y = onMonitor ? mons[size_t(monitor)].y : o.winY;
    int ww = onMonitor ? mons[size_t(monitor)].w : std::max(64, o.winW);
    int hh = onMonitor ? mons[size_t(monitor)].h : std::max(64, o.winH);

    // Recreate when switching between monitor and windowed mode (decoration changes).
    if (w && (w->monitor >= 0) != (monitor >= 0)) {
      destroy(*w);
      windows_.erase(it);
      w = nullptr;
    }

    if (!w) {
      glfwDefaultWindowHints();
      glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
      glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
      glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
      glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
      glfwWindowHint(GLFW_DECORATED, monitor >= 0 ? GLFW_FALSE : GLFW_TRUE);
      glfwWindowHint(GLFW_FLOATING, monitor >= 0 ? GLFW_TRUE : GLFW_FALSE);
      glfwWindowHint(GLFW_AUTO_ICONIFY, GLFW_FALSE);
      glfwWindowHint(GLFW_FOCUS_ON_SHOW, GLFW_FALSE);
      // Borderless window covering the monitor: more reliable than exclusive
      // fullscreen with several outputs, and does not minimise on focus loss.
      GLFWwindow *h = glfwCreateWindow(ww, hh, o.name.c_str(), nullptr, main_);
      glfwMakeContextCurrent(main_);
      if (!h) continue;
      glfwSetWindowPos(h, x, y);
      auto nw = std::make_unique<Window>();
      nw->outputId = o.id;
      nw->handle = h;
      nw->mesh = std::make_unique<DynamicMesh>();
      nw->monitor = monitor;
      nw->x = x; nw->y = y; nw->w = ww; nw->h = hh;
      nw->title = o.name;
      windows_.push_back(std::move(nw));
      swapDirty_ = true;
      continue;
    }

    if (w->x != x || w->y != y || w->monitor != monitor) {
      glfwSetWindowPos(w->handle, x, y);
      w->x = x; w->y = y; w->monitor = monitor;
    }
    if (w->w != ww || w->h != hh) {
      glfwSetWindowSize(w->handle, ww, hh);
      w->w = ww; w->h = hh;
    }
    if (w->title != o.name) {
      glfwSetWindowTitle(w->handle, o.name.c_str());
      w->title = o.name;
    }
    // Remember where the user dragged a windowed output.
    if (monitor < 0) {
      int cx, cy, cw, ch;
      glfwGetWindowPos(w->handle, &cx, &cy);
      glfwGetWindowSize(w->handle, &cw, &ch);
      o.winX = w->x = cx; o.winY = w->y = cy;
      if (cw > 0 && ch > 0) { o.winW = w->w = cw; o.winH = w->h = ch; }
    }
  }
}

void OutputManager::updateSwapIntervals() {
  // If every window waited for vsync, each swap would divide the frame rate.
#ifdef _WIN32
  // Windows: no window blocks; render() calls DwmFlush once per frame so all
  // projectors present the same frame on the same vblank.
  glfwMakeContextCurrent(main_);
  glfwSwapInterval(windows_.empty() ? 1 : 0);
  for (auto &w : windows_) {
    glfwMakeContextCurrent(w->handle);
    glfwSwapInterval(0);
  }
#else
  // Elsewhere: the first output paces the loop.
  glfwMakeContextCurrent(main_);
  glfwSwapInterval(windows_.empty() ? 1 : 0);
  for (size_t i = 0; i < windows_.size(); ++i) {
    glfwMakeContextCurrent(windows_[i]->handle);
    glfwSwapInterval(i == 0 ? 1 : 0);
  }
#endif
  glfwMakeContextCurrent(main_);
  swapDirty_ = false;
}

const Texture *OutputManager::outputMask(const OutputConfig &o, int w, int h, bool preview,
                                         DynamicMesh &mesh) {
  if (!MaskRenderer::anyActive(o.masks)) return nullptr;
  auto &map = preview ? previewMasks_ : masks2_;
  auto &fb = map[o.id];
  if (!fb) fb = std::make_unique<Framebuffer>();
  masks_.render(o.masks, float(w), float(h), w, h, *fb, mesh);
  return &fb->texture();
}

void OutputManager::draw(const Project &p, const OutputConfig &o, int index,
                         const Texture &canvas, const Texture *mask, DynamicMesh &mesh, int w,
                         int h, double time, bool blackout) {
  glViewport(0, 0, w, h);
  glClearColor(0, 0, 0, 1);
  glClear(GL_COLOR_BUFFER_BIT);
  glDisable(GL_BLEND);
  shader_.use();
  shader_.set("uTarget", float(w), float(h));
  shader_.set("uCanvas", 0);
  float cw = float(p.canvasWidth(o.canvasId)), ch = float(p.canvasHeight(o.canvasId));
  shader_.set("uRect", o.srcX / cw, o.srcY / ch, o.srcW / cw, o.srcH / ch);
  shader_.set("uSoft", o.softEdge.left, o.softEdge.right, o.softEdge.top, o.softEdge.bottom);
  shader_.set("uGamma", std::max(0.1f, o.softEdge.gamma));
  shader_.set("uPower", std::max(0.1f, o.softEdge.power));
  shader_.set("uPattern", identifying() ? int(TestPattern::Card) : int(o.pattern));
  shader_.set("uIndex", index);
  shader_.set("uRes", float(w), float(h));
  shader_.set("uCanvasPx", cw, ch);
  shader_.set("uTime", float(time));
  shader_.set("uBlack", blackout ? 1.0f : 0.0f);
  shader_.set("uColorAdj", o.color.brightness, o.color.contrast, o.color.gamma, 0.0f);
  shader_.set("uGain", o.color.gain.r, o.color.gain.g, o.color.gain.b);
  shader_.set("uBlackLevel", o.blackLevel.r, o.blackLevel.g, o.blackLevel.b);
  shader_.set("uMask", 1);
  shader_.set("uUseMask", mask ? 1 : 0);
  if (mask) mask->bind(1);
  canvas.bind(0);

  Warp warp = o.warp;
  for (auto &c : warp.corners) c = {c.x * float(w), c.y * float(h)};
  std::vector<float> verts;
  warp.tessellate(warp.hasMesh() ? 48 : 24, 0, 0, 1, 1, verts);
  mesh.draw(verts);
}

void OutputManager::render(const Project &p, const CanvasLookup &canvases, double time, bool blackout,
                           const OutputControls *controls) {
  bool anySpout = std::any_of(p.outputs.begin(), p.outputs.end(),
                              [](const OutputConfig &o) { return o.enabled && (!o.spoutName.empty() || !o.ndiName.empty()); });
  if (windows_.empty() && !anySpout && spout_.empty()) return;
  pushHistory(p, canvases);
  sendSpout(p, canvases, time, blackout);
  if (windows_.empty()) return;
  if (swapDirty_) updateSwapIntervals();

  // The canvas was drawn in the main context: make sure it is complete before
  // other contexts sample it.
  double t0 = glfwGetTime();
  // Output masks are rendered here, in the main context, at each window's size.
  std::vector<const Texture *> masks(windows_.size(), nullptr);
  DynamicMesh &mainMesh = mainMesh_;
  for (size_t k = 0; k < windows_.size(); ++k)
    if (const OutputConfig *o = p.output(windows_[k]->outputId)) {
      int fw, fh;
      glfwGetFramebufferSize(windows_[k]->handle, &fw, &fh);
      if (fw > 0 && fh > 0) masks[k] = outputMask(*o, fw, fh, false, mainMesh);
    }
  GLsync fence = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
  glFlush();

  for (size_t k = 0; k < windows_.size(); ++k) {
    auto &w = windows_[k];
    const OutputConfig *o = nullptr;
    int index = 0;
    for (size_t i = 0; i < p.outputs.size(); ++i)
      if (p.outputs[i].id == w->outputId) { o = &p.outputs[i]; index = int(i); }
    if (!o) continue;
    glfwMakeContextCurrent(w->handle);
    glWaitSync(fence, 0, GL_TIMEOUT_IGNORED);
    int fw, fh;
    glfwGetFramebufferSize(w->handle, &fw, &fh);
    if (fw > 0 && fh > 0) {
      draw(p, *o, index, delayedCanvas(o->canvasId, canvases(o->canvasId), o->delayFrames), masks[k], *w->mesh,
           fw, fh, time, blackout);
      if (controls && controls->outputId == o->id) drawControls(*o, *controls, *w->mesh, fw, fh);
    }
  }
  for (auto &w : windows_) glfwSwapBuffers(w->handle);
#ifdef _WIN32
  dwmFlush();
#endif
  glfwMakeContextCurrent(main_);
  glDeleteSync(fence);
  renderMs_ = float((glfwGetTime() - t0) * 1000.0);
}

void OutputManager::renderPreview(const Project &p, const OutputConfig &o, int index,
                                  const Texture &canvas, Framebuffer &target, DynamicMesh &mesh,
                                  double time, bool blackout) {
  const Texture *mask = outputMask(o, target.width(), target.height(), true, mesh);
  target.bind();
  draw(p, o, index, canvas, mask, mesh, target.width(), target.height(), time, blackout);
  Framebuffer::unbind();
}

} // namespace lumen
