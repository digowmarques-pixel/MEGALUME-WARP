// Output windows (projectors / screens). Each output is a borderless window
// on a monitor (or a normal window for testing) sharing the main GL context.
#pragma once

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

struct MonitorInfo {
  std::string name;
  int x = 0, y = 0, w = 0, h = 0;
  int refresh = 60;
};

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
    std::string title;
  };
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
  float renderMs_ = 0;
};

} // namespace lumen
