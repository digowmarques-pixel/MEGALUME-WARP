// Output windows (projectors / screens). Each output is a borderless window
// on a monitor (or a normal window for testing) sharing the main GL context.
#pragma once

#include "engine/monitors.h"
#include "core/project.h"
#include "render/gpu.h"
#include "render/stream.h"
#include "io/ndi.h"
#include "io/spout.h"
#include "render/mask.h"

#include <functional>

#include <map>

#include <memory>
#include <string>
#include <vector>

struct GLFWwindow;

namespace lumen {


// Calibration overlay drawn on the real projector ("show controls in output").
struct OutputControls {
  int outputId = -1;
  int corner = -1, meshPoint = -1; // selected handle
};

class OutputManager {
public:
  bool init(GLFWwindow *mainWindow);
  void shutdown();

  // Opens / closes / moves windows so they match the project.
  void sync(Project &p);
  // Draws every output from the canvas texture. Restores the main context.
  using CanvasLookup = std::function<const Texture &(int canvasId)>;
  void render(const Project &p, const CanvasLookup &canvases, double time, bool blackout,
              const OutputControls *controls = nullptr);
  // Draws an output into a framebuffer of the main context (workspace preview).
  void renderPreview(const Project &p, const OutputConfig &o, int outputIndex,
                     const Texture &canvas, Framebuffer &target, DynamicMesh &mesh,
                     double time, bool blackout);

  static std::vector<MonitorInfo> monitors();
  // Outputs without a display are shown inside Lumen (the "Monitores virtuais"
  // window) instead of each opening a window of its own.
  bool virtualMonitors = true;
  static bool onDisplay(const OutputConfig &o, const std::vector<MonitorInfo> &mons) {
    return o.monitor >= 0 && o.monitor < int(mons.size());
  }
  int openCount() const { return int(windows_.size()); }
  // Keys and the close button on an output window (they have no interface of
  // their own): Esc asks for Lumen's window in front, Ctrl+P and the close
  // button (Alt+F4) ask to close the outputs. Read and cleared by the app.
  struct Requests {
    bool raiseMain = false, closeOutputs = false;
  };
  Requests takeRequests() {
    Requests r = requests_;
    requests_ = {};
    return r;
  }
  bool windowFor(int outputId) const;
  std::vector<GLFWwindow *> handles() const;
  // Shows the numbered test card on every output for a few seconds.
  void identify(double seconds = 4.0);
  bool identifying() const;
  float renderMs() const { return renderMs_; } // CPU time of the last output pass

private:
  struct Window {
    int outputId = 0;
    GLFWwindow *handle = nullptr;
    std::unique_ptr<DynamicMesh> mesh;
    int monitor = -1;
    int x = 0, y = 0, w = 0, h = 0;
    bool floating = false; // above every window (only off the screen Lumen's own window is on)
    std::string title;
  };
  Requests requests_;
  static void onKey(GLFWwindow *w, int key, int scancode, int action, int mods);
  static void onClose(GLFWwindow *w);
  void destroy(Window &w);
  void drawControls(const OutputConfig &o, const OutputControls &c, DynamicMesh &mesh, int w, int h);
  // Canvas history for outputs with a frame delay (main-context framebuffers).
  const Texture &delayedCanvas(int canvasId, const Texture &canvas, int delay);
  void pushHistory(const Project &p, const CanvasLookup &canvases);
  // Spout senders of the outputs (rendered off-screen in the main context).
  void sendSpout(const Project &p, const CanvasLookup &canvases, double time, bool blackout);
  void draw(const Project &p, const OutputConfig &o, int index, const Texture &canvas,
            const Texture *mask, DynamicMesh &mesh, int w, int h, double time, bool blackout);
  void updateSwapIntervals();
  // An open output on another screen than Lumen's window (a projector) stays
  // on it: put back at once when Windows minimised or hid it (Win+D, Win+M,
  // "Show desktop"), without taking the keyboard from Lumen's window. One on
  // Lumen's own screen is left alone (it would cover the interface); Ctrl+P
  // or "Reabrir telas" bring it back. An output also stays on its own screen:
  // one the system moved to another screen (its projector switched off before
  // Lumen heard of it) is put back, never over the notebook's screen.
  void keepShowing(const std::vector<MonitorInfo> &mons);
  const Texture *outputMask(const OutputConfig &o, int w, int h, bool preview, DynamicMesh &mesh);

  GLFWwindow *main_ = nullptr;
  Shader shader_, solid_, copy_;
  struct History {
    std::vector<std::unique_ptr<Framebuffer>> frames;
    int head = -1, count = 0;
  };
  std::map<int, History> history_; // by canvas
  struct SpoutTarget {
    Framebuffer fb;
    SpoutOut out;
    NdiSender ndi;
    AsyncReadback readback; // NDI: pixels arrive a frame later, without stalling
    bool ndiOk = true;
    std::vector<unsigned char> px;
  };
  std::map<int, std::unique_ptr<SpoutTarget>> spout_; // by output
public:
  // Status line of an output's Spout sender ("" = off).
  std::string spoutStatus(int outputId) const;
private:
  std::map<int, std::string> spoutStatus_;
  std::vector<std::unique_ptr<Window>> windows_;
  bool swapDirty_ = true;
  MaskRenderer masks_;
  std::map<int, std::unique_ptr<Framebuffer>> masks2_, previewMasks_;
  DynamicMesh mainMesh_; // main-context mesh for mask rendering
  double identifyUntil_ = 0;
  int mainMonitor_ = -1;          // the screen Lumen's window was last seen on (kept while it is minimised)
  double lastRaise_ = 0;          // the outputs on other screens were last put on top
  float renderMs_ = 0;
};

} // namespace lumen
