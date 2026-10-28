#include "engine/outputs.h"

#include <GLFW/glfw3.h>

#include <algorithm>
#include <cmath>

namespace lumen {

static const char *kOutputFS = R"(#version 330 core
in vec2 vUV;              // 0..1 across the output warp
out vec4 fragColor;
uniform sampler2D uCanvas;
uniform vec4 uRect;       // canvas region, normalised: x y w h (y down)
uniform vec4 uSoft;       // left right top bottom (fraction)
uniform float uGamma;
uniform float uPower;
uniform int uTestCard;
uniform int uIndex;
uniform vec2 uRes;
uniform float uTime;
uniform float uBlack;

float ramp(float x, float width) {
  if (width <= 0.0) return 1.0;
  float t = clamp(x / width, 0.0, 1.0);
  // Classic edge-blend curve: symmetric power curve, then gamma compensation.
  float c = t < 0.5 ? 0.5 * pow(2.0 * t, uPower) : 1.0 - 0.5 * pow(2.0 * (1.0 - t), uPower);
  return pow(c, 1.0 / uGamma);
}

float line(float d, float w) { return 1.0 - smoothstep(0.0, w, abs(d)); }

vec3 testCard(vec2 uv) {
  vec3 hues[6] = vec3[6](vec3(1,0.3,0.3), vec3(0.3,1,0.3), vec3(0.3,0.5,1), vec3(1,1,0.3), vec3(1,0.3,1), vec3(0.3,1,1));
  vec3 accent = hues[uIndex % 6];
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

void main() {
  vec3 col;
  if (uTestCard != 0) {
    col = testCard(vUV);
  } else {
    vec2 cuv = uRect.xy + vUV * uRect.zw;
    col = texture(uCanvas, vec2(cuv.x, 1.0 - cuv.y)).rgb;
  }
  float s = ramp(vUV.x, uSoft.x) * ramp(1.0 - vUV.x, uSoft.y) *
            ramp(vUV.y, uSoft.z) * ramp(1.0 - vUV.y, uSoft.w);
  fragColor = vec4(col * s * (1.0 - uBlack), 1.0);
}
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

bool OutputManager::init(GLFWwindow *mainWindow) {
  main_ = mainWindow;
  return shader_.build(kPixelVertexShader, kOutputFS);
}

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
}

bool OutputManager::windowFor(int outputId) const {
  return std::any_of(windows_.begin(), windows_.end(),
                     [&](auto &w) { return w->outputId == outputId; });
}

void OutputManager::sync(Project &p) {
  auto mons = monitors();

  // Close windows whose output was removed or disabled.
  for (auto it = windows_.begin(); it != windows_.end();) {
    OutputConfig *o = p.output((*it)->outputId);
    if (!o || !o->enabled) {
      destroy(**it);
      it = windows_.erase(it);
    } else {
      ++it;
    }
  }

  for (auto &o : p.outputs) {
    if (!o.enabled) continue;
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
  // Only one window may block on vsync, otherwise each swap halves the frame rate.
  glfwMakeContextCurrent(main_);
  glfwSwapInterval(windows_.empty() ? 1 : 0);
  for (size_t i = 0; i < windows_.size(); ++i) {
    glfwMakeContextCurrent(windows_[i]->handle);
    glfwSwapInterval(i == 0 ? 1 : 0);
  }
  glfwMakeContextCurrent(main_);
  swapDirty_ = false;
}

void OutputManager::draw(const Project &p, const OutputConfig &o, int index,
                         const Texture &canvas, DynamicMesh &mesh, int w, int h, double time,
                         bool blackout) {
  glViewport(0, 0, w, h);
  glClearColor(0, 0, 0, 1);
  glClear(GL_COLOR_BUFFER_BIT);
  glDisable(GL_BLEND);
  shader_.use();
  shader_.set("uTarget", float(w), float(h));
  shader_.set("uCanvas", 0);
  float cw = float(std::max(1, p.canvasW)), ch = float(std::max(1, p.canvasH));
  shader_.set("uRect", o.srcX / cw, o.srcY / ch, o.srcW / cw, o.srcH / ch);
  shader_.set("uSoft", o.softEdge.left, o.softEdge.right, o.softEdge.top, o.softEdge.bottom);
  shader_.set("uGamma", std::max(0.1f, o.softEdge.gamma));
  shader_.set("uPower", std::max(0.1f, o.softEdge.power));
  shader_.set("uTestCard", o.testCard ? 1 : 0);
  shader_.set("uIndex", index);
  shader_.set("uRes", float(w), float(h));
  shader_.set("uTime", float(time));
  shader_.set("uBlack", blackout ? 1.0f : 0.0f);
  canvas.bind(0);

  Warp warp = o.warp;
  for (auto &c : warp.corners) c = {c.x * float(w), c.y * float(h)};
  std::vector<float> verts;
  warp.tessellate(warp.hasMesh() ? 48 : 24, 0, 0, 1, 1, verts);
  mesh.draw(verts);
}

void OutputManager::render(const Project &p, const Texture &canvas, double time, bool blackout) {
  if (windows_.empty()) return;
  if (swapDirty_) updateSwapIntervals();

  // The canvas was drawn in the main context: make sure it is complete before
  // other contexts sample it.
  GLsync fence = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
  glFlush();

  for (auto &w : windows_) {
    const OutputConfig *o = nullptr;
    int index = 0;
    for (size_t i = 0; i < p.outputs.size(); ++i)
      if (p.outputs[i].id == w->outputId) { o = &p.outputs[i]; index = int(i); }
    if (!o) continue;
    glfwMakeContextCurrent(w->handle);
    glWaitSync(fence, 0, GL_TIMEOUT_IGNORED);
    int fw, fh;
    glfwGetFramebufferSize(w->handle, &fw, &fh);
    if (fw > 0 && fh > 0) draw(p, *o, index, canvas, *w->mesh, fw, fh, time, blackout);
  }
  for (auto &w : windows_) glfwSwapBuffers(w->handle);
  glfwMakeContextCurrent(main_);
  glDeleteSync(fence);
}

void OutputManager::renderPreview(const Project &p, const OutputConfig &o, int index,
                                  const Texture &canvas, Framebuffer &target, DynamicMesh &mesh,
                                  double time, bool blackout) {
  target.bind();
  draw(p, o, index, canvas, mesh, target.width(), target.height(), time, blackout);
  Framebuffer::unbind();
}

} // namespace lumen
